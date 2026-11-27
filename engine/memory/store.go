package memory

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"sync"
	"time"
)

// 六层。K 关系最重要，M 元记忆最不重要。
var LayerWeight = map[string]float64{
	"K": 1.15, "R": 1.05, "E": 1.05, "F": 1.00, "WM": 0.90, "M": 0.85,
}

var Layers = []string{"K", "R", "E", "F", "WM", "M"}

const (
	maxBindings = 6
	mergeSim    = 0.92
	// nearSim 是「同一件事说得更细」的下限。VSA 原型在它自己的编码器上量出 0.62，
	// 这版 Go 编码器（同样的字+双字、不同 RNG）实测同义扩写落在 0.60 附近，
	// 所以先按 0.55 上线——**移植完必须用 120 题回放重新校定这个数**。
	nearSim = 0.55
)

// Member 是一条不可变的记忆。槽向量由成员重算，所以撤销就是「改状态 + 重算」。
type Member struct {
	ID    string   `json:"id"`
	Text  string   `json:"text"`
	Day   int      `json:"day"`
	Imp   float64  `json:"imp"`
	W     float64  `json:"w"`
	Refs  []string `json:"refs,omitempty"`
	State string   `json:"state"` // active | forgotten
}

// Slot 是一组近重复记忆。它不含「真相」，只有成员。
type Slot struct {
	ID      string   `json:"id"`
	Layer   string   `json:"layer"`
	Members []Member `json:"members"`
	Day     int      `json:"day"`
	LastDay int      `json:"lastDay"`
	Imp0    float64  `json:"imp0"`
	Hits    int      `json:"hits"`
	Tags    []string `json:"tags"`

	cache Vec
	dirty bool
}

// Vec 是成员集合的纯函数：vec = bundle(member.vec × member.w)。
// 这里刻意没有「把新向量并进旧向量」的写入路径。
func (s *Slot) Vec() Vec {
	if !s.dirty && s.cache != (Vec{}) {
		return s.cache
	}
	var vs []Vec
	var ws []float64
	for _, m := range s.Members {
		if m.State == "forgotten" {
			continue
		}
		w := m.W
		if w <= 0 {
			w = 1
		}
		vs = append(vs, Encode(m.Text))
		ws = append(ws, w)
	}
	if len(vs) == 0 {
		s.cache = Vec{}
	} else {
		s.cache = Bundle(vs, ws)
	}
	s.dirty = false
	return s.cache
}

func (s *Slot) active() []Member {
	out := make([]Member, 0, len(s.Members))
	for _, m := range s.Members {
		if m.State != "forgotten" {
			out = append(out, m)
		}
	}
	return out
}

func (s *Slot) text() string {
	best := ""
	for _, m := range s.active() {
		if len([]rune(m.Text)) > len([]rune(best)) {
			best = m.Text
		}
	}
	return best
}

// refs 返回这个槽所有活跃成员的来源 id，评测与审计都用它。
func (s *Slot) refs() []string {
	out := make([]string, 0, 4)
	for _, m := range s.active() {
		for _, r := range m.Refs {
			if !contains(out, r) {
				out = append(out, r)
			}
		}
	}
	return out
}

// Hit 是一条检索结果。
type Hit struct {
	SlotID string   `json:"slotId"`
	Layer  string   `json:"layer"`
	Text   string   `json:"text"`
	Score  float64  `json:"score"`
	Tags   []string `json:"tags,omitempty"`
	Refs   []string `json:"refs,omitempty"`
}

// Store 是一层记忆库。
type Store struct {
	mu      sync.RWMutex
	slots   map[string]*Slot
	order   []string
	Encoder string `json:"encoder"`
	Rule    string `json:"rule"`

	// 阈值按编码器校定，所以放在实例上，评测时才能扫参数。
	MergeSim float64 `json:"mergeSim"`
	NearSim  float64 `json:"nearSim"`

	Writes  int `json:"writes"`
	Dups    int `json:"dups"`
	Merges  int `json:"merges"`
	Appends int `json:"appends"`
}

// NewStore 建一个空库。encoder/rule 版本必须落盘：规则一变，旧向量就不能再信任。
func NewStore() *Store {
	return &Store{
		slots:    map[string]*Slot{},
		Encoder:  "hash-v1",
		Rule:     "member-bundle-v1",
		MergeSim: mergeSim,
		NearSim:  nearSim,
	}
}

func (st *Store) layerSlot(layer string) []*Slot {
	out := make([]*Slot, 0, 16)
	for _, id := range st.order {
		s := st.slots[id]
		if s.Layer == layer && len(s.active()) > 0 {
			out = append(out, s)
		}
	}
	return out
}

