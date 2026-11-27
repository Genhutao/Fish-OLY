# 02 · 领域内核

领域内核定义五类对象：主体、卡片、会话、记忆、权限。全部类型与不变量在此定稿；存储与事件形态见 04/05。

## 1. 标识：实体与内容的二元结构

```cpp
// 实体：有身份、有所有者、可变 —— ULID（时间前缀有序）
EntityId;      // persona、session、principal、memory-scope …

// 内容：不可变、内容寻址 —— SHA-256(content)
ContentId;     // 卡片版本体、Canon 体、用户资料体、配置体、（将来）提示板模板
```

实体持有一串版本指针；内容是可去重、可校验、可导出的不可变 blob。好处：Pin 一个版本 = 持有一个 ContentId；相同内容全局只存一份；模板/导出 = 发布内容哈希清单。

## 2. Principal：用户与角色的统一壳

```cpp
enum class PrincipalKind { User, Persona };
enum class Visibility   { Private, Public, Shared };   // Shared = 授权名单

struct Principal {
    PrincipalId  id;            // ULID
    PrincipalKind kind;
    PrincipalId  owner;         // 创建者；链根必为 User
    Visibility   visibility;
    SharedList   shared_with;   // visibility == Shared 时生效
    ToolGrants   grants;        // 可用工具/插件（创建者指定；会话内可再收窄）
};
```

用户与角色同地位：同一套所有权、可见性、权限、配额。差异仅在：User 有人工认证身份（authn）；Persona 由引擎驱动、只在会话内决策（I2）。

## 3. PersonaCard：角色的纯设定

```cpp
struct Persona {                       // 实体：元数据 + 版本链指针
    EntityId    id;
    PrincipalId owner;
    Visibility  visibility;
    Grants      grants;
    vector<CardRef> versions;          // [{seq, content: ContentId, parent}] 只增
    std::optional<ContentId> derived_from;  // 二创/改卡血缘（归属与分成用，M2）
};

struct CardBody {                      // 不可变 blob
    string name, avatar;
    string description;                // 设定描述
    Dims7   personality;               // 身份/兴趣/观点/经历/成就/社交/其他
    string core_anchor;                // 不可违背的一句话
    vector<string> goals;              // 短/长期意图
    vector<string> taboos;             // 禁忌
    vector<DialogueExample> examples;  // 示例对白
    StyleHints style;                  // 文风偏好
};
```

- 修改 = 追加新版本；旧版本永存；回滚 = 移动指针（不删数据）。
- 运行态（心情/精力/日程）**不属于**卡片，见 §5 presence。

## 4. Session：运行单元

```cpp
struct Session {
    EntityId    id;
    PrincipalId owner;                 // 创建者，可转让
    ContentId   canon;                 // 世界观内容（不可变 blob）
    bool        canon_lock;            // 冻结人格类配置（见 08）
    vector<Member> members;            // ≥ 2（I1）
    std::optional<ParentRef> parent;   // 分叉来源 {stream, seq, canon_override?}
    // 运行时部分（只存在于绑定 loop，单写者域）：
    //   有界输入队列（两车道）/ 轮式 tick 槽位 / 事件 seq 游标
    //   / 读模型（消息环、成员表、读游标、未读计数）
    //   / 导演状态（paused/rate/schedules，见 11）/ 语义储备库
};

enum class SessionRole { Owner, Admin, Member };

struct Member {
    PrincipalId principal;
    SessionRole role;
    BindingRef  binding;               // 该会话生效的卡片版本（见 03 §3）
    uint64      read_seq;              // 已读游标（未读数 = 流头 seq − read_seq）
    Presence    presence;              // 仅 Persona 有意义；User 成员只有 read_seq
};                                     // 成员数上限 M=256（平台包络，06 §2 候选分级）

struct Presence {                      // 成员运行态容器（Persona）
    Energy      energy;                // 0..1
    Mood        mood;                  // 当前情绪
    WorkingSet  memory;                // 短期记忆（下）
    FallbackLines idle_lines;          // 语气储备（06 §3.2）
};

struct WorkingSet {                    // 短期记忆：不落库、不索引，可从事件重放
    RingBuffer<TurnView> recent;       // 最近 N 轮窗口（热态预算 ~4–8KB）
    vector<AgendaItem>   agenda;       // 日程：反思阶段重写
    vector<ThreadRef>    open_threads; // 未完话题/悬念
    EmotionTrace         mood_trace;   // 情绪轨迹（历史）
};
```

**Canon 与会话元数据的边界**：`canon` = 世界设定（冻结内容）；会话名称/公告/话题等是普通可变元数据事件，不进 canon。"公告承载世界规则"由 canon 承担。

**World（世界）**不是独立实体：以同一 Canon 为世界观的会话族（模板实例与 fork 族）构成世界；世界级配置与预算挂载在 canon 上（08 §1、09 §2）。

**Canon 不可变的推论**：导演注入的是事件不是设定；演化事实进记忆派生层；改世界观 = `fork(canon_override)` 分叉新会话，旧会话封存。

## 5. 记忆：两层模型

### 5.1 短期 = WorkingSet（会话内、热）

作用域 `persona × session`，驻会话 loop（I3），内容：最近窗口、日程、未完话题、情绪轨迹。**不落库、不索引**——永远可从事件重放。

### 5.2 长期 = LongTerm（跨会话、冷、派生库）

