// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Bone Prison's twelve pieces (do 62), as tools/emu/bone_prison.py runs
// game.exe's FUN_005c5d00: `--bone-prison` reads its cases (`px py town spot
// nearby`, then twelve `dx dy`: the point in subtiles, 12-bit masks of the spots in
// a town room, free on their subtile, with room within 4, and where each of those
// lands off its spot) and prints each with every piece's `x,y,dir` or
// `-`. No game data.

#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int prison_cases() {
    static constexpr int kSpotX[12] = { -1, 1, 3, 4, 4, 3, -1, 1, -3, -4, -4, -3 };
    static constexpr int kSpotY[12] = { -4, -4, -3, -1, 1, 3, 4, 4, 3, -1, 1, -3 };
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int point_x = 0, point_y = 0;
        if (!(fields >> point_x >> point_y)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        // d2d makes all twelve on the ring whatever the spot (summons.cpp), each facing the caster's way: a caster
        // facing 0 (face(): direction 8 in 64ths).
        std::string out;
        for (int k = 0; k < 12; ++k) out += (k ? " " : "") + std::to_string(point_x + kSpotX[k]) + "," + std::to_string(point_y + kSpotY[k]) + ",8";
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--bone-prison") return prison_cases();
    std::puts("OK: --bone-prison prints d2d's Bone Prison pieces");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_bone_prison: %s\n", error.what());
    return 1;
}
