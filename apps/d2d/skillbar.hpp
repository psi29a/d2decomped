// SPDX-License-Identifier: GPL-3.0-or-later
// The skill bar: the left and right skill buttons on the control panel
// and the picker a click on one opens, plus the skill hotkeys (F1-F8).
// The chosen skills and hotkeys start from the save (header
// +0x38..0x87); a character's level in a skill is its points plus item
// bonuses (components/rules/skills.hpp). The picker's fan anchors and
// per-row iteration follow spellsel.cpp FUN_004aa7e0 (kLeftX = 0x50,
// kRightX = screen_width - 0x80, up to five rows keyed by SkillDesc
// ListRow 0..4). docs/research/re/skills.md "Skill HUD" has the trace.
// Each picker icon bound to a hotkey shows its key's name (FUN_004a8ed0 /
// FUN_004a9300 over the 16 slots, then FUN_004a8df0): colour 4 (gold),
// cut to 40 px, left edge at x + 0x22, or right-aligned at x + 47 when
// wider than 13 px, baseline at the icon's bottom - 0x23.
// ponytail: the font is whatever is current there (untraced), font8 here;
// the names are F1..F8 (the save's slots), not the key config's.
#pragma once

#include "common.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

struct SkillBar {
    const Scene* scene;
    CharCreateUI& character;
    std::vector<d2d::d2s::ItemProp> extra;   // stats on top of the gear (the skill shrine's +all skills)
    int left = 0, right = 0;               // Skills.txt ids (0: Attack)
    int picking = 0;                       // the picker open: 1 left, 2 right, 0 none

    // Buttons: panel.cpp draws the left / right skill icons on the HUD bar
    // (game.exe positions not fully traced; d2d's guess). Picker: spellsel.cpp
    // FUN_004aa7e0 fans out from EDX = 0x50 / [screen width] - 0x80.
    static constexpr int kButtonLeftX = 117, kButtonRightX = int(kScreenWidth) - 165;
    static constexpr int kPickerLeftX = 80, kPickerRightX = int(kScreenWidth) - 128;
    static constexpr int kIcon = 48;
    static constexpr std::size_t kRows = 5;    // SkillDesc ListRow 0..4

    [[nodiscard]] int cls() const { return std::max(character.character_class, 0); }
    // The skill's level, as the World works it out (fight.hpp skill_level).
    [[nodiscard]] int base_level(int id) const { return skill_base_level(*scene, character, id); }
    [[nodiscard]] int level(int id) const { return skill_level(*scene, character, id, extra); }
    // Can go on the button: known (a level), not passive, and on the left
    // only when Skills.txt's leftskill allows it.
    [[nodiscard]] bool usable(int id, bool on_left) const {
        const auto* skill = scene->skills.get(id);
        return skill && !skill->passive && (!on_left || skill->leftskill) && level(id) > 0;
    }

    // A new game: the save's left and right skills (Attack when they're no
    // longer usable, like panel.cpp's reset when the level drops below 1).
    void new_game() {
        left = int(character.header.left_skill);
        right = int(character.header.right_skill);
        if (!usable(left, true)) left = 0;
        if (!usable(right, false)) right = 0;
        picking = 0;
    }

    // The picker's rows, bottom up, keyed on SkillDesc ListRow (0..4):
    // row 0 the general skills (Attack, Throw, Unsummon, Town Portal),
    // 1..4 the class's own. Rows that have nothing to offer collapse; the
    // remainder stack immediately above the button. spellsel.cpp
    // FUN_004aa7e0's four-panel fan.
    [[nodiscard]] std::vector<std::vector<int>> rows(bool on_left) const {
        std::vector<std::vector<int>> out(kRows);
        for (const auto& skill : scene->skills.rows)
            if (skill.id >= 0 && usable(skill.id, on_left))
                out[std::size_t(std::clamp(skill.list_row, 0, int(kRows) - 1))].push_back(skill.id);
        std::erase_if(out, [](const auto& row) { return row.empty(); });
        return out;
    }
    // Where a picker icon's bottom-left corner is (row 0 sits on the
    // buttons' top edge), or {-1, -1}.
    [[nodiscard]] std::pair<int, int> picker_at(const std::vector<std::vector<int>>& rows, bool on_left, int row, int col) const {
        if (row < 0 || std::size_t(row) >= rows.size()) return { -1, -1 };
        const int x = on_left ? kPickerLeftX + col * kIcon : kPickerRightX - col * kIcon;
        return { x, int(kScreenHeight) - kIcon * (row + 1) };
    }
    // The skill under (mx, my) in the open picker, or -1.
    [[nodiscard]] int picked(int mouse_x, int mouse_y) const {
        if (!picking) return -1;
        const bool on_left = picking == 1;
        const auto row_list = rows(on_left);
        for (std::size_t row = 0; row < row_list.size(); ++row)
            for (std::size_t column = 0; column < row_list[row].size(); ++column) {
                const auto [icon_x, icon_y] = picker_at(row_list, on_left, int(row), int(column));
                if (mouse_x >= icon_x && mouse_x < icon_x + kIcon && mouse_y > icon_y - kIcon && mouse_y <= icon_y) return row_list[row][column];
            }
        return -1;
    }
    [[nodiscard]] static bool on_button(int mouse_x, int mouse_y, int x) { return mouse_x >= x && mouse_x < x + kIcon && mouse_y > int(kScreenHeight) - kIcon; }

