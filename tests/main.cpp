// SPDX-License-Identifier: GPL-3.0-or-later
#include "TestHarness.hpp"

#include <cstring>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& c : titv::test::registry()) {
        if (filter != nullptr && std::strstr(c.name, filter) == nullptr)
            continue;
        const int before = titv::test::failures();
        c.fn();
        std::printf("%s %s\n", titv::test::failures() == before ? "  ok  " : " FAIL ", c.name);
        ++run;
    }
    std::printf("\n%d tests, %d failed checks\n", run, titv::test::failures());
    return titv::test::failures() == 0 ? 0 : 1;
}
