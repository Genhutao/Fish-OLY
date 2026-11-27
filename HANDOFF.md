# 接手记录（HANDOFF）

- 时间：2026-11-24
- 工作区：`F:\AI-All\OLY`
- 接手时环境：Windows（无 Go / wails / pnpm），项目由上一轮在 Linux 容器里构建
  （`build.sh` 硬编码 `/mnt/project/...`，`bin/oly-persona` 是 ELF 二进制）。

## 1. 接手时看到的状态

| 项 | 状态 |
|---|---|
| `research/github-roleplay-emotion-arch.md` | 调研已完成，给出 A/B/C 三条技术血脉与参考架构 |
| `oly-persona` | Go + Wails v2 + React demo，README 标注 6 个模块 done |
| 缺口 | README 明确写着「OC 自动漫游（feature 4）」「技能/预设库（feature 7）」留给下一轮 |
| 遗留物 | Linux 专用 `start.sh`、`app.log`、`app.pid`、重复截图 `shot.png/shot2.png` |

因此本轮任务理解为：**补齐剩余两个模块，并把项目变成在 Windows 上可构建、可运行、可交接的状态。**

## 2. 本轮完成

### 2.1 恢复构建链

- 下载 Go 1.23.12 到 `..\.gotool\go`（项目本地，不污染系统 PATH）；
- 用 npmmirror 安装 pnpm 9，按 `pnpm-lock.yaml` 重装 `frontend/node_modules`；
- 重写 `build.sh`（去掉 `/mnt/project` 硬编码，改为 `go build` + `go build -H windowsgui`）；
- 新增 `build.ps1`（Windows 一键构建，自动定位本地 Go / pnpm）；
- 生成的 Wails 绑定 `frontend/wailsjs/**` 已提交，构建不再依赖 wails CLI；
- 产物：`bin/oly-persona.exe`（约 9.8 MB，纯 Go、`CGO_ENABLED=0`），已实机启动并截图验证。

### 2.2 功能 4：OC 自动漫游（`roam.go` + `RoamPage.tsx`）

- 4 个场景：月下庭院 / 魔法学院中庭 / 旧书店 / 雨夜长街；
- 每个角色 4 个字段的状态机：`scene / energy / status / activity`；
- 后台 ticker（默认 8s，可切 4/8/16s）：掉精力 → 低于 20 进入休息 → 回血 → 继续漫游；
  会随机换场景、按场景抽取行为模板、心情随场景与精力漂移；
- 约 14% 概率在 OC 社区发一条「漫游日记」帖（`isAI=true`，标签 `漫游/OC动态`），
  把「漫游」和「社区」两个模块串起来；
- 事件流最多保留 80 条，UI 每 2s 轮询；支持「开始/暂停」「推进一拍」手动演示；
- API：`GetRoamState` / `SetRoamRunning` / `SetRoamInterval` / `RoamStep`。

### 2.3 功能 7：技能 / 预设库（`library.go` + `LibraryPage.tsx`）

- `Skill`：图标、分类、一句话说明、提示词；内置 6 个（古琴、月华剑术、光系魔法、读心微光、诗意旁白、情绪共鸣）；
- `Preset`：场景 + 文风 + 表演要求 + 标签；内置 4 个（月下清谈、学院日常、雨夜长街、末日神官）；
- 支持自定义新建 / 删除；内置项受保护；预设可「应用到人设」，技能可「装备到人设」，
  两者都会先拍快照（可回滚）并递增 `evoVersion`；
- `Persona` 新增 `presetId` / `skillIds`，人设详情页展示「装配」行；
- **提示词装配**统一到 `buildSystemPromptLocked`：
  `身份 → 描述 → 7 维人格 → 核心锚点 → 状态行 → 预设 → 技能 → 剧情场景 → 记忆 → 收敛约束`。
  对话、剧情两条链路都走同一个装配器。

### 2.4 工程加固

- `App` 增加全局互斥锁，所有 store 访问加锁；读接口返回防御性拷贝，避免和 ticker 抢数据；
- AI 调用移出锁外（先取快照，再请求），避免 30s 超时卡住整个应用；
- `shutdown` 关闭漫游 goroutine；
- `takeSnapshot` 深拷贝 `tags/skillIds`，回滚不会串改；
- `gofmt -w`、`go vet` 通过；新增 `app_test.go` 5 个单测（漫游推进、预设/技能装配、技能 CRUD、mock 对话）。

