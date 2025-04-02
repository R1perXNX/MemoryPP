#pragma once

#include <queue>
#include <map>
#include <mutex>
#include <functional>
#include <optional>
#include "scan_result/scan_result.hpp"
#include "custom_map.hpp"


class scan_engine {
protected:
    long _pid{ -1 };
    char _current_scan{ 0 };

    std::queue<std::shared_ptr<memory_region>> get_regions(std::pair<void*, void*> range, DWORD protection_flags);
    bool read_memory(std::shared_ptr<memory_region> region);

public:
    scan_engine(long process_id) : _pid(process_id) {}
    virtual ~scan_engine() = default;

    __forceinline long get_pid() const { return _pid; }
    __forceinline void set_pid(long pid) { _pid = pid; }
};

template<typename DataType>
class scan_engine_templated : public scan_engine
{
    std::shared_ptr<custom_map<scan_result<DataType>>> _prev_scan_results;
private:

    std::function<bool(DataType, DataType, std::optional<DataType>)> compare(scan_type type);

    std::shared_ptr<custom_map<scan_result<DataType>>> first_scan(std::queue<std::shared_ptr<memory_region>>& regions, scan_type type, std::atomic<size_t>& total_entries, const DataType& value1, std::optional<DataType> value2 = std::nullopt);

    std::shared_ptr<custom_map<scan_result<DataType>>> next_scan(std::queue<std::shared_ptr<memory_region>>& regions, scan_type type,
        std::shared_ptr<custom_map<scan_result<DataType>>> prev_scan, std::atomic<size_t>& total_entries, const DataType& value1, std::optional<DataType> value2);

public:
    scan_engine_templated(long process_id) : scan_engine(process_id) {}
    virtual ~scan_engine_templated() override = default;

    size_t scan(const std::pair<void*, void*>& range, scan_type type, const DataType& value1, std::optional<DataType> value2 = std::nullopt);

    __forceinline std::shared_ptr<custom_map<scan_result<DataType>>> get_results() { return _prev_scan_results; }
};


template<typename DataType>
inline std::function<bool(DataType, DataType, std::optional<DataType>)> scan_engine_templated<DataType>::compare(scan_type type) {
    std::function<bool(DataType, DataType, std::optional<DataType>)> cmp = nullptr;

    switch (type) {
    case scan_type::exact_value: {
        cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
            return a == b;
            };
        break;
    }
    case scan_type::bigger_than: {
        if constexpr (std::is_same_v<DataType, float>) {
            cmp = [](float a, float b, std::optional<DataType> /*unused*/) {
                return a > b + 0.0001f;
                };
        }
        else if constexpr (std::is_same_v<DataType, double>) {
            cmp = [](double a, double b, std::optional<DataType> /*unused*/) {
                return a > b + 0.0000001;
                };
        }
        else {
            cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
                return a > b;
                };
        }
        break;
    }
    case scan_type::smaller_than: {
        if constexpr (std::is_same_v<DataType, float>) {
            cmp = [](float a, float b, std::optional<DataType> /*unused*/) {
                return a < b - 0.0001f;
                };
        }
        else if constexpr (std::is_same_v<DataType, double>) {
            cmp = [](double a, double b, std::optional<DataType> /*unused*/) {
                return a < b - 0.0000001;
                };
        }
        else {
            cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
                return a < b;
                };
        }
        break;
    }
    case scan_type::changed: {
        cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
            return a != b;
            };
        break;
    }
    case scan_type::unchanged: {
        cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
            return a == b;
            };
        break;
    }
    case scan_type::increased_by: {
        cmp = [](DataType a, DataType b, std::optional<DataType> c) {
            if (!c.has_value())
                return false;
            return a - b == *c;
            };
        break;
    }
    case scan_type::decreased_by: {
        cmp = [](DataType a, DataType b, std::optional<DataType> c) {
            if (!c.has_value())
                return false;
            return b - a == *c;
            };
        break;
    }
    case scan_type::value_between: {
        cmp = [](DataType a, DataType b, std::optional<DataType> c) {
            if (!c.has_value())
                return false;
            return a > b && a < *c;
            };
        break;
    }
    case scan_type::increased_value: {
        cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
            return a > b;
            };
        break;
    }
    case scan_type::decreased_value: {
        cmp = [](DataType a, DataType b, std::optional<DataType> /*unused*/) {
            return a < b;
            };
        break;
    }

    default: {
        break;
    }
    }

    return cmp;
}


template<typename DataType>
inline std::shared_ptr<custom_map<scan_result<DataType>>> scan_engine_templated<DataType>::first_scan(std::queue<std::shared_ptr<memory_region>>& regions, scan_type type, std::atomic<size_t>& total_entries, const DataType& value1, std::optional<DataType> value2)
{

    std::shared_ptr<custom_map<scan_result<DataType>>> results = std::make_shared<custom_map<scan_result<DataType>>>();
    int32_t i = 0;

    while (!regions.empty()) {

        auto current_region = regions.front();

        regions.pop();

        auto result = std::make_shared<scan_result<DataType>>(current_region, i);
        auto success = false;

        std::function<bool(DataType, DataType, std::optional<DataType>)> cmp = compare(type);

        //MULTITHREAD THIS
        {
            size_t bytes_read = 0;

            success = read_memory(current_region);

            if (success) {

                success = false;

                if (type == scan_type::unknown_value) {
                    success = current_region->dump(true);
                }

                if (cmp) {
                    success = result->search_value(cmp, value1, value2);

                    if (success)
                        total_entries += result->elements().size();
                }
                if (success) {
                    result->set_type(type);
                    results->insert(i, result);
                }
            }

        }
        i++;
    }



    return std::move(results);
}


