# OLY Persona

AI 人设扮演平台 demo：Go + Wails v2 + React + Tailwind。
界面以「聊天程序」为主形态，支持黑夜 / 白昼双主题、语音视频通话演示、
表情包收藏、AI 群聊与剧本房间，以及从 GitHub 搜索并拖拽装配技能。

## Modules

| Module | Status | Description |
|---|---|---|
| 消息 / 聊天 | ✅ | QQ 式会话列表 + 气泡聊天 + 表情 / 贴纸 / 语音 + 通话演示 |
| 群聊 / 剧本 | ✅ | AI 角色在房间里自行对话；漫游中的角色会串门；可随时插话 |
| 人设 | ✅ | 七维人格、核心锚点、好感 / 心情、快照回滚、记忆召回 |
| 漫游 | ✅ | 四场景、精力与休息状态机、漫游日记、与群聊联动 |
| 技能库 | ✅ | 内置 + 自建技能/预设，GitHub 搜索导入，拖到角色上装配 |
| 动态（社区） | ✅ | 本地 feed：发帖、标签、点赞、评论，AI 漫游日记会进入社区 |
| 卓宠 | ✅ | 页内可拖拽角色、自动闲聊、对话写入记忆 |
| 设置 | ✅ | 六组分支设置 + 主题 + GitHub Token，落盘到用户配置目录 |

## Architecture

```
Go backend
├── app.go      persona CRUD + chat + story + community + settings
├── chat.go     group / script rooms, AI-to-AI chatter, roaming interjections
├── roam.go     scene + energy state machine, event feed, ticker
├── library.go  skills & presets, prompt composition
├── github.go   GitHub skill search + SKILL.md import
├── settings.go defaults, JSON persistence, connection test, stats, reset
└── app_test.go unit tests

React frontend
├── components/Shell.tsx       icon rail, top bar, ⌘K palette, page transitions
├── components/ui.tsx          button / switch / segmented / slider / modal / toast
├── components/chat/parts.tsx  bubbles, stickers, call overlay, profile drawer
├── components/chat/ChatList.tsx
├── pages/ChatPage.tsx         QQ-style conversations + composer
├── pages/{Personas,Roam,Library,Settings,...}.tsx
├── lib/theme.ts               dark / light / system + circular reveal
└── styles/globals.css         semantic theme tokens
```

### 双主题

`html[data-theme]` 驱动一组 CSS 变量（surface / raised / field / border /
text / accent），Tailwind 颜色全部映射到变量：

- **黑夜**：墨黑 + 月光金，适合夜间使用；
- **白昼**：暖纸底 + 深金强调，保持同一套排版与层级；
- 顶栏太阳/月亮按钮切换，使用 View Transitions 以点击位置为圆心展开；
  跟随系统时监听 `prefers-color-scheme`。

### 聊天形态

- 左：会话列表（单聊 / 群聊 / 剧本），头像、在线点、未读、最后一条消息；
- 中：消息流 + 圆角气泡 + 日期分隔 + 输入中动画 + 分层圆角；
- 右：角色资料抽屉，弹簧滑入（不是生硬的侧栏）；
- 输入区：表情 / 贴纸 / 收藏、语音录制（波形 + 计时）、Enter 分支；
- 通话：语音 / 视频演示浮层，呼叫中 → 通话计时，静音 / 扬声器 / 摄像头 / 挂断；
- 表情收藏存 localStorage；贴纸消息可一键收藏。

### AI 群聊与闲逛认知

`chat.go` 里预置三个房间（月下庭院夜谈、魔法学院三年级、剧本 · 雨夜长街）。
房间是 live 的：每 9 秒会有一个角色自己发言，漫游 tick 也有概率让角色
「走进房间」说话。用户可以随时发消息，房间里 1–2 个角色会回应。

### 技能库：GitHub + 拖拽

- GitHub 标签页搜索仓库（`GET /search/repositories`），过滤含 skill/agent 的仓库；
- 导入时在仓库树里找第一个 `SKILL.md`（跳过 template/example），解析 frontmatter
  后写进本地技能库；
- 技能卡左侧手柄可拖拽：浮动卡片跟随指针，拖到页面底部的角色 chip 上松手即装配；
  支持 Token（设置里填）提高限流额度。

### Prompt 装配

```
身份 → 描述 → 七维人格 → 核心锚点 → 状态行
  → 预设（场景/文风/表演要求）→ 已装备技能 → 剧情场景 → 记忆 → 收敛约束
```

`PreviewPersonaPrompt` 可查看装配后的真实提示词。

## Build

```bash
./build.sh              # 当前平台
./build.sh linux
./build.sh windows      # → bin/oly-persona.exe
```

Windows：

```powershell
pwsh -File .\build.ps1
.\bin\oly-persona.exe
```

测试：`go test .`（含漫游、预设、提示词、群聊房间）。

## 接入真实模型

聊天是真实调用，不是脚本回复：

1. 设置 → 模型接入：选 Provider（DeepSeek / OpenAI / OpenRouter / Ollama）或手填 endpoint；
2. 填 API Key。改动会在 0.7 秒后**自动保存并生效**，也可以点「保存并测试」立即验证；
3. 顶栏显示「已接入模型」，聊天头部显示当前模型名（如 `deepseek-chat`）；
4. 发送消息会带上最近 12 轮上下文与系统提示词，请求 `/chat/completions`；
5. 失败不会静默降级：错误会以系统消息 + toast 显示（含 endpoint 与 model）；
6. 只有未配置 Key 时才使用内置 mock，回复会带 `mock` 标记，并提示去接入。

聊天历史保存在运行内存里，切换页面不会丢；重启会回到种子世界。
通话、语音、贴纸仍为前端演示交互。

## Data

运行数据全在内存，重启回到种子世界；设置保存到
`<userConfigDir>/oly-persona/settings.json`。后续可按现有 struct 1:1 换成 SQLite。
