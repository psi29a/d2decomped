// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The char panel's two hands (FUN_004e99f0 / FUN_004e93a0): a Barbarian with a weapon in each hand
// shows Attack's damage (descdam 1) for each weapon alone (FUN_004e9300 / FUN_004e9260 turn the other
// weapon's stats off, FUN_00627910); with a shield in the other hand, one value. Frenzy (descdam 19)
// shows two as well. A Sorceress never has two.

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

    // The axe with its shield: one value.
    fight.update_fighters(now);
    const auto alone = world.actor->character.panel.attack[1];
    assert(alone.skill == 0 && alone.damage && !alone.two && alone.max > 0);
    assert(!world.actor->character.panel.attack[0].damage);           // Frenzy needs two weapons: refused, no line

    // A second axe for the shield, with +20 maximum damage on it: the wielded axe's value stays as it was
    // alone (the other's stats off), the other's is its own with the +20.
    auto left = in_hand(d2d::d2s::body_location::kLeftArm);
    if (left == items.end()) { items.push_back(*right); left = items.end() - 1; }
    *left = *right;
    left->slot = d2d::d2s::body_location::kLeftArm;
    left->wielded = false;                                         // one wielded weapon: the right axe (FUN_0063bef0)
    left->props.push_back({ d2d::d2s::kMaxDamage, 0, 20 });
    fight.update_fighters(now);
    const auto both = world.actor->character.panel.attack[1];
    assert(both.damage && both.two);
    assert(both.min == alone.min && both.max == alone.max);
    assert(both.min2 == alone.min && both.max2 > alone.max);
    std::printf("OK: Attack %d-%d over %d-%d with two axes\n", both.min, both.max, both.min2, both.max2);
    assert(world.actor->character.panel.attack[0].skill == frenzy && world.actor->character.panel.attack[0].two);

    // Bare hands (the axe off, a buckler with +100 strength in the other hand): the whole strength,
    // gear included, joins the %, so the line grows with the bonus.
    items.erase(left);
    items.erase(in_hand(d2d::d2s::body_location::kRightArm));
    d2d::d2s::Item buckler;
    buckler.code = "buc"; buckler.location = d2d::d2s::item_location::kEquipped; buckler.slot = d2d::d2s::body_location::kLeftArm;
    items.push_back(buckler);
    fight.update_fighters(now);
    const auto plain_bare = world.actor->character.panel.attack[1];
    in_hand(d2d::d2s::body_location::kLeftArm)->props.push_back({ d2d::d2s::kStr, 0, 100 });
    fight.update_fighters(now);
    const auto strong_bare = world.actor->character.panel.attack[1];
    assert(plain_bare.damage && strong_bare.max > plain_bare.max);
    // A throwing axe with Throwing Mastery: Attack's rating takes the melee masteries whatever the last
    // attack was (a Throw makes the fight's fighter take the throw ones).
    const int throw_skill = data->skills.by_name.at("Throw"), throwing_mastery = data->skills.by_name.at("Throwing Mastery");
    fight.skill_level = [&](int skill) { return skill == 0 || skill == frenzy || skill == throw_skill || skill == throwing_mastery ? 10 : 0; };
    items.erase(in_hand(d2d::d2s::body_location::kLeftArm));
    d2d::d2s::Item axe;
    axe.code = "tax"; axe.location = d2d::d2s::item_location::kEquipped; axe.slot = d2d::d2s::body_location::kRightArm; axe.wielded = true; axe.quantity = 20;
    items.push_back(axe);
    world.actor->attack_skill = 0;
    fight.update_fighters(now);
    fight.update_fighters(now);
    const int after_attack = world.actor->character.panel.attack[1].attack_rating;
    world.actor->attack_skill = throw_skill;
    fight.update_fighters(now);
    fight.update_fighters(now);
    assert(after_attack > 0 && world.actor->character.panel.attack[1].attack_rating == after_attack);
    // The same items on a Sorceress: no two hands (FUN_006235a0).
    world.actor->character.character_class = 1;
    fight.update_fighters(now);
    assert(!world.actor->character.panel.attack[1].two && world.actor->character.panel.attack[1].damage);
    std::printf("test_panel_hands: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_panel_hands: %s\n", error.what());
    return 1;
}
