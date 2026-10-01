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
#include <quests.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::client {

void draw_inventory(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Scene::InvLayout& layout,
                    const std::vector<d2d::d2s::Item>& items, int mouse_x , int mouse_y , int clvl ,
                    const std::function<std::string(const d2d::d2s::Item&)>* price) {
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    // invchar6.dc6 holds two 2x2 panels (256+64 wide, 256+176 tall);
    // frames 4..7 are the inventory, 0..3 the character-stats page.
    if (scene.inv_panel.frames_per_direction() >= 8) {
        const auto& frame = scene.inv_panel.frame(0, 4);
        blit_sprite(framebuffer, frame, pal, layout.panel_x, layout.panel_y);
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 5), pal, layout.panel_x + int(frame.width), layout.panel_y);
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 6), pal, layout.panel_x, layout.panel_y + int(frame.height));
        blit_sprite(framebuffer, scene.inv_panel.frame(0, 7), pal, layout.panel_x + int(frame.width), layout.panel_y + int(frame.height));
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
        if (item.location == 0 && item.panel == 1) {
            const auto info = scene.rules.item_info.find(item.code);
            const int item_width = info != scene.rules.item_info.end() ? info->second.width : 1;
            const int item_height = info != scene.rules.item_info.end() ? info->second.height : 1;
            rect = { layout.grid_x + item.column * layout.box_w, layout.grid_y + item.row * layout.box_h, item_width * layout.box_w, item_height * layout.box_h };
        } else if (item.location == 1 && item.slot >= 1 && item.slot <= 10) {
            rect = layout.slots[std::size_t(item.slot)];
        }
        if (rect[2] <= 0) continue;
        draw_in(item, rect[0], rect[1], rect[2], rect[3]);
        if (mouse_x >= rect[0] && mouse_x < rect[0] + rect[2] && mouse_y >= rect[1] && mouse_y < rect[1] + rect[3]) { hover = &item; hover_box = rect; }
    }
    if (hover) {
        auto lines = item_lines(scene, *hover, clvl);
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
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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
        const bool fixed = value_def.id >= 6 && value_def.id <= 11;       // life/mana/stamina, 8.8
        std::int64_t value = fixed ? stats.fixed(value_def.id) : stats.get(value_def.id);
        int colour = 0;                                   // 1 red, 3 blue, 4 gold
        if (value_def.id < 12 && value_def.id != 6 && value_def.id != 8 && value_def.id != 10) {   // the maxima already hold theirs (Fight::item_max)
            const auto bonus = panel.bonus[std::size_t(value_def.id)];
            if (value_def.id < 4) value += bonus;
            colour = bonus > 0 ? 3 : bonus < 0 ? 1 : 0;
        }
        auto res = [&](int resist_index) { value = panel.res[std::size_t(resist_index)]; colour = value >= panel.res_cap[std::size_t(resist_index)] ? 4 : value < 0 ? 1 : 0; };
        switch (value_def.id) {
            case 30: value = panel.next; break;
            case 31: value = panel.defense; break;
            case 39: res(0); break;
            case 43: res(1); break;
            case 41: res(2); break;
            case 45: res(3); break;
            default: break;
        }
        if (value_def.id == 30 && value < 0) continue;                // max level: blank
        const auto txt = std::to_string(value);
        const bool small_font = (fixed || value_def.id == 31) && (value > 999 || f16.measure(txt) >= value_def.right - value_def.left);
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
        for (auto& ch : skill_name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
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
                if (const auto at = label.find("%s"); at != label.npos) label.replace(at, 2, skill_name);
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
        for (auto [id, y] : { std::pair{ 0xfeb, 355 }, { 0xfec, 363 } })
            if (auto found = lookup_string(scene, std::uint16_t(id))) text(font6, 11, 0x59 - 1, y, u16_to_latin1(*found));
        text(f16, 0x5c, 0x80 - 1, 360, std::to_string(pts));
        for (int i = 0; i < 4; ++i) {
            const auto& button = kStatButtons[i];
            dc6(scene.level_socket, 0, button.x + 5, button.y + 5);
            dc6(scene.level_button, i == pressed_button ? 1 : 0, button.x + 8, button.y + 1);
        }
    }
}

