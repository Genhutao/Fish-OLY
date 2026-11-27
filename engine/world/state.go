package world

import (
	"encoding/json"
	"time"
)

// Persona 是角色在世界里的当前状态。设定字段来自种子，运行字段来自事件。
type Persona struct {
	ID          string             `json:"id"`
	Name        string             `json:"name"`
	Avatar      string             `json:"avatar"`
	Anchor      string             `json:"anchor"`
	Personality string             `json:"personality"`
	Mood        string             `json:"mood"`
	Energy      float64            `json:"energy"`
	Affection   float64            `json:"affection"`      // 对用户的好感
	Bond        map[string]float64 `json:"bond,omitempty"` // 对其他角色的关系边
	Scene       string             `json:"scene"`          // 当前所在场景
	Activity    string             `json:"activity"`
	LastSpoke   int                `json:"lastSpoke"`
}

// Message 是一条消息。Room 是场景 id 或 "dm:<personaID>"。
// Session / Intent / Score 满足 C6（可解释）：每条自主发言都能回答谁、在哪、为什么。
type Message struct {
	ID      string    `json:"id"`
	Room    string    `json:"room"`
	Actor   string    `json:"actor"` // persona id | "user" | "system"
	Name    string    `json:"name"`
	Text    string    `json:"text"`
	At      time.Time `json:"at"`
	Mock    bool      `json:"mock"`
	Session string    `json:"session,omitempty"`
	Intent  string    `json:"intent,omitempty"`
	Score   float64   `json:"score,omitempty"`
}

// Room 是 scenes 的旧别名，兼容 board.go 的装配签名。
type Room = Scene

// Scene 是用户世界内的公共场所，角色相遇的唯一空间。
type Scene struct {
	ID         string `json:"id"`
	Name       string `json:"name"`
	Topic      string `json:"topic"`
	Capacity   int    `json:"capacity"`
	Visibility string `json:"visibility"` // public | quiet
}

// Intent 是日程的最小单位：某天某段时间待在某个场景。
// tick 以「分钟」计，一天 1440；0 点对应本地 04:00 的日界。
type Intent struct {
	ID      string `json:"id"`
	SceneID string `json:"sceneId"`
	Start   int    `json:"start"`
	End     int    `json:"end"`
	Kind    string `json:"kind"`             // solo | visit
	Target  string `json:"target,omitempty"` // visit 的对象
	Status  string `json:"status"`           // planned | met | notmet | done
}

// Plan 是一人一天的日程。种子 = H(user, persona, day)，可重放。
type Plan struct {
	PersonaID string   `json:"personaId"`
	Day       int      `json:"day"`
	Seed      uint64   `json:"seed"`
	Intents   []Intent `json:"intents"`
}

// Participant 是会话成员。访客与主人在记忆里措辞不同（C7）。
type Participant struct {
	PersonaID string `json:"personaId"`
	Role      string `json:"role"` // host | guest
}

// Session 是一段有参与者、有场景、有起止的时间窗。
// 租约就藏在参与者集合里：一个角色的窗口由日程切块保证不重叠（C1）。
type Session struct {
	ID         string        `json:"id"`
	Day        int           `json:"day"`
	SceneID    string        `json:"sceneId"`
	Kind       string        `json:"kind"` // solo | pair
	Start      int           `json:"start"`
	End        int           `json:"end"`
	Parts      []Participant `json:"parts"`
	Status     string        `json:"status"`   // planned | active | ended | cancelled | notmet
	NextTick   int           `json:"nextTick"` // 幂等游标（C4）
	Said       int           `json:"said"`
	Degraded   bool          `json:"degraded,omitempty"`
	Backfilled bool          `json:"backfilled,omitempty"`
	Summary    string        `json:"summary,omitempty"`
}

func (s *Session) Has(pid string) bool {
	for _, p := range s.Parts {
		if p.PersonaID == pid {
			return true
		}
	}
	return false
}

func (s *Session) Host() string {
	for _, p := range s.Parts {
		if p.Role == "host" {
			return p.PersonaID
		}
	}
	return ""
}

func (s *Session) Guest() string {
	for _, p := range s.Parts {
		if p.Role == "guest" {
			return p.PersonaID
		}
	}
	return ""
}

