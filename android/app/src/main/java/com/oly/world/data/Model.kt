package com.oly.world.data

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject

/** 与 client/src/api.ts 的接口一一对应；字段尽量给默认值，解码宽容。 */
@Serializable
data class Persona(
    val id: String,
    val name: String,
    val avatar: String = "",
    val anchor: String = "",
    val personality: String = "",
    val mood: String = "",
    val energy: Double = 0.0,
    val affection: Double = 0.0,
    val bond: Map<String, Double>? = null,
    val scene: String = "",
    val activity: String = "",
    val lastSpoke: Long = 0,
    val sceneName: String? = null,
    val intentNow: String? = null,
    val plan: Plan? = null,
)

@Serializable
data class Intent(
    val id: String,
    val sceneId: String,
    val start: Int = 0,
    val end: Int = 0,
    val kind: String = "solo",
    val target: String? = null,
    val status: String = "",
)

@Serializable
data class Plan(
    val personaId: String = "",
    val day: Int = 0,
    val seed: Long = 0,
    val intents: List<Intent> = emptyList(),
)

@Serializable
data class Message(
    val id: String,
    val room: String = "",
    val actor: String = "",
    val name: String = "",
    val text: String = "",
    val at: String = "",
    val mock: Boolean = false,
    val session: String? = null,
    val score: Double? = null,
)

@Serializable
data class Scene(
    val id: String,
    val name: String,
    val topic: String = "",
    val capacity: Int = 0,
    val visibility: String = "",
    val messages: List<Message>? = null,
    val here: List<String>? = null,
)

@Serializable
data class SessPart(val personaId: String, val role: String = "")

@Serializable
data class Session(
    val id: String,
    val day: Int = 0,
    val sceneId: String = "",
    val sceneName: String? = null,
    val kind: String = "solo",
    val start: Int = 0,
    val end: Int = 0,
    val parts: List<SessPart> = emptyList(),
    val partNames: List<String>? = null,
    val status: String = "planned",
    val said: Int = 0,
    val degraded: Boolean? = null,
    val backfilled: Boolean? = null,
    val summary: String? = null,
)

@Serializable
data class Member(
    val id: String,
    val text: String,
    val day: Int = 0,
    val imp: Double = 0.0,
    val w: Double = 0.0,
    val refs: List<String>? = null,
    val state: String = "active",
)

@Serializable
data class Slot(
    val id: String,
    val layer: String,
    val text: String = "",
    val members: List<Member> = emptyList(),
    val hits: Int = 0,
    val day: Int = 0,
    val lastDay: Int = 0,
    val imp0: Double = 0.0,
    val tags: List<String>? = null,
    val vec: String = "",
)

@Serializable
data class StatsView(
    val slots: Int = 0,
    val members: Int = 0,
    val active: Int = 0,
    val forgotten: Int = 0,
    val writes: Int = 0,
    val dups: Int = 0,
    val merges: Int = 0,
    val appends: Int = 0,
    val slotsList: List<Slot>? = null,
)

@Serializable
data class WorldEvent(
    val seq: Long,
    val at: String = "",
    val type: String = "",
    val room: String? = null,
    val actor: String? = null,
    val text: String? = null,
    val data: JsonObject? = null,
)

@Serializable
data class Budget(
    val used: Double = 0.0,
    val limit: Double = 0.0,
    val left: Double = 0.0,
    val over: Boolean = false,
)

@Serializable
data class View(
    val id: String,
    val tick: Long = 0,
    val day: Int = 0,
    val tickOfDay: Int = 0,
    val clock: String = "",
    val model: String = "",
    val events: Long = 0,
    val budget: Budget = Budget(),
    val dormant: Boolean = false,
    val personas: List<Persona> = emptyList(),
    val scenes: List<Scene> = emptyList(),
    val dms: Map<String, List<Message>> = emptyMap(),
    val sessions: List<Session> = emptyList(),
    val memory: Map<String, StatsView> = emptyMap(),
    val messageCount: Int = 0,
    val log: List<WorldEvent> = emptyList(),
)

@Serializable
data class Hit(
    val slotId: String,
    val layer: String = "",
    val text: String = "",
    val score: Double = 0.0,
    val tags: List<String>? = null,
    val refs: List<String>? = null,
)

@Serializable
data class ChatResult(
    val reply: String = "",
    val mock: Boolean = false,
    val hits: List<Hit>? = null,
    val memBlock: String = "",
    val remember: String = "",
    val error: String? = null,
)

@Serializable
data class ChatResponse(val result: ChatResult, val view: View? = null)

@Serializable
data class UsersInfo(
    val id: String,
    val tick: Long = 0,
    val day: Int = 0,
    val model: String = "",
)

@Serializable
data class UsersResponse(
    val users: List<UsersInfo> = emptyList(),
    val default: String = "default",
)

@Serializable
data class CreateUserResponse(val ok: Boolean = false, val id: String = "")

@Serializable
data class OpResponse(val ok: Boolean = false, val view: View? = null)

@Serializable
data class SearchResponse(
    val query: String = "",
    val personaId: String = "",
    val hits: List<Hit> = emptyList(),
)

@Serializable
data class ConsolidateReport(
    val merged: Int = 0,
    val rebalanced: Int = 0,
    val forgotten: Int = 0,
    val slotsAfter: Int = 0,
)

@Serializable
data class ConsolidateResponse(val report: ConsolidateReport, val view: View? = null)

/** 全局共享的 JSON 配置：宽容未知字段、null 兜底。 */
object Codec {
    val json = Json {
        ignoreUnknownKeys = true
        coerceInputValues = true
        explicitNulls = false
        isLenient = true
    }
}
