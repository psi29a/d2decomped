// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A walker's facing: facing_cases.inc is game.exe's own aim (FUN_0064fc60:
// the vector and the 64-step direction it writes through EDX) and turn
// (FUN_006485f0's goal, FUN_00648640 a frame) on random cases
// (tools/emu/facing.py); rules::missile_aim, rules::aim_direction and
// rules::Facing give the same.

#include <missiles.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <utility>

namespace {

struct AimCase {
    std::uint32_t x, y, to_x, to_y;
    int aim_x, aim_y, dir;
};
struct TurnFrame {
    int aim, now, goal;
};
struct TurnCase {
    int now, goal;
    TurnFrame frames[12];
};

#include "facing_cases.inc"

}  // namespace

int main() {
    int bad = 0;
    for (const auto& one : kAimCases) {
        const auto aim = d2d::rules::missile_aim(one.x, one.y, one.to_x, one.to_y);
        const int dir = d2d::rules::aim_direction(one.x, one.y, one.to_x, one.to_y);
        if (aim != std::pair(one.aim_x, one.aim_y) || dir != one.dir) {
            if (++bad < 10) std::fprintf(stderr, "aim %x,%x -> %x,%x: game (%d, %d) %d, d2d (%d, %d) %d\n", one.x, one.y, one.to_x, one.to_y,
                                         one.aim_x, one.aim_y, one.dir, aim.first, aim.second, dir);
        }
    }
    for (const auto& one : kTurnCases) {
        d2d::rules::Facing facing{ std::uint8_t(one.now), std::uint8_t(one.goal),
                                   d2d::rules::kDiffDirections[std::size_t((one.goal - one.now) & 0x3f)] };
        int frame = 0;
        for (const auto& want : one.frames) {
            if (want.aim >= 0) facing.aim(want.aim);
            facing.turn();
            if (facing.now != want.now || facing.goal != want.goal) {
                if (++bad < 10) std::fprintf(stderr, "turn from %d to %d, frame %d: game %d / %d, d2d %d / %d\n", one.now, one.goal, frame, want.now, want.goal, facing.now, facing.goal);
                break;
            }
            ++frame;
        }
    }
    if (bad) { std::fprintf(stderr, "%d cases differ\n", bad); return 1; }
    std::printf("OK: %zu aims and %zu turns as game.exe's\n", std::size(kAimCases), std::size(kTurnCases));
    return 0;
}
