package com.oly.world.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

/** 墨纸金暗色主题：跟随 Web 版 styles.css 的变量。始终暗色（引擎世界是夜色基调）。 */
private val OlyColors = darkColorScheme(
    primary = Palette.gold2,
    onPrimary = Palette.ink,
    secondary = Palette.cyan,
    onSecondary = Palette.ink,
    background = Palette.ink,
    onBackground = Palette.paper,
    surface = Palette.panel,
    onSurface = Palette.paper,
    surfaceVariant = Palette.panel2,
    onSurfaceVariant = Palette.muted,
    outline = Palette.line,
    error = Palette.red,
    onError = Palette.paper,
)

private val OlyTypography = Typography()

@Composable
fun OlyTheme(content: @Composable () -> Unit) {
    // 暗色固定；isSystemInDarkTheme 仅保留引用点，避免 lint 提示未使用
    @Suppress("UNUSED_EXPRESSION") isSystemInDarkTheme()
    MaterialTheme(
        colorScheme = OlyColors,
        typography = OlyTypography,
        content = content,
    )
}
