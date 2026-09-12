#pragma once
#include <chrono>
#include <future>
#include <mutex>

namespace scyllagpt {
// A single background refresh. Polling never waits; invalidation rejects stale results.
template<class T> class AsyncSnapshot {
public:
    explicit AsyncSnapshot(std::chrono::milliseconds interval) : interval_(interval) {}
    template<class Loader> T get(Loader loader, bool force = false) {
        std::lock_guard lock(mutex_);
        if (pending_.valid()) {
            if (!force && pending_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return value_;
            const bool current = pending_generation_ == generation_;
            try { auto result = pending_.get(); if (current) value_ = std::move(result); }
            catch (...) { /* retain the last known state on probe failure */ }
            if (current) {
                next_ = std::chrono::steady_clock::now() + interval_;
                return value_;
            }
        }
        if (force || std::chrono::steady_clock::now() >= next_) {
            pending_generation_ = generation_;
            pending_ = std::async(std::launch::async, [loader, previous = value_] { return loader(previous); });
            if (force) {
                try { value_ = pending_.get(); } catch (...) {}
                next_ = std::chrono::steady_clock::now() + interval_;
            }
        }
        return value_;
    }
    void invalidate(bool clear = false) {
        std::lock_guard lock(mutex_);
        ++generation_;
        next_ = {};
        if (clear) value_ = {};
    }
private:
    std::mutex mutex_;
    T value_{};
    std::future<T> pending_;
    unsigned generation_ = 0, pending_generation_ = 0;
    std::chrono::steady_clock::time_point next_{};
    std::chrono::milliseconds interval_;
};
}
