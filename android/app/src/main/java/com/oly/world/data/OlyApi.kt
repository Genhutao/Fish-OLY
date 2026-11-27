package com.oly.world.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.KSerializer
import kotlinx.serialization.builtins.serializer
import kotlinx.serialization.encodeToString
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

/**
 * 引擎 HTTP 客户端：端点与 client/src/api.ts 一一对应。
 * 模拟器里引擎在宿主机 127.0.0.1:8787 → 走 10.0.2.2；真机走局域网 IP。
 */
class OlyApi(private val config: ServerConfig) {

    private val http = OkHttpClient.Builder()
        .connectTimeout(6, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS) // 聊天要等模型，放宽
        .build()

    private val jsonType = "application/json; charset=utf-8".toMediaType()

    private fun base() = config.baseUrl().trimEnd('/')

    private fun enc(s: String) = URLEncoder.encode(s, "UTF-8")

    private class HttpError(val code: Int, val body: String) :
        Exception("$code $body".take(160))

    private suspend fun get(path: String): String = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(base() + path).get().build()
        http.newCall(req).execute().use { r ->
            val body = r.body?.string() ?: ""
            if (!r.isSuccessful) throw HttpError(r.code, body)
            body
        }
    }

    private suspend fun <T> getJson(path: String, ser: KSerializer<T>): T =
        Codec.json.decodeFromString(ser, get(path))

    private suspend fun <T> post(path: String, ser: KSerializer<T>, body: String = "{}"): T =
        withContext(Dispatchers.IO) {
            val payload = body.toRequestBody(jsonType)
            val req = Request.Builder().url(base() + path).post(payload).build()
            http.newCall(req).execute().use { r ->
                val text = r.body?.string() ?: ""
                if (!r.isSuccessful) throw HttpError(r.code, text)
                Codec.json.decodeFromString(ser, text)
            }
        }

    suspend fun users(): UsersResponse = getJson("/api/v1/users", UsersResponse.serializer())

    suspend fun createUser(name: String): CreateUserResponse =
        post("/api/v1/users", CreateUserResponse.serializer(), Codec.json.encodeToString(mapOf("name" to name)))

    suspend fun state(uid: String): View = getJson("/api/v1/users/${enc(uid)}/state", View.serializer())

    suspend fun tick(uid: String): View = post("/api/v1/users/${enc(uid)}/tick", View.serializer())

    suspend fun chat(uid: String, personaId: String, text: String): ChatResponse =
        post(
            "/api/v1/users/${enc(uid)}/chat",
            ChatResponse.serializer(),
            Codec.json.encodeToString(mapOf("personaId" to personaId, "text" to text)),
        )

    suspend fun sceneChat(uid: String, sceneId: String, text: String): ChatResponse =
        post(
            "/api/v1/users/${enc(uid)}/scenes/${enc(sceneId)}/messages",
            ChatResponse.serializer(),
            Codec.json.encodeToString(mapOf("text" to text)),
        )

    suspend fun search(uid: String, personaId: String, q: String, k: Int = 6): SearchResponse =
        getJson(
            "/api/v1/users/${enc(uid)}/memory/search" +
                "?personaId=${enc(personaId)}&q=${enc(q)}&k=$k",
            SearchResponse.serializer(),
        )

    suspend fun forget(uid: String, memberId: String): OpResponse =
        post(
            "/api/v1/users/${enc(uid)}/memory/forget",
            OpResponse.serializer(),
            Codec.json.encodeToString(mapOf("memberId" to memberId)),
        )

    suspend fun revive(uid: String, memberId: String): OpResponse =
        post(
            "/api/v1/users/${enc(uid)}/memory/revive",
            OpResponse.serializer(),
            Codec.json.encodeToString(mapOf("memberId" to memberId)),
        )

    suspend fun consolidate(uid: String): ConsolidateResponse =
        post("/api/v1/users/${enc(uid)}/memory/consolidate", ConsolidateResponse.serializer())
}
