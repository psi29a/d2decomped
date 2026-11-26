// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Golden Bird's record log (+0xb) moves at its FUN_00544300 sites, and FUN_005ba170 shows it to a
// player carrying neither item: the carrier's death log 1 (0x5bac45), the pickups 3 / 1 (0x5ba73d,
// 0x5ba75d), and on the talk's close (LAB_005ba0b0, armed by FUN_005ba320) Cain's 2 then 4 (0x5ba114,
// 0x5ba134), Alkor's 5 (0x5ba0e9), Meshif's 3 (0x5ba15c). No game data.
#include <gamedata.hpp>
#include <level.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <memory>

using namespace d2d::game;
using Bird = d2d::rules::GoldenBirdQuest;

namespace {

constexpr int kDocks = 75;

// What a player with the act open (word 0xf bit 0) and neither item sees.
int shown(const Bird& bird) {
    d2d::rules::QuestBits bits{};
    d2d::rules::quest::set(bits, 0xf, 0);
    return bird.log_state(bits, false, false, true);
}

}  // namespace

// Checks the record's log at each of its sites, through the World's hooks where they exist.
int main() try {
    {   // The carrier's death and the pickups (FUN_005bab60, FUN_005ba6a0).
        d2d::rules::QuestBits bits{};
        Bird bird;
        bird.made(7);
        assert(bird.killed(bits, 7) && bird.log == 1 && shown(bird) == 1);
        Bird again;
        again.log = 3;
        again.made(7);
        assert(again.killed(bits, 7) && again.log == 3);   // a log already: kept
        Bird picked;
        assert(!picked.picked_up(bits, true) && picked.state == 3 && shown(picked) == 3);
        assert(picked.picked_up(bits, false) && picked.state == 1 && shown(picked) == 1);
    }
    GameData data;
    auto& docks = *(data.levels[kDocks] = std::make_unique<Level>());
    docks.id = kDocks;
    World world(&data, -1, -1);
    world.level = &docks;
    d2d::rules::quest::set(world.quests(), 0xf, 0);
    world.golden_bird.state = world.golden_bird.log = 1;
    world.act3_talk_closed(Bird::kCain);                       // nothing armed yet
    assert(world.golden_bird.log == 1);
    world.act3_said(Bird::kCain, Bird::kCainFigurine);
    assert(world.golden_bird.state == 2 && world.golden_bird.log == 1);
    world.act3_talk_closed(Bird::kAlkor);                      // not his
    assert(world.golden_bird.log == 1);
    world.act3_talk_closed(Bird::kCain);
    assert(world.golden_bird.log == 2 && shown(world.golden_bird) == 2);
    world.golden_bird.log = 3;
    world.act3_said(Bird::kCain, Bird::kCainBird);
    world.act3_talk_closed(Bird::kCain);
    assert(world.golden_bird.log == 4);
    world.actor->character.items.push_back({ .code = Bird::kBird });
    world.act3_said(Bird::kAlkor, Bird::kAlkorTakes);
    assert(world.golden_bird.state == 4 && world.golden_bird.log == 4);
    world.act3_talk_closed(Bird::kAlkor);
    assert(world.golden_bird.log == 5);
    world.act3_talk_closed(Bird::kAlkor);                      // once
    assert(world.golden_bird.log == 5);
    // Meshif's swap (FUN_005ba320's 529; its items are the World's): log 3 on his talk's close.
    world.golden_bird = {};
    d2d::rules::QuestBits bits{}, game_bits{};
    assert(world.golden_bird.said(bits, game_bits, Bird::kMeshif, Bird::kMeshifSwap, true, false, 1).gives_bird);
    world.act3_talk_closed(Bird::kMeshif);
    assert(world.golden_bird.state == 3 && world.golden_bird.log == 3);
    std::puts("OK: the Golden Bird's log moves at its FUN_00544300 sites");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_golden_bird_log: %s\n", error.what());
    return 1;
}
