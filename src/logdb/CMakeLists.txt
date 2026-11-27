oly_define_module(logdb DEPS Oly::events Oly::core)
target_sources(oly_logdb PRIVATE
    src/store.cpp)
if(MSVC)
    # 有意使用 C stdio（FILE* + _commit 落盘纪律，05 §3）；CRT 安全性告警不适用
    target_compile_definitions(oly_logdb PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
