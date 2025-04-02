#pragma once
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <optional>
#include <array>

#include "../file_dump/file_dump.hpp"
#include "../memory_reagion/memory_region.hpp"
#include "../custom_map.hpp"

#include "../deferred_processor.hpp"

extern file_dump results;

enum scan_type {
    unknown_value,
    increased_value,
    decreased_value,
    exact_value,
    increased_by,
    decreased_by,
    smaller_than,
    bigger_than,
    changed,
    unchanged,
    value_between
};

template<typename DataType>
struct scan_entry {
    DataType value;
    uint64_t address;
};


struct result_header {
    uint64_t size;
};


template <typename DataType>
class scan_result : public dumpable<result_header, scan_entry<DataType>>
{
    std::shared_ptr<memory_region> _associated_region;  // Now using shared_ptr
    scan_type _type;
    size_t _index;

public:
    // Constructor now accepts a shared_ptr to memory_region.
    scan_result(std::shared_ptr<memory_region> region, size_t index = 0)
        : dumpable<result_header, scan_entry<DataType>>(results), _associated_region(std::move(region)), _index(index)
    {
    }

    // Delete copy constructor and copy assignment operator.
    scan_result(const scan_result& other) = delete;
    scan_result& operator=(const scan_result& other) = delete;

    // Move constructor.
    scan_result(scan_result&& other) noexcept
        : dumpable<result_header, scan_entry<DataType>>(std::move(other)),
        _associated_region(std::move(other._associated_region)),  // Shared pointer move is fine
        _index(std::exchange(other._index, -1))
    {
    }

    // Move assignment operator.
    scan_result& operator=(scan_result&& other) noexcept
    {
        if (this != &other)
        {
            dumpable<result_header, scan_entry<DataType>>::operator=(std::move(other));
            _associated_region = std::move(other._associated_region);  // Shared pointer move is fine
            _index = std::exchange(other._index, -1);
        }
        return *this;
    }
    

    __forceinline size_t index() { return _index; }
    __forceinline void set_type(scan_type type) { _type = type; }
    __forceinline scan_type type() { return _type; }

    // Function accepts a comparator to decide if a value matches.
    bool search_value(std::function<bool(DataType, DataType, std::optional<DataType>)> comparator, const DataType& value1, std::optional<DataType> value2);

    __forceinline void add_element(const scan_entry<DataType>& entry) {
        this->_data.push_back(entry);
        this->_header.size++;
        this->_valid = true;
    }

    __forceinline uint64_t region_base() { return _associated_region->base(); }

    __forceinline size_t region_size() { return _associated_region->size(); }

    __forceinline std::shared_ptr< memory_region> associated_region() { return _associated_region; }

    std::span<scan_entry<DataType>> elements() {
        if (!this->_valid)
            std::span<scan_entry<DataType>>();

        // Access the in-memory vector if it's not discarded.
        if (!this->_discarded) {
            return this->_data;
        }

        if (!this->_mapped_info && !this->load())
            std::span<scan_entry<DataType>>();

        // Access the mapped span.
        return this->_data_map;
    }
};

template<typename DataType>
inline bool scan_result<DataType>::search_value(std::function<bool(DataType, DataType, std::optional<DataType>)> comparator, const DataType& value1, std::optional<DataType> value2)
{
    this->_data.reserve(20);

    auto total_elements = _associated_region->size() / sizeof(DataType);

    constexpr size_t PARALLEL_THRESHOLD = 10000;

    if (total_elements < PARALLEL_THRESHOLD) {
        for (size_t i = 0; i < total_elements; i++) {
            auto value = _associated_region->at_index<DataType>(i);
            if (value && comparator(*value, value1, value2)) {
                auto address = _associated_region->base() + i * sizeof(DataType);
                scan_entry<DataType> entry{ *value, address };
                this->_data.push_back(entry);
                this->_header.size++;
                this->_valid = true;
            }
        }
        return this->_valid;
    }


    std::array<deferred_processor, 4> _processors;

    size_t jobs = _processors.size();  // Supponiamo jobs == 4.
    size_t elements_per_processor = total_elements / jobs;
    std::vector<std::vector<scan_entry<DataType>>> local_results(jobs);

    std::atomic<size_t> pending_count(jobs);

    for (size_t j = 0; j < jobs; ++j) {
        size_t start_index = j * elements_per_processor;
        size_t end_index = (j == jobs - 1) ? total_elements : start_index + elements_per_processor;

        _processors[j].add_operation([this, j, start_index, end_index, &local_results, &comparator, &value1, &value2, &pending_count]() {
            // Scansione del chunk assegnato.
            for (size_t i = start_index; i < end_index; i++) {
                auto value = _associated_region->at_index<DataType>(i);
                if (value && comparator(*value, value1, value2)) {
                    auto address = _associated_region->base() + i * sizeof(DataType);
                    local_results[j].push_back({ *value, address });
                }
            }
            pending_count--;
            });
    }

    while (pending_count.load() != 0) {
        std::this_thread::yield();
    }

    this->_data.clear();

    for (size_t j = 0; j < jobs; j++) {
        this->_data.insert(this->_data.end(), local_results[j].begin(), local_results[j].end());
    }
    this->_header.size = this->_data.size();
    this->_valid = !this->_data.empty();

    return this->_valid;
}