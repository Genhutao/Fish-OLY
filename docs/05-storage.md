# 05 · 事件溯源与存储

log-first 三层架构。设计原则：**事件流是唯一真相，其余皆可重建投影**；PG 永远不在写关键路径上。

## 1. 三层存储

```
第 0 层  内存投影        服务真相：读全部走内存（消息环、成员表、presence、身份投影）
第 1 层  本地段文件      持久真相：append-only 段 + 批量组提交 fsync
第 2 层  PG / 对象存储   归档与查询层：异步分层（秒–分钟滞后），历史查询、跨分片检索、
                         运维/分析、长期记忆归档；不参与恢复路径
```

## 2. 段文件格式

```
stream/{kind}/{stream_id}/seg-{seq基}.log     —— 每流一个段目录
  段 = 顺序追加的批次序列
  批次 = [count][batch_seq][records...][crc32c]
  记录 = EventEnvelope 规范二进制（payload_bin + 全部包络字段）
  段头 = magic + schema_version + 流清单 + 起始 seq
  滚动：128MB / 段；段不可变，冷段整体归档对象存储
```

- 每记录续接所在流的哈希链（prev_hash → hash）；批内 CRC；段尾校验和；
- 段不可变 → 归档 = 移动文件；截断以快照为锚（默认不启用，全量保留）。

## 3. 写路径：组提交 writer 协程

纪律：**先持久、后可见**——commit 前不发布、不应答客户端（否则重放与用户所见分叉）。

```
loop 的 writer 协程攒批：满 N 条 / 超时 T（默认 5ms）触发
  → 顺序追加段文件（O(1)），组提交 fdatasync
     （各 loop 的批次可汇入同一提交窗口；NVMe 单次 fsync ~0.5–2ms）
  → 完成游标推进 → 驱动 WS 发布与 REST 应答
```

- writer 协程严格按序处理各批，**完成游标**驱动可见性；
- 一拍内 drain 的多条命令合并为一个批次 = 天然组提交；
- 每秒数百批 × 每批数千事件 = 每盒百万级事件/秒；
- 该 writer 是将来多节点 outbox→总线复制的换型位点（§7）。

## 4. 幂等与恰一次准入

- `command_id` 由发起方生成（用户客户端 request_id；角色命令由决策事件派生）；
- 准入查重：已存在 → 返回已记录结果（含 denial）；
- REST 重试 / WS 重连补发 = 安全的 exactly-once 准入、at-least-once 投递。

## 5. 快照协议

```
快照清单（每流，每 N seq 或 M 秒，后台协程异步拍）：
  {stream, seq, blob_hash, 投影版本}
  内容：消息环、成员表、presence/WorkingSet、tick 计数、待发游标
  序列化：自有版本化二进制格式 → blobs 存储
  落后于 head 永远合法（重放补缺口）
```

- **RNG 不进快照**：打分噪声种子 = hash(stream_id, tick)，任意时刻可重导出；
- 快照异步拍，不在写关键路径。

## 6. 启动 / 迁移 / 恢复

```
启动：读进程流尾部取分片指派
  → 每活跃会话：最近快照 → 载入投影 → 从 seq+1 重放段补到头
  → 无快照 → 全量重放（确定性 fold 事实事件）
会话迁移（换 loop/换节点）= 同一份代码，仅触发者不同
NVMe 顺序读：几十 GB 日志秒级重放
```

### 故障恢复语义矩阵

| 故障点 | 语义 |
|---|---|
| 批次写一半崩溃 | 批内 CRC 失败 → 丢弃残批，从上一完整批恢复（哈希链保证边界）；流内 seq 无空洞由重放重建 |
| fsync 后、发布前崩溃 | 订阅者缺事件 → `since` 游标重连补齐 |
| 进程崩溃 | 该 loop 会话在任意 loop 按本节流程重建；内存中不存在不可重构状态 |
| 节点丢失 | primary-backup 段复制（M2+，§7）；接管 = follower 提升 + 本地段重放；接管窗口 = 复制滞后（亚秒） |
| 磁盘满/存储故障 | 停止准入（写路径错误，客户端幂等重试）；读继续走内存投影（标记陈旧）；**永不 ack 未持久** |

## 7. 多节点路线（分阶段）

