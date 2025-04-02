#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <cstdint>
#include <string>
#include <iostream>
#include <memory>
#include <cstring>
#include <utility>     
#include "scan_engine.hpp"

file_dump memory_dump("dump.bin");
file_dump results("results.bin");

bool get_main_module_info(DWORD pid, uintptr_t& base_address, SIZE_T& module_size) {
    HANDLE h_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (h_snapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create snapshot: " << GetLastError() << "\n";
        return false;
    }

    MODULEENTRY32 module_entry;
    module_entry.dwSize = sizeof(MODULEENTRY32);

    if (Module32First(h_snapshot, &module_entry)) {
        base_address = reinterpret_cast<uintptr_t>(module_entry.modBaseAddr);
        module_size = module_entry.modBaseSize;
        CloseHandle(h_snapshot);
        return true;
    }

    std::cerr << "Failed to get module info.\n";
    CloseHandle(h_snapshot);
    return false;
}



int main() {

    DWORD pid = 0;
    std::cout << "Enter the process id: " << std::endl;
    std::cin >> pid;

    uintptr_t base_address;
    SIZE_T module_size;


    if (!get_main_module_info(pid, base_address, module_size)) {
        return 0;
    }

    HANDLE h_process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h_process) {
        std::cerr << "Failed to open process: " << GetLastError() << "\n";
        return 0;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    auto module_start = reinterpret_cast<LPVOID>(sysInfo.lpMinimumApplicationAddress);
    auto module_end = reinterpret_cast<LPVOID>(sysInfo.lpMaximumApplicationAddress);

    int value = 0;
    auto engine = scan_engine_templated<int>(HandleToLong(h_process));

    std::cout << "Value to search for: " << std::endl;
    std::cin >> value;
    std::cout << "Total values found: " << engine.scan({ module_start, module_end }, scan_type::exact_value, value) << std::endl;

yes:
    std::cout << "Scan again? (y/n)" << std::endl;
    char c;
    std::cin >> c;
    if (c == 'y') {
        std::cout << "Value to search for: " << std::endl;
        std::cin >> value;

        std::cout << "Total values found: " << engine.scan({ module_start, module_end }, scan_type::exact_value, value) << std::endl;



        goto yes;
    }

    auto results = engine.get_results();

    if (results) {

        auto keys = results->keys();

        for (auto key : keys) {
            auto result = results->at(key);

            auto elements = result->elements();

            for (auto& elem : elements) {
                std::cout << "Value: " << elem.value << " Address: " << std::hex << elem.address << std::dec << "\n";
            }
        }
    }

    CloseHandle(h_process);
    return 0;


}