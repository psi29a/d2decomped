// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The client's shop click (FUN_004b3870), its repair gate (FUN_004b1f80) and
// the NPC window's line select (FUN_004b82f0), each on the cases
// tools/emu/shop_action.py ran in game.exe (shop_action_cases.inc,
// shop_gate_cases.inc, dialog_select_cases.inc).
#include "shop_action.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::rules;
    struct ClickCase { int session, npc, cls, sell, direct, all, all_flag, late, hand, gamble, repair_mode, gold, sellable, wants_repair, identified,
                       ret, mode, pending, price_mode, price_for, target, click, ask, box, sound; };
    const ClickCase click_cases[] = {
#include "shop_action_cases.inc"
    };
    for (const auto& row : click_cases) {
        ShopClick request;
        request.session = row.session; request.npc_found = row.npc; request.npc_class = row.cls; request.sell = row.sell; request.direct = row.direct;
        request.repair_all = row.all; request.repair_all_flag = row.all_flag; request.waited = row.late; request.hand = row.hand; request.gamble = row.gamble;
        request.repair_button = row.repair_mode == 4; request.gold = row.gold; request.sellable = row.sellable; request.wants_repair = row.wants_repair;
        request.identified = row.identified;
        ShopResult want;
        want.handled = row.ret; want.mode = row.mode; want.pending = row.pending == 1; want.price_mode = row.price_mode;
        want.price_all = row.price_for == 1; want.click = row.click; want.ask = row.ask; want.box = row.box;
        assert(row.box == row.sound);                     // the box always plays sound 1 first
        const ShopResult got = shop_click(request);
        if (!(got == want)) std::fprintf(stderr, "FAIL: shop click class 0x%x sell %d direct %d all %d hand %d\n", row.cls, row.sell, row.direct, row.all, row.hand);
        assert(got == want);
    }
    struct GateCase { int gold, repairable, has_dur, max_dur, dur, stackable, qty, max_qty, charge_missing, charge_zero, wants; };
    const GateCase gate_cases[] = {
#include "shop_gate_cases.inc"
    };
    for (const auto& row : gate_cases) {
        RepairGate gate;
        gate.gold = row.gold; gate.repairable = row.repairable; gate.has_durability = row.has_dur; gate.max_durability = row.max_dur;
        gate.durability = row.dur; gate.stackable = row.stackable; gate.quantity = row.qty; gate.max_quantity = row.max_qty;
        gate.charge_missing = row.charge_missing; gate.charge_zero = row.charge_zero;
        assert(repair_gate(gate) == (row.wants != 0));
    }
    struct SelectCase { int count, bits, index, set, value; };
    const SelectCase select_cases[] = {
#include "dialog_select_cases.inc"
    };
    for (const auto& row : select_cases) {
        std::vector<bool> selectable;
        for (int line = 0; line < row.count; ++line) selectable.push_back((row.bits >> line) & 1);
        int selected = 99;
        const bool done = select_line(selected, selectable, row.index);
        assert(done == (row.set != 0));
        assert(selected == (done ? row.value : 99));
    }
    // The old d2d bought with something on the cursor; game.exe's client never asks.
    ShopClick buy;
    buy.npc_class = 0x94; buy.hand = true;
    assert(!shop_click(buy).handled);
    buy.hand = false;
    assert(shop_click(buy).handled && shop_click(buy).mode == 1);
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_shop_action: %s\n", error.what());
    return 1;
}
