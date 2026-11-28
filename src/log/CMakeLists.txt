# log 模块：spdlog 门面；spdlog 缺席时编译空实现（裸构建不受影响）
find_package(spdlog CONFIG QUIET)
if(spdlog_FOUND)
    message(STATUS "spdlog found - logging enabled")
else()
    message(STATUS "spdlog not found - logging compiled as no-op")
endif()

oly_define_module(log)
if(spdlog_FOUND)
    target_link_libraries(oly_log PUBLIC spdlog::spdlog)
    target_compile_definitions(oly_log PUBLIC OLY_HAS_SPDLOG)
endif()
target_sources(oly_log PRIVATE
    src/log.cpp)
