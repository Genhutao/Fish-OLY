package main

import (
	"fmt"
	"math/rand"
	"sort"
	"time"

	"github.com/google/uuid"
)

// Group is a QQ-style room: either a persistent group chat or a script event.
// Personas post on their own while a room is live, and the user can join in.
type Group struct {
	ID        string        `json:"id"`
	Name      string        `json:"name"`
	Icon      string        `json:"icon"`
	Kind      string        `json:"kind"` // group | script
	SceneID   string        `json:"sceneId"`
	Topic     string        `json:"topic"`
	MemberIDs []string      `json:"memberIds"`
	Messages  []ChatMessage `json:"messages"`
	Live      bool          `json:"live"`
	CreatedAt time.Time     `json:"createdAt"`
}

var groupLines = map[string][]string{
	"seed-1": {
		"今晚的月亮，比昨天冷一点。",
		"风里有桂花的味道。",
		"……别站在风口。",
		"琴弦受潮了，音不太准。",
		"若无事，我先回去了。",
	},
	"seed-2": {
		"教授说实战考核改到黄昏了，救命。",
		"食堂新出的奶油面包真的很好吃！",
		"谁把我的符文笔记拿走了？",
		"我刚刚成功点亮了第三个光球！",
		"今晚有人一起去中庭试咒文吗？",
	},
	"system": {
		"雨声更大了，街灯闪了一下。",
		"远处传来钟声，一共敲了七下。",
		"桌上的茶已经凉了。",
	},
}

func (a *App) seedGroups() {
	now := time.Now()
	mk := func(name, icon, kind, scene, topic string, members []string, lines [][3]string) *Group {
		g := &Group{
			ID: "group-" + uuid.New().String()[:8], Name: name, Icon: icon, Kind: kind,
			SceneID: scene, Topic: topic, MemberIDs: members,
			Live: true, CreatedAt: now.Add(-12 * time.Hour),
		}
		for i, l := range lines {
			g.Messages = append(g.Messages, ChatMessage{
				ID: uuid.New().String(), SessionID: g.ID, Role: l[0],
				PersonaID: l[1], PersonaName: a.personaNameLocked(l[1]),
				Content: l[2], Kind: kindOf(l[0]),
				CreatedAt: now.Add(time.Duration(i-len(lines)) * 6 * time.Minute),
			})
		}
		return g
	}

	gMoon := mk("月下庭院 · 夜谈", "🌙", "group", "moon-court",
		"几位 OC 在月下庭院里闲聊，话题从月亮一路歪到剑术。",
		[]string{"seed-1", "seed-2"}, [][3]string{
			{"system", "", "【群聊创建于昨晚，夜风正好。】"},
			{"assistant", "seed-2", "千鹤！你真的每天晚上都在这儿看月亮吗？"},
			{"assistant", "seed-1", "……安静点。月亮被你吵跑了。"},
			{"assistant", "seed-2", "月亮才不会跑呢，它只是懒得理你。"},
		})

	gAcademy := mk("魔法学院三年级", "✨", "group", "academy",
		"学院群里永远有人在炸炉子、丢笔记、或者问作业。",
		[]string{"seed-2"}, [][3]string{
			{"system", "", "【课程通知：本周实战考核延至黄昏。】"},
			{"assistant", "seed-2", "有人看见我的符文学笔记吗？蓝色封皮那本。"},
			{"assistant", "seed-2", "算了，可能又被我的魔法炉吃了。"},
		})

	gScript := mk("剧本 · 雨夜长街", "🌧️", "script", "rain-street",
		"一场即兴剧本：雨夜、长街、一把只够一个人的伞。角色会自动演绎，也可以随时加入。",
		[]string{"seed-1", "seed-2"}, [][3]string{
			{"system", "", "【剧本开始：雨夜长街，23:40。】"},
			{"assistant", "seed-2", "……这么大的雨，你也没带伞？"},
			{"assistant", "seed-1", "伞只有一把。"},
			{"assistant", "seed-2", "那我们……一起走？"},
		})

	a.groups[gMoon.ID] = gMoon
	a.groups[gAcademy.ID] = gAcademy
	a.groups[gScript.ID] = gScript

	a.groupsLive = true
	a.groupsStop = make(chan struct{})
	go a.groupLoop(a.groupsStop)
}

func kindOf(role string) string {
	if role == "system" {
		return "system"
	}
	return "text"
}

func (a *App) personaNameLocked(id string) string {
	if p, ok := a.personas[id]; ok {
		return p.Name
	}
	return ""
}

func (a *App) groupLoop(stop chan struct{}) {
	ticker := time.NewTicker(9 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-stop:
			return
		case <-ticker.C:
			a.mu.Lock()
			if a.groupsLive {
				a.groupTickLocked()
			}
			a.mu.Unlock()
		}
	}
}

