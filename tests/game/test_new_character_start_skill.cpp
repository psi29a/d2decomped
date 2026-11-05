// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A new character's first slot carries its start skill: FUN_00534c70 gives
// each item of CharStats' first item slot stat 0x6b (item_singleskill) at 1,
// its param the row's StartSkill (FUN_00627150(list, 0x6b, 1, skill):
// tools/emu/new_character_stacks.py). Other slots, and a class with no
// StartSkill, get none. No game data: the Sorceress's staff and Fire Bolt
// (Skills.txt Id 36) typed in.

#include <character_store.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace d2d::game;

namespace {

int start_skill_props(const d2d::d2s::Item& item) {
    return int(std::ranges::count_if(item.props, [](const auto& prop) { return prop.stat == d2d::d2s::kSingleSkill; }));
}

}  // namespace

int main() {
    constexpr int kSorceress = 1, kBarbarian = 4, kFireBolt = 36;
    GameData game_data;
    auto& tables = game_data.rules;
    tables.item_base["sst"] = { .durability = 10 };
    tables.item_info["sst"] = { .type = "staf", .kind = 2 };
    tables.item_base["hax"] = { .durability = 28 };
    tables.item_info["hax"] = { .type = "axe", .kind = 2 };
    for (const char* type : { "staf", "axe" }) tables.types[type] = {};
    auto& skills = game_data.skills;
    skills.rows.resize(kFireBolt + 2);
    skills.rows[kFireBolt] = { .id = kFireBolt, .name = "Fire Bolt" };
    skills.rows[kFireBolt + 1] = { .id = kFireBolt + 1, .name = "Warmth" };
    skills.class_ids[kSorceress] = { kFireBolt, kFireBolt + 1 };
    game_data.class_start[kSorceress] = { .items = { { "sst", "rarm", 2 }, { "sst", "", 1 } }, .start_skill = "fire bolt" };
    game_data.class_start[kBarbarian] = { .items = { { "hax", "rarm", 1 } } };

    for (std::uint32_t seed = 1; seed <= 8; ++seed) {
        d2d::rules::Rng rng(seed);
        const auto sorceress = new_character(game_data, kSorceress, "Starter", false, true, rng);
        assert(sorceress.stats.skills[0] == 1 && sorceress.stats.skills[1] == 0);   // the skill itself, at 1
        assert(sorceress.items.size() == 3);
        for (std::size_t item_index = 0; item_index < 2; ++item_index) {   // each of the first slot's
            const auto& staff = sorceress.items[item_index];
            assert(start_skill_props(staff) == 1);
            const auto prop = std::ranges::find(staff.props, d2d::d2s::kSingleSkill, &d2d::d2s::ItemProp::stat);
            assert(prop->param == kFireBolt && prop->value == 1);
        }
        assert(start_skill_props(sorceress.items[2]) == 0);                         // the second slot's: none

        const auto barbarian = new_character(game_data, kBarbarian, "Starter", false, true, rng);
        assert(start_skill_props(barbarian.items.front()) == 0);                    // no StartSkill
    }
    return 0;
}
