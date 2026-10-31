// SPDX-License-Identifier: GPL-3.0-or-later
// draw_mode against game.exe's blend for each COF draw effect: the
// composite draw (FUN_00470ec0) hands a transparent layer's effect to the
// renderer as its draw mode, and FUN_006c8250 picks the table. Cases from
// tools/emu/cof_effects.py: {effect, kind, sprite percent}, kind 0 opaque,
// 1 additive, 2 multiply, 3 alpha, 4 max-component.
#include "world_view.hpp"

#include <cassert>

int main() {
    struct Case { int effect, kind, percent; };
    constexpr Case cases[]{
#include "cof_effect_cases.inc"
    };
    for (const auto& sample : cases) {
        const int trans = d2d::client::draw_mode(sample.effect);
        switch (sample.kind) {
        case 0: assert(trans == 0); break;
        case 1: assert(trans == 1); break;
        case 2: assert(trans == 2); break;
        case 3: assert(trans == 2 + sample.percent / 25); break;   // blit_dcc_frame: trans 3..5 draw a quarter, half, three quarters of the sprite
        default: assert(trans == 0); break;                        // ponytail: max-component (effect 6) draws opaque, see draw_mode
        }
    }
    return 0;
}
