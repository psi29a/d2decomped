// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The aura states a unit shows (Fight::aura_states_on, World::view): do 65
// (FUN_005cf010) puts aurastate on the caster and auratargetstate on each
// ally in range (FUN_0056b7e0 -> FUN_005cedc0); do 66 / 81 aurastate on the
// caster. So the merc in the player's Might shows Might, besides its own
// aura (an enemy one: Holy Freeze); the player in a totem's range shows the
// totem's target state, not its caster's; one of each name; none on a dead
// merc. The view's states cross the LAN wire (encode_view / apply_view).
// Made-up rows (test_data.hpp): the rule, not the game's numbers.

#include <ai/units.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <replication.hpp>
#include <skills.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

bool same(const std::vector<std::string_view>& states, std::initializer_list<std::string_view> expected) {
    return std::ranges::equal(states, expected);
}

}  // namespace

int main() try {
    d2d::test::TestData test;
    constexpr int kMight = 98, kHolyFreeze = 114, kOakSage = 226;
    const auto aura = [&](int id, const char* name, d2d::rules::ServerDoFunction kind, const char* state, const char* target) {
        auto& skill = test.skill(id, name);
        skill.srvdofunc = kind;
        skill.aura = true;
        skill.aurastate = state;
        skill.auratarget = target;
        skill.aurarange.code = { { d2d::rules::Calc::Const, 10 } };   // 10 subtiles: 2 cells
    };
    aura(kMight, "Might", d2d::rules::ServerDoFunction::kFriendlyAura, "might", "might");
    aura(kHolyFreeze, "Holy Freeze", d2d::rules::ServerDoFunction::kHolyFreeze, "holywindcold", "freeze");
    aura(kOakSage, "Oak Sage Aura", d2d::rules::ServerDoFunction::kFriendlyAura, "oaksagecontrol", "oaksage");
    auto& hireling = test.data.mercs[1];
    hireling.npc.code = "GU";
    World world(&test.data, -1, -1);
    Level open;
    open.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    open.walk.assign(80 * 80, 0);
    world.level = &open;
    auto& fight = world.fight;
    fight.enter(&open);
    fight.skill_level = [](int) { return 1; };                // the player's skills: Might at 1
    fight.skill_base = fight.skill_level;
    auto& actor = *world.actor;
    actor.player.x = 8; actor.player.y = 8;
    actor.aura = kMight;                                      // the Paladin's Might
    Monster merc;
    merc.hit_points = merc.stats.hit_points = 100;
    merc.unit.x = 9; merc.unit.y = 8;                         // 1 cell off: in Might's 2
    actor.merc = merc;
    actor.merc_npc = &hireling.npc;
    actor.merc_ids = { kHolyFreeze, 0, 0, 0, 0, 0 }; actor.merc_levels = { 3, 0, 0, 0, 0, 0 };
    actor.merc_aura = kHolyFreeze;                            // an Act 2 merc's own (an enemy aura)
    Fight::Pet totem;
    totem.monster.type = 2;                                   // a summon has its MonStats row: pets_turn reads it
    test.monster(2, "oaksage");
    totem.monster.hit_points = totem.monster.stats.hit_points = 100;
    totem.monster.unit.x = 6.5f; totem.monster.unit.y = 8;    // 1.5 cells from the player, 2.5 from the merc
    totem.where = &open;
    totem.aura = kOakSage; totem.aura_level = 1;
    actor.pets.push_back(std::move(totem));

    // The merc: its own aura's state, then the player's Might; the totem's out of its reach.
    assert(same(fight.aura_states_on(9, 8, kHolyFreeze), { "holywindcold", "might" }));
    // The player: its own Might, then the totem's target state (not oaksagecontrol); the merc's
    // Holy Freeze is an enemy aura, nothing on an ally.
    assert(same(fight.aura_states_on(8, 8, kMight), { "might", "oaksage" }));

    auto view = world.view();
    assert(view.merc && same(view.merc->states, { "holywindcold", "might" }));
    assert(same(view.aura_states, { "might", "oaksage" }));

    // Over the LAN wire, as a joiner's client gets them.
    ViewEncoder encoder;
    const auto bytes = encode_view(test.data, view, encoder);
    View joined;
    assert(apply_view(test.data, bytes, joined));
    assert(joined.merc && same(joined.merc->states, { "holywindcold", "might" }) && same(joined.aura_states, { "might", "oaksage" }));

    // The merc running Might too: one Might on it and on the player.
    actor.merc_ids[0] = kMight;
    actor.merc_aura = kMight;
    view = world.view();
    assert(same(view.merc->states, { "might" }) && same(view.aura_states, { "might", "oaksage" }));
    // Out of the player's range, the merc keeps its own; a dead merc shows none.
    actor.merc->unit.x = 14;
    actor.merc_ids[0] = kHolyFreeze; actor.merc_aura = kHolyFreeze;
    assert(same(world.view().merc->states, { "holywindcold" }));
    actor.merc->hit_points = 0;
    assert(world.view().merc->states.empty());

    // Vigor's velocitypercent (stat 0x43) on the merc in its range: a frame's walk goes further
    // (merc_turn: 75 + its pace + FRW + its states' %).
    constexpr int kVigor = 115;
    aura(kVigor, "Vigor", d2d::rules::ServerDoFunction::kFriendlyAura, "stamina", "stamina");
    auto& vigor = test.skill(kVigor, "Vigor");
    vigor.aurastat[0] = d2d::d2s::kVelocityPercent;
    vigor.aura_calc[0].code = { { d2d::rules::Calc::Const, 50 } };
    test.monster(1, "act2hire").velocity = 6;
    const auto walked = [&](int player_aura) {
        actor.aura = player_aura;
        actor.merc_aura = 0; actor.merc_ids[0] = 0;
        auto& walker = *actor.merc;
        walker.type = 1;
        walker.hit_points = 100;
        walker.unit.x = 9; walker.unit.y = 8;
        walker.mode = "WL"; walker.unit.walking = true; walker.mode_until = 0; walker.next_act = 1000000;
        walker.unit.path = { { 15.f, 8.f } };
        actor.merc_chase = false; actor.merc_target = -1;
        fight.merc_turn(1000, 0.04f, Crowd{}, false);
        return float(walker.unit.x) - 9;
    };
    const float bare = walked(0), quick = walked(kVigor);
    assert(bare > 0 && quick > bare);
    // A curse's velocitypercent (a Decrepify) slows it while it lasts; one past its end, still held
    // in town (merc_foe drops it only out of town), doesn't.
    constexpr int kSlowCurse = 87;
    auto& slow = test.skill(kSlowCurse, "Decrepify");
    slow.aurastat[0] = d2d::d2s::kVelocityPercent;
    slow.aura_calc[0].code = { { d2d::rules::Calc::Const, -50 } };
    actor.curses[1] = { kSlowCurse, 1, 2000 };                // on till 2000 ms; the walk's frame is at 1000
    assert(walked(0) < bare);
    actor.curses[1].until = 500;                              // spent, not yet dropped
    assert(walked(0) == bare);
    // In town a frame of the fight drops the spent curse, so the merc's stats (its sheet) lose it: here
    // its attack rating, which the live curse took 100 off.
    slow.aurastat[1] = d2d::d2s::kToHit;
    slow.aura_calc[1].code = { { d2d::rules::Calc::Const, -100 } };
    const int clean_rating = [&] { const auto held = std::exchange(actor.curses[1], {}); const int rating = fight.merc_fighter().attack_rating; actor.curses[1] = held; return rating; }();
    assert(fight.merc_fighter().attack_rating < clean_rating);   // still held
    fight.world(false, 1000, 0.04f, Crowd{});                  // in town: merc_foe doesn't run
    assert(actor.curses[1].skill < 0 && fight.merc_fighter().attack_rating == clean_rating);
    // A co-op partner's merc too (each hero's merc turns in town): its spent curse and potion state go,
    // one still on stays.
    Fight::Hero partner;
    partner.curses[1] = { kSlowCurse, 1, 500 };
    partner.merc_potion_states[0].until = 500;
    partner.merc_potion_states[1].until = 5000;
    actor.curses[1] = { kSlowCurse, 1, 5000 };
    fight.heroes.push_back(&partner);
    fight.world(false, 1000, 0.04f, Crowd{});
    fight.heroes.pop_back();
    assert(partner.curses[1].skill < 0 && partner.merc_potion_states[0].until == 0 && partner.merc_potion_states[1].until == 5000);
    assert(actor.curses[1].skill == kSlowCurse);               // the acting player's merc's, still on
    std::puts("OK: the merc shows its own aura and the player's Might; the player its own and the totem's target state; over the wire too; Vigor speeds its walk, a live curse slows it");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_aura_states: %s\n", error.what());
    return 1;
}
