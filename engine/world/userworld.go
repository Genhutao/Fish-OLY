package world

import (
	"context"
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"oly/engine/memory"
)

// dailyBudget：每用户每天自主发言的模型调用上限（C5 预算先行）。
const dailyBudget = 40

// UserWorld = 事件日志 + 投影 + 每角色一个记忆库 + 生成器。
// 同一用户的 tick / chat / 记忆操作全部串行在 mu 上（v0.3 §04）。
type UserWorld struct {
	ID    string
	Log   *Log
	St    *UserState
	Mem   map[string]*memory.Store // personaID → 记忆库
	Gen   Generator
	dir   string
	mu    sync.Mutex // 串行化该用户的一切变更
	memMu sync.Mutex // 只保护 map 本身
}

func (u *UserWorld) Day() int       { return DayOf(u.St.Tick) }
func (u *UserWorld) TickOfDay() int { return TickOfDay(u.St.Tick) }

func (u *UserWorld) append(ev Event) Event {
	out, err := u.Log.Append(ev)
	if err != nil {
		return ev
	}
	u.St.Apply(out)
	return out
}

// ── Scheduler：所有用户世界的容器 ─────────────────────────────────────────

type Scheduler struct {
	mu       sync.Mutex
	worlds   map[string]*UserWorld
	DataDir  string
	TickStep time.Duration // 1 tick = 多长真实时间；默认 1m，演示可压到秒级
	newGen   Generator     // 新建世界时挂的生成器
	stop     chan struct{}
	wg       sync.WaitGroup
}

func NewScheduler(dataDir string, tickStep time.Duration) *Scheduler {
	if tickStep <= 0 {
		tickStep = time.Minute
	}
	return &Scheduler{
		worlds:   map[string]*UserWorld{},
		DataDir:  dataDir,
		TickStep: tickStep,
		stop:     make(chan struct{}),
	}
}

// World 取（或建）一个用户世界：开日志 → 重放 → 记忆重放 → 生成今日计划 → 起调度协程。
func (sc *Scheduler) World(uid string) (*UserWorld, error) {
	sc.mu.Lock()
	defer sc.mu.Unlock()
	if w, ok := sc.worlds[uid]; ok {
		return w, nil
	}
	dir := filepath.Join(sc.DataDir, uid)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}
	log, err := OpenLog(filepath.Join(dir, "events.jsonl"))
	if err != nil {
		return nil, err
	}
	st := NewUserState(uid)
	u := &UserWorld{
		ID: uid, Log: log, St: st, dir: dir,
		Mem: map[string]*memory.Store{}, Gen: sc.newGen,
	}

	evs := log.Events(0)
	for _, ev := range evs {
		st.Apply(ev)
	}
	if len(evs) == 0 {
		u.seed()
	}
	for _, ev := range evs {
		u.applyMemoryEvent(ev)
	}
	u.ensurePlans(u.Day())
	sc.worlds[uid] = u
	sc.wg.Add(1)
	go sc.run(u)
	return u, nil
}

func (sc *Scheduler) SetGenerator(gen Generator) {
	sc.mu.Lock()
	defer sc.mu.Unlock()
	sc.newGen = gen
	for _, u := range sc.worlds {
		u.Gen = gen
	}
}

// Shutdown 停掉所有调度协程。
func (sc *Scheduler) Shutdown() {
	close(sc.stop)
	sc.wg.Wait()
}

// run 是每用户一个的调度协程：每 10 秒对齐一次应到 tick（v0.3 §04）。
func (sc *Scheduler) run(u *UserWorld) {
	defer sc.wg.Done()
	t := time.NewTicker(10 * time.Second)
	defer t.Stop()
	for {
		select {
		case <-sc.stop:
			return
		case <-t.C:
			u.AdvanceTo(realTick(u.St.Epoch, sc.TickStep), false)
		}
	}
}

// realTick 由 epoch（tick 0 = 本地 04:00 附近的世界起点）换算当前应到的绝对 tick。
func realTick(epoch time.Time, step time.Duration) int {
	if epoch.IsZero() {
		return 0
	}
	n := int(time.Since(epoch) / step)
	if n < 0 {
		n = 0
	}
	return n
}

// ── 播种 ──────────────────────────────────────────────────────────────────

