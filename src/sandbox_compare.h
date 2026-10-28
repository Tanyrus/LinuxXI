#pragma once

#include <climits>
#include <cstddef>

namespace linuxxi
{
    constexpr int kCompareError = INT_MAX; // the CRT's _NLSCMPERROR, for a missing string

    // The CRT's _wcsnicmp as the C locale has it (count SIZE_MAX for _wcsicmp): A to Z fold to a to z, nothing else
    // does, and no locale is asked, so it never reaches LCMapString and the registry.
    int CompareNoCase(const wchar_t* a, const wchar_t* b, size_t count);
}
