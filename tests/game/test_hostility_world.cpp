// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_005a5e50, FUN_005a5dc0 and FUN_005a5d20: a declaration reaches
// Player::declare_hostile, closes the declarer's portal, applies bit 8 both
// ways to the target's eligible party, and removes a declarer in that party.
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <hostile_toggle.hpp>
#include <level.hpp>
#include <player_relations.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

// Drives World::toggle_hostility (FUN_005a5e50, party branch FUN_005a5dc0 / FUN_005a5d20) in a hosted world.
int main() try {
    GameData data;
    Level town;
    town.id = 1;
    town.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    World world(&data, -1, -1);
    world.level = &town;
    auto& declarer = world.lead();
    auto& target = world.add_player();
    auto& member = world.add_player();
    auto& low_member = world.add_player();
    for (auto* player : { &declarer, &target, &member }) player->character.stats.values[d2d::d2s::kLevel] = 9;
    low_member.character.stats.values[d2d::d2s::kLevel] = 8;
    target.party = member.party = low_member.party = 7;
    world.portal[declarer.portal_pair].level = &town;
    world.portal[declarer.portal_pair + 1].level = &town;

    constexpr std::uint32_t now_ms = 12345;
    assert(world.toggle_hostility(target, 1, now_ms) == d2d::rules::HostileToggle::kDeclared);
    assert(declarer.hostile_at == now_ms);
    assert(declarer.relations.hostile_stamp(declarer.id, target.id) == now_ms + d2d::rules::kHostilePairCooldownMs);
    for (auto* hostile : { &target, &member }) {
        assert(declarer.relations.get(declarer.id, hostile->id, d2d::rules::kRelationHostile) == 8);
        assert(hostile->relations.get(hostile->id, declarer.id, d2d::rules::kRelationHostile) == 8);
    }
    assert(declarer.relations.get(declarer.id, low_member.id, d2d::rules::kRelationHostile) == 0);
    assert(!world.portal[declarer.portal_pair].level && !world.portal[declarer.portal_pair + 1].level);

    // A member of the target's party is expelled after hostility is applied.
    declarer.party = target.party;
    declarer.relations.set(declarer.id, target.id, d2d::rules::kRelationHostile, false);
    target.relations.set(target.id, declarer.id, d2d::rules::kRelationHostile, false);
    declarer.relations.hostile_stamp(declarer.id, target.id, 0);
    assert(world.toggle_hostility(target, 1, now_ms + 1) == d2d::rules::HostileToggle::kDeclared);
    assert(declarer.party == -1);

    // Clearing is one-way and does not move the last-declaration clock.
    assert(world.toggle_hostility(target, 0, now_ms + 2) == d2d::rules::HostileToggle::kCleared);
    assert(declarer.relations.get(declarer.id, target.id, d2d::rules::kRelationHostile) == 0);
    assert(target.relations.get(target.id, declarer.id, d2d::rules::kRelationHostile) == 8);
    assert(declarer.hostile_at == now_ms + 1);
    std::puts("OK: hosted hostility reaches relation nodes, target party and portal closure");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hostility_world: %s\n", error.what());
    return 1;
}
