// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The stash, the Cube and a store beside the inventory (apps/d2d/storage_close.hpp):
// the inventory's close button shuts the one that's up, as their mouse-up
// handlers in game.exe do (FUN_00489ac0, FUN_0048a190, FUN_00488b00);
// the panel keys pass or not as FUN_00453910 lets them, run by
// tools/emu/panel_keys.py (panel_keys_cases.inc); the stash's and the
// Cube's close buttons where FUN_00489980 / FUN_00489fb0 take them, run by
// tools/emu/storage_close.py (storage_close_cases.inc).
#include "storage_close.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using d2d::client::InventoryCloseShuts;
    using d2d::client::inventory_close_shuts;
    assert(inventory_close_shuts(false, false, false) == InventoryCloseShuts::kInventory);
    assert(inventory_close_shuts(true, false, false) == InventoryCloseShuts::kStash);
    assert(inventory_close_shuts(false, true, false) == InventoryCloseShuts::kCube);
    assert(inventory_close_shuts(false, false, true) == InventoryCloseShuts::kStore);
    struct KeyCase { int raised, key, allowed; };
    const KeyCase key_cases[] = {
#include "panel_keys_cases.inc"
    };
    for (const auto& row : key_cases) {
        const bool refused = d2d::client::panel_key_refused(row.key, row.raised == 0x19, row.raised == 0x1a, row.raised == 0xc);
        if (refused == (row.allowed != 0))
            std::fprintf(stderr, "FAIL: UI 0x%x up, UI %d: d2d %s, game.exe %s\n", row.raised, row.key, refused ? "refuses" : "allows", row.allowed ? "allows" : "refuses");
        assert(refused != (row.allowed != 0));
    }
    struct HitCase { int kind, expansion, mouse_x, mouse_y, over; };
    const HitCase hit_cases[] = {
#include "storage_close_cases.inc"
    };
    for (const auto& row : hit_cases) {
        const bool over = row.kind == 1 ? d2d::client::over_stash_close(row.expansion != 0, row.mouse_x, row.mouse_y)
                                        : d2d::client::over_cube_close(row.mouse_x, row.mouse_y);
        if (over != (row.over != 0))
            std::fprintf(stderr, "FAIL: kind %d expansion %d (%d, %d): d2d %d, game.exe %d\n", row.kind, row.expansion, row.mouse_x, row.mouse_y, int(over), row.over);
        assert(over == (row.over != 0));
    }
    // Drawn where FUN_0048edf0 puts it.
    static_assert(d2d::client::stash_close(false).left == 80 + 0x113 && d2d::client::stash_close(false).bottom == 540 - 0x41);
    static_assert(d2d::client::stash_close(true).left == 80 + 0x110 && d2d::client::stash_close(true).bottom == 540 - 0x40);
    static_assert(d2d::client::cube_close().left == 80 + 0x113 && d2d::client::cube_close().bottom == 540 - 0x41);
    std::printf("OK: the inventory's close button shuts the stash, Cube or store; %zu panel keys, %zu close-button points as game.exe's\n",
                sizeof(key_cases) / sizeof(key_cases[0]), sizeof(hit_cases) / sizeof(hit_cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_storage_close: %s\n", error.what());
    return 1;
}
