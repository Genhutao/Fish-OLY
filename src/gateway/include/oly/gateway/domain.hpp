// OLY · gateway —— 网关独立分片域（07 §1）：全部模型流量唯一入口的托管壳。
// M0 = 域拆分就位：独立 loop 池 + Gateway 实例生命周期；调用通路仍是同步
// submit/pump（决策驱动 M0 形态）。M1 泵协程化：co_await submit/reply、
// 流式聚合、预留-结算、饱和度广播。
#pragma once

#include "oly/exec/loop_pool.hpp"
#include "oly/gateway/gateway.hpp"

#include <memory>
#include <stddef.h>

namespace oly::gateway {

class Domain {
public:
    struct Deps {
        std::size_t loop_count = 1;   // 07 §8：分片数按目标世界数线性扩展
    };

    static Result<Domain> create(LLMProvider& provider, const Gateway::Caps caps,
                                 const Deps deps = {}) {
        auto pool = exec::LoopPool::create(deps.loop_count);
        if (!pool) return unexpected(pool.error());
        return Domain(std::move(pool.value()), std::make_unique<Gateway>(provider, caps));
    }

    Gateway& gateway() noexcept { return *gateway_; }
    exec::LoopPool& pool() noexcept { return pool_; }

    void start() { pool_.start(); }
    void stop() noexcept { pool_.stop(); }

private:
    Domain(exec::LoopPool&& pool, std::unique_ptr<Gateway> gateway)
        : pool_(std::move(pool)), gateway_(std::move(gateway)) {}

    exec::LoopPool pool_;
    std::unique_ptr<Gateway> gateway_;
};

}   // namespace oly::gateway
