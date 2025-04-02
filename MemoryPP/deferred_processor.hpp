#pragma once
#include <iostream>
#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <tuple>

class deferred_processor {
    using priority_operation = std::pair<int, std::function<void()>>;

    struct compare_priority {
        bool operator()(const priority_operation& lhs, const priority_operation& rhs) const {
            return lhs.first < rhs.first; 
        }
    };

    std::priority_queue<priority_operation, std::vector<priority_operation>, compare_priority> _operations;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::thread _worker_thread;
    std::atomic<bool> _done;

private:
    __inline void process_operations() {
        while (true) {
            std::function<void()> operation;
            {
                std::unique_lock<std::mutex> lock(_mutex);

                
                _cv.wait(lock, [this]() { return !_operations.empty() || _done; });

                if (_done && _operations.empty()) {
                    break;
                }

                
                operation = std::move(_operations.top().second);
                _operations.pop();
            }

            operation();
        }
    }

public:
    __inline deferred_processor() : _done(false) {
        _worker_thread = std::thread(&deferred_processor::process_operations, this);
    }

    __inline ~deferred_processor() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _done = true;
        }
        _cv.notify_all();
        if (_worker_thread.joinable()) {
            _worker_thread.join();
        }
    }

    __forceinline void add_operation(const std::function<void()>& operation, int priority = 0) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _operations.emplace(priority, operation);
        }
        _cv.notify_one();
    }
};
