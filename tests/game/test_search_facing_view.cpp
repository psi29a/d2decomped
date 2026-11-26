// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A standing player's shift-search facing (FUN_006238c0) is the way to its path
// target (+0x10 / +0x12, SP1): World::view puts the target in the View, the View
// codec carries it to the client, and ClientSession::shift_pick's
// rules::search_facing takes it whether the player walks or stands.
#include <monsters.hpp>
#include <replication.hpp>
#include <search_facing.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

// shift_pick's facing from a client's View, its subtiles as it takes them
// (the player's Fine, the target's cells floored).
int facing_from(const View& view) {
    const auto cell_subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return d2d::rules::search_facing(view.player.x.subtile(), view.player.y.subtile(),
                                     cell_subtile(view.path_target_x), cell_subtile(view.path_target_y),
                                     view.player.facing.goal);
}

// The View a client gets for `world` through encode_view / apply_view.
View received(const d2d::test::TestData& fixture, const World& world) {
    ViewEncoder encoder;
    View view;
    const bool applied = apply_view(fixture.data, encode_view(fixture.data, world.view(), encoder), view);
    assert(applied);
    return view;
}

}  // namespace

// A standing player off SP1, the same walking, then standing on SP1.
int main() try {
    d2d::test::TestData fixture;
    World world(&fixture.data, -1, -1);
    world.level = &fixture.data.town;
    auto& player = *world.players.front();
    player.player.x = 20.5f;
    player.player.y = 20.5f;
    player.player.goal_x = player.player.x;   // SP3 on the player: the old path's stand-in
    player.player.goal_y = player.player.y;
    player.player.walking = false;
    player.target_x = 22.5f;                  // SP1, 10 subtiles east
    player.target_y = 20.5f;

    const View standing = received(fixture, world);
    assert(!standing.player.walking);
    assert(standing.path_target_x == 22.5f && standing.path_target_y == 20.5f);
    const int east = d2d::rules::direction64(102, 102, 112, 102);
    assert(east != standing.player.facing.goal);
    assert(facing_from(standing) == east);    // standing off SP1: the way to it, not +0x65

    player.player.walking = true;             // walking makes no difference
    assert(facing_from(received(fixture, world)) == east);

    player.player.walking = false;            // on SP1: the facing as the client has it
    player.target_x = 20.5f;
    const View on_target = received(fixture, world);
    assert(facing_from(on_target) == on_target.player.facing.goal);
    std::puts("OK: World and the View codec carry a standing player's SP1 to FUN_006238c0's facing");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_search_facing_view: %s\n", error.what());
    return 1;
}
