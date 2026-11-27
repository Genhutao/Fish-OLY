package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"math"
	"math/rand"
	"net/http"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
)

// ─── Data Models ─────────────────────────────────────────────────────────────

type Persona struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	Avatar      string    `json:"avatar"`
	Description string    `json:"description"`
	Personality string    `json:"personality"`
	CoreAnchor  string    `json:"coreAnchor"`
	Tags        []string  `json:"tags"`
	CreatedAt   time.Time `json:"createdAt"`
	UpdatedAt   time.Time `json:"updatedAt"`
	Affection   float64   `json:"affection"` // -10..10
	Mood        string    `json:"mood"`
	EvoVersion  int       `json:"evoVersion"`
	PresetID    string    `json:"presetId"`
	SkillIDs    []string  `json:"skillIds"`
}

type MemorySnapshot struct {
	ID        string    `json:"id"`
	PersonaID string    `json:"personaId"`
	Version   int       `json:"version"`
	Reason    string    `json:"reason"`
	State     Persona   `json:"state"`
	CreatedAt time.Time `json:"createdAt"`
}

type MemoryEntry struct {
	ID        string    `json:"id"`
	PersonaID string    `json:"personaId"`
	Content   string    `json:"content"`
	Vector    []float64 `json:"vector"`
	Tags      []string  `json:"tags"`
	Important float64   `json:"important"`
	CreatedAt time.Time `json:"createdAt"`
}

type ChatMessage struct {
	ID          string    `json:"id"`
	SessionID   string    `json:"sessionId"`
	Role        string    `json:"role"` // user | assistant | system
	PersonaID   string    `json:"personaId,omitempty"`
	PersonaName string    `json:"personaName,omitempty"`
	Content     string    `json:"content"`
	Strategy    string    `json:"strategy,omitempty"`
	Kind        string    `json:"kind,omitempty"` // text | sticker | voice | system
	Meta        string    `json:"meta,omitempty"` // sticker emoji or voice duration
	Mock        bool      `json:"mock,omitempty"` // true when produced by the offline mock
	CreatedAt   time.Time `json:"createdAt"`
}

type StorySession struct {
	ID          string        `json:"id"`
	Title       string        `json:"title"`
	Description string        `json:"description"`
	PersonaIDs  []string      `json:"personaIds"`
	UserRole    string        `json:"userRole"`
	Messages    []ChatMessage `json:"messages"`
	CreatedAt   time.Time     `json:"createdAt"`
}

type OCPost struct {
	ID        string      `json:"id"`
	AuthorID  string      `json:"authorId"`
	Author    string      `json:"author"`
	Avatar    string      `json:"avatar"`
	Content   string      `json:"content"`
	Tags      []string    `json:"tags"`
	Likes     int         `json:"likes"`
	Comments  []OCComment `json:"comments"`
	CreatedAt time.Time   `json:"createdAt"`
	IsAI      bool        `json:"isAI"`
}

type OCComment struct {
	ID        string    `json:"id"`
	AuthorID  string    `json:"authorId"`
	Author    string    `json:"author"`
	Avatar    string    `json:"avatar"`
	Content   string    `json:"content"`
	CreatedAt time.Time `json:"createdAt"`
	IsAI      bool      `json:"isAI"`
}

type AppSettings struct {
	APIEndpoint string  `json:"apiEndpoint"`
	APIKey      string  `json:"apiKey"`
	Model       string  `json:"model"`
	MaxTokens   int     `json:"maxTokens"`
	Temperature float64 `json:"temperature"`

	// Generation branches
	TopP             float64 `json:"topP"`
	PresencePenalty  float64 `json:"presencePenalty"`
	FrequencyPenalty float64 `json:"frequencyPenalty"`
	ReplyLength      string  `json:"replyLength"` // short | normal | long

	// Memory branches
	MemoryTopK      int     `json:"memoryTopK"`
	MemoryWeightSim float64 `json:"memoryWeightSim"`
	MemoryWeightImp float64 `json:"memoryWeightImp"`
	MemoryWeightRec float64 `json:"memoryWeightRec"`

	// Roaming branches
	RoamAutoPost      float64 `json:"roamAutoPost"`      // 0..1 chance per persona per tick
	RoamRestThreshold float64 `json:"roamRestThreshold"` // energy below this => rest
	RoamEnergyDecay   float64 `json:"roamEnergyDecay"`   // 0..1 multiplier on decay

	// Interface branches
	UiDensity       string `json:"uiDensity"` // compact | comfortable
	ReduceMotion    bool   `json:"reduceMotion"`
	Typewriter      bool   `json:"typewriter"`
	EnterToSend     bool   `json:"enterToSend"`
	ShowTimestamps  bool   `json:"showTimestamps"`
	SceneBackground bool   `json:"sceneBackground"`
	Theme           string `json:"theme"` // dark | light | system
	GitHubToken     string `json:"githubToken"`
}