func (u *UserWorld) seed() {
	u.append(Event{Type: "world.epoch", Data: map[string]any{"at": time.Now().Format(time.RFC3339)}})

	personas := []struct {
		id, name, avatar, anchor, personality, scene, mood string
		energy, affection                                  float64
	}{
		{"p-chizuru", "苍月千鹤", "🌙", "用疏离掩藏温柔，从不先承认在意",
			"身份：没落贵族之后 | 兴趣：古琴、剑术、夜观星象 | 观点：人心险恶，不如月光清净", "scene-court", "calm", 74, 3.5},
		{"p-el", "白昼·艾尔", "☀️", "用过度热情掩藏对被遗弃的恐惧",
			"身份：魔法学院三年级 | 兴趣：奇怪魔法道具、和人聊天 | 观点：世界本质上是温暖的", "scene-academy", "happy", 66, 6.0},
	}
	for _, p := range personas {
		u.append(Event{Type: "persona.created", Actor: p.id, Data: map[string]any{
			"name": p.name, "avatar": p.avatar, "anchor": p.anchor,
			"personality": p.personality, "scene": p.scene, "mood": p.mood,
			"energy": p.energy, "affection": p.affection, "activity": "刚走进这里",
		}})
	}

	scenes := []struct {
		id, name, topic string
		capacity        int
	}{
		{"scene-court", "月下庭院", "青石铺地，桂影婆娑，风穿过回廊。", 4},
		{"scene-academy", "魔法学院中庭", "浮空灯与咒文余烬，永远有人在炸炉子。", 5},
		{"scene-bookshop", "旧书店", "一盏台灯，两张椅子，安静得能听见翻页。", 2},
	}
	for _, s := range scenes {
		u.append(Event{Type: "scene.created", Room: s.id, Data: map[string]any{
			"name": s.name, "topic": s.topic, "capacity": s.capacity, "visibility": "public",
		}})
	}

	u.append(Event{Type: "message.created", Room: "scene-court", Actor: "system",
		Text: "【庭院的夜风正好，桂影落在青石上。】",
		Data: map[string]any{"id": "seed-msg-1", "name": "系统"}})

	// 两条「历史记忆」：用户偏好写进每个角色的库（C7：各记各的）。
	for _, p := range personas {
		u.rememberFor(p.id, "E", "用户说过喜欢在夜里听雨声，说那样容易睡着。", 1, 3.2, []string{"seed"})
		u.rememberFor(p.id, "R", "用户不喜欢别人替他做决定。", 1, 3.8, []string{"seed"})
	}
}

// ensurePlans 生成今天所有角色的计划并解析会话（幂等）。
func (u *UserWorld) ensurePlans(day int) {
	pids := u.St.OrderPersonaIDs()
	sceneIDs := u.St.SceneIDs()
	if len(pids) == 0 || len(sceneIDs) == 0 {
		return
	}
	changed := false
	for _, pid := range pids {
		if u.St.HasPlan(pid, day) {
			continue
		}
		others := []string{}
		for _, o := range pids {
			if o != pid {
				others = append(others, o)
			}
		}
		seed := hashSeed(u.ID, pid, fmt.Sprint(day))
		pl := genPlan(u.ID, pid, day, seed, sceneIDs, others)
		u.append(Event{Type: "plan.generated", Data: map[string]any{
			"personaId": pid, "day": day, "seed": float64(seed), "intents": intentsToAny(pl.Intents),
		}})
		changed = true
	}
	if changed {
		resolveDay(u, day)
	}
}

func intentsToAny(list []Intent) []any {
	out := make([]any, 0, len(list))
	for _, it := range list {
		out = append(out, map[string]any{
			"id": it.ID, "sceneId": it.SceneID, "start": it.Start, "end": it.End,
			"kind": it.Kind, "target": it.Target, "status": it.Status,
		})
	}
	return out
}

// ── 推进：advanceTo（带快进与补偿）────────────────────────────────────────

func (u *UserWorld) AdvanceTo(target int, backfill bool) {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.advanceLocked(target, backfill)
}

// TickOnce 手动推进一拍。
func (u *UserWorld) TickOnce() {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.advanceLocked(u.St.Tick+1, false)
}

