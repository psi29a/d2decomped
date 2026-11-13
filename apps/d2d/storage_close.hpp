// SPDX-License-Identifier: GPL-3.0-or-later
// The stash, the Cube and a store beside the inventory: what shuts them
// (docs/research/re/menu.md "The view with panels open").
#pragma once

namespace d2d::client {

// What the inventory's close button shuts. game.exe draws the inventory
// with the stash, the Cube or a store open (FUN_00456ee0 runs FUN_0048edf0
// for UI 0x19, 0x1a, 0xc as for UI 1), and each one's mouse-up handler
// takes a release over the inventory's close button (FUN_00486e10), with
// nothing in the hand, as its own close: the stash's (FUN_00489ac0, UI 0x19
// at 0x489b54), the Cube's (FUN_0048a190, UI 0x1a at 0x48a208), a store's
// (FUN_00488b00 -> FUN_004b3ed0: UI 0xc and the NPC's UI 8).
enum class InventoryCloseShuts { kInventory, kStash, kCube, kStore };
[[nodiscard]] constexpr InventoryCloseShuts inventory_close_shuts(bool stash_open, bool cube_open, bool store_open) {
    if (stash_open) return InventoryCloseShuts::kStash;
    if (cube_open) return InventoryCloseShuts::kCube;
    if (store_open) return InventoryCloseShuts::kStore;
    return InventoryCloseShuts::kInventory;
}

} // namespace d2d::client
