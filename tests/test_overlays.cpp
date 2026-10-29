// SPDX-License-Identifier: GPL-3.0-or-later
// Overlay timing against stock 1.14d's own update, including fractional
// tick boundaries and loop/one-shot endings. No game data needed at runtime.
#include <overlays.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    struct Case { std::uint32_t elapsed; int rate, frames, kind, frame; };
    const Case cases[] = {
#include "overlay_cases.inc"
    };
    for (const auto& row : cases)
        assert(d2d::rules::overlay_frame(row.elapsed, row.rate, row.frames, row.kind == 4) == row.frame);
    // Zero frames must never reach the renderer's modulo. Long sessions
    // must not wrap the 32-bit product (old elapsed_ms * AnimRate).
    assert(d2d::rules::overlay_frame(100, 9, 0) == -1);
    assert(d2d::rules::overlay_frame(4'000'000'000u, 16, 16) == 0);
    assert(d2d::rules::overlay_frame(4'000'000'000u, 16, 16, true) == -1);

    d2d::rules::OverlayClock first, second;
    first.set(true, 1000);
    second.set(true, 1080);
    first.set(true, 1080);
    assert(first.start == 1000 && second.start == 1080);
    assert(d2d::rules::overlay_frame(1080 - first.start, 9, 16) == 1);
    assert(d2d::rules::overlay_frame(1080 - second.start, 9, 16) == 0);
    first.set(false, 1100);
    first.set(true, 1200);
    assert(first.start == 1200);
    first = {};                         // new game / another level
    first.set(true, 1500);
    assert(first.start == 1500);
    first.set(false, 0xfffffff0u);
    first.set(true, 0xfffffff0u);
    assert(d2d::rules::overlay_frame(std::uint32_t(64u - first.start), 9, 16) == 1);
    std::puts("overlay frames and independent alert clocks: ok");
}
