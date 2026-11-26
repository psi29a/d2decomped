// SPDX-License-Identifier: GPL-3.0-or-later
// A game's rooms in play and what they spawned (Spawning), room by room as
// they come up round the players. Levels only by pointer and reference: the
// level itself is level.hpp's.
#pragma once

#include "gamedata.hpp"

#include <monsters.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>

#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::game {

struct Level;

// A level's preset object groups in one room (Level::group_rooms, Spawning::LevelState::group_rooms):
// its seed before and after its monsters, the objrgn draw and each object made (objects.txt id, level subtile).
struct GroupRoom { std::uint32_t pre = 0, post = 0, rgn = 0; std::vector<std::array<int, 3>> made; };

// A game's monster spawning (docs/research/re/monsters.md "When a room
// populates"). game.exe populates a room once, the tick after it first
// comes into play (FUN_0052d160): the rooms one step from the player's in
// its near list, across a level's edge and a warp too. Every level's
// region is made at game start; the game seed then takes three more steps
// (the object seed, sunitproxy, quests) before the first room rolls on it.
struct ObjectRooms;                                     // objgroups.cpp
struct Spawning {
    struct LevelState {
        std::vector<d2d::rules::Spawn> spawns;             // every spawn so far, level-relative subtiles
        std::vector<int> spawn_slots;                      // by `spawns` index: where it landed in the level's monster list, -1 none
        std::vector<bool> rooms_up;                        // by Level::rooms index: its room1 made (and populated)
        // Which populated rooms currently have a server room1. FUN_0052d240
        // may drop one without clearing `rooms_up`: returning rebuilds its tiles,
        // but the stored units, rather than the preset population, come back.
        std::vector<bool> live;
        std::vector<std::uint8_t> idle_sweeps;              // FUN_0061a790's room1 +0xc, saturated for d2d
        std::vector<std::size_t> order;                    // Level::rooms indices as they came up
        std::vector<std::size_t> client_order;             // the same, each batch in made order (Level::drawn)
        // The level's object placement as it stands (objgroups.cpp): objrgn's
        // record of it, the units' collision so far; each room's object
        // groups (by Level::rooms index) and its seed after its monsters,
        // what a stand draws on.
        std::shared_ptr<ObjectRooms> objects;
        std::vector<GroupRoom> group_rooms;
        std::vector<d2d::rules::Rng> room_seeds;
        std::size_t seeds_up = 0;                          // the camp's (keep_camp_seeds): how many of `order` its seeds were laid with
        // A room's room1 seed stepped before it populated: a water monster's
        // party searching the room its leader stands in (FUN_005b2700).
        std::vector<std::optional<d2d::rules::Rng>> early_seeds;
        // Gold piles its gold placeholders made (InitFn 28, FUN_00559300), not yet
        // on the floor: level subtile x, y and the coins. World::drop_new_gold takes them.
        std::vector<std::array<int, 3>> gold;
    };
    int difficulty = 0;
    d2d::rules::Rng game;
    // The game's object seed (game +0x10f0, FUN_00546c60): every room's
    // objects, shrines, chests and every container opened step it, game-wide.
    d2d::rules::Rng objects;
    // The sunitproxy seed (FUN_00536070: {a game seed step, 666}, game
    // +0x1d24 +8, FUN_00536b40): the hire lists and the vendors' stock roll on it.
    d2d::rules::Rng proxy;
    d2d::rules::Rng quests;                 // the quests' seed (game +0x10f4 +0x18): its timers and drops (FUN_005438b0)
    std::vector<d2d::rules::Region> regions;               // by Levels.txt Id
    std::unordered_map<const Level*, LevelState> levels;
    std::bitset<128> superuniques;                         // spawned this game (game +0x1d30)
    // a5q2's cages up so far (its record's data +0x86 / +0x8c, game-wide):
    // level id, spot (level subtiles).
    std::vector<std::array<int, 3>> rescue_cages;
    int tomb_symbols = 0;                                  // the Summoner's symbols handed out (FUN_0059d720: record 13 +0x3a)
    // The game's quest words (World::game_bits, a d2d::rules::QuestBits), as
    // the town markers' inits read them (FUN_00544760).
    std::array<std::uint8_t, 96> game_quests{};
    std::uint8_t room_sweep_frame = 0;                      // FUN_0052d870 calls FUN_0052d240 every 12th frame
    // a3q5's record 0x13, its data +0xc: Khalim's Will done by a player at the
    // join (word 0x12 bit 0, FUN_005bb480): the compelling orb comes up broken.
    bool orb_smashed = false;
    // a3q7's +0xd4 (LAB_005bd2c0): the game's first player has seen the Dark
    // Wanderer (word 0x20 bit 0), its data +1 cleared: InitFn 43 makes none.
    bool wanderer_seen = false;
    // Quest-record marker fields that outlive the levels containing them.
    d2d::rules::MarkerRecords marker_records;
    const Level* room_level = nullptr;                     // the player's room
    int room = -1;
    // The client's status-2 list (the act's +0x294, FUN_0061b210 adds at
    // its tail) and FUN_0061b920's state: the cursor (+0x460), the frames
    // to the next bring-up (+0x45c) and the room1s made since (+0x98).
    std::vector<std::pair<const Level*, std::size_t>> client_ring;
    std::optional<std::pair<const Level*, std::size_t>> client_ring_cursor;
    std::uint8_t client_ring_countdown = 5;
    std::uint8_t client_made = 0;
};
Spawning start_spawning(const GameData& game_data, int difficulty);
// The player at (x, y), cells, in `level`: rooms coming into play round the player's room populate, newest
// first.
std::vector<std::pair<const Level*, std::size_t>> player_moved(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 float x, float y, bool arrived);
// A client frame's FUN_0061b920: one room of the status-2 list without a
// room1 comes up each 5 frames (player_moved fills the list).
void step_client_ring(const GameData& game_data, Spawning& spawning);
struct RoomPosition { const Level* level; float cell_x, cell_y; };
bool age_live_room(Spawning::LevelState& state, std::size_t room, bool held);
// FUN_0052d240: every twelfth server frame, age live room1s not held by a
// player's depth-1 near set and release those older than ten sweeps. Returns
// the rooms whose tiles were dropped; their population stays stored.
std::vector<std::pair<const Level*, std::size_t>> sweep_idle_rooms(const GameData& game_data, Spawning& spawning,
                                                                    std::span<const RoomPosition> players);
