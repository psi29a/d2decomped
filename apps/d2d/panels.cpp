// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for panels.hpp: inventory, character, HUD, automap, waypoint panels.
#include "panels.hpp"

#include "common.hpp"
#include "items.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s.hpp>
#include <d2s_automap.hpp>
#include <d2s_items.hpp>
#include <font.hpp>
#include <item_text.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>
#include <palette.hpp>
#include <quests.hpp>
#include <automap_scroll.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::client {

namespace {
// FUN_00483e40: a hovered identified, socketed grid item's sockets, each
// GemSocket with what fills it over the item; places in half cells from
// 0x6d91e8, [sockets - 1][width - 1][height - 1] (0, 0: none for that size).
void draw_sockets(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Item& item, int box_w, int box_h, int left, int top,
                  const d2d::palette::Palette& pal) {
    using Place = std::array<std::pair<int, int>, 6>;
    static constexpr std::array<std::array<std::array<Place, 4>, 2>, 6> kPlaces{ {
        { { { { Place{ { { 1, 1 } } }, Place{ { { 1, 2 } } }, Place{ { { 1, 3 } } }, Place{ { { 1, 4 } } } } },
            { { Place{ { { 2, 1 } } }, Place{ { { 2, 2 } } }, Place{ { { 2, 3 } } }, Place{ { { 2, 4 } } } } } } },
        { { { { Place{}, Place{ { { 1, 1 }, { 1, 3 } } }, Place{ { { 1, 2 }, { 1, 4 } } }, Place{ { { 1, 3 }, { 1, 5 } } } } },
            { { Place{ { { 1, 1 }, { 3, 1 } } }, Place{ { { 2, 1 }, { 2, 3 } } }, Place{ { { 2, 2 }, { 2, 4 } } }, Place{ { { 2, 2 }, { 2, 6 } } } } } } },
        { { { { Place{}, Place{}, Place{ { { 1, 1 }, { 1, 3 }, { 1, 5 } } }, Place{ { { 1, 2 }, { 1, 4 }, { 1, 6 } } } } },
            { { Place{}, Place{ { { 1, 1 }, { 3, 1 }, { 2, 3 } } }, Place{ { { 2, 1 }, { 2, 3 }, { 2, 5 } } }, Place{ { { 2, 2 }, { 2, 4 }, { 2, 6 } } } } } } },
        { { { { Place{}, Place{}, Place{}, Place{ { { 1, 1 }, { 1, 3 }, { 1, 5 }, { 1, 7 } } } } },
            { { Place{}, Place{ { { 1, 1 }, { 3, 3 }, { 1, 3 }, { 3, 1 } } }, Place{ { { 1, 2 }, { 3, 4 }, { 1, 4 }, { 3, 2 } } }, Place{ { { 2, 1 }, { 2, 3 }, { 2, 5 }, { 2, 7 } } } } } } },
        { { { { Place{}, Place{}, Place{}, Place{} } },
            { { Place{}, Place{}, Place{ { { 2, 3 }, { 1, 1 }, { 3, 5 }, { 1, 5 }, { 3, 1 } } }, Place{ { { 2, 4 }, { 1, 2 }, { 3, 6 }, { 1, 6 }, { 3, 2 } } } } } } },
        { { { { Place{}, Place{}, Place{}, Place{} } },
            { { Place{}, Place{}, Place{ { { 1, 1 }, { 1, 3 }, { 1, 5 }, { 3, 1 }, { 3, 3 }, { 3, 5 } } }, Place{ { { 1, 2 }, { 1, 4 }, { 1, 6 }, { 3, 2 }, { 3, 4 }, { 3, 6 } } } } } } },
    } };
    if (!item.identified || !item.socketed || item.sockets < 1 || item.sockets > 6 || scene.gem_socket.frames_per_direction() == 0) return;
    const auto [width, height] = d2d::rules::item_size(scene.rules, item.code);
    if (width < 1 || width > 2 || height < 1 || height > 4) return;
    const auto& places = kPlaces[std::size_t(item.sockets - 1)][std::size_t(width - 1)][std::size_t(height - 1)];
    const auto& socket = scene.gem_socket.frame(0, 0);
    for (int i = 0; i < item.sockets; ++i) {
        const auto [col, row] = places[std::size_t(i)];
        const int socket_x = (col - 1) * box_w / 2 + left - 1, bottom = (row + 1) * box_h / 2 + top;
        blit_sprite(framebuffer, socket, pal, socket_x, bottom - int(socket.height) + 1);
        if (std::size_t(i) >= item.socketed_items.size()) continue;
        const auto& gem = item.socketed_items[std::size_t(i)];
        if (const auto* spr = scene.item_sprite(gem); spr && spr->frames_per_direction() > 0) blit_sprite(framebuffer, spr->frame(0, 0), scene.item_pal(gem, pal), socket_x, bottom - box_h);
    }
}
}  // namespace

// The grid's items sit on a colour (FUN_00483ff0 -> FUN_004839d0): each cell
// filled at 25 % over what's there (draw mode 0; game.exe's RGB blend, as
// measured live 2026-10-25): red (0x80, 0, 0) when the wearer can't use it
// (FUN_0062eaf0's requirements, or flags 0x100 / 0x4000: FUN_0062a4e0),
// dull red (0x80, 0x40, 0x40) unidentified, else blue (0, 0, 0x80)
// (FUN_00483960; their nearest palette colours, 0x08 / 0x4d / 0xea).
// ponytail: the picked-up item's green, flag 4, FUN_004c2240's states and
// the used quest consumables' red (FUN_00483f80) aren't told apart.
void tint_cells(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Item& item, const d2d::rules::Wearer& wearer, const std::array<int, 4>& rect) {
    static constexpr std::array<std::array<int, 3>, 3> kTints{ { { 144, 0, 0 }, { 140, 64, 64 }, { 24, 0, 104 } } };   // act 1's 0x08 / 0x4d / 0xea
    const bool usable = d2d::rules::meets_requirements(scene.rules, item, wearer, d2d::game::required_level(scene, item)) && !(item.flags & 0x4100u);
    const auto& tint = kTints[!usable ? 0 : !item.identified ? 1 : 2];
    const int screen_w = fb_width(framebuffer);
    for (int y_pos = std::max(rect[1], 0); y_pos < std::min(rect[1] + rect[3], int(kScreenHeight)); ++y_pos)
        for (int x_pos = std::max(rect[0], 0); x_pos < std::min(rect[0] + rect[2], screen_w); ++x_pos) {
            auto* pixel = &framebuffer[(std::size_t(y_pos) * std::size_t(screen_w) + std::size_t(x_pos)) * 4];
            for (std::size_t channel = 0; channel < 3; ++channel) pixel[channel] = std::uint8_t((tint[channel] + 3 * pixel[channel]) / 4);
        }
}

