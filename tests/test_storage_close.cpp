// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The stash, the Cube and a store beside the inventory (apps/d2d/storage_close.hpp):
// the inventory's close button shuts the one that's up, as their mouse-up
// handlers in game.exe do (FUN_00489ac0, FUN_0048a190, FUN_00488b00);
// the panel keys pass or not as FUN_00453910 lets them, run by
// tools/emu/panel_keys.py (panel_keys_cases.inc).
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
    std::printf("OK: the inventory's close button shuts the stash, Cube or store; %zu panel keys as game.exe's\n", sizeof(key_cases) / sizeof(key_cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_storage_close: %s\n", error.what());
    return 1;
}
