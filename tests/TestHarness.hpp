// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Minimal test registry: TEST(name) { ... CHECK(cond); CHECK_NEAR(a, b, tol); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace titv::test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({ name, std::move(fn) }); }
};

inline void fail(const char* file, int line, const char* expr)
{
    std::printf("    FAILED %s:%d: %s\n", file, line, expr);
    ++failures();
}

} // namespace titv::test

#define TITV_CONCAT_(a, b) a##b
#define TITV_CONCAT(a, b) TITV_CONCAT_(a, b)

#define TEST(name)                                                                      \
    static void TITV_CONCAT(test_, name)();                                             \
    static const titv::test::Registrar TITV_CONCAT(registrar_, name) { #name,           \
                                                                       TITV_CONCAT(test_, name) }; \
    static void TITV_CONCAT(test_, name)()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond))                                                                    \
            titv::test::fail(__FILE__, __LINE__, #cond);                                \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                           \
    do {                                                                                \
        const double titv_a_ = (a), titv_b_ = (b);                                      \
        if (!(std::fabs(titv_a_ - titv_b_) <= (tol))) {                                 \
            std::printf("    %s = %g, %s = %g\n", #a, titv_a_, #b, titv_b_);            \
            titv::test::fail(__FILE__, __LINE__, #a " ~= " #b);                         \
        }                                                                               \
    } while (0)
