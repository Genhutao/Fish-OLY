package world

import (
	"fmt"
	"math/rand"
	"sort"
	"strings"
)

// ── 确定性种子 ────────────────────────────────────────────────────────────

// hashSeed = H(user, persona, day)。同一输入永远生成同一张日程（v0.3 §04）。
func hashSeed(parts ...string) uint64 {
	h := uint64(1469598103934665603)
	for _, p := range parts {
		for i := 0; i < len(p); i++ {
			h ^= uint64(p[i])
			h *= 1099511628211
		}
		h ^= 0xFF
		h *= 1099511628211
	}
	return h
}

// ── 计划生成 ──────────────────────────────────────────────────────────────

var intentDurations = []int{60, 90, 120, 150, 180, 240}

// genPlan 生成一人一天的日程：6-10 段，覆盖 [0,1440)。
// 纯函数：同 seed 同 scenes 同 others 必得到同一张表。
func genPlan(uid, personaID string, day int, seed uint64, scenes, others []string) *Plan {
	rng := rand.New(rand.NewSource(int64(seed)))
	p := &Plan{PersonaID: personaID, Day: day, Seed: seed}

	home := scenes[0]
	cur := 0
	for i := 0; cur < TicksPerDay; i++ {
		dur := intentDurations[rng.Intn(len(intentDurations))]
		if cur+dur > TicksPerDay {
			dur = TicksPerDay - cur
		}
		if dur < 20 {
			break
		}
		sc := home
		if i > 0 && rng.Float64() < 0.7 {
			sc = scenes[rng.Intn(len(scenes))]
		}
		it := Intent{
			ID:      fmt.Sprintf("i-%s-%d-%02d", shortHash(seed, i), day, i),
			SceneID: sc, Start: cur, End: cur + dur, Kind: "solo", Status: "planned",
		}
		p.Intents = append(p.Intents, it)
		cur += dur
	}

	// 串门声明：mid-day 插一段 visit（v0.3 §05 步骤 1）。
	if len(others) > 0 && rng.Float64() < 0.6 && len(p.Intents) > 2 {
		vidx := 1 + rng.Intn(len(p.Intents)-2)
		host := others[rng.Intn(len(others))]
		dur := 30 + rng.Intn(4)*15 // 30..75
		base := p.Intents[vidx].Start + rng.Intn(maxInt(1, p.Intents[vidx].End-p.Intents[vidx].Start-dur))
		visit := Intent{
			ID:      fmt.Sprintf("v-%s-%d", shortHash(seed, 99), day),
			SceneID: "", Start: base, End: base + dur,
			Kind: "visit", Target: host, Status: "planned",
		}
		head := append([]Intent(nil), p.Intents[:vidx+1]...)
		tail := append([]Intent(nil), p.Intents[vidx+1:]...)
		p.Intents = append(append(head, visit), tail...) // visit 覆盖原 intent 的时间段，解析时再定场景
	}
	return p
}

func shortHash(seed uint64, i int) string {
	return fmt.Sprintf("%04x", (seed>>16)^(uint64(i)*0x9E37))
}

// ── 租约窗口：一个角色的自由时间块 ────────────────────────────────────────

// windows 维护一个角色当天还没被会话占用的时间段。
// claim 切走 [s,e)；由此保证同一角色任一 tick 只属于一个会话（C1）。
type windows struct {
	segs [][2]int
}

func newWindows(intents []Intent) *windows {
	w := &windows{}
	for _, it := range intents {
		w.segs = append(w.segs, [2]int{it.Start, it.End})
	}
	sort.Slice(w.segs, func(i, j int) bool { return w.segs[i][0] < w.segs[j][0] })
	return w
}

func (w *windows) claim(s, e int) {
	out := [][2]int{}
	for _, seg := range w.segs {
		a, b := seg[0], seg[1]
		if e <= a || s >= b {
			out = append(out, seg)
			continue
		}
		if a < s {
			out = append(out, [2]int{a, s})
		}
		if e < b {
			out = append(out, [2]int{e, b})
		}
	}
	w.segs = out
}

func (w *windows) overlaps(s, e int) int {
	total := 0
	for _, seg := range w.segs {
		lo, hi := maxInt(seg[0], s), minInt(seg[1], e)
		if hi > lo {
			total += hi - lo
		}
	}
	return total
}

