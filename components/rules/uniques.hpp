// Champions and uniques: MonUMod.txt, the mods a champion or unique rolls
// (FUN_005a0760 -> FUN_005a0500 / FUN_005a0600) and what they do to it
// (FUN_005a2120 -> the mod functions at 0x73c008), from game.exe 1.14d.
// docs/research/re/monsters.md "Champions and uniques".
#pragma once

#include "montypes.hpp"
#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::rules {

// A MonUMod row (0x20 bytes in game.exe): enabled, champion-only, fPick
// constraint, excluded MonTypes, pick weights by difficulty.
struct UMod {
    int id = 0;
    bool enabled = false, champion = false;
    int fpick = 0;
    std::string exclude1, exclude2;
    std::array<int, 3> cpick{}, upick{};
};
// MonUMod.txt, plus its `constants` column (by row): 0 champion chance, 1..3
// minion +hp%, 4..6 champion +hp%, 7..9 unique +hp%, 10 champion +tohit%,
// 11 champion +dmg%, 12 minion +tohit%, 13 unique +tohit%, 14 minion +dmg%
// (strong), 15 unique +dmg% (strong), 16..21 minion elemental min / max %,
// 22..27 champion's, 28..33 unique's (each by difficulty).
struct UMods {
    std::vector<UMod> rows;
    std::array<int, 34> k{};
};

// What a spawned monster is (MonsterData +0x16 flags: 4 champion, 8 unique
// counted, 0x10 minion) and its mods (+0x1c, up to 9).
enum class Boss : std::uint8_t { none, champion, unique, superunique, minion };
struct BossInfo {
    Boss kind = Boss::none;
    std::vector<int> mods;                      // MonUMod ids, in the order they were added
};

// Named MonUMod ids.
namespace umod {
inline constexpr int rndname = 1, hpmultiply = 2, light = 3, leveladd = 4, strong = 5, fast = 6, curse = 7, resist = 8,
                     fire = 9, champion = 16, lightning = 17, cold = 18, manahit = 25, teleport = 26, spectralhit = 27,
                     stoneskin = 28, multishot = 29, aura = 30, ghostly = 36, fanatic = 37, possessed = 38, berserk = 39;
}

namespace unique_detail {

// FUN_005a03e0: can monster `t` take mod `u`? Enabled; not an excluded
// MonType; fPick 2 (multishot) not for melee monsters.
// ponytail: fPick 1 / 3 (teleport, fast) test FUN_0046c140, not mapped —
// allowed here; MonType exclusions match the type itself, not its parents.
inline bool can_take(const UMod& u, const MonType& t) {
    if (!u.enabled) return false;
    if (!u.exclude1.empty() && u.exclude1 == t.montype) return false;
    if (!u.exclude2.empty() && u.exclude2 == t.montype) return false;
    if (u.fpick == 2 && t.melee) return false;
    return true;
}

// Weighted pick among the rows that pass (FUN_005a0500 champion weights,
// FUN_005a0600 unique weights not already taken): rand(total), walk.
template <class Weight>
inline int pick(const UMods& m, const MonType& t, Rng& seed, Weight&& weight) {
    std::vector<std::pair<int, int>> c;
    int total = 0;
    for (const auto& u : m.rows)
        if (const int w = weight(u); w > 0 && can_take(u, t)) { c.emplace_back(u.id, w); total += w; }
    int r = seed(total);
    for (const auto& [id, w] : c) { if (r < w) return id; r -= w; }
    return 0;
}

}  // namespace unique_detail

// FUN_005a0760 on a monster just made unique-capable: with `champions`,
// rand(100) < the champion chance makes it a champion with one champion
// mod (cpick); else a unique with 1 + difficulty unique mods (upick, no
// repeats, 8 at most). Rolls the monster's own seed.
inline BossInfo roll_boss(const UMods& m, const MonType& t, int difficulty, bool champions, Rng& seed) {
    using namespace unique_detail;
    const int d = std::clamp(difficulty, 0, 2);
    BossInfo b;
    if (champions && seed(100) < m.k[0]) {
        b.kind = Boss::champion;
        if (const int id = pick(m, t, seed, [&](const UMod& u) { return u.champion ? u.cpick[std::size_t(d)] : 0; })) b.mods.push_back(id);
        return b;
    }
    b.kind = Boss::unique;
    const int n = std::min(seed(1) + 1 + d, 8);
    for (int i = 0; i < n; ++i) {
        const int id = pick(m, t, seed, [&](const UMod& u) {
            return !u.champion && std::ranges::find(b.mods, u.id) == b.mods.end() ? u.upick[std::size_t(d)] : 0;
        });
        if (!id) break;
        b.mods.push_back(id);
    }
    return b;
}

