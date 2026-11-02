// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_005a1370's resist mods on a hand-made monster: at most two
// immunities counted as the resists stand, stone skin's defence doubled
// before that gate.
#include <montypes.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::rules;

int main() {
    UMods umods;
    MonType type;
    type.diff[0].res = { 0, 0, 100, 100, 0, 0 };               // already fire and lightning immune
    const auto immune = boss_stats(umods, type, Boss::unique, { 8, 28 }, 0);   // magic resistant, stone skin
    assert(immune.res_add[4] == 0 && immune.res_add[0] == 0);  // two immunities: neither adds
    assert(immune.double_defense);                              // the defence still doubles
    type.diff[0].res = { 0, 0, 70, 0, 70, 0 };
    const auto resistant = boss_stats(umods, type, Boss::unique, { 8 }, 0);
    assert(resistant.res_add[4] == 40 && resistant.res_add[2] == 40 && resistant.res_add[3] == 0);   // cold, fire immune: lightning skipped
    std::puts("test_boss_resists: ok");
}
