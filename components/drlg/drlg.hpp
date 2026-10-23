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
inline bool apart(const Placed& first, const Placed& second, int gap = 0) {
    const int dx = first.x < second.x ? second.x - first.width - first.x : first.x - second.width - second.x;
    const int dy = first.y < second.y ? second.y - first.height - first.y : first.y - second.height - second.y;
    return !(dx < gap && dy < gap);
}

// Places one chain (FUN_006772c0): each record tries the sides of its
// link, starting at a rolled one; a record that has tried them all hands
// back to the one before, which moves on. `seed` is a copy of the act's:
// the chains don't advance it.
// ponytail: chain 2's Fixed record (Tamoe Highland, FUN_006768c0) always
// goes below its link and nothing checks chain 2's overlaps with chain
// 1 — game.exe's check for that chain (FUN_00676eb0) shifts by 200 tiles
// first, not ported.
inline std::vector<Placed> place_chain(const std::vector<Record>& recs, const LevelDefs& defs, d2d::rules::Rng seed,
                                       bool town_rules, bool act1_flags = true, bool checked = true) {
    const int count = int(recs.size());
    std::vector<Placed> placed(static_cast<std::size_t>(count));
    std::vector<int> start_dir(static_cast<std::size_t>(count), -1), start_flip(static_cast<std::size_t>(count), -1);
    for (int i = 0; i < count; ++i) {
        placed[std::size_t(i)].level = recs[std::size_t(i)].level;
        if (const auto found = defs.find(recs[std::size_t(i)].level); found != defs.end()) {
            placed[std::size_t(i)].width = found->second.width;
            placed[std::size_t(i)].height = found->second.height;
        }
    }
    // Roll a first side (and alignment), else step to the next; false once
    // every one has been tried.
    auto next_side = [&](int index, bool with_flip, int sides) {
        auto& placement = placed[std::size_t(index)];
        if (sides == 1) {                                          // Outer Steppes (FUN_00676c00): fixed dir 3, roll flip
            if (start_dir[std::size_t(index)] == -1) {
                start_dir[std::size_t(index)] = placement.dir = 3;
                start_flip[std::size_t(index)] = placement.flip = int(seed.next() & 1);
                return true;
            }
            return false;
        }
        if (sides == 2) {                                          // the Rocky Waste: left (1) or above (2)
            if (start_dir[std::size_t(index)] == -1) { start_dir[std::size_t(index)] = placement.dir = int(seed.next() & 1) + 1; return true; }
            const int next = 3 - placement.dir;
            if (next == start_dir[std::size_t(index)]) return false;
            placement.dir = next;
            return true;
        }
        if (start_dir[std::size_t(index)] == -1) {
            start_dir[std::size_t(index)] = placement.dir = int(seed.next() & std::uint32_t(sides - 1));
            if (with_flip) start_flip[std::size_t(index)] = placement.flip = int(seed.next() & 1);
            return true;
        }
        if (!with_flip) {
            const int next = (placement.dir + 1) & (sides - 1);
            if (next == start_dir[std::size_t(index)]) return false;
            placement.dir = next;
            return true;
        }
        const int next = (placement.dir + placement.flip) & 3, next_flip = (placement.flip + 1) & 1;
        if (next == start_dir[std::size_t(index)] && next_flip == start_flip[std::size_t(index)]) return false;
        placement.dir = next;
        placement.flip = next_flip;
        return true;
    };
    auto place = [&](int index) {
        const auto& record = recs[std::size_t(index)];
        auto& placement = placed[std::size_t(index)];
        if (record.how == Place::Anchor) {
            placement.dir = -1;
            if (const auto found = defs.find(record.level); found != defs.end()) { placement.x = found->second.offset_x; placement.y = found->second.offset_y; }
            return true;
        }
        // Act 5's: one roll picks the side and the shape, 64x160 (0) or 160x64 (1).
        auto act5_shape = [&] { placement.width = placement.dir ? 160 : 64; placement.height = placement.dir ? 64 : 160; };
        if (record.how == Place::Tundra) {                         // FUN_0067d8e0: at its Levels.txt offset
            start_dir[std::size_t(index)] = placement.dir = int(seed.next() & 1);
            act5_shape();
            if (const auto found = defs.find(record.level); found != defs.end()) { placement.x = found->second.offset_x; placement.y = found->second.offset_y; }
            return true;
        }
        const auto& linked = placed[std::size_t(record.link)];
        if (record.how == Place::Highlands) {                      // FUN_0067d830: left, bottoms 16 apart; never retried
            start_dir[std::size_t(index)] = placement.dir = int(seed.next() & 1);
            act5_shape();
            placement.x = linked.x - placement.width;
            placement.y = linked.y + linked.height - placement.height - 16;
            return true;
        }
        if (record.how == Place::Plateau) {                        // FUN_0067d980: off the Highlands by both shapes (0x6f1f7c)
            auto& start = start_dir[std::size_t(index)];
            if (start == -1) start = placement.dir = int(seed.next() & 1);
            else if (placement.dir == 0 ? start == 1 : start == 0) return false;
            else placement.dir = placement.dir == 0 ? 1 : 0;
            act5_shape();
            static constexpr std::array<std::pair<int, int>, 4> kOffsets = { { { 0, -160 }, { -96, -64 }, { -64, -96 }, { -160, 0 } } };
            const auto& offset = kOffsets[std::size_t(placement.dir + 2 * linked.dir)];
            placement.x = linked.x + offset.first;
            placement.y = linked.y + offset.second;
            return true;
        }
        if (record.how == Place::Fixed) {                          // FUN_006768c0: below, left-aligned
            placement.dir = 0;
            placement.x = linked.x;
            placement.y = linked.y + linked.height;
            return true;
        }
        const bool flips = record.how == Place::BloodMoor || record.how == Place::Town || record.how == Place::OuterSteppes;
        const int sides = record.how == Place::OuterSteppes ? 1 : record.how == Place::RockyWaste ? 2 : record.how == Place::Shifted || record.how == Place::Flush ? 8 : 4;
        if (!next_side(index, flips, sides)) return false;
        if (record.how == Place::BloodMoor) {                      // 96x56 left/right, 56x96 above/below
            placement.width = placement.dir & 1 ? 96 : 56;
            placement.height = placement.dir & 1 ? 56 : 96;
        }
        const int width = placement.width, height = placement.height, left = linked.x, top = linked.y, right = linked.x + linked.width, bottom = linked.y + linked.height;
        auto put_at = [&](int x, int y) { placement.x = x; placement.y = y; };
        if (record.how == Place::OuterSteppes) {                   // FUN_00676c00: right, flip 1 bottom+8 / flip 0 top-8
            put_at(right, placement.flip == 1 ? bottom - height + 8 : top - 8);
            placement.flags |= placement.flip == 1 ? 0x800000 : 0x400000;
            return true;
        }
        if (record.how == Place::RockyWaste) {                     // left, top-aligned; or above, left-aligned
            put_at(placement.dir == 1 ? left - width : left, placement.dir == 1 ? top : top - height);
            return true;
        }
        if (record.how == Place::Shifted) {                        // a side, half the level plus 8 off its corner
            const int along_x = width / 2 + 8, along_y = height / 2 + 8;
            const std::array<std::pair<int, int>, 8> spot = { { { left - along_x, bottom }, { left + along_x, bottom }, { left - width, top - along_y },
                                                                { left - width, top + along_y }, { left - along_x, top - height }, { left + along_x, top - height },
                                                                { right, top - along_y }, { right, top + along_y } } };
            put_at(spot[std::size_t(placement.dir)].first, spot[std::size_t(placement.dir)].second);
            return true;
        }
        if (record.how == Place::Flush) {                          // two rolls a side: below, left, above, right
            const std::array<std::pair<int, int>, 4> spot = { { { left, bottom }, { left - width, top }, { left, top - height }, { right, top } } };
            put_at(spot[std::size_t(placement.dir / 2)].first, spot[std::size_t(placement.dir / 2)].second);
            return true;
        }
        if (record.how == Place::Beside || (record.how == Place::BloodMoor && placement.flip == 1)) {   // FUN_00676150 / 00676650
            const std::array<std::pair<int, int>, 4> spot = { { { left - 16, bottom }, { left - width, top - 16 },
                                                                { right - width + 16, top - height }, { right, bottom - height + 16 } } };
            put_at(spot[std::size_t(placement.dir)].first, spot[std::size_t(placement.dir)].second);
        } else if (record.how == Place::BloodMoor) {
            const std::array<std::pair<int, int>, 4> spot = { { { right - width + 16, bottom }, { left - width, bottom - height + 16 },
                                                                { left - 16, top - height }, { right, top - 16 } } };
            put_at(spot[std::size_t(placement.dir)].first, spot[std::size_t(placement.dir)].second);
        } else if (placement.flip == 1) {                                                   // the town, FUN_00676450
            const std::array<std::pair<int, int>, 4> spot = { { { left, bottom }, { left - width, top + 8 },
                                                                { right - width, top - height }, { right, bottom - height - 8 } } };
            put_at(spot[std::size_t(placement.dir)].first, spot[std::size_t(placement.dir)].second);
        } else {
            const std::array<std::pair<int, int>, 4> spot = { { { right - width, bottom }, { left - width, bottom - height - 8 },
                                                                { left, top - height }, { right, top + 8 } } };
            put_at(spot[std::size_t(placement.dir)].first, spot[std::size_t(placement.dir)].second);
        }
        return true;
    };
    // FUN_00676dd0: clear of every earlier level but its link; the town
    // only where kTownAllowed says; the Burial Grounds not on the same side
    // of its link as another level from that link.
    auto fits = [&](int index) {
        const auto& record = recs[std::size_t(index)];
        for (int j = 0; j < index; ++j)
            if (j != record.link && !apart(placed[std::size_t(index)], placed[std::size_t(j)])) return false;
        if (!town_rules) return true;
        if (record.level == level_ids::kRogueEncampment) {
            const auto& linked = placed[std::size_t(record.link)];
            const auto& placement = placed[std::size_t(index)];
            return kTownAllowed[std::size_t(placement.dir + 4 * placement.flip + 8 * linked.dir + 32 * linked.flip)] != 0;
        }
        if (record.level == level_ids::kBurialGrounds)
            for (int k = 0; k < count; ++k)
                if (k != index && recs[std::size_t(k)].link == record.link && placed[std::size_t(k)].dir == placed[std::size_t(index)].dir) return false;
        return true;
    };
    for (int i = 0; i < count;) {
        if (!place(i)) {                                      // all sides tried: back up
            start_dir[std::size_t(i)] = start_flip[std::size_t(i)] = -1;
            placed[std::size_t(i)].dir = -1;
            placed[std::size_t(i)].flip = 0;
            if (--i < 0) break;
            continue;
        }
        if (!checked || fits(i)) ++i;                         // act 5's first two chains: no check (FUN_006772c0's null)
    }
    for (int i = 0; act1_flags && i < count; ++i) {           // FUN_00677180, act 1's chains
        const int dir = placed[std::size_t(i)].dir, next_dir = i + 1 < count ? placed[std::size_t(i + 1)].dir : -1;
        const int level = placed[std::size_t(i)].level;
        if (const auto found = defs.find(level); found == defs.end() || !found->second.outdoor) continue;
        for (const auto& rule : kAct1Flags)
            if ((rule.level == level || rule.level == 0) && level != rule.not1 && level != rule.not2 && dir == rule.dir && next_dir == rule.next_dir)
                placed[std::size_t(i)].flags |= rule.flag;
    }
    return placed;
}

