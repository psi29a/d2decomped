// Monsters: MonStats / MonStats2 / MonLvl rows, which monsters a level
// spawns and where (game.exe's monster region and room population), and a
// spawned monster's stats. docs/research/re/monsters.md. Fighting is in
// combat.hpp, what they drop in drops.hpp.
#pragma once

#include "montypes.hpp"
#include "rules.hpp"
#include "uniques.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2d::rules {

// The level's monster region (FUN_005479c0 / FUN_005475e0): up to NumMon
// (at most 13) types drawn without replacement from the difficulty's
// list, each kept with its rarity when MonStats enables it.
// ponytail: Levels.txt's "must pick a flagged monster first" retry
// (level def +0x31) isn't mapped to a column; act 1 wilderness doesn't set it.
struct Region {
    std::vector<std::pair<int, int>> types;     // (MonStats row, rarity)
    int total = 0;                              // rarity sum
};
inline Region monster_region(const Monsters& m, const LevelMon& L, int difficulty, Rng& seed) {
    Region r;
    auto list = difficulty == 0 ? L.mon : L.nmon;
    const int picks = std::min<int>(std::min(L.num_mon, 13), int(list.size()));
    for (int i = 0; i < picks && !list.empty(); ++i) {
        const int k = seed(int(list.size()));
        const int row = list[std::size_t(k)];
        list.erase(list.begin() + k);
        if (row < 0 || std::size_t(row) >= m.types.size() || !m.types[std::size_t(row)].enabled) continue;
        r.types.emplace_back(row, m.types[std::size_t(row)].rarity);
        r.total += m.types[std::size_t(row)].rarity;
    }
    return r;
}

// A spawned monster, in level-relative subtiles; `leader` is the index
// of its group's first monster (itself for a leader).
struct Spawn {
    int type = -1, x = 0, y = 0, leader = -1, super = -1;     // super: SuperUniques row
    Boss boss = Boss::none;                                   // champion / unique / its minion
    std::vector<int> mods;                                    // MonUMod ids
    int name_seed = 0;                                        // a unique's (rndname)
};

// A level's population so far (monster region +4 rooms done, +0xc rooms
// in the level, +0x2c8 uniques made) and what its uniques roll with.
struct Population {
    int rooms_done = 0, rooms_total = 0, uniques = 0;
    int umin = 0, umax = 0;                                   // Levels MonUMin / MonUMax for the difficulty
    int difficulty = 0;
    const UMods* umods = nullptr;
};

// A room to populate: its rect in subtiles and its seed.
struct SpawnRoom { int x = 0, y = 0, w = 0, h = 0; Rng seed; };

namespace monster_detail {

// FUN_005b2a00's placement: rings of 3 subtiles around (x, y) out to
// 3 * radius (radius < 0: the point itself), each walked from a random
// point on it round the square; the first spot in the room that the
// monster fits wins. `fits(x, y)`: the collision test (FUN_0064d9b0).
template <class Fits>
bool place(SpawnRoom& room, int x, int y, int radius, Fits&& fits, int& ox, int& oy) {
    const int last = radius < 0 ? 0 : radius * 3;
    for (int c = radius < 0 ? 0 : 3; c <= last; c += 3) {
        const bool even = (room.seed.next() & 1) == 0;
        int dx, dy;
        if (even) { dx = room.seed(c); dy = c; } else { dx = c; dy = room.seed(c); }
        int sx = even ? 1 : 0, sy = even ? 0 : 1;
        if (room.seed.next() & 1) dx = -dx;
        if (room.seed.next() & 1) dy = -dy;
        int px = x + dx, py = y + dy;
        const int x0 = x - c, x1 = x + c, y0 = y - c, y1 = y + c;
        for (int n = c ? c * 8 : 1; n > 0; --n) {
            if (px == x0 && py == y0) { sx = 1; sy = 0; }
            if (px == x1) {
                if (py == y0) { sx = 0; sy = 1; }
                if (py == y1) { sx = -1; sy = 0; }
            }
            if (px == x0) {
                if (py == y1) { sx = 0; sy = -1; }
                if (py == y0 && px == x1 && py == y1) { sx = 0; sy = 0; }
            }
            px += sx; py += sy;
            if (px >= room.x && py >= room.y && px < room.x + room.w && py < room.y + room.h && fits(px, py)) {
                ox = px; oy = py;
                return true;
            }
        }
    }
    return false;
}

}  // namespace monster_detail

