#pragma once

#include "task_queue.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

class WorkerPool {
public:
    WorkerPool(size_t workerCount, size_t queueCapacity)
        : queue_(std::make_shared<TaskQueue>(queueCapacity)) {
        workers_.reserve(workerCount);
        for (size_t i = 0; i < workerCount; ++i) {
            workers_.emplace_back([queue = queue_] {
                std::function<void()> task;
                while (queue->Pop(task)) {
                    task();
                }
            });
        }
    }

    ~WorkerPool() {
        Stop();
    }

    bool Enqueue(std::function<void()> task) {
        return queue_->Push(std::move(task));
    }

    void Stop() {
        if (!queue_) return;
        queue_->Stop();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
        queue_.reset();
    }

    size_t QueueDepth() const {
        return queue_ ? queue_->Size() : 0;
    }

    size_t WorkerCount() const {
        return workers_.size();
    }

private:
    std::shared_ptr<TaskQueue> queue_;
    std::vector<std::thread> workers_;
};
