// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A two-weapon blow's hand (FUN_00535bc0 by the skill's weapsel, Skills +0x168; FUN_00623c80 turns
// that weapon's stats on and the other's off): Attack (0) the right hand's, Frenzy (3) the right on
// its odd events and the left on its even ones (the row it turns to another target on), each with
// only its own weapon's stats. One weapon: the player's fighter. Double Throw throws one hand's
// weapon a sequence event (FUN_005d88b0).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <skills.hpp>
#include <test_data.hpp>
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

namespace {

// Without the game data (CI): the rule on typed-in rows. Each hand's fighter by weapsel (0 the right,
// 3 the left on even events), a hand a blow names (Whirlwind's), one fighter with one weapon; one
// hand's throw spends that hand's knife.
void synthetic() {
    d2d::test::TestData test;
    auto& attack = test.skill(0, "Attack");
    attack.weapsel = 0;
    auto& frenzy = test.skill(147, "Frenzy");
    frenzy.weapsel = 3;
    auto& double_throw = test.skill(140, "Double Throw");
    double_throw.weapsel = 3;
    auto& knife = test.data.missiles["throwknife"];
    knife.range = 10;
    test.data.thrown["tkf"] = "throwknife";
    World world(&test.data, -1, -1);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    actor.player_combat.phys_hi = 300;
    actor.hand_combat[0].phys_hi = 100;
    actor.hand_combat[1].phys_hi = 200;
    assert(fight.blow_fighter(&attack).phys_hi == 300);               // one weapon: the player's
    actor.two_hands = true;
    assert(fight.blow_fighter(&attack).phys_hi == 100);               // weapsel 0: the right hand's
    assert(fight.blow_fighter(&frenzy).phys_hi == 100);               // no sequence running: the right
    const std::array<d2d::rules::SeqFrame, 4> frames{};
    actor.seq = frames;
    actor.seq_struck = 1;
    assert(fight.blow_fighter(&frenzy).phys_hi == 100);
    actor.seq_struck = 2;
    assert(fight.blow_fighter(&frenzy).phys_hi == 200);               // the even event: the left
    actor.seq = {};
    actor.blow_hand = 1;
    assert(fight.blow_fighter(&attack).phys_hi == 200);               // a blow naming its hand (Whirlwind's second)
    actor.blow_hand = -1;
    // One hand's throw (FUN_005d88b0): that hand's knife flies and one of it is spent, the other's not.
    Level open;
    open.ds1 = d2d::ds1::Map(9, 9, 1, 1);
    open.walk.assign(40 * 40, 0);
    fight.enter(&open);
    actor.character.items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm, .quantity = 5 });
    actor.character.items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kLeftArm, .quantity = 7 });
    actor.player.x = actor.player.y = 4.f;
    actor.cast_x = 6.f; actor.cast_y = 4.f;
    fight.throw_hand(double_throw, 1, d2d::d2s::body_location::kLeftArm, 1000);
    assert(fight.pending.size() == 1 && actor.character.items[0].quantity == 5 && actor.character.items[1].quantity == 6);
    std::printf("synthetic: ok\n");
}

}  // namespace

int main() try {
    synthetic();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: the 1.14d rows and a whole fight: no d2data.mpq in %s\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: the 1.14d rows and a whole fight: set D2_PATCH_INSTALLER\n"); return 0; }

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
    // Double Throw (weapsel 3, its sequence): FUN_005d88b0 on each event throws one hand's knife, the
    // right on the odd events and the left on the even ones, each spending one of that hand's.
    {
        const int double_throw = data->skills.by_name.at("Double Throw");
        fight.skill_level = [&](int skill) { return skill == double_throw ? 1 : 0; };
        std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped
                                                                     && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm); });
        items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm, .identified = true, .quantity = 10 });
        items.back().wielded = true;
        items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kLeftArm, .identified = true, .quantity = 20 });
        world.actor->character.stats.values[d2d::d2s::kMana] = 200 << 8;
        world.actor->pmode = -1;
        world.actor->seq = {};
        world.fight.missiles.clear(); world.fight.pending.clear();
        std::uint32_t clock_ms = 3000 * kTickMs;
        fight.update_fighters(clock_ms);
        assert(fight.cast_missile(double_throw, world.actor->player.x + 4, world.actor->player.y, clock_ms));
        assert(!world.actor->seq.empty() && world.fight.pending.empty());   // nothing thrown yet: the events throw
        const auto quantity = [&](int slot) { return std::ranges::find_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; })->quantity; };
        int thrown = 0, right_before = quantity(d2d::d2s::body_location::kRightArm), left_before = quantity(d2d::d2s::body_location::kLeftArm);
        for (int tick = 0; tick < 200 && world.actor->pmode >= 0; ++tick) {
            clock_ms += kTickMs;
            fight.player_modes(true, clock_ms, 0.04f);
            const int now_thrown = int(world.fight.pending.size());
            if (now_thrown > thrown) {
                assert(now_thrown == thrown + 1);                          // one hand an event
                const bool left_hand = (now_thrown % 2) == 0;
                assert(quantity(left_hand ? d2d::d2s::body_location::kLeftArm : d2d::d2s::body_location::kRightArm) == (left_hand ? --left_before : --right_before));
                thrown = now_thrown;
            }
        }
        assert(thrown >= 2);
        assert(world.actor->character.stats.values[d2d::d2s::kMana] < (200 << 8));   // its cost paid, on the first event
        std::printf("Double Throw: %d knives over its sequence\n", thrown);
        // Lightning's sequence counts as a cast (seqtrans SC): it stays on the cast's timing, no attack sequence.
        const int lightning = data->skills.by_name.at("Lightning");
        assert(data->skills.get(lightning)->anim == "SQ" && data->skills.get(lightning)->seqtrans == "SC");
        fight.skill_level = [&](int skill) { return skill == lightning ? 1 : 0; };
        world.actor->pmode = -1;
        world.actor->seq = {};
        world.actor->character.stats.values[d2d::d2s::kMana] = 200 << 8;
        clock_ms += 50 * kTickMs;
        assert(fight.cast_missile(lightning, world.actor->player.x + 4, world.actor->player.y, clock_ms));
        assert(world.actor->pmode == kModeSC && world.actor->seq.empty());
    }
    std::printf("test_two_hands_blow: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_two_hands_blow: %s\n", error.what());
    return 1;
}
