# quota 模块：配额与限流（09）——两层桶 × 键控册 × 预留-结算-释放。
oly_define_module(quota DEPS Oly::core)
target_sources(oly_quota PRIVATE
    src/quota.cpp)
