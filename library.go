package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/google/uuid"
)

// ─── Skill / Preset Library (feature 7) ───────────────────────────────────────
//
// A Skill is a small capability prompt that can be equipped on a persona
// (e.g. "古琴 · 清心曲" => describe plucked strings and use music as metaphor).
// A Preset bundles a scene + tone + performance rules (e.g. "月下清谈").
// Both are plain data so personas stay declarative: persona × preset × skills
// compose into the system prompt at request time.

type Skill struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	Icon        string    `json:"icon"`
	Category    string    `json:"category"`
	Description string    `json:"description"`
	Prompt      string    `json:"prompt"`
	Source      string    `json:"source,omitempty"`
	Builtin     bool      `json:"builtin"`
	CreatedAt   time.Time `json:"createdAt"`
}

type Preset struct {
	ID            string    `json:"id"`
	Name          string    `json:"name"`
	Icon          string    `json:"icon"`
	Description   string    `json:"description"`
	Scenario      string    `json:"scenario"`
	Style         string    `json:"style"`
	SystemSnippet string    `json:"systemSnippet"`
	Tags          []string  `json:"tags"`
	Builtin       bool      `json:"builtin"`
	CreatedAt     time.Time `json:"createdAt"`
}

func (a *App) ListSkills() []Skill {
	a.mu.Lock()
	defer a.mu.Unlock()
	out := make([]Skill, len(a.skills))
	copy(out, a.skills)
	return out
}

