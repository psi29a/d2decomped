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
    // Percentages of the MonLvl row (1.10+ style), per difficulty.
    struct Diff {
        int min_hp = 0, max_hp = 0, ac = 0, exp = 0;
        int a1_min = 0, a1_max = 0, a1_th = 0, a2_min = 0, a2_max = 0, a2_th = 0;
        int aidel = 0, aidist = 0;
        std::array<int, 8> aip{};
        std::string tc;                          // TreasureClass1
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
struct MonStats { int level = 1, hp = 1, ac = 0, th = 0, a1_min = 0, a1_max = 0, a2_min = 0, a2_max = 0, exp = 0; };
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
    return s;
}

// ---- combat

// Chance to hit in percent: 200 * AR / (AR + defense) * alvl / (alvl + dlvl),
// clamped to 5..95.
// ponytail: the documented formula; game.exe's isn't traced.
inline int hit_chance(int ar, int def, int alvl, int dlvl) {
    if (ar <= 0) return 5;
    const std::int64_t num = 200LL * ar * alvl, den = std::int64_t(ar + std::max(def, 0)) * std::max(alvl + dlvl, 1);
    return int(std::clamp<std::int64_t>(num / den, 5, 95));
}

// The worn weapon (right hand, else left) and the sums of the carried
// item stats that matter to an attack. Stats by ItemStatCost id: 17/18
// enhanced max/min damage %, 19 attack rating, 21/22 min/max damage,
// 119 attack rating %.
struct Attack { int min = 1, max = 2, ar = 0; };
inline Attack player_attack(const Tables& t, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& st,
                            int to_hit_factor) {
    using namespace d2d::d2s;
    std::array<std::int64_t, 128> sum{};
    const Item* weapon = nullptr;
    for (const auto& it : items) {
        const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
        const bool charm = it.location == 0 && it.panel == 1 && (it.code == "cm1" || it.code == "cm2" || it.code == "cm3");
        if (!worn && !charm) continue;
        if (worn && (it.slot == 4 || it.slot == 5)) {
            const auto b = t.item_base.find(it.code);
            if (b != t.item_base.end() && b->second.maxdam > 0 && (!weapon || it.slot == 4)) weapon = &it;
        }
        for (const auto& pr : it.props) if (pr.stat >= 0 && pr.stat < 128) sum[std::size_t(pr.stat)] += pr.value;
    }
    Attack a;
    const auto str = st.get(kStr), dex = st.get(kDex);
    std::int64_t lo = 1, hi = 2, bonus = 0;
    if (weapon) {
        const auto& b = t.item_base.at(weapon->code);
        std::int64_t ed = 0;
        for (const auto& pr : weapon->props) if (pr.stat == 17) ed += pr.value;   // on the weapon: its base
        lo = b.mindam * (100 + ed) / 100;
        hi = b.maxdam * (100 + ed) / 100;
        bonus = str * b.str_bonus + dex * b.dex_bonus;
    } else {
        bonus = str * 100;                    // no weapon: strength counts fully (hand-to-hand)
    }
    lo += sum[21]; hi += sum[22];
    lo = lo * (10000 + bonus) / 10000;
    hi = std::max(hi * (10000 + bonus) / 10000, lo);
    a.min = int(std::max<std::int64_t>(lo, 1));
    a.max = int(std::max<std::int64_t>(hi, a.min));
    // Attack rating: 5 per dexterity point from 7 (-35 base) + ToHitFactor, items.
    std::int64_t ar = dex * 5 - 35 + to_hit_factor + sum[19];
    ar = ar * (100 + sum[119]) / 100;
    a.ar = int(std::max<std::int64_t>(ar, 1));
    return a;
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