type ChatRequest struct {
	PersonaID string `json:"personaId"`
	Message   string `json:"message"`
}

// ─── App ─────────────────────────────────────────────────────────────────────

type App struct {
	mu        sync.Mutex // guards every in-memory store below
	ctx       context.Context
	personas  map[string]*Persona
	snapshots map[string][]MemorySnapshot
	memories  map[string][]MemoryEntry
	stories   map[string]*StorySession
	posts     []OCPost
	settings  AppSettings

	// OC auto-roaming (feature 4)
	roamScenes   []RoamScene
	roamStates   map[string]*RoamState
	roamEvents   []RoamEvent
	roamRunning  bool
	roamInterval int
	roamStop     chan struct{}
	roamTicks    int

	// Skill / preset library (feature 7)
	skills  []Skill
	presets []Preset

	// Direct persona chat history (survives page switches)
	personaChats map[string][]ChatMessage

	// Chat rooms: group chats and script events (QQ-style home)
	groups      map[string]*Group
	groupsLive  bool
	groupsStop  chan struct{}
	groupsTick  int
	groupUnread map[string]int
}

func NewApp() *App {
	a := &App{
		personas:     make(map[string]*Persona),
		snapshots:    make(map[string][]MemorySnapshot),
		memories:     make(map[string][]MemoryEntry),
		stories:      make(map[string]*StorySession),
		roamScenes:   defaultRoamScenes(),
		roamStates:   make(map[string]*RoamState),
		settings:     defaultSettings(),
		personaChats: make(map[string][]ChatMessage),
		groups:       make(map[string]*Group),
		groupUnread:  make(map[string]int),
	}
	a.roamInterval = 8
	a.seedData()
	a.seedLibrary()
	a.seedGroups()
	a.ensureRoamLocked()
	a.settings = loadPersistedSettings(a.settings)
	return a
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	log.Println("[OLY Persona] started")
}

func (a *App) shutdown(ctx context.Context) {
	a.mu.Lock()
	if a.roamStop != nil {
		close(a.roamStop)
		a.roamStop = nil
	}
	if a.groupsStop != nil {
		close(a.groupsStop)
		a.groupsStop = nil
	}
	a.roamRunning = false
	a.groupsLive = false
	a.mu.Unlock()
	log.Println("[OLY Persona] shutdown")
}

// ─── Persona APIs ─────────────────────────────────────────────────────────────

func (a *App) CreatePersona(req Persona) (*Persona, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	req.ID = uuid.New().String()
	req.CreatedAt = time.Now()
	req.UpdatedAt = time.Now()
	req.EvoVersion = 1
	if req.Mood == "" {
		req.Mood = "neutral"
	}
	if req.Tags == nil {
		req.Tags = []string{}
	}
	if req.SkillIDs == nil {
		req.SkillIDs = []string{}
	}
	a.personas[req.ID] = &req
	a.ensureRoamLocked()
	out := clonePersona(&req)
	return out, nil
}

func (a *App) GetPersona(id string) (*Persona, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	p, ok := a.personas[id]
	if !ok {
		return nil, fmt.Errorf("persona not found: %s", id)
	}
	return clonePersona(p), nil
}

func (a *App) ListPersonas() []*Persona {
	a.mu.Lock()
	defer a.mu.Unlock()
	list := make([]*Persona, 0, len(a.personas))
	for _, p := range a.personas {
		list = append(list, clonePersona(p))
	}
	sort.Slice(list, func(i, j int) bool {
		return list[i].CreatedAt.After(list[j].CreatedAt)
	})
	return list
}

func (a *App) UpdatePersona(req Persona) (*Persona, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	p, ok := a.personas[req.ID]
	if !ok {
		return nil, fmt.Errorf("persona not found")
	}
	a.takeSnapshot(p, "user_edit")
	req.UpdatedAt = time.Now()
	req.CreatedAt = p.CreatedAt
	req.EvoVersion = p.EvoVersion + 1
	if req.SkillIDs == nil {
		req.SkillIDs = []string{}
	}
	if req.Mood == "" {
		req.Mood = "neutral"
	}
	a.personas[req.ID] = &req
	return clonePersona(&req), nil
}

