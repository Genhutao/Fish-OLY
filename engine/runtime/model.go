package runtime

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"oly/engine/world"
)

// ReplyRequest 是一次生成的输入。记忆块单独给，放在最后一条用户消息里。
type ReplyRequest struct {
	Persona  world.Persona
	Room     *world.Room
	System   string
	MemBlock string
	History  []world.Message
	UserText string
}

// Client 是 OpenAI 兼容客户端。没有 Key 时退化为 mock，但 mock 必须打标记。
type Client struct {
	Endpoint string
	Key      string
	Model    string
	HTTP     *http.Client
}

// NewClient 依次读环境变量、dataDir/settings.json、默认值。
func NewClient(dataDir string) *Client {
	c := &Client{
		Endpoint: "https://api.deepseek.com/v1",
		Model:    "deepseek-chat",
		HTTP:     &http.Client{Timeout: 40 * time.Second},
	}
	if p := filepath.Join(dataDir, "settings.json"); p != "" {
		if raw, err := os.ReadFile(p); err == nil {
			var s struct {
				Endpoint string `json:"apiEndpoint"`
				Key      string `json:"apiKey"`
				Model    string `json:"model"`
			}
			if json.Unmarshal(raw, &s) == nil {
				if s.Endpoint != "" {
					c.Endpoint = s.Endpoint
				}
				if s.Model != "" {
					c.Model = s.Model
				}
				c.Key = s.Key
			}
		}
	}
	if v := os.Getenv("OLY_ENDPOINT"); v != "" {
		c.Endpoint = v
	}
	if v := os.Getenv("OLY_MODEL"); v != "" {
		c.Model = v
	}
	if v := os.Getenv("OLY_API_KEY"); v != "" {
		c.Key = v
	}
	return c
}

// Label 给顶栏显示当前链路。
func (c *Client) Label() string {
	if c.Key == "" {
		return "mock"
	}
	return c.Model
}

// Real 报告是否有真实模型可用（world.Generator 接口用，无 Key 时自主发言降级模板）。
func (c *Client) Real() bool { return c.Key != "" }

type chatMsg struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

// Reply 调一次模型。失败必须显式返回错误，不许静默降级成模板。
func (c *Client) Reply(ctx context.Context, req ReplyRequest) (string, bool, error) {
	if c.Key == "" {
		return mockReply(req), true, nil
	}

	msgs := []chatMsg{{Role: "system", Content: req.System}}
	for _, m := range req.History {
		role := "user"
		if m.Actor != "user" && m.Actor != "" {
			role = "assistant"
		}
		msgs = append(msgs, chatMsg{Role: role, Content: m.Text})
	}
	user := req.UserText
	if req.MemBlock != "" {
		user = req.MemBlock + "\n\n" + user
	}
	msgs = append(msgs, chatMsg{Role: "user", Content: user})

	body, _ := json.Marshal(map[string]any{
		"model":       c.Model,
		"messages":    msgs,
		"max_tokens":  800,
		"temperature": 0.85,
	})
	endpoint := strings.TrimRight(c.Endpoint, "/") + "/chat/completions"
	httpReq, err := http.NewRequestWithContext(ctx, "POST", endpoint, bytes.NewReader(body))
	if err != nil {
		return "", false, err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("Authorization", "Bearer "+c.Key)

	resp, err := c.HTTP.Do(httpReq)
	if err != nil {
		return "", false, fmt.Errorf("模型请求失败（%s）：%w", endpoint, err)
	}
	defer resp.Body.Close()
	raw, _ := io.ReadAll(resp.Body)

	var parsed struct {
		Choices []struct {
			Message struct {
				Content string `json:"content"`
			} `json:"message"`
		} `json:"choices"`
		Error *struct {
			Message string `json:"message"`
		} `json:"error"`
	}
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return "", false, fmt.Errorf("接口返回无法解析：%s", strings.TrimSpace(string(raw)))
	}
	if parsed.Error != nil {
		return "", false, fmt.Errorf("接口错误：%s", parsed.Error.Message)
	}
	if len(parsed.Choices) == 0 {
		return "", false, fmt.Errorf("接口没有返回 choices")
	}
	return strings.TrimSpace(parsed.Choices[0].Message.Content), false, nil
}

// mockReply 不编造“真实模型”的体验，但会用上检索到的记忆，
// 这样没有 Key 的时候也能看见「紧贴发言的第二人称记忆」在起作用。
func mockReply(req ReplyRequest) string {
	name := req.Persona.Name
	line := pick(req.Persona.ID)
	if req.MemBlock != "" {
		first := firstBullet(req.MemBlock)
		if first != "" {
			return fmt.Sprintf("*%s 顿了顿* 你之前说过「%s」……这件事我记着。\n%s", name, first, line)
		}
	}
	return fmt.Sprintf("*%s 抬眼看你* %s", name, line)
}

func firstBullet(block string) string {
	for _, l := range strings.Split(block, "\n") {
		l = strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(l), "·"))
		l = strings.TrimSpace(strings.TrimPrefix(l, "你提过："))
		if l != "" && !strings.Contains(l, "我记得的事") {
			return l
		}
	}
	return ""
}

var mockLines = map[string][]string{
	"seed-1": {"月亮今晚很安静。你有什么话，就说吧。", "……我听着。别急。", "风有点凉，站过来些。"},
	"seed-2": {"欸，你来得正好！我刚想找人说话。", "今天学院的浮空灯又坏了两盏，笑死。", "你说的这个，我还真没想过。"},
}

func pick(id string) string {
	lines := mockLines[id]
	if len(lines) == 0 {
		return "嗯，我在听。"
	}
	return lines[int(time.Now().UnixNano())%len(lines)]
}
