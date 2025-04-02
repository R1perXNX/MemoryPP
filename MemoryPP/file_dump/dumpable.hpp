#pragma once
#include "file_dump.hpp"
#include <span>
#include <vector>
#include <memory>
#include <optional>

template <typename Header, typename DataType>
class dumpable {
protected:
    Header _header{};
    file_dump& _file;
    std::optional<uint64_t> _file_offset{ 0 };

    std::span<DataType> _data_map; // READ ONLY  
    std::vector<DataType> _data;

    std::unique_ptr<mapped_chunk> _mapped_info;
    bool _valid{ false };
    bool _discarded{ false };

public:
    explicit dumpable(file_dump& file) : _file(file) {}
    virtual ~dumpable() = default;

    dumpable(const dumpable& other) = delete;

    // MOVE CONSTRUCTOR
    dumpable(dumpable&& other) noexcept
        : _header(std::move(other._header)),
        _file(other._file),
        _file_offset(std::exchange(other._file_offset, 0)),
        _data_map(std::move(other._data_map)),
        _data(std::move(other._data)),
        _mapped_info(std::move(other._mapped_info)),
        _valid(std::exchange(other._valid, false)),
        _discarded(std::exchange(other._discarded, false)) {
    }

    
    // MOVE ASSIGNMENT
    dumpable& operator=(dumpable&& other) noexcept {
        if (this != &other) {
            _header = std::move(other._header);
            _file_offset = std::exchange(other._file_offset, 0);
            _data_map = std::move(other._data_map);
            _data = std::move(other._data);
            _mapped_info = std::move(other._mapped_info);
            _valid = std::exchange(other._valid, false);
            _discarded = std::exchange(other._discarded, false);
        }
        return *this;
    }

    __forceinline void copy_map_view() {
        if (!_data_map.empty()) {
            _data.assign(_data_map.begin(), _data_map.end());
        }
    }

    bool load();

    bool dump(bool discard_memory = false);
};

template<typename Header, typename DataType>
inline bool dumpable<Header, DataType>::load() {
    if (!_data_map.empty())
        return true;

    size_t total_size = _header.size * sizeof(DataType);

    _mapped_info = std::move(_file.read(_file_offset.value(), total_size));

    if (_mapped_info) {
        _data_map = std::span<DataType>(reinterpret_cast<DataType*>(_mapped_info->pointer), _header.size);
        return true;
    }
    return false;
}

template<typename Header, typename DataType>
inline bool dumpable<Header, DataType>::dump(bool discard_memory) {
    if (_data.empty())
        return false; 

    size_t data_bytes = _data.size() * sizeof(DataType);

    _file_offset = _file.write(reinterpret_cast<const uint8_t*>(_data.data()), data_bytes);

    if (!_file_offset.has_value())
        return false;

    if (discard_memory) {
        _data.clear();
        _data.shrink_to_fit();
        _data_map = std::span<DataType>(); 
        _mapped_info.reset();
        _discarded = true;
    }
    return true;
}