func (a *App) DeletePersona(id string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	delete(a.personas, id)
	delete(a.roamStates, id)
	return nil
}

func (a *App) AIExtractPersona(rawText string) (*Persona, error) {
	a.mu.Lock()
	settings := a.settings
	a.mu.Unlock()
	if settings.APIKey == "" {
		return a.mockExtract(rawText), nil
	}
	prompt := fmt.Sprintf(`You are a persona extraction assistant. Given the following character description or book excerpt, extract and return a structured JSON persona with these fields:
name, description (2-3 sentences), personality (7 aspects: identity/interests/views/experiences/achievements/social_relations/other), coreAnchor (one immutable trait sentence), mood (single word), tags (3-5 strings).
Return ONLY valid JSON. Input:
%s`, rawText)

	resp, err := a.callAI(settings, []map[string]string{{"role": "user", "content": prompt}})
	if err != nil {
		return a.mockExtract(rawText), nil
	}
	var p Persona
	start := strings.Index(resp, "{")
	end := strings.LastIndex(resp, "}")
	if start >= 0 && end > start {
		if err := json.Unmarshal([]byte(resp[start:end+1]), &p); err != nil {
			return a.mockExtract(rawText), nil
		}
	}
	if p.Name == "" {
		return a.mockExtract(rawText), nil
	}
	return &p, nil
}

func (a *App) mockExtract(text string) *Persona {
	name := "未知角色"
	words := strings.Fields(text)
	if len(words) > 0 {
		name = words[0]
	}
	desc := text
	if len(desc) > 200 {
		desc = desc[:200] + "..."
	}
	return &Persona{
		Name:        name,
		Description: desc,
		Personality: "从原文自动提取。建议手动补充：身份 | 兴趣 | 观点 | 经历 | 成就 | 社交关系 | 其他",
		CoreAnchor:  "忠于自我的本质。",
		Mood:        "calm",
		Tags:        []string{"自动提取"},
	}
}

// ─── Memory APIs ──────────────────────────────────────────────────────────────

func (a *App) takeSnapshot(p *Persona, reason string) {
	snap := MemorySnapshot{
		ID:        uuid.New().String(),
		PersonaID: p.ID,
		Version:   p.EvoVersion,
		Reason:    reason,
		State:     *clonePersona(p),
		CreatedAt: time.Now(),
	}
	a.snapshots[p.ID] = append(a.snapshots[p.ID], snap)
}

func (a *App) GetSnapshots(personaID string) []MemorySnapshot {
	a.mu.Lock()
	defer a.mu.Unlock()
	snaps := make([]MemorySnapshot, len(a.snapshots[personaID]))
	copy(snaps, a.snapshots[personaID])
	sort.Slice(snaps, func(i, j int) bool {
		return snaps[i].CreatedAt.After(snaps[j].CreatedAt)
	})
	return snaps
}

func (a *App) RollbackToSnapshot(personaID, snapshotID string) (*Persona, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	for _, s := range a.snapshots[personaID] {
		if s.ID == snapshotID {
			restored := s.State
			restored.UpdatedAt = time.Now()
			restored.EvoVersion = restored.EvoVersion + 1
			a.personas[personaID] = &restored
			a.takeSnapshot(&restored, "rollback")
			return clonePersona(&restored), nil
		}
	}
	return nil, fmt.Errorf("snapshot not found")
}

func (a *App) AddMemory(personaID, content string, important float64, tags []string) (*MemoryEntry, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.addMemoryLocked(personaID, content, important, tags), nil
}

func (a *App) addMemoryLocked(personaID, content string, important float64, tags []string) *MemoryEntry {
	if tags == nil {
		tags = []string{}
	}
	entry := MemoryEntry{
		ID:        uuid.New().String(),
		PersonaID: personaID,
		Content:   content,
		Vector:    mockEmbed(content),
		Tags:      tags,
		Important: important,
		CreatedAt: time.Now(),
	}
	a.memories[personaID] = append(a.memories[personaID], entry)
	return &entry
}

func (a *App) GetMemories(personaID string) []MemoryEntry {
	a.mu.Lock()
	defer a.mu.Unlock()
	mems := make([]MemoryEntry, len(a.memories[personaID]))
	copy(mems, a.memories[personaID])
	sort.Slice(mems, func(i, j int) bool {
		return mems[i].CreatedAt.After(mems[j].CreatedAt)
	})
	return mems
}

