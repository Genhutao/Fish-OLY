oly_define_module(events DEPS Oly::codec Oly::core)
target_sources(oly_events PRIVATE
    src/envelope.cpp)
