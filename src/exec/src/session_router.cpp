#include "oly/exec/session_router.hpp"

#include <shared_mutex>
#include <utility>

namespace oly::exec {

bool SessionRouter::bind(const uint64_t session_key, const std::size_t loop_index) {
    std::unique_lock lock(mu_);
    return table_.emplace(session_key, loop_index).second;
}

std::optional<std::size_t> SessionRouter::loop_of(const uint64_t session_key) const {
    std::shared_lock lock(mu_);
    if (const auto it = table_.find(session_key); it != table_.end()) return it->second;
    return std::nullopt;
}

std::optional<std::size_t> SessionRouter::rebind(const uint64_t session_key,
                                                 const std::size_t new_loop) {
    std::unique_lock lock(mu_);
    if (const auto it = table_.find(session_key); it != table_.end()) {
        const auto old = it->second;
        it->second = new_loop;
        return old;
    }
    return std::nullopt;
}

std::optional<std::size_t> SessionRouter::unbind(const uint64_t session_key) {
    std::unique_lock lock(mu_);
    if (const auto it = table_.find(session_key); it != table_.end()) {
        const auto old = it->second;
        table_.erase(it);
        return old;
    }
    return std::nullopt;
}

std::size_t SessionRouter::size() const {
    std::shared_lock lock(mu_);
    return table_.size();
}

}   // namespace oly::exec
