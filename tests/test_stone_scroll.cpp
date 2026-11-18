// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The deciphered scroll's symbols (FUN_0049fba0(1)): the 50 ms step clock,
// each numeral its place in the order at its stone's spot, its delay by place, the fade's
// draw modes, the glow's frames and the sound on its first step.

#include <stone_scroll.hpp>

#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using namespace d2d::rules;
    StoneScrollClock clock;
    clock.draw(1000);                                   // the first draw only starts it
    assert(clock.steps == 0 && clock.since == 1000);
    clock.draw(1050);                                   // not more than 0x32 on
    assert(clock.steps == 0);
    clock.draw(1051);
    assert(clock.steps == 1 && clock.since == 1051);
    clock.draw(5000);                                   // one step a draw, however long
    assert(clock.steps == 2);

    const std::array<int, 5> order = { 3, 0, 4, 1, 2 };
    assert(!stone_symbol(order, 0, 0));
    const auto first = stone_symbol(order, 0, 1);
    assert(first && first->place == 0 && first->stone == 3 && first->x == 47 && first->bottom == 222 && first->mode == 0 && first->sound);
    assert(first->glow_x == 190 && first->glow_bottom == 238 && first->glow_frame == 1);
    assert(stone_symbol(order, 0, 5)->mode == 1 && !stone_symbol(order, 0, 5)->sound);
    assert(stone_symbol(order, 0, 10)->mode == 2 && stone_symbol(order, 0, 15)->mode == 5);
    assert(stone_symbol(order, 0, 20)->glow_frame == 20 && stone_symbol(order, 0, 21)->glow_frame == 0);   // frame 20, then 0 (0x14 < age)
    assert(!stone_symbol(order, 1, 12) && stone_symbol(order, 1, 13)->stone == 0 && stone_symbol(order, 1, 13)->place == 1);   // 12 steps behind the one before
    assert(!stone_symbol(order, 4, 48) && stone_symbol(order, 4, 49)->x == 148);
    assert(!stone_symbol({ 5, 0, 0, 0, 0 }, 0, 1));
    std::printf("OK: the deciphered scroll's symbols\n");
    return 0;
}