func (a *App) RecallMemories(personaID, query string, topK int) []MemoryEntry {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.recallLocked(personaID, query, topK)
}

func (a *App) recallLocked(personaID, query string, topK int) []MemoryEntry {
	all := a.memories[personaID]
	if len(all) == 0 {
		return []MemoryEntry{}
	}
	qv := mockEmbed(query)
	type scored struct {
		entry MemoryEntry
		score float64
	}
	now := time.Now()
	results := make([]scored, 0, len(all))
	for _, m := range all {
		sim := cosineSim(qv, m.Vector)
		recency := math.Exp(-now.Sub(m.CreatedAt).Hours() / 72)
		score := a.settings.recallScore(sim, m.Important, recency)
		results = append(results, scored{m, score})
	}
	sort.Slice(results, func(i, j int) bool { return results[i].score > results[j].score })
	out := make([]MemoryEntry, 0, topK)
	for i := 0; i < topK && i < len(results); i++ {
		out = append(out, results[i].entry)
	}
	return out
}

// ─── Story APIs ───────────────────────────────────────────────────────────────

func (a *App) CreateStory(title, description string, personaIDs []string, userRole string) (*StorySession, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	s := &StorySession{
		ID:          uuid.New().String(),
		Title:       title,
		Description: description,
		PersonaIDs:  personaIDs,
		UserRole:    userRole,
		Messages:    []ChatMessage{},
		CreatedAt:   time.Now(),
	}
	a.stories[s.ID] = s
	out := *s
	out.Messages = append([]ChatMessage(nil), s.Messages...)
	return &out, nil
}

func (a *App) GetStory(id string) (*StorySession, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	s, ok := a.stories[id]
	if !ok {
		return nil, fmt.Errorf("story not found")
	}
	out := *s
	out.Messages = append([]ChatMessage(nil), s.Messages...)
	out.PersonaIDs = append([]string(nil), s.PersonaIDs...)
	return &out, nil
}

func (a *App) ListStories() []*StorySession {
	a.mu.Lock()
	defer a.mu.Unlock()
	list := make([]*StorySession, 0, len(a.stories))
	for _, s := range a.stories {
		cp := *s
		cp.Messages = append([]ChatMessage(nil), s.Messages...)
		cp.PersonaIDs = append([]string(nil), s.PersonaIDs...)
		list = append(list, &cp)
	}
	sort.Slice(list, func(i, j int) bool { return list[i].CreatedAt.After(list[j].CreatedAt) })
	return list
}

// storyJob carries everything needed to generate one persona reply outside the lock.
type storyJob struct {
	persona *Persona
	system  string
}

func (a *App) SendStoryMessage(storyID, personaID, content string) (*StorySession, error) {
	a.mu.Lock()
	story, ok := a.stories[storyID]
	if !ok {
		a.mu.Unlock()
		return nil, fmt.Errorf("story not found")
	}
	senderName := "旁白"
	if personaID != "" {
		if p, exists := a.personas[personaID]; exists {
			senderName = p.Name
		}
	}
	story.Messages = append(story.Messages, ChatMessage{
		ID:          uuid.New().String(),
		SessionID:   storyID,
		Role:        "user",
		PersonaID:   personaID,
		PersonaName: senderName,
		Content:     content,
		CreatedAt:   time.Now(),
	})
	history := append([]ChatMessage(nil), story.Messages...)
	title := story.Title
	settings := a.settings
	jobs := make([]storyJob, 0, len(story.PersonaIDs))
	for _, pid := range story.PersonaIDs {
		if pid == story.UserRole {
			continue
		}
		p, exists := a.personas[pid]
		if !exists {
			continue
		}
		pc := clonePersona(p)
		jobs = append(jobs, storyJob{
			persona: pc,
			system:  a.buildSystemPromptLocked(pc, "", minInt(80, a.settings.replyLimit()), title),
		})
	}
	a.mu.Unlock()

	for _, j := range jobs {
		reply := ""
		if settings.APIKey != "" {
			msgs := []map[string]string{{"role": "system", "content": j.system}}
			start := 0
			if len(history) > 10 {
				start = len(history) - 10
			}
			for _, m := range history[start:] {
				role := "user"
				if m.Role == "assistant" {
					role = "assistant"
				}
				msgs = append(msgs, map[string]string{"role": role, "content": fmt.Sprintf("[%s]: %s", m.PersonaName, m.Content)})
			}
			var err error
			reply, err = a.callAI(settings, msgs)
			if err != nil {
				reply = fmt.Sprintf("*%s 若有所思地看着你*", j.persona.Name)
			}
		} else {
			reply = a.mockPersonaReply(j.persona)
		}
		msg := ChatMessage{
			ID:          uuid.New().String(),
			SessionID:   storyID,
			Role:        "assistant",
			PersonaID:   j.persona.ID,
			PersonaName: j.persona.Name,
			Content:     reply,
			CreatedAt:   time.Now(),
		}
		history = append(history, msg)

		a.mu.Lock()
		if cur, exists := a.stories[storyID]; exists {
			cur.Messages = append(cur.Messages, msg)
		}
		a.mu.Unlock()
	}

	a.mu.Lock()
	defer a.mu.Unlock()
	cur, ok := a.stories[storyID]
	if !ok {
		return nil, fmt.Errorf("story not found")
	}
	out := *cur
	out.Messages = append([]ChatMessage(nil), cur.Messages...)
	out.PersonaIDs = append([]string(nil), cur.PersonaIDs...)
	return &out, nil
}