func (u *UserWorld) advanceLocked(target int, backfill bool) {
	if target <= u.St.Tick {
		return
	}
	// 超过 3 天的断层：世界睡了——只补日程与状态，不补会话内容（v0.3 §08）。
	if target-u.St.Tick > 3*TicksPerDay {
		from := u.St.Tick
		u.append(Event{Type: "world.dormant", Data: map[string]any{"from": float64(from), "to": float64(target)}})
		u.append(Event{Type: "world.tick", Data: map[string]any{"tick": float64(target)}})
		u.ensurePlans(u.Day())
		// 让角色的「当前位置」跟到 target。
		for _, pid := range u.St.OrderPersonaIDs() {
			scene, _ := u.St.SceneAt(pid, u.Day(), TickOfDay(target))
			if scene != "" {
				u.append(Event{Type: "persona.state", Actor: pid, Data: map[string]any{"scene": scene}})
			}
		}
		return
	}
	for t := u.St.Tick + 1; t <= target; t++ {
		u.stepTick(t, backfill && target-t > 30)
	}
}

// stepTick 推进一分钟：激活会话 → 推进活跃会话 → 过期收尾 → world.tick。
func (u *UserWorld) stepTick(tick int, backfill bool) {
	day, m := DayOf(tick), TickOfDay(tick)
	u.ensurePlans(day)
	if m == 0 {
		u.St.Budget = 0 // 预算按日重置（运行态，不进事件）
	}

	for _, sess := range u.sessionList() {
		if sess.Status == "planned" && sess.Day == day && sess.Start <= m {
			u.append(Event{Type: "session.progress", Data: map[string]any{
				"id": sess.ID, "status": "active", "nextTick": float64(m), "backfilled": backfill,
			}})
		}
	}
	for _, sess := range u.sessionList() {
		if sess.Status != "active" || sess.Day != day || m < sess.Start || m >= sess.End {
			continue
		}
		if m-sess.NextTick >= 5 {
			u.runSession(sess, tick, m, backfill)
		}
	}
	for _, sess := range u.sessionList() {
		if sess.Status == "active" && sess.Day == day && m >= sess.End {
			u.closeSession(sess, backfill)
		}
	}
	u.append(Event{Type: "world.tick", Data: map[string]any{"tick": float64(tick)}})
}

func (u *UserWorld) sessionList() []*Session {
	out := make([]*Session, 0, 8)
	for _, s := range u.St.Sessions {
		out = append(out, s)
	}
	return out
}

// ── 会话执行 ──────────────────────────────────────────────────────────────

var soloActivities = map[string][]string{
	"scene-court":    {"把剑横在膝上，听风穿过回廊。", "对着月亮弹了一小段没人听的曲子。", "在石阶上坐了很久，直到露水打湿衣角。"},
	"scene-academy":  {"追着一只失控的魔法纸鹤跑过走廊。", "点亮新学的光系咒文，又慌忙吹灭。", "被教授抓去整理实验器材。"},
	"scene-bookshop": {"从书架最上层抽出一本积灰的旧书。", "在扉页发现一行陌生批注。"},
}

var soloLines = map[string][]string{
	"p-chizuru": {"今晚的月亮，比昨天冷一点。", "风里有桂花的味道。", "……别站在风口。", "琴弦受潮了，音不太准。"},
	"p-el":      {"教授说实战考核改到黄昏了，救命。", "食堂新出的奶油面包真的很好吃！", "谁把我的符文笔记拿走了？", "我刚刚点亮了第三个光球！"},
}

var pairLines = map[string][]string{
	"p-chizuru": {"{name}，你又在瞎折腾什么？", "……坐吧，别挡着光。", "你说话总是这么大声。"},
	"p-el":      {"{name}！我跟你说，今天学院可有意思了！", "{name}你闻到没有，桂花开了！", "嘿嘿，{name}你也会来这种地方呀。"},
}

// attention 是发言权打分。全部来自本地字段，不调模型。
func attention(p *Persona, tick int) float64 {
	rel := (p.Affection + 10) / 20 * 0.18
	en := p.Energy / 100 * 0.10
	bond := 0.0
	if p.Bond != nil {
		for _, v := range p.Bond {
			bond += v
		}
		bond = clampf(bond/10.0, -0.2, 0.2)
	}
	cool := 0.0
	if tick-p.LastSpoke < 3 {
		cool = 0.42
	}
	return 0.16 + rel + en + bond - cool + rand.Float64()*0.30
}

