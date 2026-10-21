// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for help.hpp.
#include "help.hpp"

#include "common.hpp"
#include "keyconfig.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kWidth = int(kScreenWidth), kHeight = int(kScreenHeight);
constexpr int kCloseX = 0x2ad, kCloseBottom = 0x38;          // FUN_00494e40 (800x600), hit FUN_00494f30: 0x21 square from (0x2ad, 0x18)

// A white bullet's bottom-left and its line's top and bottom (FUN_00494a00:
// the line is two pixels, x + 3 and x + 4, in palette colour 0xff).
struct Pointer { int x, bottom, line_top, line_bottom; };
constexpr std::array<Pointer, 11> kPointers = { {
    { 0x37, 0x217, 0x1e6, 0x214 }, { 0x82, 0x234, 0x1bd, 0x231 }, { 0xd9, 0x23f, 0x177, 0x23c }, { 0x104, 0x249, 0x20e, 0x246 },
    { 0x136, 0x249, 0x1e6, 0x246 }, { 0x16d, 0x235, 0x20d, 0x232 }, { 0x1bd, 0x21c, 0x1c3, 0x219 }, { 0x212, 0x239, 0x20d, 0x236 },
    { 0x23c, 0x23f, 0x177, 0x23c }, { 0x29e, 0x234, 0x1bd, 0x231 }, { 0x2e4, 0x217, 0x1e6, 0x214 },
} };

// FUN_00495410's labels (traced in the emulator): string, x (the centre,
// or the left edge), baseline; `key`: the function whose first key fills
// its %s (-1: none).
struct Label { std::uint16_t id; int x, baseline; bool centred; int key; };
constexpr std::array<Label, 28> kLabels = { {
    { 0x1067, 117, 78, false, keys::kRun }, { 0x1068, 117, 98, false, keys::kShowItems }, { 0x1069, 117, 118, false, keys::kStandStill },
    { 0x106a, 117, 138, false, keys::kAutomap }, { 0x106b, 117, 158, false, -1 }, { 0x106c, 117, 178, false, -1 },
    { 0x106d, 117, 198, false, -1 }, { 0x106e, 117, 218, false, keys::kHelp },
    { 0x106f, 59, 481, true, -1 }, { 0x1070, 134, 410, true, -1 }, { 0x1071, 134, 425, true, -1 }, { 0x1072, 134, 440, true, -1 },
    { 0xf92, 222, 375, true, -1 }, { 0x1074, 264, 506, true, -1 }, { 0x1075, 264, 521, true, -1 }, { 0x1076, 314, 481, true, -1 },
    { 0x1077, 369, 505, true, -1 }, { 0x1078, 369, 520, true, -1 }, { 0x1079, 449, 401, true, -1 }, { 0x107a, 449, 416, true, -1 },
    { 0x107b, 449, 431, true, -1 }, { 0x107c, 449, 446, true, -1 }, { 0x107d, 534, 520, true, -1 }, { 0xf93, 572, 375, true, -1 },
    { 0x1073, 674, 410, true, -1 }, { 0x1071, 674, 425, true, -1 }, { 0x1072, 674, 440, true, -1 }, { 0x107e, 744, 481, true, -1 },
} };

}  // namespace

bool HelpScreen::input(const Mouse& mouse) {
    const bool over = mouse.x >= kCloseX && mouse.x <= kCloseX + 0x20 && mouse.y >= 0x18 && mouse.y <= 0x18 + 0x20;
    if (mouse.press_this_frame && over) close_down = true;
    if (mouse.release_this_frame) {
        const bool closes = close_down && over;
        close_down = false;
        if (closes) { open = false; return true; }
    }
    return false;
}

void HelpScreen::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const keys::Table& table) const {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // FUN_00494eb0: the screen above the control panel half dark.
    for (int y = 0; y < kHeight - 0x31; ++y)
        for (int x = 0; x < kWidth; ++x)
            for (int channel = 0; channel < 3; ++channel) {
                auto& value = framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4 + std::size_t(channel)];
                value = std::uint8_t(value / 2);
            }
    const auto cel = [&](const d2d::dc6::Sprite& sprite, int frame, int left, int bottom) {   // FUN_004f6480, draw mode 5
        if (frame < 0 || std::uint32_t(frame) >= sprite.frames_per_direction()) return;
        const auto& image = sprite.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, image, pal, left, bottom - int(image.height));
    };
    // FUN_004944f0: 800helpborder's seven pieces.
    static constexpr std::array<std::array<int, 2>, 7> kBorder = { { { 0, 0x100 }, { 0, 0x200 }, { 0x42, 0x14 }, { 0x142, 0x14 },
                                                                    { 0x217, 0x100 }, { 0x30c, 0x100 }, { 0x30c, 0x200 } } };
    for (std::size_t piece = 0; piece < kBorder.size(); ++piece) cel(scene.help_border, int(piece), kBorder[piece][0], kBorder[piece][1]);
    cel(scene.store_buttons, close_down ? 11 : 10, kCloseX, kCloseBottom);              // FUN_00494e40: buysellbtn
    for (int line = 0; line < 8; ++line) cel(scene.help_yellow_bullet, 0, 0x68, 0x49 + 0x14 * line);   // FUN_00494620
    const auto white = pal[0xff];
    for (const auto& pointer : kPointers) {                                            // FUN_00494a00
        cel(scene.help_white_bullet, 0, pointer.x, pointer.bottom);
        for (int y = pointer.line_top; y <= pointer.line_bottom && y < kHeight; ++y)
            for (const int x : { pointer.x + 3, pointer.x + 4 }) {
                auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
                pixel[0] = white.r; pixel[1] = white.g; pixel[2] = white.b;
            }
    }
    // FUN_00495410: the title and Close in the font then current (unverified:
    // taken as Font16), the rest in FontFormal12 (5), white.
    const auto& title_font = scene.font;
    const auto& text_font = scene.font_formal12.line_height() > 0 ? scene.font_formal12 : scene.font;
    const auto put = [&](const d2d::font::Font& font, const std::string& words, int x, int baseline, bool centred) {
        font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, centred ? x - font.measure(words) / 2 : x, glyph_top(font, baseline), words);
    };
    put(title_font, string_id(scene, 0x1066), 0x17c, 0x11, true);
    put(title_font, string_id(scene, 0x1030), 701, 0x49, true);
    for (const auto& label : kLabels) {
        std::string words = string_id(scene, label.id);
        if (label.key >= 0)
            if (const auto marker = words.find("%s"); marker != std::string::npos) {
                const auto key = keys::key_of(table, label.key, 1);
                const auto name = keys::name_id(key);
                words.replace(marker, 2, name ? string_id(scene, name) : std::string(1, char(key & 0xff)));
            }
        put(text_font, words, label.x, label.baseline, label.centred);
    }
}

}  // namespace d2d::client
