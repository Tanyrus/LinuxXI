#include "report.h"
#include "test.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

using namespace linuxxi;

namespace
{
    FixState Running()
    {
        FixState f;
        f.pool.held = 588, f.pool.peakHeld = 1348, f.pool.chunks = 3, f.pool.peakChunks = 4;
        f.poolHooked = f.poolOn = f.grace.active = true;
        f.grace.on       = false;
        f.grace.heldBack = 2055, f.grace.waiting = 1, f.stepped = 25, f.notLoaded = 3;
        return f;
    }
}

TEST(the_report_carries_each_fixs_counters_and_state)
{
    FixState f                     = Running();
    const Report r = BuildReport(f, {}, 0, 1000);
    const uint32_t others          = kPoolHooked | kPoolOn | kGraceActive;
    CHECK(r.flags == others);
    CHECK(r.copiesHeld == 588 && r.copiesPeak == 1348 && r.chunks == 3 && r.chunksPeak == 4);
    CHECK(r.releasesHeldBack == 2055 && r.releasesWaiting == 1 && r.addonsStepped == 25 && r.addonsNotLoaded == 3);
    CHECK(r.addonCount == 0);
    f.localeActive = true;
    CHECK(BuildReport(f, {}, 0, 1000).flags == (others | kLocaleActive));
    f.localeOn = true;
    CHECK(BuildReport(f, {}, 0, 1000).flags == (others | kLocaleActive | kLocaleOn));
}

TEST(the_addons_go_largest_first_and_one_not_heard_from_lately_is_left_out)
{
    const std::map<std::string, LuaEntry> lua{{"clock", {300, 9000}}, {"xiui", {41250, 9500}}, {"craftbook", {9800, 9900}},
        {"gone", {99999, 1000}}};
    const Report r = BuildReport(Running(), lua, 10000, 5000);
    CHECK(r.addonCount == 3);
    CHECK(std::string(r.lua[0].name) == "xiui" && r.lua[0].kilobytes == 41250);
    CHECK(std::string(r.lua[1].name) == "craftbook" && std::string(r.lua[2].name) == "clock");
}

TEST(only_the_largest_addons_fit_and_a_long_name_is_cut)
{
    std::map<std::string, LuaEntry> lua;
    for (uint32_t i = 0; i < kReportAddons + 5; ++i)
        lua["addon" + std::to_string(i)] = LuaEntry{1000 + i, 0};
    lua[std::string(40, 'n')] = LuaEntry{1, 0};
    const Report r = BuildReport(Running(), lua, 0, 1000);
    CHECK(r.addonCount == kReportAddons);
    CHECK(r.lua[0].kilobytes == 1000 + kReportAddons + 4);
    CHECK(r.lua[kReportAddons - 1].kilobytes == 1005);
    std::map<std::string, LuaEntry> one{{std::string(40, 'n'), LuaEntry{1, 0}}};
    CHECK(std::strlen(BuildReport(Running(), one, 0, 1000).lua[0].name) == kReportNameChars - 1);
}

TEST(the_chat_lines_give_the_total_then_the_addons_a_few_to_a_line)
{
    Report r;
    r.addonCount = 3;
    std::strcpy(r.lua[0].name, "xiui");
    r.lua[0].kilobytes = 41250;
    std::strcpy(r.lua[1].name, "craftbook");
    r.lua[1].kilobytes = 9800;
    std::strcpy(r.lua[2].name, "clock");
    r.lua[2].kilobytes = 300;
    const std::vector<std::string> lines = LuaLines(r, 2);
    CHECK(lines.size() == 3);
    CHECK(lines[0].find("50.1 MB") != std::string::npos && lines[0].find('3') != std::string::npos);
    CHECK(lines[1].find("xiui 40.3 MB") != std::string::npos && lines[1].find("craftbook 9.6 MB") != std::string::npos);
    CHECK(lines[2].find("clock 300 KB") != std::string::npos);
}

TEST(a_held_back_count_past_32_bits_reads_as_the_most_a_report_holds)
{
    FixState f;
    f.grace.heldBack = uint64_t{1} << 32;
    CHECK(BuildReport(f, {}, 0, 1000).releasesHeldBack == std::numeric_limits<uint32_t>::max());
}

TEST(an_addon_heard_exactly_the_stale_time_ago_is_kept_and_a_moment_later_dropped)
{
    const std::map<std::string, LuaEntry> lua{{"clock", {300, 5000}}};
    CHECK(BuildReport(FixState{}, lua, 10000, 5000).addonCount == 1);
    CHECK(BuildReport(FixState{}, lua, 10001, 5000).addonCount == 0);
}

TEST(addons_of_the_same_size_keep_their_names_order_from_report_to_report)
{
    // As many as a player's set, most near 2 MB: std::sort keeps small inputs in order by chance, not these.
    std::map<std::string, LuaEntry> lua;
    for (int i = 0; i < 40; ++i)
        lua["addon" + std::to_string(100 + i)] = LuaEntry{2048, 0};
    lua["xiui"] = LuaEntry{41250, 0};
    const Report r = BuildReport(FixState{}, lua, 0, 1000);
    CHECK(std::string(r.lua[0].name) == "xiui");
    std::vector<std::string> tied;
    for (uint32_t i = 1; i < r.addonCount; ++i)
        tied.push_back(r.lua[i].name);
    CHECK(std::is_sorted(tied.begin(), tied.end()));
}
