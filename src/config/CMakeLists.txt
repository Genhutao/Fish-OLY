# config 模块：配置面（08）——分层合并 / 包络 / 三级自调 / 规范编码。
oly_define_module(config DEPS Oly::core Oly::codec)
target_sources(oly_config PRIVATE
    src/config.cpp)
