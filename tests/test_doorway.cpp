// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A door's doorway test (FUN_00581d40 -> FUN_0064d800, mask 0x8180): a unit
// counts by the own bits its collision pattern stamps (FUN_0064ea90), against
// stock game.exe's answers (tools/emu/door_occupied.py, frozen in
// doorway_cases.inc). `--doorway FILE` diffs a whole run of the oracle.

#include <shrines.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<int, 8> kCases[] = {
#include "doorway_cases.inc"
};

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::array<int, 8>> cases(std::begin(kCases), std::end(kCases));
    if (argc == 3 && std::string_view(argv[1]) == "--doorway") {
        cases.clear();
        std::ifstream file(argv[2]);
        for (std::array<int, 9> line{}; file >> line[0] >> line[1] >> line[2] >> line[3] >> line[4] >> line[5] >> line[6] >> line[7] >> line[8];)
            cases.push_back({ line[0], line[2], line[3], line[4], line[5], line[6], line[7], line[8] });
    }
    int bad = 0;
    for (const auto& [pattern, unit_x, unit_y, door_x, door_y, size_x, size_y, occupied] : cases)
        if (d2d::rules::in_doorway(pattern, unit_x, unit_y, door_x, door_y, size_x, size_y) != (occupied != 0) && bad++ < 5)
            std::fprintf(stderr, "pattern %d unit %d,%d door %d,%d %dx%d: game.exe %d\n", pattern, unit_x, unit_y, door_x, door_y, size_x, size_y, occupied);
    std::printf("%zu cases, %d differ\n", cases.size(), bad);
    return bad == 0 && !cases.empty() ? 0 : 1;
}