// Act 1's outdoor levels (FUN_00677750), both chains from the act seed.
inline std::vector<Placed> act1_layout(const LevelDefs& defs, const d2d::rules::Rng& act_seed) {
    auto outdoors = place_chain(kAct1Outdoors, defs, act_seed, true);
    const auto highlands = place_chain(kAct1Highlands, defs, act_seed, false);
    outdoors.insert(outdoors.end(), highlands.begin(), highlands.end());
    return outdoors;
}

// Act 2's two Tal Rasha's Tombs (FUN_00642da0, before the layout): the
// boss tomb (act +0x94) and the staff tomb (+0x484), each rand(7) + 66 on
// the act seed, rolled in pairs until they differ.
struct Act2Tombs { int boss = 0, staff = 0; };
inline Act2Tombs roll_act2_tombs(d2d::rules::Rng& act_seed) {
    Act2Tombs tombs;
    do {
        tombs.boss = int(act_seed.next() % 7) + 66;
        tombs.staff = int(act_seed.next() % 7) + 66;
    } while (tombs.boss == tombs.staff);
    return tombs;
}

// Act 2's outdoor levels (FUN_00677790), both chains from the act seed
// after the tombs' rolls. The chain check (FUN_00676f50) is the plain
// overlap one.
inline std::vector<Placed> act2_layout(const LevelDefs& defs, d2d::rules::Rng act_seed) {
    roll_act2_tombs(act_seed);
    auto outdoors = place_chain(kAct2Outdoors, defs, act_seed, false, false);
    const auto canyon = place_chain(kAct2Canyon, defs, act_seed, false, false);
    outdoors.insert(outdoors.end(), canyon.begin(), canyon.end());
    return outdoors;
}

