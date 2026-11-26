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
    totem.monster.hit_points = totem.monster.stats.hit_points = 100;
    totem.monster.unit.x = 6.5f; totem.monster.unit.y = 8;    // 1.5 cells from the player, 2.5 from the merc
    totem.where = &open;
    totem.aura = kOakSage; totem.aura_level = 1;
    actor.pets.push_back(totem);

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
    std::puts("OK: the merc shows its own aura and the player's Might; the player its own and the totem's target state; over the wire too");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_aura_states: %s\n", error.what());
    return 1;
}
