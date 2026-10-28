#include "report.h"

#include "figures.h"

#include <algorithm>
#include <limits>

namespace linuxxi
{
    Report BuildReport(const FixState& fixes, const std::map<std::string, LuaEntry>& lua, uint64_t nowMs, uint64_t staleMs)
    {
        Report r;
        r.flags = (fixes.poolHooked ? kPoolHooked : 0u) | (fixes.poolOn ? kPoolOn : 0u) |
                  (fixes.grace.active ? kGraceActive : 0u) | (fixes.grace.on ? kGraceOn : 0u) |
                  (fixes.localeActive ? kLocaleActive : 0u) | (fixes.localeOn ? kLocaleOn : 0u);
        r.copiesHeld       = fixes.pool.held;
        r.copiesPeak       = fixes.pool.peakHeld;
        r.chunks           = fixes.pool.chunks;
        r.chunksPeak       = fixes.pool.peakChunks;
        r.chunkMb          = static_cast<uint32_t>(kChunkMb);
        r.releasesHeldBack = static_cast<uint32_t>(std::min<uint64_t>(fixes.grace.heldBack, std::numeric_limits<uint32_t>::max()));
        r.releasesWaiting  = fixes.grace.waiting;
        r.addonsStepped    = fixes.stepped;
        r.addonsNotLoaded  = fixes.notLoaded;
        std::vector<std::pair<std::string, uint32_t>> heard;
        for (const auto& [name, entry] : lua)
            if (nowMs - entry.atMs <= staleMs) heard.emplace_back(name, entry.kilobytes);
        std::stable_sort(heard.begin(), heard.end(), [](const auto& a, const auto& b) { return a.second > b.second; }); // ties by name
        r.addonCount = static_cast<uint32_t>(std::min<size_t>(heard.size(), kReportAddons));
        for (uint32_t i = 0; i < r.addonCount; ++i)
        {
            heard[i].first.copy(r.lua[i].name, kReportNameChars - 1);
            r.lua[i].kilobytes = heard[i].second;
        }
        return r;
    }

    std::vector<std::string> LuaLines(const Report& report, size_t perLine)
    {
        uint64_t total = 0;
        for (uint32_t i = 0; i < report.addonCount; ++i)
            total += report.lua[i].kilobytes;
        std::vector<std::string> lines{"Lua memory: " + KbText(total) + " in " + std::to_string(report.addonCount) + " addons."};
        for (uint32_t i = 0; i < report.addonCount; ++i)
        {
            if (i % perLine == 0) lines.emplace_back();
            lines.back() += (i % perLine == 0 ? "" : ", ") + std::string(report.lua[i].name) + " " + KbText(report.lua[i].kilobytes);
        }
        return lines;
    }
}
