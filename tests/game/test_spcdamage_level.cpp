// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// MonUMod 14 (spcdamage, FUN_005a3b50 hook 0): the trap-melee (BaseId 0x162,
// FUN_005b21b0) is made at the area's monster level on every difficulty, not
// its MonStats level, with the life rolled at the MonStats level (FUN_00573cb0).
// Synthetic game data; no MPQs.

#include <ai.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData data;
    d2d::rules::MonType trap, plain;
    trap.id = "trap-melee"; trap.base = 0x162; trap.level = { 4, 4, 4 };
    plain.id = "plain"; plain.base = 0x10; plain.level = { 4, 4, 4 };
    data.monsters.types = { trap, plain };
    data.mon_npc.resize(2);
    data.monsters.lvl.resize(130);
    for (std::size_t row = 0; row < data.monsters.lvl.size(); ++row) data.monsters.lvl[row].to_hit = { int(row) * 10, int(row) * 10, int(row) * 10 };
    data.area_level.assign(40, { 1, 1, 1, 1 });
    data.area_level[12] = { 9, 30, 50, 9 };                // Levels.txt MonLvl1Ex..3Ex
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        d2d::rules::Rng rng{ 5 };
        const int area = data.area_level[12][std::size_t(difficulty)];
        const auto spc = make_monster(data, 0, 10.f, 10.f, rng, difficulty, true, 1, 12);
        assert(spc.stats.level == area);                    // Normal too: its MonStats level is 4
        const auto other = make_monster(data, 1, 10.f, 10.f, rng, difficulty, true, 1, 12);
        assert(other.stats.level == (difficulty == 0 ? 4 : area));   // a plain row: start_level's
    }
    d2d::rules::Rng rng{ 5 };
    assert(make_monster(data, 0, 10.f, 10.f, rng, 0).stats.level == 4);   // no area known: the MonStats level, as before
    std::puts("test_spcdamage_level: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_spcdamage_level: %s\n", error.what());
    return 1;
}
