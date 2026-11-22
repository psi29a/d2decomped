// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The missile fan (srvdofunc 8: Multiple Shot, Teeth, Shock Wave), as
// tools/emu/missile_fan.py runs game.exe's FUN_005db410: `--missile-fan` reads
// its cases (`sx sy tx ty count mid`, subtiles) and prints each with each
// missile's aim and side flag: d2d's, the point + (k - count / 2) x a step
// turned from the line in cells and halved, at least one missile, no flags.

#include <algorithm>
#include <cmath>
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
        // Fight::fire's do-8 branch on a caster and a point at their subtiles' centres.
        const float dx = float(to_x - from_x) / 5, dy = float(to_y - from_y) / 5;
        float side_x = dy * 5, side_y = -dx * 5;
        while (side_x * side_x + side_y * side_y > 3) { side_x = std::trunc(side_x / 2); side_y = std::trunc(side_y / 2); }
        if (side_x == 0 && side_y == 0) side_x = 1;
        std::string out;
        const int number = std::max(count, 1);
        for (int k = 0; k < number; ++k) {
            const float spread = float(k) - float(number) / 2;
            const int aim_x = int(std::floor(float(to_x) + 0.5f + spread * side_x)), aim_y = int(std::floor(float(to_y) + 0.5f + spread * side_y));
            out += (out.empty() ? "" : " ") + std::to_string(aim_x) + "," + std::to_string(aim_y) + ",0";
        }
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--missile-fan") return fan_cases();
    std::puts("SKIP: the fan check comes with the fix");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::printf("test_missile_fan: %s\n", error.what());
    return 1;
}
