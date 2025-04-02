#include "../file_dump.hpp"

mapped_chunk::~mapped_chunk()
{
    if (view_base) {
        UnmapViewOfFile(view_base);
    }
    if (mapping_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(mapping_handle);
    }
    if (file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle);
    } 
}

mapped_chunk::mapped_chunk(mapped_chunk&& other) noexcept
    : view_base(other.view_base),
    pointer(other.pointer),
    view_size(other.view_size),
    map_offset(other.map_offset),
    chunk_size(other.chunk_size),
    file_handle(other.file_handle),
    mapping_handle(other.mapping_handle)
{
    other.view_base = nullptr;
    other.pointer = nullptr;
    other.view_size = 0;
    other.map_offset = 0;
    other.chunk_size = 0;
    other.file_handle = INVALID_HANDLE_VALUE;
    other.mapping_handle = INVALID_HANDLE_VALUE;
}

mapped_chunk& mapped_chunk::operator=(mapped_chunk&& other) noexcept
{
    if (this != &other) {
        view_base = other.view_base;
        pointer = other.pointer;
        view_size = other.view_size;
        map_offset = other.map_offset;
        chunk_size = other.chunk_size;
        file_handle = other.file_handle;
        mapping_handle = other.mapping_handle;

        other.view_base = nullptr;
        other.pointer = nullptr;
        other.view_size = 0;
        other.map_offset = 0;
        other.chunk_size = 0;
        other.file_handle = INVALID_HANDLE_VALUE;
        other.mapping_handle = INVALID_HANDLE_VALUE;
    }
    return *this;
}

std::unique_ptr<mapped_chunk> file_dump::map_file(const uint64_t& offset, const size_t& size)
{

    DWORD desired_access = GENERIC_READ | GENERIC_WRITE;
    DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD creation_disposition = OPEN_ALWAYS;

    unique_handle file_handle(CreateFileA(_file_name.c_str(),
        desired_access,
        share_mode,
        nullptr,
        creation_disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));

    if (file_handle.get() == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    uint64_t required_size = offset + size;

    if (_current_size < required_size) {
        LARGE_INTEGER new_size;
        new_size.QuadPart = required_size;
        if (!SetFilePointerEx(file_handle.get(), new_size, nullptr, FILE_BEGIN) || !SetEndOfFile(file_handle.get())) {
            return nullptr;
        }
    }

    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    uint64_t granularity = sys_info.dwAllocationGranularity;

    // Allinea l'offset di mapping alla granularità di sistema.
    uint64_t map_offset = (offset / granularity) * granularity;
    uint64_t offset_diff = offset - map_offset;
    size_t view_size = offset_diff + size;

    DWORD fl_protect = PAGE_READWRITE;
    unique_handle mapping_handle(CreateFileMappingA(file_handle.get(),
        nullptr,
        fl_protect,
        0,
        0,
        nullptr));

    if (mapping_handle.get() == nullptr) {
        return nullptr;
    }

    DWORD file_offset_high = static_cast<DWORD>(map_offset >> 32);
    DWORD file_offset_low = static_cast<DWORD>(map_offset & 0xFFFFFFFF);
    LPVOID view_base = MapViewOfFile(mapping_handle.get(),
        FILE_MAP_WRITE,
        file_offset_high,
        file_offset_low,
        view_size);

    if (!view_base) {
        return nullptr;
    }

    auto chunk = std::make_unique<mapped_chunk>();
    chunk->view_base = view_base;
    chunk->pointer = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(view_base) + offset_diff);
    chunk->view_size = view_size;
    chunk->map_offset = map_offset;
    chunk->chunk_size = size;
    chunk->file_handle = file_handle.release();
    chunk->mapping_handle = mapping_handle.release();

    return chunk;

}

bool file_dump::write_file(uint64_t offset, const uint8_t* buffer, const size_t& size)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto chunk = map_file(offset, size);

    if (!chunk) {
        return false;
    }

    std::memcpy(chunk->pointer, buffer, size);

    if (!FlushViewOfFile(chunk->view_base, chunk->view_size)) {
        return false;
    }

    uint64_t new_size = offset + size;
    if (new_size > _current_size) {
        _current_size = new_size;
    }

    return true;
}

file_dump::file_dump(const std::string& file_name) : _file_name(file_name) {
    // Apro o creo il file.
    unique_handle file_handle(CreateFileA(_file_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS, // Crea il file se non esiste
        FILE_ATTRIBUTE_NORMAL,
        nullptr));

    if (file_handle.get() != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER file_size;
        if (GetFileSizeEx(file_handle.get(), &file_size)) {
            _current_size = static_cast<size_t>(file_size.QuadPart);
        }
    }

    _buffer.resize(BUFFER_SIZE);
    _buffer_pos = 0;

}

file_dump::~file_dump() {
    DeleteFileA(_file_name.c_str());
}

std::unique_ptr<mapped_chunk> file_dump::read(const uint64_t& offset, const size_t& size)
{
    if (_buffer_pos > 0) {
        if (!write_file(get_size(), _buffer.data(), _buffer_pos))
            return nullptr;

        _buffer_pos = 0;
    }

    return std::move(map_file(offset, size));
}


std::optional<uint64_t> file_dump::write(const uint8_t* buffer, const size_t& size)
{
    std::optional<uint64_t> file_offset = std::nullopt;

    if (_buffer_pos + size <= BUFFER_SIZE) {

        std::lock_guard<std::mutex> lock(_mutex);

        std::memcpy(_buffer.data() + _buffer_pos, buffer, size);

        file_offset = get_size() + _buffer_pos;

        _buffer_pos += size;
        
    }
    else {
        if (_buffer_pos > 0) {
            if (!write_file(get_size(), _buffer.data(), _buffer_pos))
                return std::nullopt;

            _buffer_pos = 0;
        }
        if (size > BUFFER_SIZE) {
            if (!write_file(get_size(), buffer, size))
                return std::nullopt;
        }
        else {
            std::lock_guard<std::mutex> lock(_mutex);

            std::memcpy(_buffer.data(), buffer, size);

            _buffer_pos = size;
        }

        file_offset = get_size();
    }

    return file_offset;
}

