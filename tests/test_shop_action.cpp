// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The client's shop click (FUN_004b3870), its repair gate (FUN_004b1f80) and
// the NPC window's line select (FUN_004b82f0), each on the cases
// tools/emu/shop_action.py ran in game.exe (shop_action_cases.inc,
// shop_gate_cases.inc, dialog_select_cases.inc).
#include "d2s_items.hpp"
#include "rules.hpp"
#include "shop_action.hpp"
#include <item_price.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

// Replays tools/emu/shop_action.py's game.exe cases (FUN_004b3870, FUN_004b1f80, FUN_004b82f0) against rules::.
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
    // An unidentified stack short of its max: Repair All's test (rules::needs_repair) counts it, FUN_004b1f80's
    // refuses it at FUN_0062e660 (identified is required first), so the repair click asks nothing.
    Tables tables;
    tables.types["tkf"].repair = true;
    tables.types["tkf"].throwable = true;
    tables.item_info["tkn"] = { .type = "tkf", .kind = 2 };
    tables.item_base["tkn"].stackable = true;
    tables.item_base["tkn"].max_stack = 60;
    tables.item_base["tkn"].durability = 0;
    d2d::d2s::Item knives;
    knives.code = "tkn";
    knives.quantity = 20;
    knives.identified = false;
    assert(needs_repair(tables, knives));
    assert(!repair_click_gate(tables, knives));
    knives.identified = true;
    assert(repair_click_gate(tables, knives));
    knives.quantity = 60;
    assert(!repair_click_gate(tables, knives));
    // FUN_0062a840: a charged skill (stat 0xcc) at its most asks nothing, one below it does.
    knives.props.push_back({ .stat = d2d::d2s::kChargedSkill, .param = 0, .value = (10 << 8) | 10 });
    assert(!repair_click_gate(tables, knives));
    knives.props.back().value = (10 << 8) | 9;
    assert(repair_click_gate(tables, knives));
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
