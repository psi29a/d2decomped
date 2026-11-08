// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A new character's stacks come full: FUN_00534c70 sets a stackable start
// item's quantity (stat 70) to its maxstack (FUN_006295b0: Items +0xe8, at
// most 511), and FUN_00534d60 gives a quiver (ItemTypes row 5, bowq) 250 in
// a hand, 100 elsewhere. No game data: the Amazon's javelin (its weapons
// minstack 30, maxstack 60) and the rest typed in.

#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

using namespace d2d::game;

namespace {

const d2d::d2s::Item& carried(const NewCharacter& made, const std::string& code) {
    for (const auto& item : made.items)
        if (item.code == code) return item;
    assert(false && "not carried");
    return made.items.front();
}

}  // namespace

int main() try {
    GameData game_data;
    auto& tables = game_data.rules;
    tables.item_base["jav"] = { .stackable = true, .min_stack = 30, .max_stack = 60 };
    tables.item_info["jav"] = { .type = "jave", .kind = 2 };
    tables.item_base["huge"] = { .stackable = true, .min_stack = 1, .max_stack = 600 };
    tables.item_info["huge"] = { .type = "key" };
    tables.item_base["aqv"] = { .stackable = true, .min_stack = 1, .max_stack = 350 };
    tables.item_info["aqv"] = { .type = "bowq" };
    tables.item_base["cqv"] = { .stackable = true, .min_stack = 1, .max_stack = 350 };
    tables.item_info["cqv"] = { .type = "bowq" };
    tables.item_base["buc"] = { .durability = 12 };
    tables.item_info["buc"] = { .type = "shie", .kind = 1 };
    for (const char* type : { "jave", "key", "bowq", "shie" }) tables.types[type] = {};
    game_data.class_start[0].items = { { "jav", "rarm", 1 }, { "aqv", "larm", 1 }, { "cqv", "", 1 }, { "huge", "", 1 }, { "buc", "", 1 } };

    for (std::uint32_t seed = 1; seed <= 32; ++seed) {   // a drop rolls 30..60; a new character's never does
        d2d::rules::Rng rng(seed);
        const auto made = new_character(game_data, 0, "Stacks", false, true, rng);
        assert(carried(made, "jav").quantity == 60);      // full
        assert(carried(made, "huge").quantity == 511);    // FUN_006295b0's cap
        assert(carried(made, "aqv").quantity == 250);     // a quiver in a hand
        assert(carried(made, "cqv").quantity == 100);     // and elsewhere
        assert(carried(made, "buc").quantity <= 0);       // not a stack: untouched
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_new_character_stacks: %s\n", error.what());
    return 1;
}
