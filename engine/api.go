package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"time"

	"oly/engine/runtime"
	"oly/engine/world"
)

// runtimeClient 把 runtime.Client 包成 world.Generator。
type runtimeClient struct{ c *runtime.Client }

func (g runtimeClient) Reply(ctx context.Context, req world.GenerateRequest) (string, bool, error) {
	return g.c.Reply(ctx, runtime.ReplyRequest{
		Persona: req.Persona, Room: req.Room, System: req.System,
		MemBlock: req.MemBlock, History: req.History, UserText: req.UserText,
	})
}
func (g runtimeClient) Label() string { return g.c.Label() }
func (g runtimeClient) Real() bool    { return g.c.Real() }

// server 挂所有路由。世界按用户隔离，路由都带 {uid}。
type server struct {
	sc   *world.Scheduler
	gen  runtimeClient
	reg  *userRegistry
	data string
}

// runtimeClient / userRegistry 由 main.go 提供（见 main.go）。

func (s *server) register(mux *http.ServeMux) {
	// 用户管理
	mux.HandleFunc("GET /api/v1/users", s.users)
	mux.HandleFunc("POST /api/v1/users", s.createUser)

	// 每用户世界
	mux.HandleFunc("GET /api/v1/users/{uid}/state", s.state)
	mux.HandleFunc("GET /api/v1/users/{uid}/stream", s.stream)
	mux.HandleFunc("POST /api/v1/users/{uid}/tick", s.tick)
	mux.HandleFunc("POST /api/v1/users/{uid}/chat", s.chat)
	mux.HandleFunc("POST /api/v1/users/{uid}/scenes/{sid}/messages", s.sceneChat)
	mux.HandleFunc("GET /api/v1/users/{uid}/memory/search", s.search)
	mux.HandleFunc("POST /api/v1/users/{uid}/memory/forget", s.forget)
	mux.HandleFunc("POST /api/v1/users/{uid}/memory/revive", s.revive)
	mux.HandleFunc("POST /api/v1/users/{uid}/memory/consolidate", s.consolidate)

	// 旧单世界别名 → 默认用户（迁移期兼容）
	mux.HandleFunc("GET /api/v1/state", s.legacyState)
	mux.HandleFunc("GET /api/v1/stream", s.legacyStream)
	mux.HandleFunc("POST /api/v1/tick", s.legacyTick)
	mux.HandleFunc("POST /api/v1/chat", s.legacyChat)
	mux.HandleFunc("GET /api/v1/health", s.health)
	mux.HandleFunc("GET /api/health", s.health)
}

func (s *server) world(r *http.Request) (*world.UserWorld, error) {
	return s.sc.World(r.PathValue("uid"))
}

func (s *server) defaultWorld() *world.UserWorld {
	w, _ := s.sc.World(s.reg.Default())
	return w
}

// ── 用户 ─────────────────────────────────────────────────────────────────

func (s *server) users(w http.ResponseWriter, r *http.Request) {
	type uv struct {
		ID    string `json:"id"`
		Tick  int    `json:"tick"`
		Day   int    `json:"day"`
		Model string `json:"model"`
	}
	out := []uv{}
	for _, uid := range s.reg.List() {
		u, err := s.sc.World(uid)
		if err != nil {
			continue
		}
		model := ""
		if u.Gen != nil {
			model = u.Gen.Label()
		}
		out = append(out, uv{ID: uid, Tick: u.St.Tick, Day: u.Day(), Model: model})
	}
	writeJSON(w, map[string]any{"users": out, "default": s.reg.Default()})
}

func (s *server) createUser(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Name string `json:"name"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || strings.TrimSpace(req.Name) == "" {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	uid, err := s.reg.Create(strings.TrimSpace(req.Name))
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if _, err := s.sc.World(uid); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	writeJSON(w, map[string]any{"ok": true, "id": uid})
}

// ── 读 ───────────────────────────────────────────────────────────────────

func (s *server) health(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, map[string]any{
		"ok": true, "service": "oly-engine", "model": s.gen.Label(),
		"users": len(s.reg.List()), "time": time.Now().Format(time.RFC3339),
	})
}

func (s *server) state(w http.ResponseWriter, r *http.Request) {
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	writeJSON(w, u.View())
}

// ── 写 ───────────────────────────────────────────────────────────────────

func (s *server) tick(w http.ResponseWriter, r *http.Request) {
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	u.TickOnce()
	writeJSON(w, u.View())
}

func (s *server) chat(w http.ResponseWriter, r *http.Request) {
	var req struct {
		PersonaID string `json:"personaId"`
		Text      string `json:"text"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || strings.TrimSpace(req.Text) == "" {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 45*time.Second)
	defer cancel()

	res := u.DMChat(ctx, req.PersonaID, req.Text)
	if res.Error != "" {
		w.WriteHeader(http.StatusBadGateway)
	}
	writeJSON(w, map[string]any{"result": res, "view": u.View()})
}

