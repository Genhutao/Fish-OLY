package com.oly.world.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.oly.world.data.ChatResult
import com.oly.world.data.ConsolidateReport
import com.oly.world.data.Hit
import com.oly.world.data.Message
import com.oly.world.data.OlyApi
import com.oly.world.data.Scene
import com.oly.world.data.ServerConfig
import com.oly.world.data.StateStream
import com.oly.world.data.UsersInfo
import com.oly.world.data.View
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.Dispatchers

/** 当前选中会话：场景群聊或角色私聊。 */
data class Sel(val kind: String, val id: String) {
    val isDm get() = kind == "dm"
}

enum class Tab { CHAT, WORLD, MEMORY, PLAN_EVENTS }

class AppViewModel(private val config: ServerConfig) : ViewModel() {

    private val api = OlyApi(config)
    private var stream: StateStream? = null
    private var pollJob: kotlinx.coroutines.Job? = null

    var uid by mutableStateOf("default")
        private set
    var users by mutableStateOf<List<UsersInfo>>(emptyList())
        private set
    var view by mutableStateOf<View?>(null)
        private set
    var sel by mutableStateOf(Sel("scene", ""))
        private set
    var tab by mutableStateOf(Tab.CHAT)
    var memPid by mutableStateOf("")
        private set
    var busy by mutableStateOf(false)
        private set
    var toast by mutableStateOf("")
        private set
    var serverUrl by mutableStateOf(config.baseUrl())
    var hits by mutableStateOf<List<Hit>>(emptyList())
        private set
    var recall by mutableStateOf<Pair<String, String>?>(null) // memBlock to remember
        private set
    var lastConsolidate by mutableStateOf<ConsolidateReport?>(null)
        private set

    private val _serverDown = MutableStateFlow(false)
    val serverDown: StateFlow<Boolean> = _serverDown

    private var toastJob: kotlinx.coroutines.Job? = null

    fun showToast(msg: String) {
        toast = msg
        toastJob?.cancel()
        toastJob = viewModelScope.launch {
            delay(4200)
            toast = ""
        }
    }

    init {
        refreshUsers()
        connect(uid)
        // SSE 兜底轮询：对齐 Web 版 4 秒轮询（stream 挂了才靠它）
        pollJob = viewModelScope.launch {
            while (isActive) {
                delay(4000)
                val v = view ?: continue
                try {
                    val fresh = api.state(uid)
                    if (fresh.tick >= v.tick) applyView(fresh)
                } catch (_: Exception) {
                    _serverDown.value = true
                }
            }
        }
    }

    fun applyView(v: View) {
        _serverDown.value = false
        view = v
        if (sel.id.isEmpty()) {
            v.scenes.firstOrNull()?.let { sel = Sel("scene", it.id) }
        }
        if (memPid.isEmpty() || v.memory[memPid] == null && v.personas.none { it.id == memPid }) {
            memPid = v.personas.firstOrNull()?.id ?: ""
        }
    }

    fun refreshUsers() {
        viewModelScope.launch {
            try {
                users = api.users().users
            } catch (e: Exception) {
                _serverDown.value = true
            }
        }
    }

    fun connect(newUid: String) {
        uid = newUid
        view = null
        sel = Sel("scene", "")
        hits = emptyList()
        recall = null
        stream?.stop()
        stream = StateStream(config) { v -> applyViewOnMain(v) }
        stream?.start(uid)
        viewModelScope.launch {
            try {
                applyView(api.state(uid))
            } catch (e: Exception) {
                _serverDown.value = true
            }
        }
    }

    private fun applyViewOnMain(v: View) {
        viewModelScope.launch { applyView(v) }
    }

    fun switchUser(u: UsersInfo) {
        connect(u.id)
        refreshUsers()
    }

    fun createUser(name: String) {
        if (name.isBlank()) return
        viewModelScope.launch {
            try {
                val r = api.createUser(name.trim())
                refreshUsers()
                connect(r.id)
            } catch (e: Exception) {
                showToast(short(e))
            }
        }
    }

