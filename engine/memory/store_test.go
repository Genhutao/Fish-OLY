package memory

import "testing"

// 撤回一条成员之后，槽向量必须等于「剩下的成员重算一遍」。
// 这是整个成员级设计的核心断言：向量是纯函数，不是写入历史的残留。
func TestSlotVectorIsPureFunction(t *testing.T) {
	st := NewStore()
	st.Write("F", "我妈最近住院了，是高血压", 1, 3.5, nil)
	st.Write("F", "我妈最近住院了，是高血压，住了三天", 1, 3.5, nil)

	if n := len(st.Slots()); n != 1 {
		t.Fatalf("两条近重复应该并进同一个槽，实际 %d 个槽", n)
	}
	v := st.Slots()[0]
	if len(v.Members) != 2 {
		t.Fatalf("期望 2 条成员，实际 %d", len(v.Members))
	}

	slot := st.slots[v.ID]
	before := slot.Vec()

	// 撤掉第一条。
	if err := st.Forget(v.Members[0].ID); err != nil {
		t.Fatalf("forget: %v", err)
	}
	after := slot.Vec()

	// 手算：只剩第二条成员，权重 0.25（append）或 1（merge），单成员时都是它本身。
	want := Encode(v.Members[1].Text)
	if after != want {
		t.Fatalf("撤回后向量不等于剩余成员的重算结果")
	}
	if after == before {
		t.Fatalf("撤回没有改变向量，说明还在做累积写入")
	}

	// 恢复，向量必须回到原值——可逆。
	if err := st.Revive(v.Members[0].ID); err != nil {
		t.Fatalf("revive: %v", err)
	}
	if slot.Vec() != before {
		t.Fatalf("恢复后向量没有回到原值")
	}
}

// 墓碑必须下沉到成员：撤一条，槽里别的成员还得在。
func TestForgetKeepsSiblings(t *testing.T) {
	st := NewStore()
	st.Write("E", "昨天在旧书店避雨，翻到一本绝版诗集", 1, 3, nil)
	st.Write("E", "昨天在旧书店避雨，翻到一本绝版诗集，店主养的猫也很好看", 1, 3, nil)

	v := st.Slots()[0]
	if len(v.Members) != 2 {
		t.Fatalf("期望同槽 2 条，实际 %d", len(v.Members))
	}
	if err := st.Forget(v.Members[0].ID); err != nil {
		t.Fatalf("forget: %v", err)
	}
	s := st.Stats()
	if s.Active != 1 || s.Forgotten != 1 {
		t.Fatalf("期望 1 活跃 1 撤回，实际 %+v", s)
	}
	// 槽还在，检索仍然能命中剩下的那条。
	hits := st.Search("旧书店 猫", 3, 1)
	if len(hits) == 0 {
		t.Fatal("撤掉一条成员后，槽不应该整个消失")
	}
}

func TestDuplicateIsNotWrittenTwice(t *testing.T) {
	st := NewStore()
	st.Write("F", "用户对花生过敏", 1, 4, nil)
	_, action := st.Write("F", "用户对花生过敏", 1, 4, nil)
	if action != "dup" {
		t.Fatalf("完全相同的记忆应该判 dup，实际 %s", action)
	}
	if s := st.Stats(); s.Members != 1 {
		t.Fatalf("期望只有 1 条成员，实际 %d", s.Members)
	}
}

func TestLayerIsolation(t *testing.T) {
	st := NewStore()
	st.Write("F", "用户养了一只叫团子的猫", 1, 3, nil)
	st.Write("E", "用户在雨里捡到一只猫", 1, 3, nil)
	if s := st.Stats(); s.Slots != 2 {
		t.Fatalf("不同层不该互相并槽，实际 %d 个槽", s.Slots)
	}
}

// 巩固合并后，实时路径与重放路径必须产出完全相同的槽（含 tags）。
// tags 参与检索打分，两边不一致会让重启后的分数漂移。
func TestConsolidateReplayConsistency(t *testing.T) {
	seed := func(st *Store) *Store {
		// 用事件重放路径造两个同文本的槽（重放不做相似度归并），
		// 巩固时它们的向量相同，必然触发合并。
		st.ApplyWrite("slot-a", "mem-a1", "F", "我妈住院了，是高血压", 1, 3.5, 1, []string{"f1"})
		st.ApplyWrite("slot-b", "mem-b1", "F", "我妈住院了，是高血压", 1, 3.5, 1, []string{"f2"})
		st.ApplyWrite("slot-c", "mem-c1", "E", "昨天在旧书店避雨", 1, 3, 1, nil)
		return st
	}
	live := seed(NewStore())
	rep := live.Consolidate(30)
	if rep.Merged == 0 {
		t.Fatalf("两个同向量槽应当被巩固合并，实际报告 %+v", rep)
	}

	// 重放：同样的槽 + 同一份报告。
	replay := seed(NewStore())
	replay.ApplyConsolidate(rep)

	if live.Stats().Slots != replay.Stats().Slots {
		t.Fatalf("重放槽数不一致: live=%d replay=%d", live.Stats().Slots, replay.Stats().Slots)
	}
	for i, lv := range live.Slots() {
		rv := replay.Slots()[i]
		if lv.ID != rv.ID || lv.Vec != rv.Vec {
			t.Fatalf("槽 %d 不一致:\n live  = %+v\n replay= %+v", i, lv, rv)
		}
		// 关键断言：tags 与实时路径一致
		if len(lv.Tags) != len(rv.Tags) {
			t.Fatalf("tags 数量不一致: live=%v replay=%v", lv.Tags, rv.Tags)
		}
		for j := range lv.Tags {
			if lv.Tags[j] != rv.Tags[j] {
				t.Fatalf("tags 内容不一致: live=%v replay=%v", lv.Tags, rv.Tags)
			}
		}
	}
}
