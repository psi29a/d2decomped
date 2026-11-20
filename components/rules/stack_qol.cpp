// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of stack_qol.hpp's functions; their notes are in the header.
#include "stack_qol.hpp"

#include <string_view>

namespace d2d::rules {

int stack_cap(StackKind kind, bool enabled, StackGrid grid, int max_per_stack) {
    if (!enabled || kind == StackKind::None || grid == StackGrid::Belt || grid == StackGrid::Other || max_per_stack < 1) return 1;
    return max_per_stack < kStackMax ? max_per_stack : kStackMax;
}

StackMerge merge_counts(StackKind have_kind, std::string_view have_code, int have_count,
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

StackPeel peel_one(int have_count) {
    if (have_count < 2) return { have_count > 0 ? have_count : 0, 0, false };
    return { have_count - 1, 1, true };
}

}
