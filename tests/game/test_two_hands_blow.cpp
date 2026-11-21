// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A two-weapon blow's hand (FUN_00535bc0 by the skill's weapsel, Skills +0x168; FUN_00623c80 turns
// that weapon's stats on and the other's off): Attack (0) the right hand's, Frenzy (3) the right on
// its odd events and the left on its even ones (the row it turns to another target on), each with
// only its own weapon's stats. One weapon: the player's fighter.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: Skills.txt as 1.14d has it: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }

    constexpr int kBarbarian = 4;
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    const int frenzy = data->skills.by_name.at("Frenzy");
    character.header.right_skill = 0;                              // Attack
    character.header.left_skill = std::uint32_t(frenzy);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    fight.skill_level = [&](int skill) { return skill == 0 || skill == frenzy ? 1 : 0; };
    auto& items = world.actor->character.items;
    const auto in_hand = [&](int slot) {
        return std::ranges::find_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
    };
    const auto right = in_hand(d2d::d2s::body_location::kRightArm);
    assert(right != items.end() && data->rules.item_info.at(right->code).kind == 2);   // the starting hand axe
    const std::uint32_t now = 2000 * kTickMs;

    // Weapsel from the 1.14d table: Attack 0, Frenzy 3.
    assert(data->skills.get(0)->weapsel == 0 && data->skills.get(frenzy)->weapsel == 3);
    fight.update_fighters(now);
    assert(!world.actor->two_hands);                                  // the axe and a buckler: one weapon
    // A second axe, +20 maximum damage on it, in the left hand.
    auto left = in_hand(d2d::d2s::body_location::kLeftArm);
    if (left == items.end()) { items.push_back(*right); left = items.end() - 1; }
    *left = *in_hand(d2d::d2s::body_location::kRightArm);
    left->slot = d2d::d2s::body_location::kLeftArm;
    left->wielded = false;
    left->props.push_back({ d2d::d2s::kMaxDamage, 0, 20 });
    fight.update_fighters(now);
    assert(world.actor->two_hands);
    const auto& right_alone = world.actor->hand_combat[0];
    const auto& left_alone = world.actor->hand_combat[1];
    assert(left_alone.phys_hi > right_alone.phys_hi && right_alone.phys_hi < world.actor->player_combat.phys_hi + 1);
    const auto attack_blow = fight.blow_fighter(data->skills.get(0));
    assert(attack_blow.phys_hi == right_alone.phys_hi);              // Attack: the right hand's alone
    const std::array<d2d::rules::SeqFrame, 4> frames{};
    world.actor->seq = frames;                                        // a Frenzy sequence under way
    world.actor->seq_struck = 1;
    assert(fight.blow_fighter(data->skills.get(frenzy)).phys_hi == right_alone.phys_hi);
    world.actor->seq_struck = 2;
    assert(fight.blow_fighter(data->skills.get(frenzy)).phys_hi == left_alone.phys_hi);
    std::printf("test_two_hands_blow: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_two_hands_blow: %s\n", error.what());
    return 1;
}
