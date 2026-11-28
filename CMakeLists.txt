cmake_minimum_required(VERSION 3.25)
project(oly-engine VERSION 0.1.0 LANGUAGES CXX)

option(OLY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(OLY_BUILD_TESTS "Build tests (requires Catch2 via vcpkg)" ON)

# ── 公共编译纪律（01 §8）───────────────────────────────────────────────
add_library(oly_warnings INTERFACE)
add_library(Oly::warnings ALIAS oly_warnings)
if(MSVC)
    target_compile_options(oly_warnings INTERFACE /W4 /permissive- /EHsc /utf-8)
    if(OLY_WARNINGS_AS_ERRORS)
        target_compile_options(oly_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(oly_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(OLY_WARNINGS_AS_ERRORS)
        target_compile_options(oly_warnings INTERFACE -Werror)
    endif()
endif()

# 模块定义：include 公开、src 私有，物理隔离依赖方向（README 仓库布局）
function(oly_define_module NAME)
    cmake_parse_arguments(ARG "" "" "DEPS" ${ARGN})
    add_library(oly_${NAME} STATIC)
    add_library(Oly::${NAME} ALIAS oly_${NAME})
    target_include_directories(oly_${NAME}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(oly_${NAME} PUBLIC cxx_std_20)
    target_link_libraries(oly_${NAME} PUBLIC oly_warnings)
    foreach(dep IN LISTS ARG_DEPS)
        target_link_libraries(oly_${NAME} PUBLIC ${dep})
    endforeach()
endfunction()

add_subdirectory(src/log)
add_subdirectory(src/core)
add_subdirectory(src/codec)
add_subdirectory(src/events)
add_subdirectory(src/logdb)
add_subdirectory(src/quota)
add_subdirectory(src/gateway)
add_subdirectory(src/app)

# ── exec：执行内核（boost-asio；依赖就绪时启用）─────────────────────────
find_package(Boost CONFIG QUIET)
if(Boost_FOUND)
    add_subdirectory(src/exec)
else()
    message(STATUS "Boost not found - exec module disabled (vcpkg provides boost-asio)")
endif()

add_subdirectory(src/domain)
add_subdirectory(src/identity)
add_subdirectory(src/memory)
add_subdirectory(src/session)
add_subdirectory(src/api)

# ── 测试（Catch2 就绪时启用；未就绪仅禁用测试，主构建不受影响）──────────
if(OLY_BUILD_TESTS)
    find_package(Catch2 3 CONFIG QUIET)
    if(TARGET Catch2::Catch2WithMain)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "Catch2 not found - tests disabled (provided by vcpkg manifest)")
    endif()
endif()
