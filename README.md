# OLY 世界引擎 · 服务器端

以事件流为唯一真相的世界引擎：会话即世界，角色自主生活。设计文档见 [docs/](docs/)（14 篇，自包含）。

## 仓库布局

```
oly-engine/
├── docs/                 设计文档（00 总览 … 13 路线图）
├── src/
│   ├── core/             ULID / 强类型 ID / SHA-256 / CRC-32C / Result / hex
│   ├── codec/            规范二进制编码（Encoder / Decoder，05 §2）
│   ├── events/           事件包络：命令/事实两域、哈希链、创世常量（04）
│   ├── logdb/            段存储：批次组帧、CRC、滚动、读校验（05 §2–3）
│   ├── exec/             执行内核：loop 池、轮式调度、两车道队列、路由表（01）
│   ├── domain/           领域内核：主体/卡片/会话/权限 + 身份快照（02）
│   ├── identity/         身份分片：单写者执行器 + 原子快照发布（01 §1）
│   ├── session/          会话运行时：准入/幂等/估分/提示板/决策/快照重放/多 loop 分片（04–06）
│   ├── memory/           长期记忆：候选 × 审阅覆盖 × 召回 × 重建协议 + 确定性归纳器（02 §5/03 §6/06 §4）
│   ├── gateway/          模型网关：Provider 接口、优先级车道、确定性 Fake、独立域（07）
│   ├── api/              REST + WS 流：Bearer 鉴权、命令受理 202、/v1/stream 订阅重放（10）
│   ├── log/              日志门面：OLY_LOG_* 宏（spdlog 后端，无 spdlog 时退化为 no-op）
│   └── app/              oly / oly-server 可执行入口（sanity / API 服务装配）
└── tests/
    ├── unit/             各模块单元测试
    └── golden/           黄金回放框架种子（确定性断言，13 §2）
```

模块 = 独立静态库 target（`Oly::core` …），依赖方向：`logdb → events → codec → core`，物理隔离（01 §8）。

## 构建

依赖经 vcpkg 清单管理（`catch2`、`boost-asio`、`boost-beast`、`boost-json`、`spdlog`；模块各自条件门控，依赖未就绪时对应模块/测试自动禁用，主构建不受影响）。

```bat
:: 任选 VS 开发者命令行（x64 Native Tools Command Prompt）或先 call vcvars64.bat
set VCPKG_ROOT=D:\Player\VS\vcpkg
cmake --preset win-debug
cmake --build --preset win-debug
ctest --preset win-debug
```

Linux：`cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`。

- 无 vcpkg 依赖时：core/codec/events/logdb/app 仍可配置构建（tests 自动禁用）。
- 预设见 `CMakePresets.json`；警告即错误（`/W4 /WX`、`-Wall -Wextra -Wpedantic`）。

## 当前状态

| 完成 | 说明 |
|---|---|
| ULID / 强类型 ID | 单调工厂（可注入时钟，黄金回放用）、Crockford Base32 |
| SHA-256 / CRC-32C | 哈希链与段校验原语（接口即换型点，05 §10） |
| 规范编码 codec | 定宽 LE + length-prefixed；哈希链只对规范字节计算 |
| 事件包络 | 两域/三种流/创世常量/seal 与解码校验（04 §3） |
| 段存储 | 组帧 + CRC + 滚动 + 先持久后可见游标 + 读端全链校验（05 §2–6） |
| **执行内核 exec** | loop 池（thread-per-core）、中央轮式 tick 调度器（过期插入/溢出表/摘槽）、两车道有界队列、会话路由表（白名单 rwlock）（01） |
| **领域内核 domain** | 主体/卡片版本链/会话成员注册表 + 三源权限检查（所有权链超集/会话层级/可见性）（02 §6） |
| **会话运行时 session** | 准入协议（幂等→权限→应用→事实）、脚本自治驱动、一拍一批落盘、黄金场景 e2e（04/05/06） |
| **估分内核 scoring** | 确定性注意力（种子化噪声）、阈值校准/硬冷却、沉默事件化采样（06 §2） |
| **提示板 prompt_board** | 槽位预算制、Identity/Output 地板、降级序 Memory→Recent→Skills、board_manifest（06 §3.0） |
| **模型网关 gateway** | LLMProvider 适配器接口 + 确定性 FakeProvider、P0–P3 车道（背压/削减）、deadline 回收（07） |
| **决策驱动 deliberation** | 感知→估分→装配→网关→speak；语气储备降级阶梯；decision.recorded/autonomy.silent 事件（06 §3） |
| **快照与启动重放** | snap 锚 + 补缺口 recover；存储重启续链；幂等集跨重启重建；损坏快照回退旧锚（05 §5–6） |
| 黄金回放种子 | 写 → 读 → 确定性折叠断言框架 |
| **黄金场景 e2e** | 单会话两角色 30 轮脚本对话：seq 连续/哈希链读校验/幂等恰一次/拒绝审计/重放投影一致 |
| **黄金场景：重启不丢世界** | 快照+重放续演至 30 轮 ≡ 无快照全量重放；确定性闸门（同输入事件流逐字节一致） |
| **身份分片 identity** | 单写者执行器（asio 通道受理变更，满即拒）、atomic shared_ptr 快照无锁发布（01 §1） |
| **权限双源** | 注册表直查（单 loop）≡ 身份快照（多 loop 无锁）逐项等价裁决 |
| **多 loop 分片托管** | Shard/Host：会话按键%分片路由、跨 loop 有界通道、beat 泵协程（13 §4 步骤 3） |
| **网关域拆分** | 独立 loop 池托管（同步通路 M0 形态；泵协程化属 M1） |
| **黄金冒烟：1k 会话** | 1000 会话 × 4 loop × 10 拍：拍数到位/插话跨 loop 落地/链抽查；p99 < 50ms（release 判定，debug 阈值放宽并注明） |
| **日志门面 log** | OLY_LOG_* 宏 + spdlog 后端（stderr 彩色 + 滚动文件，warn 即 flush；无 spdlog 退化为 no-op），logdb/session/api 接入 |
| **存储重开** | SegmentStore 重开扫段重建链头，拒绝损坏链（05 §2） |
| **API 最小集（10）** | Bearer 鉴权 + REST（auth/personas/sessions/messages/read/commands，幂等键重放、命令状态事件索引）+ WS /v1/stream（subscribe→重放→sync→实时、瞬态信令、慢消费者断开） |
| **黄金 e2e：API 全链** | 真实 Beast 客户端：REST 全路由断言 + WS 订阅补齐/实时事件/信令广播（含自治角色发言入流） |
| **记忆管线 memory（M1-1）** | 长期册 = 内容寻址候选 × 审阅覆盖（accept/edit/merge/delete/pin，归属校验）；召回规则（scope 隐私/时效两轴/码点相关性闸/打分排序）；确定性批化归纳（同流逐字节同候选）；重建协议（重跑归纳 → 指纹重挂覆盖，人工劳动永不丢失）；决策驱动 Memory 槽真召回 + board_manifest 可断言 |

M0 退出标准（13 §1）已齐备：API 最小集 + 1k 会话冒烟 + 全量测试绿。M0-3..M0-6 一并推送。
