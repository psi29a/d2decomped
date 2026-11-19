// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// MonUMod 14 (spcdamage, FUN_005a3b50, hook 0): level = the area's monster
// level (at least 1, 2 with no room), to-hit level + 50 (at most 90), and
// the trap classes' stat pairs. tests/spcdamage_cases.inc is game.exe's own
// answer (tools/emu/spcdamage.py inc); rules::spcdamage must give it.

#include <uniques.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

using namespace d2d::rules;

namespace {
struct Case { int base, area, level, to_hit, count; std::pair<int, int> pairs[5]; };
constexpr Case kCases[] = {
#include "spcdamage_cases.inc"
};
}  // namespace

int main() try {
    for (const auto& one : kCases) {
        const auto got = spcdamage(one.base, one.area == -1000 ? std::nullopt : std::optional<int>(one.area));
        assert(got.level == one.level);
        assert(got.to_hit == one.to_hit);
        assert(int(got.list.size()) == one.count);
        for (int k = 0; k < one.count; ++k) assert(got.list[std::size_t(k)] == one.pairs[k]);
    }
    // A custom Levels.txt row can hold INT_MAX: the +50 comes after the cap, so it does not overflow.
    assert(spcdamage(0x162, std::numeric_limits<int>::max()).to_hit == 90);
    // Only the trap-melee (0x162) is a monster d2d makes: family_mods gives it the mod.
    for (const int base : { 0x146, 0x147, 0x148, 0x149, 0x14a, 0x162 }) assert(!family_mods(base).empty() && family_mods(base)[0] == umod::spcdamage);
    std::puts("test_spcdamage: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_spcdamage: %s\n", error.what());
    return 1;
}
