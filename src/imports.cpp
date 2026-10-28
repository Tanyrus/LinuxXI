#include "imports.h"

#include <cstdint>
#include <cstring>

namespace linuxxi
{
    void** ImportSlot(HMODULE module, const char* dll, const char* function)
    {
        auto* base      = reinterpret_cast<uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& table = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (table.VirtualAddress == 0) return nullptr;
        for (auto* entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + table.VirtualAddress); entry->Name != 0; ++entry)
        {
            if (_stricmp(reinterpret_cast<const char*>(base + entry->Name), dll) != 0 || entry->OriginalFirstThunk == 0) continue;
            auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + entry->OriginalFirstThunk);
            auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + entry->FirstThunk);
            for (; names->u1.AddressOfData != 0; ++names, ++slots)
            {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0) return reinterpret_cast<void**>(&slots->u1.Function);
            }
        }
        return nullptr;
    }

    bool WriteSlot(void** slot, void* value)
    {
        DWORD protection = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
        *slot = value;
        VirtualProtect(slot, sizeof(void*), protection, &protection);
        return true;
    }
}
