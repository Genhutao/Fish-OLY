oly_define_module(core)
target_sources(oly_core PRIVATE
    src/ulid.cpp
    src/hash.cpp
    src/bytes.cpp
    src/id.cpp)