func (a *App) mockPersonaReply(persona *Persona) string {
	replies := []string{
		fmt.Sprintf("*%s 微微抬眸，眼神深邃* 是这样吗……", persona.Name),
		fmt.Sprintf("「你的话让我想到了很多。」%s 轻声道。", persona.Name),
		fmt.Sprintf("*%s 沉默片刻，随后缓缓开口* 有些事情，不是那么简单的。", persona.Name),
		fmt.Sprintf("*%s 侧过脸，若无其事地说* 随便你怎么想。", persona.Name),
		fmt.Sprintf("「……」%s 没有立刻回答，只是静静地看着。", persona.Name),
	}
	return replies[rand.Intn(len(replies))]
}

// ─── Community APIs ───────────────────────────────────────────────────────────

func (a *App) GetPosts(tag string) []OCPost {
	a.mu.Lock()
	defer a.mu.Unlock()
	if tag == "" {
		out := make([]OCPost, len(a.posts))
		copy(out, a.posts)
		return out
	}
	out := make([]OCPost, 0)
	for _, p := range a.posts {
		for _, t := range p.Tags {
			if t == tag {
				out = append(out, p)
				break
			}
		}
	}
	return out
}

func (a *App) CreatePost(personaID, content string, tags []string) (*OCPost, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	name, avatar := "匿名", "✨"
	if p, ok := a.personas[personaID]; ok {
		name, avatar = p.Name, p.Avatar
	}
	post := OCPost{
		ID: uuid.New().String(), AuthorID: personaID,
		Author: name, Avatar: avatar,
		Content: content, Tags: tags,
		Likes: 0, Comments: []OCComment{},
		CreatedAt: time.Now(), IsAI: false,
	}
	a.posts = append([]OCPost{post}, a.posts...)
	return &post, nil
}

func (a *App) LikePost(postID string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	for i := range a.posts {
		if a.posts[i].ID == postID {
			a.posts[i].Likes++
			return nil
		}
	}
	return fmt.Errorf("post not found")
}

func (a *App) CommentPost(postID, personaID, content string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	name, avatar := "用户", "🙂"
	if p, ok := a.personas[personaID]; ok {
		name, avatar = p.Name, p.Avatar
	}
	for i := range a.posts {
		if a.posts[i].ID == postID {
			a.posts[i].Comments = append(a.posts[i].Comments, OCComment{
				ID: uuid.New().String(), AuthorID: personaID,
				Author: name, Avatar: avatar, Content: content,
				CreatedAt: time.Now(), IsAI: false,
			})
			return nil
		}
	}
	return fmt.Errorf("post not found")
}

// ─── Chat API ─────────────────────────────────────────────────────────────────