void draw_inventory(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Scene::InvLayout& layout,
                    const std::vector<d2d::d2s::Item>& items, int mouse_x , int mouse_y , const d2d::rules::Wearer* wearer ,
                    const std::function<std::string(const d2d::d2s::Item&)>* price, bool weapon_set_two) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // invchar6.dc6 holds two 2x2 panels (256+64 wide, 256+176 tall);
    // frames 4..7 are the inventory, 0..3 the character-stats page.
    if (scene.inv_panel.frames_per_direction() >= 8) {
        const auto& frame = scene.inv_panel.frame(0, 4);
        blit_sprite(framebuffer, frame, pal, layout.panel_x, layout.panel_y);
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 5), pal, layout.panel_x + int(frame.width), layout.panel_y);
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 6), pal, layout.panel_x, layout.panel_y + int(frame.height));
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 7), pal, layout.panel_x + int(frame.width), layout.panel_y + int(frame.height));
    }
    namespace body = d2d::d2s::body_location;
    if (weapon_set_two && scene.inv_tab.frames_per_direction() >= 2)
        for (const auto& [tab_frame, slot] : { std::pair{ 0u, body::kLeftArm }, std::pair{ 1u, body::kRightArm } }) {
            const auto& box = layout.slots[std::size_t(slot)];
            const auto& art = scene.inv_tab.frame(0, tab_frame);
            blit_sprite(framebuffer, art, pal, box[0] - 5, box[1] + box[3] + 4 - int(art.height));
        }
    // Empty body slots show their outline (FUN_0048edf0): each slot's art and
    // frame, at its box's left + dx, bottom + dy (table 0x7220b0). A hand
    // stays bare beside a two-handed weapon (FUN_0063d340 == 2: items +0x11c).
    {
        struct Outline { int slot; const d2d::dc6::Sprite* art; std::uint32_t frame; int dx, dy; };
        const std::array<Outline, 10> outlines{ {
            { body::kTorso, &scene.inv_armor, 0, 2, -2 }, { body::kBelt, &scene.inv_belt, 0, 0, -1 },
            { body::kFeet, &scene.inv_boots, 0, -1, -4 }, { body::kHead, &scene.inv_helm_glove, 1, 0, -1 },
            { body::kGloves, &scene.inv_helm_glove, 0, -1, -2 }, { body::kNeck, &scene.inv_ring_amulet, 0, 0, -1 },
            { body::kRightRing, &scene.inv_ring_amulet, 1, 0, -1 }, { body::kLeftRing, &scene.inv_ring_amulet, 1, 0, -1 },
            { body::kRightArm, &scene.inv_weapons, 0, 0, -2 }, { body::kLeftArm, &scene.inv_weapons, 0, 0, -2 },
        } };
        auto worn = [&](int slot) -> const d2d::d2s::Item* {
            for (const auto& item : items)
                if (item.location == d2d::d2s::item_location::kEquipped && item.slot == slot) return &item;
            return nullptr;
        };
        auto two_handed = [&](const d2d::d2s::Item* item) {
            const auto info = item ? scene.rules.item_info.find(item->code) : scene.rules.item_info.end();
            return info != scene.rules.item_info.end() && info->second.two_handed;
        };
        for (const auto& outline : outlines) {
            if (worn(outline.slot) || outline.art->frames_per_direction() <= outline.frame) continue;
            if (outline.slot == body::kRightArm && two_handed(worn(body::kLeftArm))) continue;
            if (outline.slot == body::kLeftArm && two_handed(worn(body::kRightArm))) continue;
            const auto& box = layout.slots[std::size_t(outline.slot)];
            const auto& art = outline.art->frame(0, outline.frame);
            const int set_two = weapon_set_two && (outline.slot == body::kRightArm || outline.slot == body::kLeftArm) ? 4 : 0;   // as the items
            blit_sprite(framebuffer, art, pal, box[0] + outline.dx + set_two, box[1] + box[3] + outline.dy - set_two - int(art.height));
        }
    }
    auto draw_in = [&](const d2d::d2s::Item& item, int x, int y, int width, int height) {
        const auto* spr = scene.item_sprite(item);
        if (!spr || spr->frames_per_direction() == 0) return;
        const auto& frame = spr->frame(0, 0);
        blit_sprite(framebuffer, frame, scene.item_pal(item, pal), x + (width - int(frame.width)) / 2, y + (height - int(frame.height)) / 2);
    };
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& item : items) {
        std::array<int, 4> rect{};
        if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) {
            const auto info = scene.rules.item_info.find(item.code);
            const int item_width = info != scene.rules.item_info.end() ? info->second.width : 1;
            const int item_height = info != scene.rules.item_info.end() ? info->second.height : 1;
            rect = { layout.grid_x + item.column * layout.box_w, layout.grid_y + item.row * layout.box_h, item_width * layout.box_w, item_height * layout.box_h };
        } else if (item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast) {
            rect = layout.slots[std::size_t(item.slot)];
            if (weapon_set_two && (item.slot == body::kRightArm || item.slot == body::kLeftArm)) { rect[0] += 4; rect[1] -= 4; }
        }
        if (rect[2] <= 0) continue;
        if (item.location == d2d::d2s::item_location::kStored && wearer) tint_cells(framebuffer, scene, item, *wearer, rect);
        draw_in(item, rect[0], rect[1], rect[2], rect[3]);
        if (mouse_x >= rect[0] && mouse_x < rect[0] + rect[2] && mouse_y >= rect[1] && mouse_y < rect[1] + rect[3]) { hover = &item; hover_box = rect; }
    }
    if (hover && hover->location == d2d::d2s::item_location::kStored) draw_sockets(framebuffer, scene, *hover, layout.box_w, layout.box_h, hover_box[0], hover_box[1], pal);
    if (hover && hover->location == d2d::d2s::item_location::kEquipped) {   // FUN_0048b540: the item's cells centred in its slot
        const auto [width, height] = d2d::rules::item_size(scene.rules, hover->code);
        draw_sockets(framebuffer, scene, *hover, layout.box_w, layout.box_h, hover_box[0] + (hover_box[2] - width * layout.box_w) / 2,
                     hover_box[1] + (hover_box[3] - height * layout.box_h) / 2, pal);
    }
    if (hover) {
        auto lines = item_lines(scene, *hover, wearer ? wearer->lvl : 1, wearer);
        if (price && *price) lines.push_back({ (*price)(*hover), kTxtWhite });
        draw_hover_text(framebuffer, scene, lines, hover_box[0], hover_box[0] + hover_box[2],
                        hover_box[1] + hover_box[3], hover_box[1]);
    }
}

int stat_button_at(int mouse_x, int mouse_y) {
    for (int i = 0; i < 4; ++i) {
        const auto& button = kStatButtons[i];
        const int x = mouse_x - kCharPanelX, y = mouse_y - kCharPanelY;
        if (x > button.x && x < button.x + 40 && y > button.y - 22 && y < button.y) return i;
    }
    return -1;
}

void draw_char_panel(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats,
                     const PanelStats& panel,
                     std::string_view name, int class_idx, int pressed_button) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const int panel_x = kCharPanelX, panel_y = kCharPanelY;
    if (scene.inv_panel.frames_per_direction() >= 8) {
        const auto& frame = scene.inv_panel.frame(0, 0);
        blit_sprite(framebuffer, frame, pal, panel_x, panel_y);
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 1), pal, panel_x + int(frame.width), panel_y);
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 2), pal, panel_x, panel_y + int(frame.height));
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 3), pal, panel_x + int(frame.width), panel_y + int(frame.height));
    }
    // As FUN_004a7d00 draws it: text centred in [x0, x1] as
    // x0 + (x1 - x0 + 1 - w) / 2 (left-aligned when it doesn't fit), y the
    // baseline. Labels in font6, a "Fire\nResistance" pair at y-4 / y+4;
    // values in font16, dropping to font8 when > 999 or too wide (life/
    // mana/stamina); name font16/font8/font6 by length, class font16.
    // Colour (local_8 in FUN_004a7d00): strength .. max stamina blue when
    // their total is over the character's own, red when under (life, mana,
    // stamina themselves stay white); a resistance gold at its cap, red
    // below 0.
    // ponytail: the states' part (Battle Orders' life, a curse's resistances
    // blue / red, defense) isn't coloured: the panel sees items and passives.
    auto pick = [&](const d2d::font::Font& font) -> const d2d::font::Font& {
        return font.line_height() > 0 ? font : scene.font;
    };
    const auto& f16 = scene.font;
    const auto& font8 = pick(scene.font_small);
    const auto& font6 = pick(scene.font_tiny);
    auto text = [&](const d2d::font::Font& font, int left, int right, int y, const std::string& label, int colour = 0) {
        const int width = font.measure(label);
        const int x = width < right - left + 1 ? left + (right - left + 1 - width) / 2 : left;
        // Glyph cells blit bottom-anchored at y, like any DC6 (font6 cells
        // are 11 tall with the baseline on row 8).
        const int text_y = panel_y + y - int(font.sheet().frame(0, 0).height) + 1;
        static constexpr std::array<std::array<std::uint8_t, 3>, 10> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 },
                                                                              { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 100 } } };
        if (colour == 0) font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, panel_x + x, text_y, label);
        else font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, panel_x + x, text_y, label, kRgb[std::size_t(colour)][0], kRgb[std::size_t(colour)][1], kRgb[std::size_t(colour)][2]);
    };
    for (const auto& label : kCharLabels) {
        const auto found = lookup_string(scene, std::uint16_t(label.id));
        if (!found) continue;
        const auto txt = u16_to_latin1(*found);
        if (const auto newline = txt.find('\n'); newline != txt.npos) {
            text(font6, label.left, label.right, label.y - 4, txt.substr(0, newline));
            text(font6, label.left, label.right, label.y + 4, txt.substr(newline + 1));
        } else {
            text(font6, label.left, label.right, label.y, txt);
        }
    }
    for (const auto& value_def : kCharValues) {
        const bool fixed = value_def.id >= d2d::d2s::kLife && value_def.id <= d2d::d2s::kMaxStamina;       // life/mana/stamina, 8.8
        std::int64_t value = fixed ? stats.fixed(value_def.id) : stats.get(value_def.id);
        int colour = 0;                                   // 1 red, 3 blue, 4 gold
        if (value_def.id < d2d::d2s::kLevel && value_def.id != d2d::d2s::kLife && value_def.id != d2d::d2s::kMana && value_def.id != d2d::d2s::kStamina) {   // the maxima already hold theirs (Fight::item_max)
            const auto bonus = panel.bonus[std::size_t(value_def.id)];
            if (value_def.id <= d2d::d2s::kVit) value += bonus;
            colour = bonus > 0 ? 3 : bonus < 0 ? 1 : 0;
        }
        auto res = [&](int resist_index) { value = panel.res[std::size_t(resist_index)]; colour = value >= panel.res_cap[std::size_t(resist_index)] ? 4 : value < 0 ? 1 : 0; };
        switch (value_def.id) {
            case d2d::d2s::kNextExperience: value = panel.next; break;
            case d2d::d2s::kArmorClass: value = panel.defense; break;
            case d2d::d2s::kFireResist: res(0); break;
            case d2d::d2s::kColdResist: res(1); break;
            case d2d::d2s::kLightningResist: res(2); break;
            case d2d::d2s::kPoisonResist: res(3); break;
            default: break;
        }
        if (value_def.id == d2d::d2s::kNextExperience && value < 0) continue;                // max level: blank
        const auto txt = std::to_string(value);
        const bool small_font = (fixed || value_def.id == d2d::d2s::kArmorClass) && (value > 999 || f16.measure(txt) >= value_def.right - value_def.left);
        text(small_font ? font8 : f16, value_def.left, value_def.right, value_def.y, txt, colour);
    }
    // The left and right skill's blocks (FUN_004eda20 -> FUN_004ed570): the
    // SkillDesc "str alt" name upper-cased (FUN_00452180) in font6; "Damage"
    // (0xfdd) and its value, font16 unless 11/7 of its font6 width overflows
    // the box, then font6 a pixel up (FUN_004e96e0); "%s\nAttack Rating"
    // (0xfdf; 0xfe1 "%s\nRating" for Attack) at y-4 / y+4 and the value in
    // font16, font8 from 1000 (FUN_004e9940). The numbers come from the
    // server (rules::attack_line).
    // ponytail: dual wield's two-value line (FUN_004e9870), the hover
    // tooltips (FUN_004a7340..), the "K" formats past 9999 and other
    // languages' y-1 aren't drawn.
    for (std::size_t block = 0; block < 2; ++block) {
        const auto& line = panel.attack[block];
        const auto* skill = scene.skills.get(line.skill);
        if (!skill) continue;
        const auto* rec = &kAttackBlock[block * 6];
        std::string skill_name;
        if (auto found = lookup_string(scene, skill->str_alt)) skill_name = u16_to_latin1(*found);
        else if (auto fallback = lookup_string(scene, std::uint16_t(0x1506))) skill_name = u16_to_latin1(*fallback);
        for (auto& letter : skill_name) letter = char(std::toupper(static_cast<unsigned char>(letter)));
        text(font6, rec[0].left, rec[0].right, rec[0].y, skill_name);
        if (line.damage) {
            if (auto found = lookup_string(scene, std::uint16_t(0xfdd))) text(font6, rec[1].left, rec[1].right, rec[1].y, u16_to_latin1(*found));
            const int top = std::max(line.max, line.min + 1);
            const auto range = std::to_string(line.min) + "-" + std::to_string(top);
            const bool wide = font6.measure(range) * 11 / 7 > rec[2].right - rec[2].left;
            text(wide ? font6 : f16, rec[2].left, rec[2].right, rec[2].y - (wide ? 1 : 0), range, line.damage_colour);
        }
        if (line.attack_rating != 0) {
            if (auto found = lookup_string(scene, std::uint16_t(line.skill == 0 ? 0xfe1 : 0xfdf))) {
                auto label = u16_to_latin1(*found);
                if (const auto placeholder = label.find("%s"); placeholder != label.npos) label.replace(placeholder, 2, skill_name);
                const auto newline = label.find('\n');
                text(font6, rec[3].left, rec[3].right, rec[3].y - 4, label.substr(0, newline));
                if (newline != label.npos) text(font6, rec[3].left, rec[3].right, rec[3].y + 4, label.substr(newline + 1));
            }
            text(line.attack_rating < 1000 ? f16 : font8, rec[5].left, rec[5].right, rec[5].y, std::to_string(line.attack_rating), line.ar_colour);
        }
    }
    std::string cls = class_idx >= 0 && class_idx < 7 ? kClassKey[class_idx] : "";
    if (auto found = lookup_string(scene, cls)) cls = u16_to_latin1(*found);
    const auto& fname = name.size() + 1 <= 11 ? f16 : name.size() + 1 < 14 ? font8 : font6;
    text(fname, 13, 13 + 0xa1 - 0xd - 1, 25, std::string(name));
    text(f16, 0xc1, 0x137 - 1, 25, cls);
    // Unspent stat points (FUN_004a7d00): the skillpoints box at (3, 364)
    // with "Stat Points" / "Remaining" (0xfeb, 0xfec) in font6 centred in
    // 11..88 at baselines 355 / 363 and the count in font16 in 92..127 at
    // 360; each stat's button, levelsocket at (x+5, y+5) under level
    // (frame 1 while pressed) at (x+8, y+1). DC6s anchor bottom-left.
    if (const auto pts = stats.get(d2d::d2s::kStatPts); pts > 0) {
        auto dc6 = [&](const d2d::dc6::Sprite& spr, int frame, int x, int y) {
            if (frame < 0 || spr.frames_per_direction() <= std::uint32_t(frame)) return;
            const auto& frame_ref = spr.frame(0, std::uint32_t(frame));
            blit_sprite(framebuffer, frame_ref, pal, panel_x + x, panel_y + y - int(frame_ref.height) + 1);
        };
        dc6(scene.points_box, 0, 3, 364);
        static constexpr std::pair<int, int> kPointLabels[] = { { 0xfeb, 355 }, { 0xfec, 363 } };    // string id, y
        for (auto [id, label_y] : kPointLabels)
            if (auto found = lookup_string(scene, std::uint16_t(id))) text(font6, 11, 0x59 - 1, label_y, u16_to_latin1(*found));
        text(f16, 0x5c, 0x80 - 1, 360, std::to_string(pts));
        for (int i = 0; i < 4; ++i) {
            const auto& button = kStatButtons[i];
            dc6(scene.level_socket, 0, button.x + 5, button.y + 5);
            dc6(scene.level_button, i == pressed_button ? 1 : 0, button.x + 8, button.y + 1);
        }
    }
}

