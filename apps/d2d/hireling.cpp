// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for hireling.hpp.
#include "hireling.hpp"

#include "common.hpp"
#include "game_api.hpp"
#include "items.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>
#include <dc6.hpp>
#include <font.hpp>
#include <item_text.hpp>
#include <merc.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kLeft = 0x50, kTop = 0x3c;                       // the left-panel spot at 800x600 (0x7a2858, -0x7a285c)
constexpr int kCloseX = kLeft + 0x110, kCloseBottom = int(kScreenHeight) - 0x3f - kTop;   // buysellbtn 10 / 11

// The labels (0x6d9ae8, 14 bytes: x, x2 (0: from x), y, string) and the
// values (0x7221a8, 20 bytes: x0, x1 (the right edge when x0 is 0), y,
// width before font8, stat), in panel coordinates.
struct Label { int x, x2, y; std::uint16_t id; };
constexpr std::array<Label, 12> kLabels = { {
    { 0xf, 0, 0xec, 0xfda }, { 0x91, 0, 0xec, 0xfd9 }, { 0xc8, 0, 0xec, 0xfdb }, { 0xf, 0, 0x119, 0xfdc },
    { 0xf, 0, 0x131, 0xfde }, { 0xf, 0, 0x149, 0xfdd }, { 0xf, 0, 0x161, 0xfe0 }, { 0xb4, 0xf5, 0x119, 0xfe7 },
    { 0xb4, 0xf5, 0x131, 0xfe8 }, { 0xb4, 0xf5, 0x149, 0xfe9 }, { 0xb4, 0xf5, 0x161, 0xfea }, { 0xb4, 0, 0xd6, 0xfe4 },
} };
enum Value { kExp, kLevel, kNext, kStr, kDex, kDamage, kDefense, kFire, kCold, kLight, kPoison, kLife };
struct ValueBox { int x0, x1, y, width; Value value; };
constexpr std::array<ValueBox, 12> kValues = { {
    { 0, 120, 254, 0, kExp }, { 0, 175, 254, 0, kLevel }, { 0, 305, 254, 0, kNext }, { 0, 154, 282, 48, kStr },
    { 0, 154, 306, 48, kDex }, { 0, 154, 330, 48, kDamage }, { 0, 154, 354, 48, kDefense }, { 0, 309, 282, 48, kFire },
    { 0, 309, 306, 48, kCold }, { 0, 309, 330, 48, kLight }, { 0, 309, 354, 48, kPoison }, { 210, 305, 215, 0, kLife },
} };
constexpr std::array<int, 4> kMercSlots = { d2d::d2s::body_location::kHead, d2d::d2s::body_location::kTorso,
                                        d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm };

}  // namespace

int HirelingPanel::input(const Scene& scene, const Mouse& mouse, bool holding) {
    const bool on_close = mouse.x >= kCloseX && mouse.x <= kCloseX + 0x20 && mouse.y >= kCloseBottom - 0x20 && mouse.y <= kCloseBottom;
    if (mouse.press_this_frame && on_close) { close_down = true; return -1; }
    if (mouse.release_this_frame && close_down) {
        close_down = false;
        if (on_close) { open = false; return -2; }
        return -1;
    }
    if (!mouse.press_this_frame) return -1;
    for (const int slot : kMercSlots) {
        const auto& box = scene.merc_layout.slots[std::size_t(slot)];
        if (box[2] <= 0 || mouse.x < box[0] || mouse.x >= box[0] + box[2] || mouse.y < box[1] || mouse.y >= box[1] + box[3]) continue;
        return holding ? 0 : slot;                                     // 0x61: give the held item, or take this slot's
    }
    return -1;
}

