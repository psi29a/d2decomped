// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Clay Golem's slow, as tools/emu/clay_slow.py runs game.exe's FUN_005c6760:
// `--clay-slow` reads its cases (`slow type kind`) and prints each with
// `| put velocity rate length state`: d2d's, which has no such slow (nothing
// put).

#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int slow_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long slow = 0, type = 0, kind = 0;
        if (!(fields >> slow >> type >> kind)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        std::printf("%s | 0 0 0 0 0\n", line.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--clay-slow") return slow_cases();
    std::puts("SKIP: the slow check comes with the fix");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::printf("test_clay_slow: %s\n", error.what());
    return 1;
}