// WriteResult 给调用方足够的信息把这次写入记成一条事件，
// 这样记忆库可以从事件重放，而不是只信任一个 JSON。
type WriteResult struct {
	SlotID   string  `json:"slotId"`
	MemberID string  `json:"memberId"`
	Action   string  `json:"action"` // new | merge | append | dup
	W        float64 `json:"w"`
}

// Write 保持旧签名，给测试与评测用。
func (st *Store) Write(layer, text string, day int, imp float64, refs []string) (string, string) {
	r := st.WriteDetailed(layer, text, day, imp, refs)
	return r.SlotID, r.Action
}

// WriteDetailed 写入一条记忆。动作 ∈ {new, merge, append, dup}。
// merge / append 的区别只在成员权重，写入路径不再改写槽向量。
func (st *Store) WriteDetailed(layer, text string, day int, imp float64, refs []string) WriteResult {
	if imp <= 0 {
		imp = 3
	}
	st.mu.Lock()
	defer st.mu.Unlock()
	st.Writes++

	v := Encode(text)
	var target *Slot
	best := -1.0
	for _, s := range st.layerSlot(layer) {
		sim := Sim(v, s.Vec())
		if sim > best {
			best, target = sim, s
		}
	}

	if target != nil && best > 0.999 {
		target.Hits++
		st.Dups++
		return WriteResult{SlotID: target.ID, Action: "dup"}
	}

	if target != nil && best > st.MergeSim && len(target.active()) < maxBindings {
		m := st.addMember(target, text, day, imp, 1.0, refs)
		st.Merges++
		return WriteResult{SlotID: target.ID, MemberID: m.ID, Action: "merge", W: 1.0}
	}
	if target != nil && best > st.NearSim && len(target.active()) < maxBindings {
		m := st.addMember(target, text, day, imp, 0.25, refs)
		st.Appends++
		return WriteResult{SlotID: target.ID, MemberID: m.ID, Action: "append", W: 0.25}
	}

	slot := &Slot{
		ID:      newID("slot"),
		Layer:   layer,
		Day:     day,
		LastDay: day,
		Imp0:    imp,
		Tags:    Tag(text),
		dirty:   true,
	}
	member := Member{
		ID: newID("mem"), Text: text, Day: day,
		Imp: imp, W: 1, Refs: refs, State: "active",
	}
	slot.Members = []Member{member}
	st.slots[slot.ID] = slot
	st.order = append(st.order, slot.ID)
	return WriteResult{SlotID: slot.ID, MemberID: member.ID, Action: "new", W: 1}
}

func (st *Store) addMember(s *Slot, text string, day int, imp, w float64, refs []string) Member {
	m := Member{
		ID: newID("mem"), Text: text, Day: day,
		Imp: imp, W: w, Refs: refs, State: "active",
	}
	s.Members = append(s.Members, m)
	s.LastDay = maxInt(s.LastDay, day)
	if imp > s.Imp0 {
		s.Imp0 = imp
	}
	s.dirty = true
	for _, t := range Tag(text) {
		if !contains(s.Tags, t) {
			s.Tags = append(s.Tags, t)
		}
	}
	return m
}

// Search 分层独立 top-k，再按分数合并。标签只做「词面零重合」时的保底。
func (st *Store) Search(query string, k int, nowDay int) []Hit {
	if k <= 0 {
		k = 3
	}
	st.mu.RLock()
	defer st.mu.RUnlock()

	qv := EncodeQuery(query)
	qtags := Tag(query)
	out := make([]Hit, 0, k*2)

	for _, layer := range Layers {
		for _, s := range st.layerSlot(layer) {
			sim := Sim(qv, s.Vec())
			age := float64(maxInt(0, nowDay-s.LastDay))
			strength := 30 * clamp(s.Imp0, 0.8, 4)
			decay := math.Pow(0.5, age/strength)
			score := sim * decay * (0.6 + 0.1*s.Imp0) * LayerWeight[layer]

			if len(qtags) > 0 {
				for _, t := range qtags {
					if contains(s.Tags, t) {
						boost := 0.06 + 1e-3*score
						if boost > score {
							score = boost
						}
						break
					}
				}
			}
			if score <= 0 {
				continue
			}
			out = append(out, Hit{
				SlotID: s.ID, Layer: s.Layer, Text: s.text(),
				Score: score, Tags: s.Tags, Refs: s.refs(),
			})
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Score > out[j].Score })
	if len(out) > k {
		out = out[:k]
	}
	return out
}

