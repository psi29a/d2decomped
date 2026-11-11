// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for use_item.hpp: the panel's draw.
#include "use_item.hpp"

#include "common.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s_items.hpp>
#include <dc6.hpp>
#include <font.hpp>
#include <item_text.hpp>
#include <palette.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::client::use_item {

// FUN_004c01e0 -> FUN_004bfdd0, then the placed item (FUN_004c01e0 state 2)
// and in state 3 the wait box.
void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Panel& panel, const d2d::d2s::Item* held,
          int mouse_x, int mouse_y, std::uint32_t now_ms) {
    if (!panel.open()) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto cel = [&](const d2d::dc6::Sprite& sprite, int frame, int left, int bottom) {   // FUN_004f6480, draw mode 5
        if (frame < 0 || std::uint32_t(frame) >= sprite.frames_per_direction()) return;
        const auto& image = sprite.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, image, pal, left, bottom - int(image.height));
    };
    const auto& font = scene.font;                               // FUN_00502ef0(1): font16
    cel(scene.use_item_panel, 0, kPanelX, kPanelBottom);         // MENU\upgrade (0x7c5494)
    for (std::size_t index = 0; index < kButtons.size(); ++index) {
        const auto& button = kButtons[index];
        cel(scene.store_buttons, button.frame + (panel.down[index] ? 1 : 0), button.x, button.bottom);   // buysellbtn (FUN_00454600)
    }
    // A button's hover, the mouse left of 0x140 and above H - 0x30: its
    // label over a box 75% dark (FUN_0046efd0 colour 0, mode 2), the box
    // w + 0xc by the font's height + 6 from (x - w/2 + 10, bottom - height -
    // 0x2a), the text white from x - w/2 + 0x10 to its bottom at bottom - 0x26.
    if (const int index = button_at(mouse_x, mouse_y); index >= 0 && mouse_x < 0x140 && mouse_y < int(kScreenHeight) - 0x30) {
        const auto& button = kButtons[std::size_t(index)];
        const auto label = string_id(scene, index == 0 ? service_label(panel.hc_idx) : button.label);
        const int width = font.measure(label), height = font.line_height();
        const int left = button.x - width / 2 + 10, top = button.bottom - height - 0x2a;
        for (int pixel_y = std::max(0, top); pixel_y < std::min(int(kScreenHeight), top + height + 6); ++pixel_y)
            for (int pixel_x = std::max(0, left); pixel_x < std::min(int(kScreenWidth), left + width + 0xc); ++pixel_x) {
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                for (int channel = 0; channel < 3; ++channel) pixel[channel] = std::uint8_t(pixel[channel] / 4);
            }
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, button.x - width / 2 + 0x10, glyph_top(font, button.bottom - 0x26), label, 255, 255, 255);
    }
    // The instructions, gold, wrapped (FUN_00502970) and centred.
    const auto lines = d2d::font::wrap_edit_box(string_id(scene, instructions(panel.hc_idx)), kTextWidth, [&](unsigned char code) {
        const auto* glyph = font.find(code);
        return glyph ? int(glyph->width) : 0;
    });
    int bottom = kTextBottom;
    for (const auto& line : lines) {
        const auto& gold = d2d::game::kTxtGold;
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, kTextX - font.measure(line) / 2, glyph_top(font, bottom), line, gold[0], gold[1], gold[2]);
        bottom += kTextStep;
    }
    // The placed item (FUN_0046ee80): centred on 0xa7, from y 0x6a, down
    // (0x72 - its height) / 2 more when under 0x70 tall.
    if (panel.placed() && held && held->id == panel.item)
        if (const auto* sprite = scene.item_sprite(*held); sprite && sprite->frames_per_direction() > 0) {
            const auto& image = sprite->frame(0, 0);
            const int item_height = int(image.height);
            blit_sprite(framebuffer, image, scene.item_pal(*held, pal), kItemX - int(image.width) / 2, kSlotY0 + (item_height < 0x70 ? (0x72 - item_height) / 2 : 0));
        }
    // The wait box (FUN_004bfaf0: FUN_004b7eb0 at 150, 130; one line 0xd19,
    // 0xf high, white, font16, not selectable).
    if (panel.state == State::kWaiting) {
        NpcMenuState box;
        box.npc = panel.npc;
        box.lines.push_back({ string_id(scene, 0xd19), 15 });
        layout_npc_menu(scene, box, 150, 130 + 150);                // layout takes the NPC's feet, 150 under the box's spot
        draw_npc_menu(framebuffer, scene, box, -1, -1, now_ms);
    }
}

}  // namespace d2d::client::use_item
