// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The missile fan (srvdofunc 8: Multiple Shot, Teeth, Shock Wave), as
// tools/emu/missile_fan.py runs game.exe's FUN_005db410: `--missile-fan` reads
// its cases (`sx sy tx ty count mid`, subtiles) and prints each with each
// missile's aim and side flag: rules::missile_fan.
// Without arguments: the rule on a few lines (no game data).

#include <missiles.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int fan_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int from_x = 0, from_y = 0, to_x = 0, to_y = 0, count = 0, mid = 0;
        if (!(fields >> from_x >> from_y >> to_x >> to_y >> count >> mid)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        std::string out;
        for (const auto& shot : d2d::rules::missile_fan(from_x, from_y, to_x, to_y, count, mid))
            out += (out.empty() ? "" : " ") + std::to_string(shot.x) + "," + std::to_string(shot.y) + "," + (shot.side ? "1" : "0");
        if (out.empty()) out = "-";
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--missile-fan") return fan_cases();
    using d2d::rules::missile_fan;
    // At the point itself every one aims at it; a line of one subtile is scaled up to 8 first (FUN_0056d370) and
    // turned to a step of 1; Multiple Shot's calc3 2 of 5: one side, two middle, two side.
    assert(missile_fan(1000, 1000, 1000, 1000, 3, 0).size() == 3 && missile_fan(1000, 1000, 1000, 1000, 3, 0)[2].x == 1000);
    const auto five = missile_fan(1000, 1000, 1000, 1001, 5, 2);
    assert(five.size() == 5 && five[0].x == 998 && five[4].x == 1002 && five[0].y == 1001);
    assert(five[0].side && !five[1].side && !five[2].side && five[3].side && five[4].side);
    assert(missile_fan(1000, 1000, 1002, 1001, 0, 0).empty() && missile_fan(1000, 1000, 1002, 1001, 2, 5).size() == 5);
    std::puts("OK: the fan's aims and side flags as FUN_005db410 makes them");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_missile_fan: %s\n", error.what());
    return 1;
}
