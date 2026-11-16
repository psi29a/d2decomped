// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A replenishing item's timer 3 (stats 0xfc / 0xfd: FUN_00562d30 over
// FUN_00562c40) and its first queueing (FUN_00558530 / FUN_00558580)
// against stock 1.14d's own, run by tools/emu/replenish.py:
// rules::replenish_step and rules::replenish_first. Then the same on
// items typed in (replenish_arm, replenish_fire). No game data needed at
// runtime. With a file argument (replenish.py's `game` output, or its
// `gen` cases) it prints each line with d2d's answer instead, to diff.
#include <d2s_items.hpp>
#include <replenish.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace d2d::rules;
using d2d::d2s::Item;

struct Case { int kind, broken, wears, stacks, owner, durability_rate, durability, durability_most, quantity_rate, quantity, quantity_most, timer; };
struct Answer {
    int durability, quantity, queued, wait, mend, told;
    friend bool operator==(const Answer&, const Answer&) = default;
};

// The owner is told (S->C, FUN_0053d130) of each point when it has a client
// (owner 3); only then can the point mend. d2d sends no packet: `told` is
// the point itself.
Answer answer(const Case& row) {
    if (row.kind != 3) {
        const auto wait = replenish_first(row.kind == kReplenishDurability ? row.durability_rate : row.quantity_rate, row.timer);
        return { row.durability, row.quantity, wait ? 1 : 0, wait.value_or(0), 0, 0 };
    }
    const bool owned = row.owner == 3;
    const auto after = replenish_step({ row.broken != 0, row.wears != 0, row.stacks != 0, owned, row.durability_rate, row.durability, row.durability_most,
                                        row.quantity_rate, row.quantity, row.quantity_most });
    const bool point = after.durability != row.durability || after.quantity != row.quantity;
    return { after.durability, after.quantity, after.wait ? 1 : 0, after.wait.value_or(0), after.mend ? 1 : 0, owned && point ? 1 : 0 };
}

