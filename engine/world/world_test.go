package world

import (
	"fmt"
	"os"
	"testing"

	"oly/engine/memory"
)

// newTestWorld 建一个临时用户世界（tick 关闭，手动推进）。
func newTestWorld(t *testing.T) *UserWorld {
	t.Helper()
	dir, err := os.MkdirTemp("", "oly-world-*")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.RemoveAll(dir) })
	log, err := OpenLog(dir + "/events.jsonl")
	if err != nil {
		t.Fatal(err)
	}
	u := &UserWorld{
		ID: "u-test", Log: log, St: NewUserState("u-test"), dir: dir,
		Mem: map[string]*memory.Store{}, Gen: nil,
	}
	u.seed()
	return u
}

func TestPlanDeterministic(t *testing.T) {
	u := newTestWorld(t)
	sceneIDs := u.St.SceneIDs()
	pids := u.St.OrderPersonaIDs()

	seed := hashSeed(u.ID, pids[0], "1")
	a := genPlan(u.ID, pids[0], 1, seed, sceneIDs, pids[1:])
	b := genPlan(u.ID, pids[0], 1, seed, sceneIDs, pids[1:])
	if len(a.Intents) != len(b.Intents) {
		t.Fatalf("同种子计划段数不同: %d vs %d", len(a.Intents), len(b.Intents))
	}
	for i := range a.Intents {
		if a.Intents[i] != b.Intents[i] {
			t.Fatalf("intent %d 不同: %+v vs %+v", i, a.Intents[i], b.Intents[i])
		}
	}
	// 常规 intent 覆盖全天且不重叠（visit 允许覆盖 solo，解析时切走）
	cur := 0
	for _, it := range a.Intents {
		if it.Kind == "visit" {
			continue
		}
		if it.Start < cur {
			t.Fatalf("intent 重叠: %d < %d", it.Start, cur)
		}
		cur = it.End
	}
	if cur != TicksPerDay {
		t.Fatalf("计划没覆盖全天: %d", cur)
	}
}

func TestSessionsNoLeaseOverlap(t *testing.T) {
	u := newTestWorld(t)
	u.ensurePlans(1) // 内部已 resolveDay

	type span struct {
		pid   string
		start int
		end   int
		id    string
	}
	spans := []span{}
	for _, sess := range u.St.Sessions {
		for _, p := range sess.Parts {
			spans = append(spans, span{p.PersonaID, sess.Start, sess.End, sess.ID})
		}
	}
	for i := 0; i < len(spans); i++ {
		for j := i + 1; j < len(spans); j++ {
			a, b := spans[i], spans[j]
			if a.pid != b.pid {
				continue
			}
			if a.start < b.end && b.start < a.end {
				t.Fatalf("C1 违反：角色 %s 在两个会话重叠 (%s %d-%d vs %s %d-%d)",
					a.pid, a.id, a.start, a.end, b.id, b.start, b.end)
			}
		}
	}
	// 每个角色必须有 solo 或 pair 会话盖住大部分时间
	for _, pid := range u.St.OrderPersonaIDs() {
		covered := 0
		for _, s := range spans {
			if s.pid == pid {
				covered += s.end - s.start
			}
		}
		if covered < TicksPerDay-240 {
			t.Fatalf("角色 %s 会话覆盖不足: %d/%d", pid, covered, TicksPerDay)
		}
	}
}

func TestVisitPairOrNotMet(t *testing.T) {
	u := newTestWorld(t)
	u.ensurePlans(1)
	// 找 visit intent
	visits := 0
	met := 0
	for _, pid := range u.St.OrderPersonaIDs() {
		pl := u.St.PlanOf(pid, 1)
		if pl == nil {
			continue
		}
		for _, it := range pl.Intents {
			if it.Kind != "visit" {
				continue
			}
			visits++
			if it.Status == "met" {
				met++
				// 必须有对应的 pair 会话
				found := false
				for _, sess := range u.St.Sessions {
					if sess.Kind == "pair" && sess.Has(pid) && sess.Has(it.Target) &&
						sess.Start == it.Start && sess.End == it.End {
						found = true
					}
				}
				if !found {
					t.Fatalf("visit met 但没有 pair 会话: %+v", it)
				}
			} else if it.Status == "notmet" {
				// 留信事件应该在日志里
				ok := false
				for _, ev := range u.Log.Events(0) {
					if ev.Type == "message.created" && ev.Actor == "system" &&
						len(ev.Text) > 0 && ev.Room != "" {
						ok = true
					}
				}
				if !ok {
					t.Fatalf("visit notmet 但没有留信事件")
				}
			}
		}
	}
	t.Logf("visits=%d met=%d sessions=%d", visits, met, len(u.St.Sessions))
}

