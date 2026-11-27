package main

import (
	"embed"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"oly/engine/eval"
	"oly/engine/memory"
	"oly/engine/runtime"
	"oly/engine/world"
)

//go:embed web/index.html
var webFS embed.FS

func main() {
	addr := flag.String("addr", "127.0.0.1:8787", "监听地址")
	data := flag.String("data", "data", "数据目录（每个用户一个子目录）")
	web := flag.String("web", "", "客户端构建产物目录（默认自动找）")
	tickStep := flag.Duration("tick-step", time.Minute, "1 tick = 多长真实时间（演示可设 1s）")
	autoTick := flag.Bool("auto-tick", true, "开调度协程（false 只手动推进）")
	evalDir := flag.String("eval", "", "跑 P0 评测集后退出（传 dataset 目录）")
	calibrate := flag.Bool("calibrate", false, "配合 -eval 扫 nearSim")
	flag.Parse()

	if *evalDir != "" {
		runEval(*evalDir, *calibrate)
		return
	}

	ensureDir(*data)
	reg := newUserRegistry(*data)
	reg.EnsureDefault()

	sc := world.NewScheduler(*data, *tickStep)
	if !*autoTick {
		_ = sc // 协程仍会起，但 realTick 只在 AdvanceTo 里推进；关掉时钟用 -tick-step 1h 也能近似关闭
	}

	client := runtime.NewClient(*data)
	sc.SetGenerator(runtimeClient{client})
	log.Printf("model=%s · tick-step=%s", client.Label(), *tickStep)

	// 迁移：旧单世界数据在 data/ 根上（events.jsonl），搬到 default/ 下。
	migrateLegacyWorld(*data)

	s := &server{sc: sc, gen: runtimeClient{client}, reg: reg, data: *data}
	mux := http.NewServeMux()
	s.register(mux)

	webDir := resolveWeb(*web)
	if webDir != "" {
		mux.Handle("/", spa(webDir))
		log.Printf("前端: %s", webDir)
	} else {
		sub, err := fs.Sub(webFS, "web")
		if err != nil {
			log.Fatalf("内嵌前端损坏: %v", err)
		}
		mux.Handle("/console/", http.StripPrefix("/console/", http.FileServer(http.FS(sub))))
		mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Path == "/" {
				http.Redirect(w, r, "/console/", http.StatusTemporaryRedirect)
				return
			}
			http.NotFound(w, r)
		})
		log.Printf("前端: 内嵌控制台（client/dist 还没构建）")
	}

	log.Printf("OLY 世界引擎（多用户） → http://%s", *addr)
	srv := &http.Server{Addr: *addr, Handler: withCORS(mux), ReadHeaderTimeout: 10 * time.Second}
	if err := srv.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}

// migrateLegacyWorld 把 v1 单世界布局（data/events.jsonl）搬进 default 用户目录。
func migrateLegacyWorld(data string) {
	old := filepath.Join(data, "events.jsonl")
	newDir := filepath.Join(data, "default")
	if _, err := os.Stat(old); err != nil {
		return
	}
	if _, err := os.Stat(filepath.Join(newDir, "events.jsonl")); err == nil {
		return // 已经迁移过
	}
	if err := os.MkdirAll(newDir, 0o755); err != nil {
		return
	}
	for _, name := range []string{"events.jsonl", "memory.json"} {
		if err := os.Rename(filepath.Join(data, name), filepath.Join(newDir, name)); err != nil {
			log.Printf("迁移 %s 失败: %v", name, err)
		}
	}
	log.Printf("旧世界数据已迁移到 %s", newDir)
}

// resolveWeb 依次尝试显式路径、当前目录、可执行文件旁边。
func resolveWeb(explicit string) string {
	cands := []string{}
	if explicit != "" {
		cands = append(cands, explicit)
	}
	cands = append(cands, "client/dist", "../client/dist")
	if exe, err := os.Executable(); err == nil {
		base := filepath.Dir(exe)
		cands = append(cands, filepath.Join(base, "client", "dist"), filepath.Join(base, "..", "client", "dist"))
	}
	for _, c := range cands {
		if _, err := os.Stat(filepath.Join(c, "index.html")); err == nil {
			return c
		}
	}
	return ""
}

// spa 提供静态文件，并让未知路径回落到 index.html。
func spa(dir string) http.Handler {
	files := http.FileServer(http.Dir(dir))
	index := filepath.Join(dir, "index.html")
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/api/") {
			http.NotFound(w, r)
			return
		}
		clean := filepath.Clean("/" + r.URL.Path)
		full := filepath.Join(dir, clean)
		if rel, err := filepath.Rel(dir, full); err != nil || strings.HasPrefix(rel, "..") {
			http.NotFound(w, r)
			return
		}
		if st, err := os.Stat(full); err != nil || st.IsDir() {
			http.ServeFile(w, r, index)
			return
		}
		files.ServeHTTP(w, r)
	})
}

// runEval 是记忆内核的回归闸门：重建 60 条事实，跑 120 题。
func runEval(dir string, sweep bool) {
	facts, qs, err := eval.Load(dir)
	if err != nil {
		log.Fatalf("读取评测集失败: %v", err)
	}
	if sweep {
		for ns := 0.40; ns <= 0.701; ns += 0.02 {
			st := memory.NewStore()
			st.NearSim = ns
			eval.Seed(st, facts)
			rep := eval.Run(st, qs, 30, 5)
			fmt.Printf("nearSim=%.2f  %s  slots=%d\n", ns, rep, st.Stats().Slots)
		}
		return
	}
	st := memory.NewStore()
	eval.Seed(st, facts)
	rep := eval.Run(st, qs, 30, 5)
	fmt.Println(rep)
	fmt.Println(fmtStats(st.Stats()))
	for i, m := range rep.Misses {
		if i >= 8 {
			break
		}
		fmt.Println("  miss:", m)
	}
}

// ── 小工具 ───────────────────────────────────────────────────────────────

func ensureDir(p string) { _ = os.MkdirAll(p, 0o755) }

func fmtStats(st memory.Stats) string {
	return fmt.Sprintf("memory: {slots %d members %d active %d forgotten %d}",
		st.Slots, st.Members, st.Active, st.Forgotten)
}