func (w *windows) bestOverlap(s, e int) (int, int, int) {
	best, bs, be := 0, -1, -1
	for _, seg := range w.segs {
		lo, hi := maxInt(seg[0], s), minInt(seg[1], e)
		if hi-lo > best {
			best, bs, be = hi-lo, lo, hi
		}
	}
	return best, bs, be
}

// ── 交集解析 ──────────────────────────────────────────────────────────────

const (
	minOverlap   = 20 // 交集最少 20 tick（v0.3 §05）
	visitShift   = 60 // 串门对表允许 ±60 tick 微调
	pairDayLimit = 1  // 同一天同一对角色最多一次 pair
)

// resolveDay 把一天的计划折叠成会话：串门对表 → 交集折叠 → 剩余块成 solo。
// 确定性：输入（计划表）相同，输出（会话集合）相同。
func resolveDay(u *UserWorld, day int) {
	st := u.St
	pids := st.OrderPersonaIDs()
	if len(pids) == 0 {
		return
	}

	// 收集每个人的计划与自由窗口。
	plans := map[string]*Plan{}
	ws := map[string]*windows{}
	scenesOf := map[string][]string{}
	for _, pid := range pids {
		pl := st.PlanOf(pid, day)
		if pl == nil {
			continue
		}
		plans[pid] = pl
		// 自由窗只算常规 intent；visit 的时段由对表去认领。
		regular := []Intent{}
		for _, it := range pl.Intents {
			if it.Kind != "visit" {
				regular = append(regular, it)
			}
		}
		ws[pid] = newWindows(regular)
		ss := map[string]bool{}
		for _, it := range pl.Intents {
			ss[it.SceneID] = true
		}
		list := make([]string, 0, len(ss))
		for s := range ss {
			list = append(list, s)
		}
		sort.Strings(list)
		scenesOf[pid] = list
	}

	pairDone := map[string]bool{}
	newSess := []*Session{}

	// 1) 串门对表：guest 的 visit 找 host 的自由窗，重叠 ≥20，±60 微调。
	for _, g := range pids {
		pl := plans[g]
		if pl == nil {
			continue
		}
		for vi := range pl.Intents {
			v := &pl.Intents[vi]
			if v.Kind != "visit" || v.Status != "planned" {
				continue
			}
			h := v.Target
			if plans[h] == nil || pairDone[g+"→"+h] {
				v.Status = "notmet"
				continue
			}
			found := false
			for _, shift := range []int{0, -15, 15, -30, 30, -60, 60} {
				s, e := v.Start+shift, v.End+shift
				if s < 0 || e > TicksPerDay {
					continue
				}
				ov, bs, be := ws[h].bestOverlap(s, e)
				if ov < minOverlap {
					continue
				}
				scene, _ := st.SceneInPlan(h, day, (bs+be)/2)
				if scene == "" || !sceneHasRoom(st, scene, day, bs, be) {
					continue
				}
				ws[g].claim(bs, be)
				ws[h].claim(bs, be)
				v.Start, v.End, v.SceneID, v.Status = bs, be, scene, "met"
				sess := &Session{
					ID: sessID(u.ID, day, scene, bs, be, g, h), Day: day,
					SceneID: scene, Kind: "pair", Start: bs, End: be,
					Parts:  []Participant{{PersonaID: g, Role: "guest"}, {PersonaID: h, Role: "host"}},
					Status: "planned", NextTick: bs,
				}
				newSess = append(newSess, sess)
				pairDone[g+"→"+h] = true
				found = true
				break
			}
			if !found {
				v.Status = "notmet"
			}
		}
	}

	// 2) 计划期交集：同一场景、自由窗重叠 ≥20 → 折成 pair（每对每天最多一次）。
	for i := 0; i < len(pids); i++ {
		for j := i + 1; j < len(pids); j++ {
			a, b := pids[i], pids[j]
			if plans[a] == nil || plans[b] == nil || pairDone[a+"→"+b] || pairDone[b+"→"+a] {
				continue
			}
			s, e, scene := largestSharedScene(st, a, b, day, ws[a], ws[b])
			if e-s < minOverlap || scene == "" {
				continue
			}
			ws[a].claim(s, e)
			ws[b].claim(s, e)
			sess := &Session{
				ID: sessID(u.ID, day, scene, s, e, a, b), Day: day,
				SceneID: scene, Kind: "pair", Start: s, End: e,
				Parts:  []Participant{{PersonaID: a, Role: "host"}, {PersonaID: b, Role: "guest"}},
				Status: "planned", NextTick: s,
			}
			newSess = append(newSess, sess)
			pairDone[a+"→"+b] = true
		}
	}

	// 3) 剩余自由块 → solo 会话（模板推进）。
	for _, pid := range pids {
		if ws[pid] == nil {
			continue
		}
		for _, seg := range ws[pid].segs {
			if seg[1]-seg[0] < 15 {
				continue
			}
			scene, _ := st.SceneInPlan(pid, day, (seg[0]+seg[1])/2)
			if scene == "" {
				continue
			}
			newSess = append(newSess, &Session{
				ID: sessID(u.ID, day, scene, seg[0], seg[1], pid), Day: day,
				SceneID: scene, Kind: "solo", Start: seg[0], End: seg[1],
				Parts:  []Participant{{PersonaID: pid, Role: "host"}},
				Status: "planned", NextTick: seg[0],
			})
		}
	}

	// 落事件：visit 结果（met/notmet）写回计划、notmet 留信、session.created。
	for _, g := range pids {
		pl := plans[g]
		if pl == nil {
			continue
		}
		for _, it := range pl.Intents {
			if it.Kind == "visit" && it.Status != "planned" {
				u.append(Event{Type: "plan.amended", Data: map[string]any{
					"personaId": g, "day": day, "intentId": it.ID,
					"status": it.Status, "sceneId": it.SceneID,
					"start": it.Start, "end": it.End,
				}})
				if it.Status == "notmet" {
					u.noteNotMet(g, it.Target, day)
				}
			}
		}
	}
	for _, sess := range newSess {
		u.append(Event{Type: "session.created", Room: sess.SceneID, Data: map[string]any{"session": sess}})
	}
}

