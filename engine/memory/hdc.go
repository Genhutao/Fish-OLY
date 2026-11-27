// Package memory 是 VSA 记忆内核的 Go 实现。
//
// 与原型的三处差异（见 design/architecture.html 第 04 节）：
//
//  1. 槽向量是成员集合的纯函数：vec = bundle(member × w)，不做累积写入。
//  2. 撤回的单位是「一条成员」，不是「一个槽」。
//  3. 每条成员带自己的权重 w，重放不依赖历史。
package memory

import "math/bits"

const (
	// D 是超向量维度（位）。
	D = 16384
	// Words 是打包后的 uint64 数量。
	Words = D / 64
)

// Vec 是一条打包超向量。
type Vec [Words]uint64

// Bind 是 XOR 绑定：bind(bind(a,b),b) == a。
func Bind(a, b Vec) Vec {
	var out Vec
	for i := range out {
		out[i] = a[i] ^ b[i]
	}
	return out
}

// Sim 返回 [-1,1] 的相似度：1 - 2·hamming/D。
func Sim(a, b Vec) float64 {
	h := 0
	for i := range a {
		h += bits.OnesCount64(a[i] ^ b[i])
	}
	return 1 - 2*float64(h)/float64(D)
}

// Bundle 是加权多数投票。bundle 天生不可逆，所以槽向量永远由成员重算，
// 不要试图「减掉」一条已经并进去的成员。
func Bundle(vs []Vec, ws []float64) Vec {
	if len(vs) == 0 {
		return Vec{}
	}
	if len(vs) == 1 {
		return vs[0]
	}
	score := make([]int32, D)
	for i, v := range vs {
		w := int32(100)
		if ws != nil {
			w = int32(ws[i] * 100)
		}
		if w == 0 {
			continue
		}
		for wi, word := range v {
			base := wi * 64
			for b := 0; b < 64; b++ {
				if word&(1<<uint(b)) != 0 {
					score[base+b] += w
				} else {
					score[base+b] -= w
				}
			}
		}
	}
	var out Vec
	for wi := range out {
		var word uint64
		for b := 0; b < 64; b++ {
			s := score[wi*64+b]
			set := s > 0
			if s == 0 {
				set = tieBit(uint64(wi*64+b))
			}
			if set {
				word |= 1 << uint(b)
			}
		}
		out[wi] = word
	}
	return out
}

func tieBit(pos uint64) bool {
	x := splitmix(pos ^ 0x9E3779B97F4A7C15)
	return x&1 == 1
}

func splitmix(x uint64) uint64 {
	x += 0x9E3779B97F4A7C15
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9
	x = (x ^ (x >> 27)) * 0x94D049BB133111EB
	return x ^ (x >> 31)
}

// Atom 把一个 token 映射成确定性超向量。同样的字符串在任意进程、
// 任意机器上必须得到同一条向量，否则落盘的槽下次加载就对不上了。
func Atom(token string) Vec {
	x := uint64(1469598103934665603)
	for i := 0; i < len(token); i++ {
		x ^= uint64(token[i])
		x *= 1099511628211
	}
	var v Vec
	for i := range v {
		x = splitmix(x)
		v[i] = x
	}
	return v
}

// Hamming 返回两条向量的汉明距离，给调试面板用。
func Hamming(a, b Vec) int {
	h := 0
	for i := range a {
		h += bits.OnesCount64(a[i] ^ b[i])
	}
	return h
}

func popcount(x uint64) int { return bits.OnesCount64(x) }
