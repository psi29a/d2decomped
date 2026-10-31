// SPDX-License-Identifier: GPL-3.0-or-later
// Counts for a d2d-only stack of items the original game keeps one to a
// cell: potions, gems, runes, and loose scrolls. Nothing here places an
// item, draws it, or writes a save. The belt stays one item to a box.
#pragma once

#include <string_view>

namespace d2d::rules {

enum class StackKind { None, Potion, Gem, Rune, Scroll };

// A stored grid may hold a stack. The belt, and anywhere that is not one
// of those grids, stays one item.
enum class StackGrid { Inventory, Stash, Cube, Belt, Other };

inline constexpr int kStackMax = 50;

// How many of `kind` may share a cell. Off, an unknown kind, the belt, and
// any other spot cap at 1. Otherwise the cap is `max_per_stack`, and never
// above kStackMax.
[[nodiscard]] inline int stack_cap(StackKind kind, bool enabled, StackGrid grid, int max_per_stack = kStackMax) {
    if (!enabled || kind == StackKind::None || grid == StackGrid::Belt || grid == StackGrid::Other || max_per_stack < 1) return 1;
    return max_per_stack < kStackMax ? max_per_stack : kStackMax;
}

// `kept` is the pile that was already there. `left` is what did not fit.
// `merged` is true when some of the incoming count moved onto that pile.
struct StackMerge {
    int kept = 0;
    int left = 0;
    bool merged = false;
};

// Move `add_count` onto `have_count` while both are the same kind, the
// item codes match, and the pile has room under `cap`. A cap above 50 is
// read as 50. Different kinds never join, and neither do two codes (a
// health potion and a mana potion, or two gems). An empty pile, a cap of
// 1, or a full pile stays as it was.
[[nodiscard]] inline StackMerge merge_counts(StackKind have_kind, std::string_view have_code, int have_count,
                                             StackKind add_kind, std::string_view add_code, int add_count, int cap) {
    if (cap > kStackMax) cap = kStackMax;
    StackMerge out{ have_count, add_count, false };
    if (have_kind != add_kind || have_code != add_code || have_kind == StackKind::None || have_count < 1 || add_count < 1 || cap < 2) return out;
    const int room = cap - have_count;
    if (room < 1) return out;
    const int moved = room < add_count ? room : add_count;
    out.kept = have_count + moved;
    out.left = add_count - moved;
    out.merged = true;
    return out;
}

struct StackPeel {
    int kept = 0;
    int taken = 0;
    bool ok = false;
};

// Take one off a pile of two or more. A single item stays put.
[[nodiscard]] inline StackPeel peel_one(int have_count) {
    if (have_count < 2) return { have_count > 0 ? have_count : 0, 0, false };
    return { have_count - 1, 1, true };
}

}  // namespace d2d::rules
