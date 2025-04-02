#pragma once
#include "../file_dump/dumpable.hpp"


extern file_dump memory_dump;

struct region_header {
    uint64_t base;  // The base address of the memory region
    size_t size;    // The size of the memory region (data only)
};

class memory_region : public dumpable<region_header, uint8_t>
{
    MEMORY_BASIC_INFORMATION _mbi;

public:

    memory_region(const MEMORY_BASIC_INFORMATION& mbi)
        : _mbi(mbi), dumpable<region_header, uint8_t>(memory_dump)
    {
        _header.base = reinterpret_cast<uint64_t>(mbi.BaseAddress);
        _header.size = mbi.RegionSize;
    }

    ~memory_region() = default;

    // Delete copy constructor and copy assignment operator.
    memory_region(const memory_region&) = delete;
    memory_region& operator=(const memory_region&) = delete;

    memory_region(memory_region&& other) noexcept
        : dumpable<region_header, uint8_t>(std::move(other))  // Call move constructor of the base class
        , _mbi(other._mbi)  // Move or copy any additional members specific to memory_region
    {
    }

    memory_region& operator=(memory_region&& other) noexcept
    {
        if (this != &other)
        {
            // Move the base class parts.
            dumpable<region_header, uint8_t>::operator=(std::move(other));  // Call move assignment operator of the base class

            // Move or copy any additional members specific to memory_region
            _mbi = other._mbi;
        }
        return *this;
    }

    template <typename DataType>
    DataType* at_offset(size_t offset);

    template <typename DataType>
    DataType* at_index(size_t index);

    template <typename DataType>
    DataType* at_address(uint64_t address);

    __forceinline bool contains(uint64_t address) {
        if (address < this->base() || address > this->base() + this->size())
            return false;
        return true;
    }

    template <typename F>
    bool read_data(F&& read_func, size_t& bytes_read);

    __forceinline uint64_t base() { return _header.base; }

    __forceinline size_t size() { return _header.size; }

    __forceinline bool has_protection_flags(DWORD protect_flags) {
		return (_mbi.Protect & protect_flags) != 0;
    }

    __forceinline bool is_commited() {
        return _mbi.State == MEM_COMMIT;
    }

	__forceinline bool is_memmapped() {
		return _mbi.Type == MEM_MAPPED;
	}
};


template<typename DataType>
inline DataType* memory_region::at_offset(size_t offset)
{
    // Check if the offset is within the valid range.
    if (offset + sizeof(DataType) > _header.size)
        return nullptr;

    // Check if the memory region is valid.
    if (!_valid)
        return nullptr;

    // If the region hasn't been discarded, use the primary data buffer.
    if (!_discarded)
        return reinterpret_cast<DataType*>(_data.data() + offset);

    // If the region is discarded, try to map the chunk if needed.
    if (!_mapped_info && !load())
        return nullptr;

    return reinterpret_cast<DataType*>(_data_map.data() + offset);
}

template<typename DataType>
inline DataType* memory_region::at_index(size_t index)
{
    // Check if the index is within valid bounds.
    if ((index + 1) * sizeof(DataType) > _header.size)
        return nullptr;

    size_t offset = index * sizeof(DataType);
    return at_offset<DataType>(offset);
}

template<typename DataType>
inline DataType* memory_region::at_address(uint64_t address)
{
    // Check if the provided address is within this memory region.
    if (!contains(address))
        return nullptr;

    size_t offset = static_cast<size_t>(address - base());
    return at_offset<DataType>(offset);
}
template<typename F>
inline bool memory_region::read_data(F&& read_func, size_t& bytes_read) {
    // Resize the vector to hold the required data.

    _data.resize(_header.size);

    // Call the provided functor to read memory.
    // Expected callable signature:
    //    bool(uint64_t address, void* buffer, size_t size, size_t* bytes_read)
    if (read_func(_header.base, _data.data(), _header.size, &bytes_read)) {
        if (_header.size != bytes_read) {
            _data.resize(_header.size);
        }

        _header.size = bytes_read;
        _valid = true;
        _data_map = std::span<uint8_t>(_data);
        return true;
    }

    // If read fails, clear the vector.
    _data.clear();
    _data.shrink_to_fit();
    _data_map = std::span<uint8_t>();  // Invalidate the span.
    _valid = false;

    return _valid;
}
