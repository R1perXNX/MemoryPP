#pragma once
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <vector>

template<typename T>
class custom_map {
private:
    std::map<int32_t, std::shared_ptr<T>> _map;
    mutable std::mutex _mutex;

public:
    custom_map() = default;

    // Disable copy operations
    custom_map(const custom_map&) = delete;
    custom_map& operator=(const custom_map&) = delete;

    // Move constructor
    custom_map(custom_map&& other) noexcept {
        std::lock_guard<std::mutex> lock(other._mutex);
        _map = std::move(other._map);
    }

    // Move assignment
    custom_map& operator=(custom_map&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(_mutex, other._mutex);
            _map = std::move(other._map);
        }
        return *this;
    }

    // Insert element
    void insert(int32_t key, std::shared_ptr<T> value) {
        std::lock_guard<std::mutex> lock(_mutex);
        _map[key] = std::move(value);
    }

    // Remove element
    bool erase(int32_t key) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _map.erase(key) > 0;
    }

    // Check if key exists
    bool contains(int32_t key) const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _map.find(key) != _map.end();
    }

    // Access element by key
    std::shared_ptr<T> at(int32_t key) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _map.find(key);
        if (it == _map.end())
            return nullptr;
        return it->second;
    }

    // Get the first element (if exists)
    std::shared_ptr<T> first() const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_map.empty())
            return nullptr;
        return _map.begin()->second;
    }

    // Apply function to each element
    template<typename Func>
    void for_each(Func func) const {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& pair : _map) {
            func(pair.first, pair.second);
        }
    }

    // Check if map is empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _map.empty();
    }

    // Get map size
    size_t size() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _map.size();
    }

    // Get a copy of all keys
    std::vector<int32_t> keys() const {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<int32_t> result;
        result.reserve(_map.size());
        for (const auto& pair : _map) {
            result.push_back(pair.first);
        }
        return result;
    }

    // Get a copy of all values
    std::vector<std::shared_ptr<T>> values() const {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<std::shared_ptr<T>> result;
        result.reserve(_map.size());
        for (const auto& pair : _map) {
            result.push_back(pair.second);
        }
        return result;
    }
};