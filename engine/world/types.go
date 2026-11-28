package world

import (
	"context"
	"encoding/json"

	"oly/engine/memory"
)

// GenerateRequest 是引擎交给模型的输入。定义在这里，避免 world 依赖 runtime。
type GenerateRequest struct {
	Persona  Persona
	Room     *Room
	System   string
	MemBlock string
	History  []Message
	UserText string
}

// Generator 是模型侧的最小接口。Real() 报告是否有真实模型可用（没有就降级模板）。
type Generator interface {
	Reply(ctx context.Context, req GenerateRequest) (string, bool, error)
	Label() string
	Real() bool
}

// ChatResult 是一次对话的返回。
type ChatResult struct {
	Reply    string       `json:"reply"`
	Mock     bool         `json:"mock"`
	Hits     []memory.Hit `json:"hits"`
	MemBlock string       `json:"memBlock"`
	Remember string       `json:"remember"` // 本轮写入的动作，空表示没记
	Error    string       `json:"error,omitempty"`
}

// ── 事件字段的小工具 ──────────────────────────────────────────────────────

func str(v any) string {
	s, _ := v.(string)
	return s
}

func num(v any) float64 {
	switch x := v.(type) {
	case float64:
		return x
	case float32:
		return float64(x)
	case int:
		return float64(x)
	case int64:
		return float64(x)
	}
	return 0
}

func strs(v any) []string {
	raw, ok := v.([]any)
	if !ok {
		if ss, ok := v.([]string); ok {
			return ss
		}
		return nil
	}
	out := make([]string, 0, len(raw))
	for _, x := range raw {
		if s, ok := x.(string); ok {
			out = append(out, s)
		}
	}
	return out
}

// decodeReport 处理「同进程里是结构体、重启后是 map」两种情况。
func decodeReport(v any) (memory.ConsolidateReport, bool) {
	var out memory.ConsolidateReport
	raw, err := json.Marshal(v)
	if err != nil {
		return out, false
	}
	if err := json.Unmarshal(raw, &out); err != nil {
		return out, false
	}
	return out, true
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// ── 事件 Data 字段读取（nameOf/strOf/numOf/boolOf/idOf）──────────────────

func idOf(e Event) string {
	if v, ok := e.Data["id"].(string); ok {
		return v
	}
	return ""
}

func nameOf(e Event) string {
	if v, ok := e.Data["name"].(string); ok {
		return v
	}
	return ""
}

func boolOf(e Event, key string) bool {
	if v, ok := e.Data[key].(bool); ok {
		return v
	}
	return false
}

func strOf(e Event, key string) string {
	if v, ok := e.Data[key].(string); ok {
		return v
	}
	return ""
}

// numOf 读数字字段。进程内写入可能是 int/int64，重放后是 float64，两者都收。
func numOf(e Event, key string, def float64) float64 {
	switch v := e.Data[key].(type) {
	case float64:
		return v
	case float32:
		return float64(v)
	case int:
		return float64(v)
	case int64:
		return float64(v)
	}
	return def
}