### 2.5 文档与截图

- README 重写：7 个模块全 ✅、新增架构/装配/漫游说明、Windows 构建说明、测试命令；
- 截图更新为 `p1`–`p8`（人设 / 人设详情 / 漫游 / 技能库 / 设置 / 社区 / 剧情 / 卓宠，1280×800）；
- 清理 Linux 专用启动脚本与运行日志，`start.sh` 改为通用启动器。

## 3. 验证记录

```
go vet .        → 通过
go test .       → 5/5 PASS
pnpm build      → vite build 成功（1587 modules）
bin/oly-persona.exe → 启动成功，截图逐页验证：
  漫游页 3 拍 / 10 条动态 / 精力条与场景分布正常
  技能库 6 技能 / 预设库 4 预设 / 使用中角色正确
  人设详情「装配」行显示 学院日常 + 光系魔法 + 情绪共鸣
```

## 4. 已知限制

1. 数据仍在内存里，重启即清空（README 的 SQLite 迁移尚未做）；
2. 漫游行为是模板生成，不调用模型；AI 只用于对话 / 剧情 / 人设抽取；
3. 前端未装 `typescript`，`vite build` 只剥离类型不做类型检查；
4. 卓宠仍是页面内的可拖拽头像，不是独立透明悬浮窗；
5. Wails 未提供自动 UI 测试，本轮用鼠标自动化 + 截图验收。

## 5. 建议的下一步

1. SQLite 持久化（现有 struct 与表一一对应，`db/` 目录已留空）；
2. 漫游接模型：事件 → 摘要 → 写入该角色记忆，形成「它记得昨天去过哪」；
3. 技能 / 预设导入导出（JSON、SillyTavern V2/V3 卡互转）；
4. 真正的桌面宠物窗口（Wails 多窗口或独立透明窗口）；
5. 评测闸门：人设一致性 + 10 轮风格漂移脚本（调研文档第 5 节的四道闸门）。

## 6. 构建与运行

```powershell
# Windows（推荐）
pwsh -File .\build.ps1
.\bin\oly-persona.exe

# 测试
$env:GOROOT='F:\AI-All\OLY\.gotool\go'
& "$env:GOROOT\bin\go.exe" test .

# Linux/macOS
./build.sh
./bin/oly-persona
```

> 本地 Go 位于 `..\.gotool\go`；`GOPATH/GOMODCACHE/GOCACHE` 默认指向
> `..\.gopath` / `..\.gomodcache` / `..\.gocache`，`build.ps1` / `build.sh` 会自动设置。

## 7. 追加轮：UI 重构与细化设置（同日）

### 7.1 先装 10 个 UI 技能

安装在项目级 skill 根 `..\.agents\skills\<name>\SKILL.md`（harness 自动发现）：

| 技能 | 来源 | 用在何处 |
|---|---|---|
| frontend-design | anthropics/skills | 设计方向、排版、反模板检查 |
| ui-ux-pro-max | SuperCodeTool/ui-ux-pro-max-skill | 配色/字体/组件清单 |
| micro-interactions | solinkz/micro-interactions-skill | 时长、曲线、状态反馈 |
| web-design-guidelines | iizcm/web-design-guidelines-skill | 可访问性、对比度、焦点 |
| css-protips | PyModel/css-pro-tips | CSS 分层与性能 |
| review-ui-design | ColourCloudSky/review-ui-design-skill | 自检清单 |
| brand-guidelines | anthropics/skills | 品牌一致性 |
| canvas-design | anthropics/skills | 构图与视觉层级 |
| theme-factory | anthropics/skills | 主题 token 思路 |
| web-artifacts-builder | anthropics/skills | 多组件前端结构 |

每个目录附 `SOURCE.txt` 记录上游链接；许可证归上游所有。

### 7.2 设计方向

「夜间观测站」：墨黑底 + 月光金主色 + 冷青辅助，宋体标题（SimSun/Noto Serif
SC 栈），76px 图标栏，唯一的记忆点是「人设」页的星图观测面板。刻意避开紫黑
SaaS 渐变卡片、全大写 eyebrow、统一圆角阴影等模板特征。

- 新 token：`surface/raised/border/muted/body/bright` + `accent`（金色 ramp）+ `aurora/ember`
- 组件库 `components/ui.tsx`：Button / Switch / Segmented / Slider / Modal /
  Toast / Meter / EmptyState / PageHeader
