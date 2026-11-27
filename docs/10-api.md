# 10 · API 契约

客户端（桌面/移动/Web/第三方）唯一依赖面。原则：**REST 走命令与查询，WS 走事件流**；契约先于实现冻结，以 OpenAPI 3.1 + AsyncAPI 2.6 文档为单一事实，CI 契约测试守护。

## 1. 总则

| 项 | 规则 |
|---|---|
| 版本 | 路径前缀 `/v1`；破坏性变更升 `/v2` 并行运行一个周期 |
| 传输 | HTTPS；WS over TLS（`wss`） |
| 鉴权 | `Authorization: Bearer <token>`；token 绑定用户主体。用户代角色操作走资源的权限链（`on_behalf_of` 由引擎解析） |
| 幂等 | 所有写请求带 `Idempotency-Key` 头（客户端 ULID）；重放返回已记录结果（含错误），恰一次准入 |
| 时间 | ISO 8601 UTC |
| ID | ULID 字符串（26 位 Crockford Base32），事件游标另见 §4 |
| 错误 | 统一错误包（§3） |
| 分页 | keyset 游标：`?cursor=&limit=`，响应含 `next_cursor`；事件类资源以 `(seq)` 为游标 |
| 限流 | 响应头 `X-RateLimit-*`；超限 429 + 错误包（§4） |
| 排序 | 默认 `seq`/时间升序；`order=desc` 显式声明 |

## 2. 错误包

```json
{
  "error": {
    "code": "quota_exhausted",            // 机器可读，枚举见 §4
    "message": "人类可读描述",
    "details": { },                        // 按码定义的附加字段
    "trace_id": "…",                       // 与服务端日志/审计链关联
    "command_id": "…"                      // 写请求幂等重放时回带
  }
}
```

常见 `code`：`unauthorized / forbidden / not_found / validation_failed / rate_limited / quota_exhausted / permission_denied / canon_locked / idempotent_replayed / server_overloaded`。

## 3. 配额错误形状（P0 交互硬失败）

```json
{
  "error": {
    "code": "quota_exhausted",
    "details": {
      "bucket": "tokens.day.strong",     // 哪个桶（见 09）
      "scope": "principal:01H…/child:01H…",
      "reset_at": "2026-11-29T00:00:00Z",
      "retry_after": 3600
    }
  }
}
```

客户端据此渲染引导（等待/升级/切换档位）。`rate_limited` 同构（桶=突发桶，retry_after 秒级）。

## 4. 资源与端点

### 4.1 认证与主体

```
POST /v1/auth/tokens          换取 token（M0：账号密码/OAuth 外接）
GET  /v1/me                   当前主体摘要 + 配额余量摘要
```

### 4.2 角色（实体 + 卡片版本链）

```
GET    /v1/me/personas                       我的角色列表（含绑定/最近互动摘要）
GET    /v1/me/relationships                  我持有/被分享的角色关系
POST   /v1/personas                          创建（含初始卡片体）
GET    /v1/personas/{id}                     元数据（不含体；含可见性/我的绑定）
GET    /v1/personas/{id}/versions            版本列表（游标分页）
GET    /v1/personas/{id}/versions/{seq}      取卡片体（支持 ?by_hash=）
PUT    /v1/personas/{id}/card                提交新版本（需 card.edit 权限）
POST   /v1/personas/{id}/proposals           card.propose（提案-审批流）
POST   /v1/proposals/{id}/decision           approve | reject（创建者）
POST   /v1/personas/{id}:share               visibility 变更 + share.offered
POST   /v1/personas/{id}:offer               创建者推送新版本给持有者
PUT    /v1/relationships/{persona_id}        持有者装订：{mode: follow|pin, version?}
GET    /v1/personas/{id}/grants              授权边列表
PUT    /v1/personas/{id}/grants              变更授权边（grant.set）
DELETE /v1/personas/{id}                     墓碑（零活跃关系时；否则 409 + 依赖清单）
```

### 4.3 会话与成员

```
POST /v1/sessions                       创建 {canon_blob | canon_hash, members[], settings}
GET  /v1/me/sessions                    我的会话列表：last_seq/read_seq/unread/最后消息摘要/导演状态
GET  /v1/sessions/{id}                  元数据 + 我的成员视图（角色/绑定/presence 摘要/导演状态）
PATCH /v1/sessions/{id}                 元数据（名称/公告——非 canon）
POST /v1/sessions/{id}/members          邀请 {principal_id, binding?}
DELETE /v1/sessions/{id}/members/{pid}  移除/退出
PUT  /v1/sessions/{id}/members/{pid}/role   role.set
POST /v1/sessions/{id}:transfer         转让 {to}
POST /v1/sessions/{id}:archive          归档（只读冻结）
POST /v1/sessions/{id}:fork             分叉 {offset_seq, canon_override?}
POST /v1/sessions/{id}:flatten          物化祖先前缀（发布用）
PUT  /v1/sessions/{id}/read             读游标推进 {seq}（read.advance，未读随之清零）
```

### 4.4 消息与历史

```
POST /v1/sessions/{id}/messages     {kind: text|sticker|…, content, meta?, reply_to?, mentions[]}
                                    → 202 {command_id}（事件确认经 WS 或 §4.10 轮询）
GET  /v1/sessions/{id}/messages     历史：?before_seq=&limit=（keyset，含 AI 发言 causality 摘要字段；
                                    superseded 消息默认隐藏，?include_superseded=1 显式查询）
GET  /v1/sessions/{id}/events       事件回放：?since_seq=&limit=（调试/离线补齐，需会话内权限）
POST /v1/messages/{id}:recall       撤回 = 标记性事件（不删除；投影隐藏、审计保留；
                                    权限与窗口为平台包络：本人窗口内 / 会话 Admin 任意）
```