// sceneHasRoom 检查容量：窗口内该场景已有会话人数 +2 ≤ capacity。
func sceneHasRoom(st *UserState, sceneID string, day, s, e int) bool {
	sc := st.Scene(sceneID)
	if sc == nil {
		return false
	}
	used := 0
	for _, sess := range st.Sessions {
		if sess.SceneID != sceneID || sess.Day != day || sess.Status == "cancelled" {
			continue
		}
		if sess.Start < e && s < sess.End {
			used += len(sess.Parts)
		}
	}
	return used+2 <= sc.Capacity
}

// largestSharedScene 找两人自由窗在同一场景的最大重叠段。
func largestSharedScene(st *UserState, a, b string, day int, wa, wb *windows) (int, int, string) {
	bestS, bestE, bestScene := 0, 0, ""
	for _, sa := range wa.segs {
		for _, sb := range wb.segs {
			lo, hi := maxInt(sa[0], sb[0]), minInt(sa[1], sb[1])
			if hi-lo <= bestE-bestS {
				continue
			}
			sceneA, _ := st.SceneInPlan(a, day, (lo+hi)/2)
			sceneB, _ := st.SceneInPlan(b, day, (lo+hi)/2)
			if sceneA == sceneB && sceneA != "" {
				bestS, bestE, bestScene = lo, hi, sceneA
			}
		}
	}
	return bestS, bestE, bestScene
}

func sessID(uid string, day int, scene string, s, e int, parts ...string) string {
	key := fmt.Sprintf("%s|%d|%s|%d|%d|%s", uid, day, scene, s, e, strings.Join(parts, ","))
	return fmt.Sprintf("sess-%08x", hashSeed(key)%0xFFFFFFFF)
}

// noteNotMet 串门降级：留信 + 访客写一条「没见到」的记忆（v0.3 §05）。
func (u *UserWorld) noteNotMet(guest, host string, day int) {
	gp := u.St.Persona(guest)
	hp := u.St.Persona(host)
	if gp == nil || hp == nil {
		return
	}
	text := fmt.Sprintf("字条 · %s：来找过你，没遇上。改天再聊。", gp.Name)
	u.append(Event{Type: "message.created", Room: hp.Scene, Actor: "system",
		Text: text, Data: map[string]any{
			"id": "note-" + sessID(u.ID, day, "note", day, 0, guest, host), "name": "系统",
		}})
	u.rememberFor(guest, "E", fmt.Sprintf("去%s待的地方找过，没遇到，留了字条。", hp.Name), day+1, 2.4, []string{"visit:" + host})
}