void draw_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats, const Hud& hud, int mouse_x, int mouse_y) {
    using namespace d2d::d2s;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const int width = int(kScreenWidth), height = int(kScreenHeight);
    auto at_bottom = [&](const d2d::dc6::Sprite& spr, int frame, int x, int bottom) {
        if (frame >= int(spr.frames_per_direction())) return;
        const auto& frame_ref = spr.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, frame_ref, pal, x, bottom - int(frame_ref.height));
    };
    if (scene.ctrl_panel.frames_per_direction() >= 6) {
        const int orb_x[6] = { 0, width / 2 - 0xeb, width / 2 - 0x6b, width / 2 + 0x15, width / 2 + 0x95, width - 0x75 };
        for (int i = 0; i < 6; ++i) at_bottom(scene.ctrl_panel, i, orb_x[i], height);
    }
    // Globe fill: only the bottom `rows` rows of the 80x80 cel.
    auto fill = [&](int frame, int x, std::int64_t cur, std::int64_t max) {
        if (max <= 0 || frame >= int(scene.globes.frames_per_direction())) return;
        const auto& frame_ref = scene.globes.frame(0, std::uint32_t(frame));
        int rows = int(std::clamp<std::int64_t>(cur * 80 / max, 0, 80));
        if (frame != 1 && rows == 1) rows = 2;               // life: 2 rows at least while alive
        const int top = height - 13 - int(frame_ref.height);
        for (int y = int(frame_ref.height) - rows; y < int(frame_ref.height); ++y)
            for (int left = 0; left < int(frame_ref.width); ++left) {
                const auto idx = frame_ref.pixels[std::size_t(y) * frame_ref.width + std::size_t(left)];
                const int pixel_x = x + left, pixel_y = top + y;
                if (!idx || pixel_x < 0 || pixel_y < 0 || pixel_x >= width || pixel_y >= height) continue;
                const auto colour = pal[idx];
                auto* pixel = framebuffer.data() + (std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4;
                pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b;
            }
    };
    fill(hud.poisoned ? 2 : 0, 29, stats.fixed(kLife), stats.fixed(kMaxLife));
    fill(1, width - 0x6f, stats.fixed(kMana), stats.fixed(kMaxMana));
    at_bottom(scene.globe_glass, 0, 28, height - 5);
    at_bottom(scene.globe_glass, 1, width - 0x6e, height - 9);
    auto str = [&](int id) {
        const auto found = lookup_string(scene, std::uint16_t(id));
        return found ? u16_to_latin1(*found) : std::string{};
    };
    auto put = [&](int x, int y, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        auto* pixel = framebuffer.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
        pixel[0] = red; pixel[1] = green; pixel[2] = blue;
    };
    constexpr std::array<std::uint8_t, 3> kWhite{ 255, 255, 255 }, kBlue{ 105, 105, 255 };
    std::vector<std::pair<std::array<int, 2>, TextLine>> hovers;   // centre x, bottom: drawn over the bars
    auto over = [&](int left, int top, int right, int bottom) { return mouse_x >= left && mouse_x <= right && mouse_y >= top && mouse_y <= bottom; };
    // Experience: the level's share of 119 px.
    const auto lvl = stats.get(kLevel), exp_now = stats.values[kExp];
    if (lvl >= 1 && std::size_t(lvl) < scene.exp_next.size()) {
        const auto next = scene.exp_next[std::size_t(lvl)], prev = scene.exp_next[std::size_t(lvl - 1)];
        if (over(width / 2 - 0x92, height - 0x2b, width / 2 - 0x17, height - 0x22)) {
            auto format = str(0x1043);
            for (auto at = format.find("%u"); at != std::string::npos; at = format.find("%u")) format[at + 1] = 'd';
            hovers.push_back({ { width / 2 - 0x92, height - 0x33 }, { game::d2_format(format, { exp_now, next }), kWhite } });
        }
        if (next > prev) {
            std::int64_t num = exp_now > prev ? exp_now - prev : 0, den = next - prev;
            if (num > 0x226b901) { num >>= 7; den >>= 7; }
            int share = den > 0 ? int(num * 0x77 / den) : 0;
            if (share > 0x77) share = 0;
            const auto colour = pal[0xff];
            if (lvl < 99 && share > 0)
                for (int x = width / 2 - 0x90; x <= width / 2 - 0x90 + share; ++x) {
                    put(x, height - 0x26, colour.r, colour.g, colour.b);
                    put(x, height - 0x25, colour.r, colour.g, colour.b);
                }
        }
    }
    // Run / walk.
    const bool on_run = over_run_button(mouse_x, mouse_y);
    at_bottom(scene.run_button, (hud.running ? 2 : 0) + (hud.run_down && on_run ? 1 : 0), width / 2 - 0x91, height - 10);
    if (on_run) {
        auto special = str(0x1052);
        if (const auto placeholder = special.find("%s"); placeholder != std::string::npos) special.replace(placeholder, 2, "R");
        hovers.push_back({ { width / 2 - 0x91, height - 0x17 }, { str(0x1053) + special, kWhite } });
    }
    // Stamina (FUN_004975b0): 102 px when full, in the palette entry nearest
    // its colour (FUN_004fb180), at three quarters (draw mode 2); over its
    // most or with a stambarblue state (flag 0x18) all of it, blue.
    auto nearest = [&](int red, int green, int blue) {
        int best = 1 << 30;
        std::array<std::uint8_t, 3> out{};
        for (std::size_t i = 0; i < 256 && i < pal.entries().size(); ++i) {
            const auto colour = pal[std::uint8_t(i)];
            const int distance = (colour.r - red) * (colour.r - red) + (colour.g - green) * (colour.g - green) + (colour.b - blue) * (colour.b - blue);
            if (distance < best) { best = distance; out = { colour.r, colour.g, colour.b }; }
        }
        return out;
    };
    std::int64_t stamina = stats.values[kStamina], stamina_max = stats.values[kMaxStamina];
    auto rgb = nearest(0xf4, 0xc0, 0x4c);
    if (stamina_max + 5 < stamina || hud.stamina_blue) { stamina_max = stamina; rgb = nearest(0, 0, 0xff); }
    const int bar = stamina_max < 1 ? 0 : int(stamina * 0x66 / stamina_max);
    if (stamina_max < 1 || bar < 0x19) rgb = nearest(0xff, 0, 0);
    for (int y = height - 0x1b; y < height - 0x1b + 0x12; ++y)
        for (int x = width / 2 - 0x7f; x < width / 2 - 0x7f + bar; ++x) {
            if (x < 0 || y < 0 || x >= width || y >= height) continue;
            auto* pixel = framebuffer.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) pixel[channel] = std::uint8_t((rgb[channel] * 3 + pixel[channel]) / 4);
        }
    if (over(width / 2 - 0x7f, height - 0x1b, width / 2 - 0x19, height - 9)) {
        auto shown = stats.values[kStamina] >> 8;
        const auto top = stats.values[kMaxStamina] >> 8;
        const bool more = shown > top;
        if (more) shown = top;
        // ponytail: game.exe's text is blue with shrine_stamina (state 0x88) on too.
        hovers.push_back({ { width / 2 - 0x4c, height - 0x34 }, { game::d2_format(str(0x1044), { shown, top }), more ? kBlue : kWhite } });
    }
    for (const auto& [anchor, line] : hovers) draw_hover_text(framebuffer, scene, { line }, anchor[0], anchor[0], anchor[1], anchor[1]);
    // The globes' text (FUN_00498120): plain, centred, in the frame's font.
    // ponytail: taken as font16, as for the level buttons.
    auto globe_text = [&](int centre, const std::string& text) {
        const int text_y = height - 0x5f - int(scene.font.sheet().frame(0, 0).height) + 1;
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, centre - scene.font.measure(text) / 2, text_y, text);
    };
    if (over(0x1e, height - 0x4b, 0x6e, height - 0xf)) {
        auto life = stats.values[kLife] >> 8;
        if (stats.values[kLife] > 0 && life < 1) life = 1;
        globe_text(0x41, game::d2_format(str(0x1045), { life, stats.values[kMaxLife] >> 8 }));
    }
    if (over(width - 0x6f, height - 0x4b, width - 0x1f, height - 0xf))
        globe_text(width - 0x50, game::d2_format(str(0x1046), { stats.values[kMana] >> 8, stats.values[kMaxMana] >> 8 }));
}

