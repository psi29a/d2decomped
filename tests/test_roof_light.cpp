// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game dt1 palette font dc6 dcc cof
// test-sources: apps/d2d/common.cpp apps/d2d/scene/render.cpp apps/d2d/tile_blit.cpp apps/d2d/world_view.cpp
// test-includes: apps/d2d
// Raised roofs use the environment's intensity (FUN_004dea70 /
// FUN_004ddef0 / FUN_0061c0e0), while flat roofs use the local light grid.
// Synthetic pixels and palettes exercise render_world without game data.
#include "common.hpp"
#include "scene.hpp"
#include "tile_blit.hpp"
#include "world_view.hpp"

#include <ds1.hpp>
#include <dt1.hpp>
#include <gamedata.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

// Exercise the real roof dispatch and pixel blitters with distinct light sources.
int main() try {
    d2d::palette::Palette::set_ramp(100, 100);   // file colours, not the shipped gamma
    using namespace d2d::client;
    Scene scene;
    std::array<d2d::palette::Palette, 32> palettes;
    for (std::size_t bank = 0; bank < palettes.size(); ++bank) {
        std::array<d2d::palette::Rgba, 256> colours{};
        colours[1] = { std::uint8_t(bank), 123, 231, 255 };
        palettes[bank] = d2d::palette::Palette(colours);
    }

    d2d::dt1::Tile tile;
    tile.width = 160;
    tile.height = 80;
    tile.type = 15;
    tile.roof_height = 1;
    tile.pixels.resize(std::size_t(tile.width * tile.height));
    tile.pixels[0] = 1;
    d2d::game::Level level;
    level.ds1 = d2d::ds1::Map(1, 1, 0, 0);
    level.picks = { { { 0, 15, &tile } } };

    Lighting lighting;
    lighting.pal = &palettes;
    lighting.ambient = 140;                         // the level: palette 17
    lighting.environment = 128;                     // the environment: palette 16
    lighting.grid.reset(0, 0, 176);                  // a locally lit flat roof: palette 22
    std::vector<std::uint8_t> framebuffer(std::size_t(kScreenWidth) * kScreenHeight * 4);
    const bool cutout = g_roof_cutout;
    g_roof_cutout = false;

    const auto check = [&](int expected_bank) {
        std::ranges::fill(framebuffer, 0);
        render_world(framebuffer, scene, level, 0, 0, 0, {}, -1, -1, nullptr, &lighting);
        int drawn = 0;
        for (std::size_t pixel = 0; pixel < framebuffer.size(); pixel += 4) {
            if (!framebuffer[pixel + 3]) continue;
            assert(framebuffer[pixel] == expected_bank);
            assert(framebuffer[pixel + 1] == 123 && framebuffer[pixel + 2] == 231);
            assert(framebuffer[pixel + 3] == 255);
            ++drawn;
        }
        assert(drawn == 1);                         // the assertion reached a visible roof pixel
    };
    check(16);
    lighting.ambient = 8;
    lighting.grid.reset(0, 0, 248);
    check(16);                                     // neither level light nor lamps affect a raised roof
    lighting.environment = 64;
    check(8);                                      // changing the environment does affect it
    tile.roof_height = 0;
    check(31);                                     // a flat roof still samples the local grid
    lighting.environment = 200;
    check(31);                                     // the environment does not replace a flat roof's grid
    g_roof_cutout = cutout;
    std::puts("test_roof_light: ok");
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_roof_light: %s\n", error.what());
    return 1;
}
