// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <automap_step.hpp>
#include <room_tiles.hpp>
#include <rules.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace d2d::client {

// What the automap's reveal keeps across frames, layers and games
// (automap.md "Adding cells"): game.exe keeps it on the tiles and in globals,
// so it isn't an Automap's (one a layer).
// - `seed`: the cel picks' own seed (0x96c8c8 by 0x744504), {0, 0x29a} as
//   AutoMap.txt loads (FUN_0061fcf0), stepped by each lookup that finds a
//   record (FUN_0061fff0); nothing else draws on it, and nothing resets it.
// - `step`: where the last reveal was (rules::automap_moved).
// - `drawn`: a tile's flag 0x20000, set by its own draw: {level, cell, slot}
//   (slot: a pick's place in its cell, or a DS1 layer's: floors 16 + i,
//   walls 32 + i).
// - `added`: its flag 0x40000, set as FUN_00457cf0 takes it, before the
//   lookup: {level, room, list, place in the list}, so a tile two rooms list
//   (their shared edge) is looked up by each.
// - `whole`: the preset levels whose every room came in at once
//   (FUN_00668100).
// A new game's rooms come with clear flags: new_game() drops drawn, added
// and whole; the seed and the last spot are game.exe's globals and stay.
struct AutomapReveal {
    d2d::rules::Rng seed;
    d2d::rules::AutomapStep step;
    std::set<std::tuple<const Level*, std::uint32_t, int>> drawn;
    std::set<std::tuple<const Level*, int, int, std::size_t>> added;
    std::set<const Level*> whole;
    std::unordered_map<const Level*, std::vector<d2d::drlg::BuiltRoom>> presets;   // preset_rooms, made once a level
    void new_game() { drawn.clear(); added.clear(); whole.clear(); presets.clear(); }
};

// Marks what render_world drew (`cells`, each its DrawnCell::source, or the
// whole cell) as drawn, the grid they're in shifted by (shift_x, shift_y): a
// neighbour level's from the drawn level's.
void automap_mark(AutomapReveal& reveal, const Level& level, std::span<const DrawnCell> cells, int shift_x = 0, int shift_y = 0);

// FUN_00458f40 over one room (AutomapRoom::room of `level`): its floors,
// then its walls, each not hidden (flag 8) and drawn (or `all`), once,
// into list 0 / 1 with a cel from the seed (automap_cel).
void automap_room(const Scene& scene, const GameData& game_data, const Level& level, int room, Automap& automap, AutomapReveal& reveal, bool all);

// FUN_00459020, a frame's: with the player at (x, y) cells in `level`,
// once it has moved 80 client px, the rooms of automap_rooms on the
// player's layer, each as automap_room.
void automap_step(const Scene& scene, const GameData& game_data, const Level& level, float x, float y, Automap& automap, AutomapReveal& reveal);

// Every room of the level whole, in the level's list order (newest
// first), as with 0x7a51a0 set: devctl `debug automap`, automap_whole.
// False when a preset level's rooms can't be made yet (no generated level
// built to lend its DRLG tables).
bool automap_all(const Scene& scene, const GameData& game_data, const Level& level, Automap& automap, AutomapReveal& reveal);

// FUN_00668100's other branch: a preset level (Levels.txt DrlgType 2) whose
// LvlPrest AutoMap is set, but for Lut Gholein, the Pandemonium Fortress
// and Harrogath (their miniatures, automap_town), has every room come in
// whole as the level comes up: the Rogue Encampment and the Kurast Docks
// (d2d grows the Docks with the jungle: its generated rooms, then).
// The level's list order (newest first), once a game.
void automap_whole(const Scene& scene, const GameData& game_data, const Level& level, Automap& automap, AutomapReveal& reveal);

}  // namespace d2d::client
