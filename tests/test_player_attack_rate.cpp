// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A player's attack rate (FUN_00623f50 from 0x6241d7): FUN_00621740's 120 v / (120 + v) on stat 93 (IAS) only, stat 68
// attackrate (100 - the weapon's speed + an aura's or skill's SIAS) added as it is, -30 in SQ, clamped 15..175, x the
// AnimData speed / 100. player_attack_rate_cases.inc is game.exe's own +0x4c on 3000 cases (tools/emu/player_attack_rate.py),
// run here through rules::attack_speed with the WSM 0 and the SIAS the stat 68 less 100.

#include <combat.hpp>

#include <cstdio>
#include <iterator>
#include <string_view>

namespace {

struct Case { std::string_view mode; int ias, rate68, speed, rate; };
constexpr Case kCases[] = {
#include "player_attack_rate_cases.inc"
};

}   // namespace

int main() {
    int bad = 0;
    for (const auto& one : kCases) {
        const int rate = d2d::rules::attack_speed(one.speed, one.ias, 0, one.rate68 - 100 - (one.mode == "SQ" ? 30 : 0));
        if (rate != one.rate && ++bad <= 5)
            std::printf("FAIL: %.*s ias %d stat68 %d speed %d: d2d %d, game.exe %d\n", int(one.mode.size()), one.mode.data(), one.ias, one.rate68, one.speed, rate, one.rate);
    }
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }
    std::printf("OK: %zu player attack rates as FUN_00623f50 sets them\n", std::size(kCases));
    return 0;
}
