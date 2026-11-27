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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.oly.world.data.Persona
import com.oly.world.data.Scene
import com.oly.world.data.Session

private fun statusLabel(s: Session): String = when (s.status) {
    "active" -> "进行中"
    "ended" -> "已结束 · ${s.said} 句"
    "cancelled" -> "已取消"
    "notmet" -> "没遇上"
    else -> "已排定"
}

@Composable
fun WorldTab(vm: AppViewModel) {
    val v = vm.view ?: return
    var showSettings by remember { mutableStateOf(false) }

    LazyColumn(Modifier.fillMaxSize(), contentPadding = androidx.compose.foundation.layout.PaddingValues(bottom = 16.dp)) {
        // ── 世界状态条 ──
        item {
            Column(Modifier.fillMaxWidth().background(Palette.panel).padding(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("OLY", fontFamily = FontFamily.Serif, fontSize = 17.sp, letterSpacing = 0.14.em, color = Palette.paper)
                    Text(" 世界引擎", fontFamily = FontFamily.Serif, fontSize = 13.sp, color = Palette.gold2)
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = { showSettings = true }) { Text("服务器", fontSize = 11.sp, color = Palette.cyan) }
                    UserMenu(vm)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.padding(top = 6.dp)) {
                    Stat("模型", v.model)
                    Stat("day", "${v.day} ${v.clock}")
                    Stat("会话", "${v.sessions.count { it.status == "active" }}")
                    Stat("预算", "${v.budget.used.toInt()}/${v.budget.limit.toInt()}", highlight = v.budget.over)
                    if (v.dormant) Chip("沉睡中", Palette.red)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 8.dp)) {
                    Button(
                        onClick = { vm.tick() },
                        colors = ButtonDefaults.buttonColors(containerColor = Palette.gold, contentColor = Palette.ink),
                        shape = RoundedCornerShape(8.dp),
                    ) { Text("+1 分钟") }
                    OutlinedButton(onClick = { vm.consolidate() }, shape = RoundedCornerShape(8.dp)) {
                        Text("巩固")
                    }
                }
            }
        }

        // ── 角色 ──
        item { SectionTitle("角色") }
        items(v.personas.size) { i ->
            PersonaCard(
                p = v.personas[i],
                active = vm.sel.isDm && vm.sel.id == v.personas[i].id,
                onClick = {
                    vm.select(Sel("dm", v.personas[i].id))
                    vm.tab = Tab.CHAT
                },
            )
        }

        // ── 场景 ──
        item { SectionTitle("场景") }
        items(v.scenes.size) { i ->
            SceneRow(
                s = v.scenes[i],
                active = !vm.sel.isDm && vm.sel.id == v.scenes[i].id,
                onClick = {
                    vm.select(Sel("scene", v.scenes[i].id))
                    vm.tab = Tab.CHAT
                },
            )
        }

        // ── 今日会话 ──
        item { SectionTitle("今日会话") }
        val today = v.sessions.filter { it.day == v.day && (it.kind == "pair" || it.status == "active") }.take(6)
        if (today.isEmpty()) {
            item { EmptyHint("今天暂时没有串门安排") }
        } else {
            items(today.size) { i ->
                val s = today[i]
                Row(
                    Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)
                        .background(Palette.panel2, RoundedCornerShape(8.dp)).padding(10.dp),
                ) {
                    Text("${clockOf(s.start)}–${clockOf(s.end)}", fontSize = 11.sp, color = Palette.gold2, fontFamily = FontFamily.Monospace)
                    Spacer(Modifier.width(8.dp))
                    Text(if (s.kind == "pair") "串门" else "独处", fontSize = 11.sp, color = Palette.cyan)
                    Spacer(Modifier.width(8.dp))
                    Text(s.sceneName ?: s.sceneId, fontSize = 11.sp, color = Palette.paper, modifier = Modifier.weight(1f))
                    Text(statusLabel(s), fontSize = 11.sp, color = Palette.muted)
                }
            }
        }

        // 巩固报告
        vm.lastConsolidate?.let { r ->
            item {
                Text(
                    "上次巩固：合并 ${r.merged} · 升降 ${r.rebalanced} · 遗忘 ${r.forgotten} · 槽 ${r.slotsAfter}",
                    fontSize = 11.sp, color = Palette.dim,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                )
            }
        }
    }

    if (showSettings) {
        ServerDialog(vm) { showSettings = false }
    }
}

@Composable
private fun Stat(label: String, value: String, highlight: Boolean = false) {
    Row {
        Text("$label ", fontSize = 11.sp, color = Palette.dim)
        Text(
            value,
            fontSize = 11.sp,
            color = if (highlight) Palette.red else Palette.gold2,
            fontFamily = FontFamily.Monospace,
        )
    }
}

