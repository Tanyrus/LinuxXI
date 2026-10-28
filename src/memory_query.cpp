#include "memory_query.h"

#include <windows.h>
#include <winternl.h>

namespace linuxxi
{
    namespace
    {
        constexpr const char* kWineKey = "Software\\Wine";
        constexpr uint64_t kSpaceEnd   = uint64_t{1} << 32;
    }

    bool UnderWine()
    {
        HKEY key        = nullptr;
        const bool wine = RegOpenKeyExA(HKEY_CURRENT_USER, kWineKey, 0, KEY_READ, &key) == ERROR_SUCCESS;
        if (key != nullptr) RegCloseKey(key);
        return wine;
    }

    std::optional<uint64_t> VirtualSizeNow()
    {
        using QueryFn           = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        static const auto query = reinterpret_cast<QueryFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess")));
        VM_COUNTERS counters{};
        if (query == nullptr || query(GetCurrentProcess(), ProcessVmCounters, &counters, sizeof counters, nullptr) != 0) return std::nullopt;
        return counters.VirtualSize;
    }

    std::optional<Region> RegionAt(uint64_t at)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (at >= kSpaceEnd || VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(at)), &info, sizeof info) == 0) return std::nullopt;
        return Region{reinterpret_cast<uintptr_t>(info.BaseAddress), info.RegionSize, info.State == MEM_FREE};
    }
}
