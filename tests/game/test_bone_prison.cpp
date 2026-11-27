// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Bone Prison's twelve pieces (do 62), as tools/emu/bone_prison.py runs
// game.exe's FUN_005c5d00: `--bone-prison` reads its cases (`px py town spot
// nearby`, then twelve `dx dy`: the point in subtiles, 12-bit masks of the spots in
// a town room, free on their subtile, with room within 4, and where each of those
// lands off its spot) and prints each with every piece's `x,y,dir` or
// `-` (rules::bone_prison). Without arguments: the rule on a few points. No
// game data.

#include <monsters.hpp>
#include <pets.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

int prison_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int point_x = 0, point_y = 0, town = 0, spot = 0, nearby = 0;
        std::array<int, 24> offsets{};
        fields >> point_x >> point_y >> town >> spot >> nearby;
        for (int& offset : offsets) fields >> offset;
        if (!fields) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        int index = -1;                                  // a town test a piece, before its spawn
        const auto pieces = d2d::rules::bone_prison(point_x, point_y, [&](int, int) { return (town >> ++index & 1) != 0; },
                                                    [&](int x, int y) -> std::optional<std::pair<int, int>> {
                                                        if (spot >> index & 1) return std::pair{ x, y };
                                                        if (nearby >> index & 1) return std::pair{ x + offsets[std::size_t(2 * index)], y + offsets[std::size_t(2 * index + 1)] };
                                                        return std::nullopt;
                                                    });
        std::string out;
        for (const auto& piece : pieces)
            out += (out.empty() ? "" : " ") + (piece.made ? std::to_string(piece.x) + "," + std::to_string(piece.y) + "," + std::to_string(piece.dir) : std::string("-"));
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--bone-prison") return prison_cases();
    // Twelve round (1000, 1000), each facing it: the first at (999, 996) faces 5, the last at (997, 997) faces 0.
    const auto here = [](int x, int y) -> std::optional<std::pair<int, int>> { return std::pair{ x, y }; };
    const auto every = d2d::rules::bone_prison(1000, 1000, [](int, int) { return false; }, here);
    assert(every[0].made && every[0].x == 999 && every[0].y == 996 && every[0].dir == 5);
    assert(every[11].made && every[11].x == 997 && every[11].y == 997 && every[11].dir == 0 && every[6].dir == 42);
    // None in a town; a spot the spawn refuses gets none and the rest still come.
    for (const auto& piece : d2d::rules::bone_prison(1000, 1000, [](int, int) { return true; }, here)) assert(!piece.made);
    const auto gap = d2d::rules::bone_prison(1000, 1000, [](int, int) { return false; }, [](int x, int y) -> std::optional<std::pair<int, int>> {
        if (x == 1003 && y == 997) return std::nullopt;                          // no room for the third
        if (x == 1004 && y == 999) return std::pair{ 1002, 1000 };              // the fourth lands nearer, facing from there
        return std::pair{ x, y };
    });
    assert(!gap[2].made && gap[1].made && gap[3].made && gap[3].x == 1002 && gap[3].dir == d2d::rules::direction64(1002, 1000, 1000, 1000));
    std::puts("OK: Bone Prison's twelve spots, none in a town or with no room, each facing the point from where it got (FUN_005c5d00)");

    return 0;
} catch (const std::exception& error) {
    std::printf("test_bone_prison: %s\n", error.what());
    return 1;
}
