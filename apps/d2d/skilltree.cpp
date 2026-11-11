// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for skilltree.hpp: the skill tree panel.
#include "skilltree.hpp"

#include "common.hpp"
#include "items.hpp"
#include "scene.hpp"

#include <d2s_items.hpp>
#include <dc6.hpp>
#include <font.hpp>
#include <hover_box.hpp>
#include <rules.hpp>
#include <skill_text.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

std::pair<int, int> skill_icon_at(const d2d::rules::ClassSkill& class_skill) {
    static constexpr int kCol[4] = { 0, 0x131, 0xec, 0xa7 };
    static constexpr int kRow[7] = { 0, 0x1a2, 0x15e, 0x11a, 0xd6, 0x91, 0x4d };
    if (class_skill.col < 1 || class_skill.col > 3 || class_skill.row < 1 || class_skill.row > 6) return { -1000, -1000 };
    return { kTreeR - kCol[class_skill.col], kTreeB - kRow[class_skill.row] };
}

int skill_at(const Scene& scene, int cls, int tab, int mouse_x, int mouse_y) {
    const auto& list = scene.rules.class_skills[std::size_t(cls)];
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].page != tab) continue;
        const auto [icon_x, icon_y] = skill_icon_at(list[i]);
        if (mouse_x > icon_x && mouse_x < icon_x + 0x30 && mouse_y > icon_y - 0x30 && mouse_y < icon_y) return int(i);
    }
    return -1;
}

int skill_tab_at(int mouse_x, int mouse_y) {
    if (mouse_x < kTreeR - 0x58 || mouse_x > kTreeR) return 0;
    if (mouse_y > kTreeB - 0x174 && mouse_y < kTreeB - 0x109) return 3;
    if (mouse_y > kTreeB - 0x108 && mouse_y < kTreeB - 0x9d) return 2;
    if (mouse_y > kTreeB - 0x9c && mouse_y < kTreeB - 0x31) return 1;
    return 0;
}

void draw_skill_tree(std::vector<std::uint8_t>& framebuffer, const Scene& scene, int cls, int tab,
                     const std::array<std::uint8_t, 30>& levels, const d2d::d2s::Stats& stats, const d2d::rules::CalcEnv& env,
                     int pressed, int mouse_x, int mouse_y) {
    if (cls < 0 || cls > 6) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // Text colours are the PL2 shifts (FUN_004f64b0): 1 red, 3 blue, 5 grey.
    // Colour 0 is the act palette itself.
    auto palette_for = [&](int colour) -> const d2d::palette::Palette& {
        if (colour == 1 && !scene.act_text_red.entries().empty()) return scene.act_text_red;
        if (colour == 3 && !scene.act_text_blue.entries().empty()) return scene.act_text_blue;
        if (colour == 5 && !scene.act_text_grey.entries().empty()) return scene.act_text_grey;
        return pal;
    };
    auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y, const d2d::palette::Palette* colour = nullptr) {
        if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
        const auto& frame_ref = spr.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, frame_ref, colour ? *colour : pal, x, y - int(frame_ref.height) + 1);
    };
    const auto& background = scene.skill_tree_bg[std::size_t(cls)];
    for (const int base : { 0, 4 * tab }) {
        dc6(background, base + 0, kTreeR - 0x140, kTreeB - 0xe0);
        dc6(background, base + 1, kTreeR - 0x40, kTreeB - 0xe0);
        dc6(background, base + 2, kTreeR - 0x140, kTreeB - 0x30);
        dc6(background, base + 3, kTreeR - 0x40, kTreeB - 0x30);
    }
    auto centred = [&](const d2d::font::Font& font, int left, int right, int y, const std::string& text) {
        const int width = font.measure(text);
        const int x = width < right - left + 1 ? left + (right - left + 1 - width) / 2 : left;
        font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x, y - int(font.sheet().frame(0, 0).height) + 1, text);
    };
    for (const auto& list : { std::vector<TreeLabel>(std::begin(kTreeHeader), std::end(kTreeHeader)),
                              kTreeTabs[std::size_t(cls)] })
        for (const auto& label : list)
            if (auto found = lookup_string(scene, label.id)) centred(scene.font, kTreeR - 0x5a, kTreeR, label.y, u16_to_latin1(*found));
    const auto pts = stats.get(d2d::d2s::kSkillPts);
    centred(scene.font, kTreeR - 0x41, kTreeR - 0x19, 140, std::to_string(pts));
    const auto& list = scene.rules.class_skills[std::size_t(cls)];
    const auto& ids = scene.skills.class_ids[std::size_t(cls)];
    const int clvl = int(stats.get(d2d::d2s::kLevel));
    int hover = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& class_skill = list[i];
        if (class_skill.page != tab) continue;
        const auto [icon_x, icon_y] = skill_icon_at(class_skill);
        const bool over_icon = mouse_x > icon_x && mouse_x < icon_x + 0x30 && mouse_y > icon_y - 0x30 && mouse_y < icon_y;
        // Points left: grey what can't take one. None left: grey every unlearned icon. Hover wins (colour 3).
        const bool greyed = pts > 0 ? !d2d::rules::can_learn(scene.rules, cls, int(i), levels, clvl) : levels[i] == 0;
        const auto& icon_palette = palette_for(over_icon ? 3 : greyed ? 5 : 0);
        dc6(scene.skill_icons[std::size_t(cls)], class_skill.icon + (int(i) == pressed ? 1 : 0), icon_x, icon_y, &icon_palette);
        const int base_level = int(levels[i]);
        int shown_level = base_level;
        if (env.level && i < ids.size()) shown_level = env.level(ids[i]);
        if (shown_level != 0) {
            const bool formal = shown_level >= 10;
            const auto& level_font = formal && scene.font_formal10.line_height() > 0 ? scene.font_formal10 : scene.font;
            const int level_colour = shown_level > base_level ? 3 : shown_level < base_level ? 1 : 0;
            level_font.draw(framebuffer, kScreenWidth, kScreenHeight, palette_for(level_colour), icon_x + 0x30 - (formal ? 4 : 0),
                            icon_y + 0xc - int(level_font.sheet().frame(0, 0).height) + 1, std::to_string(shown_level));
        }
        if (over_icon) hover = int(i);
    }
    if (hover >= 0 && std::size_t(hover) < ids.size()) {
        const auto [icon_x, icon_y] = skill_icon_at(list[std::size_t(hover)]);
        const int skill = ids[std::size_t(hover)];
        const int lvl = env.level ? env.level(skill) : levels[std::size_t(hover)];
        const int points = levels[std::size_t(hover)];
        const bool learned = std::ranges::any_of(levels, [](std::uint8_t level) { return level > 0; });   // FUN_004ed480: points in any class skill
        const auto lines = d2d::game::skill_tree_tip(scene, env, skill, lvl, points >= list[std::size_t(hover)].max_level, points == 0 && lvl > 0, learned);
        int text_w = 0;
        for (const auto& line : lines) text_w = std::max(text_w, scene.font.measure(line.text));
        draw_hover_lines(framebuffer, scene, lines, d2d::rules::skill_tree_tip_box(icon_x, icon_y, text_w, 16 * int(lines.size()), int(kScreenWidth), int(kScreenHeight)));
    }
}

}  // namespace d2d::client
