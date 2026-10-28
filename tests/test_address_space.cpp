#include "address_space.h"
#include "test.h"

#include <functional>
#include <vector>

using namespace linuxxi;

namespace
{
    constexpr uint64_t kMb = uint64_t{1} << 20;

    // Regions back to back from 0, answered as VirtualQuery answers them, counting the questions.
    struct FakeSpace
    {
        std::vector<Region> regions;
        int asked = 0;
        std::optional<Region> operator()(uint64_t at)
        {
            ++asked;
            for (const Region& r : regions)
                if (r.base <= at && at < r.base + r.size) return r;
            return std::nullopt;
        }
    };

    FakeSpace Space(std::vector<std::pair<uint64_t, bool>> mbAndFree)
    {
        FakeSpace space;
        uint64_t at = 0;
        for (const auto& [mb, free] : mbAndFree)
        {
            space.regions.push_back(Region{at, mb * kMb, free});
            at += mb * kMb;
        }
        return space;
    }
}

TEST(the_free_space_sums_the_free_regions_and_ends_where_the_space_does)
{
    FakeSpace space = Space({{1, true}, {1, false}, {1, true}, {1, false}, {1, true}, {1, false}, {1, true}});
    CHECK((FreeSpaceOf(std::ref(space)) == FreeSpace{4 * kMb, 7 * kMb}));
    CHECK_EQ(space.asked, 8); // each region once, and the space's end
}

TEST(under_wine_what_is_left_adds_the_space_linux_never_mapped_to_wines_own_free_space)
{
    // The DXVK test client on 2026-10-17: VirtualQuery called 1,008 MB of 4,095 free and VirtualSize was 2,540 MB, the
    // VmSize Linux gave; its maps had 2,538 MB below 4 GB, so 1,557 MB were never mapped.
    const MemoryReport r = MemoryOf(FreeSpace{1008 * kMb, 4095 * kMb}, 2540 * kMb, true);
    CHECK_EQ(r.leftMb, 2563u);
    CHECK_EQ(r.usedMb, 1532u);
    CHECK_EQ(r.ceilingMb, 4095u);
}

TEST(on_windows_what_is_left_is_what_virtualquery_calls_free)
{
    const MemoryReport r = MemoryOf(FreeSpace{1008 * kMb, 4095 * kMb}, 3087 * kMb, false);
    CHECK_EQ(r.leftMb, 1008u);
    CHECK_EQ(r.usedMb, 3087u);
}

TEST(more_mapped_than_the_space_holds_leaves_only_the_free_space)
{
    const MemoryReport r = MemoryOf(FreeSpace{100 * kMb, 4095 * kMb}, 4200 * kMb, true);
    CHECK_EQ(r.leftMb, 100u);
    CHECK_EQ(r.usedMb, 3995u);
}
