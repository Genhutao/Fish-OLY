// Package eval 用 VSA 的 P0 数据集给记忆内核打分。
//
// 口径：60 条事实按各自的 day 写入，120 道题各取 top-5，
// 命中判据是「检索结果里有没有出现期望事实的 id」。
package eval

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"

	"oly/engine/memory"
)

type Fact struct {
	ID    string  `json:"id"`
	Day   int     `json:"day"`
	Layer string  `json:"layer"`
	Text  string  `json:"text"`
	Imp   float64 `json:"imp"`
}

type Question struct {
	QID    string   `json:"qid"`
	Type   string   `json:"type"`
	Text   string   `json:"text"`
	Expect []string `json:"expect"`
	Forbid []string `json:"forbid"`
	Note   string   `json:"note"`
}

type Report struct {
	N       int                `json:"n"`        // 有期望答案的题数
	Silence int                `json:"silence"`  // T4：不该召回任何事实的题数
	Recall5 float64            `json:"recall5"`
	MRR     float64            `json:"mrr"`
	Clean   float64            `json:"clean"`    // T4 里没有误召回的占比
	ByType  map[string]float64 `json:"byType"`
	Misses  []string           `json:"misses,omitempty"`
	Leaks   []string           `json:"leaks,omitempty"`
}

func Load(dir string) ([]Fact, []Question, error) {
	var facts []Fact
	var qs []Question
	if err := readJSONL(filepath.Join(dir, "facts.jsonl"), &facts); err != nil {
		return nil, nil, err
	}
	if err := readJSONL(filepath.Join(dir, "questions.jsonl"), &qs); err != nil {
		return nil, nil, err
	}
	return facts, qs, nil
}

func readJSONL(path string, out any) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1024*1024), 1024*1024)
	switch v := out.(type) {
	case *[]Fact:
		for sc.Scan() {
			var x Fact
			if json.Unmarshal(sc.Bytes(), &x) == nil {
				*v = append(*v, x)
			}
		}
	case *[]Question:
		for sc.Scan() {
			var x Question
			if json.Unmarshal(sc.Bytes(), &x) == nil {
				*v = append(*v, x)
			}
		}
	}
	return sc.Err()
}

// Seed 把事实写进一个空库，ref 用事实 id，方便按 id 记分。
func Seed(st *memory.Store, facts []Fact) {
	sort.Slice(facts, func(i, j int) bool { return facts[i].Day < facts[j].Day })
	for _, f := range facts {
		layer := f.Layer
		if layer == "" {
			layer = "F"
		}
		st.Write(layer, f.Text, f.Day, f.Imp, []string{f.ID})
	}
}

// Run 跑一遍题，返回 Recall@5 与 MRR。
func Run(st *memory.Store, qs []Question, nowDay int, k int) Report {
	rep := Report{ByType: map[string]float64{}}
	typeStat := map[string][2]int{}
	mrr := 0.0
	clean := 0

	for _, q := range qs {
		hits := st.Search(q.Text, k, nowDay)
		refs := map[string]bool{}
		for _, h := range hits {
			for _, r := range h.Refs {
				refs[r] = true
			}
		}

		// T4：没有期望答案，考的是「不该说什么」。
		if len(q.Expect) == 0 {
			rep.Silence++
			leak := false
			for _, f := range q.Forbid {
				if refs[f] {
					leak = true
				}
			}
			if leak {
				rep.Leaks = append(rep.Leaks, q.QID+" "+q.Text)
			} else {
				clean++
			}
			continue
		}

		rep.N++
		rank := 0
		for i, h := range hits {
			for _, r := range h.Refs {
				for _, want := range q.Expect {
					if r == want && rank == 0 {
						rank = i + 1
					}
				}
			}
		}
		ok := 0
		if rank > 0 {
			ok = 1
			mrr += 1 / float64(rank)
		} else {
			rep.Misses = append(rep.Misses, q.QID+" "+q.Text)
		}
		rep.Recall5 += float64(ok)
		cur := typeStat[q.Type]
		cur[0] += ok
		cur[1]++
		typeStat[q.Type] = cur
	}

	if rep.N > 0 {
		rep.Recall5 /= float64(rep.N)
		rep.MRR = mrr / float64(rep.N)
	}
	if rep.Silence > 0 {
		rep.Clean = float64(clean) / float64(rep.Silence)
	}
	for t, s := range typeStat {
		if s[1] > 0 {
			rep.ByType[t] = float64(s[0]) / float64(s[1])
		}
	}
	return rep
}

func (r Report) String() string {
	s := fmt.Sprintf("n=%d  recall@5=%.3f  mrr=%.3f  silence=%d clean=%.2f", r.N, r.Recall5, r.MRR, r.Silence, r.Clean)
	keys := make([]string, 0, len(r.ByType))
	for k := range r.ByType {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		s += fmt.Sprintf("  %s=%.2f", k, r.ByType[k])
	}
	return s
}
