oly_define_module(gateway DEPS Oly::core Oly::exec)
target_sources(oly_gateway PRIVATE
    src/gateway.cpp)