template<typename DataType>
std::shared_ptr<custom_map<scan_result<DataType>>> scan_engine_templated<DataType>::next_scan(std::queue<std::shared_ptr<memory_region>>& regions, scan_type type,
    std::shared_ptr<custom_map<scan_result<DataType>>> prev_scan, std::atomic<size_t>& total_entries, const DataType& value1, std::optional<DataType> value2)
{
    std::shared_ptr<custom_map<scan_result<DataType>>> results = std::make_shared<custom_map<scan_result<DataType>>>();
    std::array<deferred_processor, 8> processors;
    int i = 0;
    auto keys = prev_scan->keys();

    for (auto key : keys) {
        if (regions.empty())
            break;

        auto current_region = regions.front();
        regions.pop();

        // Advance prev_scan until we find a region that could overlap with current_region
        if (prev_scan->at(key)->region_base() + prev_scan->at(key)->region_size() < current_region->base())
        {
            prev_scan->erase(key); // Remove regions that are completely before current_region
            continue;
        }

        // Check if the remaining region in prev_scan overlaps with current_region
        if (prev_scan->at(key)->region_base() < current_region->base() + current_region->size()) {
            auto old_scan = prev_scan->at(key);

            prev_scan->erase(key);

            if (!old_scan)
                continue;

            processors[i % 8].add_operation([this, old_scan, current_region, &results, &total_entries, type, value1, value2] {

                size_t bytes_read = 0;

                auto success = read_memory(current_region);

                if (!success)
                    return;

                auto old_scan_type = old_scan->type();

                std::span<scan_entry<DataType>> elements = std::span<scan_entry<DataType>>();

                auto prev_region = old_scan->associated_region();

                size_t total_elements{ 0 };

                if (!prev_region)
                    return;

                if (old_scan_type == scan_type::unknown_value) {
                    //we can't access the elements since we didnt create the elements in the first scan
                    total_elements = prev_region->size() / sizeof(DataType);
                }
                else {
                    elements = old_scan->elements();
                    total_elements = elements.size();
                }


                auto result = std::make_shared<scan_result<DataType>>(current_region, old_scan->index());

                for (size_t i = 0; i < total_elements; i++) {

                    success = false;

                    DataType* old_value = nullptr;

                    if (old_scan_type == scan_type::unknown_value) {
                        old_value = prev_region->at_index<DataType>(i);

                        if (!old_value)
                            continue;
                    }

                    scan_entry<DataType> old_elem = (old_scan_type == scan_type::unknown_value) ? scan_entry<DataType>{*old_value, prev_region->base() + i * sizeof(DataType)} : elements[i];

                    auto new_value = current_region->at_address<DataType>(old_elem.address);

                    if (!new_value)
                        continue;

                    std::function<bool(DataType, DataType, std::optional<DataType>)> cmp = compare(type);

                    if (!cmp)
                        continue;
                    //we must filter for allowed scan types

                    switch (type) {
                    case scan_type::exact_value: {
                        if (cmp(*new_value, value1, value2)) {

                            success = true;
                        }
                        break;
                    }
                    case scan_type::increased_value:
                    case scan_type::decreased_value:
                    case scan_type::changed:
                    case scan_type::unchanged:
                    case scan_type::decreased_by:
                    case scan_type::increased_by: {
                        if (cmp(*new_value, old_elem.value, value1)) {

                            success = true;
                        }
                        break;
                    }
                    case scan_type::value_between: {
                    case scan_type::smaller_than:
                    case scan_type::bigger_than: {
                        if (cmp(*new_value, value1, value2)) {

                            success = true;
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    }

                    if (success) {
                        total_entries++;
                        result->add_element({ *new_value ,old_elem.address });
                        result->set_type(type);
                    }
                }

                if (result->elements().size() > 0) {
                    results->insert(old_scan->index(), result);
                }
                   
                });
           

        }
    }

    return results;
}

    



template<typename DataType>
inline size_t scan_engine_templated<DataType>::scan(const std::pair<void*, void*>& range, scan_type type, const DataType& value1, std::optional<DataType> value2)
{
    auto regions = get_regions(range, PAGE_READWRITE | PAGE_WRITECOPY);

    std::atomic<size_t> total_entries = 0;
    if (_current_scan == 0) {
        _prev_scan_results = first_scan(regions, type, total_entries, value1, value2);
        _current_scan = 1;
    }
    else {
        _prev_scan_results = next_scan(regions, type, _prev_scan_results, total_entries, value1, value2);
    }

    return total_entries;
}
