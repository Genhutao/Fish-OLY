package memory

import (
	"strings"
	"sync"
	"unicode"
)

// 中文里到处都有、几乎不携带区分度的字。
var stopChars = map[rune]bool{}

var queryFillers = []string{
	"怎么样", "怎么", "什么", "哪些", "哪个", "多少", "是不是", "有没有", "后来",
	"最近", "现在", "一下", "告诉我", "记得", "还记得", "吗", "呢", "吧", "啊", "？", "?",
}

func init() {
	for _, r := range "的了是在我你他她它也就都和有不这那个上下吗呢吧啊要会说法去来一到把被给让很还又着过地得们这那么些么" {
		stopChars[r] = true
	}
}

// Tokenize 是「字 + 双字」。中文不需要分词器，双字已经能抓住大部分搭配。
func Tokenize(text string) []string {
	var toks []string
	var seg []rune
	flush := func() {
		if len(seg) == 0 {
			return
		}
		for _, r := range seg {
			if !stopChars[r] {
				toks = append(toks, string(r))
			}
		}
		for i := 0; i+1 < len(seg); i++ {
			toks = append(toks, string(seg[i:i+2]))
		}
		seg = seg[:0]
	}
	for _, r := range text {
		if unicode.IsSpace(r) || unicode.IsPunct(r) || unicode.IsSymbol(r) {
			flush()
			continue
		}
		seg = append(seg, r)
	}
	flush()
	return toks
}

func tokenWeight(t string) float64 {
	if len([]rune(t)) == 2 {
		return 1.5
	}
	return 1.0
}

var (
	atomMu    sync.RWMutex
	atomCache = map[string]Vec{}
)

func atomOf(t string) Vec {
	atomMu.RLock()
	v, ok := atomCache[t]
	atomMu.RUnlock()
	if ok {
		return v
	}
	v = Atom(t)
	atomMu.Lock()
	if len(atomCache) > 50000 {
		atomCache = map[string]Vec{}
	}
	atomCache[t] = v
	atomMu.Unlock()
	return v
}

// Encode 把一段文本编码成超向量。同样输入永远得到同一条向量。
func Encode(text string) Vec {
	toks := Tokenize(text)
	if len(toks) == 0 {
		return Atom("empty:" + text)
	}
	vs := make([]Vec, len(toks))
	ws := make([]float64, len(toks))
	for i, t := range toks {
		vs[i] = atomOf(t)
		ws[i] = tokenWeight(t)
	}
	return Bundle(vs, ws)
}

// EncodeQuery 先剥掉疑问词再编码，避免「怎么样」污染相似度。
func EncodeQuery(text string) Vec {
	q := text
	for _, f := range queryFillers {
		q = strings.ReplaceAll(q, f, "")
	}
	if strings.TrimSpace(q) == "" {
		q = text
	}
	return Encode(q)
}
