// Monsters: MonStats / MonStats2 / MonLvl rows, which monsters a level
// spawns and where (game.exe's monster region and room population), and a
// spawned monster's stats. docs/research/re/monsters.md.
#pragma once

#include "rules.hpp"

#include <array>
#include <cstdint>
#include <tuple>
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
    [[nodiscard]] int row(const std::string& id) const {
        const auto it = by_id.find(id);
        return it == by_id.end() ? -1 : it->second;
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

// ---- combat

// Chance to hit in percent (FUN_0057d9b0): negative defense adds to the
// attack rating (and vice versa); c = AR x 100 / (AR + DEF), then
// c x 2 x alvl / (alvl + dlvl), clamped to 5..95.
inline int hit_chance(int ar, int def, int alvl, int dlvl) {
    std::int64_t a = ar, d = def;
    if (d < 0) { a -= d; d = 0; }
    if (a < 0) { d -= a; a = 0; }
    const std::int64_t c = a + d != 0 ? a * 100 / (a + d) : 100;
    return int(std::clamp<std::int64_t>(c * 2 * alvl / std::max(alvl + dlvl, 1), 5, 95));
}

// Item stats summed by ItemStatCost id: what's worn, charms carried, and
// what's socketed in them (the caller resolves sockets).
using StatSum = std::array<std::int64_t, 512>;

// The player in a fight: what an attack does and what protects them.
// Elements index 0 fire, 1 lightning, 2 cold, 3 poison, 4 magic.
struct Fighter {
    int min = 1, max = 2, ar = 1;                   // physical damage, attack rating
    std::array<std::pair<int, int>, 5> elem{};      // added damage (poison: its total over poison_len)
    int cold_len = 0, poison_len = 0;               // ticks
    int crushing = 0, deadly = 0, critical = 0, open_wounds = 0;   // chances, %
    bool knockback = false;
    int life_steal = 0, mana_steal = 0;             // %
    int ias = 0, wsm = 0, frw = 0, fhr = 0, fbr = 0;
    int defense = 0, block = 0;                     // block %, standing still
    int def_melee = 0, def_missile = 0;             // stats 33 / 32: extra defense vs each
    int dodge = 0, avoid = 0, evade = 0;            // stats 338 / 339 / 340, %
    int dr_pct = 0, dr_flat = 0, mdr = 0;
    std::array<int, 4> res{};                       // fire, lightning, cold, poison, %
    int thorns = 0, thorns_light = 0;               // attackers take (melee)
    int life_regen = 0, mana_regen = 0;             // hpregen; manarecoverybonus %
};

// Builds the Fighter from the worn weapon / shield, the summed item stats
// (`sum`: everything worn and carried; `weapon_sum`: the weapon and what's
// socketed in it), the character's stats and class; defense and
// resistances come from the char panel's sums.
//   damage (FUN_0057b420): the weapon's min/max with its own enhanced
//     damage 18 / 17 (+219 per level) — op 13 stats, which only touch the
//     item they're on — plus 21 / 22 (+218 per level) flat and 111 on both;
//     then one percentage: 25 damagepercent + str x StrBonus / 100 +
//     dex x DexBonus / 100 (at least -90). Barehanded 1-2 and no stat bonus.
//   attack rating (FUN_00622560): (dex - 7) x 5 + ToHitFactor + 19 (+224
//     per level), x (1 + 119 %)
//   block (FUN_00622720, a shield only): (shield block + BlockFactor + 20) x
//     (dex - 15) / (clvl x 2), at most 75
//   elements: 48/49 fire, 50/51 lightning, 54/55 cold (56 ticks), 57/58
//     poison per tick in 256ths (59 ticks), 52/53 magic
//   136 crushing blow, 141 (+250 per level) deadly strike, 337 critical
//   strike, 135 open wounds, 81 knockback, 60/62 life/mana steal, 93 IAS
//   (weapon speed WSM), 96 FRW, 99 FHR, 102 FBR, 33 / 32 defense vs melee /
//   missiles, 338 / 339 / 340 dodge / avoid / evade, 36 damage reduced %
//   (at most 50), 34 flat, 35 magic, 78 / 128 attacker takes damage /
//   lightning, 74 replenish life, 27 mana regeneration %.
// ponytail: no skills (masteries, skill damage %) yet.
inline Fighter make_fighter(const Tables& t, const d2d::d2s::Item* weapon, const d2d::d2s::Item* shield,
                            const StatSum& sum, const StatSum& weapon_sum, const d2d::d2s::Stats& st,
                            const ClassGains& g, int defense, const std::array<int, 4>& res) {
    using namespace d2d::d2s;
    Fighter f;
    const auto S = [&](int id) { return sum[std::size_t(id)]; };
    const auto W = [&](int id) { return weapon_sum[std::size_t(id)]; };
    const std::int64_t clvl = std::max<std::int64_t>(st.get(kLevel), 1), str = st.get(kStr), dex = st.get(kDex);
    const ItemBase* wb = nullptr;
    if (weapon) if (const auto b = t.item_base.find(weapon->code); b != t.item_base.end()) wb = &b->second;
    // In 256ths, as game.exe keeps them.
    std::int64_t lo = (wb ? wb->mindam * (100 + W(18)) / 100 : 1) + S(21) + S(111),
                 hi = (wb ? wb->maxdam * (100 + W(17) + W(219) * clvl / 8) / 100 : 2) + S(22) + S(218) * clvl / 8 + S(111);
    lo = std::max<std::int64_t>(lo, 1) << 8;
    hi = std::max<std::int64_t>(hi << 8, lo + 256);
    const std::int64_t pct = std::max<std::int64_t>(S(25) + (wb ? str * wb->str_bonus / 100 + dex * wb->dex_bonus / 100 : 0), -90);
    f.min = int(std::max<std::int64_t>((lo + lo * pct / 100) >> 8, 1));
    f.max = int(std::max<std::int64_t>((hi + hi * pct / 100) >> 8, f.min));
    f.ar = int(std::max<std::int64_t>((dex * 5 - 35 + g.to_hit + S(19) + S(224) * clvl / 8) * (100 + S(119)) / 100, 1));
    if (shield) if (const auto b = t.item_base.find(shield->code); b != t.item_base.end() && b->second.block > 0)
        f.block = int(std::clamp<std::int64_t>((b->second.block + g.block + S(20)) * (dex - 15) / (clvl * 2), 0, 75));
    f.elem = { { { int(S(48)), int(S(49)) }, { int(S(50)), int(S(51)) }, { int(S(54)), int(S(55)) },
                 { int(S(57) * S(59) / 256), int(S(58) * S(59) / 256) }, { int(S(52)), int(S(53)) } } };
    f.cold_len = int(S(56)); f.poison_len = int(S(59));
    f.crushing = int(S(136)); f.deadly = int(S(141) + S(250) * clvl / 8); f.critical = int(S(337));
    f.open_wounds = int(S(135));
    f.knockback = S(81) > 0;
    f.life_steal = int(S(60)); f.mana_steal = int(S(62));
    f.ias = int(S(93)); f.wsm = wb ? wb->speed : 0; f.frw = int(S(96)); f.fhr = int(S(99)); f.fbr = int(S(102));
    f.defense = defense;
    f.def_melee = int(S(33)); f.def_missile = int(S(32));
    f.dodge = int(S(338)); f.avoid = int(S(339)); f.evade = int(S(340));
    f.dr_pct = int(std::min<std::int64_t>(S(36), 50)); f.dr_flat = int(S(34)); f.mdr = int(S(35));
    f.res = res;
    f.thorns = int(S(78)); f.thorns_light = int(S(128));
    f.life_regen = int(S(74)); f.mana_regen = int(S(27));
    return f;
}

// Diminishing returns on the speed stats: IAS / FHR / FBR count
// 120 x v / (120 + v), FRW 150 x v / (150 + v).
inline int effective_speed(int v, int k = 120) { return v > 0 ? k * v / (k + v) : v; }

// Ticks an attack animation of `frames` frames at AnimData `rate` takes with
// `ias` and weapon speed `wsm` (1.10): EIAS = effective IAS - WSM, clamped
// to -85..75; ticks = ceil(256 x frames / (rate x (100 + EIAS) / 100)).
inline int attack_ticks(int frames, int rate, int ias, int wsm) {
    const int eias = std::clamp(effective_speed(ias) - wsm, -85, 75);
    const int speed = std::max(rate * (100 + eias) / 100, 1);
    return (256 * frames + speed - 1) / speed;
}

// Open wounds: bleeding per second for a character level (1.10's table in
// 256ths a tick, times 25 ticks), for 8 seconds.
inline int open_wounds_per_sec(int clvl) {
    const int v = clvl < 15 ? 9 * clvl + 31 : clvl < 31 ? 18 * clvl - 104 : clvl < 46 ? 27 * clvl - 374
                : clvl < 61 ? 36 * clvl - 779 : 45 * clvl - 1319;
    return std::max(v, 0) * 25 / 256;
}

// A monster being struck: its life now and max, defense, level, block
// chance (0 unless it can block), resistances (physical, magic, fire,
// lightning, cold, poison %), Drain (the % of leech that works on it).
struct Target {
    int hp = 1, max_hp = 1, ac = 0, level = 1, block = 0;
    std::array<int, 6> res{};
    int drain = 100;
};
// What one player hit does: the instant damage (physical after resistance,
// fire / lightning / cold / magic, crushing blow), leeched life and mana,
// poison (total and ticks), chill ticks, and whether it bled, knocked back.
struct Blow {
    bool hit = false, blocked = false, crushing = false, deadly = false, bleed = false, knockback = false;
    int damage = 0, life = 0, mana = 0, poison = 0, poison_ticks = 0, chill_ticks = 0;
};
inline int resisted(int dmg, int res) { return res >= 100 ? 0 : dmg * (100 - res) / 100; }

// The player's melee hit on `t` (hit chance, then the monster's block):
// critical or deadly strike doubles the physical damage; physical resistance cuts it;
// leech is the physical damage dealt x steal % x Drain %; crushing blow
// takes a quarter of its current life (less physical resistance).
// ponytail: crushing blow's boss / difficulty divisors aren't applied.
inline Blow player_blow(const Fighter& f, const Target& t, int clvl, Rng& rng) {
    Blow b;
    if (rng(100) >= hit_chance(f.ar, t.ac, clvl, t.level)) return b;
    if (t.block > 0 && rng(100) < t.block) { b.blocked = true; return b; }
    b.hit = true;
    int phys = rng.range(f.min, f.max);
    // Critical strike and deadly strike are separate rolls; either doubles
    // (FUN_0057b7d0; the mastery crit joins them with skills).
    if ((f.critical > 0 && rng(100) < f.critical) || (f.deadly > 0 && rng(100) < f.deadly)) { phys *= 2; b.deadly = true; }
    phys = resisted(phys, t.res[0]);
    b.life = phys * f.life_steal * t.drain / 10000;
    b.mana = phys * f.mana_steal * t.drain / 10000;
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    int elem = 0;
    for (int e = 0; e < 5; ++e) {
        const auto [lo, hi] = f.elem[std::size_t(e)];
        if (hi <= 0) continue;
        const int d = resisted(rng.range(lo, hi), t.res[std::size_t(kRes[e])]);
        if (e == 3) { b.poison = d; b.poison_ticks = std::max(f.poison_len, 1); continue; }
        if (e == 2 && d > 0) b.chill_ticks = f.cold_len;
        elem += d;
    }
    int cb = 0;
    if (f.crushing > 0 && rng(100) < f.crushing) { b.crushing = true; cb = resisted(t.hp / 4, t.res[0]); }
    b.bleed = f.open_wounds > 0 && rng(100) < f.open_wounds;
    b.knockback = f.knockback;
    b.damage = phys + elem + cb;
    return b;
}

// A monster's attack on the player (`moving`: walking or running;
// `missile`: a spike rather than a swing): hit chance against defense plus
// the vs-melee / vs-missile bonus; then the defender's rolls
// (FUN_0057dfb0 / FUN_0057dd60): block (a third while moving), then evade
// while moving, else dodge a swing / avoid a missile; then physical damage
// less damage-reduced % then flat (it can reach 0), and each elemental
// attack (at its chance) less resistance, fire / lightning / cold less
// magic damage reduction; poison lands as a total over its ticks.
// ponytail: poison isn't cut by resistance length; cold doesn't slow the player.
struct Taken { bool hit = false, blocked = false, dodged = false; int damage = 0, poison = 0, poison_ticks = 0; };
inline Taken monster_blow(const Fighter& d, int dlvl, bool moving, const MonStats& m, bool a2, Rng& rng,
                          bool missile = false) {
    Taken k;
    if (rng(100) >= hit_chance(m.th, d.defense + (missile ? d.def_missile : d.def_melee), m.level, dlvl)) return k;
    if (d.block > 0 && rng(100) < (moving ? d.block / 3 : d.block)) { k.blocked = true; return k; }
    const int dodge = moving ? d.evade : missile ? d.avoid : d.dodge;
    if (dodge > 0 && rng(100) < dodge) { k.dodged = true; return k; }
    k.hit = true;
    int phys = a2 ? rng.range(m.a2_min, m.a2_max) : rng.range(m.a1_min, m.a1_max);
    phys = std::max(phys * (100 - d.dr_pct) / 100 - d.dr_flat, 0);
    k.damage = phys;
    for (const auto& e : m.el) {
        if (e.type < 0 || e.mode != (a2 ? "A2" : "A1") || rng(100) >= e.pct) continue;
        const int roll = rng.range(e.min, e.max);
        switch (e.type) {
            case 3: k.poison += resisted(roll, d.res[3]); k.poison_ticks = std::max(e.dur, 25); break;
            case 4: k.damage += std::max(roll - d.mdr, 0); break;
            default: k.damage += std::max(resisted(roll, d.res[std::size_t(e.type)]) - d.mdr, 0); break;
        }
    }
    return k;
}

// Experience for a kill: the monster's, less when the character outlevels
// it by more than 5 (81 / 62 / 43 / 24 % at 6..9 levels, 5 % from 10), or
// scaled by clvl / mlvl when the monster is more than 5 levels higher.
// ponytail: single player, no party share, no experience.txt ExpRatio
// past level 69.
inline std::int64_t kill_exp(int exp, int clvl, int mlvl) {
    static constexpr int kPenalty[5] = { 81, 62, 43, 24, 5 };
    if (clvl > mlvl + 5) return std::int64_t(exp) * kPenalty[std::min(clvl - mlvl - 6, 4)] / 100;
    if (mlvl > clvl + 5) return std::int64_t(exp) * clvl / mlvl;
    return exp;
}

// Adds experience; every level reached (exp_next[level] = experience for
// level + 1) gives StatPerLevel stat points, a skill point and the class's
// life/stamina/mana per level (quarter points, 8.8 fixed stats). Returns the
// levels gained.
inline int gain_exp(d2d::d2s::Stats& st, std::int64_t exp, const std::vector<std::int64_t>& exp_next, const ClassGains& g) {
    using namespace d2d::d2s;
    st.v[kExp] += exp;
    int gained = 0;
    for (;;) {
        const auto lvl = st.get(kLevel);
        if (lvl < 1 || std::size_t(lvl) >= exp_next.size() || exp_next[std::size_t(lvl)] <= 0
            || st.v[kExp] < exp_next[std::size_t(lvl)] || lvl >= 99) break;
        ++st.v[kLevel];
        ++gained;
        st.v[kStatPts] += g.stat_per_level;
        st.v[kSkillPts] += 1;
        for (auto [cur, max, q] : { std::tuple{ kLife, kMaxLife, g.life_per_level },
                                    std::tuple{ kStamina, kMaxStamina, g.stamina_per_level },
                                    std::tuple{ kMana, kMaxMana, g.mana_per_level } }) {
            st.v[std::size_t(max)] += std::int64_t(q) * 64;
            st.v[std::size_t(cur)] += std::int64_t(q) * 64;
        }
    }
    return gained;
}

// ---- drops

// What a kill drops: an item (code, quality 1 low .. 7 unique) or gold.
struct Drop { std::string code; int quality = 2; int gold = 0; };

// The item's quality at item level ilvl (ITEMS_RollQuality in 1.10's
// terms; ItemRatio.txt): from unique down to superior, the odds are
// (base - (ilvl - qlvl) / divisor) * 128, at least `min`, less the treasure
// class's modifier (1024ths); a roll below 128 of them wins. Rings,
// amulets, charms, jewels (ItemTypes Magic) are at least magic; potions,
// gems and the like (not armor or weapons, not Magic) stay normal.
// ponytail: no magic find, no class-specific rows, no low quality.
inline int roll_quality(const Tables& t, const std::string& code, int ilvl, const std::array<int, 4>& mod, Rng& rng) {
    const auto* info = [&]() -> const ItemInfo* { const auto i = t.item_info.find(code); return i == t.item_info.end() ? nullptr : &i->second; }();
    const auto type = info ? t.types.find(info->type) : t.types.end();
    const bool magic_only = type != t.types.end() && type->second.always_magic;
    if (!magic_only && (!info || info->kind == 0 || (type != t.types.end() && type->second.always_normal))) return 2;
    const auto b = t.item_base.find(code);
    const int qlvl = b != t.item_base.end() ? b->second.level : 1;
    const bool uber = b != t.item_base.end() && (b->second.ubercode == code || b->second.ultracode == code)
                      && b->second.normcode != code;
    const auto& R = t.quality_ratio[uber ? 1 : 0];
    static constexpr int kQuality[5] = { 7, 5, 6, 4, 3 };
    for (int q = 0; q < (magic_only ? 4 : 5); ++q) {
        if (q == 2 && type != t.types.end() && !type->second.can_rare) continue;
        std::int64_t chance = std::int64_t(R[std::size_t(q)].base - (ilvl - qlvl) / std::max(R[std::size_t(q)].divisor, 1)) * 128;
        chance = std::max<std::int64_t>(chance, R[std::size_t(q)].min);
        if (q < 4) chance -= chance * mod[std::size_t(q)] / 1024;
        if (chance <= 0 || rng(int(std::min<std::int64_t>(chance, 1 << 30))) < 128) return kQuality[q];
    }
    return magic_only ? 4 : 2;
}

// Resolve treasure class `tc` for a monster of level mlvl: each pick
// rolls NoDrop against the entries' weights; a class entry resolves in
// turn (the highest quality modifiers on the way win), "gld" is gold
// (",mul=N": times N/256).
// ponytail: single player NoDrop as written (no player-count scaling),
// no negative picks, the gold amount isn't traced (mlvl + rand(8 mlvl)).
inline void roll_drops(const Tables& t, const std::string& tc, int mlvl, Rng& rng, std::vector<Drop>& out,
                       std::array<int, 4> mod = {}, int depth = 0) {
    const auto it = t.treasure.find(tc);
    if (it == t.treasure.end() || depth > 10) return;
    const auto& c = it->second;
    for (int i = 0; i < 4; ++i) mod[std::size_t(i)] = std::max(mod[std::size_t(i)], c.mod[std::size_t(i)]);
    int total = c.nodrop;
    for (const auto& [n, p] : c.items) total += p;
    for (int pick = 0; pick < std::max(c.picks, 1); ++pick) {
        int r = rng(total);
        if (r < c.nodrop) continue;
        r -= c.nodrop;
        for (const auto& [name, p] : c.items) {
            if (r >= p) { r -= p; continue; }
            if (name.starts_with("gld")) {
                int gold = mlvl + rng(8 * std::max(mlvl, 1));
                if (const auto m = name.find("mul="); m != std::string::npos) gold = gold * std::atoi(name.c_str() + m + 4) / 256;
                out.push_back({ "gld", 2, std::max(gold, 1) });
            } else if (t.treasure.contains(name)) {
                roll_drops(t, name, mlvl, rng, out, mod, depth + 1);
            } else {
                out.push_back({ name, roll_quality(t, name, mlvl, mod, rng), 0 });
            }
            break;
        }
    }
}

// The auto classes game.exe builds from the item tables: weapN / armoN
// hold the spawnable weapons / armor of level N-2..N (N = 3, 6, ... 87),
// weighted by their rarity.
inline void add_auto_treasure(Tables& t, const std::vector<std::pair<std::string, int>>& weapons_by_level,
                              const std::vector<std::pair<std::string, int>>& armor_by_level) {
    for (const auto& [prefix, list] : { std::pair{ "weap", &weapons_by_level }, std::pair{ "armo", &armor_by_level } })
        for (int n = 3; n <= 87; n += 3) {
            TreasureClass c;
            c.nodrop = 0;
            for (const auto& [code, lvl] : *list)
                if (lvl > n - 3 && lvl <= n) {
                    const auto r = t.item_rarity.find(code);
                    c.items.emplace_back(code, r == t.item_rarity.end() ? 1 : std::max(r->second, 1));
                }
            if (!c.items.empty()) t.treasure.try_emplace(prefix + std::to_string(n), std::move(c));
        }
}

}  // namespace d2d::rules
