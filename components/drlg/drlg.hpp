// SPDX-License-Identifier: GPL-3.0-or-later
// Dungeon/level generation (game.exe's .\DRLG\*.cpp): where an act's
// levels sit, and (later) what's inside the outdoor ones.
// docs/research/re/drlg.md.
#pragma once

#include <level_ids.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::drlg {

// Levels.txt ids: d2d::rules::level_ids (the quests use them too).
namespace level_ids = d2d::rules::level_ids;

// What the layout needs from Levels.txt: size for the difficulty and the
// fixed offset of a level that anchors a chain.
struct LevelDef { int width = 0, height = 0, offset_x = 0, offset_y = 0; bool outdoor = false; };
using LevelDefs = std::unordered_map<int, LevelDef>;   // by Levels.txt Id

// A placed level: its rectangle in act tiles, the side of its link it
// went to (0 below, 1 left, 2 above, 3 right; -1 the chain's anchor), the
// alignment variant, and the outdoor flags the placement set.
struct Placed { int level = 0, x = 0, y = 0, width = 0, height = 0, dir = -1, flip = 0; std::uint32_t flags = 0; };

// How a record places its level against its link (game.exe's callbacks).
// Act 2's: RockyWaste (FUN_006769a0), Shifted (FUN_00676280), Flush
// (FUN_00676ae0). Act 4's: OuterSteppes (FUN_00676c00). Act 5's: Highlands
// (FUN_0067d830), Plateau (FUN_0067d980), Tundra (FUN_0067d8e0).
enum class Place { Anchor, Beside, BloodMoor, Town, Fixed, RockyWaste, Shifted, Flush, OuterSteppes, Highlands, Plateau, Tundra };
struct Record { Place how; int level; int link; };

// Act 1's two chains (0x6f0750, 0x6f0840): Stony Field anchors Cold
// Plains, the Blood Moor, the town and the Burial Grounds; the Moo Moo
// Farm and the Monastery anchor Tamoe Highland, Black Marsh, Dark Wood.
inline const std::vector<Record> kAct1Outdoors = {
    { Place::Anchor, 4, -1 }, { Place::Beside, 3, 0 }, { Place::BloodMoor, 2, 1 }, { Place::Town, 1, 2 },
    { Place::Beside, 17, 1 } };
inline const std::vector<Record> kAct1Highlands = {
    { Place::Anchor, 39, -1 }, { Place::Anchor, 26, -1 }, { Place::Fixed, 7, 1 }, { Place::Beside, 6, 2 },
    { Place::Beside, 5, 3 } };

// Act 2's chains (0x6f0930, 0x6f0a20; FUN_00677790): Lut Gholein anchors
// the Rocky Waste, Dry Hills, Far Oasis, Lost City and the Valley of
// Snakes; the Canyon of the Magi stands alone. No outdoor flags.
inline const std::vector<Record> kAct2Outdoors = {
    { Place::Anchor, 40, -1 }, { Place::RockyWaste, 41, 0 }, { Place::Shifted, 42, 1 }, { Place::Shifted, 43, 2 },
    { Place::Shifted, 44, 3 }, { Place::Flush, 45, 4 } };
inline const std::vector<Record> kAct2Canyon = { { Place::Anchor, 46, -1 } };

// Act 4's chains (0x6f0c00, 0x6f0cf0; FUN_00678a20): Pandemonium Fortress
// anchors Outer Steppes, Plains of Despair, City of the Damned; Chaos
// Sanctuary anchors its chain. No outdoor flag rules.
inline const std::vector<Record> kAct4Outdoors = {
    { Place::Anchor, level_ids::kPandemoniumFortress, -1 },
    { Place::OuterSteppes, level_ids::kOuterSteppes, 0 },
    { Place::Beside, level_ids::kPlainsOfDespair, 1 },
    { Place::Beside, level_ids::kCityOfTheDamned, 2 } };
inline const std::vector<Record> kAct4Chaos = { { Place::Anchor, level_ids::kChaosSanctuary, -1 } };

