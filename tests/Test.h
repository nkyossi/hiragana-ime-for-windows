// A tiny test harness for the core tests.

#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "Text.h"

namespace test {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline std::string show(const std::string& s) { return s; }
inline std::string show(const hiragana::ustring& s) { return hiragana::to_utf8(s); }
inline std::string show(const char32_t* s) { return hiragana::to_utf8(s); }
inline std::string show(const char* s) { return s; }
template <class T>
std::string show(const T& v) {
    return std::to_string(v);
}

}  // namespace test

#define TEST(name)                                                    \
    static void name();                                               \
    static ::test::Register name##_registration(#name, name);         \
    static void name()

#define EXPECT_EQ(actual, expected)                                                                      \
    do {                                                                                                 \
        auto&& a_ = (actual);                                                                            \
        auto&& e_ = (expected);                                                                          \
        if (!(a_ == e_)) {                                                                               \
            ++::test::failures();                                                                        \
            std::cerr << __FILE__ << ":" << __LINE__ << ": EXPECT_EQ(" #actual ", " #expected ")\n"      \
                      << "  actual:   " << ::test::show(a_) << "\n  expected: " << ::test::show(e_) << "\n"; \
        }                                                                                                \
    } while (0)

#define EXPECT_TRUE(cond)                                                              \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++::test::failures();                                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": EXPECT_TRUE(" #cond ")\n"; \
        }                                                                              \
    } while (0)