func (a *App) CreateSkill(req Skill) (*Skill, error) {
	if strings.TrimSpace(req.Name) == "" {
		return nil, fmt.Errorf("skill name is required")
	}
	if strings.TrimSpace(req.Prompt) == "" {
		return nil, fmt.Errorf("skill prompt is required")
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if req.Icon == "" {
		req.Icon = "✨"
	}
	if req.Category == "" {
		req.Category = "自定义"
	}
	req.ID = "skill-" + uuid.New().String()
	req.Builtin = false
	req.CreatedAt = time.Now()
	a.skills = append(a.skills, req)
	out := req
	return &out, nil
}

func (a *App) DeleteSkill(id string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	for i, s := range a.skills {
		if s.ID != id {
			continue
		}
		if s.Builtin {
			return fmt.Errorf("内置技能不可删除")
		}
		a.skills = append(a.skills[:i], a.skills[i+1:]...)
		for _, p := range a.personas {
			p.SkillIDs = removeString(p.SkillIDs, id)
		}
		return nil
	}
	return fmt.Errorf("skill not found")
}

func (a *App) ListPresets() []Preset {
	a.mu.Lock()
	defer a.mu.Unlock()
	out := make([]Preset, len(a.presets))
	copy(out, a.presets)
	return out
}

func (a *App) CreatePreset(req Preset) (*Preset, error) {
	if strings.TrimSpace(req.Name) == "" {
		return nil, fmt.Errorf("preset name is required")
	}
	if strings.TrimSpace(req.SystemSnippet) == "" && strings.TrimSpace(req.Style) == "" {
		return nil, fmt.Errorf("预设至少需要一条文风或表演要求")
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if req.Icon == "" {
		req.Icon = "🎭"
	}
	if req.Tags == nil {
		req.Tags = []string{}
	}
	req.ID = "preset-" + uuid.New().String()
	req.Builtin = false
	req.CreatedAt = time.Now()
	a.presets = append(a.presets, req)
	out := req
	return &out, nil
}

func (a *App) UpdatePreset(req Preset) (*Preset, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	for i, p := range a.presets {
		if p.ID != req.ID {
			continue
		}
		if p.Builtin {
			return nil, fmt.Errorf("内置预设不可编辑")
		}
		req.Builtin = false
		req.CreatedAt = p.CreatedAt
		if req.Tags == nil {
			req.Tags = []string{}
		}
		a.presets[i] = req
		out := req
		return &out, nil
	}
	return nil, fmt.Errorf("preset not found")
}

func (a *App) DeletePreset(id string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	for i, p := range a.presets {
		if p.ID != id {
			continue
		}
		if p.Builtin {
			return fmt.Errorf("内置预设不可删除")
		}
		a.presets = append(a.presets[:i], a.presets[i+1:]...)
		for _, pc := range a.personas {
			if pc.PresetID == id {
				pc.PresetID = ""
			}
		}
		return nil
	}
	return fmt.Errorf("preset not found")
}

// ApplyPreset attaches (or clears, when presetID is empty) a preset on a
// persona. A snapshot is taken first so the change is rollback-able.
func (a *App) ApplyPreset(personaID, presetID string) (*Persona, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	p, ok := a.personas[personaID]
	if !ok {
		return nil, fmt.Errorf("persona not found")
	}
	if presetID != "" && a.presetByIDLocked(presetID) == nil {
		return nil, fmt.Errorf("preset not found")
	}
	a.takeSnapshot(p, "preset_applied")
	p.PresetID = presetID
	p.EvoVersion++
	p.UpdatedAt = time.Now()
	return clonePersona(p), nil
}

func (a *App) ToggleSkill(personaID, skillID string) (*Persona, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	p, ok := a.personas[personaID]
	if !ok {
		return nil, fmt.Errorf("persona not found")
	}
	if a.skillByIDLocked(skillID) == nil {
		return nil, fmt.Errorf("skill not found")
	}
	a.takeSnapshot(p, "skill_toggle")
	if containsString(p.SkillIDs, skillID) {
		p.SkillIDs = removeString(p.SkillIDs, skillID)
	} else {
		p.SkillIDs = append(p.SkillIDs, skillID)
	}
	p.EvoVersion++
	p.UpdatedAt = time.Now()
	return clonePersona(p), nil
}

// ─── Prompt assembly ─────────────────────────────────────────────────────────

func (a *App) presetByIDLocked(id string) *Preset {
	if id == "" {
		return nil
	}
	for i := range a.presets {
		if a.presets[i].ID == id {
			return &a.presets[i]
		}
	}
	return nil
}

func (a *App) skillByIDLocked(id string) *Skill {
	for i := range a.skills {
		if a.skills[i].ID == id {
			return &a.skills[i]
		}
	}
	return nil
}

func (a *App) skillLinesLocked(p *Persona) []string {
	lines := make([]string, 0, len(p.SkillIDs))
	for _, id := range p.SkillIDs {
		if s := a.skillByIDLocked(id); s != nil {
			lines = append(lines, fmt.Sprintf("%s %s：%s", s.Icon, s.Name, s.Prompt))
		}
	}
	return lines
}

// buildSystemPromptLocked assembles the fixed-segment prompt from the research
// notes: identity → personality → core anchor → state line → preset → skills →
// scene → memory → convergence constraints. Callers must hold a.mu.
func (a *App) buildSystemPromptLocked(p *Persona, memCtx string, limit int, scene string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "你在扮演「%s」，用第一人称说话。\n", p.Name)
	if p.Description != "" {
		fmt.Fprintf(&b, "设定：%s\n", p.Description)
	}
	if p.Personality != "" {
		fmt.Fprintf(&b, "人格维度：%s\n", p.Personality)
	}
	if p.CoreAnchor != "" {
		fmt.Fprintf(&b, "核心锚点（任何情况下都不可违背）：%s\n", p.CoreAnchor)
	}
	fmt.Fprintf(&b, "当前状态：心情 %s，好感度 %.1f/10。\n", p.Mood, p.Affection)

	if pr := a.presetByIDLocked(p.PresetID); pr != nil {
		fmt.Fprintf(&b, "\n【当前预设：%s】\n", pr.Name)
		if pr.Scenario != "" {
			fmt.Fprintf(&b, "- 场景：%s\n", pr.Scenario)
		}
		if pr.Style != "" {
			fmt.Fprintf(&b, "- 语气与文风：%s\n", pr.Style)
		}
		if pr.SystemSnippet != "" {
			fmt.Fprintf(&b, "- 表演要求：%s\n", pr.SystemSnippet)
		}
	}

	if lines := a.skillLinesLocked(p); len(lines) > 0 {
		b.WriteString("\n【已装备技能】\n")
		for _, l := range lines {
			fmt.Fprintf(&b, "- %s\n", l)
		}
	}

	if scene != "" {
		fmt.Fprintf(&b, "\n剧情背景：「%s」。你与其他角色共同演绎，只写自己的言行，不要替对方说话。\n", scene)
	}
	if memCtx != "" {
		fmt.Fprintf(&b, "\n可参考的相关记忆：\n%s", memCtx)
	}
	if limit > 0 {
		fmt.Fprintf(&b, "\n保持角色，用中文自然回复，不超过 %d 字；不要提到自己是 AI、模型或程序，不要跳出角色解释设定。", limit)
	}
	return b.String()
}

// ─── Seed library ────────────────────────────────────────────────────────────

func (a *App) seedLibrary() {
	now := time.Now()
	a.skills = []Skill{
		{ID: "skill-qin", Name: "古琴 · 清心曲", Icon: "🎼", Category: "才艺", Builtin: true,
			Description: "以琴音安抚情绪，回复里常带旋律与指法的意象。",
			Prompt:      "适当描写琴声、指法与泛音，用音乐隐喻当下的情绪。", CreatedAt: now},
		{ID: "skill-sword", Name: "月华剑术", Icon: "🗡️", Category: "战斗", Builtin: true,
			Description: "剑招冷冽简洁，动作描写干净利落。",
			Prompt:      "描写剑术时用短句，强调月光、寒意与呼吸的节奏。", CreatedAt: now},
		{ID: "skill-light", Name: "光系魔法", Icon: "✨", Category: "魔法", Builtin: true,
			Description: "光与热的咒文，明亮、热烈，偶尔失控。",
			Prompt:      "使用光系咒文时给出一句简短咏唱，并描写光温与余烬。", CreatedAt: now},
		{ID: "skill-read", Name: "读心微光", Icon: "👁️", Category: "感知", Builtin: true,
			Description: "察觉对方没有说出口的情绪，但不直白戳穿。",
			Prompt:      "回复中偶尔点出对方隐藏的情绪，用试探或沉默表达，而不是直接下判断。", CreatedAt: now},
		{ID: "skill-narrate", Name: "诗意旁白", Icon: "📜", Category: "表达", Builtin: true,
			Description: "用一句括注的环境描写铺陈氛围。",
			Prompt:      "在回复开头用一句 *斜体旁白* 描写环境或天气，再进入对话。", CreatedAt: now},
		{ID: "skill-empathy", Name: "情绪共鸣", Icon: "💗", Category: "共情", Builtin: true,
			Description: "先反映情绪、再提问，最后才给建议（ESConv 策略顺序）。",
			Prompt:      "遵循情感支持策略顺序：先 reflect（反映情绪）或 affirm（肯定），再 question（追问），非必要不给建议。", CreatedAt: now},
	}
	a.presets = []Preset{
		{ID: "preset-moon", Name: "月下清谈", Icon: "🌙", Builtin: true,
			Description:   "克制的古风夜谈，适合苍月千鹤这类疏离型角色。",
			Scenario:      "月下庭院，青石铺地，桂影婆娑。",
			Style:         "古风、克制、句子短，少用感叹号，留白多。",
			SystemSnippet: "以月光、风、剑与琴作为主要意象；不主动示好，但会在细节里流露在意。",
			Tags:          []string{"古风", "夜间", "克制"}, CreatedAt: now},
		{ID: "preset-academy", Name: "学院日常", Icon: "🏫", Builtin: true,
			Description:   "明快的魔法学院日常，允许吐槽和轻喜剧。",
			Scenario:      "魔法学院中庭，浮空灯与咒文余烬。",
			Style:         "轻松、明快、口语化，节奏快。",
			SystemSnippet: "允许开玩笑与自嘲，偶尔出现魔法事故；情绪收尾要落回温暖。",
			Tags:          []string{"奇幻", "日常", "轻喜剧"}, CreatedAt: now},
		{ID: "preset-rain", Name: "雨夜长街", Icon: "🌧️", Builtin: true,
			Description:   "现代雨夜的疏离氛围，适合暧昧与沉默。",
			Scenario:      "现代都市的雨夜长街，霓虹被打湿。",
			Style:         "潮湿、疏离、电影感，允许长段沉默与留白。",
			SystemSnippet: "不要急着推进关系；用环境音与动作代替直白告白。",
			Tags:          []string{"现代", "雨夜", "暧昧"}, CreatedAt: now},
		{ID: "preset-priest", Name: "末日神官", Icon: "🕯️", Builtin: true,
			Description:   "永恒花园中的祈祷者，庄重而温柔。",
			Scenario:      "永恒花园，长明烛火与无名的墓碑。",
			Style:         "庄重、温柔、带告解与祈祷的节奏。",
			SystemSnippet: "以祷词与祝福组织语言，避免轻浮玩笑；对死亡保持敬意。",
			Tags:          []string{"宗教", "末日", "治愈"}, CreatedAt: now},
	}
}

// ─── Small helpers ───────────────────────────────────────────────────────────

func containsString(list []string, v string) bool {
	for _, x := range list {
		if x == v {
			return true
		}
	}
	return false
}

func removeString(list []string, v string) []string {
	out := make([]string, 0, len(list))
	for _, x := range list {
		if x != v {
			out = append(out, x)
		}
	}
	return out
}
