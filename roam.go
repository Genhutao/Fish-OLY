package main

import (
	"fmt"
	"math"
	"math/rand"
	"sort"
	"time"

	"github.com/google/uuid"
)

// ─── OC Auto-Roaming (feature 4) ──────────────────────────────────────────────
//
// Personas are not bound to a chat box: when roaming is on, a background tick
// lets every OC lose energy, wander between scenes, do small things, rest, and
// occasionally post a diary entry to the OC community. The state is tiny and
// readable (scene / energy / status / activity), following the "state machine
// over hidden vectors" lesson from the Shikigami and d-tavern designs.

type RoamScene struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	Icon        string `json:"icon"`
	Description string `json:"description"`
	Ambience    string `json:"ambience"`
}

type RoamState struct {
	PersonaID string    `json:"personaId"`
	SceneID   string    `json:"sceneId"`
	Energy    float64   `json:"energy"`
	Status    string    `json:"status"` // roaming | resting
	Activity  string    `json:"activity"`
	RestTicks int       `json:"restTicks"`
	UpdatedAt time.Time `json:"updatedAt"`
}

type RoamEvent struct {
	ID          string    `json:"id"`
	PersonaID   string    `json:"personaId"`
	PersonaName string    `json:"personaName"`
	Avatar      string    `json:"avatar"`
	SceneID     string    `json:"sceneId"`
	SceneName   string    `json:"sceneName"`
	Kind        string    `json:"kind"` // move | action | rest | wake | post
	Content     string    `json:"content"`
	CreatedAt   time.Time `json:"createdAt"`
}

type RoamPersona struct {
	PersonaID string  `json:"personaId"`
	Name      string  `json:"name"`
	Avatar    string  `json:"avatar"`
	Mood      string  `json:"mood"`
	Affection float64 `json:"affection"`
	SceneID   string  `json:"sceneId"`
	SceneName string  `json:"sceneName"`
	Status    string  `json:"status"`
	Activity  string  `json:"activity"`
	Energy    float64 `json:"energy"`
}

type RoamView struct {
	Running  bool          `json:"running"`
	Interval int           `json:"interval"`
	Tick     int           `json:"tick"`
	Scenes   []RoamScene   `json:"scenes"`
	Personas []RoamPersona `json:"personas"`
	Events   []RoamEvent   `json:"events"`
}

func defaultRoamScenes() []RoamScene {
	return []RoamScene{
		{ID: "moon-court", Name: "月下庭院", Icon: "🌙",
			Description: "青石铺地，桂影婆娑。适合独处、弹琴与低语。",
			Ambience:    "风掠过檐角，月光像一层薄霜。"},
		{ID: "academy", Name: "魔法学院中庭", Icon: "✨",
			Description: "浮空灯与咒文余烬，热闹，也总有点危险。",
			Ambience:    "远处传来实验失败的闷响，没人抬头。"},
		{ID: "bookstore", Name: "旧书店", Icon: "📚",
			Description: "纸页与尘埃的味道，时间在这里变慢。",
			Ambience:    "午后的光斜斜地落在书脊上。"},
		{ID: "rain-street", Name: "雨夜长街", Icon: "🌧️",
			Description: "霓虹被雨水打湿，适合遇见不该遇见的人。",
			Ambience:    "雨点敲在伞面上，城市在低处发亮。"},
	}
}

func (a *App) sceneByIDLocked(id string) *RoamScene {
	for i := range a.roamScenes {
		if a.roamScenes[i].ID == id {
			return &a.roamScenes[i]
		}
	}
	return nil
}

// ensureRoamLocked creates a roam state for personas that do not have one yet
// (new personas / first launch). Callers must hold a.mu.
func (a *App) ensureRoamLocked() {
	if len(a.roamScenes) == 0 {
		a.roamScenes = defaultRoamScenes()
	}
	for _, p := range a.personas {
		if _, ok := a.roamStates[p.ID]; ok {
			continue
		}
		scene := a.roamScenes[rand.Intn(len(a.roamScenes))]
		a.roamStates[p.ID] = &RoamState{
			PersonaID: p.ID,
			SceneID:   scene.ID,
			Energy:    60 + rand.Float64()*40,
			Status:    "roaming",
			Activity:  "刚走进" + scene.Name,
			UpdatedAt: time.Now(),
		}
	}
}