// Act 5's chains (0x6f0de0, 0x6f0ed0, 0x6f0b10; FUN_00678a70): Harrogath and
// the Bloody Foothills both at their Levels.txt offsets, the Frigid
// Highlands and Arreat Plateau grown off them; the Frozen Tundra alone; the
// Forgotten Sands and Uber Tristram anchored.
inline const std::vector<Record> kAct5Outdoors = {
    { Place::Anchor, level_ids::kHarrogath, -1 },
    { Place::Anchor, level_ids::kBloodyFoothills, 0 },
    { Place::Highlands, level_ids::kFrigidHighlands, 1 },
    { Place::Plateau, level_ids::kArreatPlateau, 2 } };
inline const std::vector<Record> kAct5Tundra = { { Place::Tundra, level_ids::kFrozenTundra, -1 } };
inline const std::vector<Record> kAct5Uber = { { Place::Anchor, level_ids::kForgottenSands, -1 }, { Place::Anchor, level_ids::kUberTristram, -1 } };

// Which (town dir, town flip, Blood Moor dir, Blood Moor flip) are allowed
// (0x6f1158), indexed dir + 4 flip + 8 bm_dir + 32 bm_flip.
inline constexpr std::array<std::uint8_t, 64> kTownAllowed = {
    1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1,
    0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1 };

// Outdoor flags from consecutive placements (0x6f1258, FUN_00677180): a
// record's level (0 = any), unless it's one of two levels, gets `flag`
// when its dir and the next record's dir are (a, b).
struct FlagRule { int level, not1, not2, dir, next_dir; std::uint32_t flag; };
inline constexpr std::array<FlagRule, 15> kAct1Flags = { {
    { 0, 2, 3, 1, 0, 4 }, { 0, 2, 3, 2, 3, 4 }, { 0, 3, 17, 2, 1, 8 }, { 0, 3, 17, 3, 0, 8 },
    { 0, 3, 17, 1, 1, 16 }, { 0, 3, 17, 3, 3, 16 }, { 2, 0, 0, 0, 0, 8 }, { 2, 0, 0, 2, 2, 8 },
    { 2, 0, 0, 3, 0, 8 }, { 2, 0, 0, 3, 2, 8 }, { 2, 0, 0, 0, 1, 1024 }, { 2, 0, 0, 1, 1, 1024 },
    { 2, 0, 0, 2, 1, 512 }, { 2, 0, 0, 2, 2, 128 }, { 2, 0, 0, 3, 2, 256 } } };

// Rectangles overlap unless they're `gap` or more apart on an axis
// (FUN_0066b800; gap 0 lets them touch).
bool apart(const Placed& first, const Placed& second, int gap = 0);

// Places one chain (FUN_006772c0): each record tries the sides of its
// link, starting at a rolled one; a record that has tried them all hands
// back to the one before, which moves on. `seed` is a copy of the act's:
// the chains don't advance it. Act 1's chain 2 (FUN_00677750) is checked
// by FUN_00676eb0 (`farm_margin`): clear of every earlier level but its
// link, then clear of its first record (the Moo Moo Farm) grown 200 tiles
// upward (y - 200, height + 200). Its Fixed record (Tamoe Highland,
// FUN_006768c0) sets dir 0 before its switch, so it always goes below, and
// never retries. Neither chain is checked against the other.
std::vector<Placed> place_chain(const std::vector<Record>& recs, const LevelDefs& defs, d2d::rules::Rng seed,
                                       bool town_rules, bool act1_flags = true, bool checked = true, bool farm_margin = false);

// Act 1's outdoor levels (FUN_00677750), both chains from the act seed.
std::vector<Placed> act1_layout(const LevelDefs& defs, const d2d::rules::Rng& act_seed);

// Act 2's two Tal Rasha's Tombs (FUN_00642da0, before the layout): the
// boss tomb (act +0x94) and the staff tomb (+0x484), each rand(7) + 66 on
// the act seed, rolled in pairs until they differ.
struct Act2Tombs { int boss = 0, staff = 0; };
Act2Tombs roll_act2_tombs(d2d::rules::Rng& act_seed);

