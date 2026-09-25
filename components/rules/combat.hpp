// Combat: the player (or merc) as a Fighter built from gear and stats, one
// blow each way (hit chance, block and the defender's rolls, damage,
// resistances, crushing / deadly / critical strike, leech, poison, chill),
// swing speed breakpoints, experience and levels. Traced in game.exe where
// noted: docs/research/re/combat.md.
#pragma once

#include "monsters.hpp"

#include <array>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::rules {

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
    int min = 1, max = 2, ar = 1;                   // physical damage, attack rating (as the panel shows them)
    // Before their percentages, so a skill's % joins the same sum as in
    // game.exe: the weapon damage in 256ths and its % (FUN_0057b420), the
    // attack rating and its % (FUN_0057d9b0).
    int phys_lo = 256, phys_hi = 512, phys_pct = 0;
    int ar_base = 1, ar_pct = 0;
    // Kicks (FUN_00646280): stat 137 + the boots' kick damage, and its % —
    // the boots' StrBonus / DexBonus on strength / dexterity, + 25, + 17.
    int kick_lo = 0, kick_hi = 0, kick_pct = 0;
    int smite_lo = 0, smite_hi = 0, smite_pct = 0;  // the shield's (Smite, FUN_005ce9f0 -> FUN_0057b420 on the shield)
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
                            const ClassGains& g, int defense, const std::array<int, 4>& res,
                            const d2d::d2s::Item* boots = nullptr) {
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
    f.phys_lo = int(lo); f.phys_hi = int(hi); f.phys_pct = int(pct);
    f.min = int(std::max<std::int64_t>((lo + lo * pct / 100) >> 8, 1));
    f.max = int(std::max<std::int64_t>((hi + hi * pct / 100) >> 8, f.min));
    f.ar_base = int(dex * 5 - 35 + g.to_hit + S(19) + S(224) * clvl / 8);
    f.ar_pct = int(S(119));
    f.ar = int(std::max<std::int64_t>(std::int64_t(f.ar_base) * (100 + f.ar_pct) / 100, 1));
    f.kick_lo = f.kick_hi = int(S(137));
    if (boots) if (const auto b = t.item_base.find(boots->code); b != t.item_base.end()) {
        f.kick_lo += b->second.mindam;
        f.kick_hi = std::max(f.kick_hi + b->second.maxdam, f.kick_lo);
        f.kick_pct = int(std::max<std::int64_t>(str * b->second.str_bonus / 100 + dex * b->second.dex_bonus / 100 + S(25), -90)
                         + S(17) - W(17));
    }
    if (shield) if (const auto b = t.item_base.find(shield->code); b != t.item_base.end()) {
        f.smite_lo = b->second.mindam;
        f.smite_hi = std::max(b->second.maxdam, f.smite_lo);
        f.smite_pct = int(std::max<std::int64_t>(str * b->second.str_bonus / 100 + dex * b->second.dex_bonus / 100 + S(25), -90)
                          + S(17) - W(17));
    }
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

// A fighter with just damage, attack rating and defense (the merc, its
// arrows, tests).
inline Fighter simple_fighter(int min, int max, int ar, int defense = 0) {
    Fighter f;
    f.min = min; f.max = std::max(max, min); f.ar = f.ar_base = ar; f.defense = defense;
    f.phys_lo = min << 8; f.phys_hi = f.max << 8;
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
    int phys = 0;                        // the physical rolled, before resistance (Dragon Tail's fire is a share of it)
    int stun_ticks = 0;
};
inline int resisted(int dmg, int res) { return res >= 100 ? 0 : dmg * (100 - res) / 100; }

// What a skill adds to a blow (docs/research/re/skills.md, "Melee skills"):
// its attack-rating bonus % (toht) and enhanced damage % (calc1) joining
// the gear's percentages, damage added after the build (calc2), the share
// of weapon damage (SrcDam, 128ths), knockback; a kick's damage comes from
// the boots and the skill's own physical damage (in 256ths) instead of the
// weapon, and isn't doubled by critical / deadly strike (FUN_005d54b0).
struct Swing {
    int ar_pct = 0, ed_pct = 0, flat = 0, srcdam = 128;
    bool kick = false, knockback = false;
    int skill_lo = 0, skill_hi = 0;
    int stun_ticks = 0;                   // EType stun: the target stands this long (FUN_0057aae0)
    int conv_type = -1, conv_pct = 0;     // calc4 % of the physical becomes this element (Skill::etype order)
    // Elemental damage as % of the physical rolled (Vengeance, FUN_005cfe10:
    // FUN_0057b420's physical x calc1 fire, calc2 cold, calc3 lightning).
    int fire_pct = 0, cold_pct = 0, ltng_pct = 0, cold_len = 0;
    // Smite (FUN_005ce9f0): the shield's damage, sure to hit, and the record
    // marked built (flags 2), so FUN_0057dbf0 skips FUN_0057b7d0: no crit,
    // gear elements, leech, crushing blow or open wounds.
    bool smite = false;
};

// The player's melee hit on `t` (hit chance, then the monster's block):
// critical or deadly strike doubles the physical damage; physical resistance cuts it;
// leech is the physical damage dealt x steal % x Drain %; crushing blow
// takes a quarter of its current life (less physical resistance).
// ponytail: crushing blow's boss / difficulty divisors aren't applied.
inline Blow player_blow(const Fighter& f, const Target& t, int clvl, Rng& rng, const Swing& sw = {}) {
    Blow b;
    const int ar = std::max(int(std::int64_t(f.ar_base) * (100 + f.ar_pct + sw.ar_pct) / 100), 1);
    if (!sw.smite && rng(100) >= hit_chance(ar, t.ac, clvl, t.level)) return b;
    if (t.block > 0 && rng(100) < t.block) { b.blocked = true; return b; }
    b.hit = true;
    std::int64_t lo, hi;                                  // 256ths
    if (sw.smite) {
        const std::int64_t sp = std::max<std::int64_t>(f.smite_pct + sw.ed_pct, -90);
        lo = ((std::int64_t(f.smite_lo) << 8) + sw.skill_lo) * (100 + sp) / 100;
        hi = ((std::int64_t(f.smite_hi) << 8) + sw.skill_hi) * (100 + sp) / 100;
    } else if (sw.kick) {
        const std::int64_t kp = f.kick_pct + sw.ed_pct;
        lo = sw.skill_lo + std::int64_t(sw.skill_lo) * sw.ed_pct / 100 + (std::int64_t(f.kick_lo) << 8) * (100 + kp) / 100;
        hi = sw.skill_hi + std::int64_t(sw.skill_hi) * sw.ed_pct / 100 + (std::int64_t(f.kick_hi) << 8) * (100 + kp) / 100;
    } else {
        const std::int64_t p = std::max<std::int64_t>(f.phys_pct + sw.ed_pct, -90);
        lo = f.phys_lo + f.phys_lo * p / 100;
        hi = f.phys_hi + f.phys_hi * p / 100;
    }
    std::int64_t d = hi > lo ? lo + rng(int(hi - lo)) : lo;
    const int rolled = int(d >> 8);                       // before crit: what Vengeance's elements are a share of
    // Critical strike and deadly strike are separate rolls; either doubles
    // (FUN_0057b7d0; the mastery crit joins them with skills). Not kicks.
    if (!sw.kick && !sw.smite && ((f.critical > 0 && rng(100) < f.critical) || (f.deadly > 0 && rng(100) < f.deadly))) { d *= 2; b.deadly = true; }
    d = d * sw.srcdam / 128;
    // Conversion, last in the build (FUN_0057b7d0, record +0x65 / +0x68):
    // pct % of the physical moves to the element, calc2's add comes after.
    // ponytail: poison conversion (an eighth, 50-tick length) isn't done.
    const std::int64_t conv = sw.conv_type >= 0 && sw.conv_type < 5 ? d * std::clamp(sw.conv_pct, 0, 100) / 100 : 0;
    d -= conv;
    int phys = int(std::max<std::int64_t>(d >> 8, sw.kick || conv ? 0 : 1)) + sw.flat;
    b.phys = phys;
    phys = resisted(phys, t.res[0]);
    if (sw.smite) {
        b.damage = phys;
        b.knockback = sw.knockback;
        b.stun_ticks = std::min(sw.stun_ticks, 250);
        return b;
    }
    b.life = phys * f.life_steal * t.drain / 10000;
    b.mana = phys * f.mana_steal * t.drain / 10000;
    static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };             // element -> Target::res index
    int elem = conv > 0 && sw.conv_type != 3 ? resisted(int(conv >> 8), t.res[std::size_t(kRes[sw.conv_type])]) : 0;
    elem += resisted(rolled * sw.fire_pct / 100, t.res[2]) + resisted(rolled * sw.ltng_pct / 100, t.res[3]);
    if (const int c = resisted(rolled * sw.cold_pct / 100, t.res[4]); c > 0) { elem += c; b.chill_ticks = std::max(sw.cold_len, 1); }
    for (int e = 0; e < 5; ++e) {
        const auto [lo, hi] = f.elem[std::size_t(e)];
        if (hi <= 0) continue;
        const int d = resisted(rng.range(lo, hi), t.res[std::size_t(kRes[e])]);
        if (e == 3) { b.poison = d; b.poison_ticks = std::max(f.poison_len, 1); continue; }
        if (e == 2 && d > 0) b.chill_ticks = std::max(b.chill_ticks, f.cold_len);
        elem += d;
    }
    int cb = 0;
    if (f.crushing > 0 && rng(100) < f.crushing) { b.crushing = true; cb = resisted(t.hp / 4, t.res[0]); }
    b.bleed = f.open_wounds > 0 && rng(100) < f.open_wounds;
    b.knockback = f.knockback || sw.knockback;
    b.stun_ticks = std::min(sw.stun_ticks, 250);
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

}  // namespace d2d::rules