func (a *App) roamViewLocked() *RoamView {
	a.ensureRoamLocked()
	view := &RoamView{
		Running:  a.roamRunning,
		Interval: a.roamInterval,
		Tick:     a.roamTicks,
		Scenes:   append([]RoamScene(nil), a.roamScenes...),
		Personas: []RoamPersona{},
		Events:   append([]RoamEvent(nil), a.roamEvents...),
	}
	ids := make([]string, 0, len(a.roamStates))
	for id := range a.roamStates {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	for _, id := range ids {
		st := a.roamStates[id]
		p, ok := a.personas[id]
		if !ok {
			continue
		}
		sceneName := ""
		if sc := a.sceneByIDLocked(st.SceneID); sc != nil {
			sceneName = sc.Name
		}
		view.Personas = append(view.Personas, RoamPersona{
			PersonaID: p.ID, Name: p.Name, Avatar: p.Avatar,
			Mood: p.Mood, Affection: p.Affection,
			SceneID: st.SceneID, SceneName: sceneName,
			Status: st.Status, Activity: st.Activity,
			Energy: math.Round(st.Energy*10) / 10,
		})
	}
	return view
}

func (a *App) addRoamEventLocked(p *Persona, st *RoamState, kind, content string) {
	sceneName := ""
	if sc := a.sceneByIDLocked(st.SceneID); sc != nil {
		sceneName = sc.Name
	}
	ev := RoamEvent{
		ID: uuid.New().String(), PersonaID: p.ID, PersonaName: p.Name,
		Avatar: p.Avatar, SceneID: st.SceneID, SceneName: sceneName,
		Kind: kind, Content: content, CreatedAt: time.Now(),
	}
	a.roamEvents = append([]RoamEvent{ev}, a.roamEvents...)
	if len(a.roamEvents) > 80 {
		a.roamEvents = a.roamEvents[:80]
	}
}

var roamActions = map[string][]string{
	"moon-court": {
		"对着月亮弹了一小段没人听的曲子。",
		"把剑横在膝上，闭眼听风穿过回廊。",
		"低声念了一句谁也听不清的诗。",
		"在石阶上坐了很久，直到露水打湿衣角。",
	},
	"academy": {
		"追着一只失控的魔法纸鹤跑过整条走廊。",
		"点亮新学的光系咒文，又慌忙把它吹灭。",
		"被教授抓去整理实验器材，偷偷翻了个白眼。",
		"和同学打赌谁能把浮空灯叠得更高，输了。",
	},
	"bookstore": {
		"从书架最上层抽出一本积灰的旧书。",
		"在扉页发现一行陌生批注，愣了很久。",
		"和店主聊起一本绝版诗集，忘了时间。",
		"把一本倒扣的书轻轻扶正。",
	},
	"rain-street": {
		"撑伞站在屋檐下，看雨水顺着瓦片落下。",
		"替一只流浪猫挡住了半程的雨。",
		"在便利店门口犹豫要不要买那把贵得离谱的伞。",
		"把伞倾向身边的人，自己的肩膀湿了一片。",
	},
}

var roamDiaries = map[string][]string{
	"moon-court":  {"月亮很好，好到让人不想说话。", "今晚的庭院很安静，适合把心事摊开晾一晾。"},
	"academy":     {"今天又炸了一个魔法炉，教授的表情很精彩。", "学院的浮空灯换新了，比星星还亮。"},
	"bookstore":   {"在旧书里捡到一句别人的批注，像是隔世的对白。", "旧书店的猫今天肯让我摸了。"},
	"rain-street": {"雨把整条街洗得很干净，包括那些不想被记住的事。", "在便利店的热饮柜前站了很久，最后什么也没买。"},
}

func moodForScene(sceneID string, energy float64) string {
	if energy < 30 {
		return "tired"
	}
	switch sceneID {
	case "moon-court":
		return "calm"
	case "academy":
		return "happy"
	case "bookstore":
		return "calm"
	case "rain-street":
		return "sad"
	default:
		return "neutral"
	}
}

func randomOtherScene(current string, scenes []RoamScene) string {
	if len(scenes) <= 1 {
		return current
	}
	for {
		id := scenes[rand.Intn(len(scenes))].ID
		if id != current {
			return id
		}
	}
}

// roamTickLocked advances the world by one tick. Callers must hold a.mu.
func (a *App) roamTickLocked() {
	a.ensureRoamLocked()
	a.roamTicks++

	decay := a.settings.RoamEnergyDecay
	if decay <= 0 {
		decay = 1
	}
	restAt := a.settings.RoamRestThreshold
	if restAt <= 0 {
		restAt = 20
	}
	autoPost := a.settings.RoamAutoPost
	if autoPost < 0 {
		autoPost = 0.14
	}

	ids := make([]string, 0, len(a.personas))
	for id := range a.personas {
		ids = append(ids, id)
	}
	sort.Strings(ids)

	for _, id := range ids {
		p := a.personas[id]
		st := a.roamStates[id]
		if st == nil {
			continue
		}

		if st.Status == "resting" {
			st.Energy = math.Min(100, st.Energy+28)
			st.RestTicks--
			if st.RestTicks <= 0 || st.Energy >= 85 {
				st.Status = "roaming"
				st.Activity = "伸了个懒腰，重新出发"
				a.addRoamEventLocked(p, st, "wake", "休息够了，重新开始漫游。")
			} else {
				st.Activity = "靠着墙小憩，慢慢回血"
			}
			st.UpdatedAt = time.Now()
			continue
		}

		st.Energy -= (6 + rand.Float64()*10) * decay
		if st.Energy < 0 {
			st.Energy = 0
		}
		if st.Energy <= restAt {
			st.Status = "resting"
			st.RestTicks = 2
			st.Activity = "精力见底，找了处安静的地方休息"
			p.Mood = "tired"
			a.addRoamEventLocked(p, st, "rest", "走不动了，缩进角落充会儿电。")
			st.UpdatedAt = time.Now()
			continue
		}

		if rand.Float64() < 0.45 {
			fromName := ""
			if sc := a.sceneByIDLocked(st.SceneID); sc != nil {
				fromName = sc.Name
			}
			st.SceneID = randomOtherScene(st.SceneID, a.roamScenes)
			toName := ""
			if sc := a.sceneByIDLocked(st.SceneID); sc != nil {
				toName = sc.Name
			}
			a.addRoamEventLocked(p, st, "move", fmt.Sprintf("从%s走到了%s。", fromName, toName))
		}

		actions := roamActions[st.SceneID]
		if len(actions) > 0 {
			st.Activity = actions[rand.Intn(len(actions))]
		} else {
			st.Activity = "四处张望。"
		}
		p.Mood = moodForScene(st.SceneID, st.Energy)
		p.UpdatedAt = time.Now()
		a.addRoamEventLocked(p, st, "action", st.Activity)

		if rand.Float64() < autoPost {
			a.roamDiaryPostLocked(p, st)
		}
		// Roaming cognition: sometimes the character drifts into a live room.
		if rand.Float64() < 0.22 {
			a.groupTickLocked()
		}
		st.UpdatedAt = time.Now()
	}
}

func (a *App) roamDiaryPostLocked(p *Persona, st *RoamState) {
	scene := a.sceneByIDLocked(st.SceneID)
	if scene == nil {
		return
	}
	lines := roamDiaries[scene.ID]
	line := "在这里待了一会儿。"
	if len(lines) > 0 {
		line = lines[rand.Intn(len(lines))]
	}
	content := fmt.Sprintf("【漫游·%s】%s\n%s", scene.Name, scene.Ambience, line)
	post := OCPost{
		ID: uuid.New().String(), AuthorID: p.ID,
		Author: p.Name, Avatar: p.Avatar,
		Content: content, Tags: []string{"漫游", "OC动态"},
		Likes: rand.Intn(24), Comments: []OCComment{},
		CreatedAt: time.Now(), IsAI: true,
	}
	a.posts = append([]OCPost{post}, a.posts...)
	a.addRoamEventLocked(p, st, "post", "在 OC 社区发了一条漫游日记。")
}

func (a *App) roamLoop(stop chan struct{}, interval int) {
	ticker := time.NewTicker(time.Duration(interval) * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-stop:
			return
		case <-ticker.C:
			a.mu.Lock()
			if a.roamRunning {
				a.roamTickLocked()
			}
			a.mu.Unlock()
		}
	}
}

// ─── Roaming APIs ────────────────────────────────────────────────────────────

func (a *App) GetRoamState() *RoamView {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.roamViewLocked()
}

func (a *App) SetRoamRunning(running bool) (*RoamView, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	if running == a.roamRunning {
		return a.roamViewLocked(), nil
	}
	if running {
		a.roamRunning = true
		stop := make(chan struct{})
		a.roamStop = stop
		interval := a.roamInterval
		go a.roamLoop(stop, interval)
	} else {
		a.roamRunning = false
		if a.roamStop != nil {
			close(a.roamStop)
			a.roamStop = nil
		}
	}
	return a.roamViewLocked(), nil
}

func (a *App) SetRoamInterval(seconds int) (*RoamView, error) {
	if seconds < 3 {
		seconds = 3
	}
	if seconds > 120 {
		seconds = 120
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	a.roamInterval = seconds
	if a.roamRunning {
		if a.roamStop != nil {
			close(a.roamStop)
			a.roamStop = nil
		}
		stop := make(chan struct{})
		a.roamStop = stop
		go a.roamLoop(stop, seconds)
	}
	return a.roamViewLocked(), nil
}

func (a *App) RoamStep() (*RoamView, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.roamTickLocked()
	return a.roamViewLocked(), nil
}
