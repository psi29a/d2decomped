// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A rand El element (MonStats ElType "rand") takes the kind its swing rolled,
// rand(5) + 1 (FUN_005a4f50 at 0x5a52f1, its table at 0x5a5444: 1 fire,
// 2 lightning, 3 magic, 4 cold with 25 frames when the row has none, 5 poison).

#include <ai.hpp>

#include <cassert>

using namespace d2d::game;

int main() {
    Monster monster;
    monster.stats.elements[0] = { -3, 50, 5, 7, 0, "A1" };
    monster.swing_mode = "A1";
    monster.element_on[0] = true;
    constexpr int kWant[] = { 0, 0, 1, 4, 2, 3 };              // d2d's El types: fire, ltng, cold, pois, mag
    for (int kind = 1; kind <= 5; ++kind) {
        monster.rand_kind[0] = kind;
        const auto element = monster.swing_stats().elements[0];
        assert(element.type == kWant[kind] && element.pct == 100 && element.min == 5 && element.max == 7);
        assert(element.dur == (kind == 4 ? 25 : 0));
    }
    monster.element_on[0] = false;
    assert(monster.swing_stats().elements[0].type == -1);      // not rolled in: left out
    return 0;
}
