// Monsters: MonStats / MonStats2 / MonLvl rows, which monsters a level
// spawns and where (game.exe's monster region and room population), and a
// spawned monster's stats. docs/research/re/monsters.md. Fighting is in
// combat.hpp, what they drop in drops.hpp.
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2d::rules {

// The MonStats / MonStats2 columns spawning and fighting read, per row.
struct MonType {
    std::string id, code, name_key, ai;         // Id, Code, NameStr, AI
    int base = -1;                              // BaseId row (19: fallen1, 91: scarab1)
    int min_grp = 0, max_grp = 0, party_min = 0, party_max = 0, sparse = 0, rarity = 0;
    std::array<int, 2> minion{ -1, -1 };        // minion1/2 rows
    std::array<int, 3> level{};                 // Level, Level(N), Level(H)
    int velocity = 0, run = 0;
    bool enabled = false, killable = false, melee = false;
    bool undead = false, demon = false;         // hUndead / lUndead, demon (Holy Bolt, FoH, Blessed Hammer)
    std::string miss_a2;                        // MissA2: what an A2 attack fires (quillrat1: spike1)
    std::string sound;                          // MonSound: its MonSounds.txt row
    // El1..3 Mode ("A1", "A2", ...) and Type (0 fire, 1 light, 2 cold, 3 poison, 4 magic, -1 none).
    std::array<std::string, 3> el_mode;
    std::array<int, 3> el_type{ -1, -1, -1 };
    bool can_block = false;                     // MonStats2 mBL
    // Percentages of the MonLvl row (1.10+ style), per difficulty.
    struct Diff {
        int min_hp = 0, max_hp = 0, ac = 0, exp = 0;
        int a1_min = 0, a1_max = 0, a1_th = 0, a2_min = 0, a2_max = 0, a2_th = 0;
        int aidel = 0, aidist = 0;
        std::array<int, 8> aip{};
        std::string tc;                          // TreasureClass1
        std::array<int, 6> res{};                // ResDm, ResMa, ResFi, ResLi, ResCo, ResPo (%)
        int to_block = 0, drain = 100, cold_effect = 0;   // ToBlock, Drain (leech %), coldeffect (speed % while chilled)
        struct El { int pct = 0, min = 0, max = 0, dur = 0; };
        std::array<El, 3> el{};                  // El1..3 Pct / MinD / MaxD (MonLvl %) / Dur (ticks)
    };
    std::array<Diff, 3> diff{};
    // MonStats2.
    int size = 2;                               // SizeX
    std::string base_w;                         // BaseW
    std::array<std::vector<std::string>, 16> parts;   // HDv..S8v components, per layer present
};

// MonLvl.txt, by level: the base values MonStats' percentages apply to.
struct MonLvl { std::array<int, 3> ac{}, th{}, hp{}, dm{}, xp{}; };

struct Monsters {
    std::vector<MonType> types;                 // MonStats rows
    std::unordered_map<std::string, int> by_id;
    std::vector<MonLvl> lvl;                    // by level
    // By Id, any case (Skills.txt's summon says ClayGolem for claygolem).
    [[nodiscard]] int row(std::string id) const {
        if (const auto it = by_id.find(id); it != by_id.end()) return it->second;
        for (auto& c : id) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const auto& [k, v] : by_id)
            if (k.size() == id.size() && std::ranges::equal(k, id, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; })) return v;
        return -1;
    }
};

// Levels.txt monster columns for one level.
struct LevelMon {
    std::array<int, 3> density{};               // MonDen, (N), (H): chance in 100000 per 3x3 subtiles
    std::array<int, 3> umin{}, umax{};          // MonUMin/Max (normal has none in 1.14d act 1)
    bool wander = false;                        // MonWndr
    int num_mon = 0;                            // NumMon
    std::vector<int> mon, nmon;                 // mon1.., nmon1.. rows (normal / NM+hell)
};

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
struct Spawn { int type = -1, x = 0, y = 0, leader = -1; };

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
// ponytail: champions/uniques not rolled; the seed at +0x20 the counts
// use isn't identified — the room's seed stands in; MonStats `spawn`
// replacements aren't applied (no act 1 wilderness monster has one).
template <class Fits, class Near>
void populate_room(const Monsters& m, const Region& reg, int density, SpawnRoom room, Rng& game,
                   Fits&& fits, Near&& near_entrance, std::vector<Spawn>& out) {
    using monster_detail::place;
    if (reg.types.empty() || density <= 0) return;
    density = std::min(density, 10000);
    for (int n = (room.h / 3) * (room.w / 3); n > 0; --n) {
        if (int(game.next() % 100000) > density) continue;
        int r = room.seed(reg.total) + 1;               // rarity pick
        std::size_t k = 0;
        for (; k < reg.types.size(); ++k) { r -= reg.types[k].second; if (r < 1) break; }
        k = std::min(k, reg.types.size() - 1);
        const int type = reg.types[k].first;
        const auto& t = m.types[std::size_t(type)];
        (void)room.seed(100);                           // FUN_005be020's roll: a group either way
        int lo = t.min_grp, hi = t.max_grp;
        if (t.base == 19 || t.base == 91) lo = hi = 1;  // FUN_0054ec40
        if (t.sparse && t.sparse < int(game.next() % 100)) continue;
        if (!lo || !hi || lo > hi) continue;
        // A spot in the room (the rect shrunk by one subtile at the top left).
        const int rx = room.x + 1, ry = room.y + 1, rw = room.x + room.w - rx, rh = room.y + room.h - ry;
        int sx = 0, sy = 0;
        bool found = false;
        for (int tries = 0; tries < 20 && !found; ++tries) {
            const int x = room.seed(rw) + rx, y = room.seed(rh) + ry;
            if (near_entrance(x, y)) continue;
            int px, py;
            found = place(room, x, y, -1, fits, px, py);
            if (found) { sx = x; sy = y; }
        }
        if (!found) continue;
        int lx, ly;
        if (!place(room, sx, sy, -1, fits, lx, ly)) continue;
        const int leader = int(out.size());
        out.push_back({ type, lx, ly, leader });
        auto near = [&](int who, int radius) {
            int px, py;
            if (who >= 0 && std::size_t(who) < m.types.size() && place(room, lx, ly, radius, fits, px, py))
                out.push_back({ who, px, py, leader });
        };
        if (t.minion[0] >= 0) {                         // FUN_005b2830
            const int count = room.seed.range(t.party_min, t.party_max);
            const int kinds = t.minion[1] >= 0 ? 2 : 1;
            for (int i = 0; i < count; ++i) near(t.minion[std::size_t(i % kinds)], 4);
        }
        for (int extra = room.seed(hi - lo + 1) + lo - 1; extra > 0; --extra) near(type, 3);
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
inline MonStats monster_stats(const Monsters& m, int type, int difficulty, Rng& rng) {
    MonStats s;
    if (type < 0 || std::size_t(type) >= m.types.size()) return s;
    const auto& t = m.types[std::size_t(type)];
    const int d = std::clamp(difficulty, 0, 2);
    s.level = std::max(t.level[std::size_t(d)], 1);
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
