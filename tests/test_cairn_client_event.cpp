// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// S->C 0x89 event 1 at LAB_0046aee0: the player/room/StoneAlpha gates,
// FUN_004cdb40's missile 0x120 request at the stone, its lifetime, and the
// FUN_0046ae50 dip arguments frozen from tools/emu/cairn_event.py.

#include <cairn_effect.hpp>

#include <cassert>
#include <cstdio>
#include <optional>
#include <utility>

namespace {

struct Case {
    int player, room, stone, stone_x, stone_y, param1, range;
    int made, missile, made_x, made_y, fade, hold, back, frames;
};

constexpr Case kCases[] = {
#include "cairn_client_event_cases.inc"
};

}  // namespace

int main() {
    for (const auto& test : kCases) {
        const auto stone = test.stone ? std::optional{ std::pair{ test.stone_x, test.stone_y } }
                                      : std::optional<std::pair<int, int>>{};
        const auto got = d2d::rules::cairn_client_event(test.player != 0, test.room != 0, stone, test.param1, test.range);
        assert(bool(got) == (test.made != 0));
        if (!got) continue;
        assert(got->missile == test.missile);
        assert(got->subtile_x == test.made_x && got->subtile_y == test.made_y);
        assert(got->dip.fade_in == test.fade && got->dip.hold == test.hold && got->dip.back == test.back);
        assert(got->frames == test.frames);
    }
    std::printf("OK: %zu Cairn client-event cases match game.exe\n", std::size(kCases));
}
