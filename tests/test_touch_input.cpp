// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Actual touch adapter: independent fingers, latched page/skill, safe cancellation.
#include "touch_input.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

/**
 * Exercise independent touch ownership, page latching, safe cancellation and attack press/hold
 * edges, including the single-pointer owner.
 */
int main() try {
    using namespace d2d::client::touch;
    Input input;
    const Target fire{ 54, 7, 5, 6, false, true };
    input.event(1, Phase::Down, 126, 462);
    input.event(1, Phase::Move, 191, 462);
    assert(input.move_x == 1 && input.move_y == 0);
    input.event(2, Phase::Down, 598, 530, fire);
    input.event(2, Phase::Move, 630, 500);
    assert(input.aiming && input.dragged && input.casts.empty());
    input.event(3, Phase::Down, 736, 440);
    assert(input.alternate && input.move_x == 1 && input.preview.skill == 54);
    input.event(2, Phase::Cancel, 610, 450);
    assert(!input.aiming && input.casts.empty() && input.move_x == 1);
    input.event(2, Phase::Down, 598, 530, fire);
    input.event(2, Phase::Up, 380, 170);
    assert(input.casts.empty() && input.move_x == 1); // visible cancel region
    input.event(2, Phase::Down, 598, 530, fire);
    input.event(1, Phase::Up, 191, 462);
    assert(input.aiming && input.casts.empty() && input.move_x == 0);
    input.event(2, Phase::Up, 598, 530);
    assert(input.casts.size() == 1 && input.casts[0].skill == 54 && input.casts[0].unit == 7);
    input.frame();
    input.event(1, Phase::Down, 126, 462);
    input.event(1, Phase::Move, 171, 462);
    assert(input.move_held());
    input.release_finger(1);
    assert(!input.move_held() && input.move_x == 0 && input.move_y == 0);
    input.event(1, Phase::Down, 126, 462);
    input.event(1, Phase::Move, 171, 462);
    input.event(2, Phase::Down, 598, 530, fire, true);
    assert(input.casts.empty()); // even self casts are cancellable until lift
    input.event(2, Phase::Cancel, 598, 530);
    assert(input.casts.empty() && input.move_x > 0);
    input.event(2, Phase::Down, 598, 530, fire);
    input.clear(); // a panel/focus change invalidates all pending gestures
    input.event(2, Phase::Up, 598, 530);
    assert(input.casts.empty() && input.move_x == 0 && !input.aiming);
    input.event(3, Phase::Down, 736, 536);
    assert(input.attack_press && input.attack_down);
    input.frame();
    assert(!input.attack_press && input.attack_down);
    input.event(3, Phase::Up, 736, 536);
    assert(!input.attack_down);
    // Single-pointer adapters have a separate valid owner, not the no-finger sentinel.
    input.clear();
    input.event(-2, Phase::Down, 736, 536);
    assert(input.attack_down);
    input.event(-2, Phase::Up, 736, 536);
    assert(!input.attack_down);
    assert(!chrome_hit(400, 300));
    assert(!chrome_hit(200, 200));
    assert(chrome_hit(391, 455, true)); // kInteract only blocks field taps when visible
    // layout_boxes positions depend on safe_inset and kUiScale only, not on pad_field().
    safe_inset = {};
    const auto field = layout_boxes();
    assert(std::abs(field.skills_bar.y - field.menu.y) < 1.f);
    assert(field.skills_bar.x >= field.menu.x + field.menu.width + ui_scale(kChipGap) - 1.f);
    assert(std::abs(field.quest.y - field.bind.y) < 1.f);
    assert(field.quest.y >= field.menu.y + field.menu.height + ui_scale(kChipGap) - 1.f);
    assert(std::abs(field.bind.x - field.skills_bar.x) < 1.f);
    assert(field.menu.y < 12.f);
    assert(field.move.contains(field.move_visual.x + field.move_visual.width * .5f, field.move_visual.y + field.move_visual.height * .5f));
    assert(field.menu.x < 20.f && field.done.x + field.done.width > 740.f);
    assert(field.attack.x + field.attack.width > 790.f && field.attack.y + field.attack.height > 590.f);
    const auto centre = [](Box box) { return std::pair{ box.x + box.width * .5f, box.y + box.height * .5f }; };
    const auto gap = [&](Box left, Box right) {
        const auto [left_x, left_y] = centre(left);
        const auto [right_x, right_y] = centre(right);
        return std::hypot(left_x - right_x, left_y - right_y) - left.width * .5f - right.width * .5f;
    };
    assert(gap(field.attack, field.page) >= 8.f);
    float skill_top = field.skills[0].y;
    for (const auto& skill : field.skills) {
        skill_top = std::min(skill_top, skill.y);
        assert(gap(field.attack, skill) >= 8.f);
        assert(gap(field.page, skill) >= 8.f);
        for (const auto& belt : field.belts) assert(gap(skill, belt) >= 8.f);
    }
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) assert(gap(field.skills[std::size_t(i)], field.skills[std::size_t(j)]) >= 8.f);
    for (const auto& belt : field.belts) {
        assert(belt.x >= 0.f && belt.y >= 0.f);
        assert(belt.x + belt.width <= 800.f && belt.y + belt.height <= 600.f);
        assert(belt.y + 1.f >= field.map.y + field.map.height);
        assert(belt.x + belt.width > 400.f);
    }
    (void)skill_top;
    assert(std::abs(field.belts[0].x - field.belts[2].x) < 1.f);
    assert(std::abs(field.belts[1].x - field.belts[3].x) < 1.f);
    assert(field.belts[1].x >= field.belts[0].x + field.belts[0].width - 1.f);
    assert(std::abs(field.belts[0].y - field.belts[1].y) < 1.f);
    const float chip_h = ui_scale(kChipSize);
    const float gap_bars = bar_gap();
    assert(gap_bars >= 0.25f * chip_h && gap_bars <= 0.5f * chip_h);
    assert(field.quest_track.y + 1.f >= field.bind.y + field.bind.height + gap_bars + kBarBlockH);
    const float grid_w = 2.f * field.belts[0].width + ui_scale(kBeltGap);
    assert(std::abs(field.belts[1].x - (field.map.x + field.map.width - grid_w)) < 1.f);
    const auto overlap = [](Box left, Box right) {
        return left.x < right.x + right.width && right.x < left.x + left.width && left.y < right.y + right.height && right.y < left.y + left.height;
    };
    assert(!overlap(field.map, field.belts[1]));
    assert(!overlap(field.map, field.belts[3]));
    assert(chip_hit(field.menu.x + 2.f, field.menu.y + 2.f));
    assert(chip_hit(field.bind.x + 2.f, field.bind.y + 2.f));
    assert(!chip_hit(field.move.x + 8.f, field.move.y + 8.f));
    assert(!chrome_hit(400, 80, false, false));
    const auto icon_at = centred_blit_origin({ 10.f, 20.f, 100.f, 80.f }, 40.f, 20.f);
    assert(std::abs(icon_at.first - 40.f) < 0.01f && std::abs(icon_at.second - 50.f) < 0.01f);
    const float attack_scale = fit_icon_scale(96.f, 96.f, 48.f, 48.f, 0.10f);
    assert(attack_scale > 1.5f && attack_scale < 1.7f);
    assert(!item_dragged(10.f, 10.f, 12.f, 10.f));
    assert(item_dragged(10.f, 10.f, 10.f + kItemDragSlop + 1.f, 10.f));
    const auto sheet = make_item_sheet(true, true, false);
    assert(sheet.open && sheet.show_use && sheet.show_sell && !sheet.show_buy);
    assert(sheet.hit(sheet.use.x + 1.f, sheet.use.y + 1.f) == ItemAction::Use);
    assert(sheet.hit(sheet.confirm.x + 1.f, sheet.confirm.y + 1.f) == ItemAction::Confirm);
    assert(sheet.hit(sheet.cancel.x + 1.f, sheet.cancel.y + 1.f) == ItemAction::Cancel);
    input.clear();
    input.event(9, Phase::Down, field.done.x + 2.f, field.done.y + 2.f);
    assert(input.done);
    input.frame();
    input.alternate = false;
    input.event(8, Phase::Down, field.page.x + 2.f, field.page.y + 2.f);
    assert(input.alternate);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_input: %s\n", error.what());
    return 1;
}
