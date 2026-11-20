// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// test-includes: apps/d2d
// The hand cursor's animation (CursorAnim: FUN_00468310, FUN_004681c0,
// FUN_004682c0, FUN_00467f20, FUN_00467fa0, FUN_00468840): protate while
// idle, ppress looping while pressed and protate again on release, ohand
// then orotate after 5 s with no input, ohand backward on a move. A tick
// is a draw at least 17 ms after the last tick.
#include <cursor_anim.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using d2d::client::CursorAnim;

int main() try {
    CursorAnim hand;
    std::uint32_t now = 1000;
    hand.input_ms = now;
    assert(hand.state == CursorAnim::kIdle && hand.row == CursorAnim::kProtate);

    // Pressed: ppress from frame 0, 0x40 a tick, looping over its 8 frames.
    hand.press(now);
    assert(hand.state == CursorAnim::kPressed && hand.row == CursorAnim::kPpress && hand.frame() == 0);
    for (int tick = 1; tick <= 32; ++tick) { now += 40; hand.tick(now); }
    assert(hand.row == CursorAnim::kPpress && hand.pos == 0);   // 32 x 0x40 = 8 frames: round once
    now += 40; hand.tick(now);
    assert(hand.pos == 0x40);
    hand.tick(now + 10);                                        // under 17 ms: no step
    assert(hand.pos == 0x40);
    hand.release(now);
    assert(hand.state == CursorAnim::kIdle && hand.row == CursorAnim::kProtate && hand.pos == 0);

    // Idle steps only by 0x20, on a quarter of the seed's draws.
    int steps = 0;
    for (int tick = 0; tick < 400; ++tick) {
        now += 40;
        hand.input_ms = now;
        const int before = hand.pos;
        hand.tick(now);
        if (hand.pos != before) { assert((hand.pos - before + 0x800) % 0x800 == 0x20); ++steps; }
    }
    assert(steps > 60 && steps < 140);

    // 5 s with no input: ohand forward, then orotate looping.
    hand.input_ms = now;
    now += 5001; hand.tick(now);
    assert(hand.state == CursorAnim::kOpening && hand.row == CursorAnim::kOhand && hand.pos == 0);
    for (int tick = 0; tick < 32; ++tick) { now += 40; hand.tick(now); }
    assert(hand.state == CursorAnim::kOpen && hand.row == CursorAnim::kOrotate && hand.pos == 0);
    now += 40; hand.tick(now);
    assert(hand.pos == 0x20);

    // A move: ohand from its last frame backward, then idle protate.
    hand.move(now);
    assert(hand.state == CursorAnim::kClosing && hand.row == CursorAnim::kOhand && hand.frame() == 7);
    for (int tick = 0; tick < 28; ++tick) { now += 40; hand.tick(now); }
    assert(hand.state == CursorAnim::kClosing && hand.pos == 0);
    now += 40; hand.tick(now);
    assert(hand.state == CursorAnim::kIdle && hand.row == CursorAnim::kProtate && hand.pos == 0);
    std::puts("OK: the hand cursor's animation (FUN_00468310)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