// Populate one room (FUN_0054ec90): one roll of the game seed per 3x3
// subtiles against the density; a hit picks a type by rarity
// (FUN_005bde80), rolls unique-or-group (FUN_005be020; no uniques while
// MonUMin/Max are 0, as in normal act 1), then places a group
// (FUN_0054df80): a free spot at a random point of the room (20 tries,
// not within WarpDist of an entrance, FUN_0054dc40), the leader there,
// PartyMin..Max minions round it (FUN_005b2830, radius 4), then
// MinGrp..MaxGrp - 1 more of its type (radius 3). Fallen and dung
// soldiers come as one leader plus their party.
// `fits(x, y)`: can a monster stand at subtile (x, y); `near_entrance(x, y)`:
// too close to where players come in.
// With `pop`, champions and uniques as FUN_005be020 / FUN_005a43e0 roll them.
// ponytail: the seed at +0x20 the counts use isn't identified — the
// room's seed stands in; MonStats `spawn` replacements aren't applied (no
// act 1 wilderness monster has one).
template <class Fits, class Near>
void populate_room(const Monsters& m, const Region& reg, int density, SpawnRoom room, Rng& game,
                   Fits&& fits, Near&& near_entrance, std::vector<Spawn>& out, Population* pop = nullptr) {
    using monster_detail::place;
    if (pop) ++pop->rooms_done;                                 // FUN_0054ebc0
    if (reg.types.empty() || density <= 0) return;
    density = std::min(density, 10000);
    // FUN_0054dc40: a spot in the room (the rect shrunk by one subtile at
    // the top left), 20 tries, not by an entrance, where a monster fits.
    auto spot = [&](int& sx, int& sy) {
        const int rx = room.x + 1, ry = room.y + 1, rw = room.x + room.w - rx, rh = room.y + room.h - ry;
        for (int tries = 0; tries < 20; ++tries) {
            const int x = room.seed(rw) + rx, y = room.seed(rh) + ry;
            if (near_entrance(x, y)) continue;
            int px, py;
            if (place(room, x, y, -1, fits, px, py)) { sx = x; sy = y; return true; }
        }
        return false;
    };
    for (int n = (room.h / 3) * (room.w / 3); n > 0; --n) {
        if (int(game.next() % 100000) > density) continue;
        int r = room.seed(reg.total) + 1;               // rarity pick
        std::size_t k = 0;
        for (; k < reg.types.size(); ++k) { r -= reg.types[k].second; if (r < 1) break; }
        k = std::min(k, reg.types.size() - 1);
        const int type = reg.types[k].first;
        const auto& t = m.types[std::size_t(type)];
        // FUN_005be020 (room seed): a unique while under MonUMin (chance
        // rooms done / rooms in the level) or under MonUMax (6 %); else a
        // group (its champion answer, 1, becomes a group too).
        bool boss = false;
        if (pop && pop->uniques < pop->umin && pop->rooms_total > 0)
            boss = room.seed(100) < pop->rooms_done * 100 / pop->rooms_total;
        if (pop && !boss && pop->uniques < pop->umax) boss = room.seed(100) < 6;
        if (!boss) (void)room.seed(100);
        if (boss && pop->umods) {
            // FUN_005a43e0: one monster of a type picked again (FUN_005bde80
            // unique pick: NM / hell use the region's list), at a spot
            // (FUN_005a09e0), champion or unique (FUN_005a0760), then a
            // champion's pack (FUN_0054e1e0) or a unique's minions (FUN_005a0c00).
            // ponytail: normal's pick from Levels.txt umon1.. isn't there (act 1
            // normal has MonUMin / MonUMax 0); a monster's own seed (unit
            // +0x20) is the room's here; MonStats `spawn` replacement skipped.
            int r2 = room.seed(reg.total) + 1;
            std::size_t k2 = 0;
            for (; k2 < reg.types.size(); ++k2) { r2 -= reg.types[k2].second; if (r2 < 1) break; }
            const int utype = reg.types[std::min(k2, reg.types.size() - 1)].first;
            int sx, sy, lx, ly;
            if (!spot(sx, sy) || !place(room, sx, sy, -1, fits, lx, ly)) continue;
            const auto& ut = m.types[std::size_t(utype)];
            auto b = roll_boss(*pop->umods, ut, pop->difficulty, true, room.seed);
            const int leader = int(out.size());
            out.push_back({ utype, lx, ly, leader, -1, b.kind, b.mods, b.name_seed });
            ++pop->uniques;
            int px, py;
            if (b.kind == Boss::champion) {                       // FUN_0054e1e0: 1..3 more champions
                for (int c = room.seed(3) + 1; c > 0; --c)
                    if (place(room, lx, ly, 4, fits, px, py)) out.push_back({ utype, px, py, leader, -1, Boss::champion, { umod::champion } });
            } else {                                              // FUN_005a0c00: 3..6 minions (minion1 or its own type)
                const int mt = ut.minion[0] >= 0 ? ut.minion[0] : utype;
                for (int c = room.seed(4) + 3; c > 0; --c)
                    if (place(room, lx, ly, 3, fits, px, py)) out.push_back({ mt, px, py, leader, -1, Boss::minion, {} });
            }
            continue;
        }
        int lo = t.min_grp, hi = t.max_grp;
        if (t.base == 19 || t.base == 91) lo = hi = 1;  // FUN_0054ec40
        if (t.sparse && t.sparse < int(game.next() % 100)) continue;
        if (!lo || !hi || lo > hi) continue;
        int sx = 0, sy = 0;
        if (!spot(sx, sy)) continue;
        int lx, ly;
        if (!place(room, sx, sy, -1, fits, lx, ly)) continue;
        const int leader = int(out.size());
        out.push_back({ type, lx, ly, leader });
        auto nearby = [&](int who, int radius) {
            int px, py;
            if (who >= 0 && std::size_t(who) < m.types.size() && place(room, lx, ly, radius, fits, px, py))
                out.push_back({ who, px, py, leader });
        };
        if (t.minion[0] >= 0) {                         // FUN_005b2830
            const int count = room.seed.range(t.party_min, t.party_max);
            const int kinds = t.minion[1] >= 0 ? 2 : 1;
            for (int i = 0; i < count; ++i) nearby(t.minion[std::size_t(i % kinds)], 4);
        }
        for (int extra = room.seed(hi - lo + 1) + lo - 1; extra > 0; --extra) nearby(type, 3);
    }
}

