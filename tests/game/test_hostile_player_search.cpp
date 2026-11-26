// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game d2gs
// test-sources: apps/d2d/session/near_players.cpp
// test-includes: apps/d2d
// A SearchEnemyNear cast takes another player only when our relation node toward
// it has the hostile bit 8 (FUN_00465c60 -> FUN_004dc440(..., 8)) and it is alive
// (the finder's mask 0x2003 drops a player in mode 0 / 0x11): the flag goes through
// the View codec, near_pick's candidates (add_near_players) pass
// rules::search_enemy_near with it, and the UseSkill keeps target type 0 through
// the command codec into C->S 0x06 (FUN_00461700 -> FUN_004786a0).
#include "session/near_players.hpp"

#include <d2gs/c2s.hpp>
#include <monsters.hpp>
#include <protocol.hpp>
#include <replication.hpp>
#include <search_enemy.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <variant>
#include <vector>

using namespace d2d::game;

namespace {

// The View a client gets for `sent` through encode_view / apply_view.
View received(const d2d::test::TestData& fixture, const View& sent) {
    ViewEncoder encoder;
    View view;
    const bool applied = apply_view(fixture.data, encode_view(fixture.data, sent, encoder), view);
    assert(applied);
    return view;
}

// near_pick's pick over the other players alone, as add_near_players gives
// them; the click nearer the first.
int pick(const View& view) {
    std::vector<d2d::rules::NearUnit> players;
    std::vector<d2d::rules::RoomUnit> listed;
    d2d::client::add_near_players(view, nullptr, players, listed);
    assert(players.size() == view.others.size() && listed.size() == players.size());
    int spot_x = 56, spot_y = 52;
    return d2d::rules::search_enemy_near(false, false, false, -1, -1, spot_x, spot_y, players);
}

}  // namespace

// Two players without bit 8, then one with it (alive, then dead), then its cast down to the C->S bytes.
int main() try {
    d2d::test::TestData fixture;
    View sent;
    sent.level = &fixture.data.town;
    sent.others.resize(2);
    sent.others[0].id = 101;
    sent.others[0].unit.x = 10.5f;
    sent.others[0].unit.y = 10.5f;
    sent.others[1].id = 202;
    sent.others[1].unit.x = 12.5f;
    sent.others[1].unit.y = 10.5f;

    assert(pick(received(fixture, sent)) == -1);        // two players without bit 8 fail
    sent.others[1].hostile = true;
    const View view = received(fixture, sent);
    assert(!view.others[0].hostile && view.others[1].hostile);
    assert(pick(view) == 1);                             // the hostile one is the pick
    View dead_view = view;                               // the host's 0x0d command 8 / 9 on it
    dead_view.others[1].dead = true;
    assert(pick(dead_view) == -1);                       // a dead hostile player is no candidate

    const Command command = cmd::UseSkill{ 0, 12.5f, 10.5f, int(view.others[1].id), true, -1, false, 0 };
    const auto decoded = decode(encode(command));
    const auto* use = decoded ? std::get_if<cmd::UseSkill>(&*decoded) : nullptr;
    assert(use && use->unit == 202 && use->unit_type == 0);
    const auto packet = d2d::net::d2gs::c2s::skill_on(true, std::uint32_t(use->unit_type), std::uint32_t(use->unit));
    assert((packet == d2d::net::d2gs::Bytes{ 0x06, 0, 0, 0, 0, 0xca, 0, 0, 0 }));
    std::puts("OK: a hostile player enters SearchEnemyNear and goes as C->S target type 0");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hostile_player_search: %s\n", error.what());
    return 1;
}