func (s *server) sceneChat(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Text string `json:"text"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || strings.TrimSpace(req.Text) == "" {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 45*time.Second)
	defer cancel()

	res := u.SceneChat(ctx, r.PathValue("sid"), req.Text)
	if res.Error != "" {
		w.WriteHeader(http.StatusBadGateway)
	}
	writeJSON(w, map[string]any{"result": res, "view": u.View()})
}

// ── 记忆 ─────────────────────────────────────────────────────────────────

func (s *server) search(w http.ResponseWriter, r *http.Request) {
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	q := r.URL.Query().Get("q")
	pid := r.URL.Query().Get("personaId")
	k := 6
	fmt.Sscanf(r.URL.Query().Get("k"), "%d", &k)
	if pid == "" {
		if pids := u.St.OrderPersonaIDs(); len(pids) > 0 {
			pid = pids[0]
		}
	}
	writeJSON(w, map[string]any{"query": q, "personaId": pid, "hits": u.SearchMemory(pid, q, k)})
}

func (s *server) forget(w http.ResponseWriter, r *http.Request) {
	var req struct {
		MemberID string `json:"memberId"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	if err := u.Forget(req.MemberID); err != nil {
		w.WriteHeader(http.StatusBadRequest)
		writeJSON(w, map[string]any{"error": err.Error()})
		return
	}
	writeJSON(w, map[string]any{"ok": true, "view": u.View()})
}

func (s *server) revive(w http.ResponseWriter, r *http.Request) {
	var req struct {
		MemberID string `json:"memberId"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	if err := u.Revive(req.MemberID); err != nil {
		w.WriteHeader(http.StatusBadRequest)
		writeJSON(w, map[string]any{"error": err.Error()})
		return
	}
	writeJSON(w, map[string]any{"ok": true, "view": u.View()})
}

func (s *server) consolidate(w http.ResponseWriter, r *http.Request) {
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	reps := u.Consolidate()
	total := world.TotalConsolidate(reps)
	writeJSON(w, map[string]any{"report": total, "view": u.View()})
}

// ── SSE ──────────────────────────────────────────────────────────────────

func (s *server) stream(w http.ResponseWriter, r *http.Request) {
	u, err := s.world(r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	last := ""
	send := func() bool {
		v := u.View()
		sig := fmt.Sprintf("%d/%d/%d/%d/%d/%v", v.Tick, v.Events, v.MessageCount, len(v.Sessions), len(v.Memory), v.Dormant)
		if sig == last {
			return true
		}
		last = sig
		raw, err := json.Marshal(v)
		if err != nil {
			return false
		}
		if _, err := fmt.Fprintf(w, "event: state\ndata: %s\n\n", raw); err != nil {
			return false
		}
		fl.Flush()
		return true
	}

	if !send() {
		return
	}
	t := time.NewTicker(time.Second)
	defer t.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case <-t.C:
			if !send() {
				return
			}
		}
	}
}

// ── 旧单世界别名 ──────────────────────────────────────────────────────────

func (s *server) legacyState(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, s.defaultWorld().View())
}

func (s *server) legacyStream(w http.ResponseWriter, r *http.Request) {
	r.SetPathValue("uid", s.reg.Default())
	s.stream(w, r)
}

func (s *server) legacyTick(w http.ResponseWriter, r *http.Request) {
	r.SetPathValue("uid", s.reg.Default())
	s.tick(w, r)
}

func (s *server) legacyChat(w http.ResponseWriter, r *http.Request) {
	r.SetPathValue("uid", s.reg.Default())
	s.chat(w, r)
}

// ── 工具 ─────────────────────────────────────────────────────────────────

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	_ = json.NewEncoder(w).Encode(v)
}

func withCORS(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		h.ServeHTTP(w, r)
	})
}
