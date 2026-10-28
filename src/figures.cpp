#include "figures.h"

#include <cstdio>

namespace linuxxi
{
    namespace
    {
        constexpr uint64_t kKbPerMb = 1024;
        constexpr size_t kDigitsPerGroup = 3;
    }

    std::string Grouped(uint64_t value)
    {
        const std::string digits = std::to_string(value);
        std::string out;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            if (i > 0 && (digits.size() - i) % kDigitsPerGroup == 0) out += ',';
            out += digits[i];
        }
        return out;
    }

    std::string KbText(uint64_t kb)
    {
        if (kb < kKbPerMb) return Grouped(kb) + " KB";
        char text[32];
        std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(kb) / kKbPerMb);
        return text;
    }
}
