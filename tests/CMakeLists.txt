add_executable(oly_tests
    support/quiet_debug.cpp
    unit/test_ulid.cpp
    unit/test_hash.cpp
    unit/test_codec.cpp
    unit/test_logdb.cpp
    unit/test_permissions.cpp
    unit/test_tick_wheel.cpp
    unit/test_lane_queue.cpp
    unit/test_loop_pool.cpp
    unit/test_session_router.cpp
    unit/test_scoring.cpp
    unit/test_prompt_board.cpp
    unit/test_gateway.cpp
    unit/test_identity_shard.cpp
    unit/test_memory_store.cpp
    unit/test_quota.cpp
    golden/test_replay_seed.cpp
    golden/test_session_e2e.cpp
    golden/test_snapshot_replay.cpp
    golden/test_deliberation_e2e.cpp
    golden/test_memory_pipeline.cpp
    golden/test_quota_loop.cpp
    golden/test_host_smoke.cpp)
target_compile_features(oly_tests PRIVATE cxx_std_20)
target_link_libraries(oly_tests PRIVATE
    Oly::core
    Oly::codec
    Oly::events
    Oly::logdb
    Oly::exec
    Oly::domain
    Oly::identity
    Oly::gateway
    Oly::memory
    Oly::quota
    Oly::session
    Oly::warnings
    Catch2::Catch2WithMain)
add_test(NAME oly.all COMMAND oly_tests)

# api e2e（api 模块就绪时纳入；Boost::json 门控）
if(TARGET Oly::api)
    target_sources(oly_tests PRIVATE golden/test_api_e2e.cpp)
    target_link_libraries(oly_tests PRIVATE Oly::api)
    if(MSVC)
        target_compile_definitions(oly_tests PRIVATE _WIN32_WINNT=0x0601)
    endif()
endif()
