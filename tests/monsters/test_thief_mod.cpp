// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// MonUMod 24 (thief) is never put on a monster in game.exe 1.14d: its row
// is disabled (FUN_005a03e0 refuses it, and its upick weights are 0), and
// FUN_005a49b0 skips it in a superunique's Mod1..3 (0x5a4b07: Lord De Seis
// has 5, 30, 24). So its hit hook FUN_005a30e0 never runs.

#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::rules;

int main() try {
    UMods umods;
    UMod thief;
    thief.id = 24;                                       // MonUMod.txt: enabled 0, no cpick / upick
    umods.rows.push_back(thief);
    UMod strong;
    strong.id = umod::strong; strong.enabled = true; strong.upick = { 1, 1, 1 };
    umods.rows.push_back(strong);
    const MonType type;
    assert(!unique_detail::can_take(thief, type));
    Rng seed{ 1 };
    const auto de_seis = superunique_mods(umods, type, { umod::strong, umod::aura, 24 }, 0, seed);
    assert((de_seis == std::vector<int>{ umod::strong, umod::aura }));
    for (std::uint32_t start = 0; start < 200; ++start) {       // its weight never wins a pick
        Rng rolls{ start };
        const auto boss = roll_boss(umods, type, 2, false, rolls);
        for (const int mod : boss.mods) assert(mod != 24);
    }
    std::puts("test_thief_mod: ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_thief_mod: %s\n", error.what());
    return 1;
}
