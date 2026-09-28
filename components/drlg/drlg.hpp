// Dungeon/level generation (game.exe's .\DRLG\*.cpp): where an act's
// levels sit, and (later) what's inside the outdoor ones.
// docs/research/re/drlg.md.
#pragma once

#include <rules.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace d2d::drlg {

// What the layout needs from Levels.txt: size for the difficulty and the
// fixed offset of a level that anchors a chain.
struct LevelDef { int width = 0, height = 0, offset_x = 0, offset_y = 0; bool outdoor = false; };
using LevelDefs = std::unordered_map<int, LevelDef>;   // by Levels.txt Id

// A placed level: its rectangle in act tiles, the side of its link it
// went to (0 below, 1 left, 2 above, 3 right; -1 the chain's anchor), the
// alignment variant, and the outdoor flags the placement set.
struct Placed { int level = 0, x = 0, y = 0, width = 0, height = 0, dir = -1, flip = 0; std::uint32_t flags = 0; };

// How a record places its level against its link (game.exe's callbacks).
enum class Place { Anchor, Beside, BloodMoor, Town, Fixed };
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
                                       bool town_rules) {
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
    auto next_side = [&](int index, bool with_flip) {
        auto& placement = placed[std::size_t(index)];
        if (start_dir[std::size_t(index)] == -1) {
            start_dir[std::size_t(index)] = placement.dir = int(seed.next() & 3);
            if (with_flip) start_flip[std::size_t(index)] = placement.flip = int(seed.next() & 1);
            return true;
        }
        if (!with_flip) {
            const int next = (placement.dir + 1) & 3;
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
        const auto& linked = placed[std::size_t(record.link)];
        if (record.how == Place::Fixed) {                          // FUN_006768c0: below, left-aligned
            placement.dir = 0;
            placement.x = linked.x;
            placement.y = linked.y + linked.height;
            return true;
        }
        const bool flips = record.how == Place::BloodMoor || record.how == Place::Town;
        if (!next_side(index, flips)) return false;
        if (record.how == Place::BloodMoor) {                      // 96x56 left/right, 56x96 above/below
            placement.width = placement.dir & 1 ? 96 : 56;
            placement.height = placement.dir & 1 ? 56 : 96;
        }
        const int width = placement.width, height = placement.height, left = linked.x, top = linked.y, right = linked.x + linked.width, bottom = linked.y + linked.height;
        auto put_at = [&](int x, int y) { placement.x = x; placement.y = y; };
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
        if (record.level == 1) {
            const auto& linked = placed[std::size_t(record.link)];
            const auto& placement = placed[std::size_t(index)];
            return kTownAllowed[std::size_t(placement.dir + 4 * placement.flip + 8 * linked.dir + 32 * linked.flip)] != 0;
        }
        if (record.level == 17)
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
        if (fits(i)) ++i;
    }
    if (town_rules)
        for (int i = 0; i < count; ++i) {
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

// The town's preset, from the side of the Blood Moor it went to
// (LvlPrest "Act 1 - Town 1" File1..4): 0 townN1, 1 townE1, 2 townS1,
// 3 townW1 — named for the side the Blood Moor is on.
inline int town_file(const std::vector<Placed>& layout) {
    for (const auto& placement : layout) if (placement.level == 1) return placement.dir;
    return -1;
}

}  // namespace d2d::drlg