void typed_items() {
    Tables tables;
    tables.item_info["tkf"] = { .type = "tkni", .kind = 2 };
    tables.item_base["tkf"] = { .stackable = true, .durability = 4, .max_stack = 160 };
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    tables.item_base["hax"] = { .durability = 40 };

    // "Replenishes quantity", 1 in 4 seconds (rate 25): 2500 / 25 + 1 = 101
    // frames to the first point, then 125 (5 seconds) between points.
    Item knives;
    knives.code = "tkf";
    knives.quantity = 158; knives.durability = knives.max_durability = 4;
    knives.props = { { .stat = kReplenishQuantity, .param = 0, .value = 25 } };
    replenish_arm(tables, knives);
    assert(knives.replenish_in == 101);
    replenish_arm(tables, knives, kReplenishQuantity);                 // one timer 3 an item (FUN_005415a0)
    assert(knives.replenish_in == 101);
    assert(!replenish_fire(tables, knives, true) && knives.quantity == 159 && knives.replenish_in == 125);
    assert(!replenish_fire(tables, knives, true) && knives.quantity == 160 && knives.replenish_in == 125);
    assert(!replenish_fire(tables, knives, true) && knives.quantity == 160 && knives.replenish_in == 0);   // full: not queued again

    // Thrown out and broken (a magic one stays, flag 0x100): its first one
    // back mends it, durability and all.
    knives.quantity = 0; knives.durability = 0; knives.flags |= 0x100u;
    replenish_arm(tables, knives, kReplenishQuantity);
    assert(replenish_fire(tables, knives, true) && knives.quantity == 1 && !broken(knives) && knives.durability == 4 && knives.replenish_in == 125);
    knives.quantity = 0; knives.durability = 0; knives.flags |= 0x100u;
    assert(!replenish_fire(tables, knives, false) && knives.quantity == 1 && broken(knives) && knives.durability == 0);   // no owner to tell: it stays broken

    // "Repairs 1 durability in 20 seconds" (rate 5): 501 frames each.
    Item axe;
    axe.code = "hax";
    axe.durability = 38; axe.max_durability = 40;
    axe.props = { { .stat = kReplenishDurability, .param = 0, .value = 5 } };
    replenish_arm(tables, axe);
    assert(axe.replenish_in == 501);
    assert(!replenish_fire(tables, axe, true) && axe.durability == 39 && axe.replenish_in == 501);
    assert(!replenish_fire(tables, axe, true) && axe.durability == 40 && axe.replenish_in == 501);
    assert(!replenish_fire(tables, axe, true) && axe.durability == 40 && axe.replenish_in == 0);
    axe.durability = 0; axe.flags |= 0x100u;                                                    // broken: timer 3 passes it by
    assert(!replenish_fire(tables, axe, true) && axe.durability == 0 && broken(axe) && axe.replenish_in == 0);

    // A rate a socket holds counts (the item's full stat); none: no timer.
    Item plain;
    plain.code = "hax";
    plain.durability = 10; plain.max_durability = 40;
    replenish_arm(tables, plain);
    assert(plain.replenish_in == 0);
    Item jewel;
    jewel.code = "jew";
    jewel.props = { { .stat = kReplenishDurability, .param = 0, .value = 2500 } };
    plain.socketed_items.push_back(jewel);
    replenish_arm(tables, plain);
    assert(plain.replenish_in == 2);
    // A rate that puts the frame at or before the game's: the next frame (FUN_005416b0).
    plain.replenish_in = 0;
    plain.socketed_items[0].props[0].value = -3;
    replenish_arm(tables, plain);
    assert(plain.replenish_in == 1);
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1) {
        std::ifstream input(argv[1]);
        if (!input) {
            std::fprintf(stderr, "test_replenish: cannot open %s\n", argv[1]);
            return 1;
        }
        std::string line;
        int line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            std::istringstream fields(line);
            long long id = 0;
            Case row{};
            if (!(fields >> id >> row.kind >> row.broken >> row.wears >> row.stacks >> row.owner >> row.durability_rate >> row.durability >> row.durability_most
                         >> row.quantity_rate >> row.quantity >> row.quantity_most >> row.timer)) {
                std::fprintf(stderr, "test_replenish: %s:%d: not a case line (want 13 numbers)\n", argv[1], line_number);
                return 1;
            }
            const auto got = answer(row);
            std::printf("%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", id, row.kind, row.broken, row.wears, row.stacks, row.owner,
                        row.durability_rate, row.durability, row.durability_most, row.quantity_rate, row.quantity, row.quantity_most, row.timer,
                        got.durability, got.quantity, got.queued, got.wait, got.mend, got.told);
        }
        if (input.bad() || !input.eof()) {
            std::fprintf(stderr, "test_replenish: %s: read error after line %d\n", argv[1], line_number);
            return 1;
        }
        return 0;
    }
    struct Row { Case in; Answer out; };
    const Row cases[] = {
#include "replenish_cases.inc"
    };
    for (const auto& row : cases) {
        const auto got = answer(row.in);
        if (!(got == row.out))
            std::fprintf(stderr, "FAIL: fn %d broken %d wears %d stacks %d owner %d, 0xfc %d (%d of %d), 0xfd %d (%d of %d), timer %d: d2d %d %d %d %d %d %d, game.exe %d %d %d %d %d %d\n",
                         row.in.kind, row.in.broken, row.in.wears, row.in.stacks, row.in.owner, row.in.durability_rate, row.in.durability, row.in.durability_most,
                         row.in.quantity_rate, row.in.quantity, row.in.quantity_most, row.in.timer, got.durability, got.quantity, got.queued, got.wait, got.mend, got.told,
                         row.out.durability, row.out.quantity, row.out.queued, row.out.wait, row.out.mend, row.out.told);
        assert(got == row.out);
    }
    typed_items();
    std::printf("OK: %zu replenish timers as game.exe's\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_replenish: %s\n", error.what());
    return 1;
}
