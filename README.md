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
│   ├── domain/           领域内核：主体/卡片/会话/权限（02）
│   ├── session/          会话运行时：准入/幂等/自治驱动/落盘（04 §5、05 §3）
│   ├── gateway/          （M1）模型网关
│   └── app/              oly 可执行入口（sanity）
└── tests/
    ├── unit/             各模块单元测试
    └── golden/           黄金回放框架种子（确定性断言，13 §2）
```

模块 = 独立静态库 target（`Oly::core` …），依赖方向：`logdb → events → codec → core`，物理隔离（01 §8）。

## 构建

依赖经 vcpkg 清单管理（当前仅 `catch2`；boost/openssl 等随对应模块落地加入清单）。

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
| 黄金回放种子 | 写 → 读 → 确定性折叠断言框架 |
| **黄金场景 e2e** | 单会话两角色 30 轮脚本对话：seq 连续/哈希链读校验/幂等恰一次/拒绝审计/重放投影一致 |

下一步（13 §4 M0 收尾）：快照与启动重放接入会话运行时 → 提示板与 LLM 网关雏形 → 30 轮黄金场景升格为自主生成（自治引擎 M1）。
