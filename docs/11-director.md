# 11 · 导演控制

导演 = 世界控制者。全部导演操作都是**命令事件**（source=Director，绝不伪装成角色发言），与会话内其他命令同包裹、同准入、同审计。客户端通过 WS `director` 帧看到一切控制动作。

## 1. 命令面

```
POST /v1/sessions/{id}/director
  {op: "pause",  mode: "autonomy" | "full"}
  {op: "resume"}
  {op: "step",   n: 1..16}
  {op: "set_rate", r: 0.25..16}            // tick 周期 /= r，包络钳制
  {op: "inject", payload: {…}}             // 立即注入
  {op: "schedule", entries: [{at_tick | after_ticks, payload}], replace?}
  {op: "schedule_cancel", schedule_id}
  {op: "rollback", target_seq | target_tick, reason?}
```

权限默认值：`pause/resume/step/set_rate/inject/schedule` → Admin+；`rollback/transfer` → **Owner only**（默认，属配置面可收紧不可放宽超出包络）。

## 2. 暂停与单步

- **pause(autonomy)**（默认）：轮式调度摘除该会话槽位，角色不再自主行动；用户仍可正常发言（交互命令准入不变）；在途环境档（P2）决策 cancel 并按预留-结算返还预算；交互档（P0）允许完成；
- **pause(full)**：只读冻结——等同归档语义的临时形态：拒绝一切用户命令准入（denial 事件），仅系统事件与导演命令可用；
- **resume**：槽位回填，虚拟时钟从暂停点继续（不补拍）；
- **step(n)**：手动投 n 拍（可在 paused 状态下使用，拍完维持暂停）——单步调试与逐帧推演的基础；
- **set_rate(r)**：加速/减速 = 缩放该会话 tick 间隔；与中央轮式调度器兼容（每会话独立周期）。

## 3. 注入与剧本排程

```cpp
// payload 类型
narration   { text }                         // 导演旁白，房间可见标记（source=director）
scene_event { kind, attrs }                  // 结构化场景事件：天气/来客/道具/转幕
ambient     { text }                         // 环境事件，参与打分（f_urgency 高于闲聊）
```

- 注入事件进入会话输入队列后走**正常感知-估分**：角色可对其反应，也可沉默——导演推剧情，角色仍按人设行动；
- **schedule** = 定时注入序列（剧本/跑团主干）：投影持有排程表，tick 到点自动发射；`replace` 整表替换；`schedule_cancel` 作废；全部事件可审计；
- 注入速率受导演主体配额约束（09 执行点矩阵）。

## 4. 回滚协议（append-only 日志下的回滚）

日志不可变（I5），因此回滚是**投影层面的时间线重置**，不是删数据：

```
director.rollback(target)
  1. cancel 全部在途决策协程（预算返还）
  2. 投影重置：最近快照(≤ target) + 段重放 → 精确重建 target 时刻状态
     （与启动/迁移同一份代码）
  3. 写 session.rolled_back {target_seq, superseded_from: target_seq+1, at_seq: 当前 seq}
  4. 后续一切新事件正常追加；seq 单调不复用
```

语义细则：

- **有效时间线** = 最近一次 `rolled_back` 之后的世界；`superseded` 区间事件保留在日志中，查询时带 flag，可审计可"考古"；
- **消息列表**按投影（有效时间线）展示 + "已回滚"标记；superseded 消息默认隐藏、显式查询可见；
- **记忆过滤**：sources 落在 superseded 区间的长期记忆条目**保留但不召回**（不删除——它们是审计事实）；
- **WorkingSet/日程**由重放重建；打分噪声种子 = hash(stream_id, tick)，回滚后重演同一拍得到同一噪声序列（LLM 输出为已记录输入，同样回放）；
- 回滚后 tick 从 target+1 继续；快照协议照常（新快照以有效时间线为准）。

## 5. 行动解释（explain）

```
GET /v1/sessions/{id}/explain/{event_id}
{
  "event_id": "…",
  "decision": {
    "attention": {"score": 0.71, "threshold": 0.52,
                  "features": {"mention":1, "topic":0.44, "relation":0.6, …}},
    "weights_config": "ContentId",           // 生效配置版本
    "card_version": 7,
    "cited_memories": [{"id":"…","score":0.83,"source":{"stream":"…","seq":…}}],
    "board_manifest": {"identity":412,"canon":455,"state":231,"memory":1180,
                        "recent":990,"skills":160,"output":180},   // 各槽位实际 token
    "model": "…", "tier": "ambient", "tokens": {"in":2530,"out":142},
    "cost": {…}, "latency_ms": 1830
  },
  "chain": [ … ]                               // causality 链（触发事件→引用记忆→卡片版本）
}
```

任何一条 AI 发言/沉默/移动都可展开为上表——审计、评测、客服、"记忆考古"共用这一个端点。

## 6. 与各模块的既有挂钩（汇总）

| 挂钩点 | 出处 |
|---|---|
| 轮式调度摘槽/单步/变速 | 01 §2 |
| P2 cancel + 预算返还 | 06 §5、07 §3 |
| 回滚 = 快照+重放同一代码 | 05 §6 |
| rolled_back 事件与 superseded 查询 | 04 §2 |
| 导演命令配额 | 09 §4 |
| explain 端点契约 | 10 §4.8 |
