// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A unit's footprint height (rules/unit_size.hpp) against stock 1.14d's
// FUN_006205a0, run with its record lookups by tools/emu/unit_size.py: 400
// cases over every unit type, signed and unsigned bytes, the full dword of
// an object's SizeY and each way a lookup misses. No game data needed at
// runtime. With a cases file argument (unit_size.py's `gen` output) it
// prints its `game` lines instead, to diff.
#include <unit_size.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

// One case line: `seq type value miss`. `value` is the row's height
// field; `miss` nonzero means the monster's or missile's row isn't found.
int answer(const std::string& line) {
    std::istringstream fields(line);
    long long seq = 0, type = 0, value = 0, miss = 0;
    fields >> seq >> type >> value >> miss;
    const auto field = miss == 0 ? std::optional<int>(int(std::uint32_t(value))) : std::nullopt;
    return d2d::rules::unit_size_y(int(type), field);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        for (std::string line; std::getline(input, line);) std::printf("%s = %d\n", line.c_str(), answer(line));
        return 0;
    }
    struct Case { const char* line; int height; };
    const Case cases[] = {
#include "unit_size_cases.inc"
    };
    for (const auto& row : cases) {
        const int height = answer(row.line);
        if (height != row.height) std::fprintf(stderr, "FAIL: %s: d2d %d, game.exe %d\n", row.line, height, row.height);
        assert(height == row.height);
    }
    // A player 2, an item 1, a tile 0, whatever the record.
    using d2d::rules::UnitType, d2d::rules::unit_size_y;
    static_assert(unit_size_y(int(UnitType::kPlayer), std::nullopt) == 2 && unit_size_y(int(UnitType::kItem), 7) == 1);
    static_assert(unit_size_y(int(UnitType::kTile), 3) == 0 && unit_size_y(int(UnitType::kMonster), 0xff) == -1);
    std::puts("unit_size: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_unit_size: %s\n", error.what());
    return 1;
}
