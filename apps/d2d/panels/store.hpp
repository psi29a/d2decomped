// SPDX-License-Identifier: GPL-3.0-or-later
// Vendor store: stock, buy/sell, panel and gold readouts.
#pragma once

#include "panels/panels.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s_items.hpp>
#include <hire.hpp>
#include <rules.hpp>
#include <vendor.hpp>
#include <wear.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

// The hire list (FUN_004b5c60): a 490x350 NPC text window, header
// "Your Gold: %d     Hire which Mercenary?" (0xd24, carried + stash gold)
// in gold, a list box (rules/hire_list.hpp, FUN_004bf8f0) of the offers,
// two lines each, and "cancel" (0xd48). Enter hires the selected row
// (rules::list_enter, session/update.cpp).
NpcMenuState open_hire_menu(const Scene& scene, int npc, const std::vector<d2d::rules::MercOffer>& offers,
                            std::int64_t gold);

Scene::InvLayout store_grid(int panel_x = kCharPanelX);

// The store item under the cursor (index into the open tab), or -1.
int store_item_at(const Scene& scene, const Store& store, int mouse_x, int mouse_y, int panel_x = kCharPanelX);

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
// The tab labels: x centre and string, 18-byte records at 0x722110.
inline constexpr int kTabLabelX[4] = { 42, 121, 201, 281 };
inline constexpr std::uint16_t kTabString[4] = { 0xfc4, 0xfc5, 0xfc5, 0xfc7 };
// A button's hover (FUN_004886c0, box x .. x+0x2d by 432..474): its
// string centred on x 80 + its table x, bottom 440; Repair all's (0x276f)
// with what it would cost.
void draw_store(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Store& store, int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer,
                const std::vector<d2d::d2s::Item>* items = nullptr, int panel_x = kCharPanelX);

// Gold readouts (FUN_00488100, docs/research/re/store.md), font16 white,
// baselines at 800x600: the inventory's carried gold (stat 14) at x 508,
// y 468 after the goldcoinbtn (frame 0, bottom-left 484,469); with a
// store open, "Stash" (0xcf3) at x 101, y 434 and the stash gold (15)
// right-aligned to x 278.
// The coins open the gold box (GoldBox below).
void draw_gold(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats, bool store);
// The stash's (FUN_00488100 with 0): goldcoinbtn at (155, bottom 100),
// the stash gold at x 245, baseline 100, and over the coin's box
// (stash_coin_at) string 0x101c at x 133, baseline 82; a classic game's
// all stash_gold_drop lower.
void draw_stash_gold(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats, int mouse_x, int mouse_y,
                     bool expansion = true);
// The gold box (0x454150): how much gold to drop (mode 0 / 1), offer in a
// trade (2), deposit (3) or withdraw (4); prompts 0xfc1 / 0xfce / 0xfd1 /
// 0xfd2 wrapped to 200 px from 24 down. MENU\dialogbackground (210x158)
// top-left at (215, 140); MENU\spinner (frames 0 up, 2 down, +1 held) up
// arrow bottom 219, down 232, at x 224; the amount (an edit box up to the
// carried gold, the stash's when withdrawing; 10 digits) bottom 228 from
// x 258, 100 wide; buysellbtn OK (frame 16, 0xd49) and Cancel (10,
// 0x102e) bottom 287 at x 250 / 355. The prompt is Font16: the box's only
// set_font is ECX 1 (0x454285, before FUN_00502970(200)). The amount is
// too: FUN_004bbd80's last argument 1 is the edit box's font (+0x4e, set
// at 0x4bb246 / 0x4bb7fa). A button's label (FUN_004bb0f0; the 0x7275a8
// table's string) shows only while the mouse is over its art, edges in
// (ButtonImplementation's draw 0x4bb000, vtable 0x6da6f4 slot 1): Font16
// (ECX 1 at 0x4bb088), colour 0, centred by (art width - text) / 2,
// baseline the art's top - 5. Positions as the 800x600 game reads them.
struct GoldBox {
    int mode = 0;
    std::string amount;            // as typed
    int spin = 0;                  // the spinner held: +1 up, -1 down
    std::uint32_t spin_ms = 0, next_ms = 0;   // pressed at, last stepped at (FUN_004bbfb0: +0x44 / +0x48)
    std::int64_t draws = 0;        // the frames drawn while held (FUN_004bbe70: +0x40)
    bool ok_down = false, cancel_down = false;
};
enum class GoldBoxPart { kNone, kUp, kDown, kOk, kCancel };
GoldBoxPart gold_box_at(int mouse_x, int mouse_y);
void draw_gold_box(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const GoldBox& box, std::uint32_t now_ms, int mouse_x, int mouse_y);

// FUN_00486820's boxes, 20x18 up from the bottom-left: the stash's coin
// at (153, 102) (a classic game's stash_gold_drop lower), the inventory's
// at (483, 471).
bool stash_coin_at(int mouse_x, int mouse_y, bool expansion = true);
bool inventory_coin_at(int mouse_x, int mouse_y);

}  // namespace d2d::client
