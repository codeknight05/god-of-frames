#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

class TaskQueue {
public:
    explicit TaskQueue(size_t capacity) : capacity_(capacity) {}

    bool Push(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || tasks_.size() >= capacity_) return false;
        tasks_.push_back(std::move(task));
        cv_.notify_one();
        return true;
    }

    bool Pop(std::function<void()>& task) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return stopped_ || !tasks_.empty(); });
        if (tasks_.empty()) return false;
        task = std::move(tasks_.front());
        tasks_.pop_front();
        return true;
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_.notify_all();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

private:
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stopped_ = false;
};