void draw_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats) {
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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
        const int rows = int(std::clamp<std::int64_t>(cur * 80 / max, 0, 80));
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
    fill(0, 29, stats.fixed(d2d::d2s::kLife), stats.fixed(d2d::d2s::kMaxLife));
    fill(1, width - 0x6f, stats.fixed(d2d::d2s::kMana), stats.fixed(d2d::d2s::kMaxMana));
    at_bottom(scene.globe_glass, 0, 28, height - 5);
    at_bottom(scene.globe_glass, 1, width - 0x6e, height - 9);
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
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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

void draw_storage(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& layout, int panel,
                  int mouse_x, int mouse_y, int clvl) {
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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
        if (item.location != 0 || item.panel != panel) continue;
        const auto [x, y, width, height] = grid_rect(scene, layout, item);
        if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
            const auto& frame = spr->frame(0, 0);
            blit_sprite(framebuffer, frame, scene.item_pal(item, pal), x + (width - int(frame.width)) / 2, y + (height - int(frame.height)) / 2);
        }
        if (mouse_x >= x && mouse_x < x + width && mouse_y >= y && mouse_y < y + height) { hover = &item; hover_box = { x, y, width, height }; }
    }
    if (hover) draw_hover_text(framebuffer, scene, item_lines(scene, *hover, clvl), hover_box[0], hover_box[0] + hover_box[2], hover_box[1] + hover_box[3], hover_box[1]);
}

void draw_belt(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
               int mouse_x, int mouse_y, int clvl, bool popup) {
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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
        if (item.location != 2 || item.column < 0 || item.column >= belt.boxes || (!popup && item.column > 3)) continue;
        const auto& box = belt.box[std::size_t(item.column)];
        if (box[1] <= box[0]) continue;
        if (const auto* spr = scene.item_sprite(item); spr && spr->frames_per_direction() > 0) {
            const auto& frame = spr->frame(0, 0);
            blit_sprite(framebuffer, frame, scene.item_pal(item, pal), box[0] + (box[1] - box[0] + 1 - int(frame.width)) / 2,
                        box[2] + (box[3] - box[2] + 1 - int(frame.height)) / 2);
        }
        if (mouse_x >= box[0] && mouse_x <= box[1] && mouse_y >= box[2] && mouse_y <= box[3]) { hover = &item; hover_box = box; }
    }
    if (hover) draw_hover_text(framebuffer, scene, item_lines(scene, *hover, clvl), hover_box[0], hover_box[1] + 1, hover_box[3] + 1, hover_box[2]);
}

int automap_cel(const Scene& scene, const Level& level, int orientation, int main, int sub, std::uint32_t hash) {
    for (const auto& rule : scene.automap_rules) {
        if (rule.level_type != level.type || rule.orientation != orientation) continue;
        if (rule.main >= 0 && rule.main != main) continue;
        if (rule.sub0 >= 0 && (sub < rule.sub0 || sub > rule.sub1)) continue;
        return rule.cels[hash % rule.cels.size()];
    }
    return -1;
}

bool Automap::add(const Cell& cell) {
    // Interchangeable cels (0x711258, cel then group).
    static constexpr int kCelGroups[][2] = {
        {0,0},{1,0},{2,0},{3,0},{6,1},{7,1},{8,1},{11,2},{12,2},{13,3},{14,3},{20,4},{38,4},{21,5},{39,5},{46,6},{47,6},
        {48,6},{49,6},{51,7},{52,7},{53,7},{54,7},{60,8},{70,8},{61,9},{71,9},{120,10},{169,10},{171,10},{121,11},{170,11},
        {172,11},{257,12},{258,12},{259,12},{266,13},{267,13},{337,14},{338,14},{472,15},{473,15},{474,15},{475,15},
        {520,16},{521,16},{522,16},{533,17},{534,17} };
    int group = -1;
    for (const auto& pair : kCelGroups)
        if (pair[0] == cell.cel) group = pair[1];
    // ponytail: matches against every cell at (x, y); the tree only meets those on its search path.
    const auto at = placed.lower_bound({ cell.list, cell.y, cell.x, -1 });
    const bool taken = group < 0 ? at != placed.end() && std::get<0>(*at) == cell.list && std::get<1>(*at) == cell.y && std::get<2>(*at) == cell.x
                                 : placed.count({ cell.list, cell.y, cell.x, group }) != 0;
    if (taken) return false;
    placed.insert({ cell.list, cell.y, cell.x, group });
    cells.push_back(cell);
    return true;
}