// UserState 是一个用户世界的投影。所有读写都要过锁；真相在事件日志里。
type UserState struct {
	UserID   string                   `json:"userId"`
	Epoch    time.Time                `json:"epoch"` // tick 0 = 本地 04:00
	Personas map[string]*Persona      `json:"personas"`
	Order    []string                 `json:"order"`
	Scenes   map[string]*Scene        `json:"scenes"`
	SceneOrd []string                 `json:"sceneOrder"`
	Messages []Message                `json:"messages"`
	Plans    map[string]map[int]*Plan `json:"plans"`    // personaID → day → plan
	Sessions map[string]*Session      `json:"sessions"` // id → session
	Tick     int                      `json:"tick"`
	Budget   int                      `json:"budget"` // 今日已用模型调用
	Dormant  bool                     `json:"dormant"`
}

func NewUserState(uid string) *UserState {
	return &UserState{
		UserID:   uid,
		Personas: map[string]*Persona{},
		Scenes:   map[string]*Scene{},
		Plans:    map[string]map[int]*Plan{},
		Sessions: map[string]*Session{},
	}
}

// Apply 把一条事件折叠进状态。重放和实时写入走同一条路径。
func (s *UserState) Apply(e Event) {
	switch e.Type {
	case "world.epoch":
		if v, ok := e.Data["at"].(string); ok {
			if t, err := time.Parse(time.RFC3339, v); err == nil {
				s.Epoch = t
			}
		}
	case "persona.created":
		p := &Persona{
			ID: e.Actor, Name: nameOf(e), Avatar: strOf(e, "avatar"),
			Anchor: strOf(e, "anchor"), Personality: strOf(e, "personality"),
			Mood: strOf(e, "mood"), Energy: numOf(e, "energy", 60),
			Affection: numOf(e, "affection", 0), Scene: strOf(e, "scene"),
			Activity: strOf(e, "activity"), Bond: map[string]float64{},
		}
		if _, ok := s.Personas[p.ID]; !ok {
			s.Order = append(s.Order, p.ID)
		}
		s.Personas[p.ID] = p
	case "scene.created":
		sc := &Scene{
			ID: e.Room, Name: nameOf(e), Topic: strOf(e, "topic"),
			Capacity: int(numOf(e, "capacity", 4)), Visibility: strOf(e, "visibility"),
		}
		if sc.Visibility == "" {
			sc.Visibility = "public"
		}
		if _, ok := s.Scenes[sc.ID]; !ok {
			s.SceneOrd = append(s.SceneOrd, sc.ID)
		}
		s.Scenes[sc.ID] = sc
	case "plan.generated":
		pid := strOf(e, "personaId")
		day := int(numOf(e, "day", 0))
		p := &Plan{PersonaID: pid, Day: day, Seed: uint64(numOf(e, "seed", 0))}
		if raw, ok := e.Data["intents"].([]any); ok {
			p.Intents = decodeIntents(raw)
		}
		if s.Plans[pid] == nil {
			s.Plans[pid] = map[int]*Plan{}
		}
		s.Plans[pid][day] = p
	case "session.created":
		if sess, ok := decodeSession(e.Data["session"]); ok {
			if _, exists := s.Sessions[sess.ID]; !exists {
				s.Sessions[sess.ID] = sess
			}
		}
	case "session.progress":
		if sess := s.Sessions[strOf(e, "id")]; sess != nil {
			if v, ok := e.Data["status"].(string); ok {
				sess.Status = v
			}
			sess.NextTick = int(numOf(e, "nextTick", float64(sess.NextTick)))
			sess.Said = int(numOf(e, "said", float64(sess.Said)))
			sess.Degraded = boolOf(e, "degraded")
			sess.Backfilled = boolOf(e, "backfilled")
		}
	case "session.ended":
		if sess := s.Sessions[strOf(e, "id")]; sess != nil {
			sess.Status = "ended"
			sess.Summary = strOf(e, "summary")
			sess.Backfilled = boolOf(e, "backfilled")
		}
	case "session.cancelled":
		if sess := s.Sessions[strOf(e, "id")]; sess != nil {
			sess.Status = "cancelled"
		}
	case "message.created":
		m := Message{
			ID: idOf(e), Room: e.Room, Actor: e.Actor,
			Name: nameOf(e), Text: e.Text, At: e.At, Mock: boolOf(e, "mock"),
			Session: strOf(e, "session"), Intent: strOf(e, "intent"),
			Score: numOf(e, "score", 0),
		}
		s.Messages = append(s.Messages, m)
		if len(s.Messages) > 600 {
			s.Messages = s.Messages[len(s.Messages)-600:]
		}
	case "persona.state":
		p := s.Personas[e.Actor]
		if p == nil {
			return
		}
		if v, ok := e.Data["energy"].(float64); ok {
			p.Energy = v
		}
		if v, ok := e.Data["mood"].(string); ok {
			p.Mood = v
		}
		if v, ok := e.Data["activity"].(string); ok {
			p.Activity = v
		}
		if v, ok := e.Data["scene"].(string); ok {
			p.Scene = v
		} else if v, ok := e.Data["room"].(string); ok { // 兼容旧单世界事件
			p.Scene = v
		}
		if v, ok := e.Data["lastSpoke"].(float64); ok {
			p.LastSpoke = int(v)
		}
	case "relation.delta":
		p := s.Personas[e.Actor]
		if p == nil {
			return
		}
		peer := strOf(e, "peer")
		d := numOf(e, "delta", 0)
		if p.Bond == nil {
			p.Bond = map[string]float64{}
		}
		p.Bond[peer] = clampf(p.Bond[peer]+d, -10, 10)
	case "world.tick":
		if v, ok := e.Data["tick"].(float64); ok {
			s.Tick = int(v)
		}
	case "world.dormant":
		s.Dormant = true
	}
}

