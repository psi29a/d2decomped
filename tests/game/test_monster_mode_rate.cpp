// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's mode rate (FUN_00623f50, 0x623f50): attack modes at 100 +
// attackrate + its IAS clamped 15..175, the cast mode at 100 + its FCR
// (at most 175, no attackrate), any other non-moving mode at
// other_animrate clamped 15..175. The modes with their own rate (moving ones,
// GH, BL, KB, DT, DD) are mode_rate's, test_mode_rates'; here a WL is held as
// any other mode, so it never divides by 100 + anim_pct at or below -100.

#include <ai.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::game;

int main() {
    Monster monster;
    monster.anim_pct = 200;                          // Fanaticism-like
    assert(anim_rate_pct(monster, "A1") == 75);      // capped at 175
    assert(anim_rate_pct(monster, "S3") == 75);
    assert(anim_rate_pct(monster, "WL") == 75);      // without GameData: as any other mode
    monster.anim_pct = -90;                          // Holy Freeze-like
    assert(anim_rate_pct(monster, "A2") == -85);     // floored at 15
    assert(anim_rate_pct(monster, "NU") == -85);
    monster.anim_pct = 50;
    assert(anim_rate_pct(monster, "SC") == 0);       // no attackrate on a cast
    monster.props.fcr = 1000;
    assert(anim_rate_pct(monster, "SC") <= 75);
    std::printf("monster mode rate: ok\n");
    return 0;
}
