// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Overlay timing against stock 1.14d's own update, including fractional
// tick boundaries and loop/one-shot endings. No game data needed at runtime.
#include <overlays.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

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
    // Kind 8 (states.md "Rotating overlays"): Might (state 33) on, then
    // Conviction (28) joins hidden; Might's shows 51 ticks (counts to 50,
    // then rotates), Conviction's next; Conviction gone, Might's again.
    std::vector<d2d::rules::AuraOverlay> auras;
    d2d::rules::aura_overlay_made(auras, { .state = 33, .row = 0 });
    d2d::rules::aura_overlay_made(auras, { .state = 28, .row = 0 });
    d2d::rules::aura_overlay_made(auras, { .state = 28, .row = 1 });
    assert(auras.size() == 3 && !auras[0].shown && !auras[1].shown && auras[2].shown && auras[2].state == 33);
    const auto both = [](int) { return true; };
    for (int tick = 0; tick < 50; ++tick) d2d::rules::aura_overlays_tick(auras, both);
    assert(auras[2].shown && auras[2].count == 50 && !auras[0].shown);
    d2d::rules::aura_overlays_tick(auras, both);
    assert(!auras[2].shown && auras[2].count == -1 && auras[0].shown && auras[0].count == 0 && auras[1].shown);   // none above 33: the lowest, 28
    d2d::rules::aura_overlays_tick(auras, both);
    assert(auras[0].count == 1);
    // Conviction off: both removed, each rotating to Might from 0, which the
    // same walk then counts once.
    d2d::rules::aura_overlays_tick(auras, [](int state) { return state != 28; });
    assert(auras.size() == 1 && auras[0].state == 33 && auras[0].shown && auras[0].count == 1);
    // A new one of the shown state takes its count.
    d2d::rules::aura_overlay_made(auras, { .state = 33, .row = 1 });
    assert(auras[0].shown && auras[0].count == 1);
    std::puts("overlay frames and independent alert clocks: ok");
}