func TestPairSessionMemoryTwoSides(t *testing.T) {
	u := newTestWorld(t)
	u.ensurePlans(1)
	// 手动造一个 pair 会话并收尾
	g, h := u.St.OrderPersonaIDs()[0], u.St.OrderPersonaIDs()[1]
	sess := &Session{
		ID: "sess-test", Day: 1, SceneID: "scene-court", Kind: "pair",
		Start: 600, End: 660,
		Parts:  []Participant{{PersonaID: g, Role: "host"}, {PersonaID: h, Role: "guest"}},
		Status: "active", NextTick: 600,
	}
	u.St.Sessions[sess.ID] = sess
	u.closeSession(sess, false)

	gm := u.MemOf(g).Stats()
	hm := u.MemOf(h).Stats()
	// 每端各写 K+E 两条（加上 seed 的 2 条）
	if hm.Members < 4 || gm.Members < 4 {
		t.Fatalf("pair 收尾记忆不足: host=%d guest=%d", hm.Members, gm.Members)
	}
	// C7：两边文本必须不同（措辞按视角）
	ht, gt := "", ""
	for _, sv := range u.MemOf(h).Slots() {
		ht += sv.Text
	}
	for _, sv := range u.MemOf(g).Slots() {
		gt += sv.Text
	}
	if ht == gt {
		t.Fatalf("两端记忆文本相同，没有按视角写")
	}
}

func TestReplayEquivalence(t *testing.T) {
	u := newTestWorld(t)
	u.ensurePlans(1)
	// 推进 100 tick
	for i := 0; i < 100; i++ {
		u.TickOnce()
	}
	// 快照 1：会话集合 + 记忆指纹
	sig1 := worldSig(u)

	// 重放：新 store + 新 state，按同一份日志
	log2, err := OpenLog(u.dir + "/events.jsonl")
	if err != nil {
		t.Fatal(err)
	}
	st2 := NewUserState(u.ID)
	u2 := &UserWorld{ID: u.ID, Log: log2, St: st2, dir: u.dir,
		Mem: map[string]*memory.Store{}}
	evs := log2.Events(0)
	for _, ev := range evs {
		st2.Apply(ev)
	}
	for _, ev := range evs {
		u2.applyMemoryEvent(ev)
	}
	u2.ensurePlans(u2.Day())
	sig2 := worldSig(u2)

	if sig1 != sig2 {
		t.Fatalf("重放不一致:\n sig1=%s\n sig2=%s", sig1, sig2)
	}
}

func worldSig(u *UserWorld) string {
	day := u.Day()
	s := fmt.Sprintf("tick=%d sess=%d", u.St.Tick, len(u.St.Sessions))
	for _, pid := range u.St.OrderPersonaIDs() {
		st := u.MemOf(pid).Stats()
		s += fmt.Sprintf(" %s:members=%d,active=%d", pid, st.Members, st.Active)
		for _, sv := range u.MemOf(pid).Slots() {
			s += "|" + sv.ID + ":" + sv.Vec
		}
		// 计划 intents 状态
		if pl := u.St.PlanOf(pid, day); pl != nil {
			for _, it := range pl.Intents {
				s += fmt.Sprintf("/%s:%s", it.ID, it.Status)
			}
		}
	}
	return s
}

func TestDormantAfterGap(t *testing.T) {
	u := newTestWorld(t)
	u.ensurePlans(1)
	before := u.St.Tick
	// 跳 5 天 → dormant
	u.AdvanceTo(before+5*TicksPerDay, false)
	if !u.St.Dormant {
		t.Fatalf("超过 3 天断层没有标记 dormant")
	}
	found := false
	for _, ev := range u.Log.Events(0) {
		if ev.Type == "world.dormant" {
			found = true
		}
	}
	if !found {
		t.Fatalf("没有 world.dormant 事件")
	}
}

func TestVisitNotMetWritesNote(t *testing.T) {
	u := newTestWorld(t)
	// 造一个必然失败的 visit：target 的计划不存在（用不存在的 pid）
	u.noteNotMet("p-chizuru", "p-el", 1)
	found := false
	for _, ev := range u.Log.Events(0) {
		if ev.Type == "message.created" && ev.Actor == "system" {
			found = true
		}
	}
	if !found {
		t.Fatal("notmet 没有写留信事件")
	}
	// 访客要有记忆
	if u.MemOf("p-chizuru").Stats().Members < 3 {
		t.Fatal("访客没写「没见到」记忆")
	}
}
