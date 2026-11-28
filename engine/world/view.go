package world

import (
	"fmt"

	"oly/engine/memory"
)

// PersonaView 是角色卡：状态 + 当前场景 + 今日意图（C6 可解释）。
type PersonaView struct {
	Persona
	SceneName string `json:"sceneName"`
	IntentNow string `json:"intentNow"`
	Plan      *Plan  `json:"plan,omitempty"` // 今日日程
}

type SceneView struct {
	Scene
	Messages []Message `json:"messages"`
	Here     []string  `json:"here"` // 当前在场的角色名
}

type SessionView struct {
	Session
	SceneName string   `json:"sceneName"`
	PartNames []string `json:"partNames"`
}

// UserView 是给前端的整屏快照。
type UserView struct {
	ID           string               `json:"id"`
	Tick         int                  `json:"tick"`
	Day          int                  `json:"day"`
	TickOfDay    int                  `json:"tickOfDay"`
	Clock        string               `json:"clock"`
	Model        string               `json:"model"`
	Events       int64                `json:"events"`
	Budget       BudgetView           `json:"budget"`
	Dormant      bool                 `json:"dormant"`
	Personas     []PersonaView        `json:"personas"`
	Scenes       []SceneView          `json:"scenes"`
	DMs          map[string][]Message `json:"dms"`
	Sessions     []SessionView        `json:"sessions"`
	Memory       map[string]StatsView `json:"memory"` // personaID → 记忆统计
	MessageCount int                  `json:"messageCount"`
	Log          []Event              `json:"log"`
}

type BudgetView struct {
	Used  int  `json:"used"`
	Limit int  `json:"limit"`
	Left  int  `json:"left"`
	Over  bool `json:"over"`
}

type StatsView struct {
	Slots     int               `json:"slots"`
	Members   int               `json:"members"`
	Active    int               `json:"active"`
	Forgotten int               `json:"forgotten"`
	Writes    int               `json:"writes"`
	Dups      int               `json:"dups"`
	Merges    int               `json:"merges"`
	Appends   int               `json:"appends"`
	SlotsList []memory.SlotView `json:"slotsList,omitempty"`
}

// View 组装一个用户世界的快照（线程安全：与推进/聊天共用一把锁）。
func (u *UserWorld) View() UserView {
	u.mu.Lock()
	defer u.mu.Unlock()
	return u.viewLocked()
}

func (u *UserWorld) viewLocked() UserView {
	day, m := u.Day(), u.TickOfDay()
	v := UserView{
		ID: u.ID, Tick: u.St.Tick, Day: day, TickOfDay: m,
		Clock: Clock(m), Events: u.Log.Count(), Dormant: u.St.Dormant,
		DMs:    map[string][]Message{},
		Memory: map[string]StatsView{},
		Budget: BudgetView{Used: u.St.Budget, Limit: dailyBudget, Left: dailyBudget - u.St.Budget, Over: u.St.Budget >= dailyBudget},
		Log:    u.Log.Events(24),
	}
	if u.Gen != nil {
		v.Model = u.Gen.Label()
	}
	for _, pid := range u.St.OrderPersonaIDs() {
		p := u.St.Persona(pid)
		if p == nil {
			continue
		}
		pv := PersonaView{Persona: *p}
		if sc := u.St.Scene(p.Scene); sc != nil {
			pv.SceneName = sc.Name
		}
		sceneHere, intentID := u.St.SceneAt(pid, day, m)
		if it := u.intentAt(pid, day, intentID); it != nil {
			name := "（场景待定）"
			if sc := u.St.Scene(it.SceneID); sc != nil {
				name = sc.Name
			}
			pv.IntentNow = fmt.Sprintf("%s–%s · %s", Clock(it.Start), Clock(it.End), name)
		} else if sc := u.St.Scene(sceneHere); sc != nil {
			pv.IntentNow = "现在 · " + sc.Name
		}
		pv.Plan = u.St.PlanOf(pid, day)
		v.Personas = append(v.Personas, pv)
		v.DMs[pid] = u.St.RoomMessages("dm:"+pid, 30)
		v.Memory[pid] = u.statsOf(pid)
	}
	for _, sid := range u.St.SceneOrd {
		sc := u.St.Scene(sid)
		if sc == nil {
			continue
		}
		sv := SceneView{Scene: *sc, Messages: u.St.RoomMessages(sid, 30)}
		for _, pid := range u.St.OrderPersonaIDs() {
			if here, _ := u.St.SceneAt(pid, day, m); here == sid {
				if p := u.St.Persona(pid); p != nil {
					sv.Here = append(sv.Here, p.Name)
				}
			}
		}
		v.Scenes = append(v.Scenes, sv)
	}
	for _, sess := range u.sessionList() {
		if sess.Day < day {
			continue
		}
		sv := SessionView{Session: *sess}
		if sc := u.St.Scene(sess.SceneID); sc != nil {
			sv.SceneName = sc.Name
		}
		for _, part := range sess.Parts {
			if p := u.St.Persona(part.PersonaID); p != nil {
				sv.PartNames = append(sv.PartNames, p.Name)
			}
		}
		v.Sessions = append(v.Sessions, sv)
	}
	v.MessageCount = len(u.St.Messages)
	return v
}

func (u *UserWorld) intentAt(pid string, day int, intentID string) *Intent {
	pl := u.St.PlanOf(pid, day)
	if pl == nil {
		return nil
	}
	for i := range pl.Intents {
		if pl.Intents[i].ID == intentID {
			return &pl.Intents[i]
		}
	}
	return nil
}

func (u *UserWorld) statsOf(pid string) StatsView {
	mem := u.MemOf(pid)
	st := mem.Stats()
	return StatsView{
		Slots: st.Slots, Members: st.Members, Active: st.Active, Forgotten: st.Forgotten,
		Writes: st.Writes, Dups: st.Dups, Merges: st.Merges, Appends: st.Appends,
		SlotsList: mem.Slots(),
	}
}

// TotalConsolidate 把多角色的巩固报告加总，给 API 返回。
func TotalConsolidate(reps []memory.ConsolidateReport) memory.ConsolidateReport {
	out := memory.ConsolidateReport{ImpUpdates: map[string]float64{}}
	for _, r := range reps {
		out.Merged += r.Merged
		out.Rebalanced += r.Rebalanced
		out.Forgotten += r.Forgotten
		out.SlotsAfter += r.SlotsAfter
		out.Moves = append(out.Moves, r.Moves...)
		out.ForgottenIDs = append(out.ForgottenIDs, r.ForgottenIDs...)
	}
	return out
}

// SearchMemory 检索某角色的记忆（只在该角色库内，C7）。
func (u *UserWorld) SearchMemory(pid, q string, k int) []memory.Hit {
	return u.MemOf(pid).Search(q, k, u.Day()+1)
}

// SlotsOf 返回某角色的槽视图（给 UI 撤回/恢复）。
func (u *UserWorld) SlotsOf(pid string) []memory.SlotView {
	return u.MemOf(pid).Slots()
}
