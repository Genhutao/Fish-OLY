// OLY · app —— 引擎入口骨架：sanity 自检（版本与 ULID 工厂自检）。
// 执行内核接入后此处变为 loop 池装配与信号处理（01 §1）。
#include <oly/core/ulid.hpp>

#include <cstdio>

#ifndef OLY_VERSION_STRING
#define OLY_VERSION_STRING "0.0.0-dev"
#endif

int main() {
    oly::UlidFactory factory;
    std::printf("oly-engine %s\n", OLY_VERSION_STRING);
    std::printf("boot ulid: %s\n", factory.next().str().c_str());
    return 0;
}
