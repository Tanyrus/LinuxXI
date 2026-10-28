#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace linuxxi
{
    constexpr uint64_t kSpacingMs  = 300;   // one addon is sent its code a turn
    constexpr uint64_t kRetryMs    = 60000; // an addon that answered "not loaded" is asked again after
    constexpr uint64_t kStaleTurns = 3;     // an addon's Lua figure older than this many of its turns is dropped
    constexpr uint64_t kMinStaleMs = 10000; // and never sooner

    // Which addon is sent its code next: one a kSpacingMs in turn, an addon that answered "not loaded" left out for
    // kRetryMs.
    class AddonTurns
    {
    public:
        explicit AddonTurns(size_t addons) : m_Count(addons) {}

        // The addon whose turn it is at nowMs; nothing between turns or when every addon is left out.
        std::optional<size_t> Next(uint64_t nowMs);
        void NotLoaded(size_t addon, uint64_t nowMs);
        bool Loaded(size_t addon) const { return m_Missing.find(addon) == m_Missing.end(); }
        size_t NotLoadedCount() const { return m_Missing.size(); }
        uint64_t StaleMs() const;

    private:
        size_t m_Count;
        size_t m_Turn     = 0;
        uint64_t m_NextAt = 0;
        std::map<size_t, uint64_t> m_Missing; // when each answered "not loaded"
    };
}
