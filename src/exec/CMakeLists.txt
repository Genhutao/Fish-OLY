oly_define_module(exec DEPS Oly::core Boost::headers)
target_sources(oly_exec PRIVATE
    src/loop_pool.cpp
    src/tick_wheel.cpp
    src/session_router.cpp)
