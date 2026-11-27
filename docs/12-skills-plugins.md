# 12 · 技能与插件

技能（Skill）= 可安装、可版本化、可授权的**能力包**（提示词片段和/或工具声明）。插件（Plugin）= **进程外**的扩展宿主，为技能工具与引擎扩展点提供运行时。两者共同构成平台的开放能力层。

## 1. 概念分层

```
Skill（能力包）       prompt.md（Skills 槽片段） + tools/*.yaml（工具声明）
  ├── 纯提示词技能     只影响风格/知识（进入提示板 Skills 槽）
  ├── 工具技能         调用外部能力（内置工具或插件工具）
  └── 混合技能         两者兼有
Plugin（扩展宿主）    进程外进程，经宿主协议提供：工具（ToolProvider）、
                      事件钩子（EventHook）、模型适配器（LLMProvider）
来源三层：内置（平台随版本维护）/ 社区（registry 发布，M2）/ 外部（MCP 生态，M2）
```

## 2. 技能包格式

```
skill-name/
  manifest.yaml
    id, name, version, kind: prompt|tool|hybrid
    conflict_class: 文风|知识|行为|…          # 装配冲突分类
    scenes: [月下庭院, 学院, …]              # 场景标签（场景化选择）
    permissions: { … }                       # 见 §3
    compat: {engine: ">=x.y", canon_tags: […] }
  prompt.md            # Skills 槽片段（长度上限进入包络）
  tools/*.yaml         # name, description, input_schema(JSON Schema),
                       # execution: builtin:<name> | plugin:<plugin>/<tool>
  assets/              # icon, 示例对话, 测试用例
```

## 3. 权限模型

| 权限 | 默认 | 说明 |
|---|---|---|
| 读取角色/世界（当前 scope） | 允许 | 仅限装备上下文内 |
| 网络访问 | **拒绝** | 按域名白名单声明；宿主代理出口强制 |
| 文件系统 | **拒绝** | 宿主不提供任何文件访问 |
| 模型调用 | 允许 | 计入主体配额；档位受 manifest 上限约束 |
| 发送消息 | 需声明 | 独立速率桶；防刷屏 |

权限在两处强制：**宿主侧**（进程边界，物理隔离）+ **引擎侧**（准入点二次校验）。技能不能越出其所被授予主体（角色/用户）的权限与配额。

## 4. 安装管线

```
fetch → manifest schema 校验 → 静态扫描（工具白名单、域名白名单、prompt 长度上限）
  → 内容寻址入库（blobs）→ registry 注册（skill.registered，身份流）
  → 签名（社区技能，M2）
```

- M0：内置技能 + 本地包安装（运营操作）；
- M2：registry、社区发布审核流、签名与版本回滚。

## 5. 装配与冲突

```
persona.skills ≤ 8（平台包络）
同类冲突（conflict_class 相同）→ 按装配优先级 + 场景标签选择：
  会话 canon 场景 tags ∈ skill.scenes 决定本次 deliberation 的生效集
同时装配同类冲突 → 装配时警告提示（不静默覆盖，不互相打架）
生效集在 decision.recorded 中留痕（可解释"本次用了哪些技能"）
```

- `prompt.md` → 提示板 Skills 槽（预算见 06 §3.0）；
- 工具声明 → 工具注册表，绑定版本区间（与 grants 一致）；
- 主版本升级视为新工具，需重新授权（02 §7）。

## 6. 工具执行路径（工具技能 / 插件工具）

```
角色 tool_calls → 决策协程翻译 → 工具注册表查版本
  → 权限检查（主体 grants × 技能 manifest × 会话收窄）
  → 配额桶（每技能/插件速率，09）
  → 执行：
      builtin:<name>  → 引擎内置执行器（进程内，权限同上）
      plugin:<p>/<t>  → 宿主协议 tools/call（超时/取消槽必备）
  → 结果回填决策循环（读类）或翻译为命令事件（改状态类）
```

## 7. 插件宿主协议（MCP 形态）

```
传输：stdio（本地插件）或 TCP+TLS（远程宿主）；JSON-RPC 2.0 帧
方法（引擎 → 插件）：
  initialize      → 能力/清单交换（工具清单、hook 声明、权限需求）
  tools/list      → 工具描述（name、input_schema、成本注记）
  tools/call      → {name, args} → {result | error}（带超时与取消）
（插件 → 引擎，M2）：hook.emit（白名单声明）、log
生命周期：spawn → initialize → serve → 健康检查 → 退出
监管：崩溃 → 指数退避重启；连续失败 → 熔断禁用（事件进进程流）
```

协议形态刻意贴近 MCP：DeepSeek 等外部生态的插件/工具服务可通过薄桥接入，引擎无需改造。**进程外即沙箱**——平台永不执行插件提供的进程内代码。

## 8. 三个引擎扩展点（预留接口，M0 只有内置实现）

| 扩展点 | 接口 | 插件可注册 |
|---|---|---|
| ToolProvider | tools/list + tools/call | 新工具（供技能工具声明引用） |
| EventHook | 进程流过滤订阅（M2） | 对引擎事件的旁路处理 |
| LLMProvider | submit/reply（与 07 内置适配器同接口） | 新模型供应商 |

插件生命周期：本地开发（热重载）→ 私有安装 → 审核上架（M2）→ 版本升级 → 弃用。升级前跑兼容性用例，失败阻止上架。

## 9. 版本化与授权挂钩

- 技能全部内容寻址；`grants(skill_id, version_range)` 挂在主体上（创建者指定"这个角色可用哪些技能"）；
- 范围内小版本自动前进；主版本升级 = 需重新授权；
- 技能下架不影响已装配角色（已注册内容永存；新装配禁止）。