- Shell：图标栏 + 顶栏（mock 状态、时钟、⌘K）+ 命令面板 + 页面过渡
- 动效：仅回应用户操作；统一 150–280ms、`cubic-bezier(0.16,1,0.3,1)`；
  尊重系统 reduced-motion 与应用内「减少动效」开关

### 7.3 页面

- **人设**：星图 hero（角色/记忆/社区/好感统计）、搜索 + 标签 + 排序、整卡可点、
  好感环、悬停上浮
- **人设详情**：双栏（对话 + 档案 rail：状态/装配/最近记忆）、打字机、悬停复制与
  时间戳、回车/⌘回车分支、提示词预览弹窗
- **漫游**：控制台条、角色精力卡、场景卡（有人时描边）、时间线动态、参数弹窗
  （能耗/休息阈值/发帖概率）
- **技能库**：三列技能卡、预设双列、装备到人设、应用预设、装配结果预览、删除确认
- **设置**：六个分组（模型/生成/漫游/记忆/界面/数据），Provider 预设、密钥显隐、
  测试连接、滑杆/分段/开关、未保存指示、重置演示数据
- **社区/剧情/卓宠/记忆/创建**：统一到新 token 与组件，补空状态、分步提示、
  标签筛选、卓宠参数（间隔/体型/自动闲聊）

### 7.4 后端配合

- `AppSettings` 扩展 16 个字段，落盘 `<userConfigDir>/oly-persona/settings.json`
- 新增 `GetStats` / `TestConnection` / `ResetDemoData` / `PreviewPersonaPrompt`
- 生成参数进入请求体；回复长度进入提示词；记忆权重进入召回打分；漫游参数进入
  tick；界面参数通过 `data-*` 属性即时生效

### 7.5 验证

`go vet` / `go test`（5/5）通过，Vite 构建通过；实机逐页截图核对了人设、详情、
漫游、技能库、设置（模型/记忆）、社区、卓宠，并实际发送了一条 mock 对话验证
打字机与记忆写入。

### 7.6 还差什么

1. 故事/社区仍偏静态，缺真实的多轮剧情编排与社区互动动画；
2. 设置里部分开关（场景氛围底纹）目前只存值未深度联动；
3. 卓宠仍是页内组件，未做独立透明窗口；
4. 未接入 TS 类型检查（项目未装 typescript），仅靠 esbuild 转译。

## 8. 第三轮：QQ 式聊天程序 + 双主题 + GitHub 拖拽

### 8.1 需求拆解

- 黑夜 / 白昼两套界面；
- 从「工作台」改成 QQ 那样的真实聊天程序：单聊、群聊、剧本房间、通话、表情包；
- 抽屉不要僵硬，所有转场要有弹性；
- 技能库接 GitHub 搜索，技能可拖到指定角色身上；
- AI 闲逛要进入群聊 / 剧本。

### 8.2 主题系统

- `styles/globals.css` 新增两组 CSS 变量（夜 / 昼），覆盖 surface / raised /
  field / panel / track / border / text / accent / aurora / ember / 阴影；
- `tailwind.config.js` 全部颜色映射到 `rgb(var(--x) / <alpha-value>)`；
- `lib/theme.ts`：dark / light / system + `localStorage` + 设置落盘 +
  View Transitions 圆形展开（尊重 reduce-motion）；
- 顶栏太阳/月亮按钮切换；设置 → 界面可切「黑夜 / 白昼 / 跟随系统」；
- 启动时 Shell 读取持久化设置并应用，避免 localStorage 与设置文件互相覆盖。

### 8.3 聊天主界面（新的默认首页 `/chat`）

- `ChatPage` + `ChatList` + `chat/parts.tsx`：
  - 会话列表：单聊 / 群聊 / 剧本筛选、搜索、未读、在线点、最后消息；
  - 消息流：分层圆角气泡、系统消息、日期分隔、发送弹入、输入中动画；
  - 输入区：表情、贴纸、收藏、图片（占位）、语音录制（波形 + 计时 + 取消/发送）；
  - 表情面板：表情 / 贴纸 / 收藏三栏；贴纸消息 hover 可收藏，favorites 存 localStorage；
  - 通话浮层：呼叫中 → 计时，静音 / 扬声器 / 摄像头 / 挂断，视频模式有模糊背景；
  - 角色资料抽屉：弹簧滑入，展示好感、装配、最近记忆；群聊展示房间信息与成员；
