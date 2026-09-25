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
struct LevelDef { int w = 0, h = 0, offset_x = 0, offset_y = 0; bool outdoor = false; };
using LevelDefs = std::unordered_map<int, LevelDef>;   // by Levels.txt Id

// A placed level: its rectangle in act tiles, the side of its link it
// went to (0 below, 1 left, 2 above, 3 right; -1 the chain's anchor), the
// alignment variant, and the outdoor flags the placement set.
struct Placed { int level = 0, x = 0, y = 0, w = 0, h = 0, dir = -1, flip = 0; std::uint32_t flags = 0; };

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
struct FlagRule { int level, not1, not2, a, b; std::uint32_t flag; };
inline constexpr std::array<FlagRule, 15> kAct1Flags = { {
    { 0, 2, 3, 1, 0, 4 }, { 0, 2, 3, 2, 3, 4 }, { 0, 3, 17, 2, 1, 8 }, { 0, 3, 17, 3, 0, 8 },
    { 0, 3, 17, 1, 1, 16 }, { 0, 3, 17, 3, 3, 16 }, { 2, 0, 0, 0, 0, 8 }, { 2, 0, 0, 2, 2, 8 },
    { 2, 0, 0, 3, 0, 8 }, { 2, 0, 0, 3, 2, 8 }, { 2, 0, 0, 0, 1, 1024 }, { 2, 0, 0, 1, 1, 1024 },
    { 2, 0, 0, 2, 1, 512 }, { 2, 0, 0, 2, 2, 128 }, { 2, 0, 0, 3, 2, 256 } } };