### 4.5 记忆

```
GET   /v1/memories?persona=&mine=true            我的分册（owner=me 过滤）
GET   /v1/memories/{id}
PATCH /v1/memories/{id}                          人工编辑（edit 覆盖）
POST  /v1/memories/{id}:delete                   删除（级联撤销向量/摘要引用）
POST  /v1/memories:search                        {persona_id, query, k}（调试/展示）
```

### 4.6 配置

```
GET    /v1/configs/{scope}          合并生效视图 + 各层覆盖值 + 该 scope 可调项定义
PUT    /v1/configs/{scope}          设置本层覆盖（包络钳制；scope=world|session|persona|self，
                                    world 按 session 的 canon 解析）
DELETE /v1/configs/{scope}/{key}    清除覆盖回退上层
GET    /v1/configs/envelope         平台包络（客户端渲染滑杆范围/档位）
```

### 4.7 配额

```
GET /v1/quotas/me                 我的树：各桶余量/重置时间/下级分配
PUT /v1/quotas/me/allocations     调整下级分配（Σ ≤ self_limit 校验）
GET /v1/quotas/costs?window=      消费台账（按 tier/世界/角色聚合）
```

### 4.8 导演（会话 Owner/Admin；详态见 11）

```
POST /v1/sessions/{id}/director   {op: pause|resume|step(n)|set_rate(r)|inject(payload)|rollback(target)}
GET  /v1/sessions/{id}/explain/{event_id}   行动解释报告
```

### 4.9 技能与插件（M2，契约先定）

```
GET /v1/skills                        内置 + 已装列表
POST /v1/skills:install               {source} → 校验/扫描/注册
PUT  /v1/personas/{id}/skills         装配集（≤ 上限，冲突策略见 12）
GET  /v1/plugins · POST /v1/plugins:register   预留
```

### 4.10 命令状态（REST-only 客户端的确认通道）

```
GET /v1/commands/{command_id}
  → {state: accepted | denied | executing | unknown,
     result_events?: [{stream, seq, type}…],      // 该命令产生的事实事件引用
     error?: 错误包}
```

与幂等键同源：以同一 `Idempotency-Key` 重试/查询，永远返回同一结果。配合 WS 缺席时（弱网/后台任务）轮询确认写操作结果。

## 5. WebSocket 事件协议

```
WS  /v1/stream
上行帧：
  {"op":"subscribe","streams":["session:01H…","identity:01H…"],"since":{"session:01H…": 12345}}
  {"op":"unsubscribe","streams":[…]}
  {"op":"ping"}
下行帧：
  {"op":"sync","stream":"session:01H…","from_seq":12345,"to_seq":12680}     // 订阅确认+补齐范围
  {"op":"event","stream":"session:01H…","seq":12681,"tick":910,
   "kind":"fact","type":"message.appended","payload":{…},"ts":"…",
   "causality":{"actor":"01H…","decision":"01H…"}}                          // 对外投影（见 §6）
  {"op":"pong"} {"op":"error","code":"slow_consumer","last_seq":12690}
```

规则：

- **续传**：重连带 `since` → 服务端先 `sync` 补齐段再进入实时；游标缺段（已归档）→ `error: gap_too_large` + REST 回退指引；
- **背压**：每订阅者有界缓冲；持续超限 → 服务端断开并携带 `last_seq`（客户端续传恢复，不丢事件）；
- **心跳**：30s ping/pong；两周期无响应断开；
- **权限**：订阅即鉴权；会话流仅投订阅者有权见的事件；identity 流仅投本人/被授权者；
- **瞬态信令**（双向，零持久化）：`{"op":"signal","stream":"session:01H…","type":"typing"|"online","payload":…}` —— 不占 seq、不重放、不进事件日志（01 §4.1）；
- **本人通知流**：`identity:me` 订阅除事件外接收 `config.changed`、配额水位告警、`persona.card_changed` 等本人相关推送。

## 6. 对外投影（哪些事件可见、如何脱敏）

| 内部事件 | 对外帧 | 说明 |
|---|---|---|
| message.appended | `message` | 含 kind/meta/reply_to；AI 发言附 `causality.actor` |
| message.recalled | `message.recalled` | 撤回标记；客户端同步隐藏原消息，审计保留 |
| persona.spoke | 同 message | 附 `explain_ref`（可展开"为什么说"） |
| persona.silenced | `presence.hesitated` | 仅被@沉默的必记类 |
| decision.recorded | 不直接下发 | 经 `explain` 端点按需取（含 attention/引用/配置版本/成本） |
| member.*/role.* | `membership` | |
| persona.card_changed | `persona.updated` | 附 diff 摘要与版本号 |
| memory.updated / cost.recorded / 哈希链 | 不下发 | 内部审计面 |
| director.* | `director` | 注入事件标记 source=director，房间可见 |

## 7. 契约工程化

- OpenAPI 3.1 / AsyncAPI 2.6 文档入库，与代码同仓同版本；
- CI 契约测试：spec ↔ 处理器绑定一致性 + 黄金请求/响应样例；
- 客户端绑定代码生成（TS/Kotlin 等）由 spec 派生，禁止手写模型类；
- 事件 payload 的 schema 版本遵循 04 §3 的 upcaster 规则——**旧客户端兼容窗口内**，服务端按客户端声明版本降级投递。
