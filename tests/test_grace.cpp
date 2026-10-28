#include "grace.h"
#include "test.h"

#include <vector>

using namespace linuxxi;

namespace
{
    constexpr uintptr_t kTexture = 0x1000, kBuffer = 0x2000;
    constexpr int kTextureKind = 0, kBufferKind = 2;

    std::vector<uintptr_t> Objects(const std::vector<Held>& held)
    {
        std::vector<uintptr_t> objects;
        for (const Held& h : held)
            objects.push_back(h.object);
        return objects;
    }
}

TEST(a_last_release_reaches_direct3d_only_once_its_frames_have_passed)
{
    GraceLedger ledger;
    CHECK(ledger.Hold(kTexture, 10, kTextureKind));
    CHECK(ledger.Due(11, {}).empty());
    CHECK(ledger.Due(12, {}).empty());
    const std::vector<Held> due = ledger.Due(13, {});
    CHECK(Objects(due) == std::vector<uintptr_t>{kTexture});
    CHECK(due[0].kind == kTextureKind);
    CHECK(ledger.Waiting() == 0);
}

TEST(an_object_released_again_while_held_is_held_once)
{
    GraceLedger ledger;
    CHECK(ledger.Hold(kTexture, 10, kTextureKind));
    CHECK(!ledger.Hold(kTexture, 11, kTextureKind));
    CHECK(ledger.Waiting() == 1);
    CHECK(ledger.HeldBack() == 1);
    CHECK(Objects(ledger.Due(13, {})) == std::vector<uintptr_t>{kTexture});
}

TEST(an_object_the_device_still_has_bound_waits_its_frames_again_once_unbound)
{
    GraceLedger ledger;
    ledger.Hold(kBuffer, 10, kBufferKind);
    ledger.Hold(kTexture, 10, kTextureKind);
    CHECK(Objects(ledger.Due(13, {kBuffer})) == std::vector<uintptr_t>{kTexture});
    CHECK(ledger.Due(15, {}).empty());
    const std::vector<Held> due = ledger.Due(16, {});
    CHECK(Objects(due) == std::vector<uintptr_t>{kBuffer});
    CHECK(due[0].kind == kBufferKind);
    CHECK(ledger.HeldBack() == 2);
}

// A lost device must be rid of every default-pool resource before its Reset, so nothing waits for frames or binding.
TEST(everything_held_goes_at_once_when_the_device_is_lost)
{
    GraceLedger ledger;
    ledger.Hold(kBuffer, 10, kBufferKind);
    ledger.Hold(kTexture, 12, kTextureKind);
    const std::vector<Held> all = ledger.TakeAll();
    CHECK(Objects(all) == (std::vector<uintptr_t>{kBuffer, kTexture}));
    CHECK(all[0].kind == kBufferKind && all[1].kind == kTextureKind);
    CHECK(ledger.Waiting() == 0);
    CHECK(ledger.Due(20, {}).empty());
    CHECK(ledger.HeldBack() == 2);
}

TEST(the_frame_count_wrapping_round_does_not_release_early)
{
    GraceLedger ledger;
    ledger.Hold(kTexture, 0xFFFFFFFEu, kTextureKind);
    CHECK(ledger.Due(0xFFFFFFFFu, {}).empty());
    CHECK(ledger.Due(0, {}).empty());
    CHECK(Objects(ledger.Due(1, {})) == std::vector<uintptr_t>{kTexture});
}
