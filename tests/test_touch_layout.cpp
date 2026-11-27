// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/input.cpp
#include "input/touch/input.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client::touch;
    for (const float width : { 800.f, 1066.f, 1400.f }) {
        viewport_width = width;
        safe_inset = { 24, 8, 36, 18 };
        const auto layout = layout_boxes();
        assert(layout.attack.x + layout.attack.width <= width - safe_inset.right);
        assert(layout.menu.x >= safe_inset.left && layout.done.x + layout.done.width <= width - safe_inset.right);
        assert(layout.life_orb.y + layout.life_orb.height <= 600.f - safe_inset.bottom);
        assert(layout.mana_orb.x + layout.mana_orb.width < layout.healing.x);
        assert(std::abs(to_legacy_x(to_viewport_x(512.f)) - 512.f) < .01f);
        Input input;
        Target target{ 54, -1, 1, 1, false, true };
        const float attack_x = layout.attack.x + layout.attack.width / 2;
        const float attack_y = layout.attack.y + layout.attack.height / 2;
        input.event(7, Phase::Down, attack_x, attack_y, target);
        input.event(7, Phase::Up, attack_x, attack_y);
        assert(input.casts.size() == 1 && input.casts[0].skill == 54);
        StaminaPress press;
        const auto box = stamina_bar_box(layout);
        assert(box.y + box.height < layout.quest_track.y);
        assert(!press.event(11, Phase::Down, box.x + 2, box.y + 2, box));
        assert(!press.event(12, Phase::Up, box.x + 2, box.y + 2, box));
        assert(press.event(11, Phase::Up, box.x + 2, box.y + 2, box));
        press.event(11, Phase::Down, box.x + 2, box.y + 2, box);
        press.event(11, Phase::Move, 799, 1, box);
        assert(!press.event(11, Phase::Up, box.x + 2, box.y + 2, box));
        press.event(11, Phase::Down, box.x + 2, box.y + 2, box);
        assert(!press.event(11, Phase::Cancel, box.x + 2, box.y + 2, box) && !press.owns(11));
    }
    viewport_width = 800.f; safe_inset = {};
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