// groupTickLocked lets a random live room speak one line on its own. Called by
// the roaming loop too, so "闲逛" personas drift into rooms and talk.
func (a *App) groupTickLocked() {
	if len(a.groups) == 0 {
		return
	}
	a.groupsTick++
	if rand.Float64() > 0.75 {
		// occasionally a system/ambient beat instead of a line
		ids := a.groupIDsLocked()
		g := a.groups[ids[rand.Intn(len(ids))]]
		a.appendGroupMessageLocked(g, ChatMessage{
			ID: uuid.New().String(), SessionID: g.ID, Role: "system",
			Content: groupLines["system"][rand.Intn(len(groupLines["system"]))],
			Kind:    "system", CreatedAt: time.Now(),
		})
		return
	}
	ids := a.groupIDsLocked()
	g := a.groups[ids[rand.Intn(len(ids))]]
	speaker := ""
	if len(g.MemberIDs) > 0 {
		speaker = g.MemberIDs[rand.Intn(len(g.MemberIDs))]
	}
	lines := groupLines[speaker]
	if len(lines) == 0 {
		lines = groupLines["seed-2"]
	}
	a.appendGroupMessageLocked(g, ChatMessage{
		ID: uuid.New().String(), SessionID: g.ID, Role: "assistant",
		PersonaID: speaker, PersonaName: a.personaNameLocked(speaker),
		Content: lines[rand.Intn(len(lines))], Kind: "text",
		CreatedAt: time.Now(),
	})
}

func (a *App) groupIDsLocked() []string {
	ids := make([]string, 0, len(a.groups))
	for id := range a.groups {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	return ids
}

func (a *App) appendGroupMessageLocked(g *Group, m ChatMessage) {
	g.Messages = append(g.Messages, m)
	if len(g.Messages) > 120 {
		g.Messages = g.Messages[len(g.Messages)-120:]
	}
}

// ─── Group APIs ──────────────────────────────────────────────────────────────

func (a *App) ListGroups() []*Group {
	a.mu.Lock()
	defer a.mu.Unlock()
	out := make([]*Group, 0, len(a.groups))
	for _, g := range a.groups {
		cp := *g
		cp.Messages = append([]ChatMessage(nil), g.Messages...)
		cp.MemberIDs = append([]string(nil), g.MemberIDs...)
		out = append(out, &cp)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].CreatedAt.After(out[j].CreatedAt) })
	return out
}

func (a *App) GetGroup(id string) (*Group, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	g, ok := a.groups[id]
	if !ok {
		return nil, fmt.Errorf("group not found")
	}
	cp := *g
	cp.Messages = append([]ChatMessage(nil), g.Messages...)
	cp.MemberIDs = append([]string(nil), g.MemberIDs...)
	return &cp, nil
}

func (a *App) SendGroupMessage(groupID, content, kind, meta string) (*Group, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	g, ok := a.groups[groupID]
	if !ok {
		return nil, fmt.Errorf("group not found")
	}
	if kind == "" {
		kind = "text"
	}
	a.appendGroupMessageLocked(g, ChatMessage{
		ID: uuid.New().String(), SessionID: g.ID, Role: "user",
		PersonaName: "我", Content: content, Kind: kind, Meta: meta,
		CreatedAt: time.Now(),
	})

	// one or two members answer the user
	responders := append([]string(nil), g.MemberIDs...)
	rand.Shuffle(len(responders), func(i, j int) { responders[i], responders[j] = responders[j], responders[i] })
	if len(responders) > 1 && rand.Float64() < 0.45 {
		responders = responders[:2]
	} else if len(responders) > 0 {
		responders = responders[:1]
	}
	for _, pid := range responders {
		line := a.groupReplyLocked(pid, content)
		a.appendGroupMessageLocked(g, ChatMessage{
			ID: uuid.New().String(), SessionID: g.ID, Role: "assistant",
			PersonaID: pid, PersonaName: a.personaNameLocked(pid),
			Content: line, Kind: "text", CreatedAt: time.Now().Add(300 * time.Millisecond),
		})
	}

	cp := *g
	cp.Messages = append([]ChatMessage(nil), g.Messages...)
	cp.MemberIDs = append([]string(nil), g.MemberIDs...)
	return &cp, nil
}

func (a *App) groupReplyLocked(personaID, userText string) string {
	if rand.Float64() < 0.35 {
		return "……"
	}
	lines := groupLines[personaID]
	if len(lines) == 0 {
		return "嗯，我在听。"
	}
	return lines[rand.Intn(len(lines))]
}

func (a *App) SetGroupsLive(live bool) error {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.groupsLive = live
	if live && a.groupsStop == nil {
		a.groupsStop = make(chan struct{})
		go a.groupLoop(a.groupsStop)
	}
	if !live && a.groupsStop != nil {
		close(a.groupsStop)
		a.groupsStop = nil
	}
	return nil
}