// TicksPerDay：一天 1440 tick（每 tick 一分钟）；tick 0 对应本地 04:00。
const TicksPerDay = 1440

// DayOf / TickOfDay 把绝对 tick 拆成天号与日内分钟。
func DayOf(tick int) int     { return tick / TicksPerDay }
func TickOfDay(tick int) int { return tick % TicksPerDay }

// Clock 把日内分钟变成 HH:MM 显示。日界 04:00 → 显示从 04:00 起算。
func Clock(tickOfDay int) string {
	m := (4*60 + tickOfDay) % TicksPerDay
	return pad2(m/60) + ":" + pad2(m%60)
}

func pad2(n int) string {
	if n < 10 {
		return "0" + itoa(n)
	}
	return itoa(n)
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b [4]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	return string(b[i:])
}

func clampf(x, lo, hi float64) float64 {
	if x < lo {
		return lo
	}
	if x > hi {
		return hi
	}
	return x
}

// ── 读路径 ────────────────────────────────────────────────────────────────

// OrderPersonaIDs 返回角色 id（注册顺序）。
func (s *UserState) OrderPersonaIDs() []string {
	return append([]string(nil), s.Order...)
}

// SceneIDs 返回场景 id（注册顺序）。
func (s *UserState) SceneIDs() []string {
	return append([]string(nil), s.SceneOrd...)
}

// Persona 取一个角色（副本，含 Bond 拷贝）。
func (s *UserState) Persona(id string) *Persona {
	p := s.Personas[id]
	if p == nil {
		return nil
	}
	cp := *p
	if p.Bond != nil {
		cp.Bond = make(map[string]float64, len(p.Bond))
		for k, v := range p.Bond {
			cp.Bond[k] = v
		}
	}
	return &cp
}

// Scene 取一个场景（副本）。
func (s *UserState) Scene(id string) *Scene {
	sc := s.Scenes[id]
	if sc == nil {
		return nil
	}
	cp := *sc
	return &cp
}

// PlanOf 取某天计划（副本）。
func (s *UserState) PlanOf(personaID string, day int) *Plan {
	if pm := s.Plans[personaID]; pm != nil {
		if p := pm[day]; p != nil {
			cp := *p
			cp.Intents = append([]Intent(nil), p.Intents...)
			return &cp
		}
	}
	return nil
}

// HasPlan 查询某天计划是否已生成（写锁内用）。
func (s *UserState) HasPlan(personaID string, day int) bool {
	if pm := s.Plans[personaID]; pm != nil {
		return pm[day] != nil
	}
	return false
}