// What the mods do to a monster's stats (FUN_005a2120: the fixed rndname,
// hpmultiply, light, leveladd for uniques, then its own mods; champions
// and minions get theirs through `champion`). `boss` is the unique's /
// champion's own kind; a minion takes the minion rows.
struct BossStats {
    int level_add = 0;                          // leveladd +3; champion -1 on top (net +2)
    int exp_mult = 1;                           // unique x5, champion x3
    int hp_pct = 0;                             // + life %
    int dmg_pct = 0, tohit_pct = 0;             // + damage %, + attack rating %
    int velocity_pct = 0;                       // + speed % (fast; champion +20)
    std::array<int, 6> res_add{};               // + DR%, MR%, fire, light, cold, poison resist (ResDm..ResPo order)
    bool double_defense = false;                // stone skin
    int elem = -1, elem_min_pct = 0, elem_max_pct = 0;   // enchanted: the MonLvl damage % it adds as that element (0 fire, 1 light, 2 cold)
};
// ponytail: resistance adds skip FUN_005a1370's "at most two immunities"
// bookkeeping only as far as the monster's own base resists count (they
// do); the difficulty percentage FUN_00611d30 +0x34 scales champion /
// strong damage and to-hit by is taken as 100.
inline BossStats boss_stats(const UMods& m, const MonType& t, Boss kind, const std::vector<int>& mods, int difficulty) {
    const int d = std::clamp(difficulty, 0, 2);
    BossStats s;
    const bool unique = kind == Boss::unique || kind == Boss::superunique;
    const bool champion = std::ranges::find(mods, umod::champion) != mods.end() || kind == Boss::champion;
    if (unique) {                                                        // FUN_005a0e40 leveladd, FUN_005a0dc0 hpmultiply
        s.level_add += 3;
        s.exp_mult = 5;
        s.hp_pct = m.k[std::size_t(7 + d)];
    } else if (kind == Boss::minion) {
        s.hp_pct = m.k[std::size_t(1 + d)];
    }
    if (champion) {                                                      // FUN_005a0e80
        s.level_add += unique ? -1 : 2;
        s.exp_mult = 3;
        s.hp_pct = m.k[std::size_t(4 + d)];
        s.tohit_pct += m.k[10];
        s.dmg_pct += m.k[11];
        s.velocity_pct += 20;
    }
    auto resist = [&](int id) {                                          // FUN_005a1370
        auto& r = s.res_add;
        auto base = [&](int i) { return t.diff[std::size_t(d)].res[std::size_t(i)] + r[std::size_t(i)]; };
        int immune = 0;
        for (int i = 0; i < 6; ++i) immune += base(i) > 99;
        if (immune >= 2) return;
        auto add = [&](int i, int v, int below) {
            if (base(i) < below) { r[std::size_t(i)] += v; if (base(i) > 99) ++immune; }
        };
        switch (id) {
        case umod::resist:                                               // magic resistant: +40 cold, fire, light
            add(4, 40, 100); if (immune < 2) add(2, 40, 100); if (immune < 2) add(3, 40, 100); break;
        case umod::fire: r[2] += 75; break;
        case umod::cold: r[4] += 75; break;
        case umod::lightning: r[3] += 75; break;
        case 23: r[5] += 75; break;                                      // poison hit
        case 25: r[1] += 20; break;                                      // mana burn: +20 magic resist
        case umod::spectralhit:                                          // +20 cold, fire, light under 75
            add(4, 20, 75); if (immune < 2) add(2, 20, 75); if (immune < 2) add(3, 20, 75); break;
        case umod::stoneskin: s.double_defense = true; r[0] += 50; break;
        default: break;
        }
    };
    for (const int id : mods) {
        switch (id) {
        case umod::strong:                                               // FUN_005a17e0
            s.dmg_pct += m.k[kind == Boss::minion ? 14 : 15];
            s.tohit_pct += m.k[kind == Boss::minion ? 12 : 13];
            break;
        case umod::fast:                                                 // FUN_005a1910: 2048 / Velocity - 128, 10..100
            if (t.velocity > 0) s.velocity_pct += std::clamp(2048 / t.velocity - 128, 10, 100);
            break;
        case umod::fire: case umod::lightning: case umod::cold: {        // FUN_005a1990 & co: MonLvl damage x %
            s.elem = id == umod::fire ? 0 : id == umod::lightning ? 1 : 2;
            const int base = kind == Boss::minion ? 16 : champion ? 22 : 28;
            s.elem_min_pct = m.k[std::size_t(base + d)];
            s.elem_max_pct = m.k[std::size_t(base + 3 + d)];
            resist(id);
            break;
        }
        default: resist(id); break;
        }
    }
    return s;
}

}  // namespace d2d::rules
