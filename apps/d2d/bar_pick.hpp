// SPDX-License-Identifier: GPL-3.0-or-later
// Which monster the one life bar names. The hovered monster wins, then the
// one being attacked, then the nearest hostile whose tile is lit. Callers
// pass only monsters the frame is drawing, so one outside that cull is absent.
#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace d2d::client {

struct BarFoe {
    int id = -1;
    float x = 0.f;
    float y = 0.f;
    bool hostile = false;
    int light = 0;
    bool on_screen = true;
};

// `screen_x` / `screen_y` are pixels from the view centre (iso: (dx-dy)*80, (dx+dy)*40).
// Inside the 800x600 playfield, short of the right edge and the panel.
[[nodiscard]] inline bool bar_on_screen(float screen_x, float screen_y) {
    return std::abs(screen_x) <= 320.f && screen_y >= -220.f && screen_y <= 160.f;
}

[[nodiscard]] inline int bar_monster(int hovered, int attack, std::span<const BarFoe> drawn, float from_x, float from_y) {
    const auto shown = [&](int foe_id) { return foe_id >= 0 && std::ranges::find(drawn, foe_id, &BarFoe::id) != drawn.end(); };
    if (shown(hovered)) return hovered;
    if (shown(attack)) return attack;
    int best = -1;
    float best_distance = 0.f;
    for (const BarFoe& foe : drawn) {
        if (!foe.hostile || foe.light <= 0 || !foe.on_screen) continue;
        const float distance = std::hypot(foe.x - from_x, foe.y - from_y);
        if (best < 0 || distance < best_distance) {
            best = foe.id;
            best_distance = distance;
        }
    }
    return best;
}

}  // namespace d2d::client
