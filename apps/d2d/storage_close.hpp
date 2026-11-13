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

// The UIs the panel keys and the mini-panel's buttons open.
inline constexpr int kUiInventory = 1, kUiCharacter = 2, kUiSkillTree = 4;
// Does game.exe let that panel open with the stash, the Cube or a store up?
// FUN_00455f20 asks FUN_00453910, which reads the table at 0x711130 (row:
// the UI up, column: the one opening; 2 refuses): the stash's (0x710a10)
// and the Cube's (0x710aa8) rows refuse UI 1, 2 and 4, a store's (0x710090)
// refuses 2 and 4 and passes 1. Oracle: tools/emu/panel_keys.py.
[[nodiscard]] constexpr bool panel_key_refused(int panel_ui, bool stash_open, bool cube_open, bool store_open) {
    const bool right_or_char = panel_ui == kUiInventory || panel_ui == kUiCharacter || panel_ui == kUiSkillTree;
    if (!right_or_char) return false;
    if (stash_open || cube_open) return true;
    return store_open && panel_ui != kUiInventory;
}

} // namespace d2d::client
