// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/input.hpp"
#include "session.hpp"

#include "common.hpp"
#include "input/touch/effects.hpp"
#include "input/touch/icon.hpp"
#include "scene.hpp"
#include "wear.hpp"

#include <skill_text.hpp>
#include <skills.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {
bool ClientSession::touch_effect_event(std::int64_t finger, touch::Phase phase, float x, float y) {
    const auto result = touch_effect.event(finger, phase, x, y, touch::layout_boxes(), view.effects, now_ms);
    if (result.dismiss) send(cmd::DismissEffect{ result.dismiss->kind, result.dismiss->instance });
    if (touch_effect.modal) {
        touch_cancel_skill_chase(); touch_stop_skill_chase(false);
        touch_input.clear(); touch_skill_hold.clear();
    }
    return result.consumed;
}
void ClientSession::draw_touch_effects(std::vector<std::uint8_t>& framebuffer) {
    if (!scene || !touch_hides_classic_hud() || !touch_shows_field_controls() || view.dead) return;
    const auto chrome = touch::layout_boxes();
    const int width = fb_width(framebuffer);
    const auto& palette = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto& font = scene->font_small;
    const auto button = [&](touch::Box box, const std::string& text) {
        for (int pixel_y = int(box.y); pixel_y < int(box.y + box.height); ++pixel_y)
            for (int pixel_x = int(box.x); pixel_x < int(box.x + box.width); ++pixel_x) {
                if (pixel_x < 0 || pixel_y < 0 || pixel_x >= width || pixel_y >= int(kScreenHeight)) continue;
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(width) + std::size_t(pixel_x)) * 4];
                pixel[0] = std::uint8_t(pixel[0] / 2); pixel[1] = std::uint8_t(pixel[1] / 2); pixel[2] = std::uint8_t(pixel[2] / 2);
            }
        font.draw(framebuffer, width, kScreenHeight, palette, int(box.x) + 4, int(box.y) + 12, text);
    };
    const auto capacity = touch::effect_capacity(chrome);
    const auto start = touch::effect_page_start(view.effects.size(), capacity, touch_effect.page);
    const auto count = std::min(view.effects.size() - start, touch::effects_per_page(view.effects.size(), capacity));
    if (view.effects.size() > capacity && capacity > 1) button(touch::effect_box(chrome, capacity - 1), "+");
    for (std::size_t index = 0; index < count; ++index) {
        const auto& effect = view.effects[start + index];
        const auto box = touch::effect_box(chrome, index);
        button(box, effect.kind == 1 ? "S" : "");
        if (effect.kind != 0) continue;
        const auto* skill = scene->skills.get(effect.code);
        if (!skill || skill->icon < 0) continue;
        int class_index = -1;
        for (int column = 0; column < 7; ++column) if (skill->cls == rules::kClassCode[std::size_t(column)]) class_index = column;
        const auto& art = class_index >= 0 ? scene->skill_icons[std::size_t(class_index)] : scene->generic_skill_icons;
        if (std::uint32_t(skill->icon) >= art.frames_per_direction()) continue;
        const auto& frame = art.frame(0, std::uint32_t(skill->icon));
        touch::draw_circular_icon(framebuffer, width, int(kScreenHeight), box.x, box.y, box.width, box.height,
            frame.pixels, int(frame.width), int(frame.height), [&](std::uint8_t entry) { return palette[entry]; }, 2.f);
    }
    const auto* selected = touch_effect.current(view.effects);
    if (!selected || (!touch_effect.modal && !touch_effect.inspecting(now_ms))) return;
    const auto* skill = selected->kind == 0 ? scene->skills.get(selected->code) : nullptr;
    static constexpr const char* shrine_names[]{ "Armor", "Combat", "Fire Resist", "Cold Resist", "Lightning Resist", "Poison Resist", "Skill", "Mana Recharge", "Stamina", "Experience" };
    const std::string name = skill ? skill->name : selected->code >= 6 && selected->code <= 15
        ? std::string(shrine_names[selected->code - 6]) + " Shrine" : "Effect";
    const std::string description = skill ? game::skill_text::key_text(*scene, skill->str_short) : "Temporary shrine bonus";
    button({ touch::to_viewport_x(270.f), 190, 300, 104 }, "");
    font.draw(framebuffer, width, kScreenHeight, palette, int(touch::to_viewport_x(280.f)), 200, name);
    font.draw(framebuffer, width, kScreenHeight, palette, int(touch::to_viewport_x(280.f)), 220, "Remaining: " + std::to_string((std::uint64_t(selected->remaining_ms) + 999) / 1000) + "s");
    font.draw(framebuffer, width, kScreenHeight, palette, int(touch::to_viewport_x(280.f)), 240, description);
    font.draw(framebuffer, width, kScreenHeight, palette, int(touch::to_viewport_x(280.f)), 270,
        touch_effect.modal ? "Dismiss this effect?" : selected->dismissible ? "Double tap to dismiss" : "Cannot be dismissed");
    if (touch_effect.modal) { button(touch::effect_confirm(), "Confirm"); button(touch::effect_cancel(), "Cancel"); }
}
} // namespace d2d::client
