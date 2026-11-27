package com.oly.world.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.oly.world.data.Message
import java.time.OffsetDateTime
import java.time.format.DateTimeFormatter

private val timeFmt = DateTimeFormatter.ofPattern("HH:mm")

private fun timeOf(iso: String): String = try {
    timeFmt.format(OffsetDateTime.parse(iso))
} catch (_: Exception) {
    ""
}

@Composable
fun ChatTab(vm: AppViewModel) {
    val v = vm.view ?: return

    Column(Modifier.fillMaxSize().imePadding()) {
        // 会话头部
        Column(Modifier.fillMaxWidth().background(Palette.panel).padding(horizontal = 14.dp, vertical = 10.dp)) {
            Text(vm.sessionTitle(), fontSize = 16.sp, color = Palette.paper)
            Text(
                vm.sessionSubtitle(),
                fontSize = 11.sp,
                color = Palette.dim,
                modifier = Modifier.padding(top = 2.dp),
            )
        }

        // 消息列表
        val msgs = vm.messages()
        val listState = rememberLazyListState()
        LaunchedEffect(msgs.size, selHash(vm)) {
            if (msgs.isNotEmpty()) listState.animateScrollToItem(msgs.size - 1)
        }
        LazyColumn(
            state = listState,
            modifier = Modifier.weight(1f).fillMaxWidth(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 10.dp),
        ) {
            if (msgs.isEmpty()) {
                item { EmptyHint("还没有消息。说一句，或者去世界页推进一分钟。") }
            }
            items(msgs) { m ->
                MessageRow(m)
            }
            if (vm.busy) {
                item { Text("对方正在输入…", fontSize = 11.sp, color = Palette.dim, modifier = Modifier.padding(horizontal = 14.dp, vertical = 4.dp)) }
            }
        }

        // 输入栏
        Row(
            Modifier.fillMaxWidth().background(Palette.panel).padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.Bottom,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            var draft by remember { mutableStateOf("") }
            Box(
                Modifier
                    .weight(1f)
                    .heightIn(min = 42.dp, max = 120.dp)
                    .background(Palette.panel2, RoundedCornerShape(8.dp))
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                androidx.compose.foundation.text.BasicTextField(
                    value = draft,
                    onValueChange = { draft = it },
                    textStyle = TextStyle(color = Palette.paper, fontSize = 14.sp),
                    modifier = Modifier.fillMaxWidth(),
                    decorationBox = { innerTextField ->
                        Column {
                            if (draft.isEmpty()) {
                                Text("说点什么…", fontSize = 14.sp, color = Palette.dim)
                            }
                            innerTextField()
                        }
                    },
                )
            }
            Button(
                onClick = {
                    vm.send(draft)
                    draft = ""
                },
                enabled = !vm.busy && draft.isNotBlank(),
                colors = ButtonDefaults.buttonColors(
                    containerColor = Palette.gold,
                    contentColor = Palette.ink,
                    disabledContainerColor = Palette.panel2,
                    disabledContentColor = Palette.dim,
                ),
                shape = RoundedCornerShape(8.dp),
                modifier = Modifier.heightIn(min = 42.dp),
            ) {
                Text(if (vm.busy) "…" else "发送")
            }
        }
    }
}

/** 选中目标变化时也触发滚动。 */
private fun selHash(vm: AppViewModel): String = vm.sel.kind + ":" + vm.sel.id

@Composable
private fun MessageRow(m: Message) {
    if (m.actor == "system") {
        Text(
            m.text,
            fontSize = 11.sp,
            color = Palette.cyan,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 4.dp),
        )
        return
    }
    val mine = m.actor == "user"
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 3.dp),
        horizontalArrangement = if (mine) Arrangement.End else Arrangement.Start,
    ) {
        Column(
            Modifier.widthIn(max = 320.dp),
            horizontalAlignment = if (mine) Alignment.End else Alignment.Start,
        ) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                Text(m.name.ifEmpty { if (mine) "我" else "" }, fontSize = 10.sp, color = Palette.muted)
                if (m.mock) Chip("mock", Palette.red)
                m.score?.let { Chip("×%.2f".format(it), Palette.cyan) }
                Text(timeOf(m.at), fontSize = 10.sp, color = Palette.dim)
            }
            Box(
                Modifier
                    .padding(top = 2.dp)
                    .background(
                        if (mine) Palette.gold.copy(alpha = 0.16f) else Palette.panel2,
                        RoundedCornerShape(
                            topStart = 4.dp, topEnd = 10.dp, bottomStart = 10.dp, bottomEnd = 10.dp,
                        ),
                    )
                    .padding(horizontal = 10.dp, vertical = 7.dp),
            ) {
                Text(m.text, fontSize = 14.sp, color = Palette.paper, fontFamily = FontFamily.Default)
            }
        }
    }
}
