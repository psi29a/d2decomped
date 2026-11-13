// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The stash, the Cube and a store beside the inventory (apps/d2d/storage_close.hpp):
// the inventory's close button shuts the one that's up, as their mouse-up
// handlers in game.exe do (FUN_00489ac0, FUN_0048a190, FUN_00488b00).
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
    std::printf("OK: the inventory's close button shuts the stash, Cube or store\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_storage_close: %s\n", error.what());
    return 1;
}
