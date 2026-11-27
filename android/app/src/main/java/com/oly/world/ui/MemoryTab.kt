package com.oly.world.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.oly.world.data.Member
import com.oly.world.data.Slot

/** 记忆层中文标签（对齐 Web 版 LAYER_NAME）。 */
private val LAYER_NAME = mapOf(
    "K" to "关系", "R" to "人格", "E" to "情景", "F" to "事实", "WM" to "工作", "M" to "元记忆",
)

@Composable
fun MemoryTab(vm: AppViewModel) {
    val v = vm.view ?: return
    val stats = v.memory[vm.memPid]

    Column(Modifier.fillMaxSize()) {
        // 角色选择行
        LazyRow(
            Modifier.fillMaxWidth().background(Palette.panel).padding(horizontal = 10.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            items(v.personas.size) { i ->
                val p = v.personas[i]
                val on = p.id == vm.memPid
                Box(
                    Modifier
                        .background(if (on) Palette.gold.copy(alpha = 0.18f) else Palette.panel2, RoundedCornerShape(8.dp))
                        .clickable { vm.pickMemPid(p.id) }
                        .padding(horizontal = 10.dp, vertical = 6.dp),
                ) {
                    Text("${p.avatar} ${p.name}", fontSize = 12.sp, color = if (on) Palette.gold2 else Palette.muted)
                }
            }
        }
        // 检索行
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            var query by remember { mutableStateOf("") }
            Box(
                Modifier.weight(1f).heightIn(min = 40.dp)
                    .background(Palette.panel2, RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                BasicTextField(
                    value = query,
                    onValueChange = { query = it },
                    textStyle = TextStyle(color = Palette.paper, fontSize = 13.sp),
                    modifier = Modifier.fillMaxWidth(),
                    decorationBox = { inner ->
                        val name = v.personas.firstOrNull { it.id == vm.memPid }?.name ?: ""
                        if (query.isEmpty()) Text("检索 $name 的记忆", fontSize = 13.sp, color = Palette.dim)
                        inner()
                    },
                )
            }
            Button(
                onClick = { vm.search(query) },
                enabled = query.isNotBlank(),
                colors = ButtonDefaults.buttonColors(containerColor = Palette.gold, contentColor = Palette.ink),
                shape = RoundedCornerShape(8.dp),
            ) { Text("检索") }
            if (vm.hits.isNotEmpty()) {
                TextButton(onClick = { vm.clearSearch() }) { Text("清除", fontSize = 12.sp, color = Palette.muted) }
            }
        }

        LazyColumn(Modifier.fillMaxSize(), contentPadding = androidx.compose.foundation.layout.PaddingValues(bottom = 16.dp)) {
            // 注入预览
            vm.recall?.takeIf { it.first.isNotEmpty() }?.let { (block, remember) ->
                item {
                    Column(
                        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)
                            .background(Palette.panel2, RoundedCornerShape(10.dp)).padding(10.dp),
                    ) {
                        Text("注入到这一轮", fontSize = 11.sp, color = Palette.cyan)
                        Text(block, fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.muted, modifier = Modifier.padding(top = 4.dp))
                        if (remember.isNotEmpty()) {
                            Text("本轮写入 · $remember", fontSize = 10.sp, color = Palette.gold2, modifier = Modifier.padding(top = 6.dp))
                        }
                    }
                }
            }

            // 检索结果
            if (vm.hits.isNotEmpty()) {
                item { SectionTitle("检索结果") }
                items(vm.hits.size) { i ->
                    val h = vm.hits[i]
                    Row(
                        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 3.dp)
                            .background(Palette.panel, RoundedCornerShape(8.dp)).padding(8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("%.3f".format(h.score), fontSize = 11.sp, color = Palette.gold2, fontFamily = FontFamily.Monospace)
                        Spacer(Modifier.padding(3.dp))
                        Chip(h.layer, Palette.cyan)
                        Spacer(Modifier.padding(3.dp))
                        Text(h.text, fontSize = 12.sp, color = Palette.paper, modifier = Modifier.weight(1f))
                    }
                }
            }

            // 槽位
            if (stats != null) {
                item {
                    Text(
                        buildString {
                            append("槽 ${stats.slots} · 成员 ${stats.active}")
                            if (stats.forgotten > 0) append(" · 撤回 ${stats.forgotten}")
                        },
                        fontSize = 12.sp, color = Palette.muted,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                    )
                }
                val slots = (stats.slotsList ?: emptyList()).reversed()
                items(slots.size) { i ->
                    val s = slots[i]
                    SlotCard(s, onForget = { vm.memOp(true, it) }, onRevive = { vm.memOp(false, it) })
                }
            } else {
                item { EmptyHint("选中一个角色查看它的记忆库") }
            }
        }
    }
}

@Composable
private fun SlotCard(slot: Slot, onForget: (String) -> Unit, onRevive: (String) -> Unit) {
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)
            .background(Palette.panel, RoundedCornerShape(10.dp)).padding(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(slot.layer, Palette.gold2)
            Text(LAYER_NAME[slot.layer] ?: "", fontSize = 10.sp, color = Palette.muted)
            Text("${slot.members.size} 成员", fontSize = 10.sp, color = Palette.dim)
            Text("hits ${slot.hits}", fontSize = 10.sp, color = Palette.dim, fontFamily = FontFamily.Monospace)
            Spacer(Modifier.weight(1f))
            Text(slot.vec.take(24), fontSize = 9.sp, color = Palette.dim, fontFamily = FontFamily.Monospace, maxLines = 1)
        }
        if (slot.text.isNotEmpty()) {
            Text(slot.text, fontSize = 13.sp, color = Palette.paper, modifier = Modifier.padding(top = 6.dp))
        }
        slot.tags?.takeIf { it.isNotEmpty() }?.let {
            Text("#" + it.joinToString(" #"), fontSize = 10.sp, color = Palette.cyan, modifier = Modifier.padding(top = 4.dp))
        }
        Column(Modifier.padding(top = 6.dp)) {
            slot.members.forEach { m ->
                MemberRow(m, onForget, onRevive)
            }
        }
    }
}

@Composable
private fun MemberRow(m: Member, onForget: (String) -> Unit, onRevive: (String) -> Unit) {
    val forgotten = m.state == "forgotten"
    Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(
                m.text,
                fontSize = 12.sp,
                color = if (forgotten) Palette.dim else Palette.paper,
                textDecoration = if (forgotten) TextDecoration.LineThrough else null,
            )
            Text(
                "day${m.day} · imp %.1f · w %.2f".format(m.imp, m.w),
                fontSize = 9.sp, color = Palette.dim, fontFamily = FontFamily.Monospace,
            )
        }
        TextButton(onClick = { onForget(m.id) }, enabled = !forgotten) {
            Text("忘", fontSize = 11.sp, color = if (forgotten) Palette.dim else Palette.red)
        }
        TextButton(onClick = { onRevive(m.id) }, enabled = forgotten) {
            Text("复", fontSize = 11.sp, color = if (forgotten) Palette.cyan else Palette.dim)
        }
    }
}
