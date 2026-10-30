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
// a slot's name is its first key's, else its second's (which game.exe
// shows is untraced). Town sets `hotkey_names` from the key config.
// Charged items (SpellSel's skill grid, FUN_004aa7e0's top row): one entry
// per item and skill (game::charged_skills), its charges left drawn on the
// icon and the button (SPELLSEL_DrawSkillLevel: at x + 4, the icon's bottom
// - 1, colour 3, nothing at 0). A hand or hotkey keeps the entry's item; a
// hand whose entry is gone goes back to Attack / skill 0
// (SKILLS_AddOrRemoveItemSkill's remove).
// ponytail: a hotkey bound to an entry that's gone stays bound (a no-op till
// the item's back) where game.exe clears it at 0 charges (SPELLSEL_ClearSkillBinding).
#pragma once

#include <array>
#include <optional>
#include <string>

#include "common.hpp"
#include "items.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <character.hpp>
#include <d2s_items.hpp>
#include <item_text.hpp>
#include <rules.hpp>
#include <skill_text.hpp>
#include <skills.hpp>

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
    int left_item = -1, right_item = -1;   // a charged entry's item (Item::id), -1 the character's own skill
    int picking = 0;                       // the picker open: 1 left, 2 right, 0 none
    bool coop = false;                     // local co-op: Resurrect (kResurrectSkill) on the right, its icon Prayer's

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
        if (id == kResurrectSkill) return coop && !on_left;
        const auto* skill = scene->skills.get(id);
        return skill && !skill->passive && (!on_left || skill->leftskill) && level(id) > 0;
    }
    // A picker icon, a hand's or a hotkey's skill: its Skills.txt id and,
    // for a charged entry, the item's unit id (-1 none).
    struct Pick {
        int skill = -1, item = -1;
        friend bool operator==(const Pick&, const Pick&) = default;
    };
    // The charged entry `pick` names, while it's there.
    [[nodiscard]] std::optional<d2d::game::ChargedSkill> entry(Pick pick) const {
        if (pick.item < 0) return std::nullopt;
        for (const auto& charged : d2d::game::charged_skills(character.items))
            if (charged.item == pick.item && charged.skill == pick.skill) return charged;
        return std::nullopt;
    }
    [[nodiscard]] bool usable(Pick pick, bool on_left) const {
        if (pick.item < 0) return usable(pick.skill, on_left);
        const auto* skill = scene->skills.get(pick.skill);
        return skill && !skill->passive && (!on_left || skill->leftskill) && entry(pick);
    }
    [[nodiscard]] Pick hand(bool on_left) const { return on_left ? Pick{ left, left_item } : Pick{ right, right_item }; }
    void choose(bool on_left, Pick pick) { (on_left ? left : right) = pick.skill; (on_left ? left_item : right_item) = pick.item; }
    // A skill's level as cast from `pick`: a charged entry's own (no +skills).
    [[nodiscard]] int level(Pick pick) const {
        if (const auto charged = entry(pick)) return charged->level;
        return level(pick.skill);
    }
    // Each frame: a hand whose charged entry is gone (used up, unequipped,
    // swapped out) goes back to Attack on the left, skill 0 on the right.
    void drop_gone() {
        if (left_item >= 0 && !entry(hand(true))) choose(true, { 0, -1 });
        if (right_item >= 0 && !entry(hand(false))) choose(false, { 0, -1 });
    }

    // A new game: the save's left and right skills (Attack when they're no
    // longer usable, like panel.cpp's reset when the level drops below 1).
    void new_game() {
        left = int(character.header.left_skill);
        right = int(character.header.right_skill);
        if (!usable(left, true)) left = 0;
        if (!usable(right, false)) right = 0;
        left_item = right_item = -1;
        hotkey_items.fill(-1);
        picking = 0;
    }

    // The picker's rows, bottom up, keyed on SkillDesc ListRow (0..4):
    // a known skill it can't use now too (FUN_004aa3a0 doesn't filter on the
    // status: it draws it in colour 1, and FUN_004a9bd0 picks it).
    // row 0 the general skills (Attack, Throw, Unsummon, Town Portal),
    // 1..3 the class's own, 4 the scrolls and tomes. Rows that have nothing
    // to offer collapse; the remainder stack immediately above the button.
    // spellsel.cpp FUN_004aa7e0's fan. The charged entries join row 4.
    [[nodiscard]] std::vector<std::vector<Pick>> rows(bool on_left) const {
        std::vector<std::vector<Pick>> out(kRows);
        for (const auto& skill : scene->skills.rows)
            if (skill.id >= 0 && usable(skill.id, on_left))
                out[std::size_t(std::clamp(skill.list_row, 0, int(kRows) - 1))].push_back({ skill.id, -1 });
        if (usable(kResurrectSkill, on_left)) out[0].push_back({ kResurrectSkill, -1 });   // with the general skills
        for (const auto& charged : d2d::game::charged_skills(character.items))
            if (const Pick pick{ charged.skill, charged.item }; usable(pick, on_left)) out[kRows - 1].push_back(pick);
        std::erase_if(out, [](const auto& row) { return row.empty(); });
        return out;
    }
    // Where a picker icon's bottom-left corner is (row 0 sits on the
    // buttons' top edge), or {-1, -1}.
    [[nodiscard]] std::pair<int, int> picker_at(const std::vector<std::vector<Pick>>& rows, bool on_left, int row, int col) const {
        if (row < 0 || std::size_t(row) >= rows.size()) return { -1, -1 };
        const int x = on_left ? kPickerLeftX + col * kIcon : kPickerRightX - col * kIcon;
        return { x, int(kScreenHeight) - kIcon * (row + 1) };
    }
    // The skill under (mx, my) in the open picker, or skill -1.
    [[nodiscard]] Pick picked(int mouse_x, int mouse_y) const {
        if (!picking) return {};
        const bool on_left = picking == 1;
        const auto row_list = rows(on_left);
        for (std::size_t row = 0; row < row_list.size(); ++row)
            for (std::size_t column = 0; column < row_list[row].size(); ++column) {
                const auto [icon_x, icon_y] = picker_at(row_list, on_left, int(row), int(column));
                if (mouse_x >= icon_x && mouse_x < icon_x + kIcon && mouse_y > icon_y - kIcon && mouse_y <= icon_y) return row_list[row][column];
            }
        return {};
    }
    [[nodiscard]] static bool on_button(int mouse_x, int mouse_y, int x) { return mouse_x >= x && mouse_x < x + kIcon && mouse_y > int(kScreenHeight) - kIcon; }

    // A click: on a button, open (or close) its picker; in an open picker,
    // choose; elsewhere, close it. True when the bar took the click.
    bool click(const Mouse& mouse) {
        if (!mouse.press_this_frame) return picking != 0;
        if (const auto pick = picked(mouse.x, mouse.y); pick.skill >= 0) {
            choose(picking == 1, pick);
            picking = 0;
            return true;
        }
        const int was = picking;
        picking = 0;
        if (on_button(mouse.x, mouse.y, kButtonLeftX)) { picking = was == 1 ? 0 : 1; return true; }
        if (on_button(mouse.x, mouse.y, kButtonRightX)) { picking = was == 2 ? 0 : 2; return true; }
        return was != 0;
    }
    // Skill hotkey `slot` (0..15, the key config's Skill 1..16): with a
    // picker open, give the hovered skill that slot (the left picker marks
    // it for the left button, 0x8000); otherwise choose the skill it holds.
    void hotkey(int slot_index, int mouse_x, int mouse_y) {
        if (slot_index < 0 || slot_index >= 16) return;
        const auto slot = std::size_t(slot_index);
        auto& hotkeys = character.header.hotkeys;
        if (const auto pick = picked(mouse_x, mouse_y); pick.skill >= 0) {
            for (std::size_t other = 0; other < hotkeys.size(); ++other)
                if (bound(other) == pick && ((hotkeys[other] & 0x8000) != 0) == (picking == 1)) { hotkeys[other] = 0xffff; hotkey_items[other] = -1; }
            hotkeys[slot] = std::uint32_t(pick.skill) | (picking == 1 ? 0x8000u : 0u);
            hotkey_items[slot] = pick.item;
            return;
        }
        const auto hotkey = hotkeys[slot];
        if ((hotkey & 0xffff) == 0xffff) return;
        const bool on_left = hotkey & 0x8000;
        if (usable(bound(slot), on_left)) { choose(on_left, bound(slot)); last_slot = slot_index; }
    }
    // What hotkey `slot` holds (skill -1 none). The save keeps its skill id;
    // the charged entry's item lives for the game (the save has no unit ids).
    [[nodiscard]] Pick bound(std::size_t slot) const {
        const auto hotkey = character.header.hotkeys[slot];
        return (hotkey & 0xffff) == 0xffff ? Pick{} : Pick{ int(hotkey & 0x7fff), hotkey_items[slot] };
    }
    // Previous / Next Skill (FUN_004aa740, -1 / 1): from the hotkey last
    // used, the next slot round holding a right-hand skill that can be
    // used becomes the right hand's.
    void cycle(int step) {
        int slot = last_slot;
        for (int tries = 0; tries < 16; ++tries) {
            slot = (slot + (step > 0 ? 1 : 15)) % 16;
            const auto hotkey = character.header.hotkeys[std::size_t(slot)];
            if ((hotkey & 0xffff) == 0xffff || (hotkey & 0x8000)) continue;
            if (!usable(bound(std::size_t(slot)), false)) continue;
            choose(false, bound(std::size_t(slot)));
            last_slot = slot;
            return;
        }
    }
    int last_slot = 0;                             // 0x7c0708: the hotkey last used
    std::array<int, 16> hotkey_items = [] { std::array<int, 16> none{}; none.fill(-1); return none; }();   // each slot's charged entry item, -1 none
    std::array<std::string, 16> hotkey_names{ "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8" };

    // The buttons, the open picker (hotkey labels on its icons) and the
    // hovered skill's name.
    void draw(std::vector<std::uint8_t>& framebuffer, int mouse_x, int mouse_y, const d2d::rules::CalcEnv& env) const {
        // A hovered button's or picker icon's tooltip (FUN_004a8d50 -> FUN_004ef990).
        const auto tip = [&](Pick pick, int x, int y) {
            const int id = pick.skill;
            if (id < 0 || id == kResurrectSkill) return;
            std::vector<TextLine> corner;   // a scroll's or tome's count, its own box first (FUN_004ed3f0)
            const int lvl = pick.item >= 0 ? level(pick) : env.level ? env.level(id) : 1;
            const auto lines = d2d::game::skill_button_tip(*scene, env, id, lvl, &corner);
            if (!corner.empty()) draw_hover_text(framebuffer, *scene, corner, int(kScreenWidth) - 0x80, int(kScreenWidth) - 0x80, int(kScreenHeight) - 0x34, int(kScreenHeight) - 0x34);
            draw_hover_text(framebuffer, *scene, lines, x, x + kIcon, y, y - kIcon);
        };
        const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
        // A skill it can't use now draws in text colour 1 (FUN_004aa3a0's and
        // FUN_004a8d30's draw colour off FUN_004d9fc0: 0 usable, 4 status 6, else 1).
        const auto& refused_pal = scene->act_text_red.entries().empty() ? pal : scene->act_text_red;
        auto icon = [&](Pick pick, int x, int y) {
            const int id = pick.skill;
            const auto prayer = scene->skills.by_name.find("Prayer");   // Resurrect wears Prayer's gold
            const auto* skill = scene->skills.get(id == kResurrectSkill && prayer != scene->skills.by_name.end() ? prayer->second : id);
            if (!skill) return;
            int column = -1;
            for (int i = 0; i < 7; ++i) if (skill->cls == d2d::rules::kClassCode[std::size_t(i)]) column = i;
            const auto& spr = column >= 0 ? scene->skill_icons[std::size_t(column)] : scene->generic_skill_icons;
            if (skill->icon < 0 || spr.frames_per_direction() <= std::uint32_t(skill->icon)) return;
            const auto& frame = spr.frame(0, std::uint32_t(skill->icon));
            blit_sprite(framebuffer, frame, pick.item < 0 && skill_refused(*scene, character, id) ? refused_pal : pal, x, y - int(frame.height) + 1);
            if (const auto charged = entry(pick); charged && charged->left > 0) {   // SPELLSEL_DrawSkillLevel: the charges left
                std::string count = std::to_string(charged->left);
                while (scene->font_small.measure(count) > 0x28) count.pop_back();
                scene->font_small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 4, y - 1 - scene->font_small.line_height(), count,
                                              d2d::game::kTxtBlue[0], d2d::game::kTxtBlue[1], d2d::game::kTxtBlue[2]);
            }
        };
        icon(hand(true), kButtonLeftX, int(kScreenHeight));
        icon(hand(false), kButtonRightX, int(kScreenHeight));
        if (!picking) {
            if (on_button(mouse_x, mouse_y, kButtonLeftX)) tip(hand(true), kButtonLeftX, int(kScreenHeight));
            if (on_button(mouse_x, mouse_y, kButtonRightX)) tip(hand(false), kButtonRightX, int(kScreenHeight));
            return;
        }
        const bool on_left = picking == 1;
        const auto row_list = rows(on_left);
        for (std::size_t row = 0; row < row_list.size(); ++row)
            for (std::size_t column = 0; column < row_list[row].size(); ++column) {
                const auto [icon_x, icon_y] = picker_at(row_list, on_left, int(row), int(column));
                icon(row_list[row][column], icon_x, icon_y);
                for (std::size_t k = 0; k < 16; ++k) {
                    const auto hotkey = character.header.hotkeys[k];
                    if (bound(k) == row_list[row][column] && ((hotkey & 0x8000) != 0) == on_left) {
                        const auto& label = hotkey_names[k];
                        const int width = scene->font_small.measure(label);
                        scene->font_small.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, icon_x + 0x22 + std::min(0, 13 - width),
                                                      icon_y - 0x23 - scene->font_small.line_height(), label, 199, 179, 119);
                    }
                }
            }
        if (const auto pick = picked(mouse_x, mouse_y); pick.skill == kResurrectSkill)
            scene->font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, std::clamp(mouse_x - scene->font.measure("Resurrect") / 2, 0, int(kScreenWidth) - scene->font.measure("Resurrect")),
                             mouse_y - 30, "Resurrect");
        else if (pick.skill >= 0) tip(pick, mouse_x - kIcon / 2, mouse_y);
    }
};

}  // namespace d2d::client
