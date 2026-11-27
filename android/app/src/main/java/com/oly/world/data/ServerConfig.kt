package com.oly.world.data

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** 服务器地址配置：SharedPreferences 持久化，默认模拟器宿主映射地址。 */
class ServerConfig(context: Context) {
    private val prefs = context.getSharedPreferences("oly_server", Context.MODE_PRIVATE)

    companion object {
        const val DEFAULT_BASE = "http://10.0.2.2:8787"
        private const val KEY_BASE = "base_url"
    }

    private val _baseUrl = MutableStateFlow(prefs.getString(KEY_BASE, DEFAULT_BASE) ?: DEFAULT_BASE)
    val baseUrlFlow: StateFlow<String> = _baseUrl

    fun baseUrl(): String = _baseUrl.value

    fun setBaseUrl(url: String) {
        val v = url.trim().ifEmpty { DEFAULT_BASE }
        prefs.edit().putString(KEY_BASE, v).apply()
        _baseUrl.value = v
    }
}
