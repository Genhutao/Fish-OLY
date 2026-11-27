package world

import (
	"context"
	"fmt"
	"strings"

	"oly/engine/memory"
)

// DMChat 是一次单聊：检索自己的记忆 → 装配 → 生成 → 写回 → 记忆候选。
// 记忆作用域 = 该角色自己的库（C7），跨角色/跨用户都看不到。
func (u *UserWorld) DMChat(ctx context.Context, personaID, text string) ChatResult {
	u.mu.Lock()
	defer u.mu.Unlock()

	p := u.St.Persona(personaID)
	if p == nil {
		return ChatResult{Error: "persona not found"}
	}
	roomID := "dm:" + personaID
	// 先取历史，再写这一句，否则用户发言会在上下文里出现两次。
	history := ClipRecent(u.St.RoomMessages(roomID, 10), 8, 900)
	u.append(Event{Type: "message.created", Room: roomID, Actor: "user", Text: text,
		Data: map[string]any{"id": newMsgID(u, roomID), "name": "我"}})

	mem := u.MemOf(personaID)
	hits := mem.Search(text, 3, u.Day()+1)
	room := &Room{ID: roomID, Name: "对话 · " + p.Name, Topic: ""}
	system, memBlock := Assemble(*p, room, hits, 120)

	if u.Gen == nil {
		return ChatResult{Error: "no generator"}
	}
	reply, mock, err := u.Gen.Reply(ctx, GenerateRequest{
		Persona: *p, Room: room, System: system, MemBlock: memBlock,
		History: history, UserText: text,
	})
	if err != nil {
		return ChatResult{Error: err.Error(), Hits: hits, MemBlock: memBlock}
	}

	u.append(Event{Type: "message.created", Room: roomID, Actor: personaID, Text: reply,
		Data: map[string]any{"id": newMsgID(u, roomID), "name": p.Name, "mock": mock}})

	action := ""
	if layer, imp, ok := shouldRemember(text); ok {
		r := mem.WriteDetailed(layer, text, u.Day()+1, imp, []string{"chat"})
		if r.Action != "dup" {
			u.append(Event{Type: "memory.remember", Data: map[string]any{
				"personaId": personaID, "slotId": r.SlotID, "memberId": r.MemberID,
				"layer": layer, "text": text, "day": u.Day() + 1, "imp": imp,
				"w": r.W, "refs": []string{"chat"}, "action": r.Action,
			}})
			action = r.Action
		}
	}
	u.SaveMemory(personaID)
	return ChatResult{Reply: reply, Mock: mock, Hits: hits, MemBlock: memBlock, Remember: action}
}