// Forget 撤回**一条成员**。槽里还有别的成员时，槽继续存在，向量重算。
func (st *Store) Forget(memberID string) error {
	st.mu.Lock()
	defer st.mu.Unlock()
	for _, s := range st.slots {
		for i := range s.Members {
			if s.Members[i].ID != memberID {
				continue
			}
			if s.Members[i].State == "forgotten" {
				return fmt.Errorf("这条已经撤回了")
			}
			s.Members[i].State = "forgotten"
			s.dirty = true
			return nil
		}
	}
	return fmt.Errorf("member not found: %s", memberID)
}

// Revive 把一条被撤回的成员放回去，向量同样重算。
func (st *Store) Revive(memberID string) error {
	st.mu.Lock()
	defer st.mu.Unlock()
	for _, s := range st.slots {
		for i := range s.Members {
			if s.Members[i].ID == memberID {
				s.Members[i].State = "active"
				s.dirty = true
				return nil
			}
		}
	}
	return fmt.Errorf("member not found: %s", memberID)
}

// Fingerprint 给演示面板看「向量真的变了」。取前 8 字节 + 置位数。
func (st *Store) Fingerprint(slotID string) string {
	st.mu.RLock()
	defer st.mu.RUnlock()
	s, ok := st.slots[slotID]
	if !ok {
		return "-"
	}
	v := s.Vec()
	n := 0
	for _, w := range v {
		n += popcount(w)
	}
	return fmt.Sprintf("%08x…%04d", uint32(v[0]), n)
}

// SlotView 是给 UI 的只读投影。
type SlotView struct {
	ID      string   `json:"id"`
	Layer   string   `json:"layer"`
	Text    string   `json:"text"`
	Members []Member `json:"members"`
	Hits    int      `json:"hits"`
	Day     int      `json:"day"`
	LastDay int      `json:"lastDay"`
	Imp0    float64  `json:"imp0"`
	Tags    []string `json:"tags"`
	Vec     string   `json:"vec"`
}

func (st *Store) Slots() []SlotView {
	st.mu.RLock()
	defer st.mu.RUnlock()
	out := make([]SlotView, 0, len(st.order))
	for _, id := range st.order {
		s := st.slots[id]
		out = append(out, SlotView{
			ID: s.ID, Layer: s.Layer, Text: s.text(), Members: s.Members,
			Hits: s.Hits, Day: s.Day, LastDay: s.LastDay, Imp0: s.Imp0,
			Tags: s.Tags, Vec: st.fingerprintLocked(s),
		})
	}
	return out
}

func (st *Store) fingerprintLocked(s *Slot) string {
	v := s.Vec()
	n := 0
	for _, w := range v {
		n += popcount(w)
	}
	return fmt.Sprintf("%08x…%04d", uint32(v[0]), n)
}

// Stats 给顶栏。
type Stats struct {
	Slots     int `json:"slots"`
	Members   int `json:"members"`
	Active    int `json:"active"`
	Forgotten int `json:"forgotten"`
	Writes    int `json:"writes"`
	Dups      int `json:"dups"`
	Merges    int `json:"merges"`
	Appends   int `json:"appends"`
}

func (st *Store) Stats() Stats {
	st.mu.RLock()
	defer st.mu.RUnlock()
	out := Stats{Slots: len(st.order), Writes: st.Writes, Dups: st.Dups,
		Merges: st.Merges, Appends: st.Appends}
	for _, s := range st.slots {
		for _, m := range s.Members {
			out.Members++
			if m.State == "forgotten" {
				out.Forgotten++
			} else {
				out.Active++
			}
		}
	}
	return out
}

// ── 巩固：合并 → 升降 → 遗忘，顺序不可换 ────────────────────────────────

type MergeMove struct {
	From string `json:"from"`
	To   string `json:"to"`
}

type ConsolidateReport struct {
	Merged       int                `json:"merged"`
	Rebalanced   int                `json:"rebalanced"`
	Forgotten    int                `json:"forgotten"`
	SlotsAfter   int                `json:"slotsAfter"`
	Moves        []MergeMove        `json:"moves,omitempty"`
	ForgottenIDs []string           `json:"forgottenIds,omitempty"`
	ImpUpdates   map[string]float64 `json:"impUpdates,omitempty"`
}

// consolidateSim 是「两个槽讲的是同一件事」的阈值。
// 0.86 是 VSA 在 90 天模拟里量出来的，低于它误并开始上升。
const consolidateSim = 0.86

