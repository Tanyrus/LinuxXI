#pragma once

#include "grace.h"
#include "pool.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace linuxxi
{
    // Raised to every plugin through Ashita's plugin events; HeadsUp's Memory widget reads its bytes, so its layout
    // changes only with the version raised. Fixed-width fields alone, so the 32-bit build and the 64-bit tests agree.
    constexpr const char* kReportEvent = "linuxxi_report";
    constexpr double kReportSeconds    = 1.0;
    constexpr uint32_t kReportVersion  = 1;
    constexpr uint32_t kReportAddons   = 64;
    constexpr uint32_t kReportNameChars = 32; // with its terminator

    enum ReportFlags : uint32_t
    {
        kPoolHooked   = 1, // the addon host's copies come from the pool
        kPoolOn       = 2,
        kGraceActive  = 4, // Direct3D's releases are held back
        kGraceOn      = 8,
        kLocaleActive = 16, // sandbox's registry compares keep to the C locale
        kLocaleOn     = 32,
    };

    struct ReportAddon
    {
        char name[kReportNameChars];
        uint32_t kilobytes;
    };

    struct Report
    {
        uint32_t size    = sizeof(Report);
        uint32_t version = kReportVersion;
        uint32_t flags   = 0;
        uint32_t copiesHeld = 0, copiesPeak = 0, chunks = 0, chunksPeak = 0, chunkMb = 0;
        uint32_t releasesHeldBack = 0, releasesWaiting = 0;
        uint32_t addonsStepped = 0, addonsNotLoaded = 0;
        uint32_t addonCount = 0; // of lua, largest first
        ReportAddon lua[kReportAddons]{};
    };
    static_assert(sizeof(ReportAddon) == 36 && sizeof(Report) == 52 + 36 * kReportAddons);

    struct LuaEntry
    {
        uint32_t kilobytes = 0;
        uint64_t atMs      = 0; // when the addon last said
    };

    struct FixState
    {
        PoolStats pool;
        GraceState grace;
        bool poolHooked = false, poolOn = false, localeActive = false, localeOn = false;
        uint32_t stepped = 0, notLoaded = 0;
    };

    // The fixes' counters, and the addons heard from within staleMs, largest first.
    Report BuildReport(const FixState& fixes, const std::map<std::string, LuaEntry>& lua, uint64_t nowMs, uint64_t staleMs);
    // The addons' Lua memory as chat lines: their total, then perLine addons a line.
    std::vector<std::string> LuaLines(const Report& report, size_t perLine);
}