bool over_run_button(int mouse_x, int mouse_y) {
    const int width = int(kScreenWidth), height = int(kScreenHeight);
    return mouse_x >= width / 2 - 0x91 && mouse_x <= width / 2 - 0x80 && mouse_y >= height - 0x1c && mouse_y <= height - 8;
}

// UI 6 / 7 latch on when points are unspent and their panel is shut, off
// when none are left; drawn only while not hidden (FUN_004a6a00 /
// FUN_004a6d50), so on screen that's points and panel shut. The panel
// state (FUN_0045ae90): 1 right open, 2 left, 3 both.
// ponytail: UI 0x16's cases (with the inventory / tree) aren't kept.
LevelButtons level_buttons(const d2d::d2s::Stats& stats, bool left_open, bool right_open, bool char_open, bool tree_open, bool store_open) {
    const int width = int(kScreenWidth);
    LevelButtons out;
    if ((left_open && right_open) || store_open) return out;
    if (stats.get(d2d::d2s::kStatPts) != 0 && !char_open) out.stats_x = left_open ? width / 2 + 0x28 : 0x28;
    if (stats.get(d2d::d2s::kSkillPts) > 0 && !tree_open) out.skills_x = (right_open ? width - width / 2 : width) - 0x49;
    return out;
}

bool over_stats_button(int x, int mouse_x, int mouse_y) {
    const int height = int(kScreenHeight);
    return x >= 0 && mouse_x > x && mouse_x < x + 0x22 && mouse_y > height - 0x8b && mouse_y < height - 0x66;
}

bool over_skills_button(int x, int mouse_x, int mouse_y) {
    const int height = int(kScreenHeight);
    return x >= 0 && mouse_x > x && mouse_x < x + 0x21 && mouse_y > height - 0x8a && mouse_y < height - 0x66;
}

void draw_level_buttons(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const LevelButtons& buttons, int mouse_x, int mouse_y) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const int height = int(kScreenHeight);
    auto at_bottom = [&](const d2d::dc6::Sprite& spr, int frame, int x, int bottom) {
        if (frame >= int(spr.frames_per_direction())) return;
        const auto& frame_ref = spr.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, frame_ref, pal, x, bottom - int(frame_ref.height));
    };
    // The label in the font the frame left set.
    // ponytail: taken as font16; FUN_004a6b30 sets none of its own.
    auto button = [&](int x, int label_id, bool pressed) {
        if (x < 0 || scene.level_socket.frames_per_direction() == 0) return;
        if (const auto found = lookup_string(scene, std::uint16_t(label_id))) {
            const auto label = u16_to_latin1(*found);
            const int socket_w = int(scene.level_socket.frame(0, 0).width);
            const int text_y = height - 0x8e - int(scene.font.sheet().frame(0, 0).height) + 1;
            scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x + 1 + socket_w / 2 - scene.font.measure(label) / 2, text_y, label);
        }
        at_bottom(scene.level_socket, 0, x, height - 0x69);
        at_bottom(scene.level_button, pressed ? 1 : 0, x + 3, height - 0x6d);
    };
    button(buttons.stats_x, 0xf92, buttons.stats_down && over_stats_button(buttons.stats_x, mouse_x, mouse_y));
    button(buttons.skills_x, 0xf93, buttons.skills_down && over_skills_button(buttons.skills_x, mouse_x, mouse_y));
}

std::array<int, 4> grid_rect(const Scene& scene, const Scene::InvLayout& layout, const d2d::d2s::Item& item) {
    const auto info = scene.rules.item_info.find(item.code);
    return { layout.grid_x + item.column * layout.box_w, layout.grid_y + item.row * layout.box_h,
             (info != scene.rules.item_info.end() ? info->second.width : 1) * layout.box_w,
             (info != scene.rules.item_info.end() ? info->second.height : 1) * layout.box_h };
}

// The Cube panel's bottom as game.exe anchors it: its art's top-left
// quarter ends 0xe0 above it (FUN_0048edf0).
static int cube_bottom(const Scene& scene) {
    return kCharPanelY + (scene.cube_panel.frames_per_direction() ? int(scene.cube_panel.frame(0, 0).height) : 0) + 0xe0;
}

bool transmute_at(const Scene& scene, int mouse_x, int mouse_y) {
    const int bottom = cube_bottom(scene);
    return mouse_x > kCharPanelX + 0x90 && mouse_x < kCharPanelX + 0xb8 && mouse_y > bottom - 0xdf && mouse_y < bottom - 0xb7;
}

void draw_storage(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& layout, int panel,
                  int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer, bool transmute_down) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    if (art.frames_per_direction() >= 4) {
        const auto& frame = art.frame(0, 0);
        blit_sprite(framebuffer, frame, pal, kCharPanelX, kCharPanelY);
        blit_sprite(framebuffer, art.frame(0, 1), pal, kCharPanelX + int(frame.width), kCharPanelY);
        blit_sprite(framebuffer, art.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(frame.height));
        blit_sprite(framebuffer, art.frame(0, 3), pal, kCharPanelX + int(frame.width), kCharPanelY + int(frame.height));
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& item : items) {
        if (item.location != d2d::d2s::item_location::kStored || item.panel != panel) continue;
        const auto [box_x, box_y, width, height] = grid_rect(scene, layout, item);
        if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
            const auto& frame = spr->frame(0, 0);
            blit_sprite(framebuffer, frame, scene.item_pal(item, pal), box_x + (width - int(frame.width)) / 2, box_y + (height - int(frame.height)) / 2);
        }
        if (mouse_x >= box_x && mouse_x < box_x + width && mouse_y >= box_y && mouse_y < box_y + height) { hover = &item; hover_box = { box_x, box_y, width, height }; }
    }
    // The Transmute button (miniconvert, frame 0 up / 1 held) at x + 0x90,
    // its bottom 0xbc over the panel's.
    if (panel == d2d::d2s::item_panel::kCube && scene.transmute_button.frames_per_direction() > 1) {
        const auto& frame = scene.transmute_button.frame(0, transmute_down ? 1 : 0);
        blit_sprite(framebuffer, frame, pal, kCharPanelX + 0x90, cube_bottom(scene) - 0xbc - int(frame.height) + 1);
    }
    if (hover) draw_sockets(framebuffer, scene, *hover, layout.box_w, layout.box_h, hover_box[0], hover_box[1], pal);
    if (hover) draw_hover_text(framebuffer, scene, item_lines(scene, *hover, wearer ? wearer->lvl : 1, wearer), hover_box[0], hover_box[0] + hover_box[2], hover_box[1] + hover_box[3], hover_box[1]);
}

namespace {

// The trade's boxes: {x0, y0, x1, y1}.
constexpr std::array<int, 4> kTradeAsk{ 250, 200, 550, 290 }, kTradeYes{ 270, 252, 390, 278 }, kTradeNo{ 410, 252, 530, 278 };
constexpr std::array<int, 4> kTradeAccept{ 301, 443, 335, 477 }, kTradeCancel{ 353, 443, 387, 477 }, kTheirAccept{ 353, 228, 387, 262 };
constexpr std::array<int, 4> kOurGold{ 236, 286, 386, 304 };

bool inside(const std::array<int, 4>& box, int x, int y) { return x >= box[0] && x < box[2] && y >= box[1] && y < box[3]; }

void shade(std::vector<std::uint8_t>& framebuffer, const std::array<int, 4>& box, int keep) {   // keep / 4 of the colour
    for (int y = std::max(box[1], 0); y < std::min(box[3], int(kScreenHeight)); ++y)
        for (int x = std::max(box[0], 0); x < std::min(box[2], int(kScreenWidth)); ++x)
            for (int channel = 0; channel < 3; ++channel) {
                auto& value = framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4 + std::size_t(channel)];
                value = std::uint8_t(value * keep / 4);
            }
}

} // namespace

