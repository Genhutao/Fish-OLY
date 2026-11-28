package world

import (
	"fmt"
	"strings"

	"oly/engine/memory"
)

// 提示板预算（按字符数近似 token）。Memory 不进 system，单独返回。
const (
	budgetIdentity = 420
	budgetCanon    = 300
	budgetState    = 160
	budgetOutput   = 200
	budgetRecent   = 900
	budgetMemory   = 600
)

// Assemble 按槽位装配 system 与记忆块。
//
// 与原型 demo 的关键差别：记忆块不拼进 system，而是返回给调用方，
// 由它贴在最后一条用户消息之前（第二人称、紧贴发言）。
func Assemble(p Persona, room *Room, hits []memory.Hit, replyLimit int) (system, memBlock string) {
	var b strings.Builder

	identity := fmt.Sprintf("你在扮演「%s」，用第一人称说话。\n", p.Name)
	if p.Personality != "" {
		identity += "人格维度：" + clip(p.Personality, budgetIdentity) + "\n"
	}
	if p.Anchor != "" {
		identity += "核心锚点（任何情况下都不可违背）：" + p.Anchor + "\n"
	}
	b.WriteString(clip(identity, budgetIdentity))
	b.WriteString("\n")

	if room != nil {
		canon := fmt.Sprintf("【当前场景】%s。%s\n", room.Name, room.Topic)
		b.WriteString(clip(canon, budgetCanon))
	}

	state := fmt.Sprintf("【当前状态】心情 %s，精力 %.0f/100，对用户好感 %.1f/10。\n",
		p.Mood, p.Energy, p.Affection)
	b.WriteString(clip(state, budgetState))

	b.WriteString("\n【输出】\n")
	out := fmt.Sprintf("保持角色，用中文自然回复，不超过 %d 字；不要提到自己是 AI、模型或程序，不要跳出角色解释设定。", replyLimit)
	b.WriteString(clip(out, budgetOutput))

	if len(hits) > 0 {
		var m strings.Builder
		m.WriteString("【我记得的事】\n")
		used := 0
		for _, h := range hits {
			line := "· 你提过：" + h.Text + "\n"
			if used+len([]rune(line)) > budgetMemory {
				break
			}
			m.WriteString(line)
			used += len([]rune(line))
		}
		memBlock = strings.TrimSpace(m.String())
	}
	return b.String(), memBlock
}

// ClipRecent 取最近 n 条，超出预算从最旧开始丢。
func ClipRecent(msgs []Message, n, budget int) []Message {
	if len(msgs) > n {
		msgs = msgs[len(msgs)-n:]
	}
	total := 0
	for i := len(msgs) - 1; i >= 0; i-- {
		total += len([]rune(msgs[i].Text))
		if total > budget {
			return msgs[i+1:]
		}
	}
	return msgs
}

func clip(s string, n int) string {
	r := []rune(s)
	if len(r) <= n {
		return s
	}
	return string(r[:n]) + "…"
}
