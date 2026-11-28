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
│   ├── gateway/          （M1）模型网关
│   ├── domain/           （M0-3）领域内核类型
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
| 黄金回放种子 | 写 → 读 → 确定性折叠断言框架 |

下一步（13 §4）：领域类型（domain）→ 单会话端到端黄金场景（两角色脚本对话 30 轮）。