void draw_trade(std::vector<std::uint8_t>& framebuffer, const Scene& scene, int state, const std::string& with, const std::string& our_name,
                const std::vector<d2d::d2s::Item>& theirs, const std::vector<d2d::d2s::Item>& ours, std::uint32_t their_gold,
                std::uint32_t our_gold, const std::string* typing, int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    auto centred = [&](const std::array<int, 4>& box, const std::string& text) {
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, (box[0] + box[2]) / 2 - scene.font.measure(text) / 2,
                        (box[1] + box[3]) / 2 - scene.font.line_height() / 2, text);
    };
    if (state == 1 || state == 2) {
        shade(framebuffer, kTradeAsk, 1);
        centred({ kTradeAsk[0], kTradeAsk[1] + 6, kTradeAsk[2], kTradeAsk[1] + 40 }, state == 2 ? with + " wants to trade" : "Asking " + with + " to trade");
        if (state == 2) { shade(framebuffer, kTradeYes, 2); centred(kTradeYes, "Accept"); }
        shade(framebuffer, kTradeNo, 2);
        centred(kTradeNo, state == 2 ? "Decline" : "Cancel");
        return;
    }
    draw_storage(framebuffer, scene, theirs, scene.trade_panel, scene.trade_layout[0], 100, mouse_x, mouse_y, wearer);
    draw_storage(framebuffer, scene, ours, d2d::dc6::Sprite{}, scene.trade_layout[1], 101, mouse_x, mouse_y, wearer);
    // The buttons (the trade draw after the panel, 0x48fb20): buysellbtn
    // frame 0x10 (0x11 once accepted) for ours at x+0xdf and theirs at
    // x+0x113 bottom 0x116 up from the panel's foot; cancel 10 at x+0x113;
    // ours and cancel 0x40 up. Hovered: "Accept Trade" / "Cancel".
    if (std::uint32_t(0x11) < scene.store_buttons.frames_per_direction()) {
        auto button = [&](std::uint32_t frame_index, int x, int bottom) {
            const auto& frame = scene.store_buttons.frame(0, frame_index);
            blit_sprite(framebuffer, frame, pal, x, bottom - int(frame.height) + 1);
        };
        button(state == 7 ? 0x11 : 0x10, kCharPanelX + 0xdf, int(kScreenHeight) - 60 - 0x40);
        button(state == 5 ? 0x11 : 0x10, kCharPanelX + 0x113, int(kScreenHeight) - 60 - 0x116);
        button(10, kCharPanelX + 0x113, int(kScreenHeight) - 60 - 0x40);
    }
    const bool on_accept = inside(kTradeAccept, mouse_x, mouse_y) || inside(kTheirAccept, mouse_x, mouse_y);
    if (on_accept || inside(kTradeCancel, mouse_x, mouse_y)) {
        const auto tip = string_id(scene, on_accept ? 0x1023 : 0x1022);
        scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, mouse_x - scene.font.measure(tip) / 2, mouse_y - 30, tip);
    }
    // The bars over each grid (the art's): their name and gold; ours.
    // ponytail: placed by eye on the art, not traced.
    centred({ 98, 70, 222, 88 }, with);
    if (their_gold) centred({ 236, 70, 386, 88 }, std::to_string(their_gold));
    centred({ 98, 286, 222, 304 }, our_name);
    if (typing) centred(kOurGold, *typing + "_");
    else if (our_gold) centred(kOurGold, std::to_string(our_gold));
}

TradeClick trade_click(const Scene& scene, int state, int mouse_x, int mouse_y) {
    if (state == 1 || state == 2) {
        if (state == 2 && inside(kTradeYes, mouse_x, mouse_y)) return TradeClick::kAccept;
        return inside(kTradeNo, mouse_x, mouse_y) ? TradeClick::kDecline : TradeClick::kNone;
    }
    if (inside(kTradeAccept, mouse_x, mouse_y)) return TradeClick::kAccept;
    if (inside(kTradeCancel, mouse_x, mouse_y)) return TradeClick::kDecline;
    if (inside(kOurGold, mouse_x, mouse_y)) return TradeClick::kGold;
    const auto& ours = scene.trade_layout[1];
    if (mouse_x >= ours.grid_x && mouse_x < ours.grid_x + ours.cols * ours.box_w && mouse_y >= ours.grid_y && mouse_y < ours.grid_y + ours.rows * ours.box_h)
        return TradeClick::kOurGrid;
    return TradeClick::kNone;
}

std::vector<std::array<int, 4>> trade_boxes(const Scene& scene, int state) {
    if (state == 1) return { kTradeNo };
    if (state == 2) return { kTradeYes, kTradeNo };
    std::vector<std::array<int, 4>> out = { kTradeAccept, kTradeCancel, kOurGold };
    const auto& ours = scene.trade_layout[1];
    for (int row = 0; row < ours.rows; ++row)
        for (int col = 0; col < ours.cols; ++col) {
            const int x = ours.grid_x + col * ours.box_w, y = ours.grid_y + row * ours.box_h;
            out.push_back({ x, y, x + ours.box_w, y + ours.box_h });
        }
    return out;
}

void draw_belt(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
               int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer, bool popup) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto& belt = scene.belts[std::size_t(belt_index(scene, items))];
    const int rows = belt.boxes / 4;
    if (popup && scene.popbelt.frames_per_direction() > 0) {
        const auto& frame = scene.popbelt.frame(0, 0);
        for (int i = 0; i + 1 < rows; ++i)
            blit_sprite(framebuffer, frame, pal, int(kScreenWidth) / 2 + 21, int(kScreenHeight) - 41 - 32 * i - int(frame.height) + 1);
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& item : items) {
        if (item.location != d2d::d2s::item_location::kBelt || item.column < 0 || item.column >= belt.boxes || (!popup && item.column > 3)) continue;
        const auto& box = belt.box[std::size_t(item.column)];
        if (box[1] <= box[0]) continue;
        if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
            const auto& frame = spr->frame(0, 0);
            blit_sprite(framebuffer, frame, scene.item_pal(item, pal), box[0] + (box[1] - box[0] + 1 - int(frame.width)) / 2,
                        box[2] + (box[3] - box[2] + 1 - int(frame.height)) / 2);
        }
        if (mouse_x >= box[0] && mouse_x <= box[1] && mouse_y >= box[2] && mouse_y <= box[3]) { hover = &item; hover_box = box; }
    }
    if (hover) draw_hover_text(framebuffer, scene, item_lines(scene, *hover, wearer ? wearer->lvl : 1, wearer), hover_box[0], hover_box[1] + 1, hover_box[3] + 1, hover_box[2]);
}

int automap_cel(const Scene& scene, const Level& level, int orientation, int main, int sub, std::uint32_t hash) {
    const auto key = [](const Scene::AutomapRule& rule) { return std::pair(rule.level_type, rule.orientation); };
    for (const auto& rule : std::ranges::equal_range(scene.automap_rules, std::pair(level.type, orientation), {}, key)) {
        if (rule.main >= 0 && rule.main != main) continue;
        if (rule.sub0 >= 0 && (sub < rule.sub0 || sub > rule.sub1)) continue;
        return rule.cels[hash % rule.cels.size()];
    }
    return -1;
}

bool Automap::add(const Cell& cell) {
    int group = -1;
    for (const auto& pair : kCelGroups)
        if (pair[0] == cell.cel) group = pair[1];
    // ponytail: matches against every cell at (x, y); the tree only meets those on its search path.
    const auto found = placed.lower_bound({ cell.list, cell.y, cell.x, -1 });
    const bool taken = group < 0 ? found != placed.end() && std::get<0>(*found) == cell.list && std::get<1>(*found) == cell.y && std::get<2>(*found) == cell.x
                                 : placed.count({ cell.list, cell.y, cell.x, group }) != 0;
    if (taken) return false;
    placed.insert({ cell.list, cell.y, cell.x, group });
    cells.push_back(cell);
    return true;
}

void load_automap(Automap& automap, const std::filesystem::path& dir, const std::string& name, std::uint32_t map_seed, int layer, int cel_count) {
    if (dir.empty() || name.empty()) return;
    const auto file = d2d::d2s::automap_file(dir, name, map_seed);
    if (file.empty()) return;
    const auto loaded = d2d::d2s::read_automap(file, layer, game::object_seed(map_seed).low,
                                                { cel_count > 0 ? cel_count : -1, -1, -1, -1 });   // no MaxiMap: unchecked (else every record cut); no miniatures: list 3 unchecked
    for (std::size_t list = 0; list < loaded.lists.size(); ++list)
        for (const auto& cell : loaded.lists[list]) automap.add({ cell.cel, cell.x, cell.y, int(list), true });
}

void save_automap(Automap& automap, const std::filesystem::path& dir, const std::string& name, std::uint32_t map_seed, int layer) {
    if (dir.empty() || name.empty()) return;
    d2d::d2s::AutomapLayer fresh;
    fresh.object_seed = game::object_seed(map_seed).low;
    for (const auto& cell : automap.cells)
        if (!cell.saved) fresh.lists[std::size_t(cell.list)].push_back({ std::int16_t(cell.cel), std::int16_t(cell.x), std::int16_t(cell.y) });
    if (std::all_of(fresh.lists.begin(), fresh.lists.end(), [](const auto& list) { return list.empty(); })) return;
    const auto file = d2d::d2s::automap_file(dir, name, map_seed);
    if (file.empty() || !d2d::d2s::append_automap(file, layer, fresh)) return;
    for (auto& cell : automap.cells) cell.saved = true;
}