func (a *App) Chat(req ChatRequest) (*ChatMessage, error) {
	a.mu.Lock()
	p, ok := a.personas[req.PersonaID]
	if !ok {
		a.mu.Unlock()
		return nil, fmt.Errorf("persona not found")
	}
	persona := clonePersona(p)
	topK := a.settings.MemoryTopK
	if topK <= 0 {
		topK = 3
	}
	recalled := a.recallLocked(req.PersonaID, req.Message, topK)
	memCtx := ""
	for _, m := range recalled {
		memCtx += "- " + m.Content + "\n"
	}
	settings := a.settings
	sys := a.buildSystemPromptLocked(persona, memCtx, settings.replyLimit(), "")

	// Keep the last few turns so a real model has conversation context.
	prior := a.personaChats[req.PersonaID]
	start := 0
	if len(prior) > 12 {
		start = len(prior) - 12
	}
	context := make([]map[string]string, 0, len(prior)-start+2)
	context = append(context, map[string]string{"role": "system", "content": sys})
	for _, m := range prior[start:] {
		role := "user"
		if m.Role == "assistant" {
			role = "assistant"
		}
		context = append(context, map[string]string{"role": role, "content": m.Content})
	}
	context = append(context, map[string]string{"role": "user", "content": req.Message})

	userMsg := ChatMessage{
		ID: uuid.New().String(), SessionID: req.PersonaID, Role: "user",
		PersonaName: "我", Content: req.Message, Kind: "text", CreatedAt: time.Now(),
	}
	a.personaChats[req.PersonaID] = append(a.personaChats[req.PersonaID], userMsg)
	a.mu.Unlock()

	reply := ""
	isMock := false
	if settings.APIKey != "" {
		var err error
		reply, err = a.callAI(settings, context)
		if err != nil {
			return nil, fmt.Errorf("模型调用失败（%s · %s）：%v", settings.APIEndpoint, settings.Model, err)
		}
	} else {
		reply = a.mockPersonaReply(persona)
		isMock = true
	}
	if len(req.Message) > 20 {
		cap := len(reply)
		if cap > 100 {
			cap = 100
		}
		a.mu.Lock()
		a.addMemoryLocked(req.PersonaID,
			fmt.Sprintf("用户：%s → 我：%s", req.Message, reply[:cap]),
			0.5, []string{"chat"})
		a.mu.Unlock()
	}

	msg := ChatMessage{
		ID: uuid.New().String(), SessionID: req.PersonaID, Role: "assistant",
		PersonaID: persona.ID, PersonaName: persona.Name,
		Content: reply, Kind: "text", Mock: isMock, CreatedAt: time.Now(),
	}
	a.mu.Lock()
	a.personaChats[req.PersonaID] = append(a.personaChats[req.PersonaID], msg)
	if len(a.personaChats[req.PersonaID]) > 200 {
		a.personaChats[req.PersonaID] = a.personaChats[req.PersonaID][len(a.personaChats[req.PersonaID])-200:]
	}
	a.mu.Unlock()
	return &msg, nil
}

// GetPersonaMessages returns the persisted direct-chat history for a persona.
func (a *App) GetPersonaMessages(personaID string) []ChatMessage {
	a.mu.Lock()
	defer a.mu.Unlock()
	out := make([]ChatMessage, len(a.personaChats[personaID]))
	copy(out, a.personaChats[personaID])
	return out
}

// ClearPersonaMessages wipes one persona's direct-chat history.
func (a *App) ClearPersonaMessages(personaID string) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	delete(a.personaChats, personaID)
	return nil
}

// ─── Settings ─────────────────────────────────────────────────────────────────

func (a *App) GetSettings() AppSettings {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.settings
}

func (a *App) SaveSettings(s AppSettings) error {
	a.mu.Lock()
	if s.MemoryTopK <= 0 {
		s.MemoryTopK = 3
	}
	if s.MemoryTopK > 20 {
		s.MemoryTopK = 20
	}
	if s.MemoryWeightSim+s.MemoryWeightImp+s.MemoryWeightRec <= 0 {
		s.MemoryWeightSim, s.MemoryWeightImp, s.MemoryWeightRec = 0.5, 0.3, 0.2
	}
	if s.RoamRestThreshold < 5 {
		s.RoamRestThreshold = 5
	}
	if s.RoamRestThreshold > 80 {
		s.RoamRestThreshold = 80
	}
	if s.RoamEnergyDecay < 0.2 {
		s.RoamEnergyDecay = 0.2
	}
	if s.RoamEnergyDecay > 3 {
		s.RoamEnergyDecay = 3
	}
	if s.RoamAutoPost < 0 {
		s.RoamAutoPost = 0
	}
	if s.RoamAutoPost > 1 {
		s.RoamAutoPost = 1
	}
	a.settings = s
	a.mu.Unlock()
	if err := persistSettings(s); err != nil {
		log.Printf("[settings] persist failed: %v", err)
	}
	return nil
}

