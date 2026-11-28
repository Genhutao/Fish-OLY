// OLY · log —— 日志门面（进程观测面，01 §1 观测/05 §8 归档的运维侧雏形）。
// spdlog 落地后统一格式：时间戳 + 线程 id + 着色级别；模块/会话维度的结构化
// 字段随 M1 进程流事件（11/05）接入。无 spdlog 时宏退化为空操作（裸构建仍可编译）。
#pragma once

#if defined(OLY_HAS_SPDLOG)
#include <spdlog/spdlog.h>

#define OLY_LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define OLY_LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define OLY_LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define OLY_LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define OLY_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define OLY_LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)
#else

#define OLY_LOG_TRACE(...) do {} while (0)
#define OLY_LOG_DEBUG(...) do {} while (0)
#define OLY_LOG_INFO(...) do {} while (0)
#define OLY_LOG_WARN(...) do {} while (0)
#define OLY_LOG_ERROR(...) do {} while (0)
#define OLY_LOG_CRITICAL(...) do {} while (0)

#endif

namespace oly::log {

enum class Level : int { Trace = 0, Debug, Info, Warn, Error, Critical, Off };

// 全局初始化：stderr 控制台（着色）+ 可选轮转文件；level 之下全部静默。
// 幂等（重复调用重设级别）。进程退出无需手工 flush（注册 atexit/sink 自理）。
void init(Level level = Level::Info, const char* file_path = nullptr);
void set_level(Level level) noexcept;

}   // namespace oly::log