// game.exe adds the tiles drawn on screen (flag 0x20000) of the player's
// room and its near rooms each time the player has moved 80 px
// (FUN_00459020); d2d the tiles render_world drew (g_drawn), each by its
// own draw: a cell's floors, walls and roof come in apart. A cel already
// placed isn't added again (Automap::add).
void automap_reveal(const Scene& scene, const Level& level, Automap& automap, std::span<const DrawnCell> cells,
                    int shift_x, int shift_y) {
    const auto& map = level.ds1;
    const int width = int(map.width()), height = int(map.height());
    if (width == 0) return;
    for (const auto& drawn : cells) {
        const int tile_x = drawn.x - shift_x, tile_y = drawn.y - shift_y;
        if (tile_x < 0 || tile_y < 0 || tile_x >= width || tile_y >= height) continue;
        const auto index = std::size_t(tile_y * width + tile_x);
        const auto wanted = [&](const void* source) { return !drawn.source || drawn.source == source; };
        const int act_x = tile_x + level.world_x, act_y = tile_y + level.world_y;     // act tiles
        const std::uint32_t hash = std::uint32_t(act_x) * 73856093u ^ std::uint32_t(act_y) * 19349663u;   // unsigned: wraps, not UB
        const int automap_x = (act_x - act_y) * 80 / 10, automap_y = (act_x + act_y) * 40 / 10;
        // A generated level's tiles are its picks (a maze's DS1 is blank):
        // FUN_00458f40 adds the room's floors, then its walls, each
        // not hidden (flag 8); FUN_00457cf0 looks the cel up by the
        // DT1 tile's orientation, main and sub index (+0x14, +0x18,
        // +0x1c); a lower wall (the room tile's orientation past 15) sits 24 down.
        if (!level.shown().empty()) {
            for (const auto& pick : level.shown()[index]) {
                if (pick.layer > 1 || pick.hidden || !wanted(&pick)) continue;
                if (const int cel = automap_cel(scene, level, pick.tile->type, pick.tile->style, pick.tile->sequence, hash); cel >= 0)
                    automap.add({ cel, automap_x, automap_y + (pick.orient > 15 ? 24 : 0), pick.layer == 1 ? 0 : 1 });
            }
            continue;
        }
        for (const auto& layer : map.floors()) {
            const auto& tile = layer.cells[index];
            if (tile.hidden || !(tile.prop1 & 2) || !wanted(&tile)) continue;
            if (const int cel = automap_cel(scene, level, 0, tile.style, tile.sequence, hash); cel >= 0) automap.add({ cel, automap_x, automap_y, 0 });
        }
        for (const auto& layer : map.walls()) {
            const auto& tile = layer.cells[index];
            if (tile.hidden || tile.wall_type == 0 || !wanted(&tile)) continue;
            if (const int cel = automap_cel(scene, level, tile.wall_type, tile.style, tile.sequence, hash); cel >= 0)
                automap.add({ cel, automap_x, automap_y + (tile.wall_type > 15 ? 24 : 0), 1 });
        }
    }
}

// FUN_00458dc0 over a room's units: an object the view draws (unit flag
// 0x10000000, FUN_004dc7b0) with an Objects.txt AutoMap cel adds it once
// (flag 0x20000000), at its world px / 10 + (1, -3) (FUN_00457e80): the
// bank (267) only in Acts 3 and 4, the sewer stairs (366) when ON, the
// waypoint 402 only in the Arcane Sanctuary. The view draws the units of
// the player's room and its near rooms whose box meets the view's, those
// in its unit grid (rules::room_in_view, rules::in_unit_grid).
// ponytail: monsters' MonStats2 cels aren't added (no 1.14d row has one).
void automap_icons(const GameData& game_data, const Level& level, Automap& automap, float player_x, float player_y,
                   const std::vector<UnitState>& states) {
    constexpr int kWidth = int(kScreenWidth), kViewHeight = int(kScreenHeight) - 40;   // the view (FUN_00476000)
    const float player_act_x = player_x + float(level.world_x), player_act_y = player_y + float(level.world_y);
    const int view_x = int(std::lround((player_act_x - player_act_y) * 80)) - kWidth / 2;   // FUN_0044c990
    const int view_y = int(std::lround((player_act_x + player_act_y) * 40)) - kViewHeight / 2;
    auto rooms = game::missile_rooms(game_data, level, player_x, player_y);   // act subtiles: the player's room, then its near list
    if (rooms.empty()) return;
    rooms.erase(rooms.begin());
    std::erase_if(rooms, [&](const auto& room) {
        return !d2d::rules::room_in_view(view_x, view_y, kWidth, kViewHeight, room[0] / 5, room[1] / 5, room[2] / 5, room[3] / 5);
    });
    const auto add_icons = [&](const Level& owner, const std::vector<UnitState>& owner_states) {
        namespace ids = d2d::rules::object_ids;
        const int act = d2d::rules::level_ids::act_of(owner.id);
        for (std::size_t slot = 0; slot < owner.npcs.size(); ++slot) {
            const auto& npc = owner.npcs[slot];
            if (npc.root != "objects" || npc.automap <= 0) continue;
            const auto* state = slot < owner_states.size() ? &owner_states[slot] : nullptr;
            if (state && state->hidden) continue;
            const float x = state ? state->x : npc.x, y = state ? state->y : npc.y;
            const std::string_view mode = state && !state->mode.empty() ? state->mode : std::string_view(npc.mode);
            if (npc.object_id == ids::kBank && act != 2 && act != 3) continue;
            if (npc.object_id == ids::kSewerStairs && mode != "ON") continue;
            if (npc.object_id == ids::kSanctuaryWaypoint && owner.id != d2d::rules::level_ids::kArcaneSanctuary) continue;
            const float act_x = x + float(owner.world_x), act_y = y + float(owner.world_y);
            const int subtile_x = int(std::floor(act_x * 5)), subtile_y = int(std::floor(act_y * 5));
            if (std::ranges::none_of(rooms, [&](const auto& room) {
                    return subtile_x >= room[0] && subtile_y >= room[1] && subtile_x < room[0] + room[2] && subtile_y < room[1] + room[3];
                })) continue;
            const int pixel_x = int(std::lround((act_x - act_y) * 80)), pixel_y = int(std::lround((act_x + act_y) * 40));
            if (!d2d::rules::in_unit_grid(view_x, view_y, kWidth, kViewHeight, pixel_x, pixel_y)) continue;
            automap.add({ npc.automap, pixel_x / 10 + 1, pixel_y / 10 - 3, 2 });
        }
    };
    add_icons(level, states);
    for (const auto& neighbour : level.nearby)   // a near room can be the next level's
        if (neighbour.level->layer == level.layer) add_icons(*neighbour.level, {});
}

// The automap (FUN_00459700 / FUN_00459440 / FUN_0045a860): its cels
// scrolled round the player, full screen or (mini) at half size in a
// third of the screen's corner, faded by the option, then the marks.
void draw_automap(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Automap& automap, float player_x, float player_y) {
    const bool mini = automap.mini == 1;
    const auto& cels = mini && scene.automap_cels_small.frames_per_direction() > 0 ? scene.automap_cels_small : scene.automap_cels;
    if (!automap.open || cels.frames_per_direction() == 0) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    constexpr int kWidth = int(kScreenWidth), kHeight = int(kScreenHeight);
    const int divisor = mini ? 20 : 10;
    // A panel open on one side: the full map moves a quarter over, the mini
    // one goes to the other corner (FUN_00459700).
    const bool on_left = automap.panel_side == 1 ? true : automap.panel_side == 2 ? false : automap.left;
    const int shift = mini ? 0 : automap.panel_side == 1 ? -kWidth / 4 : automap.panel_side == 2 ? kWidth / 4 : 0;
    // The player's cell on their feet, full size (rules/automap_scroll.hpp).
    const auto [scroll_x, scroll_y] = d2d::rules::automap_scroll(int(std::lround((player_x - player_y) * 80)), int(std::lround((player_x + player_y) * 40)),
                                                                 kWidth, kHeight, mini, on_left, automap.portraits, automap.panel_side);
    // The cels' clip: the screen, or the mini map's box (FUN_00459440).
    const int clip_x0 = !mini ? 0 : on_left ? 0 : kWidth - 0x119;
    const int clip_y0 = !mini ? 0 : on_left ? (automap.portraits ? 0x60 : 0) - 0x15 : 0x39;
    const int clip_x1 = !mini ? kWidth - 1 : clip_x0 + 0x117, clip_y1 = !mini ? kHeight - 1 : clip_y0 + 0xe1;
    // Portraits off, the box pokes 0x15 above the screen; D2GFX clips its
    // draws to the screen, so only the pixel writes stop at row 0.
    const int draw_y0 = std::max(clip_y0, 0);
    // The fade (FUN_004576c0's mode; mini takes Centre as Everything):
    // draw modes 0 / 1 / 2 (a quarter, half, three quarters seen), 5 whole.
    const int fade = mini && automap.fade == 1 ? 2 : automap.fade;
    const auto mode_of = [&](int x, int y) {
        if (fade == 2) return 1;
        if (fade == 3) return automap.in_town ? (mini ? 1 : 2) : 5;
        if (fade != 1 || mini) return 5;
        const int centre_x = kWidth / 2 + shift, centre_y = (kHeight - 40) / 2;   // 0x7a5220 / 2, 0x7a521c / 2
        if (x < centre_x - 0x8c || x > centre_x + 0x8c || y < centre_y - 0x96 || y > centre_y + 0x82) return 5;
        const int across = std::abs(centre_x - x), down = std::abs(centre_y - 10 - y);
        const int distance = (down < across ? down + across * 2 : across + down * 2) / 2;
        return distance >= 0x96 ? 5 : distance < 50 ? 0 : distance < 100 ? 1 : 2;
    };
    for (int list = 0; list < 3; ++list)                            // a pass a list, as FUN_00459700
    for (const auto& cell : automap.cells) {
        if (cell.list != list || cell.cel < 0 || std::uint32_t(cell.cel) >= cels.frames_per_direction()) continue;   // list 3, the miniatures: another cel file
        const auto& frame = cels.frame(0, std::uint32_t(cell.cel));
        const int x = cell.x * 10 / divisor - scroll_x, bottom = cell.y * 10 / divisor - scroll_y;
        const int top = bottom - int(frame.height) + 1;
        if (x > clip_x1 || x + int(frame.width) < clip_x0 || bottom < clip_y0 || top > clip_y1) continue;
        const int mode = mode_of(x, bottom);
        const int seen = mode == 5 ? 4 : mode + 1;                   // quarters of the cel over what's there
        for (int row = 0; row < int(frame.height); ++row)
            for (int column = 0; column < int(frame.width); ++column) {
                const int pixel_x = x + column, pixel_y = top + row;
                const auto index = frame.pixels[std::size_t(row) * frame.width + std::size_t(column)];
                if (index == 0 || pixel_x < clip_x0 || pixel_x > clip_x1 || pixel_y < draw_y0 || pixel_y > clip_y1) continue;
                const auto colour = pal[index];
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                pixel[0] = std::uint8_t((pixel[0] * (4 - seen) + colour.r * seen) / 4);
                pixel[1] = std::uint8_t((pixel[1] * (4 - seen) + colour.g * seen) / 4);
                pixel[2] = std::uint8_t((pixel[2] * (4 - seen) + colour.b * seen) / 4);
            }
    }
    // The marks (FUN_0045a860 -> FUN_0045a7f0): the 13-point shape at
    // 0x6d6638 doubled, at (unit px / div - scroll + 8, py / div - scroll
    // - 8), in the palette colour nearest FUN_004fb180(r, g, b) of each
    // mark's (Town::automap_frame picks them as FUN_00459bc0 does). Names
    // in font6, centred, 10 above the mark (FUN_0045a760).
    const auto nearest = [&](int want_r, int want_g, int want_b) {
        const auto colour = pal[pal.nearest(want_r, want_g, want_b)];
        return std::array<std::uint8_t, 3>{ colour.r, colour.g, colour.b };
    };
    const auto mark = [&](float unit_x, float unit_y, const std::array<std::uint8_t, 3>& colour) {
        const int marker_x = int(std::lround((unit_x - unit_y) * 80 / float(divisor))) - scroll_x + 8;
        const int marker_y = int(std::lround((unit_x + unit_y) * 40 / float(divisor))) - scroll_y - 8;
        if (marker_x < clip_x0 - 8 || marker_x > clip_x1 || marker_y < clip_y0 || marker_y > clip_y1) return std::pair{ -1, -1 };
        auto plot = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
            auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
            pixel[0] = colour[0]; pixel[1] = colour[1]; pixel[2] = colour[2];
        };
        for (int i = 0; i + 1 < 13; ++i) {                   // Bresenham, like a D2GFX line
            int left = marker_x + kMark[i][0] * 2, top = marker_y + kMark[i][1] * 2;
            const int right = marker_x + kMark[i + 1][0] * 2, bottom = marker_y + kMark[i + 1][1] * 2;
            const int dx = std::abs(right - left), dy = -std::abs(bottom - top), step_x = left < right ? 1 : -1, step_y = top < bottom ? 1 : -1;
            for (int err = dx + dy;;) {
                plot(left, top);
                if (left == right && top == bottom) break;
                const int doubled_error = 2 * err;
                if (doubled_error >= dy) { err += dy; left += step_x; }
                if (doubled_error <= dx) { err += dx; top += step_y; }
            }
        }
        return std::pair{ marker_x, marker_y };
    };
    const auto& name_font = scene.font_tiny.line_height() > 0 ? scene.font_tiny : scene.font;
    for (const auto& other : automap.marks) {
        const auto [x, y] = mark(other.x, other.y, nearest(other.rgb[0], other.rgb[1], other.rgb[2]));
        if (x < 0 || other.name.empty()) continue;
        name_font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x - name_font.measure(other.name) / 2, y - 10 - name_font.line_height() + 1,
                              other.name, other.name_rgb[0], other.name_rgb[1], other.name_rgb[2]);
    }
    mark(player_x, player_y, nearest(0, 0, 0xff));                  // you: blue
    // The top right's lines (FUN_0045ad60): font16 in gold, right-aligned
    // 16 in from the edge, baselines from 0x18 every 0x10.
    int baseline = 0x18;
    for (const auto& line : automap.lines) {
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, kWidth - scene.font.measure(line) - 0x10, baseline - scene.font.line_height() + 1,
                               line, 199, 179, 119);
        baseline += 0x10;
    }
}