// Consolidate 是夜间批处理。合并必须在遗忘之前：
// 否则同一件事的五条重复会各自衰减到阈值以下，然后被逐条删掉。
func (st *Store) Consolidate(nowDay int) ConsolidateReport {
	st.mu.Lock()
	defer st.mu.Unlock()
	rep := ConsolidateReport{ImpUpdates: map[string]float64{}}

	for _, layer := range Layers {
		slots := st.layerSlot(layer)
		used := map[string]bool{}
		for i := 0; i < len(slots); i++ {
			a := slots[i]
			if used[a.ID] {
				continue
			}
			for j := i + 1; j < len(slots); j++ {
				b := slots[j]
				if used[b.ID] {
					continue
				}
				if len(a.active())+len(b.active()) > maxBindings {
					continue
				}
				if Sim(a.Vec(), b.Vec()) < consolidateSim {
					continue
				}
				// 把 b 的成员整条搬进 a，成员 id 与状态原样保留。
				a.Members = append(a.Members, b.Members...)
				a.LastDay = maxInt(a.LastDay, b.LastDay)
				if b.Imp0 > a.Imp0 {
					a.Imp0 = b.Imp0
				}
				for _, t := range b.Tags {
					if !contains(a.Tags, t) {
						a.Tags = append(a.Tags, t)
					}
				}
				a.dirty = true
				used[b.ID] = true
				delete(st.slots, b.ID)
				rep.Merged++
				rep.Moves = append(rep.Moves, MergeMove{From: b.ID, To: a.ID})
			}
		}
		if len(used) > 0 {
			kept := st.order[:0]
			for _, id := range st.order {
				if !used[id] {
					kept = append(kept, id)
				}
			}
			st.order = kept
		}
	}

	// 升降：被反复命中的加固，从没被用过的缓慢降权。
	for _, s := range st.slots {
		if len(s.active()) == 0 {
			continue
		}
		old := s.Imp0
		if s.Hits >= 3 {
			s.Imp0 = clamp(s.Imp0+0.2, 0.8, 6)
			rep.Rebalanced++
		} else if s.Hits == 0 && nowDay-s.LastDay > 60 {
			s.Imp0 = clamp(s.Imp0*0.9, 0.8, 6)
			rep.Rebalanced++
		}
		if s.Imp0 != old {
			rep.ImpUpdates[s.ID] = s.Imp0
		}
	}

	// 遗忘：只有「不重要 + 很久没提 + 从没命中」的成员才会被墓碑。
	for _, s := range st.slots {
		for i := range s.Members {
			m := &s.Members[i]
			if m.State == "forgotten" || m.Imp >= 2.5 || nowDay-m.Day <= 120 || s.Hits > 0 {
				continue
			}
			m.State = "forgotten"
			s.dirty = true
			rep.Forgotten++
			rep.ForgottenIDs = append(rep.ForgottenIDs, m.ID)
		}
	}

	// 全是墓碑的槽没有存在意义，清掉。
	kept := st.order[:0]
	for _, id := range st.order {
		s := st.slots[id]
		if len(s.active()) == 0 {
			delete(st.slots, id)
			continue
		}
		kept = append(kept, id)
	}
	st.order = kept
	rep.SlotsAfter = len(st.order)
	st.Writes++
	return rep
}

// Rebuild 把所有槽向量按成员重算一遍。规则变了、编码器升级了，就跑这个。
func (st *Store) Rebuild() {
	st.mu.Lock()
	defer st.mu.Unlock()
	for _, s := range st.slots {
		s.dirty = true
		_ = s.Vec()
	}
}

// ── 事件重放：槽表是投影，事件才是真相 ─────────────────────────────────

// Reset 清空内存里的槽，重放前用。
func (st *Store) Reset() {
	st.mu.Lock()
	defer st.mu.Unlock()
	st.slots = map[string]*Slot{}
	st.order = nil
	st.Writes, st.Dups, st.Merges, st.Appends = 0, 0, 0, 0
}

// ApplyWrite 用事件里带的显式 id 落一条成员。
// id 必须来自事件，不能在这里重新生成——否则重放两次得到两个不同的库。
func (st *Store) ApplyWrite(slotID, memberID, layer, text string, day int, imp, w float64, refs []string) {
	st.mu.Lock()
	defer st.mu.Unlock()
	if w <= 0 {
		w = 1
	}
	if imp <= 0 {
		imp = 3
	}
	s, ok := st.slots[slotID]
	if !ok {
		s = &Slot{
			ID: slotID, Layer: layer, Day: day, LastDay: day,
			Imp0: imp, Tags: Tag(text), dirty: true,
		}
		st.slots[slotID] = s
		st.order = append(st.order, slotID)
	}
	for _, m := range s.Members {
		if m.ID == memberID {
			return
		}
	}
	s.Members = append(s.Members, Member{
		ID: memberID, Text: text, Day: day, Imp: imp, W: w,
		Refs: refs, State: "active",
	})
	s.LastDay = maxInt(s.LastDay, day)
	if imp > s.Imp0 {
		s.Imp0 = imp
	}
	for _, t := range Tag(text) {
		if !contains(s.Tags, t) {
			s.Tags = append(s.Tags, t)
		}
	}
	s.dirty = true
}

