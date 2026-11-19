// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Life Tap's heal, as tools/emu/life_tap.py runs game.exe's FUN_005c61e0:
// `--life-tap` reads its cases (`striker life max damage pct`, 256ths) and
// prints each with `| life`: rules::life_tap_life on the striker's life.
// Without arguments: life_tap_life on a few typed-in numbers.

#include <combat.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int life_tap_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int striker = 0, damage = 0, pct = 0;
        std::int64_t life = 0, max_life = 0;
        if (!(fields >> striker >> life >> max_life >> damage >> pct)) { std::fprintf(stderr, "malformed case: %s\n", line.c_str()); return 1; }
        std::printf("%s | %lld\n", line.c_str(), static_cast<long long>(d2d::rules::life_tap_life(life, max_life, damage, pct)));
    }
    return 0;
}

int heal_checks() {
    using d2d::rules::life_tap_life;
    assert(life_tap_life(100 * 256, 400 * 256, 50 * 256, 10) == 105 * 256);
    assert(life_tap_life(399 * 256, 400 * 256, 50 * 256, 10) == 400 * 256);   // held at the max
    std::puts("OK: Life Tap's heal");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--life-tap") return life_tap_cases();
    return heal_checks();
} catch (const std::exception& error) {
    std::printf("test_life_tap: %s\n", error.what());
    return 1;
}