// Act 2's outdoor levels (FUN_00677790), both chains from the act seed
// after the tombs' rolls. The chain check (FUN_00676f50) is the plain
// overlap one.
std::vector<Placed> act2_layout(const LevelDefs& defs, d2d::rules::Rng act_seed);

// Act 4's outdoor levels (FUN_00678a20), both chains from the act seed.
// The chain check (FUN_006770a0 / FUN_00677110) is the plain overlap one.
std::vector<Placed> act4_layout(const LevelDefs& defs, d2d::rules::Rng act_seed);

// Act 5's outdoor levels (FUN_00678a70), each chain from the act seed. No
// chain check but the uber levels' (FUN_00677030, the plain overlap one).
std::vector<Placed> act5_layout(const LevelDefs& defs, d2d::rules::Rng act_seed);

// Lut Gholein's preset file (LvlPrest File1 / File2): the side the Rocky
// Waste went (FUN_006772c0 reads the next record's dir for level 40):
// 1 left, 2 above.
int lut_gholein_file(const std::vector<Placed>& layout);

// The town's preset, from the side of the Blood Moor it went to
// (LvlPrest "Act 1 - Town 1" File1..4): 0 townN1, 1 townE1, 2 townS1,
// 3 townW1 — named for the side the Blood Moor is on.
int town_file(const std::vector<Placed>& layout);

// Courtyard 1's preset file (LvlPrest File1..3), from the side the Black
// Marsh went (FUN_00677180): above rolls File2 / File3, below File1 /
// File2 on the act seed; else -1, its own roll.
int courtyard_file(const std::vector<Placed>& layout, d2d::rules::Rng act_seed);

// Act 3's layout (FUN_006789b0; drlg.md "Act 3's layout"). The jungle's
// three levels are pieces grown up from the docks, each a grid of 32-tile
// cells holding one LvlPrest Def (0 none); the Kurast levels stack above
// the top piece.
struct JungleCells { std::vector<int> presets; int specials = 0; };   // row-major, width / 32 a row; specials: Defs past 574
struct Act3Layout {
    std::vector<Placed> levels;                   // 75..83
    std::array<JungleCells, 3> jungle;            // 76, 77, 78
    int roll = 0;                                 // FUN_00642da0's act 3 roll (act +0x474)
    bool failed = false;                          // game.exe asserts (OutPlace.cpp 0x78c / 0x799), or no layout could be made
    int tries = 0;                                // grids and river paths laid (bounded)
};

// The clearing Defs by a cell's branch bits >> 4 (0x6f13bc), and a path
// cell's Def by its low bits and one branch bit 0x10..0x80 (0x6f13f0).
inline constexpr std::array<int, 16> kJungleClearings = { 256, 575, 576, 577, 578, 579, 580, 0, 581, 582, 583, 0, 584, 0, 0, 0 };
inline constexpr std::array<std::array<int, 4>, 15> kJungleBranches = { {
    { 0, 0, 0, 0 }, { 0, 545, 546, 547 }, { 548, 0, 549, 550 }, { 0, 0, 551, 552 }, { 553, 554, 0, 555 },
    { 0, 556, 0, 557 }, { 558, 0, 0, 559 }, { 0, 0, 0, 560 }, { 561, 562, 563, 0 }, { 0, 564, 565, 0 },
    { 566, 0, 567, 0 }, { 0, 0, 568, 0 }, { 569, 570, 0, 0 }, { 0, 571, 0, 0 }, { 572, 0, 0, 0 } } };

// The jungle (FUN_00677880): three pieces, each grown off a random earlier
// one (rand(5): above, or beside a third / two thirds up), a river path up
// each, branches off it into clearings and the pieces next door; a branch
// that finds nothing starts the cells over.
Act3Layout act3_layout(const LevelDefs& defs, d2d::rules::Rng act_seed);

}  // namespace d2d::drlg
