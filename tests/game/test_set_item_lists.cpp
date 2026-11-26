// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A set item's own bonus lists switched by its set's worn members, against
// stock 1.14d's FUN_00663a20 run by tools/emu/set_item_lists.py: every add
// func 0..3, own index 0..5 and mask below 0x40 (set_item_lists_cases.inc),
// through game::set_bonus_props. No game data needed.
#include <character.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>

#include <cstddef>
#include <cstdio>
#include <exception>
#include <vector>

namespace {

struct Case { int add_func, own, mask, lists_on; };

constexpr Case kCases[] = {
#include "set_item_lists_cases.inc"
};

// A worn piece of set 0 whose SetItems row is `row` (its member index).
d2d::d2s::Item worn(int row) {
    d2d::d2s::Item piece{ .code = "cap" };
    piece.quality = 5;
    piece.set_id = row;
    piece.location = d2d::d2s::item_location::kEquipped;
    piece.slot = d2d::d2s::body_location::kFirst;
    return piece;
}

// d2d's lists on for one case: the item with all five lists (list i holds
// stat i + 1), the other members in the mask worn; its own bit clear means
// it isn't counted (flag 0x4000, as FUN_0062a370 leaves it out).
int lists_on(const Case& one) {
    d2d::game::GameData game_data;
    game_data.rules.sets.resize(6);
    for (int row = 0; row < 6; ++row) {
        auto& special = game_data.rules.sets[std::size_t(row)];
        special.set = 0;
        special.member = row;
        special.add_func = one.add_func;
        special.bonus_apart = one.add_func != 0;
    }
    std::vector<d2d::d2s::Item> items{ worn(one.own) };
    auto& own = items.front();
    own.set_lists = 0x1f;
    own.set_list_sizes = { 1, 1, 1, 1, 1 };
    for (int list = 0; list < 5; ++list) own.set_props.push_back({ .stat = list + 1, .value = 1 });
    if (!(one.mask >> one.own & 1)) own.flags |= 0x4000u;
    for (int row = 0; row < 6; ++row)
        if (row != one.own && (one.mask >> row & 1)) items.push_back(worn(row));
    int out = 0;
    for (const auto& prop : d2d::game::set_bonus_props(game_data, items, items.front())) out |= 1 << (prop.stat - 1);
    return out;
}

}   // namespace

// Checks set_bonus_props against every frozen FUN_00663a20 case; prints the first few that differ.
int main() try {
    int bad = 0;
    for (const auto& one : kCases) {
        const int got = lists_on(one);
        if (got != one.lists_on && ++bad <= 5)
            std::printf("add func %d own %d mask %#x: game %#x d2d %#x\n", one.add_func, one.own, one.mask, one.lists_on, got);
    }
    if (bad) {
        std::printf("FAIL: %d of %zu FUN_00663a20 cases differ\n", bad, std::size(kCases));
        return 1;
    }
    std::printf("OK: %zu FUN_00663a20 cases (a set item's own lists by add func and worn members)\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_set_item_lists: %s\n", error.what());
    return 1;
}
