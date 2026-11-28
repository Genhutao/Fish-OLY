oly_define_module(domain DEPS Oly::core Oly::codec)
target_sources(oly_domain PRIVATE
    src/domain.cpp
    src/permissions.cpp)