@Composable
private fun PersonaCard(p: Persona, active: Boolean, onClick: () -> Unit) {
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)
            .background(if (active) Palette.gold.copy(alpha = 0.10f) else Palette.panel, RoundedCornerShape(10.dp))
            .clickable(onClick = onClick)
            .padding(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(p.avatar, fontSize = 22.sp)
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(p.name, fontSize = 14.sp, color = Palette.paper)
                    if (active) Chip("单聊中", Palette.gold2)
                }
                Text(
                    buildString {
                        append(p.mood)
                        append(" · 好感 %.1f".format(p.affection))
                        p.bond?.takeIf { it.isNotEmpty() }?.let {
                            append(" · ")
                            append(it.values.joinToString("/") { "%.1f".format(it) })
                        }
                    },
                    fontSize = 11.sp, color = Palette.muted,
                )
            }
        }
        // 精力条
        Box(
            Modifier.fillMaxWidth().padding(top = 6.dp).height(5.dp)
                .background(Palette.panel2, RoundedCornerShape(3.dp)),
        ) {
            Box(
                Modifier.fillMaxWidth(fraction = (p.energy.coerceIn(3.0, 100.0) / 100.0).toFloat())
                    .height(5.dp)
                    .background(if (p.energy < 30) Palette.red else Palette.cyan, RoundedCornerShape(3.dp)),
            )
        }
        Text(p.intentNow ?: p.activity, fontSize = 11.sp, color = Palette.dim, modifier = Modifier.padding(top = 4.dp))
    }
}

@Composable
private fun SceneRow(s: Scene, active: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)
            .background(if (active) Palette.gold.copy(alpha = 0.10f) else Palette.panel, RoundedCornerShape(10.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(s.name, fontSize = 14.sp, color = Palette.paper)
            Text((s.here?.joinToString("、")?.ifEmpty { "空" }) ?: "空", fontSize = 11.sp, color = Palette.muted, modifier = Modifier.padding(top = 2.dp))
        }
        Text("${s.here?.size ?: 0}/${s.capacity}", fontSize = 11.sp, color = Palette.gold2, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun UserMenu(vm: AppViewModel) {
    var open by remember { mutableStateOf(false) }
    var creating by remember { mutableStateOf(false) }
    var newName by remember { mutableStateOf("") }

    Box {
        TextButton(onClick = { open = true }) {
            Text("${vm.uid} · day${vm.view?.day ?: "?"}", fontSize = 11.sp, color = Palette.gold2)
        }
        androidx.compose.material3.DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            vm.users.forEach { u ->
                androidx.compose.material3.DropdownMenuItem(
                    text = { Text("${u.id} · day${u.day}", fontSize = 13.sp) },
                    onClick = {
                        open = false
                        vm.switchUser(u)
                    },
                )
            }
            androidx.compose.material3.DropdownMenuItem(
                text = { Text("＋ 新建世界…", fontSize = 13.sp, color = Palette.gold2) },
                onClick = {
                    open = false
                    creating = true
                },
            )
        }
    }
    if (creating) {
        AlertDialog(
            onDismissRequest = { creating = false },
            containerColor = Palette.panel,
            title = { Text("新建用户世界", color = Palette.paper) },
            text = {
                OutlinedTextField(
                    value = newName,
                    onValueChange = { newName = it },
                    placeholder = { Text("用户名", color = Palette.dim) },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = Palette.gold,
                        cursorColor = Palette.gold2,
                    ),
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    vm.createUser(newName)
                    newName = ""
                    creating = false
                }) { Text("创建", color = Palette.gold2) }
            },
            dismissButton = {
                TextButton(onClick = { creating = false }) { Text("取消", color = Palette.muted) }
            },
        )
    }
}

@Composable
private fun ServerDialog(vm: AppViewModel, onDismiss: () -> Unit) {
    var url by remember { mutableStateOf(vm.serverUrl) }
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = Palette.panel,
        title = { Text("服务器地址", color = Palette.paper) },
        text = {
            Column {
                Text(
                    "模拟器默认 http://10.0.2.2:8787（宿主机回环）。\n真机请填电脑局域网 IP，引擎需以 -addr 0.0.0.0 启动。",
                    fontSize = 11.sp, color = Palette.muted,
                )
                Spacer(Modifier.height(10.dp))
                OutlinedTextField(
                    value = url,
                    onValueChange = { url = it },
                    singleLine = true,
                    textStyle = androidx.compose.ui.text.TextStyle(color = Palette.paper, fontSize = 13.sp, fontFamily = FontFamily.Monospace),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = Palette.gold,
                        cursorColor = Palette.gold2,
                    ),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                vm.applyServerUrl(url)
                onDismiss()
            }) { Text("保存并重连", color = Palette.gold2) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("取消", color = Palette.muted) } },
    )
}
