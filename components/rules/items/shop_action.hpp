// SPDX-License-Identifier: GPL-3.0-or-later
// The client's shop click (FUN_004b3870, UI/npcmenu.cpp), its repair gate
// (FUN_004b1f80) and the NPC window's line select (FUN_004b82f0): what a
// click on an item does while an NPC session is up, and which line of an
// NPC window is selected. docs/research/re/npc-menu.md "Shop clicks".
// Checked against game.exe by tools/emu/shop_action.py (shop_action_cases.inc,
// shop_gate_cases.inc, dialog_select_cases.inc; tests/test_shop_action.cpp).
#pragma once

#include <d2s_items.hpp>
#include <rules.hpp>

#include <algorithm>

namespace d2d::rules {

// What an NPC class's click means (FUN_004b3870's two switch tables, bytes
// 0x4b3b60 and 0x4b3be8 over the jump tables at 0x4b3b50 and 0x4b3bd8): the
// vendors that buy and sell, the repairers (they sell too unless the store's
// repair button is on), Cain's identify, and every other class: nothing.
enum class ShopKind { none, sell, repair, identify };
// What a click on an item does at this NPC class (FUN_004b3870's class switch).
ShopKind shop_kind(int npc_class);

// FUN_004b3870's inputs: the session (0x7c0d29) and its NPC (FUN_00463990
// on 0x7c0d25) up; the NPC's class; the click's sell argument (exactly 1
// means an item of the player's, anything else the vendor's stock); `direct`
// (the seventh argument: ask the server at once); `repair_all` (the eighth)
// with the repair-all flag (0x7bcc48, FUN_00489870); `waited` (500 ms or
// more since the last ask, 0x7c0df1); an item on the cursor; gamble mode
// (0x7c0db0, FUN_004b3500); the store's repair button on (0x7bcbf0 is 4,
// FUN_00489860); the item is gold (item type 4, FUN_0062b400), a vendor
// takes it (FUN_0062a130), it wants a repair (repair_gate), it is
// identified (item flag 0x10).
struct ShopClick {
    bool session = true, npc_found = true;
    int npc_class = 0;
    int sell = 0;
    bool direct = false, repair_all = false, repair_all_flag = false, waited = true, hand = false, gamble = false, repair_button = false;
    bool gold = false, sellable = true, wants_repair = true, identified = false;
};

// What it did. `mode` is 0x7c0d31 (1 buy, 2 sell, 3 repair, 4 identify; -1
// unchanged), `price_mode` the last argument of the price call (FUN_0062fdc0
// 0 buy, 1 sell, 2 gamble buy, 3 repair; FUN_0062fe60 is 3 for the whole
// worn set) or -1 for no price call, `price_all` the call is Repair All's,
// `click` the click's x and y were kept (0x7c0d4b / 4f), `pending` the
// pending item was set (0x7c0de0 on), `ask` FUN_004b2650 sent the request,
// `box` the confirm box opened (FUN_004b2f50, after sound 1 FUN_004b9a00).
struct ShopResult {
    bool handled = false;       // the return value
    int mode = -1, price_mode = -1;
    bool price_all = false, click = false, pending = false, ask = false, box = false;
    constexpr bool operator==(const ShopResult&) const = default;
};

// FUN_004b3870. A refused click returns false having changed nothing, except
// that a refusal after the click was kept (a buy with a hand full, a sale
// the vendor won't take) leaves the click's x, y (and a price already
// asked) behind, which `click` and `price_mode` report.
ShopResult shop_click(const ShopClick& request);

// FUN_004b1f80's inputs, each the answer of a leaf it asks of the item: gold
// (FUN_0062b400 is 4), FUN_0062e660, FUN_00629930, max durability
// (FUN_00625e00), durability (stat 0x48), stackable (FUN_006289f0), quantity
// (stat 0x46), max quantity (FUN_006295b0), FUN_0062a840's two answers.
struct RepairGate {
    bool gold = false, repairable = false, has_durability = false, stackable = false, charge_missing = false, charge_zero = false;
    int max_durability = 0, durability = 0, quantity = 0, max_quantity = 0;
};

// FUN_004b1f80: does the item want a repair (the repair button's click, and
// whether FUN_004b3870 goes on).
bool repair_gate(const RepairGate& gate);

// The gate for one of d2d's items: FUN_0062e660 is `repairable`, the
// durability reads are the item's, a stack is Items.txt's stackable.
bool repair_click_gate(const Tables& tables, const d2d::d2s::Item& item);

// The NPC window's selected line (+0x44; the line count +0x50, each line's
// selectable word at +0x174 + 0x110 * line). Returns false for a halt,
// leaving `selected` as it was.
bool select_line(int& selected, const std::vector<bool>& selectable, int index);

}  // namespace d2d::rules