void load_automap(Automap& automap, const std::filesystem::path& dir, const std::string& name, std::uint32_t map_seed, int layer) {
    if (dir.empty() || name.empty()) return;
    const auto file = d2d::d2s::automap_file(dir, name, map_seed);
    if (file.empty()) return;
    const auto loaded = d2d::d2s::read_automap(file, layer, game::object_seed(map_seed).low);
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

void automap_reveal(const Scene& scene, const Level& level, Automap& automap, float player_x, float player_y) {
    const auto& map = level.ds1;
    const int width = int(map.width()), height = int(map.height());
    if (width == 0) return;
    auto& seen = automap.revealed[level.id];
    if (seen.size() != std::size_t(width * height)) seen.assign(std::size_t(width * height), 0);
    const int cell_x = int(std::floor(player_x)), cell_y = int(std::floor(player_y)), reach = 12;
    for (int tile_y = std::max(0, cell_y - reach); tile_y < std::min(height, cell_y + reach); ++tile_y)
        for (int tile_x = std::max(0, cell_x - reach); tile_x < std::min(width, cell_x + reach); ++tile_x) {
            auto& done = seen[std::size_t(tile_y * width + tile_x)];
            if (done) continue;
            done = 1;
            const int act_x = tile_x + level.world_x, act_y = tile_y + level.world_y;     // act tiles
            const std::uint32_t hash = std::uint32_t(act_x * 73856093) ^ std::uint32_t(act_y * 19349663);
            const int automap_x = (act_x - act_y) * 80 / 10, automap_y = (act_x + act_y) * 40 / 10;
            for (const auto& layer : map.floors()) {
                const auto& tile = layer.cells[std::size_t(tile_y * width + tile_x)];
                if (tile.hidden || !(tile.prop1 & 2)) continue;
                if (const int cel = automap_cel(scene, level, 0, tile.style, tile.sequence, hash); cel >= 0) automap.add({ cel, automap_x, automap_y, 0 });
            }
            for (const auto& layer : map.walls()) {
                const auto& tile = layer.cells[std::size_t(tile_y * width + tile_x)];
                if (tile.hidden || tile.wall_type == 0) continue;
                if (const int cel = automap_cel(scene, level, tile.wall_type, tile.style, tile.sequence, hash); cel >= 0)
                    automap.add({ cel, automap_x, automap_y + (tile.wall_type > 15 ? 24 : 0), 1 });
            }
        }
}

void draw_automap(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Automap& automap, float player_x, float player_y) {
    if (!automap.open || scene.automap_cels.frames_per_direction() == 0) return;
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    const int scroll_x = int(std::lround((player_x - player_y) * 80 / 10)) - int(kScreenWidth) / 2 + 40;
    const int scroll_y = int(std::lround((player_x + player_y) * 40 / 10)) - int(kScreenHeight) / 2 + 15;
    for (const auto& cell : automap.cells) {
        if (cell.list == 3 || cell.cel < 0 || std::uint32_t(cell.cel) >= scene.automap_cels.frames_per_direction()) continue;   // miniatures: another cel file
        const auto& frame = scene.automap_cels.frame(0, std::uint32_t(cell.cel));
        const int x = cell.x - scroll_x, y = cell.y - scroll_y;
        if (x < -32 || x > int(kScreenWidth) + 32 || y < -64 || y > int(kScreenHeight) + 64) continue;
        blit_sprite(framebuffer, frame, pal, x, y - int(frame.height) + 1);
    }
    // Your own mark (FUN_0045a860 -> FUN_0045a7f0): the 13-point shape at
    // 0x6d6638 doubled, at (unit px / div - scroll + 8, py / div - scroll
    // - 8), in the palette colour nearest FUN_004fb180(0, 0, 0xff) — the
    // palette is BGR, so red (party green, other players blue).
    static constexpr int kMark[13][2] = { {0,-1},{2,-2},{4,-1},{2,0},{4,1},{2,2},{0,1},{-2,2},{-4,1},{-2,0},{-4,-1},{-2,-2},{0,-1} };
    std::uint8_t marker_red = 255, marker_green = 0, marker_blue = 0;
    {
        int best = 1 << 30;
        for (std::size_t i = 0; i < 256 && i < pal.entries().size(); ++i) {
            const auto colour = pal[std::uint8_t(i)];
            const int distance = (colour.r - 255) * (colour.r - 255) + colour.g * colour.g + colour.b * colour.b;
            if (distance < best) { best = distance; marker_red = colour.r; marker_green = colour.g; marker_blue = colour.b; }
        }
    }
    const int marker_x = int(std::lround((player_x - player_y) * 80 / 10)) - scroll_x + 8;
    const int marker_y = int(std::lround((player_x + player_y) * 40 / 10)) - scroll_y - 8;
    auto plot = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= int(kScreenWidth) || y >= int(kScreenHeight)) return;
        auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
        pixel[0] = marker_red; pixel[1] = marker_green; pixel[2] = marker_blue;
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
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
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

bool draw_quest_log(std::vector<std::uint8_t>& framebuffer, const Scene& scene, QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits,
                    const QuestState& quest_state, std::uint32_t now_ms) {
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    const int panel_x = kCharPanelX, panel_y = kCharPanelY;
    bool sound = false;
    auto bottom = [&](const d2d::dc6::Sprite& sprite, std::uint32_t frame, int x, int y) {   // DC6s draw up from their bottom-left
        if (frame >= sprite.frames_per_direction()) return;
        const auto& frame_ref = sprite.frame(0, frame);
        blit_sprite(framebuffer, frame_ref, pal, panel_x + x, panel_y + y - int(frame_ref.height));
    };
    bottom(scene.quest_bg, 0, 0, 256); bottom(scene.quest_bg, 1, 256, 256);
    bottom(scene.quest_bg, 2, 0, 432); bottom(scene.quest_bg, 3, 256, 432);
    for (int act = 0; act < 5; ++act) bottom(scene.quest_tabs, std::uint32_t(act * 2 + (act == quest_log.act ? 0 : 1)), kQuestTabX[std::size_t(act)], 33);
    const QuestEntry* sel = nullptr;
    for (const auto& entry : kQuestLog) {
        if (entry.act != quest_log.act) continue;
        const auto [x, y] = kQuestSlot[std::size_t(entry.slot)];
        const bool selected = entry.slot == quest_log.slot;
        if (selected) sel = &entry;
        const int shown = quest_text(quest_bits, entry.quest, quest_state).shown;
        int frame = quest_icon_frame(shown, selected);
        const auto quest_index = std::size_t(entry.quest);
        if (shown == 0 && quest_index < quest_log.seen.size() && !quest_log.seen[quest_index]) {   // the done animation
            if (!quest_log.frame_ms[quest_index]) quest_log.frame_ms[quest_index] = now_ms;
            if (now_ms - quest_log.frame_ms[quest_index] > 100) {
                quest_log.frame_ms[quest_index] = now_ms;
                if (++quest_log.frame[quest_index] == 1) sound = true;
            }
            if (quest_log.frame[quest_index] > 24) { quest_log.frame[quest_index] = 24; quest_log.seen[quest_index] = true; }
            frame = quest_log.frame[quest_index];
        }
        bottom(scene.quest_icons[std::size_t(entry.icon)], std::uint32_t(frame), x, y);
        bottom(scene.quest_sockets, selected ? 1u : 0u, x - 4, y + 5);
    }
    if (std::uint32_t(11) < scene.store_buttons.frames_per_direction()) bottom(scene.store_buttons, quest_log.close_down ? 11u : 10u, 0x116, 422);
    bottom(scene.quest_last, quest_log.last_down ? 1u : 0u, 0xe2, 422);
    if (!sel) return sound;
    auto centred = [&](const std::string& text, int y) { scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, panel_x + (320 - scene.font.measure(text)) / 2, panel_y + y - scene.font.line_height(), text); };
    centred(string_id(scene, std::uint16_t(sel->name)), 248);
    if (const auto quest_text_entry = quest_text(quest_bits, sel->quest, quest_state); quest_text_entry.string) {   // word-wrapped to 270 px (FUN_00502970(0x10e))
        std::string text = string_id(scene, std::uint16_t(quest_text_entry.string)), row;
        if (quest_text_entry.count >= 0) text += std::to_string(quest_text_entry.count);
        int y = 270;
        std::size_t start = 0;
        while (start < text.size()) {
            const auto end = std::min(text.find(' ', start), text.size());
            const std::string word = text.substr(start, end - start);
            if (!row.empty() && scene.font.measure(row + " " + word) > 270) { centred(row, y); y += 20; row.clear(); }
            row += (row.empty() ? "" : " ") + word;
            start = end + 1;
        }
        if (!row.empty()) centred(row, y);
    }
    return sound;
}

}  // namespace d2d::client
