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

// A close button's spot: buysellbtn 10 (11 held), bottom-left anchored.
struct StorageClose { int left, bottom; };
// 800x600: the left panels' x (0x7a2858) and their foot (H + 0x7a285c).
inline constexpr int kStoragePanelX = 80, kStoragePanelFoot = 540;

// The stash's close button (FUN_0048edf0 state 0xc): x + 0x113, bottom
// foot - 0x41 (FUN_00486b00); the expansion stash (FUN_0044dcc0, the
// game's expansion flag) x + 0x110, foot - 0x40. A press over it holds it
// (FUN_00492510: 0x7bce38, frame 11), a release over it shuts the stash
// (FUN_00489ac0, UI 0x19 at 0x489b86), a hand holding an item or not.
[[nodiscard]] constexpr StorageClose stash_close(bool expansion, int panel_x = kStoragePanelX, int foot = kStoragePanelFoot) {
    return { panel_x + (expansion ? 0x110 : 0x113), foot - (expansion ? 0x40 : 0x41) };
}
// Over it (FUN_00489980), every edge excluded: left .. left + 0x28,
// bottom - 0x23 .. bottom + 5. Oracle: tools/emu/storage_close.py.
[[nodiscard]] constexpr bool over_stash_close(bool expansion, int mouse_x, int mouse_y,
                                              int panel_x = kStoragePanelX, int foot = kStoragePanelFoot) {
    const StorageClose spot = stash_close(expansion, panel_x, foot);
    return mouse_x > spot.left && mouse_x < spot.left + 0x28 && mouse_y > spot.bottom - 0x23 && mouse_y < spot.bottom + 5;
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