// ─── AI Client ────────────────────────────────────────────────────────────────

type openAIRequest struct {
	Model            string              `json:"model"`
	Messages         []map[string]string `json:"messages"`
	MaxTokens        int                 `json:"max_tokens"`
	Temperature      float64             `json:"temperature"`
	TopP             float64             `json:"top_p,omitempty"`
	PresencePenalty  float64             `json:"presence_penalty,omitempty"`
	FrequencyPenalty float64             `json:"frequency_penalty,omitempty"`
}
type openAIResponse struct {
	Choices []struct {
		Message struct {
			Content string `json:"content"`
		} `json:"message"`
	} `json:"choices"`
	Error *struct {
		Message string `json:"message"`
	} `json:"error"`
}

func (a *App) callAI(settings AppSettings, messages []map[string]string) (string, error) {
	body, _ := json.Marshal(openAIRequest{
		Model: settings.Model, Messages: messages,
		MaxTokens:        settings.MaxTokens,
		Temperature:      settings.Temperature,
		TopP:             settings.TopP,
		PresencePenalty:  settings.PresencePenalty,
		FrequencyPenalty: settings.FrequencyPenalty,
	})
	endpoint := strings.TrimRight(settings.APIEndpoint, "/") + "/chat/completions"
	req, err := http.NewRequest("POST", endpoint, bytes.NewReader(body))
	if err != nil {
		return "", err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+settings.APIKey)

	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(resp.Body)
	var oar openAIResponse
	if err := json.Unmarshal(raw, &oar); err != nil {
		return "", fmt.Errorf("json decode: %w", err)
	}
	if oar.Error != nil {
		return "", fmt.Errorf("api error: %s", oar.Error.Message)
	}
	if len(oar.Choices) == 0 {
		return "", fmt.Errorf("no choices returned")
	}
	return oar.Choices[0].Message.Content, nil
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// clonePersona returns a defensive copy so callers can read a persona without
// racing the roaming engine or other mutators.
func clonePersona(p *Persona) *Persona {
	if p == nil {
		return nil
	}
	cp := *p
	cp.Tags = append([]string(nil), p.Tags...)
	cp.SkillIDs = append([]string(nil), p.SkillIDs...)
	return &cp
}

func mockEmbed(text string) []float64 {
	v := make([]float64, 32)
	for i, ch := range text {
		v[i%32] += float64(ch) / 1000.0
	}
	norm := 0.0
	for _, x := range v {
		norm += x * x
	}
	if norm = math.Sqrt(norm); norm > 0 {
		for i := range v {
			v[i] /= norm
		}
	}
	return v
}

func cosineSim(a, b []float64) float64 {
	dot, na, nb := 0.0, 0.0, 0.0
	for i := range a {
		dot += a[i] * b[i]
		na += a[i] * a[i]
		nb += b[i] * b[i]
	}
	if na == 0 || nb == 0 {
		return 0
	}
	return dot / (math.Sqrt(na) * math.Sqrt(nb))
}

// ─── Seed Data ────────────────────────────────────────────────────────────────

func (a *App) seedData() {
	now := time.Now()
	p1 := &Persona{
		ID: "seed-1", Name: "苍月千鹤", Avatar: "🌙",
		Description: "银发红瞳的古风少女，出身没落贵族，表面冷淡疏离，实则内心温柔而执着。擅长古琴与剑术，言语中常带若有若无的疏离感。",
		Personality: "身份：没落贵族之后 | 兴趣：古琴、剑术、夜观星象 | 观点：人心险恶，不如月光清净 | 经历：家族没落后独自漂泊 | 成就：剑术入神 | 社交：极少主动，但对认定之人忠心耿耿 | 其他：有轻微月亮执念",
		CoreAnchor:  "用疏离掩藏温柔，从不先承认在意",
		Tags:        []string{"古风", "冷淡系", "反差萌"}, Affection: 3.5, Mood: "calm", EvoVersion: 3,
		PresetID: "preset-moon", SkillIDs: []string{"skill-qin", "skill-sword", "skill-read"},
		CreatedAt: now.Add(-48 * time.Hour), UpdatedAt: now.Add(-2 * time.Hour),
	}
	p2 := &Persona{
		ID: "seed-2", Name: "白昼·艾尔", Avatar: "☀️",
		Description: "阳光开朗的魔法学院学生，总是笑呵呵的，能用魔法创造光与热。偶尔说出出人意料的深刻话语，让人一时语塞。",
		Personality: "身份：魔法学院三年级 | 兴趣：收集奇怪魔法道具、和人聊天 | 观点：世界本质上是温暖的 | 经历：孤儿院长大，靠奖学金入学 | 成就：光系魔法造诣极高 | 社交：广交朋友，偶尔让人感到太热情 | 其他：怕黑",
		CoreAnchor:  "用过度热情掩藏对被遗弃的恐惧",
		Tags:        []string{"奇幻", "阳光系", "魔法"}, Affection: 6.0, Mood: "happy", EvoVersion: 2,
		PresetID: "preset-academy", SkillIDs: []string{"skill-light", "skill-empathy"},
		CreatedAt: now.Add(-24 * time.Hour), UpdatedAt: now.Add(-1 * time.Hour),
	}
	a.personas["seed-1"] = p1
	a.personas["seed-2"] = p2

	a.memories["seed-1"] = []MemoryEntry{
		{ID: uuid.New().String(), PersonaID: "seed-1", Content: "用户曾在深夜向我倾诉孤独，我告诉他月亮永远在那里", Vector: mockEmbed("孤独月亮夜晚"), Tags: []string{"重要"}, Important: 0.9, CreatedAt: now.Add(-30 * time.Hour)},
		{ID: uuid.New().String(), PersonaID: "seed-1", Content: "用户喜欢古风音乐，我们聊了很久《广陵散》", Vector: mockEmbed("古风音乐广陵散"), Tags: []string{"兴趣"}, Important: 0.6, CreatedAt: now.Add(-10 * time.Hour)},
	}
	a.memories["seed-2"] = []MemoryEntry{
		{ID: uuid.New().String(), PersonaID: "seed-2", Content: "用户帮我解答了一道魔法题，我非常感激", Vector: mockEmbed("帮助魔法感激"), Tags: []string{"感激"}, Important: 0.7, CreatedAt: now.Add(-20 * time.Hour)},
	}
	a.snapshots["seed-1"] = []MemorySnapshot{
		{ID: uuid.New().String(), PersonaID: "seed-1", Version: 1, Reason: "initial", State: *p1, CreatedAt: now.Add(-48 * time.Hour)},
		{ID: uuid.New().String(), PersonaID: "seed-1", Version: 2, Reason: "user_edit", State: *p1, CreatedAt: now.Add(-10 * time.Hour)},
	}

	a.posts = []OCPost{
		{ID: uuid.New().String(), AuthorID: "seed-1", Author: "苍月千鹤", Avatar: "🌙",
			Content: "月光如此清净，为何总有人要扰它……\n偶尔会想，如果能在月宫住上一夜，会不会找到一点安宁。",
			Tags:    []string{"古风", "日记"}, Likes: 47, Comments: []OCComment{}, CreatedAt: now.Add(-5 * time.Hour), IsAI: true},
		{ID: uuid.New().String(), AuthorID: "seed-2", Author: "白昼·艾尔", Avatar: "☀️",
			Content: "今天在实验课上炸了第三个魔法炉🔥 教授说我「创意十足」……我想他的意思应该是「你给我出去」（笑哭）\n不过新的光系咒文效果超棒！有没有人想一起测试？",
			Tags:    []string{"魔法", "日常", "糗事"}, Likes: 128, Comments: []OCComment{}, CreatedAt: now.Add(-2 * time.Hour), IsAI: true},
		{ID: uuid.New().String(), AuthorID: "", Author: "luna_writer", Avatar: "🌸",
			Content: "分享一个OC提示词——「在永恒的花园中守望的末日神官」\n核心设定：她每天为逝去的人祈祷，只有在有人真心向她说话时才会露出真实的笑容……",
			Tags:    []string{"提示词", "OC设定", "奇幻"}, Likes: 93, Comments: []OCComment{}, CreatedAt: now.Add(-8 * time.Hour), IsAI: false},
		{ID: uuid.New().String(), AuthorID: "", Author: "codeweaver_x", Avatar: "🤖",
			Content: "问大家：给AI角色加「情绪疲劳」机制有没有意义？就是聊太久之后角色会变得更沉默、更需要「充电时间」那种。",
			Tags:    []string{"讨论", "AI设计", "机制"}, Likes: 215, Comments: []OCComment{}, CreatedAt: now.Add(-12 * time.Hour), IsAI: false},
	}
}