// Act 4's outdoor levels (FUN_00678a20), both chains from the act seed.
// The chain check (FUN_006770a0 / FUN_00677110) is the plain overlap one.
inline std::vector<Placed> act4_layout(const LevelDefs& defs, d2d::rules::Rng act_seed) {
    auto outdoors = place_chain(kAct4Outdoors, defs, act_seed, false, false);
    const auto chaos = place_chain(kAct4Chaos, defs, act_seed, false, false);
    outdoors.insert(outdoors.end(), chaos.begin(), chaos.end());
    return outdoors;
}

// Act 5's outdoor levels (FUN_00678a70), each chain from the act seed. No
// chain check but the uber levels' (FUN_00677030, the plain overlap one).
inline std::vector<Placed> act5_layout(const LevelDefs& defs, d2d::rules::Rng act_seed) {
    auto outdoors = place_chain(kAct5Outdoors, defs, act_seed, false, false, false);
    const auto tundra = place_chain(kAct5Tundra, defs, act_seed, false, false, false);
    const auto uber = place_chain(kAct5Uber, defs, act_seed, false, false);
    outdoors.insert(outdoors.end(), tundra.begin(), tundra.end());
    outdoors.insert(outdoors.end(), uber.begin(), uber.end());
    return outdoors;
}

// Lut Gholein's preset file (LvlPrest File1 / File2): the side the Rocky
// Waste went (FUN_006772c0 reads the next record's dir for level 40):
// 1 left, 2 above.
inline int lut_gholein_file(const std::vector<Placed>& layout) {
    for (const auto& placement : layout) if (placement.level == level_ids::kRockyWaste) return placement.dir;
    return -1;
}

