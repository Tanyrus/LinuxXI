#include "address_space.h"

#include <algorithm>

namespace linuxxi
{
    namespace
    {
        constexpr int kMbShift = 20;
        uint32_t Mb(uint64_t bytes) { return static_cast<uint32_t>(bytes >> kMbShift); }
    }

    FreeSpace FreeSpaceOf(const RegionQuery& query)
    {
        FreeSpace space;
        for (;;)
        {
            const std::optional<Region> region = query(space.ceiling);
            const uint64_t end                 = region ? region->base + region->size : 0;
            if (!region || end <= space.ceiling) return space;
            if (region->free) space.freeBytes += end - space.ceiling;
            space.ceiling = end;
        }
    }

    MemoryReport MemoryOf(const FreeSpace& space, uint64_t mapped, bool wine)
    {
        const uint64_t unmapped = wine && mapped < space.ceiling ? space.ceiling - mapped : 0;
        const uint64_t left     = std::min(space.ceiling, space.freeBytes + unmapped);
        return MemoryReport{Mb(space.ceiling - left), Mb(left), Mb(space.ceiling)};
    }
}
