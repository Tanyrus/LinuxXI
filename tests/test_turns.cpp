#include "test.h"
#include "turns.h"

#include <algorithm>
#include <optional>
#include <vector>

using namespace linuxxi;

namespace
{
    // Every addon given a turn from start to end, one call a millisecond.
    std::vector<size_t> TurnsBetween(AddonTurns& turns, uint64_t start, uint64_t end)
    {
        std::vector<size_t> given;
        for (uint64_t now = start; now < end; ++now)
            if (const std::optional<size_t> next = turns.Next(now)) given.push_back(*next);
        return given;
    }
}

TEST(each_addon_has_its_turn_in_order_one_a_spacing)
{
    AddonTurns turns(3);
    CHECK(TurnsBetween(turns, 0, 4 * kSpacingMs) == (std::vector<size_t>{0, 1, 2, 0}));
}

TEST(an_addon_not_loaded_is_left_out_until_its_retry)
{
    AddonTurns turns(3);
    turns.NotLoaded(1, 0);
    CHECK(!turns.Loaded(1) && turns.Loaded(0));
    CHECK_EQ(turns.NotLoadedCount(), size_t{1});
    CHECK(TurnsBetween(turns, 0, 4 * kSpacingMs) == (std::vector<size_t>{0, 2, 0, 2}));
    const std::vector<size_t> later = TurnsBetween(turns, kRetryMs, kRetryMs + 3 * kSpacingMs);
    CHECK(later.size() == 3 && std::find(later.begin(), later.end(), size_t{1}) != later.end());
    CHECK(turns.Loaded(1) && turns.NotLoadedCount() == 0);
}

TEST(no_turn_is_given_when_every_addon_is_missing_or_there_are_none)
{
    AddonTurns none(0);
    CHECK(TurnsBetween(none, 0, 2 * kSpacingMs).empty());
    AddonTurns one(1);
    one.NotLoaded(0, 0);
    CHECK(TurnsBetween(one, 0, 2 * kSpacingMs).empty());
}

// A figure is dropped once its addon has missed three turns, and never sooner than ten seconds.
TEST(a_lua_figure_goes_stale_after_three_turns_or_ten_seconds)
{
    CHECK_EQ(AddonTurns(2).StaleMs(), uint64_t{10000});
    CHECK_EQ(AddonTurns(40).StaleMs(), uint64_t{3 * 40 * kSpacingMs});
}