// The town's preset, from the side of the Blood Moor it went to
// (LvlPrest "Act 1 - Town 1" File1..4): 0 townN1, 1 townE1, 2 townS1,
// 3 townW1 — named for the side the Blood Moor is on.
inline int town_file(const std::vector<Placed>& layout) {
    for (const auto& placement : layout) if (placement.level == level_ids::kRogueEncampment) return placement.dir;
    return -1;
}

// Courtyard 1's preset file (LvlPrest File1..3), from the side the Black
// Marsh went (FUN_00677180): above rolls File2 / File3, below File1 /
// File2 on the act seed; else -1, its own roll.
inline int courtyard_file(const std::vector<Placed>& layout, d2d::rules::Rng act_seed) {
    for (const auto& placement : layout) {
        if (placement.level != level_ids::kBlackMarsh) continue;
        if (placement.dir == 1) return 2 - int(act_seed.next() & 1);
        if (placement.dir == 3) return 1 - int(act_seed.next() & 1);
    }
    return -1;
}

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
inline Act3Layout act3_layout(const LevelDefs& defs, d2d::rules::Rng act_seed) {
    Act3Layout out;
    out.roll = int(act_seed.next() & 1);
    const auto def = [&](int level) { const auto found = defs.find(level); return found == defs.end() ? LevelDef{} : found->second; };
    const auto docks = def(level_ids::kKurastDocks);
    out.levels.push_back({ level_ids::kKurastDocks, docks.offset_x, docks.offset_y, docks.width, docks.height });
    const int width = def(level_ids::kSpiderForest).width, height = def(level_ids::kSpiderForest).height;
    const int cols = width / 32, rows = height / 32;
    if (cols < 2 || rows < 4) { out.failed = true; return out; }   // the ways in and out need 2 columns, 4 rows (Levels.txt row missing or short)
    // ponytail: game.exe retries without end; 1000 is far past any seed's need (at most 7 over act seeds of map seeds 1..1e6:
    // drlg-dump <data> 1-1000000 75 layout with $LAYOUT_STATS), it marks the layout failed
    constexpr int kTries = 1000;
    struct Piece { int x = 0, y = 0, dir = 0; std::vector<int> children; int cell_x = 0, cell_y = 0; JungleCells cells; };
    std::array<Piece, 3> pieces;
    pieces[0].x = docks.offset_x;
    pieces[0].y = docks.offset_y - height;
    int min_x = pieces[0].x, min_y = pieces[0].y, max_x = pieces[0].x + width;
    const int max_y = docks.offset_y;
    // FUN_006777d0's offsets up: a third, two thirds of the height (asm's rounding).
    const int third = [&] { const int half = (int((std::int64_t(height) * 0x55555555) >> 32) - height) >> 1; return half + int(unsigned(half) >> 31); }();
    const int two_thirds = [&] { const int part = int((std::int64_t(-2 * height) * 0x55555556) >> 32); return part + int(unsigned(part) >> 31); }();
    for (int count = 1; count < 3;) {
        const int parent = act_seed(count);
        Piece piece;
        piece.dir = int(act_seed.next() % 5);
        const std::array<std::pair<int, int>, 5> offset = { { { 0, -height }, { -width, third }, { width, third }, { -width, two_thirds }, { width, two_thirds } } };
        piece.x = pieces[std::size_t(parent)].x + offset[std::size_t(piece.dir)].first;
        piece.y = pieces[std::size_t(parent)].y + offset[std::size_t(piece.dir)].second;
        const Placed rect{ 0, piece.x, piece.y, width, height };
        bool clear = true;
        for (int other = 0; other < count && clear; ++other)
            clear = apart({ 0, pieces[std::size_t(other)].x, pieces[std::size_t(other)].y, width, height }, rect);
        if (!clear) continue;
        pieces[std::size_t(parent)].children.push_back(count);
        min_x = std::min(min_x, piece.x);
        min_y = std::min(min_y, piece.y);
        max_x = std::max(max_x, piece.x + width);
        pieces[std::size_t(count++)] = std::move(piece);
    }
    const int grid_w = (max_x - min_x) / 32 + 2, grid_h = (max_y - min_y) / 32 + 2;
    const auto cells = std::size_t(grid_w * grid_h);
    std::vector<int> owner, order, mark, bits;
    const std::array<int, 4> step = { -grid_w, grid_w, 1, -1 };   // up, down, right, left
    const std::array<int, 4> branch_bit = { 0x80, 0x40, 0x20, 0x10 };
    for (bool done = false; !done;) {
        if (++out.tries > kTries) { out.failed = true; return out; }
        owner.assign(cells, 0); order.assign(cells, 0); mark.assign(cells, 0); bits.assign(cells, 0);
        for (int index = 0; index < 3; ++index) {
            auto& piece = pieces[std::size_t(index)];
            piece.cell_x = (piece.x - min_x) / 32 + 1;
            piece.cell_y = (piece.y - min_y) / 32 + 1;
            int side = index == 0 ? int(act_seed.next() & 1) : int(piece.dir % 2 == 1);
            const int bottom = piece.cell_y + rows - 1;
            const auto cell_at = [&](int row, int col) { return std::size_t(row * grid_w + col); };
            int branches = 0;
            do {
                for (int row = piece.cell_y; row <= bottom; ++row)
                    for (int col = piece.cell_x; col < piece.cell_x + cols; ++col) {
                        owner[cell_at(row, col)] = index + 1;
                        order[cell_at(row, col)] = mark[cell_at(row, col)] = bits[cell_at(row, col)] = 0;
                    }
                int col = side + piece.cell_x, row = bottom;
                if (index != 0) {                 // the way in from the piece below
                    mark[cell_at(bottom, col)] = 1;
                    mark[cell_at(bottom, col + (side == 0 ? 1 : -1))] = 2;
                }
                for (const int child : piece.children) {   // the ways out, by the side the child went
                    const int dir = pieces[std::size_t(child)].dir;
                    const int exit_row = piece.cell_y + (dir == 0 ? 0 : dir <= 2 ? 3 : 1);
                    mark[cell_at(exit_row, piece.cell_x + (dir == 2 || dir == 4 ? 1 : 0))] = 1;
                }
                branches = index != 0;
                int steps = 0, number = (index + 1) * 100;
                while (row >= piece.cell_y) {     // the river, bottom to top, now and then a column over
                    order[cell_at(row, col)] = number++;
                    if (steps != 0 && row != piece.cell_y && act_seed.next() % 3 == 0) {
                        steps = 0;
                        col += side == 0 ? 1 : -1;
                        side ^= 1;
                        continue;
                    }
                    ++steps;
                    const auto beside = cell_at(row, col + (side == 0 ? 1 : -1));
                    if (steps > 1 && row > 1 && mark[beside] == 0) { ++branches; mark[beside] = 2; }
                    --row;
                }
            } while (branches < 2 && ++out.tries <= kTries);
            if (out.tries > kTries) { out.failed = true; return out; }
            while (branches > 3) {                // at most three: drop one at random
                const int drop = act_seed(branches);
                int seen = 0;
                for (int row = piece.cell_y, found = 0; row < piece.cell_y + rows && !found; ++row)
                    for (int col = piece.cell_x; col < piece.cell_x + cols && !found; ++col)
                        if (mark[cell_at(row, col)] == 2 && seen++ == drop) { mark[cell_at(row, col)] = 0; found = 1; }
                --branches;
            }
        }
        // Each cell's links: 8 up, 4 down, 2 right, 1 left.
        for (int cell = 0; cell < int(cells); ++cell) {
            int dir = 0;
            const auto own = owner[std::size_t(cell)];
            const std::array<int, 4> flag = { 8, 4, 2, 1 };
            if (mark[std::size_t(cell)] == 1) {   // a way out joins the piece's lowest-numbered neighbour
                int best = std::numeric_limits<int>::max();
                for (int side = 0; side < 4; ++side) {
                    const auto next = std::size_t(cell + step[std::size_t(side)]);
                    if (order[next] != 0 && order[next] < best && owner[next] == own) { dir = flag[std::size_t(side)]; best = order[next]; }
                }
                for (int side = 0; side < 4; ++side)
                    if (dir & flag[std::size_t(side)]) bits[std::size_t(cell + step[std::size_t(side)])] |= flag[std::size_t(side ^ 1)];
                for (int side = 0; side < 4; ++side) {
                    const auto next = std::size_t(cell + step[std::size_t(side)]);
                    if (mark[next] == 1 && owner[next] != own) dir |= flag[std::size_t(side)];
                }
            }
            if (const int number = order[std::size_t(cell)]; number != 0)
                for (int side = 0; side < 4; ++side)
                    if (std::abs(number - order[std::size_t(cell + step[std::size_t(side)])]) == 1) dir |= flag[std::size_t(side)];
            bits[std::size_t(cell)] |= dir;
        }
        // The branches: each joins a path cell of its piece, else starts over.
        done = true;
        for (int cell = 0; cell < int(cells) && done; ++cell) {
            int value = bits[std::size_t(cell)];
            const auto own = owner[std::size_t(cell)];
            const auto path = [&](int next) { return bits[std::size_t(next)] != 0 && bits[std::size_t(next)] < 0xf; };
            const int start = int(act_seed.next() & 3);
            const bool empty = value == 0;
            if (mark[std::size_t(cell)] != 2) { bits[std::size_t(cell)] |= value; continue; }
            for (int k = 0; k < 4; ++k) {
                const int side = (k + start) & 3, next = cell + step[std::size_t(side)];
                if (owner[std::size_t(next)] == own && path(next)) { value |= branch_bit[std::size_t(side)]; break; }
            }
            if (value == 0) { done = false; break; }
            if (empty) {                          // a clearing: maybe into the piece next door, maybe one more path
                bool none = true;
                for (int k = 0; k < 4 && none; ++k) {
                    const int side = (k + start) & 3, next = cell + step[std::size_t(side)];
                    if (owner[std::size_t(next)] != own && mark[std::size_t(next)] == 2) { value |= branch_bit[std::size_t(side)]; none = false; }
                }
                bool more = (act_seed.next() & 1) != 0 && none;
                const int again = int(act_seed.next() & 3);
                for (int k = 0; k < 4 && more; ++k) {
                    const int side = (again + k) & 3;
                    if (path(cell + step[std::size_t(side)]) && !(value & branch_bit[std::size_t(side)])) { value |= branch_bit[std::size_t(side)]; more = false; }
                }
            }
            for (int side = 0; side < 4; ++side)
                if (value & branch_bit[std::size_t(side)]) bits[std::size_t(cell + step[std::size_t(side)])] |= branch_bit[std::size_t(side ^ 1)];
            mark[std::size_t(cell)] = 0;
            bits[std::size_t(cell)] |= value;
        }
    }
    // Bits to LvlPrest Defs: 529 + links, a clearing by its branches, a
    // path cell with a branch through 0x6f13f0.
    for (auto& value : bits) {
        int def_id = value & 15;
        if (def_id == 0) {
            if (value > 15 && (def_id = kJungleClearings[std::size_t(value >> 4)]) == 0) out.failed = true;
        } else if (value < 16) {
            def_id += 529;
        } else {
            // ponytail: a second branch indexes the table by a Def (game.exe reads past it); counted as failed. Never
            // seen, nor a clearing without a Def: 0 failed layouts over map seeds 1..1e6 ($LAYOUT_STATS)
            for (int column = 0; column < 4; ++column)
                if (value & (0x10 << column)) def_id = def_id < 15 ? kJungleBranches[std::size_t(def_id)][std::size_t(column)] : 0;
            if (def_id == 0) out.failed = true;
        }
        value = def_id;
    }
    for (auto& piece : pieces)
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < cols; ++col) {
                const int def_id = bits[std::size_t((piece.cell_y + row) * grid_w + piece.cell_x + col)];
                piece.cells.presets.push_back(def_id);
                piece.cells.specials += def_id > 574;
            }
    // The lowest piece is the Spider Forest, the highest the Flayer Jungle.
    std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& first, const Piece& second) { return first.y > second.y; });
    for (int index = 0; index < 3; ++index) {
        const auto& piece = pieces[std::size_t(index)];
        out.levels.push_back({ level_ids::kSpiderForest + index, piece.x, piece.y, width, height });
        out.jungle[std::size_t(index)] = piece.cells;
    }
    // Kurast (FUN_00678910): 79..83 stacked up from the Flayer Jungle, centred on it.
    const Placed top = out.levels.back();
    for (int level = level_ids::kLowerKurast, rise = 0; level <= level_ids::kTravincal; ++level) {
        const auto kurast = def(level);
        rise -= kurast.height;
        out.levels.push_back({ level, top.x + top.width / 2 - kurast.width / 2, top.y + rise, kurast.width, kurast.height });
    }
    return out;
}

}  // namespace d2d::drlg