// A monster's stats at `level`: MonStats' percentages of the MonLvl row
// (1.10+ tables; normal uses the monster's own Level). HP is rolled.
// ponytail: the stat init (MONSTER_InitStats in 1.10) isn't traced in
// game.exe — this is the documented txt contract.
// Elemental attacks come as MonLvl damage percentages too (El1..3 MinD/MaxD).
struct MonStats {
    int level = 1, hp = 1, ac = 0, th = 0, a1_min = 0, a1_max = 0, a2_min = 0, a2_max = 0, exp = 0;
    struct El { int type = -1, pct = 0, min = 0, max = 0, dur = 0; std::string_view mode; };
    std::array<El, 3> el{};
};
inline MonStats monster_stats(const Monsters& m, int type, int difficulty, Rng& rng, int level_add = 0) {
    MonStats s;
    if (type < 0 || std::size_t(type) >= m.types.size()) return s;
    const auto& t = m.types[std::size_t(type)];
    const int d = std::clamp(difficulty, 0, 2);
    s.level = std::max(t.level[std::size_t(d)] + level_add, 1);
    if (m.lvl.empty()) return s;
    const auto& L = m.lvl[std::min<std::size_t>(std::size_t(s.level), m.lvl.size() - 1)];
    const auto& p = t.diff[std::size_t(d)];
    auto pct = [](int base, int v) { return base * v / 100; };
    const int hp = L.hp[std::size_t(d)];
    s.hp  = std::max(rng.range(pct(hp, p.min_hp), pct(hp, p.max_hp)), 1);
    s.ac  = pct(L.ac[std::size_t(d)], p.ac);
    s.th  = pct(L.th[std::size_t(d)], p.a1_th);
    s.a1_min = pct(L.dm[std::size_t(d)], p.a1_min);
    s.a1_max = std::max(pct(L.dm[std::size_t(d)], p.a1_max), s.a1_min);
    s.a2_min = pct(L.dm[std::size_t(d)], p.a2_min);
    s.a2_max = std::max(pct(L.dm[std::size_t(d)], p.a2_max), s.a2_min);
    s.exp = pct(L.xp[std::size_t(d)], p.exp);
    for (std::size_t e = 0; e < 3; ++e) {
        const auto& E = p.el[e];
        if (t.el_type[e] < 0 || E.max <= 0) continue;
        s.el[e] = { t.el_type[e], E.pct ? E.pct : 100, pct(L.dm[std::size_t(d)], E.min),
                    std::max(pct(L.dm[std::size_t(d)], E.max), pct(L.dm[std::size_t(d)], E.min)), E.dur, t.el_mode[e] };
    }
    return s;
}

}  // namespace d2d::rules
