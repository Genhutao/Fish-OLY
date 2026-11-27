# 03 · 身份域与版本化

身份域管理"会话之外"的一切主体状态：卡片版本链、关系绑定、授权边、用户资料、配额树结构、记忆审阅覆盖。它是与会话域对等的单写者执行域。

## 1. 身份域的执行结构

```
身份分片：shard = hash(principal_id) % S     （M0：S=1；按负载裂分，协议不变）
  分片内单写者 → 与会话 loop 同一套无锁纪律
  拥有的投影：卡片索引+版本链、关系绑定、授权边、用户资料、配额树、审阅覆盖
  命令路由：生产方 loop → 命令事件 → 分片准入执行 → 事实事件回流
  两级一致性：
    强读：成员边界处（邀请、绑定裁决）请求-响应通道 co_await 分片快照读
    弱传：绑定变化 → 变更事件 → 订阅中的会话 loop 注入 persona.card_changed
  身份流不绑定 tick（卡片管理没有世界时间），只有 seq 与墙钟
```

## 2. 内容寻址与版本链

```cpp
// 实体 = 元数据 + 版本链；内容 = 不可变 blob
struct CardRef { uint64 seq; ContentId content; std::optional<uint64> parent; };

// 身份流上的卡片事件
card.created(v1)
card.edited(vN, by=actor, on_behalf_of=链根)     // 创建者直改或被授权角色自改
card.proposed(vN+1草案, by=角色)                  // 提案-审批流
card.approved(草案) | card.rejected(草案)
share.offered(version, to=[...])                  // 创建者选择推送
share.accepted(holder, version) | share.kept(holder)
grant.changed(subject, action, object, allow|deny)
binding.changed(holder, mode, version)
```

- 哈希链防篡改（04 §3）；blob 去重（一万张相同卡只存一份）。
- 版本是**镜头不是容器**：换版本不搬家，记忆与历史锚定实体 ID。

## 3. 绑定与同步

规则见 02 §8。补充推演侧约束：

- **分叉封存绑定**：fork 时以分叉时刻的生效版本封存；分支内 Pin 不追溯上游更新；
- 分支内要试**不同人设** → 走提案-沙盒流程（§5），不污染本体；
- 公开角色的关注者默认 FollowHead；OnlyHead 变更通过 `binding.changed` 主动降级为 Pin。

## 4. 会话分叉协议（推演）

```
fork(S, offset E, canon' = ∅ | 新 Canon blob) → S'
  S'.parent  = (S.stream, E)
  S'.canon   = canon' ? canon' : S.canon          // 分叉的第二用途：换世界观
  S'.members = S@E 成员快照；owner 默认分叉者（可转让）
  S'.bindings= S@E 生效版本封存
```

- **存储**：S' 只存自有事件；重放链 = S' 自有事件 ∪ 沿 parent 链到根；深度上限（默认 8）；发布时 `flatten` 物化父前缀。
- **推演**：同一 (S, E) 可分叉 N 条互不可见分支——同一剧情不同结果。
- **记忆过滤**：召回时 `provenance.source ∈ S' 祖先链`；分叉前共享历史，分叉后各分支独立积累。
- **配置随分叉**：世界级配置随 Canon 天然共享（世界 = 同 canon 会话族，00 术语表）；会话级配置以 E 时刻生效值封存进分支。
- **明确不支持分支合并**：时间线合并造成记忆归属歧义，语义不可控。

## 5. 设定沙盒试演（fork + 版本 + 身份域的组合能力）

```
card.proposed(草案)                    // 角色或创建者提出设定草稿
  → 引擎自动 fork 试演会话（绑定=草案版本，环境=原会话上下文快照）
  → 角色/陪演角色在新设定下试聊 K 轮
  → share.offered 附带：高光对话、行为 diff（配置版本间参数对照）
  → 创建者凭证据 card.approved / rejected
```

零新增机制：全部由分叉、版本链、身份流组合实现。这是"角色自主进化"的安全轨道。

## 6. 记忆审阅覆盖层

规则见 02 §5.4。事件形态：

```
memory.reviewed(entry, op: accept|edit|merge|delete|pin, by=owner)   // 身份流
```

重建协议保证覆盖与候选解耦：重跑归纳 → 按 `sources + content hash` 匹配重放覆盖。删除级联在投影层执行（撤销向量/摘要引用），原事件不修改。

**记忆服务拓扑**：归纳 worker 池消费会话流产出候选 → 候选按 **owner hash** 写入对应身份分片的在线召回索引（usearch），并异步归档 PG（05 §8.2）；审阅覆盖驻身份分片。召回规则见 02 §5.3。

## 7. 导出与模板

| 包 | 内容 |
|---|---|
| 角色包 | 实体元数据 + 版本链 + blob 清单（可含示例对白） |
| 世界模板包 | Canon blob + 种子角色卡 hash 清单 + 可选示例事件段 |
| 会话包 | manifest + 本会话段 + 祖先链引用（flatten 可选） |

导入 = 校验哈希 → 重建投影 → 可选重放验证。外部角色卡格式（如社区通用卡）导入 = 一次 blob 包装 + 字段映射，M2。

## 8. 身份域事件清单（汇总）

| 事件 | 流 | 说明 |
|---|---|---|
| principal.created / tombstoned | identity | 主体创建/墓碑 |
| card.created / edited / proposed / approved / rejected | identity | 版本链 |
| share.offered / accepted / kept | identity | 同步 |
| binding.changed | identity | 装订变更 |
| grant.changed | identity | 授权边 |
| relationship.created / removed | identity | 关注/取消 |
| quota.granted / allocated / revoked | identity | 配额树结构（见 09） |
| memory.reviewed | identity | 审阅覆盖 |
| persona.card_changed（通知） | session | 由会话 loop 消费并注入房间可见标记 |

## 9. 架构潜力兑现清单（零新增机制的组合能力）

1. **设定沙盒试演**（§5）；
2. **时间旅行查询**："角色 X 在 T 时刻知道什么" = 重建 T 时刻投影 + 装配提示板；审计、评测、"记忆考古"同一条路；
3. **确定性回放 = 评测闸门**：长程对话、锚点漂移脚本都是"重放 + 断言"，直接进 CI；
4. **客户端增量同步**：`since=(stream, seq)` 即完整离线续传协议；
5. **全链路可解释**：任何一条 AI 发言 → causality 链 → 触发事件 → attention 分 → 引用记忆 → 卡片版本 → 生效配置 ContentId。
