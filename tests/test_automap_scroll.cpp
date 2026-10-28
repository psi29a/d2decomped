// SPDX-License-Identifier: GPL-3.0-or-later
// The automap's scroll (rules/automap_scroll.hpp) vs game.exe's own
// FUN_00459700 (tools/emu/automap_scroll.py), full and mini, both corners,
// portraits, side panels.
#include <automap_scroll.hpp>

#include <cassert>
#include <cstdio>
#include <iterator>
#include <utility>

int main() {
    struct Case { int client_x, client_y, mini, on_left, portraits, side, scroll_x, scroll_y; };
    constexpr Case kOracle[] = {
#include "automap_scroll_cases.inc"
    };
    for (const auto& oracle : kOracle) {
        const auto scroll = d2d::rules::automap_scroll(oracle.client_x, oracle.client_y, 800, 600, oracle.mini != 0, oracle.on_left != 0,
                                                       oracle.portraits != 0, oracle.side);
        if (scroll.x != oracle.scroll_x || scroll.y != oracle.scroll_y)
            std::printf("(%d, %d) mini %d left %d portraits %d side %d: got (%d, %d) want (%d, %d)\n", oracle.client_x, oracle.client_y,
                        oracle.mini, oracle.on_left, oracle.portraits, oracle.side, scroll.x, scroll.y, oracle.scroll_x, oracle.scroll_y);
        assert(scroll.x == oracle.scroll_x && scroll.y == oracle.scroll_y);
    }
    std::printf("automap scroll: %zu cases match game.exe\n", std::size(kOracle));

    // FUN_00643340 and FUN_006433c0 under tools/emu: (2y + x, 2y - x) tiles, and a tile's corner.
    struct Tile { int client_x, client_y, tile_x, tile_y; };
    constexpr Tile kTiles[] = { { 0, 0, 0, 0 }, { 159, 0, 0, -1 }, { 160, 0, 1, -2 }, { -1, 0, -1, 0 }, { -160, 0, -2, 1 },
                                { -161, 0, -2, 1 }, { 0, 80, 1, 1 }, { -400, -280, -7, -2 }, { 12345, -6789, -8, -163 },
                                { -12345, 6789, 7, 162 }, { 15600, 7720, 194, -2 }, { 799, -1, 4, -6 }, { -800, -80, -7, 4 } };
    for (const auto& tile : kTiles)
        assert(d2d::rules::client_tile(2 * tile.client_y + tile.client_x) == tile.tile_x
               && d2d::rules::client_tile(2 * tile.client_y - tile.client_x) == tile.tile_y);
    // The grid at 800 x 600 (view 800 x 560): 34 tiles square, from the
    // view's tile (-3, -16). The player at tile (100, 100): the view's top
    // left is client (-400, 16000 - 280), tile (94, 99); the grid x 91..124,
    // y 83..116.
    const auto in_grid = [](float tile_x, float tile_y) {
        const auto client = [](float x, float y) { return std::pair{ int((x - y) * 80), int((x + y) * 40) }; };
        return d2d::rules::in_unit_grid(0 - 400, 8000 - 280, 800, 560, client(tile_x, tile_y).first, client(tile_x, tile_y).second);
    };
    assert(in_grid(100.5f, 100.5f) && in_grid(91.5f, 83.5f) && in_grid(124.5f, 116.5f));
    assert(!in_grid(90.5f, 100.5f) && !in_grid(125.5f, 100.5f) && !in_grid(100.5f, 82.5f) && !in_grid(100.5f, 117.5f));
    // A room's box against the view: room (8, 0) 8 x 8's corners (560 .. ., ...).
    assert(d2d::rules::room_in_view(0, 0, 800, 560, 0, 0, 8, 8) && !d2d::rules::room_in_view(0, 0, 800, 560, 40, 40, 8, 8));
    std::printf("automap unit grid: game.exe's tiles and bounds\n");
    return 0;
}