    // A click: on a button, open (or close) its picker; in an open picker,
    // choose; elsewhere, close it. True when the bar took the click.
    bool click(const Mouse& mouse) {
        if (!mouse.press_this_frame) return picking != 0;
        if (const int id = picked(mouse.x, mouse.y); id >= 0) {
            (picking == 1 ? left : right) = id;
            picking = 0;
            return true;
        }
        const int was = picking;
        picking = 0;
        if (on_button(mouse.x, mouse.y, kButtonLeftX)) { picking = was == 1 ? 0 : 1; return true; }
        if (on_button(mouse.x, mouse.y, kButtonRightX)) { picking = was == 2 ? 0 : 2; return true; }
        return was != 0;
    }
    // F1..F8: with a picker open, give the hovered skill that key (the
    // left picker marks it for the left button, 0x8000); otherwise choose
    // the skill that key holds.
    void key(SDL_Keycode key_code, int mouse_x, int mouse_y) {
        if (key_code < SDLK_F1 || key_code > SDLK_F8) return;
        const auto slot = std::size_t(key_code - SDLK_F1);
        auto& hotkeys = character.header.hotkeys;
        if (const int id = picked(mouse_x, mouse_y); id >= 0) {
            for (auto& hotkey : hotkeys) if ((hotkey & 0x7fff) == std::uint32_t(id) && ((hotkey & 0x8000) != 0) == (picking == 1)) hotkey = 0xffff;
            hotkeys[slot] = std::uint32_t(id) | (picking == 1 ? 0x8000u : 0u);
            return;
        }
        const auto hotkey = hotkeys[slot];
        if ((hotkey & 0xffff) == 0xffff) return;
        const int id = int(hotkey & 0x7fff);
        const bool on_left = hotkey & 0x8000;
        if (usable(id, on_left)) (on_left ? left : right) = id;
    }

    // The buttons, the open picker (hotkey labels on its icons) and the
    // hovered skill's name.
    void draw(std::vector<std::uint8_t>& framebuffer, int mouse_x, int mouse_y) const {
        const auto& pal = scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal;
        auto icon = [&](int id, int x, int y) {
            const auto* skill = scene->skills.get(id);
            if (!skill) return;
            int column = -1;
            for (int i = 0; i < 7; ++i) if (skill->cls == d2d::rules::kClassCode[std::size_t(i)]) column = i;
            const auto& spr = column >= 0 ? scene->skill_icons[std::size_t(column)] : scene->generic_skill_icons;
            if (skill->icon < 0 || spr.frames_per_direction() <= std::uint32_t(skill->icon)) return;
            const auto& frame = spr.frame(0, std::uint32_t(skill->icon));
            blit_sprite(framebuffer, frame, pal, x, y - int(frame.height) + 1);
        };
        icon(left, kButtonLeftX, int(kScreenHeight));
        icon(right, kButtonRightX, int(kScreenHeight));
        if (!picking) return;
        const bool on_left = picking == 1;
        const auto row_list = rows(on_left);
        for (std::size_t row = 0; row < row_list.size(); ++row)
            for (std::size_t column = 0; column < row_list[row].size(); ++column) {
                const auto [icon_x, icon_y] = picker_at(row_list, on_left, int(row), int(column));
                icon(row_list[row][column], icon_x, icon_y);
                for (std::size_t k = 0; k < 8; ++k) {
                    const auto hotkey = character.header.hotkeys[k];
                    if ((hotkey & 0xffff) != 0xffff && int(hotkey & 0x7fff) == row_list[row][column] && ((hotkey & 0x8000) != 0) == on_left) {
                        const auto label = "F" + std::to_string(k + 1);
                        const int width = scene->font_small.measure(label);
                        scene->font_small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, icon_x + 0x22 + std::min(0, 13 - width),
                                                      icon_y - 0x23 - scene->font_small.line_height(), label, 199, 179, 119);
                    }
                }
            }
        if (const int id = picked(mouse_x, mouse_y); id >= 0)
            if (const auto* skill = scene->skills.get(id)) {
                auto found = lookup_string(*scene, skill->str_name);
                const std::string name = found ? u16_to_latin1(*found) : skill->name;
                scene->font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, std::clamp(mouse_x - scene->font.measure(name) / 2, 0, int(kScreenWidth) - scene->font.measure(name)),
                                 mouse_y - 30, name);
            }
    }
};

}  // namespace d2d::client
