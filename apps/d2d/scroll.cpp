// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for scroll.hpp.
#include "scroll.hpp"

#include "common.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <dc6.hpp>
#include <palette.hpp>
#include <stone_scroll.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kWidth = int(kScreenWidth), kHeight = int(kScreenHeight);
constexpr int kLeft = 0x50, kTop = 0x3c;           // the left-panel spot at 800x600 (0x7a2858, -0x7a285c)

}  // namespace

bool ScrollPanel::input(const Mouse& mouse) {
    if (!open || !mouse.press_this_frame || mouse.y >= kHeight - 0x30 || mouse.x >= kWidth / 2) return false;
    open = false;
    return true;
}

bool ScrollPanel::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, std::uint32_t now_ms) {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // FUN_004f6480 by the frame's bottom-left, moved by its offsets (scroin's
    // top pieces sit 115 up, scroin3's glows left of their spot); draw modes
    // 0 / 1 / 2 a quarter, half, three quarters seen, 3 added, 5 whole
    // (deviations.md 1: the blends in RGB).
    const auto cel = [&](const d2d::dc6::Sprite& sprite, int frame, int left, int bottom, int mode) {
        if (frame < 0 || std::uint32_t(frame) >= sprite.frames_per_direction()) return;
        const auto& image = sprite.frame(0, std::uint32_t(frame));
        const int top = bottom + image.offset_y - int(image.height);
        left += image.offset_x;
        for (int row = 0; row < int(image.height); ++row)
            for (int column = 0; column < int(image.width); ++column) {
                const int pixel_x = left + column, pixel_y = top + row;
                const auto index = image.pixels[std::size_t(row) * image.width + std::size_t(column)];
                if (index == 0 || pixel_x < 0 || pixel_x >= kWidth || pixel_y < 0 || pixel_y >= kHeight) continue;
                const auto colour = pal[index];
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * kScreenWidth + std::size_t(pixel_x)) * 4];
                const std::array<int, 3> source = { colour.r, colour.g, colour.b };
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const int under = pixel[channel], over = source[channel];
                    pixel[channel] = std::uint8_t(mode == 3 ? std::min(under + over, 255) : mode < 3 ? (under * (3 - mode) + over * (mode + 1)) / 4 : over);
                }
            }
    };
    // FUN_0049fa10: black under it (FUN_004f6300 mode 5), scroin's four
    // pieces, buysellbtn's close button (frame 10).
    for (int y = std::max(kHeight - kTop - 0xe0, 0); y < kHeight - 0x30; ++y)
        for (int x = kLeft; x < kWidth / 2; ++x) std::fill_n(&framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4], 3, std::uint8_t(0));
    for (int piece = 0; piece < 4; ++piece)
        cel(scene.scroll, piece, kLeft + (piece & 1) * 0x100, kHeight - kTop - (piece < 2 ? 0x6d : 0x31), 5);
    cel(scene.store_buttons, 10, kLeft + 0x115, kHeight - kTop - 0x3d, 5);
    if (!deciphered || !order) return false;
    clock.draw(now_ms);
    bool sound = false;
    for (std::size_t place = 0; place < order->size(); ++place) {
        const auto symbol = d2d::rules::stone_symbol(*order, place, clock.steps);
        if (!symbol) continue;
        sound = sound || symbol->sound;
        cel(scene.scroll_symbols, symbol->place, kLeft + symbol->x, symbol->bottom + kTop, symbol->mode);
        cel(scene.scroll_glow, symbol->glow_frame, kLeft + symbol->glow_x, symbol->glow_bottom + kTop, 3);
    }
    return sound;
}

}  // namespace d2d::client
