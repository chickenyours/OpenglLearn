#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <algorithm>
#include <chrono>
#include <functional>
#include "Render/Public/rhi_resource_handle.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }

        cv_.notify_one();
    }

    template <typename... Args>
    void emplace(Args&&... args) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace(std::forward<Args>(args)...);
        }

        cv_.notify_one();
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    T wait_pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this] {
            return !queue_.empty();
        });

        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
};



namespace Render{
    class RHICommandReturnSystem{
        public:
            ThreadSafeQueue<std::function<void()>> callbacks;

            // Main-thread pump: concurrent producers must not extend this tick
            // indefinitely. Individual callbacks must themselves be short.
            std::size_t DrainCallbacks(std::size_t maxCount = 64) {
                const auto count = std::min(maxCount, callbacks.size());
                const auto start = std::chrono::steady_clock::now();
                std::size_t processed = 0;
                while(processed < count) {
                    if(processed > 0 && std::chrono::steady_clock::now() - start >=
                        std::chrono::milliseconds(1)) break;
                    auto callback = callbacks.try_pop();
                    if(!callback) break;
                    if(*callback) (*callback)();
                    ++processed;
                }
                return processed;
            }
    };
}
