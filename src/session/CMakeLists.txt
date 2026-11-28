oly_define_module(session DEPS Oly::domain Oly::events Oly::logdb Oly::exec Oly::codec)
target_sources(oly_session PRIVATE
    src/session_runtime.cpp)