// SceneChat 是用户在场景里说一句。
// 场景里有活跃会话 → 加入会话（user.joined 语义，消息挂 session）；
// 没有 → 在场角色按发言权回应（第一名必回，第二名要过线，最多两人）。
func (u *UserWorld) SceneChat(ctx context.Context, sceneID, text string) ChatResult {
	u.mu.Lock()
	defer u.mu.Unlock()

	sc := u.St.Scene(sceneID)
	if sc == nil {
		return ChatResult{Error: "scene not found"}
	}
	day, m := u.Day(), u.TickOfDay()
	history := ClipRecent(u.St.RoomMessages(sceneID, 10), 8, 700)
	u.append(Event{Type: "message.created", Room: sceneID, Actor: "user", Text: text,
		Data: map[string]any{"id": newMsgID(u, sceneID), "name": "我"}})

	sess := u.St.ActiveSessionIn(sceneID, day, m)
	if sess != nil {
		// 加入会话：把会话游标前移，避免调度器立刻又安排一次发言。
		u.append(Event{Type: "session.progress", Data: map[string]any{
			"id": sess.ID, "nextTick": float64(m), "said": float64(sess.Said),
		}})
	}

	// 候选：会话成员优先，否则当前在场的所有人。
	candidates := []string{}
	if sess != nil {
		for _, part := range sess.Parts {
			candidates = append(candidates, part.PersonaID)
		}
	} else {
		for _, pid := range u.St.OrderPersonaIDs() {
			if here, _ := u.St.SceneAt(pid, day, m); here == sceneID {
				candidates = append(candidates, pid)
			}
		}
	}

	type cand struct {
		id    string
		score float64
	}
	cands := make([]cand, 0, len(candidates))
	for _, pid := range candidates {
		p := u.St.Persona(pid)
		if p == nil {
			continue
		}
		cands = append(cands, cand{pid, attention(p, u.St.Tick) + 0.26})
	}
	// 按分数排序（稳定：同分按 id，保证确定性）。
	for i := 0; i < len(cands); i++ {
		for j := i + 1; j < len(cands); j++ {
			if cands[j].score > cands[i].score || (cands[j].score == cands[i].score && cands[j].id < cands[i].id) {
				cands[i], cands[j] = cands[j], cands[i]
			}
		}
	}

	const speakLine = 1.02
	reply := ""
	spoke := 0
	for _, c := range cands {
		if spoke >= 2 {
			break
		}
		if spoke == 1 && c.score < speakLine {
			break
		}
		p := u.St.Persona(c.id)
		if p == nil {
			continue
		}
		mem := u.MemOf(c.id)
		hits := mem.Search(text, 2, u.Day()+1)
		system, memBlock := Assemble(*p, sc, hits, 80)
		out, mock, err := u.Gen.Reply(ctx, GenerateRequest{
			Persona: *p, Room: sc, System: system, MemBlock: memBlock,
			History: history, UserText: text,
		})
		if err != nil {
			reply = "（接口错误：" + err.Error() + "）"
			break
		}
		data := map[string]any{"id": newMsgID(u, sceneID), "name": p.Name, "mock": mock, "score": c.score}
		if sess != nil {
			data["session"] = sess.ID
		}
		u.append(Event{Type: "message.created", Room: sceneID, Actor: c.id, Text: out, Data: data})
		history = append(history, Message{Actor: c.id, Name: p.Name, Text: out})
		if spoke == 0 {
			reply = out
		}
		spoke++
	}
	for _, pid := range candidates {
		u.SaveMemory(pid)
	}
	return ChatResult{Reply: reply}
}

// shouldRemember 是规则抽取：问句、寒暄、太短的一律不记；
// 默认层降重要度，防止闲聊灌满槽表。
func shouldRemember(text string) (layer string, imp float64, ok bool) {
	t := strings.TrimSpace(text)
	r := []rune(t)
	if len(r) < 8 {
		return "", 0, false
	}
	for _, q := range []string{"吗", "呢", "?", "？", "什么", "怎么", "多少", "吧", "哦", "嗯"} {
		if strings.HasSuffix(t, q) || strings.HasSuffix(t, q+"。") {
			return "", 0, false
		}
	}
	for _, w := range []string{"你好", "在吗", "早上好", "晚上好", "再见", "谢谢", "不好意思", "哈哈", "然后", "其实", "刚刚", "现在"} {
		if strings.Contains(t, w) {
			return "", 0, false
		}
	}
	switch {
	case strings.Contains(t, "约定"), strings.Contains(t, "答应"), strings.Contains(t, "说好"):
		return "K", 4.2, true
	case strings.Contains(t, "喜欢"), strings.Contains(t, "讨厌"), strings.Contains(t, "习惯"):
		return "R", 3.4, true
	}
	tags := memory.Tag(text)
	for _, tag := range tags {
		switch tag {
		case "health", "family", "work", "money":
			return "F", 3.6, true
		}
	}
	return "E", 2.2, true
}

// newMsgID 生成事件级消息 id（幂等靠事件重放，不靠这里确定）。
func newMsgID(u *UserWorld, room string) string {
	return fmt.Sprintf("m-%s-%d-%d", u.ID, u.St.Tick, len(u.St.Messages)+1)
}
