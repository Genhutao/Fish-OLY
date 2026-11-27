package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// offlineApp ignores the developer's real settings.json so tests stay hermetic.
func offlineApp() *App {
	a := NewApp()
	a.settings = defaultSettings()
	return a
}

func TestSeedDataAndRoamView(t *testing.T) {
	a := offlineApp()
	view := a.GetRoamState()
	if len(view.Personas) != 2 {
		t.Fatalf("expected 2 roamer states, got %d", len(view.Personas))
	}
	if len(view.Scenes) != 4 {
		t.Fatalf("expected 4 scenes, got %d", len(view.Scenes))
	}
	if view.Running {
		t.Fatal("roaming should be off by default")
	}
}

func TestRoamStepAdvancesWorld(t *testing.T) {
	a := NewApp()
	for i := 0; i < 6; i++ {
		if _, err := a.RoamStep(); err != nil {
			t.Fatalf("roam step: %v", err)
		}
	}
	view := a.GetRoamState()
	if view.Tick != 6 {
		t.Fatalf("expected tick 6, got %d", view.Tick)
	}
	if len(view.Events) == 0 {
		t.Fatal("expected roam events")
	}
	for _, p := range view.Personas {
		if p.Energy <= 0 || p.Energy > 100 {
			t.Fatalf("energy out of range: %v", p.Energy)
		}
		if p.SceneName == "" {
			t.Fatalf("persona %s has no scene", p.Name)
		}
	}
}

func TestPresetAndSkillComposePrompt(t *testing.T) {
	a := NewApp()
	persona, err := a.ApplyPreset("seed-1", "preset-rain")
	if err != nil {
		t.Fatalf("apply preset: %v", err)
	}
	if persona.PresetID != "preset-rain" {
		t.Fatalf("preset not applied: %q", persona.PresetID)
	}
	persona, err = a.ToggleSkill("seed-1", "skill-narrate")
	if err != nil {
		t.Fatalf("toggle skill: %v", err)
	}
	if !containsString(persona.SkillIDs, "skill-narrate") {
		t.Fatal("skill-narrate not equipped")
	}

	a.mu.Lock()
	sys := a.buildSystemPromptLocked(persona, "- 用户喜欢雨声", 120, "雨夜重逢")
	a.mu.Unlock()

	for _, want := range []string{"苍月千鹤", "雨夜长街", "诗意旁白", "用户喜欢雨声", "雨夜重逢", "120"} {
		if !strings.Contains(sys, want) {
			t.Fatalf("system prompt missing %q:\n%s", want, sys)
		}
	}
}

func TestSkillCRUD(t *testing.T) {
	a := NewApp()
	created, err := a.CreateSkill(Skill{Name: "测试技能", Icon: "🧪", Category: "测试", Prompt: "测试提示"})
	if err != nil {
		t.Fatalf("create skill: %v", err)
	}
	if created.ID == "" || created.Builtin {
		t.Fatal("bad created skill")
	}
	if err := a.DeleteSkill(created.ID); err != nil {
		t.Fatalf("delete skill: %v", err)
	}
	if err := a.DeleteSkill("skill-qin"); err == nil {
		t.Fatal("builtin skill should not be deletable")
	}
}

func TestChatMockMode(t *testing.T) {
	a := offlineApp()
	msg, err := a.Chat(ChatRequest{PersonaID: "seed-1", Message: "今晚的月亮真好看，你也在看吗"})
	if err != nil {
		t.Fatalf("chat: %v", err)
	}
	if msg.Content == "" || msg.PersonaName != "苍月千鹤" {
		t.Fatalf("bad mock reply: %+v", msg)
	}
}

func TestGroupsSeedAndReply(t *testing.T) {
	a := NewApp()
	groups := a.ListGroups()
	if len(groups) != 3 {
		t.Fatalf("expected 3 rooms, got %d", len(groups))
	}
	var script *Group
	for _, g := range groups {
		if g.Kind == "script" {
			script = g
		}
	}
	if script == nil {
		t.Fatal("expected a script room")
	}
	before := len(script.Messages)
	updated, err := a.SendGroupMessage(script.ID, "我也在雨里", "text", "")
	if err != nil {
		t.Fatalf("send group message: %v", err)
	}
	if len(updated.Messages) <= before {
		t.Fatal("expected user message plus replies")
	}
	if updated.Messages[before].Role != "user" {
		t.Fatalf("expected user message, got %q", updated.Messages[before].Role)
	}
}

func TestGroupTickerSpeaks(t *testing.T) {
	a := NewApp()
	groups := a.ListGroups()
	before := len(groups[0].Messages)
	a.mu.Lock()
	a.groupTickLocked()
	a.mu.Unlock()
	total := 0
	for _, g := range a.ListGroups() {
		total += len(g.Messages)
	}
	if total <= before {
		t.Fatal("expected a room to gain a message")
	}
}

func TestEmptyTagReturnsEmptySlice(t *testing.T) {
	a := NewApp()
	posts := a.GetPosts("不存在的标签")
	if posts == nil {
		t.Fatal("filtered posts must be an empty slice, not nil (JSON null crashes the UI)")
	}
	if len(posts) != 0 {
		t.Fatalf("expected 0 posts, got %d", len(posts))
	}
}

func TestChatUsesConfiguredEndpoint(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/chat/completions" {
			t.Errorf("unexpected path %s", r.URL.Path)
		}
		if r.Header.Get("Authorization") != "Bearer test-key" {
			t.Errorf("missing bearer token")
		}
		_ = json.NewEncoder(w).Encode(map[string]any{
			"choices": []map[string]any{{"message": map[string]string{"content": "来自真实接口的回复"}}},
		})
	}))
	defer srv.Close()

	a := NewApp()
	a.settings.APIKey = "test-key"
	a.settings.APIEndpoint = srv.URL
	msg, err := a.Chat(ChatRequest{PersonaID: "seed-1", Message: "你好"})
	if err != nil {
		t.Fatalf("chat: %v", err)
	}
	if msg.Content != "来自真实接口的回复" {
		t.Fatalf("unexpected reply: %q", msg.Content)
	}
	if msg.Mock {
		t.Fatal("reply from a configured endpoint must not be marked mock")
	}
	if hist := a.GetPersonaMessages("seed-1"); len(hist) != 2 {
		t.Fatalf("expected user+assistant in history, got %d", len(hist))
	}
}

func TestChatPropagatesModelError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "bad key", http.StatusUnauthorized)
	}))
	defer srv.Close()

	a := NewApp()
	a.settings.APIKey = "bad"
	a.settings.APIEndpoint = srv.URL
	if _, err := a.Chat(ChatRequest{PersonaID: "seed-1", Message: "你好"}); err == nil {
		t.Fatal("expected an error instead of a silent mock fallback")
	}
}

func TestChatMarksMockWhenNoKey(t *testing.T) {
	a := offlineApp()
	msg, err := a.Chat(ChatRequest{PersonaID: "seed-1", Message: "你好"})
	if err != nil {
		t.Fatalf("chat: %v", err)
	}
	if !msg.Mock {
		t.Fatal("expected mock flag when no API key is configured")
	}
}
