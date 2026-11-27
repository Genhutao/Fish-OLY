package com.oly.world.data

import okhttp3.Call
import okhttp3.OkHttpClient
import okhttp3.Request
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

/**
 * SSE 订阅 /api/v1/users/{uid}/stream：引擎每秒对比签名，有变化才推 `state` 事件。
 * 对齐 Web 版 subscribe()：断线 2 秒重连，另加 4 秒轮询兜底（轮询由 ViewModel 做，
 * 这里只负责事件流；onView 回调已在 IO 线程，调用方自行切主线程）。
 */
class StateStream(
    private val config: ServerConfig,
    private val onView: (View) -> Unit,
) {
    @Volatile
    private var running = false

    @Volatile
    private var call: Call? = null

    private val http = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.SECONDS) // SSE 长连接不超时
        .build()

    fun start(uid: String) {
        if (running) stop()
        running = true
        Thread {
            val url = config.baseUrl().trimEnd('/') +
                "/api/v1/users/" + URLEncoder.encode(uid, "UTF-8") + "/stream"
            while (running) {
                try {
                    val req = Request.Builder()
                        .url(url)
                        .header("Accept", "text/event-stream")
                        .build()
                    val c = http.newCall(req)
                    call = c
                    c.execute().use { resp ->
                        if (!resp.isSuccessful) throw Exception("stream ${resp.code}")
                        val src = resp.body?.source() ?: throw Exception("empty body")
                        while (running) {
                            val line = src.readUtf8Line() ?: break
                            parseLine(line)
                        }
                    }
                } catch (e: Exception) {
                    if (!running) return@Thread
                }
                if (running) {
                    try {
                        Thread.sleep(2000)
                    } catch (_: InterruptedException) {
                        return@Thread
                    }
                }
            }
        }.start()
    }

    @Volatile
    private var pendingData: StringBuilder? = null

    private fun parseLine(line: String) {
        // SSE 帧：event: state / data: {...} / 空行结束。引擎只发 state 一种事件。
        when {
            line.startsWith("event:") -> pendingData = StringBuilder()
            line.startsWith("data:") -> {
                val sb = pendingData ?: StringBuilder().also { pendingData = it }
                if (sb.isNotEmpty()) sb.append('\n')
                sb.append(line.removePrefix("data:").trim())
            }
            line.isBlank() -> {
                val payload = pendingData?.toString()
                pendingData = null
                if (!payload.isNullOrEmpty()) {
                    try {
                        onView(Codec.json.decodeFromString<View>(payload))
                    } catch (_: Exception) {
                        // 坏帧忽略
                    }
                }
            }
        }
    }

    fun stop() {
        running = false
        try {
            call?.cancel()
        } catch (_: Exception) {
        }
        http.dispatcher.executorService.shutdown()
    }
}