- 侧栏改为 消息 / 角色 / 漫游 / 动态 / 技能库 / 设置，命令面板保留剧情、卓宠入口。

### 8.4 群聊 / 剧本 / 闲逛

- `chat.go`：`Group` 模型（群聊 / 剧本）、预置三个房间、每 9 秒随机角色自发言、
  用户插话后 1–2 个角色回应；
- 漫游 tick 有 22% 概率让角色「走进房间」补一句，形成闲逛认知联动；
- 会话列表显示「AI 演绎中」状态与未读。

### 8.5 技能库 GitHub + 拖拽

- `github.go`：仓库搜索（过滤 skill/agent、按 star 排序、空结果回退精选列表）、
  直接输入 `owner/repo`、导入时遍历仓库树找第一个 `SKILL.md`（跳过
  template/example），解析 frontmatter 写入本地技能；
- 前端新增 GitHub 标签页、搜索结果卡（star / 描述 / 打开仓库 / 导入）；
- 技能卡左侧手柄可拖拽：浮动卡跟随指针、目标角色 chip 放大高亮，
  松手即 `ToggleSkill` 装配；底部常驻「拖到角色上装配」dock；
- 设置新增 GitHub Token 字段。

### 8.6 验证

- `go vet` / `go test` 通过；
- 实机验证：暗色聊天、亮色聊天、亮色人设 / 漫游 / 技能库 / 设置、贴纸面板、
  GitHub 标签页；
- GitHub 实测：搜索 “frontend design skill” 返回 Claude-Code-Frontend-Design-Toolkit；
  导入 `anthropics/skills` 成功解析 6003 字符的 SKILL.md（临时 live test，已删除）；
- 截图更新为 `p1-chat-dark`、`p2-chat-light`、`p3-personas`、`p4-roam`、
  `p5-library`、`p6-settings`、`p7-stickers`、`p8-github`。

### 8.7 本轮已知限制

1. 通话 / 语音为前端演示，不接真实 WebRTC / 麦克风录制；
2. 群聊自动发言是模板台词，未调用模型；用户插话的回复也是本地模板；
3. 拖拽装配已实现但未做自动化 UI 测试，建议手动在技能页试一次；
4. 未实现「新建群聊」表单（当前只预置房间）；
5. GitHub 只用仓库搜索，未接 code search；无 Token 时限流明显；
6. 已修复：页面切换白屏——主滚动容器复用 + AnimatePresence wait 空档。改为按路由重建 <main>、去掉 wait 过渡、错误边界包住路由、未知路由重定向 /chat；
7. 已修复：社区按空标签筛选时后端返回 nil 切片（JSON null），导致 posts.length 崩溃。



## 9. 真实 AI 接入（同日）

现象：设置里填了 DeepSeek Key/模型，但聊天仍是 mock；测试连接却请求 `api.openai.com` 超时。

根因：
1. 表单里的 endpoint/model/key 没有保存，后端仍是旧配置（openai + gpt-4o-mini）；
2. 「测试连接」测的是已保存配置，不是当前表单；
3. 之前 Key 输入依赖 React onChange，密码管理器/输入法填值可能不同步到 state；
4. 模型调用失败会静默回退 mock，用户无法知道没连上。

修复：
- 设置页 0.7 秒**自动保存**，并保留「保存并测试」；测试前先持久化当前表单；
- 保存时从 DOM ref 读取 Key，防止自动填充导致 state 为空；
- 顶栏/聊天头部显示当前模型与连接状态；
- `Chat` 记录 user/assistant 历史、带最近 12 轮上下文调用模型；
- 调用失败返回带 endpoint/model 的错误，前端显示系统消息 + toast，不再静默 mock；
- mock 回复打 `mock` 标记，并显示「去设置接入」提示；
- 新增 `GetPersonaMessages` / `ClearPersonaMessages`，切页不丢聊天记录；
- 测试隔离：`offlineApp()` 忽略本机 settings.json，避免测试依赖真实 Key。

实测：用本机已保存的 Key 请求 `https://api.deepseek.com/v1` / `deepseek-chat`，返回真实回复
（26 字，mock=false）。同时把本机保存的 endpoint/model 迁移为 DeepSeek，避免继续请求 OpenAI。