// Rectangles overlap unless they're `gap` or more apart on an axis
// (FUN_0066b800; gap 0 lets them touch).
inline bool apart(const Placed& a, const Placed& b, int gap = 0) {
    const int dx = a.x < b.x ? b.x - a.w - a.x : a.x - b.w - b.x;
    const int dy = a.y < b.y ? b.y - a.h - a.y : a.y - b.h - b.y;
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
    const int n = int(recs.size());
    std::vector<Placed> p(static_cast<std::size_t>(n));
    std::vector<int> start_dir(static_cast<std::size_t>(n), -1), start_flip(static_cast<std::size_t>(n), -1);
    for (int i = 0; i < n; ++i) {
        p[std::size_t(i)].level = recs[std::size_t(i)].level;
        if (const auto d = defs.find(recs[std::size_t(i)].level); d != defs.end()) {
            p[std::size_t(i)].w = d->second.w;
            p[std::size_t(i)].h = d->second.h;
        }
    }
    // Roll a first side (and alignment), else step to the next; false once
    // every one has been tried.
    auto next_side = [&](int i, bool with_flip) {
        auto& q = p[std::size_t(i)];
        if (start_dir[std::size_t(i)] == -1) {
            start_dir[std::size_t(i)] = q.dir = int(seed.next() & 3);
            if (with_flip) start_flip[std::size_t(i)] = q.flip = int(seed.next() & 1);
            return true;
        }
        if (!with_flip) {
            const int d = (q.dir + 1) & 3;
            if (d == start_dir[std::size_t(i)]) return false;
            q.dir = d;
            return true;
        }
        const int d = (q.dir + q.flip) & 3, f = (q.flip + 1) & 1;
        if (d == start_dir[std::size_t(i)] && f == start_flip[std::size_t(i)]) return false;
        q.dir = d;
        q.flip = f;
        return true;
    };
    auto place = [&](int i) {
        const auto& r = recs[std::size_t(i)];
        auto& q = p[std::size_t(i)];
        if (r.how == Place::Anchor) {
            q.dir = -1;
            if (const auto d = defs.find(r.level); d != defs.end()) { q.x = d->second.offset_x; q.y = d->second.offset_y; }
            return true;
        }
        const auto& L = p[std::size_t(r.link)];
        if (r.how == Place::Fixed) {                          // FUN_006768c0: below, left-aligned
            q.dir = 0;
            q.x = L.x;
            q.y = L.y + L.h;
            return true;
        }
        const bool flips = r.how == Place::BloodMoor || r.how == Place::Town;
        if (!next_side(i, flips)) return false;
        if (r.how == Place::BloodMoor) {                      // 96x56 left/right, 56x96 above/below
            q.w = q.dir & 1 ? 96 : 56;
            q.h = q.dir & 1 ? 56 : 96;
        }
        const int W = q.w, H = q.h, x0 = L.x, y0 = L.y, x1 = L.x + L.w, y1 = L.y + L.h;
        auto at = [&](int x, int y) { q.x = x; q.y = y; };
        if (r.how == Place::Beside || (r.how == Place::BloodMoor && q.flip == 1)) {   // FUN_00676150 / 00676650
            const std::array<std::pair<int, int>, 4> spot = { { { x0 - 16, y1 }, { x0 - W, r.how == Place::Beside ? y0 - 16 : y1 - H + 16 },
                                                                { x1 - W + 16, y0 - H }, { x1, y1 - H + 16 } } };
            at(spot[std::size_t(q.dir)].first, spot[std::size_t(q.dir)].second);
        } else if (r.how == Place::BloodMoor) {
            const std::array<std::pair<int, int>, 4> spot = { { { x1 - W + 16, y1 }, { x0 - W, y0 - 16 },
                                                                { x0 - 16, y0 - H }, { x1, y0 - 16 } } };
            at(spot[std::size_t(q.dir)].first, spot[std::size_t(q.dir)].second);
        } else if (q.flip == 1) {                                                   // the town, FUN_00676450
            const std::array<std::pair<int, int>, 4> spot = { { { x0, y1 }, { x0 - W, y1 - H - 8 },
                                                                { x1 - W, y0 - H }, { x1, y1 - H - 8 } } };
            at(spot[std::size_t(q.dir)].first, spot[std::size_t(q.dir)].second);
        } else {
            const std::array<std::pair<int, int>, 4> spot = { { { x1 - W, y1 }, { x0 - W, y0 + 8 },
                                                                { x0, y0 - H }, { x1, y0 + 8 } } };
            at(spot[std::size_t(q.dir)].first, spot[std::size_t(q.dir)].second);
        }
        return true;
    };
    // FUN_00676dd0: clear of every earlier level but its link; the town
    // only where kTownAllowed says; the Burial Grounds not on the same side
    // of its link as another level from that link.
    auto fits = [&](int i) {
        const auto& r = recs[std::size_t(i)];
        for (int j = 0; j < i; ++j)
            if (j != r.link && !apart(p[std::size_t(i)], p[std::size_t(j)])) return false;
        if (!town_rules) return true;
        if (r.level == 1) {
            const auto& L = p[std::size_t(r.link)];
            const auto& q = p[std::size_t(i)];
            return kTownAllowed[std::size_t(q.dir + 4 * q.flip + 8 * L.dir + 32 * L.flip)] != 0;
        }
        if (r.level == 17)
            for (int k = 0; k < n; ++k)
                if (k != i && recs[std::size_t(k)].link == r.link && p[std::size_t(k)].dir == p[std::size_t(i)].dir) return false;
        return true;
    };
    for (int i = 0; i < n;) {
        if (!place(i)) {                                      // all sides tried: back up
            start_dir[std::size_t(i)] = start_flip[std::size_t(i)] = -1;
            p[std::size_t(i)].dir = -1;
            p[std::size_t(i)].flip = 0;
            if (--i < 0) break;
            continue;
        }
        if (fits(i)) ++i;
    }
    if (town_rules)
        for (int i = 0; i < n; ++i) {
            const int a = p[std::size_t(i)].dir, b = i + 1 < n ? p[std::size_t(i + 1)].dir : -1;
            const int lv = p[std::size_t(i)].level;
            if (const auto d = defs.find(lv); d == defs.end() || !d->second.outdoor) continue;
            for (const auto& fr : kAct1Flags)
                if ((fr.level == lv || fr.level == 0) && lv != fr.not1 && lv != fr.not2 && a == fr.a && b == fr.b)
                    p[std::size_t(i)].flags |= fr.flag;
        }
    return p;
}

// Act 1's outdoor levels (FUN_00677750), both chains from the act seed.
inline std::vector<Placed> act1_layout(const LevelDefs& defs, const d2d::rules::Rng& act_seed) {
    auto a = place_chain(kAct1Outdoors, defs, act_seed, true);
    const auto b = place_chain(kAct1Highlands, defs, act_seed, false);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// The town's preset, from the side of the Blood Moor it went to
// (LvlPrest "Act 1 - Town 1" File1..4): 0 townN1, 1 townE1, 2 townS1,
// 3 townW1 — named for the side the Blood Moor is on.
inline int town_file(const std::vector<Placed>& layout) {
    for (const auto& p : layout) if (p.level == 1) return p.dir;
    return -1;
}

}  // namespace d2d::drlg
