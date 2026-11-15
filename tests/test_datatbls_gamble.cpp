// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Frozen row order from tools/emu/datatbls_gamble.py, FUN_00638ae0.
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

int main() try {
    d2d::rules::Tables tables;
    constexpr std::array<int, 6> levels{5, 5, 1, 5, 1, 99};
    for (std::size_t index = 0; index < levels.size(); ++index) {
        const auto code = std::to_string(index);
        tables.gamble.push_back(code);
        tables.item_base[code].level = levels[index];
    }
    const auto sorted = d2d::rules::gamble_by_level(tables);
    assert((sorted == std::vector<std::string>{"2", "4", "3", "1", "0", "5"}));
    std::puts("test_datatbls_gamble: ok");
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_datatbls_gamble: %s\n", error.what());
    return 1;
}
