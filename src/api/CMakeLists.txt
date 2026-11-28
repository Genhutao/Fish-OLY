# api 模块：REST + WS（10）。依赖 Boost.Beast（头文件库）与 Boost.JSON（编译库）；
# Boost::json 就绪时启用，否则跳过（主构建不受影响）。
find_package(Boost CONFIG QUIET COMPONENTS json)
if(NOT TARGET Boost::json)
    message(STATUS "Boost::json not found - api module disabled (vcpkg provides boost-json)")
    return()
endif()

oly_define_module(api DEPS Oly::core Oly::exec Oly::domain Oly::identity Oly::events Oly::logdb Oly::session Oly::log Boost::json)
target_sources(oly_api PRIVATE
    src/server.cpp)
if(MSVC)
    target_compile_definitions(oly_api PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0601)
endif()
