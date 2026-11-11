// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// d2d-only stack counts (stack_qol.hpp). Off, the belt, and an unknown
// kind stay at 1. Same kinds join up to 50. A peel takes one. No save.
#include <stack_qol.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

int main() try {
    assert(stack_cap(StackKind::Potion, false, StackGrid::Inventory) == 1);
    assert(stack_cap(StackKind::Gem, true, StackGrid::Belt) == 1);
    assert(stack_cap(StackKind::Rune, true, StackGrid::Other) == 1);
    assert(stack_cap(StackKind::None, true, StackGrid::Stash) == 1);
    assert(stack_cap(StackKind::Potion, true, StackGrid::Inventory) == 50);
    assert(stack_cap(StackKind::Scroll, true, StackGrid::Stash) == 50);
    assert(stack_cap(StackKind::Gem, true, StackGrid::Cube) == 50);
    assert(stack_cap(StackKind::Rune, true, StackGrid::Inventory, 10) == 10);
    assert(stack_cap(StackKind::Potion, true, StackGrid::Inventory, 80) == 50);
    assert(stack_cap(StackKind::Potion, true, StackGrid::Inventory, 0) == 1);
    const StackMerge room = merge_counts(StackKind::Potion, "hp1", 40, StackKind::Potion, "hp1", 20, 50);
    assert(room.merged && room.kept == 50 && room.left == 10);
    const StackMerge capped = merge_counts(StackKind::Potion, "hp1", 40, StackKind::Potion, "hp1", 20, 80);
    assert(capped.merged && capped.kept == 50 && capped.left == 10);
    const StackMerge high = merge_counts(StackKind::Potion, "hp1", 10, StackKind::Potion, "hp1", 5, 80);
    assert(high.merged && high.kept == 15 && high.left == 0);
    const StackMerge fit = merge_counts(StackKind::Gem, "gcv", 10, StackKind::Gem, "gcv", 5, 50);
    assert(fit.merged && fit.kept == 15 && fit.left == 0);
    const StackMerge kinds = merge_counts(StackKind::Potion, "hp1", 4, StackKind::Rune, "r01", 3, 50);
    assert(!kinds.merged && kinds.kept == 4 && kinds.left == 3);
    const StackMerge potions = merge_counts(StackKind::Potion, "hp1", 4, StackKind::Potion, "mp1", 3, 50);
    assert(!potions.merged && potions.kept == 4 && potions.left == 3);
    const StackMerge gems = merge_counts(StackKind::Gem, "gcv", 4, StackKind::Gem, "gcw", 3, 50);
    assert(!gems.merged && gems.kept == 4 && gems.left == 3);
    const StackMerge piled = merge_counts(StackKind::Scroll, "tsc", 50, StackKind::Scroll, "tsc", 2, 50);
    assert(!piled.merged && piled.kept == 50 && piled.left == 2);
    const StackMerge single = merge_counts(StackKind::Potion, "hp1", 1, StackKind::Potion, "hp1", 1, 1);
    assert(!single.merged && single.kept == 1 && single.left == 1);
    const StackMerge vacant = merge_counts(StackKind::Gem, "gcv", 0, StackKind::Gem, "gcv", 4, 50);
    assert(!vacant.merged && vacant.left == 4);
    const StackPeel peeled = peel_one(5);
    assert(peeled.ok && peeled.kept == 4 && peeled.taken == 1);
    const StackPeel last = peel_one(1);
    assert(!last.ok && last.kept == 1 && last.taken == 0);
    const StackPeel none = peel_one(0);
    assert(!none.ok && none.kept == 0 && none.taken == 0);
    std::puts("OK: stack counts cap at 50 and do not write a save");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_stack_qol: %s\n", error.what());
    return 1;
}