// SceneInPlan 返回角色当天日内分钟 m 按日程所在的场景（不算会话覆盖）。
// m 落在 visit intent 里时，回退到最近的常规 intent 场景：
// 访客没走成（notmet）时他人其实还在原地。
func (s *UserState) SceneInPlan(personaID string, day, m int) (sceneID, intentID string) {
	if pm := s.Plans[personaID]; pm != nil {
		if p := pm[day]; p != nil {
			var fallback string
			for _, it := range p.Intents {
				if it.Kind == "visit" {
					continue
				}
				if m >= it.Start && m < it.End {
					return it.SceneID, it.ID
				}
				if it.End <= m {
					fallback = it.SceneID
				}
			}
			for _, it := range p.Intents {
				if m >= it.Start && m < it.End {
					return fallback, it.ID // visit 时段：回退最近常规场景
				}
			}
			if fallback != "" {
				return fallback, ""
			}
		}
	}
	if p := s.Personas[personaID]; p != nil {
		return p.Scene, ""
	}
	return "", ""
}

func (s *UserState) RoomMessages(room string, n int) []Message {
	out := make([]Message, 0, n)
	for i := len(s.Messages) - 1; i >= 0 && len(out) < n; i-- {
		if s.Messages[i].Room == room {
			out = append(out, s.Messages[i])
		}
	}
	for i, j := 0, len(out)-1; i < j; i, j = i+1, j-1 {
		out[i], out[j] = out[j], out[i]
	}
	return out
}

func (s *UserState) PersonaMessages(personaID string, n int) []Message {
	return s.RoomMessages("dm:"+personaID, n)
}

// SceneAt 返回某角色在日内分钟 m 所处的场景：会话优先于日程。
func (s *UserState) SceneAt(personaID string, day, m int) (sceneID, intentID string) {
	for _, sess := range s.Sessions {
		if sess.Day == day && sess.Has(personaID) && sess.Status != "cancelled" && sess.Status != "notmet" &&
			m >= sess.Start && m < sess.End {
			return sess.SceneID, ""
		}
	}
	if pl := s.Plans[personaID]; pl != nil {
		if p := pl[day]; p != nil {
			for _, it := range p.Intents {
				if m >= it.Start && m < it.End {
					return it.SceneID, it.ID
				}
			}
			if len(p.Intents) > 0 {
				return p.Intents[len(p.Intents)-1].SceneID, ""
			}
		}
	}
	if p := s.Personas[personaID]; p != nil {
		return p.Scene, ""
	}
	return "", ""
}

// ActiveSessionIn 返回场景里当前活跃的会话（按 start 最晚优先）。
func (s *UserState) ActiveSessionIn(sceneID string, day, m int) *Session {
	var best *Session
	for _, sess := range s.Sessions {
		if sess.SceneID != sceneID || sess.Day != day || sess.Status != "active" {
			continue
		}
		if m < sess.Start || m >= sess.End {
			continue
		}
		if best == nil || sess.Start > best.Start {
			best = sess
		}
	}
	return best
}

// SessionAtTick 返回角色在某 tick 所在的活跃会话（C1 的查询面）。
func (s *UserState) SessionAtTick(personaID string, day, m int) *Session {
	for _, sess := range s.Sessions {
		if sess.Day == day && sess.Has(personaID) && sess.Status == "active" &&
			m >= sess.Start && m < sess.End {
			return sess
		}
	}
	return nil
}

func decodeIntents(raw []any) []Intent {
	out := make([]Intent, 0, len(raw))
	for _, v := range raw {
		m, ok := v.(map[string]any)
		if !ok {
			continue
		}
		it := Intent{
			ID: str(m["id"]), SceneID: str(m["sceneId"]),
			Start: int(num(m["start"])), End: int(num(m["end"])),
			Kind: str(m["kind"]), Target: str(m["target"]), Status: str(m["status"]),
		}
		if it.Status == "" {
			it.Status = "planned"
		}
		out = append(out, it)
	}
	return out
}

func decodeSession(v any) (*Session, bool) {
	raw, err := json.Marshal(v)
	if err != nil {
		return nil, false
	}
	var s Session
	if err := json.Unmarshal(raw, &s); err != nil {
		return nil, false
	}
	return &s, true
}
