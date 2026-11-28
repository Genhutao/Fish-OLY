oly_define_module(identity DEPS Oly::domain Oly::exec)
target_sources(oly_identity PRIVATE
    src/shard.cpp)