bool waypoint_act_open(const d2d::d2s::Header& header, int act) {
    static constexpr int kQuest[5] = { -1, 7, 15, 23, 26 };
    return act == 0 || header.quest_flag(header.active_difficulty(), kQuest[act], 0);
}

int waypoint_row_at(const Scene& scene, const WaypointUI& waypoints, const d2d::d2s::Header& header, int mouse_x, int mouse_y) {
    const auto& rows = scene.waypoint_levels[std::size_t(waypoints.tab)];
    for (std::size_t i = 0; i < rows.size() && i < 9; ++i) {
        if (!header.waypoint(header.active_difficulty(), rows[i].waypoint)) continue;
        const int x = kCharPanelX + 17, y = 60 + kWpHitTop[i];
        if (mouse_x > x && mouse_x < x + 280 && mouse_y > y && mouse_y < y + 30) return int(i);
    }
    return -1;
}

int waypoint_tab_at(const d2d::d2s::Header& header, bool expansion, int mouse_x, int mouse_y) {
    const int x = mouse_x - kCharPanelX, y = mouse_y - 60;
    if (y > 30 || x < 0 || x > 320) return -1;
    const int tab = std::min(x / (expansion ? 64 : 80), expansion ? 4 : 3);
    return waypoint_act_open(header, tab) ? tab : -1;
}

void draw_waypoints(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const WaypointUI& waypoints,
                    const d2d::d2s::Header& header, bool expansion, int current_level, int mouse_x, int mouse_y) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    if (scene.wp_bg.frames_per_direction() >= 4) {
        const auto& frame = scene.wp_bg.frame(0, 0);
        blit_sprite(framebuffer, frame, pal, kCharPanelX, kCharPanelY);
        blit_sprite(framebuffer, scene.wp_bg.frame(0, 1), pal, kCharPanelX + int(frame.width), kCharPanelY);
        blit_sprite(framebuffer, scene.wp_bg.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(frame.height));
        blit_sprite(framebuffer, scene.wp_bg.frame(0, 3), pal, kCharPanelX + int(frame.width), kCharPanelY + int(frame.height));
    }
    static constexpr int kTabX[2][5] = { { 3, 81, 159, 237, 0 }, { 3, 67, 129, 191, 253 } };
    const auto& tabs = scene.wp_tabs[expansion ? 1 : 0];
    for (int i = 0; i < (expansion ? 5 : 4); ++i) {
        if (i != waypoints.tab && !waypoint_act_open(header, i)) continue;
        const auto frame_index = std::uint32_t(i * 2 + (i == waypoints.tab ? 0 : 1));
        if (frame_index >= tabs.frames_per_direction()) continue;
        const auto& frame = tabs.frame(0, frame_index);
        blit_sprite(framebuffer, frame, pal, kCharPanelX + kTabX[expansion ? 1 : 0][i], 60 + 34 - int(frame.height) + 1);
    }
    const int cell = scene.font.sheet().frames_per_direction() > 0 ? int(scene.font.sheet().frame(0, 0).height) : 16;
    auto text = [&](int x, int baseline, const std::string& label, std::array<std::uint8_t, 3> colour) {
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, baseline - cell + 1, label, colour[0], colour[1], colour[2]);
    };
    const int diff = header.active_difficulty();
    const auto& rows = scene.waypoint_levels[std::size_t(waypoints.tab)];
    const int hover = waypoint_row_at(scene, waypoints, header, mouse_x, mouse_y);
    bool others = false;
    for (const auto& act_levels : scene.waypoint_levels)
        for (const auto& level_row : act_levels) others |= level_row.level != current_level && header.waypoint(diff, level_row.waypoint);
    for (std::size_t i = 0; i < rows.size() && i < 9; ++i) {
        const bool active = header.waypoint(diff, rows[i].waypoint), here = rows[i].level == current_level;
        const int frame_index = here ? 0 : active ? 3 + (hover == int(i)) : -1;
        if (frame_index >= 0 && std::uint32_t(frame_index) < scene.wp_icons.frames_per_direction()) {
            const auto& frame = scene.wp_icons.frame(0, std::uint32_t(frame_index));
            blit_sprite(framebuffer, frame, pal, kCharPanelX + 17, 60 + kWpIconBottom[i] - int(frame.height) + 1);
        }
        const auto name = lookup_string(scene, std::string_view(rows[i].name));
        text(kCharPanelX + 80, 60 + kWpTextBase[i], name ? u16_to_latin1(*name) : rows[i].name,
             !active ? kTxtGrey : here || hover == int(i) ? kTxtBlue : kTxtWhite);
    }
    const auto title = string_id(scene, others ? 0xf96 : 0xf97);
    text(kCharPanelX + 160 - scene.font.measure(title) / 2, 108, title, kTxtWhite);
    if (std::uint32_t(11) < scene.store_buttons.frames_per_direction()) {
        const auto& frame = scene.store_buttons.frame(0, waypoints.cancel_down ? 11 : 10);
        blit_sprite(framebuffer, frame, pal, kCharPanelX + 0x111, 477 - int(frame.height) + 1);
    }
    if (mouse_x - (kCharPanelX + 0x111) >= 0 && mouse_x - (kCharPanelX + 0x111) < 0x24 && mouse_y - 0x183 - 60 >= 0 && mouse_y - 0x183 - 60 < 0x22) {
        const auto cancel = string_id(scene, 0x1022);
        const int width = scene.font.measure(cancel);
        draw_hover_text(framebuffer, scene, { { cancel, kTxtWhite } }, kCharPanelX + 0x126 - width / 2, kCharPanelX + 0x126 + width / 2, 0x183 + 60, 0x172 + 60);
    }
}

QuestText quest_log_read(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, int quest, const QuestState& quest_state, bool& fresh) {
    const auto text = quest_text(quest_bits, quest, quest_state);
    auto& last = quest_log.last_state[std::size_t(quest)];
    fresh = text.marks && text.state != last;
    if (text.state >= 0) last = text.state;
    return text;
}

