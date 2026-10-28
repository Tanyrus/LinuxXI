#pragma once

#include "address_space.h"

#include <cstdint>
#include <optional>

namespace linuxxi
{
    // Whether the game runs under Wine: every prefix has HKCU\Software\Wine, and Proton hides ntdll's wine_get_version.
    // It opens a registry key, which sandbox.dll's hook compares: ask once, at load.
    bool UnderWine();
    // The process's VirtualSize, which Wine takes from Linux's own count of what is mapped; nothing when ntdll will not say.
    std::optional<uint64_t> VirtualSizeNow();
    // VirtualQuery's answer for the region at an address; nothing past the 32-bit space.
    std::optional<Region> RegionAt(uint64_t at);
}