    fun applyServerUrl(url: String) {
        config.setBaseUrl(url)
        serverUrl = config.baseUrl()
        connect(uid) // 换地址后重连
    }

    fun select(sel: Sel) {
        this.sel = sel
    }

    /** 当前会话的消息列表。 */
    fun messages(): List<Message> {
        val v = view ?: return emptyList()
        return if (sel.isDm) {
            v.dms[sel.id] ?: emptyList()
        } else {
            v.scenes.firstOrNull { it.id == sel.id }?.messages ?: emptyList()
        }
    }

    fun sessionTitle(): String {
        val v = view ?: return ""
        return if (sel.isDm) {
            v.personas.firstOrNull { it.id == sel.id }?.name ?: "单聊"
        } else {
            v.scenes.firstOrNull { it.id == sel.id }?.name ?: "场景"
        }
    }

    fun sessionSubtitle(): String {
        val v = view ?: return ""
        if (sel.isDm) return "单聊 · 记忆贴着这条发言注入"
        val sc: Scene = v.scenes.firstOrNull { it.id == sel.id } ?: return ""
        return "${sc.topic}（在场：${sc.here?.joinToString("、") ?: ""}）"
    }

    fun send(text: String) {
        val t = text.trim()
        if (t.isEmpty() || busy || sel.id.isEmpty()) return
        busy = true
        viewModelScope.launch {
            try {
                val res = if (sel.isDm) api.chat(uid, sel.id, t) else api.sceneChat(uid, sel.id, t)
                res.result.error?.let { showToast("模型错误：$it") } ?: run {
                    recall = res.result.memBlock to res.result.remember
                }
                res.view?.let { applyView(it) }
            } catch (e: Exception) {
                showToast(short(e))
            } finally {
                busy = false
            }
        }
    }

    fun tick() {
        viewModelScope.launch {
            try {
                applyView(api.tick(uid))
            } catch (e: Exception) {
                showToast(short(e))
            }
        }
    }

    fun consolidate() {
        viewModelScope.launch {
            try {
                val r = api.consolidate(uid)
                lastConsolidate = r.report
                showToast("巩固完成：合并 ${r.report.merged} · 升降 ${r.report.rebalanced} · 遗忘 ${r.report.forgotten}")
                r.view?.let { applyView(it) }
            } catch (e: Exception) {
                showToast(short(e))
            }
        }
    }

    fun search(q: String) {
        if (q.isBlank() || memPid.isEmpty()) return
        viewModelScope.launch {
            try {
                hits = api.search(uid, memPid, q.trim()).hits
            } catch (e: Exception) {
                showToast(short(e))
            }
        }
    }

    fun clearSearch() {
        hits = emptyList()
    }

    fun memOp(forget: Boolean, memberId: String) {
        viewModelScope.launch {
            try {
                val r = if (forget) api.forget(uid, memberId) else api.revive(uid, memberId)
                r.view?.let { applyView(it) }
            } catch (e: Exception) {
                showToast(short(e))
            }
        }
    }

    fun pickMemPid(pid: String) {
        memPid = pid
        hits = emptyList()
    }

    private fun short(e: Exception): String = e.message?.take(120) ?: e.toString()

    override fun onCleared() {
        stream?.stop()
        pollJob?.cancel()
    }

    companion object {
        fun factory(config: ServerConfig): ViewModelProvider.Factory =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : ViewModel> create(modelClass: Class<T>): T =
                    AppViewModel(config) as T
            }
    }
}

/** 主线程装饰色（暗色墨纸金主题，与 Web 版 styles.css 对齐）。 */
object Palette {
    val ink = Color(0xFF08080B)
    val panel = Color(0xFF0D0D12)
    val panel2 = Color(0xFF111117)
    val line = Color(0x1FEDE7DC) // 12% paper
    val paper = Color(0xFFEDE7DC)
    val muted = Color(0xFF8C8578)
    val dim = Color(0xFF5F5A51)
    val gold = Color(0xFFC9A227)
    val gold2 = Color(0xFFE3C36B)
    val cyan = Color(0xFF6FA8A0)
    val red = Color(0xFFB4574E)
}
