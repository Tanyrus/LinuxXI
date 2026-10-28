#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace linuxxi
{
    // One region as VirtualQuery gives it.
    struct Region
    {
        uint64_t base = 0, size = 0;
        bool free = false;
    };

    struct FreeSpace
    {
        uint64_t freeBytes = 0, ceiling = 0; // ceiling: where the space VirtualQuery answers for ends
        bool operator==(const FreeSpace&) const = default;
    };

    // query answers for the region at an address, nothing past the space's end.
    using RegionQuery = std::function<std::optional<Region>(uint64_t at)>;

    FreeSpace FreeSpaceOf(const RegionQuery& query);

    struct MemoryReport
    {
        uint32_t usedMb = 0, leftMb = 0, ceilingMb = 0;
        bool operator==(const MemoryReport&) const = default;
    };

    // What is used and left of the space, mapped being the process's VirtualSize. Under Wine, VirtualQuery calls free
    // only the unused part of Wine's own reservations (which VirtualSize counts) and calls what Linux has not mapped,
    // with what native libraries have, reserved; Wine places memory in both, so both are left.
    MemoryReport MemoryOf(const FreeSpace& space, uint64_t mapped, bool wine);
}
