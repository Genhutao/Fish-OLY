package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// ─── Settings: defaults, persistence, connection test, stats ─────────────────

func defaultSettings() AppSettings {
	return AppSettings{
		APIEndpoint: "https://api.openai.com/v1",
		Model:       "gpt-4o-mini",
		MaxTokens:   2048,
		Temperature: 0.85,

		TopP:             1,
		PresencePenalty:  0.1,
		FrequencyPenalty: 0,
		ReplyLength:      "normal",

		MemoryTopK:      3,
		MemoryWeightSim: 0.5,
		MemoryWeightImp: 0.3,
		MemoryWeightRec: 0.2,

		RoamAutoPost:      0.14,
		RoamRestThreshold: 20,
		RoamEnergyDecay:   1,

		UiDensity:       "comfortable",
		ReduceMotion:    false,
		Typewriter:      true,
		EnterToSend:     true,
		ShowTimestamps:  true,
		SceneBackground: true,
		Theme:           "dark",
		GitHubToken:     "",
	}
}

func settingsPath() string {
	dir, err := os.UserConfigDir()
	if err != nil {
		return ""
	}
	return filepath.Join(dir, "oly-persona", "settings.json")
}

// loadPersistedSettings overlays a previously saved settings file onto defaults,
// so a partial or older file still produces a complete settings value.
func loadPersistedSettings(base AppSettings) AppSettings {
	path := settingsPath()
	if path == "" {
		return base
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return base
	}
	merged := base
	if err := json.Unmarshal(raw, &merged); err != nil {
		return base
	}
	if merged.ReplyLength == "" {
		merged.ReplyLength = base.ReplyLength
	}
	if merged.UiDensity == "" {
		merged.UiDensity = base.UiDensity
	}
	return merged
}

func persistSettings(s AppSettings) error {
	path := settingsPath()
	if path == "" {
		return fmt.Errorf("no user config dir")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	raw, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, raw, 0o644)
}

// replyLimit maps the reply-length branch to a prompt constraint.
func (s AppSettings) replyLimit() int {
	switch s.ReplyLength {
	case "short":
		return 80
	case "long":
		return 320
	default:
		return 150
	}
}

// recallScore normalizes the three memory weights so they always sum to 1.
func (s AppSettings) recallScore(sim, imp, rec float64) float64 {
	ws, wi, wr := s.MemoryWeightSim, s.MemoryWeightImp, s.MemoryWeightRec
	sum := ws + wi + wr
	if sum <= 0 {
		return 0.5*sim + 0.3*imp + 0.2*rec
	}
	return (ws*sim + wi*imp + wr*rec) / sum
}

// TestConnection performs a lightweight authenticated request against the
// configured OpenAI-compatible endpoint and reports what it found.
func (a *App) TestConnection() (string, error) {
	a.mu.Lock()
	s := a.settings
	a.mu.Unlock()

	if strings.TrimSpace(s.APIKey) == "" {
		return "", fmt.Errorf("未填写 API Key，当前运行在 mock 模式")
	}
	url := strings.TrimRight(s.APIEndpoint, "/") + "/models"
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return "", err
	}
	req.Header.Set("Authorization", "Bearer "+s.APIKey)
	client := &http.Client{Timeout: 15 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return "", fmt.Errorf("连接失败（%s）：%v", url, err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 400 {
		return "", fmt.Errorf("接口返回 %d：%s", resp.StatusCode, truncate(string(body), 160))
	}
	var parsed struct {
		Data []struct {
			ID string `json:"id"`
		} `json:"data"`
	}
	_ = json.Unmarshal(body, &parsed)
	if len(parsed.Data) > 0 {
		return fmt.Sprintf("连接成功 · 可用模型 %d 个 · 当前 %s", len(parsed.Data), s.Model), nil
	}
	return "连接成功 · 接口可达", nil
}

func truncate(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return string(r[:n]) + "…"
}

// ─── Stats (hero strip / demo footer) ────────────────────────────────────────

type AppStats struct {
	Personas     int     `json:"personas"`
	Memories     int     `json:"memories"`
	Posts        int     `json:"posts"`
	Stories      int     `json:"stories"`
	Skills       int     `json:"skills"`
	Presets      int     `json:"presets"`
	Roaming      int     `json:"roaming"`
	AvgAffection float64 `json:"avgAffection"`
	Ticks        int     `json:"ticks"`
}

func (a *App) GetStats() AppStats {
	a.mu.Lock()
	defer a.mu.Unlock()
	st := AppStats{
		Personas: len(a.personas),
		Posts:    len(a.posts),
		Stories:  len(a.stories),
		Skills:   len(a.skills),
		Presets:  len(a.presets),
		Ticks:    a.roamTicks,
	}
	total := 0.0
	for id, p := range a.personas {
		st.Memories += len(a.memories[id])
		total += p.Affection
		if rs, ok := a.roamStates[id]; ok && rs.Status == "roaming" {
			st.Roaming++
		}
	}
	if len(a.personas) > 0 {
		st.AvgAffection = total / float64(len(a.personas))
	}
	return st
}

// PreviewPersonaPrompt returns the exact system prompt a persona would send
// right now, so the library UI can show the composed effect of preset × skills.
func (a *App) PreviewPersonaPrompt(personaID string) (string, error) {
	a.mu.Lock()
	defer a.mu.Unlock()
	p, ok := a.personas[personaID]
	if !ok {
		return "", fmt.Errorf("persona not found")
	}
	return a.buildSystemPromptLocked(clonePersona(p), "（这里会插入按相似度检索到的记忆片段）", a.settings.replyLimit(), ""), nil
}

// ResetDemoData restores the seeded demo world (personas, posts, roam state,
// library) while keeping the current AI settings.
func (a *App) ResetDemoData() error {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.roamStop != nil {
		close(a.roamStop)
		a.roamStop = nil
	}
	if a.groupsStop != nil {
		close(a.groupsStop)
		a.groupsStop = nil
	}
	a.groupsLive = false
	a.roamRunning = false
	a.personas = make(map[string]*Persona)
	a.snapshots = make(map[string][]MemorySnapshot)
	a.memories = make(map[string][]MemoryEntry)
	a.stories = make(map[string]*StorySession)
	a.posts = nil
	a.roamStates = make(map[string]*RoamState)
	a.roamEvents = nil
	a.roamTicks = 0
	a.skills = nil
	a.presets = nil
	a.groups = make(map[string]*Group)
	a.seedData()
	a.seedLibrary()
	a.seedGroups()
	a.ensureRoamLocked()
	return nil
}
