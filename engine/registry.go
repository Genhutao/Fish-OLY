package main

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
)

// userRegistry 管用户名单：users/ 目录下每个子目录就是一个用户世界。
// 默认用户固定叫 default，旧客户端别名都落在它上面。
type userRegistry struct {
	mu      sync.Mutex
	dataDir string
	def     string
}

var uidPattern = regexp.MustCompile(`^[a-zA-Z0-9_-]{1,32}$`)

func newUserRegistry(dataDir string) *userRegistry {
	return &userRegistry{dataDir: dataDir, def: "default"}
}

// List 返回所有用户 id（字母序）。
func (r *userRegistry) List() []string {
	r.mu.Lock()
	defer r.mu.Unlock()
	entries, err := os.ReadDir(r.dataDir)
	if err != nil {
		return nil
	}
	out := []string{}
	for _, e := range entries {
		if e.IsDir() && uidPattern.MatchString(e.Name()) && !strings.HasPrefix(e.Name(), ".") {
			out = append(out, e.Name())
		}
	}
	sort.Strings(out)
	return out
}

// Default 返回默认用户 id（永远存在）。
func (r *userRegistry) Default() string { return r.def }

// Create 新建一个用户世界目录，返回它的 id。
func (r *userRegistry) Create(name string) (string, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	uid := slugify(name)
	if uid == "" {
		return "", fmt.Errorf("用户名只能用字母、数字、-、_")
	}
	dir := filepath.Join(r.dataDir, uid)
	if _, err := os.Stat(dir); err == nil {
		return uid, nil // 已存在就直接复用
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	return uid, nil
}

// EnsureDefault 保证默认用户存在。
func (r *userRegistry) EnsureDefault() {
	r.mu.Lock()
	defer r.mu.Unlock()
	_ = os.MkdirAll(filepath.Join(r.dataDir, r.def), 0o755)
}

// slugify 把任意名字压成合法目录名。中文等非法字符直接丢弃，空了就拒绝。
func slugify(name string) string {
	name = strings.ToLower(strings.TrimSpace(name))
	var b strings.Builder
	for _, r := range name {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '-' || r == '_' {
			b.WriteRune(r)
		}
	}
	s := b.String()
	if len(s) > 32 {
		s = s[:32]
	}
	return s
}
