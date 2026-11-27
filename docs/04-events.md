# 04 · 事件模型

一切状态变更先成事件；事件流是唯一真相；其余皆可重建投影。本文件定义命令/事实两域、事件包络、三种流与准入协议。

## 1. 两域：命令事件与事实事件

```cpp
// ── 输入域：会话/身份队列里的东西。统一包裹，来源不同、地位相同 ──
enum class CommandSource { User, Persona, Director, System };

struct CommandEvent {
    CommandId   id;                 // 全局唯一；发起方生成（幂等键）
    PrincipalId actor;              // User | Persona | Director | System
    PrincipalId on_behalf_of;       // 链根用户（角色行动时；计费与审计归账）
    Action      action;             // 见 §2 动作清单
    Payload     payload;            // 规范二进制 + 派生 JSON（05 §2）
    vector<EventId> causality;      // 决策依据（角色命令必带）
    SourceMeta  source;             // 来源细节（如决策事件引用）
};

// ── 效果域：命令执行产生的不可变状态变更，只追加 ──
// message.appended / member.added / role.changed / session.forked
// / session.rolled_back / session.migrated / persona.spoke / persona.silenced
// / decision.recorded / memory.updated / cost.recorded / card.edited / ...
```

**统一规则**：用户一条请求可展开为 1..n 条命令事件（入口适配器负责）；角色一次决策的 tool_calls 展开为 n 条命令事件（每条 causality 指向 decision.recorded）；导演注入不伪装成角色发言（source=Director）。

用户命令与工具调用**不是**统一为"工具"，而是统一为**命令事件**——同一包裹、同一准入、同一审计；工具层只是"角色决策协程把 LLM tool_calls 翻译成命令事件的适配器"，与用户入口适配器对等。

## 2. 动作清单（M0–M2 范围）

| 域 | 动作 |
|---|---|
| 会话域 | message.append / message.react / **message.recall**（撤回=标记，不删除） / **read.advance**（读游标推进） / member.add / member.remove / role.set / session.transfer / session.archive / session.fork / session.migrate / meta.set（名称/公告） |
| 身份域 | card.edit / card.propose / card.approve / share.offer / share.accept / binding.set / grant.set / relationship.set / quota.allocate / config.set / memory.review |
| 系统域 | tick / gateway.reply / tiering.checkpoint / director.pause / director.step / director.inject / director.rollback |
| 扩展 | skill.install / skill.equip / plugin.call（M2） |

## 3. 事件包络（完整规格）

```cpp
struct EventEnvelope {
    StreamRef   stream;             // {kind: Session|Identity|Process, id}
    uint64      seq;                // 流内无空洞、单调 —— 订阅续传游标
    uint64      tick;               // 仅会话流：虚拟时钟位；因果排序靠 tick+seq
    WallTime    ts;                 // 信息性
    EventKind   kind;               // CommandAccepted | Fact | Denied | Notification
    CommandId   command;            // Fact/Denied 必带
    vector<EventId> causes;         // 因果父事件（跨流用全局 EventId）
    PrincipalId actor, on_behalf_of;
    TypeRef     schema;             // {type, schema_version}
    Bytes       payload_bin;        // 规范二进制（哈希链算它）
    Json        payload_json;       // 派生列：运维/调试
    Hash        prev_hash, hash;    // 流内哈希链：hash = H(prev_hash ‖ 规范字节)
};
```

要点：

- **无空洞 seq**：客户端续传、投影游标、订阅读复位全部以 `(stream, seq)` 为锚；
- **流开篇**：每条流以 `*.created` 事实为 seq 0（会话流 `session.created`、身份流 `principal.created`、进程流 `process.bootstrapped`），首事件 `prev_hash` = 创世常量；
- **哈希链**：建立在跨版本稳定的规范编码上（`payload_bin`，自带编码版本号）；jsonb/JSON 派生列不参与哈希；
- **Schema 演化**：旧段永不重写；读取端注册 upcaster `(type, version) → 当前版`；写入端只写当前版；
- **tick 只在会话流**；身份/进程流只有 seq + 墙钟。

## 4. 三种流

| 流 | 每流一条 | 内容 | 时钟 |
|---|---|---|---|
| Session | 每会话 | 聊天、成员、自治、导演、迁移 | tick + seq |
| Identity | 每主体（分片聚合存储） | 卡片/分享/授权/配额/审阅/资料 | seq |
| Process | 每进程（集群：每节点） | 路由表、分片指派、插件注册、平台默认配置发布 | seq |

跨流联动只用**事件引用**（如会话流 `persona.card_changed` 引用身份流 `card.edited` 的 EventId），不共享可变状态。

## 5. 准入协议（唯一写入口）

```
入口适配器（REST/WS/内部决策）→ 展开为命令事件 → 有界队列
dequeue（loop/分片上，对当前状态检查，单写者无竞态）：
  1. 幂等：command_id 已存在 → 返回已记录结果（含 denial），不执行
  2. 权限：check(actor, action, object)（02 §6）
  3. 配额：速率桶（租约本地桶）+ 实体计数（09）
  4. 包络钳制：配置类命令对平台包络校验（08）
  通过 → 执行 → 产生 1..n 条事实事件
  拒绝 → 写 denial 事件（可审计），不影响生效流
```

## 6. 日志批次（原子单元）

```
日志批次 = { 本拍接受的 CommandEvents, 产生的 FactEvents, denial 事件 }
  —— 一次原子落盘（05 §3）；批内计算哈希链；先持久后可见（发布/应答在 commit 之后）
```

## 7. 因果与查询

- `causes` 构成 DAG；"哪句话导致这次卡片变更" = 沿 `command_id` / `causes` 走链；
- 全局时序：同流内 seq 全序；跨流用 causality DAG + ULID 时间前缀近似；
- 解释查询（行动解释）= 沿链聚合 decision.recorded / attention / 引用记忆 / 配置版本（10 §6）。
