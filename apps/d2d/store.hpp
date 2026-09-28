// Vendor store: stock, buy/sell, panel and gold readouts.
#pragma once

#include "scene.hpp"
#include "ui.hpp"

namespace d2d::client {

// The hire list (FUN_004b5c60): a 490x350 NPC text window, header
// "Your Gold: %d     Hire which Mercenary?" (0xd24, carried + stash gold)
// in gold, one line per offer — its name, " - ", then "Lvl" (0xd28),
// "Life" (0xd26), "Def" (0xd27), "Cost" (0xd29), each ": %u" — and
// "cancel" (0xd48).
// ponytail: one centred text line per offer in the NPC-menu style; the
// game's scrolling list widget (FUN_004bf8f0, 490x280 at (W-490)/2,
// H/2-160, rows 35 high) and the second line of hire description aren't
// drawn.
NpcMenuState open_hire_menu(const Scene& scene, int npc, const std::vector<d2d::rules::MercOffer>& offers,
                            std::int64_t gold);

// The store item under the cursor (index into the open tab), or -1.
int store_item_at(const Scene& scene, const Store& store, int mouse_x, int mouse_y);

std::array<int, 4> store_button_frames(const Store& store);

// The vendor store (panel 0xc). Layout from FUN_00488400: buysell.dc6
// as the 2x2 left panel; tabs (buyselltabs, frame i active / i+4 not) at
// x 80 + 80i, bottom 90, labels from the 18-byte records at 0x722110
// (x 42/121/201/281, baseline 79, font16, gold when active: Armor,
// Weapons, Weapons, Misc); buttons (buysellbtn, frame base + pressed) at
// x 80 - 1 + {116, 169, 221, 273}, bottom 476: buy (2), sell (4), then
// repair (6) and repair all (18) at repair vendors, else an empty slot
// (0) and close (10) (FUN_00487ed0). Stock grid: inventory.txt "Monster2"
// (10x10 at 96,123).
void draw_store(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Store& store, int mouse_x, int mouse_y, int clvl);

// Gold readouts (FUN_00488100, docs/research/re/store.md), font16 white,
// baselines at 800x600: the inventory's carried gold (stat 14) at x 508,
// y 468 after the goldcoinbtn (frame 0, bottom-left 484,469); with a
// store open, "Stash" (0xcf3) at x 101, y 434 and the stash gold (15)
// right-aligned to x 278.
// ponytail: the coin button doesn't click (no gold drop/withdraw yet).
void draw_gold(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats, bool store);

}  // namespace d2d::client
