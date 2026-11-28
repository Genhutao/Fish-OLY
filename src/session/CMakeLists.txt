oly_define_module(session DEPS Oly::domain Oly::events Oly::logdb Oly::exec Oly::gateway Oly::memory Oly::quota Oly::config Oly::log Oly::codec)
if(MSVC)
    target_compile_definitions(oly_session PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_sources(oly_session PRIVATE
    src/session_runtime.cpp
    src/shard.cpp
    src/scoring.cpp
    src/prompt_board.cpp
    src/deliberation.cpp
    src/snapshot.cpp
    src/replay.cpp)
