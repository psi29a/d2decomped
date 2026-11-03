// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Throwing Mastery (FUN_00645720): with a throwable in hand and Throw,
// Left Hand Throw or Double Throw in use (range rng, itypea1 thro), the
// throw masteries (345..347) take the melee ones' place (342..344) in the
// REAL Fight::player_fighter; with another skill, the melee ones. Needs
// the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace d2d::game;

int main() {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 4, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 4;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    namespace loc = d2d::d2s::item_location;
    namespace body = d2d::d2s::body_location;
    auto& items = world.actor->character.items;
    std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == loc::kEquipped && (item.slot == body::kRightArm || item.slot == body::kLeftArm); });
    d2d::d2s::Item javelins;
    javelins.code = "jav"; javelins.location = loc::kEquipped; javelins.slot = body::kRightArm; javelins.id = 9102; javelins.quantity = 5;
    items.push_back(javelins);
    const std::vector<d2d::rules::PassiveStat> passives{ { d2d::d2s::kPassiveMasteryMeleeToHit, 10, "jave" }, { d2d::d2s::kPassiveMasteryThrowToHit, 40, "thro" },
                                                         { d2d::d2s::kPassiveMasteryThrowDamage, 25, "thro" } };
    const auto with = [&](const char* skill) {
        world.actor->attack_skill = data->skills.by_name.at(skill);
        d2d::rules::StatSum sum{};
        (void)world.fight.player_fighter(nullptr, nullptr, &passives, &sum);
        return sum;
    };
    for (const char* thrower : { "Throw", "Double Throw" }) {
        const auto sum = with(thrower);
        assert(sum[d2d::d2s::kPassiveMasteryMeleeToHit] == 40 && sum[d2d::d2s::kPassiveMasteryMeleeDamage] == 25);
    }
    const auto stabbing = with("Attack");
    assert(stabbing[d2d::d2s::kPassiveMasteryMeleeToHit] == 10 && stabbing[d2d::d2s::kPassiveMasteryMeleeDamage] == 0);
    return 0;
}
