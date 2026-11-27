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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.oly.world.data.Intent
import com.oly.world.data.Persona
import com.oly.world.data.View

private fun intentLabel(st: String): String = when (st) {
    "met" -> "✓ 见面"
    "notmet" -> "✗ 没遇上"
    else -> ""
}

@Composable
fun PlanEventsTab(vm: AppViewModel) {
    val v = vm.view ?: return
    var mode by remember { mutableStateOf("plan") }

    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().background(Palette.panel).padding(8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            ModeChip("今日日程", mode == "plan") { mode = "plan" }
            ModeChip("事件流", mode == "events") { mode = "events" }
        }

        if (mode == "plan") {
            LazyColumn(Modifier.fillMaxSize(), contentPadding = androidx.compose.foundation.layout.PaddingValues(bottom = 16.dp)) {
                items(v.personas.size) { i ->
                    PlanCard(v.personas[i], v)
                }
            }
        } else {
            LazyColumn(Modifier.fillMaxSize(), contentPadding = androidx.compose.foundation.layout.PaddingValues(bottom = 16.dp)) {
                val evs = v.log.asReversed()
                items(evs.size) { i ->
                    val e = evs[i]
                    Row(
                        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 3.dp)
                            .background(Palette.panel, RoundedCornerShape(8.dp)).padding(8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("${e.seq}", fontSize = 9.sp, color = Palette.dim, fontFamily = FontFamily.Monospace)
                        Spacer(Modifier.padding(3.dp))
                        Chip(e.type, Palette.cyan)
                        Spacer(Modifier.padding(3.dp))
                        Text(
                            e.text ?: e.data.toString().take(48),
                            fontSize = 11.sp, color = Palette.paper,
                            modifier = Modifier.weight(1f), maxLines = 2,
                        )
                    }
                }
                if (v.log.isEmpty()) {
                    item { EmptyHint("还没有事件") }
                }
            }
        }
    }
}

@Composable
private fun ModeChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier
            .background(if (on) Palette.gold.copy(alpha = 0.18f) else Palette.panel2, RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 7.dp),
    ) {
        Text(label, fontSize = 12.sp, color = if (on) Palette.gold2 else Palette.muted)
    }
}

@Composable
private fun PlanCard(p: Persona, v: View) {
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)
            .background(Palette.panel, RoundedCornerShape(10.dp)).padding(10.dp),
    ) {
        Text("${p.avatar} ${p.name} · day ${v.day}", fontSize = 13.sp, color = Palette.paper)
        Spacer(Modifier.padding(2.dp))
        val intents: List<Intent> = p.plan?.intents ?: emptyList()
        if (intents.isEmpty()) {
            Text("今天还没有日程", fontSize = 11.sp, color = Palette.dim, modifier = Modifier.padding(top = 4.dp))
        }
        intents.forEach { it ->
            val now = v.tickOfDay
            val inWindow = it.status != "met" && it.status != "notmet" && now >= it.start && now < it.end
            val color = when {
                it.status == "met" -> Palette.cyan
                it.status == "notmet" -> Palette.red
                inWindow -> Palette.gold2
                else -> Palette.muted
            }
            Row(
                Modifier.fillMaxWidth().padding(top = 4.dp)
                    .background(if (inWindow) Palette.gold.copy(alpha = 0.08f) else Palette.panel2, RoundedCornerShape(6.dp))
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    "${clockOf(it.start)}–${clockOf(it.end)}",
                    fontSize = 10.sp, color = color, fontFamily = FontFamily.Monospace,
                )
                Spacer(Modifier.width(6.dp))
                Text(
                    if (it.kind == "visit") {
                        "→ 串门找 " + (v.personas.firstOrNull { x -> x.id == it.target }?.name ?: it.target)
                    } else {
                        v.scenes.firstOrNull { s -> s.id == it.sceneId }?.name ?: it.sceneId
                    },
                    fontSize = 11.sp, color = Palette.paper, modifier = Modifier.weight(1f), maxLines = 1,
                )
                val st = intentLabel(it.status)
                if (st.isNotEmpty()) Text(st, fontSize = 10.sp, color = color)
            }
        }
    }
}
