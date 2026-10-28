#include "grace.h"

#include <algorithm>
#include <utility>

namespace linuxxi
{
    bool GraceLedger::Hold(uintptr_t object, uint32_t frame, int kind)
    {
        if (std::any_of(m_Held.begin(), m_Held.end(), [&](const Held& held) { return held.object == object; })) return false;
        m_Held.push_back(Held{object, frame, kind});
        ++m_HeldBack;
        return true;
    }

    std::vector<Held> GraceLedger::TakeAll() { return std::exchange(m_Held, {}); }

    std::vector<Held> GraceLedger::Due(uint32_t frame, const std::vector<uintptr_t>& bound)
    {
        std::vector<Held> due;
        auto keep = m_Held.begin();
        for (Held held : m_Held)
        {
            if (frame - held.frame < kGraceFrames)
                *keep++ = held;
            else if (std::find(bound.begin(), bound.end(), held.object) != bound.end())
            {
                held.frame = frame;
                *keep++    = held;
            }
            else
                due.push_back(held);
        }
        m_Held.erase(keep, m_Held.end());
        return due;
    }
}
