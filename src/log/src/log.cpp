#include "oly/log/log.hpp"

#if defined(OLY_HAS_SPDLOG)
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <memory>
#include <vector>

namespace oly::log {
namespace {

spdlog::level::level_enum to_spdlog(const Level l) noexcept {
    switch (l) {
        case Level::Trace: return spdlog::level::trace;
        case Level::Debug: return spdlog::level::debug;
        case Level::Info: return spdlog::level::info;
        case Level::Warn: return spdlog::level::warn;
        case Level::Error: return spdlog::level::err;
        case Level::Critical: return spdlog::level::critical;
        case Level::Off: return spdlog::level::off;
    }
    return spdlog::level::info;
}

}   // namespace

void init(const Level level, const char* file_path) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
    if (file_path != nullptr) {
        // 5MB × 3 个轮转文件（运维留痕；05 §8 归档层另接）
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file_path, 5 * 1024 * 1024, 3));
    }
    auto logger = std::make_shared<spdlog::logger>("oly", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [t:%t] %^%l%$: %v");
    logger->set_level(to_spdlog(level));
    logger->flush_on(spdlog::level::warn);   // warn 起即落盘
    spdlog::set_default_logger(logger);
}

void set_level(const Level level) noexcept {
    if (const auto l = spdlog::default_logger_raw(); l != nullptr) {
        l->set_level(to_spdlog(level));
    }
}

}   // namespace oly::log

#else

namespace oly::log {
void init(Level, const char*) {}
void set_level(Level) noexcept {}
}   // namespace oly::log

#endif
