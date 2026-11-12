// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game dt1 palette font dc6 dcc cof
// test-includes: apps/d2d
// FUN_00483ff0 (0x483ff0) -> FUN_004839d0: the cell under the cursor is green
// (kind 1, FUN_00483960's nearest of (0, 0x80, 0)). Item flag 0x4
// (FUN_006280a0 at 0x48424f) and FUN_004c2240 (0x4c2240: FUN_00625760(item, 2)
// or the player in state 0x36) are red (kind 0). An identified usable item
// stays blue.

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <d2s_items.hpp>
#include <palette.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    d2d::palette::Palette::set_ramp(100, 100);   // file colours, not the shipped gamma
    d2d::client::Scene scene;
    std::array<d2d::palette::Rgba, 256> entries{};
    for (auto& entry : entries) entry = { 255, 0, 255, 255 };
    entries[0x76] = { 0, 160, 8, 255 };   // nearest of (0, 0x80, 0); not the raw source colour
    scene.act_pal = d2d::palette::Palette(entries);
    scene.rules.item_info["hax"] = {};
    const d2d::rules::Wearer wearer{ 0, 10, 10, 10 };

    d2d::d2s::Item item;
    item.code = "hax";
    item.identified = true;
    item.flags = 0x10;

    const std::array<int, 4> cell{ 0, 0, 1, 1 };
    auto fresh = [] {
        std::vector<std::uint8_t> framebuffer(std::size_t(d2d::client::kScreenWidth) * d2d::client::kScreenHeight * 4, 8);
        return framebuffer;
    };
    auto expect = [](const std::vector<std::uint8_t>& framebuffer, int red, int green, int blue) {
        assert(framebuffer[0] == red && framebuffer[1] == green && framebuffer[2] == blue);
    };

    auto blue = fresh();
    d2d::client::tint_cells(blue, scene, item, wearer, cell);
    expect(blue, 12, 6, 32);   // (24, 0, 104) at 25 % over (8, 8, 8)

    item.flags |= 0x4u;
    auto flagged = fresh();
    d2d::client::tint_cells(flagged, scene, item, wearer, cell);
    expect(flagged, 42, 6, 6);   // kind 0, act 1's 0x08
    auto flagged_hover = fresh();
    d2d::client::tint_cells(flagged_hover, scene, item, wearer, cell, nullptr, true);
    expect(flagged_hover, 6, 46, 8);   // kind 1 wins: (0, 160, 8) over (8, 8, 8)
    item.flags = 0x10;

    auto cursed = fresh();
    d2d::client::tint_cells(cursed, scene, item, wearer, cell, nullptr, false, true);
    expect(cursed, 42, 6, 6);   // FUN_00625760(item, 2)
    auto whirling = fresh();
    d2d::client::tint_cells(whirling, scene, item, wearer, cell, nullptr, false, false, true);
    expect(whirling, 42, 6, 6);   // player state 0x36

    item.identified = false;
    item.flags = 0;
    auto unknown = fresh();
    d2d::client::tint_cells(unknown, scene, item, wearer, cell);
    expect(unknown, 41, 22, 22);   // kind 4, act 1's 0x4d
    auto picked = fresh();
    d2d::client::tint_cells(picked, scene, item, wearer, cell, nullptr, true);
    expect(picked, 6, 46, 8);

    std::printf("grid tint: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
