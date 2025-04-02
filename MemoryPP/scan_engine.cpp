#include "scan_engine.hpp"


std::queue<std::shared_ptr<memory_region>> scan_engine::get_regions(std::pair<void*, void*> range, DWORD protection_flags)
{
    std::queue<std::shared_ptr<memory_region>> regions;

    auto current_address = range.first;

    HANDLE h_process = LongToHandle(this->get_pid());

    while (current_address < range.second) {
        MEMORY_BASIC_INFORMATION mbi;

        if (VirtualQueryEx(h_process, current_address, &mbi, sizeof(mbi)) == 0) {
            break;
        }

        if (reinterpret_cast<BYTE*>(mbi.BaseAddress) < range.first)
            mbi.BaseAddress = range.first;

        if (reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize > range.second)
            mbi.RegionSize = reinterpret_cast<uint64_t>(range.second) - reinterpret_cast<uint64_t>(mbi.BaseAddress);

        auto current_region = std::make_shared<memory_region>(mbi);


        if (current_region && current_region->has_protection_flags(protection_flags) && current_region->is_commited() && !current_region->is_memmapped())
            regions.push(current_region);

        current_address = reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
    }

    return regions;
}

bool scan_engine::read_memory(std::shared_ptr<memory_region> region)
{
    size_t bytes_read = 0;

    HANDLE h_process = LongToHandle(this->get_pid());

    auto success = region->read_data(
        [h_process](uint64_t address, void* buffer, size_t size, size_t* bytes_read_ptr) -> bool {
            SIZE_T local_bytes_read = 0;
            BOOL ok = ReadProcessMemory(h_process,
                reinterpret_cast<LPCVOID>(address),
                buffer,
                size,
                &local_bytes_read);
            if (bytes_read_ptr) {
                *bytes_read_ptr = static_cast<size_t>(local_bytes_read);
            }
            // Return true only if the full read succeeded.
            return (ok && local_bytes_read == size);
        },
        bytes_read
    );

    return success;
}
