#include "sandbox_compare.h"

namespace linuxxi
{
    namespace
    {
        wchar_t Folded(wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c; }
    }

    int CompareNoCase(const wchar_t* a, const wchar_t* b, size_t count)
    {
        if (count == 0) return 0;
        if (a == nullptr || b == nullptr) return kCompareError;
        wchar_t left = 0, right = 0;
        do
        {
            left  = Folded(*a++);
            right = Folded(*b++);
        } while (--count != 0 && left != 0 && left == right);
        return left - right;
    }
}