void HirelingPanel::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const View::MercSheet& sheet, const std::string& name,
                         const std::vector<d2d::d2s::Item>& worn, const std::optional<d2d::d2s::Item>& held, int mouse_x, int mouse_y) const {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto cel = [&](const d2d::dc6::Sprite& sprite, int frame, int left, int bottom) {   // FUN_004f6480, draw mode 5
        if (frame < 0 || std::uint32_t(frame) >= sprite.frames_per_direction()) return;
        const auto& image = sprite.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, image, pal, left, bottom - int(image.height));
    };
    // NPCInv's four frames (256 + 64 wide, 256 + 176 tall), the close button.
    const int height = int(kScreenHeight);
    cel(scene.npc_inv, 0, kLeft, height - 0xe0 - kTop);
    cel(scene.npc_inv, 1, kLeft + 0x100, height - 0xe0 - kTop);
    cel(scene.npc_inv, 2, kLeft, height - 0x30 - kTop);
    cel(scene.npc_inv, 3, kLeft + 0x100, height - 0x30 - kTop);
    cel(scene.store_buttons, close_down ? 11 : 10, kCloseX, kCloseBottom);
    // The empty slots' pictures (FUN_0048b0a0, 0x7220b0's offsets from a
    // box's left and bottom): armour, the helm (inv_helm_glove 1), the weapons.
    const auto worn_in = [&](int slot) { return std::ranges::find(worn, slot, &d2d::d2s::Item::slot) != worn.end(); };
    const auto empty = [&](const d2d::dc6::Sprite& sprite, int frame, int slot, int off_x, int off_y) {
        const auto& box = scene.merc_layout.slots[std::size_t(slot)];
        if (box[2] > 0 && !worn_in(slot)) cel(sprite, frame, box[0] + off_x, box[1] + box[3] + off_y);
    };
    namespace body = d2d::d2s::body_location;
    empty(scene.inv_armor, 0, body::kTorso, 2, -2);
    empty(scene.inv_helm_glove, 1, body::kHead, 0, -1);
    empty(scene.inv_weapons, 0, body::kRightArm, 0, -2);
    empty(scene.inv_weapons, 0, body::kLeftArm, 0, -2);
    // With an item in hand, the slot under the mouse goes green where the
    // merc would wear it, red where it wouldn't (FUN_0048b3f0, FUN_0048b290).
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const int slot : kMercSlots) {
        const auto& box = scene.merc_layout.slots[std::size_t(slot)];
        if (box[2] <= 0 || mouse_x < box[0] || mouse_x >= box[0] + box[2] || mouse_y < box[1] || mouse_y >= box[1] + box[3]) continue;
        if (held) {
            const bool fits = d2d::rules::merc_slot(scene.rules, *held, sheet.cls, sheet.str, sheet.dex, sheet.level) == slot;
            for (int y = std::max(box[1], 0); y < std::min(box[1] + box[3], height); ++y)
                for (int x = std::max(box[0], 0); x < std::min(box[0] + box[2], int(kScreenWidth)); ++x) {
                    auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
                    pixel[fits ? 1 : 0] = std::uint8_t(std::min(255, pixel[fits ? 1 : 0] + 64));
                }
        }
    }
    for (const auto& item : worn) {                                   // FUN_0048b540: each in its box, centred
        if (item.slot < body::kFirst || item.slot > body::kLast) continue;
        const auto& box = scene.merc_layout.slots[std::size_t(item.slot)];
        if (box[2] <= 0) continue;
        if (const auto* sprite = scene.item_sprite(item); sprite && sprite->frames_per_direction() > 0) {
            const auto& frame = sprite->frame(0, 0);
            blit_sprite(framebuffer, frame, scene.item_pal(item, pal), box[0] + (box[2] - int(frame.width)) / 2, box[1] + (box[3] - int(frame.height)) / 2);
        }
        if (mouse_x >= box[0] && mouse_x < box[0] + box[2] && mouse_y >= box[1] && mouse_y < box[1] + box[3]) { hover = &item; hover_box = box; }
    }
    // The name (FUN_0048a7d0: cut to 0xa0 wide), the labels in font6 (two
    // lines 8 apart), the values in font16 (font8 past their box) — gold at
    // a resistance's cap, red below 0 (FUN_0048a990's colours).
    const auto& font16 = scene.font;
    const auto& font6 = scene.font_tiny.line_height() > 0 ? scene.font_tiny : scene.font;
    const auto& font8 = scene.font_small.line_height() > 0 ? scene.font_small : scene.font;
    const auto put = [&](const d2d::font::Font& font, const std::string& words, int left, int right, int baseline, int colour, bool right_align) {
        static constexpr std::array<std::array<std::uint8_t, 3>, 5> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 } } };
        const int width = font.measure(words);
        const int x = right_align ? right - width : right > left ? left + (right - left - width) / 2 : left;
        const auto& rgb = kRgb[std::size_t(std::clamp(colour, 0, 4))];
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, kLeft + x, kTop + glyph_top(font, baseline), words, rgb[0], rgb[1], rgb[2]);
    };
    std::string shown = name;
    while (!shown.empty() && font16.measure(shown) > 0xa0) shown.pop_back();
    put(font16, shown, 0xf, 0, 0xd6, 0, false);
    for (const auto& label : kLabels) {
        const std::string words = string_id(scene, label.id);
        if (const auto newline = words.find('\n'); newline != std::string::npos) {
            put(font6, words.substr(0, newline), label.x, label.x2, label.y - 4, 0, false);
            put(font6, words.substr(newline + 1), label.x, label.x2, label.y + 4, 0, false);
        } else {
            put(font6, words, label.x, label.x2, label.y, 0, false);
        }
    }
    for (const auto& box : kValues) {
        std::string words;
        int colour = 0;
        const auto res = [&](int index) { const int value = sheet.res[std::size_t(index)]; colour = value >= 75 ? 4 : value < 0 ? 1 : 0; return std::to_string(value); };
        switch (box.value) {
            case kExp: words = std::to_string(sheet.exp); break;
            case kLevel: words = std::to_string(sheet.level); break;
            case kNext: words = std::to_string(sheet.next); break;
            case kStr: words = std::to_string(sheet.str); break;
            case kDex: words = std::to_string(sheet.dex); break;
            case kDamage: words = std::to_string(sheet.min) + "-" + std::to_string(sheet.max); break;
            case kDefense: words = std::to_string(sheet.defense); break;
            case kFire: words = res(0); break;
            case kLight: words = res(1); break;
            case kCold: words = res(2); break;
            case kPoison: words = res(3); break;
            case kLife: words = std::to_string(std::max(sheet.life, 1)) + " / " + std::to_string(sheet.max_life); break;
        }
        // font8; font6 when wider than its box (FUN_0048a990: 0x7221b4).
        const bool small = box.width > 0 && font8.measure(words) > box.width;
        put(small ? font6 : font8, words, box.x0, box.x1, box.y - 2, colour, box.x0 == 0);
    }
    if (hover && !held) {
        const d2d::rules::Wearer merc{ -1, sheet.str, sheet.dex, sheet.level };
        draw_hover_text(framebuffer, scene, item_lines(scene, *hover, sheet.level, &merc), hover_box[0], hover_box[0] + hover_box[2], hover_box[1] + hover_box[3], hover_box[1]);
    }
}

}  // namespace d2d::client
