#include "sandbox_compare.h"
#include "test.h"

#include <clocale>
#include <cstdint>
#include <string>

using namespace linuxxi;

namespace
{
    constexpr size_t kWhole = SIZE_MAX;
}

TEST(paths_differing_only_in_ascii_case_are_equal)
{
    CHECK(CompareNoCase(L"Software\\PlayOnlineUS\\SquareEnix", L"SOFTWARE\\playonlineus\\squareenix", kWhole) == 0);
}

// The CRT folds to lower case and returns the first difference after folding: '_' sits between 'Z' and 'a'.
TEST(the_result_is_the_first_difference_after_folding_to_lower_case)
{
    CHECK(CompareNoCase(L"Axes\\", L"AXEZ", kWhole) == L's' - L'z');
    CHECK(CompareNoCase(L"_", L"A", kWhole) == L'_' - L'a');
    CHECK(CompareNoCase(L"ab", L"abc", kWhole) == -static_cast<int>(L'c'));
}

TEST(only_the_count_given_is_compared)
{
    CHECK(CompareNoCase(L"Axes\\Actuators", L"AXES\\Buttons", 5) == 0);
    CHECK(CompareNoCase(L"Axes\\Actuators", L"AXES\\Buttons", 6) == L'a' - L'b');
    CHECK(CompareNoCase(L"a", L"b", 0) == 0);
}

// What sandbox compares must not follow an addon's setlocale: in the C locale only A to Z fold.
TEST(letters_past_ascii_never_fold_whatever_the_locale)
{
    const std::string before = std::setlocale(LC_ALL, nullptr);
    CHECK(std::setlocale(LC_ALL, "C.UTF-8") != nullptr);
    CHECK(CompareNoCase(L"Été", L"éTÉ", kWhole) == 0xC9 - 0xE9);
    std::setlocale(LC_ALL, before.c_str());
}

TEST(a_missing_string_is_the_crts_error_value)
{
    CHECK(CompareNoCase(nullptr, L"a", kWhole) == kCompareError);
    CHECK(CompareNoCase(L"a", nullptr, kWhole) == kCompareError);
}

TEST(a_string_ends_the_compare_at_its_terminator)
{
    constexpr wchar_t a[] = L"ab\0x", b[] = L"AB\0y";
    CHECK(CompareNoCase(a, b, kWhole) == 0);
    CHECK(CompareNoCase(L"ab", L"abc", 2) == 0);
}
