#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace test
{
    struct Case
    {
        const char* name;
        std::function<void()> fn;
    };

    inline std::vector<Case>& Registry()
    {
        static std::vector<Case> cases;
        return cases;
    }

    struct Register
    {
        Register(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
    };

    struct Failure
    {
        std::string message;
    };

    inline std::string Where(const char* file, int line) { return std::string(file) + ":" + std::to_string(line) + ": "; }

    // Text compares as text, even as two const char*.
    template <typename A, typename B>
    bool Same(const A& a, const B& b)
    {
        if constexpr (std::is_convertible_v<A, std::string_view> && std::is_convertible_v<B, std::string_view>)
            return std::string_view(a) == std::string_view(b);
        else
            return a == b;
    }

    template <typename T>
    std::string Show(const T& v)
    {
        if constexpr (std::is_enum_v<T>)
            return std::to_string(static_cast<std::underlying_type_t<T>>(v));
        else if constexpr (std::is_arithmetic_v<T>)
            return std::to_string(v);
        else
            return '"' + std::string(v) + '"';
    }
}

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                    \
    static void name();                                               \
    static const test::Register TEST_CAT(register_, name)(#name, name); \
    static void name()

#define CHECK(cond)                                                                       \
    do                                                                                    \
    {                                                                                     \
        if (!(cond)) throw test::Failure{test::Where(__FILE__, __LINE__) + "CHECK(" #cond ")"}; \
    } while (0)

#define CHECK_EQ(a, b)                                                                                 \
    do                                                                                                 \
    {                                                                                                  \
        const auto va_ = (a);                                                                          \
        const auto vb_ = (b);                                                                          \
        if (!test::Same(va_, vb_))                                                                     \
            throw test::Failure{test::Where(__FILE__, __LINE__) + "CHECK_EQ(" #a ", " #b "): " +       \
                                test::Show(va_) + " != " + test::Show(vb_)};                           \
    } while (0)