// runSession 推进一拍会话：solo 模板；pair 可调模型，预算不足或失败则降级（C5）。
func (u *UserWorld) runSession(sess *Session, tick, m int, backfill bool) {
	if sess.Kind == "solo" {
		u.runSolo(sess, tick, backfill)
		return
	}

	// 谁开口（C6：分数进事件）。
	best, bestScore := "", 0.0
	for _, part := range sess.Parts {
		p := u.St.Persona(part.PersonaID)
		if p == nil {
			continue
		}
		if s := attention(p, tick); s > bestScore {
			best, bestScore = p.ID, s
		}
	}
	if best == "" {
		return
	}

	// 预算先行（C5）：超预算 → degraded，只出模板。
	if backfill || u.St.Budget >= dailyBudget || u.Gen == nil || !u.Gen.Real() {
		if u.St.Budget >= dailyBudget || backfill {
			u.append(Event{Type: "session.progress", Data: map[string]any{
				"id": sess.ID, "nextTick": float64(m), "degraded": true, "said": float64(sess.Said),
			}})
		}
		u.speakTemplate(sess, tick, best, backfill)
		return
	}
	u.St.Budget++

	p := u.St.Persona(best)
	room := u.St.Scene(sess.SceneID)
	history := ClipRecent(u.St.RoomMessages(sess.SceneID, 10), 8, 700)
	hits := u.MemOf(best).Search(u.lastSceneText(sess.SceneID), 2, u.Day()+1)
	system, memBlock := Assemble(*p, room, hits, 80)
	peer := peerID(sess, best)
	userText := fmt.Sprintf("（场景进行中，以「%s」的身份自然地说一句，不要长篇大论。）", p.Name)
	if op := u.St.Persona(peer); op != nil {
		userText = fmt.Sprintf("（场景进行中。你和 %s 正在%s闲聊，以「%s」的身份自然地说一句，不要长篇大论。）",
			op.Name, roomName(room), p.Name)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	out, mock, err := u.Gen.Reply(ctx, GenerateRequest{
		Persona: *p, Room: room, System: system, MemBlock: memBlock,
		History: history, UserText: userText,
	})
	if err != nil {
		u.append(Event{Type: "session.progress", Data: map[string]any{
			"id": sess.ID, "nextTick": float64(m), "degraded": true, "said": float64(sess.Said),
		}})
		u.speakTemplate(sess, tick, best, false)
		return
	}
	u.emitSpeak(sess, best, p.Name, out, tick, mock, bestScore)
}

// runSolo：solo 只漂状态 + 偶尔模板自言自语，绝不调模型（v0.3 §06）。
func (u *UserWorld) runSolo(sess *Session, tick int, backfill bool) {
	pid := sess.Host()
	p := u.St.Persona(pid)
	if p == nil {
		return
	}
	acts := soloActivities[sess.SceneID]
	activity := p.Activity
	if len(acts) > 0 {
		activity = acts[int(rand.Int63())%len(acts)]
	}
	energy := clampf(p.Energy-0.4, 10, 100)
	mood := p.Mood
	switch sess.SceneID {
	case "scene-court":
		mood = "calm"
	case "scene-academy":
		mood = "happy"
	case "scene-bookshop":
		mood = "calm"
	}
	u.append(Event{Type: "persona.state", Actor: pid, Data: map[string]any{
		"energy": energy, "mood": mood, "activity": activity, "scene": sess.SceneID,
	}})
	if !backfill && sess.Said < 2 && rand.Float64() < 0.12 {
		u.speakTemplate(sess, tick, pid, false)
	}
}

// speakTemplate 会话内的模板台词（solo / 降级 / 快进共用）。
func (u *UserWorld) speakTemplate(sess *Session, tick int, pid string, backfill bool) {
	p := u.St.Persona(pid)
	if p == nil {
		return
	}
	var line string
	if sess.Kind == "pair" {
		// 对方 = 会话里除了自己之外的参与者。
		if op := u.St.Persona(peerID(sess, pid)); op != nil {
			lines := pairLines[pid]
			if len(lines) > 0 {
				line = strings.ReplaceAll(lines[int(rand.Int63())%len(lines)], "{name}", op.Name)
			}
		}
	} else {
		lines := soloLines[pid]
		if len(lines) > 0 {
			line = lines[int(rand.Int63())%len(lines)]
		}
	}
	if line == "" {
		line = "（安静地待了一会儿）"
	}
	u.emitSpeak(sess, pid, p.Name, line, tick, true, 0)
}

func roomName(r *Room) string {
	if r == nil {
		return "共享的场景里"
	}
	return r.Name
}

// peerID 返回会话里「对方」的角色 id。
func peerID(sess *Session, pid string) string {
	if sess.Guest() == pid {
		return sess.Host()
	}
	return sess.Guest()
}

// emitSpeak 落一条发言事件 + lastSpoke + 会话游标。
func (u *UserWorld) emitSpeak(sess *Session, pid, name, text string, tick int, mock bool, score float64) {
	u.append(Event{Type: "message.created", Room: sess.SceneID, Actor: pid, Text: text,
		Data: map[string]any{
			"id": fmt.Sprintf("m-%s-%d", sess.ID, tick), "name": name, "mock": mock,
			"session": sess.ID, "score": score,
		}})
	u.append(Event{Type: "persona.state", Actor: pid, Data: map[string]any{"lastSpoke": float64(tick)}})
	u.append(Event{Type: "session.progress", Data: map[string]any{
		"id": sess.ID, "nextTick": float64(tick), "said": float64(sess.Said + 1),
	}})
}

// lastSceneText 取场景里最近一条非系统发言，作为记忆检索 query。
func (u *UserWorld) lastSceneText(sceneID string) string {
	msgs := u.St.RoomMessages(sceneID, 6)
	for i := len(msgs) - 1; i >= 0; i-- {
		if msgs[i].Actor != "system" {
			return msgs[i].Text
		}
	}
	return ""
}

// closeSession 收尾：pair 写两端记忆 + 关系边；solo 只收档（C7：各记各的）。
func (u *UserWorld) closeSession(sess *Session, backfill bool) {
	scene := u.St.Scene(sess.SceneID)
	sname := "共享场景"
	if scene != nil {
		sname = scene.Name
	}
	if sess.Kind == "pair" {
		g, h := sess.Guest(), sess.Host()
		gp, hp := u.St.Persona(g), u.St.Persona(h)
		if gp != nil && hp != nil {
			window := fmt.Sprintf("%s–%s", Clock(sess.Start), Clock(sess.End))
			tag := ""
			if backfill || sess.Backfilled {
				tag = "（补记）"
			}
			u.rememberFor(h, "K", fmt.Sprintf("%s%s来%s找过我，我们待了一阵。", tag, gp.Name, sname), u.Day()+1, 3.6, []string{sess.ID})
			u.rememberFor(h, "E", fmt.Sprintf("%s和%s在%s待到%s。", tag, gp.Name, sname, window), u.Day()+1, 2.8, []string{sess.ID})
			u.rememberFor(g, "K", fmt.Sprintf("%s我去%s找%s，一起待了一阵。", tag, sname, hp.Name), u.Day()+1, 3.6, []string{sess.ID})
			u.rememberFor(g, "E", fmt.Sprintf("%s在%s和%s共处到%s。", tag, sname, hp.Name, window), u.Day()+1, 2.8, []string{sess.ID})
			// 关系边：单次上限 0.3（v0.3 §07）
			u.append(Event{Type: "relation.delta", Actor: h, Data: map[string]any{"peer": g, "delta": 0.25}})
			u.append(Event{Type: "relation.delta", Actor: g, Data: map[string]any{"peer": h, "delta": 0.25}})
		}
	}
	u.append(Event{Type: "session.ended", Data: map[string]any{
		"id": sess.ID, "summary": fmt.Sprintf("%s 会话结束（%d 条发言）", sname, sess.Said),
		"backfilled": backfill || sess.Backfilled,
	}})
}

// rememberFor 给指定角色的记忆库写入并落事件（事件里带 personaId，重放才知道写谁）。
func (u *UserWorld) rememberFor(pid, layer, text string, day int, imp float64, refs []string) {
	r := u.MemOf(pid).WriteDetailed(layer, text, day, imp, refs)
	if r.Action == "dup" {
		return
	}
	u.append(Event{Type: "memory.remember", Data: map[string]any{
		"personaId": pid, "slotId": r.SlotID, "memberId": r.MemberID, "layer": layer,
		"text": text, "day": day, "imp": imp, "w": r.W, "refs": refs, "action": r.Action,
	}})
}

// Forget 撤回一条成员并落事件。
func (u *UserWorld) Forget(memberID string) error {
	u.mu.Lock()
	defer u.mu.Unlock()
	for pid, mem := range u.memSnapshot() {
		if err := mem.Forget(memberID); err == nil {
			u.append(Event{Type: "memory.forget", Data: map[string]any{"personaId": pid, "memberId": memberID}})
			u.SaveMemory(pid)
			return nil
		}
	}
	return fmt.Errorf("member not found: %s", memberID)
}

// Revive 恢复一条成员并落事件。
func (u *UserWorld) Revive(memberID string) error {
	u.mu.Lock()
	defer u.mu.Unlock()
	for pid, mem := range u.memSnapshot() {
		if err := mem.Revive(memberID); err == nil {
			u.append(Event{Type: "memory.revive", Data: map[string]any{"personaId": pid, "memberId": memberID}})
			u.SaveMemory(pid)
			return nil
		}
	}
	return fmt.Errorf("member not found: %s", memberID)
}

// Consolidate 跑一次夜间批处理（所有角色），明细进事件可重放。
func (u *UserWorld) Consolidate() []memory.ConsolidateReport {
	u.mu.Lock()
	defer u.mu.Unlock()
	reps := []memory.ConsolidateReport{}
	for _, pid := range u.St.OrderPersonaIDs() {
		mem := u.MemOf(pid)
		rep := mem.Consolidate(u.Day() + 1)
		if rep.Merged+rep.Forgotten+rep.Rebalanced > 0 {
			u.append(Event{Type: "memory.consolidated", Data: map[string]any{
				"personaId": pid, "report": rep,
			}})
		}
		reps = append(reps, rep)
		u.SaveMemory(pid)
	}
	return reps
}

func (u *UserWorld) memSnapshot() map[string]*memory.Store {
	u.memMu.Lock()
	defer u.memMu.Unlock()
	out := make(map[string]*memory.Store, len(u.Mem))
	for k, v := range u.Mem {
		out[k] = v
	}
	return out
}

// ── 记忆事件重放 ──────────────────────────────────────────────────────────

func (u *UserWorld) applyMemoryEvent(ev Event) {
	pid := str(ev.Data["personaId"])
	if pid == "" {
		pid = ev.Actor
	}
	// 旧单世界版的事件没有 personaId（当时记忆是世界级的）。
	// 那份「过去」属于用户与每个角色的共同经历，让所有角色都继承它。
	if pid == "" {
		for _, p := range u.St.OrderPersonaIDs() {
			u.applyMemoryEventFor(p, ev)
		}
		return
	}
	u.applyMemoryEventFor(pid, ev)
}

func (u *UserWorld) applyMemoryEventFor(pid string, ev Event) {
	mem := u.MemOf(pid)
	switch ev.Type {
	case "memory.remember":
		mem.ApplyWrite(
			str(ev.Data["slotId"]), str(ev.Data["memberId"]), str(ev.Data["layer"]),
			str(ev.Data["text"]), int(num(ev.Data["day"])), num(ev.Data["imp"]), num(ev.Data["w"]),
			strs(ev.Data["refs"]),
		)
	case "memory.forget":
		mem.ApplyMemberState(str(ev.Data["memberId"]), "forgotten")
	case "memory.revive":
		mem.ApplyMemberState(str(ev.Data["memberId"]), "active")
	case "memory.consolidated":
		if rep, ok := decodeReport(ev.Data["report"]); ok {
			mem.ApplyConsolidate(rep)
		}
	}
}

// MemOf 取角色的记忆库（懒建）。
func (u *UserWorld) MemOf(pid string) *memory.Store {
	u.memMu.Lock()
	defer u.memMu.Unlock()
	m := u.Mem[pid]
	if m == nil {
		m = memory.NewStore()
		u.Mem[pid] = m
	}
	return m
}

// LoadMemory 从磁盘读角色的记忆缓存（缺失就靠事件重放）。
func (u *UserWorld) LoadMemory(pid string) {
	p := filepath.Join(u.dir, "memory-"+pid+".json")
	if err := u.MemOf(pid).Load(p); err != nil {
		fmt.Printf("记忆缓存加载失败（按事件重放）: %v\n", err)
	}
}

// SaveMemory 把角色的记忆缓存落盘。
func (u *UserWorld) SaveMemory(pid string) {
	if err := u.MemOf(pid).Save(filepath.Join(u.dir, "memory-"+pid+".json")); err != nil {
		fmt.Printf("记忆落盘失败: %v\n", err)
	}
}

// LoadAllMemories 启动时把已知角色的记忆缓存都读一遍。
func (u *UserWorld) LoadAllMemories() {
	for _, pid := range u.St.OrderPersonaIDs() {
		u.LoadMemory(pid)
	}
}
