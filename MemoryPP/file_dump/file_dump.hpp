#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <cstdint>
#include <string>
#include <iostream>
#include <memory>
#include <cstring>
#include <utility>
#include <span>
#include <mutex>
#include <optional>

// RAII wrapper per HANDLE.
class unique_handle {
public:
    explicit unique_handle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~unique_handle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;
    unique_handle(unique_handle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            if (handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    HANDLE get() const { return handle_; }
    // Rilascio della HANDLE senza chiuderla.
    HANDLE release() {
        HANDLE tmp = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return tmp;
    }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// File header usato per il file dump.
struct file_header {
    uint32_t magic_number{ 0xDEADBEEF };
    uint32_t number_of_entries{ 0 };
};

struct mapped_chunk {
    LPVOID view_base;        // Base pointer returned by MapViewOfFile (used for unmapping)
    LPVOID pointer;          // Pointer to the mapped chunk (may differ from view_base due to alignment)
    size_t view_size;        // Total size of the mapped view (including alignment offset)
    uint64_t map_offset;     // Aligned offset used for mapping (multiple of system granularity)
    size_t chunk_size;       // The actual requested size (i.e., header + region data)
    HANDLE file_handle;      // Handle to the opened file
    HANDLE mapping_handle;   // Handle to the mapping object

    mapped_chunk() = default;

    ~mapped_chunk();

    mapped_chunk(const mapped_chunk&) = delete;
    mapped_chunk& operator=(const mapped_chunk&) = delete;

    mapped_chunk(mapped_chunk&& other) noexcept;


    mapped_chunk& operator=(mapped_chunk&& other) noexcept;
};



class file_dump {
private:
    const size_t INITIAL_MAP_SIZE = 100 * 1024 * 1024;
    const size_t BUFFER_SIZE = 100 * 1024 * 1024;

    file_header _header;
    std::string _file_name;
    size_t _current_size{ 0 };
    std::mutex _mutex;

    std::vector<uint8_t> _buffer;
    size_t _buffer_pos{ 0 };

private:
     std::unique_ptr<mapped_chunk>      map_file(const uint64_t& offset, const size_t& size);
     bool                               write_file(uint64_t offset, const uint8_t* buffer, const size_t& size);

public:
    file_dump(const std::string& file_name);
    ~file_dump();

    std::unique_ptr<mapped_chunk>   read(const uint64_t& offset, const size_t& size);

    std::optional<size_t>           write(const uint8_t* buffer, const size_t& size);

    __forceinline size_t            get_size() { return _current_size; }
};



