// Package world 是世界域：事件日志 + 状态投影 + 一拍调度。
//
// 不变量：所有变化先写事件，内存里的 State 只是投影。重启时重放事件，
// 世界就回来了——这是「重启不丢世界」唯一的实现方式。
package world

import (
	"bufio"
	"encoding/json"
	"os"
	"sync"
	"time"
)

// Event 是世界史的一行。
type Event struct {
	Seq   int64          `json:"seq"`
	At    time.Time      `json:"at"`
	Type  string         `json:"type"` // message.created | persona.state | persona.moved | world.tick
	Room  string         `json:"room,omitempty"`
	Actor string         `json:"actor,omitempty"`
	Text  string         `json:"text,omitempty"`
	Data  map[string]any `json:"data,omitempty"`
}

// Log 是只追加的事件日志（JSONL）。
type Log struct {
	mu     sync.Mutex
	f      *os.File
	events []Event
	seq    int64
}

// OpenLog 打开（或新建）一个日志文件，并把已有事件读进内存。
func OpenLog(path string) (*Log, error) {
	l := &Log{}
	if f, err := os.Open(path); err == nil {
		sc := bufio.NewScanner(f)
		sc.Buffer(make([]byte, 1024*1024), 1024*1024)
		for sc.Scan() {
			var e Event
			if json.Unmarshal(sc.Bytes(), &e) != nil {
				continue
			}
			l.events = append(l.events, e)
			if e.Seq > l.seq {
				l.seq = e.Seq
			}
		}
		f.Close()
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		return nil, err
	}
	l.f = f
	return l, nil
}

// Append 写一条事件并落盘。
func (l *Log) Append(e Event) (Event, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.seq++
	e.Seq = l.seq
	if e.At.IsZero() {
		e.At = time.Now()
	}
	raw, _ := json.Marshal(e)
	if _, err := l.f.Write(append(raw, '\n')); err != nil {
		return e, err
	}
	l.events = append(l.events, e)
	if len(l.events) > 2000 {
		l.events = l.events[len(l.events)-2000:]
	}
	return e, nil
}

// Events 返回最近 n 条事件（n<=0 表示全部）。
func (l *Log) Events(n int) []Event {
	l.mu.Lock()
	defer l.mu.Unlock()
	if n <= 0 || n > len(l.events) {
		n = len(l.events)
	}
	out := make([]Event, n)
	copy(out, l.events[len(l.events)-n:])
	return out
}

// Count 是事件总数。
func (l *Log) Count() int64 {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.seq
}

func (l *Log) Close() error {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.f.Close()
}