// FUN_004a3220: the tab's quests in the table's order, read as the panel
// draws them, then the pick.
static void quest_log_select(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state, bool reset) {
    std::vector<d2d::rules::QuestLogPick> entries;
    for (const auto& entry : kQuestLog) {
        if (entry.act != quest_log.act) continue;
        bool fresh = false;
        const int shown = quest_log_read(quest_log, quest_bits, entry.quest, quest_state, fresh).shown;
        entries.push_back({ entry.slot, shown, fresh });
    }
    const auto act = std::size_t(quest_log.act);
    quest_log.slot = d2d::rules::quest_log_pick(entries, quest_log.pending[act], quest_log.remembered[act]);
    if (reset) quest_log.pending.fill(-1);
}

// The tab's done animations still to play count as seen (FUN_004a2760).
static void quest_log_mark_seen(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state) {
    for (const auto& entry : kQuestLog)
        if (entry.act == quest_log.act && quest_text(quest_bits, entry.quest, quest_state).shown == 0) quest_log.seen[std::size_t(entry.quest)] = true;
}

void quest_log_open(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state, int player_act) {
    quest_log.open = true;
    quest_log.notified = false;
    quest_log.act = d2d::rules::quest_log_act(quest_bits, player_act);
    quest_log.frame.fill(0);
    quest_log.frame_ms.fill(0);
    quest_log.pressed = -1;
    quest_log.close_down = quest_log.last_down = false;
    quest_log_select(quest_log, quest_bits, quest_state, false);
}

void quest_log_close(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state) {
    if (!quest_log.open) return;
    quest_log_mark_seen(quest_log, quest_bits, quest_state);
    quest_log.open = false;
    quest_log.pressed = -1;
    quest_log.close_down = quest_log.last_down = false;
}

void quest_log_tab(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state, int act, bool expansion) {
    if (act < 0 || act >= 5 || act == quest_log.act || (act == 4 && !expansion)) return;
    quest_log_mark_seen(quest_log, quest_bits, quest_state);
    quest_log.act = d2d::rules::quest_log_act(quest_bits, act);   // FUN_004a2220
    quest_log.frame.fill(0);
    quest_log.frame_ms.fill(0);
    quest_log_select(quest_log, quest_bits, quest_state, true);
    quest_log.pressed = -1;                   // FUN_004a2390
    quest_log.close_down = quest_log.last_down = false;
}

void quest_log_notify(QuestLog& quest_log, int quest) {
    const auto found = std::ranges::find(kQuestLog, quest, &QuestEntry::quest);
    if (found == kQuestLog.end()) return;
    if (!quest_log.open) {
        quest_log.button = quest_log.notified = true;
        quest_log.pending[std::size_t(found->act)] = found->slot;     // FUN_004a2c30
    } else if (found->act == quest_log.act) {
        quest_log.slot = found->slot;
    }
}

bool draw_quest_log(std::vector<std::uint8_t>& framebuffer, const Scene& scene, QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits,
                    const QuestState& quest_state, std::uint32_t now_ms, bool expansion, int mouse_x, int mouse_y) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto& font = scene.font_formal11.line_height() > 0 ? scene.font_formal11 : scene.font;   // font 8
    const int panel_x = kCharPanelX, panel_y = kCharPanelY;
    bool sound = false;
    auto bottom = [&](const d2d::dc6::Sprite& sprite, std::uint32_t frame, int x, int y) {   // DC6s draw up from their bottom-left
        if (frame >= sprite.frames_per_direction()) return;
        const auto& frame_ref = sprite.frame(0, frame);
        blit_sprite(framebuffer, frame_ref, pal, panel_x + x, panel_y + y - int(frame_ref.height));
    };
    bottom(scene.quest_bg, 0, 0, 256); bottom(scene.quest_bg, 1, 256, 256);
    bottom(scene.quest_bg, 2, 0, 432); bottom(scene.quest_bg, 3, 256, 432);
    // The tabs: the open one's frame 2 x act, the others' + 1, an act not
    // yet reached none.
    const bool classic_tabs = !expansion && scene.quest_tabs_classic.frames_per_direction() > 0;
    const auto& tabs = classic_tabs ? scene.quest_tabs_classic : scene.quest_tabs;
    for (int act = 0; act < (expansion ? 5 : 4); ++act) {
        const int x = expansion ? kQuestTabX[std::size_t(act)] : kQuestTabXClassic[std::size_t(act)];
        if (act == quest_log.act) bottom(tabs, std::uint32_t(act * 2), x, expansion ? 33 : 32);
        else if (d2d::rules::quest_act_open(quest_bits, act, expansion)) bottom(tabs, std::uint32_t(act * 2 + 1), x, expansion ? 33 : 32);
    }
    // Each quest's icon by `shown`: 2 frame 26 (not started); 3 frame 0,
    // 25 while held; 1 frame 24, its questdone plate (by its icon) a row up
    // while held; 0 the done animation. Its socket under it, frame 1 and a
    // row up when picked.
    QuestText picked;
    const QuestEntry* sel = nullptr;
    for (const auto& entry : kQuestLog) {
        if (entry.act != quest_log.act) continue;
        const auto& slot = kQuestSlot[std::size_t(entry.slot)];
        bool fresh = false;
        const auto text = quest_log_read(quest_log, quest_bits, entry.quest, quest_state, fresh);
        const bool held = entry.slot == quest_log.pressed;
        const auto& icon = scene.quest_icons[std::size_t(entry.icon)];
        const auto quest_index = std::size_t(entry.quest);
        if (text.shown == 0 && !quest_log.seen[quest_index]) {   // the done animation
            if (!quest_log.frame_ms[quest_index]) quest_log.frame_ms[quest_index] = now_ms;
            if (now_ms - quest_log.frame_ms[quest_index] > 100) {
                quest_log.frame_ms[quest_index] = now_ms;
                if (++quest_log.frame[quest_index] == 1) sound = true;
            }
            if (quest_log.frame[quest_index] > 24) { quest_log.frame[quest_index] = 24; quest_log.seen[quest_index] = true; }
            bottom(icon, std::uint32_t(quest_log.frame[quest_index]), slot.x, slot.y);
        } else if (text.shown <= 1) {
            if (held) bottom(scene.quest_done, std::uint32_t(entry.icon), slot.x, slot.y - 1);
            else bottom(icon, 24, slot.x, slot.y);
        } else {
            bottom(icon, text.shown == 2 ? 26u : held ? 25u : 0u, slot.x, slot.y);
        }
        const bool selected = entry.slot == quest_log.slot;
        bottom(scene.quest_sockets, selected ? 1u : 0u, slot.x - 4, slot.y + 5 - (selected ? 1 : 0));
        if (selected && text.shown != 2) { sel = &entry; picked = text; }
    }
    if (std::uint32_t(11) < scene.store_buttons.frames_per_direction()) bottom(scene.store_buttons, quest_log.close_down ? 11u : 10u, 0x116, 422);
    bottom(scene.quest_last, quest_log.last_down ? 1u : 0u, 0xe2, 422);
    // The buttons' hover texts (FUN_00502280, centred, bottom 0x5f above the panel's).
    if (const int button = quest_button_at(mouse_x, mouse_y); button) {
        const int centre = panel_x + (button == 1 ? 0x128 : 0xf1), hover_bottom = panel_y + 480 - 0x5f;
        draw_hover_text(framebuffer, scene, { { string_id(scene, button == 1 ? 0x1030 : 0xe88), kTxtWhite } }, centre, centre, hover_bottom, hover_bottom);
    }
    if (!sel) return sound;
    // The name centred at 160, the lines from x 16, wrapped to 270 (FUN_00502970).
    auto line = [&](const std::string& text, int x, int y) { font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, panel_x + x, panel_y + y - font.line_height(), text); };
    const auto name = string_id(scene, std::uint16_t(sel->name));
    line(name, 0xa0 - font.measure(name) / 2, 248);
    if (picked.string) {
        std::string text = string_id(scene, std::uint16_t(picked.string)), row;
        if (picked.count >= 0) text += std::to_string(picked.count);
        int y = 270;
        std::size_t start = 0;
        while (start < text.size()) {
            const auto end = std::min(text.find(' ', start), text.size());
            const std::string word = text.substr(start, end - start);
            if (!row.empty() && font.measure(row + " " + word) > 270) { line(row, 16, y); y += 20; row.clear(); }
            row += (row.empty() ? "" : " ") + word;
            start = end + 1;
        }
        if (!row.empty()) line(row, 16, y);
    }
    return sound;
}

QuestLogButton quest_log_button(const QuestLog& quest_log, bool left_open, bool right_open, bool char_open) {
    if (!quest_log.button || quest_log.open || (left_open && right_open)) return {};
    const int width = int(kScreenWidth), height = int(kScreenHeight);
    const bool low = left_open && char_open;            // rows 1 / 3, else 2 (FUN_004a2900)
    const int button_x = left_open ? width / 2 + 0x28 : 0x28;
    return { button_x, button_x + 0x23, height - (low ? 0x8c : 0xc3), height - (low ? 0x69 : 0xa0), height - (low ? 0x8f : 0xc6) };
}

void draw_quest_log_button(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const QuestLogButton& button, bool held, int mouse_x, int mouse_y) {
    if (button.x0 < 0 || scene.level_socket.frames_per_direction() == 0) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    auto at_bottom = [&](const d2d::dc6::Sprite& sprite, int frame, int x, int bottom_y) {
        if (frame >= int(sprite.frames_per_direction())) return;
        const auto& frame_ref = sprite.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, frame_ref, pal, x, bottom_y - int(frame_ref.height));
    };
    // ponytail: the label in font16, as the level buttons' (the frame's font).
    const auto label = string_id(scene, 0xf58);
    const int socket_w = int(scene.level_socket.frame(0, 0).width);
    scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, button.x0 + 1 + socket_w / 2 - scene.font.measure(label) / 2,
                    button.label - int(scene.font.sheet().frame(0, 0).height) + 1, label);
    at_bottom(scene.level_socket, 0, button.x0, button.y1);
    at_bottom(scene.level_button, held && over_quest_log_button(button, mouse_x, mouse_y) ? 1 : 0, button.x0 + 3, button.y1 - 4);
}

}  // namespace d2d::client
