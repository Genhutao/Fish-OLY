package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sort"
	"strings"
	"time"

	"github.com/google/uuid"
)

// GitHubSkill is a search hit from GitHub that may contain a SKILL.md.
type GitHubSkill struct {
	FullName    string `json:"fullName"`
	Name        string `json:"name"`
	Description string `json:"description"`
	URL         string `json:"url"`
	Stars       int    `json:"stars"`
	Icon        string `json:"icon"`
}

type ghSearchResp struct {
	Items []struct {
		FullName        string `json:"full_name"`
		Name            string `json:"name"`
		Description     string `json:"description"`
		HTMLURL         string `json:"html_url"`
		StargazersCount int    `json:"stargazers_count"`
		Owner           struct {
			Login string `json:"login"`
		} `json:"owner"`
	} `json:"items"`
	Message string `json:"message"`
}

type ghTreeResp struct {
	Tree []struct {
		Path string `json:"path"`
		Type string `json:"type"`
	} `json:"tree"`
	Message string `json:"message"`
}

func (a *App) githubGet(rawURL string, settings AppSettings) ([]byte, error) {
	req, err := http.NewRequest("GET", rawURL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	req.Header.Set("User-Agent", "oly-persona-demo")
	if settings.GitHubToken != "" {
		req.Header.Set("Authorization", "Bearer "+settings.GitHubToken)
	}
	client := &http.Client{Timeout: 15 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("无法连接 GitHub：%v", err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	if resp.StatusCode == 403 || resp.StatusCode == 429 {
		return nil, fmt.Errorf("GitHub 限流，请在设置里填入 GitHub Token")
	}
	if resp.StatusCode >= 400 {
		return nil, fmt.Errorf("GitHub 返回 %d", resp.StatusCode)
	}
	return body, nil
}

// SearchGitHubSkills searches repositories that look like agent skills.
func (a *App) SearchGitHubSkills(query string) ([]GitHubSkill, error) {
	a.mu.Lock()
	settings := a.settings
	a.mu.Unlock()

	q := strings.TrimSpace(query)
	if q == "" {
		q = "ui design"
	}
	// A direct owner/repo is treated as an explicit target.
	if strings.Count(q, "/") == 1 && !strings.ContainsAny(q, " \t") {
		return []GitHubSkill{{
			FullName: q, Name: strings.Split(q, "/")[1],
			Description: "直接导入这个仓库里的第一个 SKILL.md", URL: "https://github.com/" + q,
			Icon: "🐙",
		}}, nil
	}
	search := fmt.Sprintf("%s skill in:name,description", q)
	u := "https://api.github.com/search/repositories?q=" + url.QueryEscape(search) +
		"&sort=stars&order=desc&per_page=15"

	body, err := a.githubGet(u, settings)
	if err != nil {
		return nil, err
	}
	var parsed ghSearchResp
	if err := json.Unmarshal(body, &parsed); err != nil {
		return nil, fmt.Errorf("解析 GitHub 响应失败")
	}
	if parsed.Message != "" && len(parsed.Items) == 0 {
		return nil, fmt.Errorf("%s", parsed.Message)
	}

	out := make([]GitHubSkill, 0, len(parsed.Items))
	for _, it := range parsed.Items {
		blob := strings.ToLower(it.Name + " " + it.Description)
		if !strings.Contains(blob, "skill") && !strings.Contains(blob, "agent") && !strings.Contains(blob, "prompt") {
			continue
		}
		if it.StargazersCount < 5 {
			continue
		}
		out = append(out, GitHubSkill{
			FullName:    it.FullName,
			Name:        it.Name,
			Description: truncate(it.Description, 120),
			URL:         it.HTMLURL,
			Stars:       it.StargazersCount,
			Icon:        "🐙",
		})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Stars > out[j].Stars })
	if len(out) == 0 {
		return curatedSkills(q), nil
	}
	return out[:minInt(12, len(out))], nil
}

// curatedSkills is a small offline fallback so the demo still works when the
// GitHub search API is rate-limited or the query is too narrow.
func curatedSkills(query string) []GitHubSkill {
	all := []GitHubSkill{
		{FullName: "anthropics/skills", Name: "skills", Description: "Anthropic 官方 skills：frontend-design、brand-guidelines、canvas-design 等", URL: "https://github.com/anthropics/skills", Stars: 0, Icon: "🐙"},
		{FullName: "SuperCodeTool/ui-ux-pro-max-skill", Name: "ui-ux-pro-max-skill", Description: "UI/UX 设计智能：50 种风格、21 套配色、字体搭配与栈指南", URL: "https://github.com/SuperCodeTool/ui-ux-pro-max-skill", Stars: 0, Icon: "🐙"},
		{FullName: "solinkz/micro-interactions-skill", Name: "micro-interactions-skill", Description: "20+ 微交互模式、弹簧物理、缓动曲线与可访问性", URL: "https://github.com/solinkz/micro-interactions-skill", Stars: 0, Icon: "🐙"},
		{FullName: "ColourCloudSky/review-ui-design-skill", Name: "review-ui-design-skill", Description: "UI 设计评审：问题标注、优化建议与增量复评", URL: "https://github.com/ColourCloudSky/review-ui-design-skill", Stars: 0, Icon: "🐙"},
		{FullName: "PyModel/css-pro-tips", Name: "css-pro-tips", Description: "现代 CSS 技巧与性能/层叠规范", URL: "https://github.com/PyModel/css-pro-tips", Stars: 0, Icon: "🐙"},
	}
	q := strings.ToLower(query)
	matched := make([]GitHubSkill, 0, len(all))
	for _, c := range all {
		if strings.Contains(strings.ToLower(c.Name+c.Description), q) {
			matched = append(matched, c)
		}
	}
	if len(matched) > 0 {
		return matched
	}
	return all
}

// ImportGitHubSkill finds the first SKILL.md in a repository and imports it
// into the local skill library.
func (a *App) ImportGitHubSkill(repo string) (*Skill, error) {
	a.mu.Lock()
	settings := a.settings
	a.mu.Unlock()

	repo = strings.TrimSpace(repo)
	repo = strings.TrimPrefix(repo, "https://github.com/")
	repo = strings.TrimSuffix(repo, "/")
	if strings.Count(repo, "/") != 1 {
		return nil, fmt.Errorf("仓库格式应为 owner/repo")
	}

	treeURL := fmt.Sprintf("https://api.github.com/repos/%s/git/trees/HEAD?recursive=1", repo)
	body, err := a.githubGet(treeURL, settings)
	if err != nil {
		return nil, err
	}
	var tree ghTreeResp
	if err := json.Unmarshal(body, &tree); err != nil {
		return nil, fmt.Errorf("解析仓库目录失败")
	}

	var candidates []string
	for _, item := range tree.Tree {
		if item.Type == "blob" && strings.HasSuffix(strings.ToLower(item.Path), "skill.md") {
			candidates = append(candidates, item.Path)
		}
	}
	if len(candidates) == 0 {
		return nil, fmt.Errorf("这个仓库里没有找到 SKILL.md")
	}
	if len(candidates) > 1 {
		score := func(p string) int {
			lp := strings.ToLower(p)
			s := len(p)
			if strings.Contains(lp, "template") || strings.Contains(lp, "example") || strings.Contains(lp, "test") {
				s += 500
			}
			if strings.Contains(lp, "frontend") || strings.Contains(lp, "design") || strings.Contains(lp, "ui/") {
				s -= 60
			}
			return s
		}
		sort.Slice(candidates, func(i, j int) bool { return score(candidates[i]) < score(candidates[j]) })
	}
	path := candidates[0]

	rawURL := fmt.Sprintf("https://api.github.com/repos/%s/contents/%s", repo, url.PathEscape(path))
	raw, err := a.githubGet(rawURL, settings)
	if err != nil {
		return nil, err
	}
	content := string(raw)

	name, desc, prompt := parseSkillMarkdown(content)
	if name == "" {
		name = strings.Split(repo, "/")[1]
	}
	if desc == "" {
		desc = "从 GitHub 导入的界面/agent 技能"
	}
	if len(prompt) > 6000 {
		prompt = prompt[:6000] + "…"
	}

	skill := Skill{
		ID:          "skill-" + uuid.New().String(),
		Name:        name,
		Icon:        "🐙",
		Category:    "GitHub",
		Description: truncate(desc, 160),
		Prompt:      prompt,
		Source:      "https://github.com/" + repo + "/blob/HEAD/" + path,
		Builtin:     false,
		CreatedAt:   time.Now(),
	}

	a.mu.Lock()
	a.skills = append(a.skills, skill)
	a.mu.Unlock()
	out := skill
	return &out, nil
}

// parseSkillMarkdown extracts frontmatter name/description and the body, with a
// small fallback for plain markdown files.
func parseSkillMarkdown(content string) (name, desc, prompt string) {
	body := content
	if strings.HasPrefix(content, "---") {
		parts := strings.SplitN(content, "---", 3)
		if len(parts) >= 3 {
			front := parts[1]
			body = parts[2]
			for _, line := range strings.Split(front, "\n") {
				line = strings.TrimSpace(line)
				if strings.HasPrefix(line, "name:") {
					name = strings.Trim(strings.TrimSpace(strings.TrimPrefix(line, "name:")), `"'`)
				}
				if strings.HasPrefix(line, "description:") {
					desc = strings.Trim(strings.TrimSpace(strings.TrimPrefix(line, "description:")), `"'`)
				}
			}
		}
	}
	prompt = strings.TrimSpace(body)
	if prompt == "" {
		prompt = strings.TrimSpace(content)
	}
	return name, desc, prompt
}