// Where a missile in the room holding (x, y) cells can be: that room's rect, then its near list's, act subtiles
// {x, y, w, h}.
std::vector<std::array<int, 4>> missile_rooms(const GameData& game_data, const Level& level, float x, float y, const Spawning* spawning = nullptr);
// FUN_00463740: the room holding (x, y), subtiles off `level`'s origin, among
// room `from`'s own and its near list's (neighbour levels' rooms too).
struct NearHolder { const Level* level; std::size_t room; };
std::optional<NearHolder> near_room_holder(const GameData& game_data, const Level& level, std::size_t from, int x, int y, const Spawning* spawning = nullptr);
// The automap's rooms (FUN_00459020): the one holding (x, y) cells and its
// near list (FUN_00619790), itself included, in the list's order: each
// room's level, its index (Level::rooms, or a preset level's 8x8 room in
// the order they're split, row by row) and its rect in act tiles.
struct AutomapRoom { const Level* level; int room; int x, y, width, height; };
std::vector<AutomapRoom> automap_rooms(const GameData& game_data, const Level& level, float x, float y);

// Every room of the level populated at once, newest first: tools/emu
// monsters.py's order, for diffing against game.exe (drlg-dump monsters).
// The rooms come up `came_up` (Level::rooms indices) first, the rest in list
// order. Returns each room in the order populated with its first spawn's
// index; `done` hears of each room as it's populated.
std::vector<std::pair<std::size_t, std::size_t>> populate_level(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 const std::vector<std::size_t>& came_up = {},
                                                                 const std::function<void(std::size_t)>& done = {});

}  // namespace d2d::game