- **shard 是唯一水平扩展单元**：shard = 一组会话流 + 身份流 + 其段文件，绑一个 loop；迁移协议即负载均衡协议；
- **复制**：每 shard 一个 follower 节点；writer 批次同步推 follower 追加段（异步 fsync，primary-backup，无共识选举）；M2 前实现，单盒阶段仅本地段 + 快照；
- 跨节点请求 = 现有 channel 往返换 RPC 传输层，业务协程不变。

## 8. PostgreSQL 归档层

### 8.1 角色边界

- **只做**：历史消息/事件检索（跨分片）、运维与分析查询、长期记忆归档 + 向量检索、blob 副本（冷备）、审计导出；
- **不做**：写关键路径、恢复路径、运行时唯一真相。

### 8.2 表草案

```sql
-- 事件镜像（tierer 异步写入；流内 seq 与段文件一致）
CREATE TABLE events (
  stream_kind smallint NOT NULL,     -- 1=session 2=identity 3=process
  stream_id   uuid     NOT NULL,     -- ULID 存 16 字节
  seq         bigint   NOT NULL,
  tick        bigint,
  ts          timestamptz NOT NULL,
  kind        smallint NOT NULL,     -- 1=command 2=fact 3=denied 4=notification
  command_id  uuid,  causes uuid[] NOT NULL DEFAULT '{}',
  actor uuid, on_behalf_of uuid,
  type text NOT NULL, schema_ver smallint NOT NULL,
  payload_bin bytea NOT NULL, payload_json jsonb,
  prev_hash bytea NOT NULL, hash bytea NOT NULL,
  PRIMARY KEY (stream_kind, stream_id, seq)
) PARTITION BY LIST (stream_kind);
CREATE UNIQUE INDEX ON events (command_id) WHERE command_id IS NOT NULL;

CREATE TABLE blobs (                 -- 内容寻址（卡片体/canon/资料/配置/快照）
  content_hash bytea PRIMARY KEY, size bigint, kind smallint, data bytea NOT NULL
);

CREATE TABLE memory_entries (        -- 长期记忆 + 向量（归档与全库检索）
  id uuid PRIMARY KEY,
  owner uuid NOT NULL, persona uuid NOT NULL,
  source_stream uuid, source_seq bigint, sources uuid[],
  card_version int, kind smallint, content text,
  importance real, valid_from timestamptz, valid_to timestamptz,
  review_state smallint NOT NULL DEFAULT 0, generation int NOT NULL,
  embedding vector(1536)
);
CREATE INDEX ON memory_entries USING hnsw (embedding vector_cosine_ops);
CREATE INDEX ON memory_entries (owner, persona);
```

- 实体元数据表（personas/sessions/members/relationships/grants…）在 PG 中是**归档镜像**（tierer 从投影同步），供运维查询；服务真相在内存投影；
- 记忆的**在线召回**走身份分片上的进程内 usearch；PG 向量用于全库分析与非在线查询。

### 8.3 tierer

- 每 loop 的 writer 完成游标之后，tierer 协程把批次异步转投 PG（批量 COPY）+ 冷段上传对象存储；
- 滞后秒–分钟级；监控暴露 tier lag；PG 不可用只影响归档与历史深查询，不影响在线服务。

## 9. 容量预算

| 项 | 数字 |
|---|---|
| 事件均值 | ~1KB/条 |
| 写入 | 百万级事件/秒 ≈ 1GB/s 峰值（组提交 fsync 支撑） |
| 会话热态 | <10KB/会话（热消息环 4–8KB + WorkingSet ~1KB + 游标） |
| 20 万会话内存 | ≈2GB + 协议缓冲池 |
| 向量 | 百万条 × 1536 维 ≈ 6GB（分片进程内） |
| 段保留 | 全量保留；冷段归档对象存储 |

## 10. 换型点清单

| 压力 | 换型 | 动哪里 |
|---|---|---|
| 多节点 | 段复制 → 总线（outbox→NATS 形态） | writer 协程背后一层 |
| 向量至千万级 | usearch → 专用引擎 | 检索接口后移 |
| 大文件（语音等） | blobs → 对象存储直挂 | blob 接口后移 |
| 历史查询规模化 | PG → 列存/专用检索 | tierer 目标与查询接口 |
| 多租户合规 | 行级安全/独立 schema | 表已带 owner/stream 维度 |