// ApplyMemberState 重放 forget / revive。
func (st *Store) ApplyMemberState(memberID, state string) {
	st.mu.Lock()
	defer st.mu.Unlock()
	for _, s := range st.slots {
		for i := range s.Members {
			if s.Members[i].ID == memberID {
				s.Members[i].State = state
				s.dirty = true
				return
			}
		}
	}
}

// ApplyConsolidate 重放巩固：合并后的成员会被搬到目标槽，源槽消失。
func (st *Store) ApplyConsolidate(rep ConsolidateReport) {
	st.mu.Lock()
	defer st.mu.Unlock()
	for _, move := range rep.Moves {
		from, to := move.From, move.To
		src, ok := st.slots[from]
		if !ok {
			continue
		}
		dst, ok := st.slots[to]
		if !ok {
			continue
		}
		dst.Members = append(dst.Members, src.Members...)
		dst.LastDay = maxInt(dst.LastDay, src.LastDay)
		if src.Imp0 > dst.Imp0 {
			dst.Imp0 = src.Imp0
		}
		// tags 必须与实时 Consolidate 同步合并，否则重放后的槽标签偏少、打分走样。
		for _, t := range src.Tags {
			if !contains(dst.Tags, t) {
				dst.Tags = append(dst.Tags, t)
			}
		}
		dst.dirty = true
		delete(st.slots, from)
	}
	for id, imp := range rep.ImpUpdates {
		if s, ok := st.slots[id]; ok {
			s.Imp0 = imp
		}
	}
	for _, mid := range rep.ForgottenIDs {
		for _, s := range st.slots {
			for i := range s.Members {
				if s.Members[i].ID == mid {
					s.Members[i].State = "forgotten"
					s.dirty = true
				}
			}
		}
	}
	kept := st.order[:0]
	for _, id := range st.order {
		if _, ok := st.slots[id]; ok {
			kept = append(kept, id)
		}
	}
	st.order = kept
}

// ── 持久化：槽表是投影，随时可以从成员重建 ──────────────────────────────

type snapshot struct {
	Encoder string    `json:"encoder"`
	Rule    string    `json:"rule"`
	Order   []string  `json:"order"`
	Slots   []*Slot   `json:"slots"`
	SavedAt time.Time `json:"savedAt"`
}

func (st *Store) Save(path string) error {
	st.mu.RLock()
	snap := snapshot{Encoder: st.Encoder, Rule: st.Rule, Order: st.order, SavedAt: time.Now()}
	for _, id := range st.order {
		snap.Slots = append(snap.Slots, st.slots[id])
	}
	raw, err := json.MarshalIndent(snap, "", "  ")
	st.mu.RUnlock()
	if err != nil {
		return err
	}
	return os.WriteFile(path, raw, 0o644)
}

func (st *Store) Load(path string) error {
	raw, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	var snap snapshot
	if err := json.Unmarshal(raw, &snap); err != nil {
		return err
	}
	st.mu.Lock()
	defer st.mu.Unlock()
	st.Encoder, st.Rule = snap.Encoder, snap.Rule
	if st.MergeSim == 0 {
		st.MergeSim = mergeSim
	}
	if st.NearSim == 0 {
		st.NearSim = nearSim
	}
	st.order = snap.Order
	st.slots = map[string]*Slot{}
	for _, s := range snap.Slots {
		s.dirty = true
		st.slots[s.ID] = s
	}
	return nil
}

func contains(list []string, v string) bool {
	for _, x := range list {
		if x == v {
			return true
		}
	}
	return false
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func clamp(x, lo, hi float64) float64 {
	if x < lo {
		return lo
	}
	if x > hi {
		return hi
	}
	return x
}

// newID 用 crypto/rand 生成短 id，避免为演示引入外部依赖。
func newID(prefix string) string {
	b := make([]byte, 4)
	if _, err := rand.Read(b); err != nil {
		return fmt.Sprintf("%s-%d", prefix, time.Now().UnixNano())
	}
	return prefix + "-" + hex.EncodeToString(b)
}
