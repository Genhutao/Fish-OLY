package com.oly.world.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

/** 时钟：tick 分钟数 → 世界内时刻（日界 04:00 起）。 */
fun clockOf(m: Int): String {
    val t = (4 * 60 + m).mod(1440)
    return "%02d:%02d".format(t / 60, t % 60)
}

@Composable
fun App(vm: AppViewModel) {
    val snackbar = remember { SnackbarHostState() }
    val serverDown by vm.serverDown.collectAsState()

    LaunchedEffect(vm.toast) {
        vm.toast.takeIf { it.isNotEmpty() }?.let { snackbar.showSnackbar(it) }
    }

    MaterialTheme {
        Scaffold(
            containerColor = Palette.ink,
            snackbarHost = { SnackbarHost(snackbar) },
            bottomBar = { BottomBar(vm) },
        ) { pad ->
            Box(
                Modifier
                    .padding(pad)
                    .fillMaxSize()
                    .statusBarsPadding(),
            ) {
                when {
                    vm.view == null -> BootScreen(serverDown)
                    vm.tab == Tab.CHAT -> ChatTab(vm)
                    vm.tab == Tab.WORLD -> WorldTab(vm)
                    vm.tab == Tab.MEMORY -> MemoryTab(vm)
                    else -> PlanEventsTab(vm)
                }
            }
        }
    }
}

@Composable
private fun BottomBar(vm: AppViewModel) {
    NavigationBar(containerColor = Palette.panel) {
        val items = listOf(
            Tab.CHAT to "聊天",
            Tab.WORLD to "世界",
            Tab.MEMORY to "记忆",
            Tab.PLAN_EVENTS to "日程·事件",
        )
        items.forEach { (tab, label) ->
            NavigationBarItem(
                selected = vm.tab == tab,
                onClick = { vm.tab = tab },
                icon = {},
                label = { Text(label) },
            )
        }
    }
}

@Composable
private fun BootScreen(serverDown: Boolean) {
    Column(
        Modifier.fillMaxSize().background(Palette.ink),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            "OLY",
            fontSize = 42.sp,
            letterSpacing = 0.2.em,
            color = Palette.gold2,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Serif,
        )
        Text(
            if (serverDown) "连不上引擎。检查服务器地址与网络。"
            else "正在连接世界…",
            fontSize = 12.sp,
            color = Palette.dim,
            modifier = Modifier.padding(top = 8.dp),
        )
        if (!serverDown) {
            CircularProgressIndicator(color = Palette.gold, modifier = Modifier.padding(top = 16.dp))
        }
    }
}

/** 分组小标题。 */
@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        style = MaterialTheme.typography.titleSmall,
        color = Palette.gold2,
        modifier = modifier.padding(horizontal = 12.dp, vertical = 6.dp),
    )
}

/** 通用小徽章。 */
@Composable
fun Chip(text: String, color: androidx.compose.ui.graphics.Color = Palette.muted) {
    Box(
        Modifier
            .background(color.copy(alpha = 0.14f), MaterialTheme.shapes.small)
            .padding(horizontal = 6.dp, vertical = 2.dp),
    ) {
        Text(text, fontSize = 10.sp, color = color)
    }
}

@Composable
fun EmptyHint(text: String) {
    Text(
        text,
        color = Palette.dim,
        fontSize = 12.sp,
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth().padding(vertical = 24.dp),
    )
}
