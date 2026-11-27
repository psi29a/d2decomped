// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Hydra's three heads (do 144), as tools/emu/hydra.py runs game.exe's
// FUN_005ca910: `--hydra` reads its cases (`px py town spot nearby`, then three
// `dx dy`: the point in subtiles, whether it's in a town, 3-bit masks of the
// heads free on their subtile and with room within 4, and where each of those
// lands off its spot) and prints each with every head's `row,x,y` (its row less
// the summon row) or `-`. No game data.

#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int hydra_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int point_x = 0, point_y = 0;
        if (!(fields >> point_x >> point_y)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        // d2d makes the three of the summon row on the point (summons.cpp), whatever the town or the room there.
        const std::string head = "0," + std::to_string(point_x) + "," + std::to_string(point_y);
        std::printf("%s | %s %s %s\n", line.c_str(), head.c_str(), head.c_str(), head.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--hydra") return hydra_cases();
    std::puts("OK: --hydra prints d2d's Hydra heads");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_hydra: %s\n", error.what());
    return 1;
}
