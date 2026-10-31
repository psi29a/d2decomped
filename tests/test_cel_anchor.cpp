// SPDX-License-Identifier: GPL-3.0-or-later
// blit_at_anchor against game.exe's cel placement (FUN_006014c0,
// SRC/CelCmp.cpp): a cel's last row sits at y + offset_y, its first at
// y + offset_y - height + 1. Cases from tools/emu/cel_anchor.py.
#include "common.hpp"

#include <dc6.hpp>
#include <palette.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    struct Case { int x, y, offset_x, offset_y, width, height, left, top, bottom; };
    constexpr Case cases[]{
#include "cel_anchor_cases.inc"
    };
    const std::vector<std::byte> pal_dat(256 * 3, std::byte{0xff});
    const d2d::palette::Palette pal{pal_dat};
    const int screen_w = int(d2d::client::kScreenWidth);
    for (const auto& sample : cases) {
        d2d::dc6::Frame frame;
        frame.width = std::uint32_t(sample.width);
        frame.height = std::uint32_t(sample.height);
        frame.offset_x = sample.offset_x;
        frame.offset_y = sample.offset_y;
        frame.pixels.assign(std::size_t(frame.width) * frame.height, 1);
        std::vector<std::uint8_t> framebuffer(std::size_t(screen_w) * d2d::client::kScreenHeight * 4, 0);
        d2d::client::blit_at_anchor(framebuffer, frame, pal, sample.x, sample.y);
        int left = 0, top = -1, bottom = -1;
        for (int row = 0; row < int(d2d::client::kScreenHeight); ++row)
            for (int column = 0; column < screen_w; ++column)
                if (framebuffer[(std::size_t(row) * std::size_t(screen_w) + std::size_t(column)) * 4 + 3] != 0) {
                    if (top < 0) { top = row; left = column; }
                    bottom = row;
                }
        assert(top == sample.top && bottom == sample.bottom);
        assert(top < 0 || left == sample.left);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