```cpp
struct LongTermEntry {
    MemoryId    id;
    PrincipalId owner;                 // 归属与该角色互动的主体（A5 裁决）
    EntityId    persona;
    StreamRef   source;                // 出处 {stream, seq}
    vector<EventId> sources;           // 溯源
    int         card_version;          // 产生时的卡片"镜头"
    EntryKind   kind;                  // Fact | Summary | Episode
    string      content;               // Fact: SPO + 置信度 + valid 区间
    double      importance;
    TimeRange   valid;                 // 冲突不覆盖，保留时效
    ReviewState review;                // candidate | accepted | edited | deleted
    int         generation;            // 归纳管线代数（重建用）
};
```

### 5.3 召回规则（装配 X 在会话 S 的提示板时）

1. S 的近期窗口（事实事件）；
2. X 在 S 的 WorkingSet（日程等）；
3. 长期召回 scope=(X, P)，**P ∈ members(S)** 才召回归属 P 的分册 —— 不在座主体的私密记忆不可被引用（隐私 + 相关性）；
4. 分叉会话追加过滤：`source ∈ 本会话祖先链`；
5. 排除 sources 落于 `superseded` 区间的事件（回滚语义，11 §4）——条目保留不删除，仅不召回。

每条召回附 provenance（来源事件、当时卡片版本），前端可展开"为什么记得"。

### 5.4 派生与人工审阅共存

- 派生候选由归纳 worker 从会话流生成，可整批重算；
- 人工覆盖（accept/edit/merge/delete/pin_importance）是身份流上的独立事件；
- 生效视图 = 候选 × 覆盖；重建协议 = 重跑候选 → 按 `sources + content hash` 重新套用覆盖 → **人工劳动永不丢失**；
- 删除级联：delete 覆盖 → 投影层撤销向量与摘要引用（防记忆污染）。

## 6. 权限模型

三个来源，单一检查点：

```cpp
// 纯函数，单点执行于命令准入（04 §5）
Decision check(PrincipalId actor, Action action, ObjectId object, Ctx ctx);

// ① 所有权链（隐式超集）：链上任一主体享有对象全权；链根 User 终控
// ② 会话层级：Owner ⊇ Admin ⊇ Member
// ③ 显式授权边：grant(subject × action × object[, field_mask])
//    - 授权不得超过所有者权限；所有者随时可撤
//    - action 粒度即工作流：card.edit（直改） vs card.propose（提案-审批）
// ④ 可见性：Private | Public | Shared 名单 —— 决定"能否建立关系"，不是行动权限
```

- 权限链不可变 → 按对象 ID 缓存，仅转让/新建/撤销时失效。
- 角色创建角色的命令记录 `actor=角色, on_behalf_of=链根用户`：审计与配额同时记在人与角色头上。
- **I4 超集不变量**是结构保证（所有权链），不是可配置策略。

## 7. 可变对象的变更波及表

"当 X 改变，引用 X 的东西怎么办"的系统答案：

| 变更 | 波及 | 处理 |
|---|---|---|
| card 更新/回滚 | Follow 绑定/会话 | offer → accept → `persona.card_changed` 注入 Follow 会话；Pin 不动；记忆与历史不动（版本是镜头不是容器） |
| card unpublish | 现有关系 | 关系保留、禁止新分享；存量豁免（已在会话中可继续） |
| card 墓碑 | 一切引用 | 仅当**零活跃关系**允许；历史保留（消息是事件）；记忆归互动者保留；blob 内容寻址仍可验证 |
| session 转让 | 权限链 | 旧 Owner 降 Admin（可选 Member）；缓存失效；审计链不变 |
| session 归档 | 成员/订阅 | 只读冻结：停 tick、拒绝用户命令准入（系统事件除外）；**fork 即复活** |
| 成员移除 | presence/WorkingSet | 工作集可重建；长期记忆不动；会话内日程作废 |
| 用户删除 | 全部所有物 | 级联表：persona→转让或墓碑；session→转让或归档；其持有记忆分册→导出后删除或匿名化（合规开关）；孤儿实体进墓碑态。M1 前定稿产品策略 |
| grant 撤销 | 准入 | 即时生效；已准入命令放行（窗口=同拍）；在途决策下一次再查 |
| tool/plugin 升级 | grants 引用 | grant 绑版本区间；主版本升级视为新工具需重新授权 |
| 会话改名/公告 | — | 普通元数据事件，与 canon 无关 |
| 分叉内改卡 | 分支 | 分叉封存**绑定**（卡版本+canon+成员快照）；卡片实体全分支共享；上游更新因 Pin 不入分支 |

## 8. 关系（Relationship）：主体对角色的持有

```cpp
enum class BindMode { FollowHead, Pin };
struct Relationship {
    PrincipalId holder;                // 创建者或分享对象
    EntityId    persona;
    BindMode    mode;
    CardVersion pinned_version;        // Pin 时有效
};
```

- 创建者默认 FollowHead；分享目标默认 Pin(当时版本)；公开角色被关注默认 FollowHead（可自降级 Pin）。
- 同步流程全部是身份流事件：`card.edited → share.offered → share.accepted | share.kept`。
- 会话内的生效版本：邀请时以**邀请者的关系绑定**初始化；Follow 会话自动前进并注入 `persona.card_changed`；Pin 会话保持到会话 Owner 手动前进。**一个会话内一个生效版本**（同室成员眼中只有一个 X）。
