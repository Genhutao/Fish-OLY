# OLY 世界引擎 · 多用户 demo

架构稿 `../design/architecture.html`（v0.2）与 `../design/multi-persona.html`（v0.3 多用户调度）的
可运行切片。服务端 + 客户端分离：

```
engine/          Go 服务端：用户世界、日计划、会话、事件、记忆、模型调用
client/          React 客户端：Vite + TS，只通过 HTTP/SSE 说话
```

服务端零外部依赖（标准库），客户端只依赖 React。

## 跑

```bash
./engine/启动.command          # 构建（首次）并打开 http://127.0.0.1:8787
```

开发模式（前后端分开跑，改前端热更新）：

```bash
# 终端 A：服务端（1 tick = 2 秒，演示压缩；正式口径 1 tick = 1 分钟）
cd engine && "$HOME/.local/go/bin/go" run . -data ./data -tick-step 2s -web ../client/dist
# 终端 B：客户端（Vite，5273，代理 /api → 8787）
cd client && pnpm dev
```

## 多用户模型（v0.3 落地版）

- **User → World**：一个用户一个世界。数据在 `data/<uid>/`：`events.jsonl`（唯一真相）+
  `memory-<persona>.json`（缓存，可删，靠事件重放重建）。
- **日计划**：每角色每天 6-10 段 intent（种子 = H(user, persona, day)，可重放），
  一天 1440 tick，tick 0 对应本地 04:00。
- **计划期交集**：串门在计划阶段对表（±60 tick 微调，重叠 ≥20 才成立）；
  成立 → pair 会话；不成 → 留信 + 访客写「没见到」。
- **在场（C1）**：自由时间窗切块认领，一个角色任一 tick 只属于一个会话（有单测）。
- **执行**：每用户一个调度协程，10 秒对齐一次应到 tick；solo 只漂状态（模板），
  pair 每 5 tick 按 attention 决定谁开口；会话结束两端各写 K/E 记忆 + 关系边 ±0.25。
- **预算（C5）**：每用户每天 40 次自主模型调用，超了自动降级模板（degraded），不阻塞世界。
- **快进（C8）**：断档 ≤3 天按模板补会话（backfilled）；>3 天写 `world.dormant`，
  只补日程与位置，不补内容。
- **记忆边界（C7）**：记忆按 persona 分库，跨角色/跨用户互不可见。

## 接口

所有世界级路由都带用户 id；`/api/v1/*` 旧别名仍指向默认用户。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/users` | 用户列表 |
| POST | `/api/v1/users` | 建新用户 `{name}` |
| GET | `/api/v1/users/{uid}/state` | 整屏快照：角色、场景、日程、会话、记忆 |
| GET | `/api/v1/users/{uid}/stream` | SSE，状态一变就推 |
| POST | `/api/v1/users/{uid}/tick` | 手动推进一分钟 |
| POST | `/api/v1/users/{uid}/chat` | 单聊 `{personaId, text}` |
| POST | `/api/v1/users/{uid}/scenes/{sid}/messages` | 场景发言（有会话则加入） |
| GET | `/api/v1/users/{uid}/memory/search?personaId=&q=&k=` | 分层检索（单角色库内） |
| POST | `/api/v1/users/{uid}/memory/forget` | 撤回一条成员 `{memberId}` |
| POST | `/api/v1/users/{uid}/memory/revive` | 恢复一条成员 |
| POST | `/api/v1/users/{uid}/memory/consolidate` | 巩固（所有角色） |
| GET | `/api/v1/health` | 服务状态 |

## 这一版真的做到了

| 设计 | 实现 |
|---|---|
| 事件先于状态 | 每用户 `events.jsonl` 只追加；启动重放，世界不回到种子 |
| 记忆也只信事件 | 写入/撤回/恢复/巩固都记 `memory.*` 事件；删缓存可逐位重建（有单测） |
| 成员级撤回 | 撤一条成员，同槽其它成员不受影响；恢复后指纹逐位回原值 |
| 计划可重放 | 日计划带种子进事件；visit 结果走 `plan.amended`；重放逐位一致 |
| 计划期交集 | 串门先对表，失败降级留信；同天同对最多一次 pair |
| 单在场（C1） | 自由窗切块认领；`TestSessionsNoLeaseOverlap` 守住 |
| 幂等（C4） | 会话游标 `NextTick` 进事件；重放不重复发言、不重复记忆 |
| 预算先行（C5） | 每用户日预算 40；超了 degraded，只出模板 |
| 可解释（C6） | 每条发言带 session/score；日程页能看到 intent 状态 |
| 双视角记忆（C7） | 会话结束 host/guest 各写一份，措辞按视角；记忆按 persona 分库 |
| 模型失败 | 自主发言降级模板并标记；用户对话显式报错，不静默吞掉 |

## 评测（记忆内核的回归闸门）

```bash
./engine/bin/oly-engine -eval /Users/yhx/WorkBuddy/VSA/p0/dataset
```

当前结果（与单世界版一致，内核未动）：

```
n=95  recall@5=0.747  mrr=0.585  silence=25 clean=0.76  T1=0.70 T2=0.85 T3=0.64
```

## 还没做

- **摘要 / 事实抽取层**：六层里的 E/F 仍靠规则打标直接写。
- **计划期对表的 ±60 微调**还没回写「为什么调整」的解释事件。
- **跨用户访客协议**（C2 的异步访客）没实现——现在用户之间完全隔离。
- **每用户独立模型配置**：模型接入还是全局 settings.json（`data/settings.json`，环境变量
  `OLY_ENDPOINT / OLY_MODEL / OLY_API_KEY` 可覆盖）。
- **场景容量**只在计划期检查；执行期用户加入还没有挤位逻辑。
- **漫游文案**：solo 会话的台词仍是模板。
