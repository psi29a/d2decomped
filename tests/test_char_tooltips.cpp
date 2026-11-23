// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-sources: apps/d2d/char_tooltips.cpp apps/d2d/common.cpp apps/d2d/items.cpp
// test-includes: apps/d2d
// The char panel's hovers (FUN_004a7ae0 / FUN_004a7180), where game.exe puts them at 800x600: attack
// line 1's under x 80+0xa2..80+0x140, y 540-0x14e..540-0x13b darkens its 0x9b x 0x1e box at (80+154,
// 60+115); off it nothing; the defence's under x 80+0xad..80+0x137, y 540-0x11e..540-0x10b draws a hover
// box from (80+0x8b, 540-0x13b). Made-up rows and a one-glyph font: the places, not the game's art.

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <test_data.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::client;

namespace {

d2d::font::Font one_glyph_font() {
    std::vector<std::byte> sprite(28 + 32 + 3);
    auto write32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 4; ++shift) sprite[offset + shift] = std::byte((value >> (8 * shift)) & 255);
    };
    write32(0, 6); write32(16, 1); write32(20, 1); write32(24, 28); write32(32, 1); write32(36, 1); write32(56, 3);
    sprite[60] = std::byte{ 1 }; sprite[61] = std::byte{ 1 }; sprite[62] = std::byte{ 0x80 };
    std::vector<std::byte> metrics(12 + 14);
    metrics[0] = std::byte{ 'W' }; metrics[1] = std::byte{ 'o' }; metrics[2] = std::byte{ 'o' }; metrics[3] = std::byte{ '!' }; metrics[4] = std::byte{ 1 };
    metrics[12] = std::byte{ 'x' }; metrics[15] = std::byte{ 1 }; metrics[16] = std::byte{ 1 };
    return d2d::font::Font(metrics, d2d::dc6::Sprite(sprite));
}

bool darkened(const std::vector<std::uint8_t>& framebuffer, int x, int y) {
    return framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4] < 200;
}

}  // namespace

int main() try {
    d2d::test::TestData rows;
    rows.monster(0, "zombie1").level = { 5, 5, 5 };
    rows.mon_level(5, 100, 50, 60, 10);
    Scene scene;
    scene.monsters = rows.data.monsters;
    scene.mon_npc.resize(1);
    scene.mon_npc[0].title = "Zombie";
    scene.font = one_glyph_font();
    std::array<d2d::palette::Rgba, 256> entries{};
    entries[1] = { 255, 255, 255, 255 };
    scene.pal = d2d::palette::Palette(entries);
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kLevel] = 10;
    PanelStats panel;
    panel.attack[0].attack_rating = 120;
    panel.defense = 40;
    d2d::d2s::Header header;
    Hud hud;
    hud.header = &header;
    hud.tohit_class = 0; hud.behit_class = 0;
    const auto frame = [&](int mouse_x, int mouse_y) {
        std::vector<std::uint8_t> framebuffer(kScreenWidth * kScreenHeight * 4, 240);
        draw_char_tooltips(framebuffer, scene, stats, panel, hud, mouse_x, mouse_y);
        return framebuffer;
    };
    const auto row_one = frame(80 + 0xa2 + 10, 540 - 0x14e + 5);
    assert(darkened(row_one, 80 + 154 + 1, 60 + 115 + 1) && darkened(row_one, 80 + 154 + 0x9b - 1, 60 + 115 + 0x1e - 1));
    assert(!darkened(row_one, 80 + 154 + 0x9b + 1, 60 + 115 + 1));          // the box's own width
    const auto row_two = frame(80 + 0xa2 + 10, 540 - 0x134 + 5);            // line 2 has no rating: nothing
    assert(!darkened(row_two, 80 + 154 + 1, 60 + 130 + 1));
    const auto away = frame(10, 10);
    assert(!darkened(away, 80 + 154 + 1, 60 + 115 + 1));
    const auto defence = frame(80 + 0xad + 5, 540 - 0x11e + 5);
    assert(darkened(defence, 80 + 0x8b + 1, 540 - 0x13b - 2));             // the hover box over its bottom-left
    std::puts("OK: the char panel's hovers where game.exe draws them");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_char_tooltips: %s\n", error.what());
    return 1;
}
