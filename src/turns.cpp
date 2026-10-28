#include "turns.h"

#include <algorithm>

namespace linuxxi
{
    std::optional<size_t> AddonTurns::Next(uint64_t nowMs)
    {
        if (m_Count == 0 || nowMs < m_NextAt) return std::nullopt;
        m_NextAt = nowMs + kSpacingMs;
        for (size_t tried = 0; tried < m_Count; ++tried)
        {
            const size_t addon = m_Turn;
            m_Turn             = (m_Turn + 1) % m_Count;
            const auto missing = m_Missing.find(addon);
            if (missing != m_Missing.end() && nowMs - missing->second < kRetryMs) continue;
            if (missing != m_Missing.end()) m_Missing.erase(missing);
            return addon;
        }
        return std::nullopt;
    }

    void AddonTurns::NotLoaded(size_t addon, uint64_t nowMs) { m_Missing[addon] = nowMs; }

    uint64_t AddonTurns::StaleMs() const { return std::max<uint64_t>(kMinStaleMs, kStaleTurns * kSpacingMs * m_Count); }
}
