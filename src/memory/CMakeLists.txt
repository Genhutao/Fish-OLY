# memory 模块：长期记忆管线（02 §5、03 §6）——候选 × 覆盖 × 召回 × 重建。
oly_define_module(memory DEPS Oly::core Oly::codec)
target_sources(oly_memory PRIVATE
    src/memory.cpp
    src/summarizer.cpp)
