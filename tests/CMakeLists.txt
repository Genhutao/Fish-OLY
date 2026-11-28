add_executable(oly_tests
    support/quiet_debug.cpp
    unit/test_ulid.cpp
    unit/test_hash.cpp
    unit/test_codec.cpp
    unit/test_logdb.cpp
    unit/test_tick_wheel.cpp
    unit/test_lane_queue.cpp
    unit/test_loop_pool.cpp
    unit/test_session_router.cpp
    golden/test_replay_seed.cpp)
target_compile_features(oly_tests PRIVATE cxx_std_20)
target_link_libraries(oly_tests PRIVATE
    Oly::core
    Oly::codec
    Oly::events
    Oly::logdb
    Oly::exec
    Oly::warnings
    Catch2::Catch2WithMain)
add_test(NAME oly.all COMMAND oly_tests)
