// Skills: Skills.txt rows, the calc language they're written in (compiled
// here to a small stack program, run like game.exe's FUN_006c0bc0), the
// per-level values game.exe derives from a row (level brackets, elemental
// damage and length, to-hit, mana cost) and a character's level in each
// skill (points + item bonuses). docs/research/re/skills.md.
#pragma once

#include "combat.hpp"
#include "rules.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace d2d::rules {

// ---- the calc language

// A compiled calc: postfix instructions for a stack machine. Operand codes
// are skillcalc.txt rows (FUN_00646460's switch: 0 ln12 .. 72 skpt).
struct Calc {
    enum Op : std::uint8_t { Const, Operand, SkillRef, StatRef, Min, Max, Rand,
                             Add, Sub, Mul, Div, Lt, Gt, Le, Ge, Eq, Ne, Neg, Cond };
    struct Ins { Op op; int a = 0, b = 0; };
    std::vector<Ins> code;
    [[nodiscard]] bool empty() const { return code.empty(); }
};

// What names mean when compiling: skillcalc.txt's operand names in order,
// skill names (Skills.txt `skill`) and stat names (ItemStatCost `Stat`).
struct CalcNames {
    std::vector<std::string> operands;
    std::unordered_map<std::string, int> skills, stats;
};

// Compiles one Skills.txt expression: numbers, operand names, + - * /,
// < > <= >= == !=, `c ? a : b`, parentheses, unary minus, min(a, b),
// max(a, b), rand(a, b), skill('Name'.operand), stat('name'.accr).
// Double-quoted cells are unwrapped; a "(" still open at the end is
// closed. Something it can't read (Bone Wall's calc2 is "par34") gives an empty
// calc, which evaluates to 0, and `error` says why.
// ponytail: game.exe's own compiler isn't traced; the grammar is what
// Skills.txt uses, with C precedence.
inline Calc compile_calc(std::string_view src, const CalcNames& names, std::string* error = nullptr) {
    Calc out;
    // Cells holding a comma come double-quoted from the .txt.
    if (src.size() >= 2 && src.front() == '"' && src.back() == '"') src = src.substr(1, src.size() - 2);
    std::size_t i = 0;
    bool bad = false;
    auto fail = [&](std::string why) { if (!bad && error) *error = std::move(why); bad = true; };
    auto ws = [&] { while (i < src.size() && std::isspace((unsigned char)src[i])) ++i; };
    auto eat = [&](std::string_view s) {
        ws();
        if (src.substr(i, s.size()) != s) return false;
        i += s.size();
        return true;
    };
    auto ident = [&] {
        ws();
        std::size_t j = i;
        while (j < src.size() && (std::isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
        std::string s(src.substr(i, j - i));
        i = j;
        return s;
    };
    auto quoted = [&] {
        ws();
        if (i >= src.size() || src[i] != '\'') { fail("expected a quoted name"); return std::string{}; }
        const auto e = src.find('\'', i + 1);
        if (e == std::string_view::npos) { fail("unterminated name"); return std::string{}; }
        std::string s(src.substr(i + 1, e - i - 1));
        i = e + 1;
        return s;
    };
    auto operand = [&](const std::string& n) {
        for (std::size_t k = 0; k < names.operands.size(); ++k) if (names.operands[k] == n) return int(k);
        return -1;
    };
    std::function<void()> ternary;
    auto emit = [&](Calc::Op op, int a = 0, int b = 0) { out.code.push_back({ op, a, b }); };
    std::function<void()> unary;
    auto primary = [&] {
        ws();
        if (i >= src.size()) { fail("unexpected end"); return; }
        // A ( left open at the very end counts as closed (Fire Wall's
        // EDmgSymPerCalc is missing its last ")"; the game reads it).
        if (eat("(")) { ternary(); ws(); if (!eat(")") && i < src.size()) fail("expected )"); return; }
        if (std::isdigit((unsigned char)src[i])) {
            int v = 0;
            while (i < src.size() && std::isdigit((unsigned char)src[i])) v = v * 10 + (src[i++] - '0');
            emit(Calc::Const, v);
            return;
        }
        const auto n = ident();
        if (n.empty()) { fail(std::string("unexpected '") + src[i] + "'"); ++i; return; }
        if (n == "min" || n == "max" || n == "rand") {
            if (!eat("(")) { fail("expected ( after " + n); return; }
            ternary();
            if (!eat(",")) { fail("expected , in " + n); return; }
            ternary();
            if (!eat(")")) { fail("expected ) in " + n); return; }
            emit(n == "min" ? Calc::Min : n == "max" ? Calc::Max : Calc::Rand);
            return;
        }
        if (n == "skill" || n == "stat") {
            if (!eat("(")) { fail("expected ( after " + n); return; }
            const auto what = quoted();
            if (!eat(".")) { fail("expected . in " + n + "()"); return; }
            const auto field = ident();
            if (!eat(")")) { fail("expected ) in " + n + "()"); return; }
            if (n == "skill") {
                const auto s = names.skills.find(what);
                const int code = operand(field);
                if (s == names.skills.end()) { fail("unknown skill '" + what + "'"); return; }
                if (code < 0) { fail("unknown operand '" + field + "'"); return; }
                emit(Calc::SkillRef, s->second, code);
            } else {
                const auto s = names.stats.find(what);
                if (s == names.stats.end()) { fail("unknown stat '" + what + "'"); return; }
                emit(Calc::StatRef, s->second);
            }
            return;
        }
        if (const int code = operand(n); code >= 0) { emit(Calc::Operand, code); return; }
        fail("unknown name '" + n + "'");
    };
    unary = [&] {
        if (eat("-")) { unary(); emit(Calc::Neg); return; }
        primary();
    };
    auto mul = [&] {
        unary();
        for (;;) {
            if (eat("*")) { unary(); emit(Calc::Mul); }
            else if (eat("/")) { unary(); emit(Calc::Div); }
            else return;
        }
    };
    auto add = [&] {
        mul();
        for (;;) {
            if (eat("+")) { mul(); emit(Calc::Add); }
            else if (eat("-")) { mul(); emit(Calc::Sub); }
            else return;
        }
    };
    auto cmp = [&] {
        add();
        for (;;) {
            if (eat("<=")) { add(); emit(Calc::Le); }
            else if (eat(">=")) { add(); emit(Calc::Ge); }
            else if (eat("==")) { add(); emit(Calc::Eq); }
            else if (eat("!=")) { add(); emit(Calc::Ne); }
            else if (eat("<")) { add(); emit(Calc::Lt); }
            else if (eat(">")) { add(); emit(Calc::Gt); }
            else return;
        }
    };
    ternary = [&] {
        cmp();
        if (eat("?")) {
            ternary();
            if (!eat(":")) { fail("expected : after ?"); return; }
            ternary();
            emit(Calc::Cond);
        }
    };
    ternary();
    ws();
    if (i < src.size()) fail("trailing '" + std::string(src.substr(i)) + "'");
    if (bad) out.code.clear();
    return out;
}

// ---- skill rows

// A Skills.txt row: the columns game.exe's skill records hold (0x23c-byte
// records at [0x744304]+0xb98; offsets in docs/research/re/skills.md).
struct Skill {
    int id = -1;
    std::string name, cls, desc;           // skill, charclass ("ama".. or ""), skilldesc
    int srvstfunc = 0, srvdofunc = 0;
    std::string anim, range;               // anim (A1, SC, KK, ...), range (h2h, rng, both, none)
    bool leftskill = false, passive = false, aura = false, use_attack_rate = false, in_town = false;
    bool attack_no_mana = false;
    int reqlevel = 1, maxlvl = 20;
    int mana = 0, lvlmana = 0, manashift = 8, minmana = 0;
    int tohit = 0, levtohit = 0;
    Calc tohit_calc;
    std::array<Calc, 4> calc;              // calc1..4
    std::array<int, 8> par{};              // Param1..8
    int hitshift = 8, srcdam = 128;
    int srcdam_raw = 0;                    // SrcDam as written: a missile's weapon share (FUN_0064b860), 0 none
    std::string srvmissile;                // +0x46: the Missiles.txt row the skill fires
    std::string srvmissilea;               // +0x48: the row its srvdofunc fires (FUN_005d3cf0)
    int perdelay = 0;                      // an aura's pulse, ticks
    std::string summon, pettype;           // +0xbc the MonStats row it raises, +0xbe its pet group
    Calc petmax;                           // +0xc0: how many of the group at once
    bool target_corpse = false;            // TargetCorpse: raised from a corpse
    int result_flags = 0;                  // ResultFlags (8: knockback)
    int etype = -1;                        // 0 fire, 1 lightning, 2 cold, 3 poison, 4 magic, 5 stun
    int emin = 0, emax = 0;
    std::array<int, 5> emin_lev{}, emax_lev{};
    int elen = 0;
    std::array<int, 3> elen_lev{};
    Calc edmg_sym, elen_sym;               // EDmgSymPerCalc, ELenSymPerCalc: percent bonuses
    int mindam = 0, maxdam = 0;
    std::array<int, 5> mindam_lev{}, maxdam_lev{};
    Calc dmg_sym;                          // DmgSymPerCalc
    std::array<int, 5> passive_stat{ -1, -1, -1, -1, -1 };   // +0x98
    std::array<Calc, 5> passive_calc;      // +0xa4
    std::string passive_itype;             // +0x96: the stats' layer, the weapon type they need ("" any)
    std::array<Calc, 6> aura_calc;         // aurastatcalc1..6
    std::array<int, 6> aurastat{ -1, -1, -1, -1, -1, -1 };   // aurastat1..6 (ItemStatCost ids)
    Calc auralen, aurarange;               // auralencalc (+0x60, ticks), aurarangecalc (+0x64, subtiles)
    int prgdam = 0;
    int seqnum = 0;                        // +0x13: anim SQ's sequence (sequences.hpp)                        // +0x44: what a charge-up's charges add to the releasing hit
    std::array<int, 3> prgfunc{};          // srvprgfunc1..3 (+0x30): srvdofunc slots run on release
    std::array<Calc, 3> prgcalc;           // prgcalc1..3 (+0x38..): by the charges held (FUN_005d3da0)
    bool prgstack = false;                 // a release runs srvprgfunc 1..n, not just n (FUN_005d5220)
    std::string srvmissileb, srvmissilec;  // +0x4a / +0x4c: 2 / 3 charges' missile (FUN_005d3cf0)
    int page = 0;                          // SkillDesc SkillPage: 1..3 its class's tabs
    int icon = 0;                          // SkillDesc IconCel
    std::string str_name;                  // SkillDesc "str name"
};

struct SkillTables {
    std::vector<Skill> rows;               // by Id
    std::unordered_map<std::string, int> by_name;
    CalcNames names;
    // Each class's skill ids in Skills.txt order: how a save's 30 skill
    // bytes (Stats::skills) are indexed.
    std::array<std::vector<int>, 7> class_ids;
    [[nodiscard]] const Skill* get(int id) const {
        return id >= 0 && std::size_t(id) < rows.size() && rows[std::size_t(id)].id == id ? &rows[std::size_t(id)] : nullptr;
    }
};

// ---- per-level values

// The *Lev1..5 brackets (FUN_00644b70): Lev1 for each level 2..8, Lev2
// 9..16, Lev3 17..22, Lev4 23..28, Lev5 from 29.
inline int level_bonus(const std::array<int, 5>& lev, int lvl) {
    if (lvl < 2) return 0;
    if (lvl > 28) return (lvl - 28) * lev[4] + (lev[3] + lev[2]) * 6 + lev[1] * 8 + lev[0] * 7;
    if (lvl > 22) return (lvl - 22) * lev[3] + lev[2] * 6 + lev[1] * 8 + lev[0] * 7;
    if (lvl > 16) return (lvl - 16) * lev[2] + lev[1] * 8 + lev[0] * 7;
    if (lvl > 8) return (lvl - 8) * lev[1] + lev[0] * 7;
    return (lvl - 1) * lev[0];
}
// ln (FUN_004e6ca0): a + b x (lvl - 1). dm (FUN_00645b20): a + (b - a) x
// (110 x lvl / (lvl + 6)) / 100, at most b.
inline int calc_ln(int a, int b, int lvl) { return lvl > 0 ? a + b * (lvl - 1) : 0; }
inline int calc_dm(int a, int b, int lvl) {
    if (lvl < 1) return 0;
    return std::min((110 * lvl / (lvl + 6)) * (b - a) / 100 + a, b);
}
// Mana cost in 256ths (FUN_0056c160): (mana + lvlmana x (lvl - 1)) << manashift,
// at least minmana points.
inline int mana_cost(const Skill& s, int lvl) {
    if (lvl < 1) return 0;
    return std::max((s.mana + s.lvlmana * (lvl - 1)) << (s.manashift & 31), s.minmana * 256);
}

// What calcs may ask of the unit using the skill.
struct CalcEnv {
    std::function<int(int skill)> base_level;   // blvl: points in the skill
    std::function<int(int skill)> level;        // lvl: with item bonuses
    std::function<int(int stat)> stat;          // stat('x'.accr)
    int clvl = 1;                               // ulvl
    Rng* rng = nullptr;                         // rand()
};

inline int eval_calc(const SkillTables& t, const Calc& c, const CalcEnv& env, int skill, int lvl, int depth = 0);

// Elemental damage in 256ths (FUN_00644d50 / FUN_00644e40): (EMin + brackets)
// << HitShift, plus EDmgSymPerCalc percent of that; with `mastery` (the
// flag), plus the element's mastery % of the sum (FUN_00644c90: fire 329,
// lightning 330, cold 331, poison 332; env.stat).
inline int elem_damage(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl, bool max, int depth = 0,
                       bool mastery = false) {
    if (lvl < 1) return 0;
    int d = ((max ? s.emax : s.emin) + level_bonus(max ? s.emax_lev : s.emin_lev, lvl)) << (s.hitshift & 31);
    if (!s.edmg_sym.empty()) d += d * eval_calc(t, s.edmg_sym, env, s.id, lvl, depth + 1) / 100;
    if (mastery && env.stat && s.etype >= 0 && s.etype <= 3) d += int(std::int64_t(d) * env.stat(329 + s.etype) / 100);
    return d;
}
// Elemental length in ticks (FUN_00644f20): ELen + ELevLen1..3 over levels
// 2..8, 9..16, 17 up, plus ELenSymPerCalc percent.
inline int length_bonus(const std::array<int, 3>& l, int lvl) {
    return lvl < 2 ? 0 : lvl < 9 ? (lvl - 1) * l[0] : lvl < 17 ? (lvl - 8) * l[1] + l[0] * 7
                      : (lvl - 16) * l[2] + l[1] * 8 + l[0] * 7;
}
inline int elem_length(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl, int depth = 0) {
    if (lvl < 1) return 0;
    int n = s.elen + length_bonus(s.elen_lev, lvl);
    if (!s.elen_sym.empty()) n += n * eval_calc(t, s.elen_sym, env, s.id, lvl, depth + 1) / 100;
    return n;
}
// The skill's own physical damage in 256ths (FUN_00647bc0 without the
// weapon share): (MinDam + brackets) plus DmgSymPerCalc percent, << HitShift.
inline int skill_phys(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl, bool max, int depth = 0) {
    if (lvl < 1) return 0;
    int d = (max ? s.maxdam : s.mindam) + level_bonus(max ? s.maxdam_lev : s.mindam_lev, lvl);
    if (!s.dmg_sym.empty()) d += d * eval_calc(t, s.dmg_sym, env, s.id, lvl, depth + 1) / 100;
    return d << (s.hitshift & 31);
}

// What a skill's missile carries (FUN_0064b860, the missile's Skill set):
// combat.hpp's MissileDamage.
// A row with no Skill carries its own element at the missile's level
// (FUN_0064b100 / 0064b1d0 / 0064b2a0): (EMin + MinELev brackets) <<
// HitShift, the same for the max, ELen + ELevLen brackets.
inline MissileDamage row_damage(int etype, int emin, int emax, const std::array<int, 5>& emin_lev,
                                const std::array<int, 5>& emax_lev, int hitshift, int elen,
                                const std::array<int, 3>& elen_lev, int lvl) {
    MissileDamage m;
    if (etype < 0 || lvl < 1) return m;
    m.etype = etype;
    m.elo = (emin + level_bonus(emin_lev, lvl)) << (hitshift & 31);
    m.ehi = std::max((emax + level_bonus(emax_lev, lvl)) << (hitshift & 31), m.elo);
    m.elen = elen + length_bonus(elen_lev, lvl);
    return m;
}
inline MissileDamage missile_damage(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl) {
    MissileDamage m;
    m.phys_lo = skill_phys(t, s, env, lvl, false);
    m.phys_hi = std::max(skill_phys(t, s, env, lvl, true), m.phys_lo);
    m.etype = s.etype;
    if (s.etype >= 0) {
        m.elo = elem_damage(t, s, env, lvl, false, 0, true);
        m.ehi = std::max(elem_damage(t, s, env, lvl, true, 0, true), m.elo);
        m.elen = elem_length(t, s, env, lvl);
    }
    m.srcdam = s.srcdam_raw;
    return m;
}
// Attack rating bonus % (FUN_006449f0): ToHitCalc, else ToHit + LevToHit x (lvl - 1).
inline int skill_tohit(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl, int depth = 0) {
    if (lvl < 1) return 0;
    if (!s.tohit_calc.empty()) return eval_calc(t, s.tohit_calc, env, s.id, lvl, depth + 1);
    return s.tohit + s.levtohit * (lvl - 1);
}

// One operand (FUN_00646460, codes in skillcalc.txt order).
// ponytail: the missile operands (m1en.., 26..37, 43..48), len, rng,
// pets, skpt read 0 until their phase.
inline int calc_operand(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl, int code, int depth) {
    const auto& p = s.par;
    switch (code) {
        case 0: return calc_ln(p[0], p[1], lvl);  case 1: return calc_dm(p[0], p[1], lvl);
        case 2: return calc_ln(p[2], p[3], lvl);  case 3: return calc_dm(p[2], p[3], lvl);
        case 4: return calc_ln(p[4], p[5], lvl);  case 5: return calc_dm(p[4], p[5], lvl);
        case 6: return calc_ln(p[6], p[7], lvl);  case 7: return calc_dm(p[6], p[7], lvl);
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: return p[std::size_t(code - 8)];
        case 16: return lvl;
        case 17: return elem_damage(t, s, env, lvl, false, depth) >> 8;          // edmn
        case 18: return elem_damage(t, s, env, lvl, true, depth) >> 8;           // edmx
        case 19: return elem_length(t, s, env, lvl, depth);                      // edln
        case 20: return skill_tohit(t, s, env, lvl, depth);                      // toht
        case 21: return mana_cost(s, lvl) >> 8;                                  // mana
        case 22: return lvl < 1 ? 0 : (((s.mana + s.lvlmana * (lvl - 1)) * 25 / 2) << (s.manashift & 31)) >> 8;   // mps
        case 23: case 24: case 25: {                                             // math / madm / macr
            for (std::size_t k = 0; k < 5; ++k) {
                const int st = s.passive_stat[k], want = 0x156 + (code - 23);
                if (st == want || st == want + 3) return eval_calc(t, s.passive_calc[k], env, s.id, lvl, depth + 1);
            }
            return 0;
        }
        case 49: return elem_damage(t, s, env, lvl, false, depth, true) >> 8;    // enma (FUN_00644d50 flag 1)
        case 50: return elem_damage(t, s, env, lvl, true, depth, true) >> 8;     // exma
        case 51: return elem_length(t, s, env, lvl, depth);                      // edma (FUN_00644f20 flag 1)
        case 52: return elem_damage(t, s, env, lvl, false, depth, true);         // enms
        case 53: return elem_damage(t, s, env, lvl, true, depth, true);          // exms
        case 38: return elem_damage(t, s, env, lvl, false, depth);               // edns
        case 39: return elem_damage(t, s, env, lvl, true, depth);                // edxs
        case 40: return env.clvl;                                                // ulvl
        case 41: return env.base_level ? env.base_level(s.id) : lvl;             // blvl
        case 42: return lvl < 1 ? 0 : (s.mana + s.lvlmana * (lvl - 1)) << (s.manashift & 31);   // usmc
        case 55: case 56: case 57: case 58: return eval_calc(t, s.calc[std::size_t(code - 55)], env, s.id, lvl, depth + 1);
        case 60: case 61: case 62: case 63: case 64: case 65:
            return eval_calc(t, s.aura_calc[std::size_t(code - 60)], env, s.id, lvl, depth + 1);
        case 66: case 67: case 68: case 69: case 70:
            return eval_calc(t, s.passive_calc[std::size_t(code - 66)], env, s.id, lvl, depth + 1);
        default: return 0;
    }
}

// ---- passives

// A passive skill's stat on the player (FUN_00646d60): passivestat k =
// passivecalc k at the skill's level with item bonuses, in the skill's
// passivestate, on the layer passiveitype. Refreshed when the level
// changes (FUN_00646f20 walks every skill that has a passivestate).
struct PassiveStat { int stat = -1, value = 0; std::string itype; };

// Every skill with passive stats the player has a level in (env.level >
// 0), its stats up to the first empty passivestat: the passives, and the
// Paladin's auras — theirs sit in the passivestate while the aura is off
// (the state at +0x80 holds FUN_00646d60 off) and in the aura's own state
// while it's on (FUN_005cf3a0 fills it from +0x98), so they're always on.
inline std::vector<PassiveStat> passive_stats(const SkillTables& t, const CalcEnv& env) {
    std::vector<PassiveStat> out;
    for (const auto& s : t.rows) {
        if (s.id < 0 || s.passive_stat[0] < 0 || !env.level) continue;
        const int lvl = env.level(s.id);
        if (lvl < 1) continue;
        for (std::size_t k = 0; k < 5 && s.passive_stat[k] >= 0; ++k)
            out.push_back({ s.passive_stat[k], eval_calc(t, s.passive_calc[k], env, s.id, lvl, 0), s.passive_itype });
    }
    return out;
}

// Runs a calc for `skill` at `lvl` (the stack machine; divide by zero
// gives 0, as FUN_006c0bc0's op 0x13).
inline int eval_calc(const SkillTables& t, const Calc& c, const CalcEnv& env, int skill, int lvl, int depth) {
    const Skill* s = t.get(skill);
    if (c.empty() || !s || depth > 8) return 0;
    std::vector<int> st;
    st.reserve(c.code.size());
    auto pop = [&] { if (st.empty()) return 0; const int v = st.back(); st.pop_back(); return v; };
    for (const auto& in : c.code) {
        switch (in.op) {
            case Calc::Const: st.push_back(in.a); break;
            case Calc::Operand: st.push_back(calc_operand(t, *s, env, lvl, in.a, depth)); break;
            case Calc::SkillRef: {                     // that skill's operand at the unit's level in it
                const Skill* o = t.get(in.a);
                const int l = in.b == 41 ? (env.base_level ? env.base_level(in.a) : 0) : env.level ? env.level(in.a) : 0;
                st.push_back(o && in.b != 41 ? calc_operand(t, *o, env, l, in.b, depth + 1) : l);
                break;
            }
            case Calc::StatRef: st.push_back(env.stat ? env.stat(in.a) : 0); break;
            case Calc::Neg: st.push_back(-pop()); break;
            case Calc::Cond: { const int b = pop(), a = pop(), k = pop(); st.push_back(k ? a : b); break; }
            default: {
                const int b = pop(), a = pop();
                switch (in.op) {
                    case Calc::Min: st.push_back(std::min(a, b)); break;
                    case Calc::Max: st.push_back(std::max(a, b)); break;
                    case Calc::Rand: st.push_back(env.rng ? env.rng->range(a, b) : a); break;
                    case Calc::Add: st.push_back(a + b); break;
                    case Calc::Sub: st.push_back(a - b); break;
                    case Calc::Mul: st.push_back(a * b); break;
                    case Calc::Div: st.push_back(b ? a / b : 0); break;
                    case Calc::Lt: st.push_back(a < b); break;
                    case Calc::Gt: st.push_back(a > b); break;
                    case Calc::Le: st.push_back(a <= b); break;
                    case Calc::Ge: st.push_back(a >= b); break;
                    case Calc::Eq: st.push_back(a == b); break;
                    case Calc::Ne: st.push_back(a != b); break;
                    default: st.push_back(0); break;
                }
            }
        }
    }
    return st.empty() ? 0 : st.back();
}

// ---- Whirlwind

// Frames between Whirlwind's hits (FUN_005d9320) by the weapon's attack
// length in frames (FUN_0062a710): under 12 → 4, 15 → 6, 18 → 8, 20 → 10,
// 23 → 12, then 14, and 16 past 25; 10 bare-handed.
inline int whirlwind_gap(int attack_frames) {
    const int f = attack_frames;
    return f < 12 ? 4 : f < 15 ? 6 : f < 18 ? 8 : f < 20 ? 10 : f < 23 ? 12 : f > 25 ? 16 : 14;
}

// ---- charge-ups (Assassin martial arts)

// What `n` charges of charge-up `s` (at level `lvl`) add to the hit that
// releases them (FUN_005d3ba0, by prgdam):
// 1 (Tiger Strike, FUN_005d3680): calc1 x n enhanced damage;
// 2 (Cobra Strike, FUN_005d3790): ln12 % steal: life at 1 charge, life and
//   mana at 2, both doubled at 3;
// 4 (Fists of Fire, Claws of Thunder, Blades of Ice, FUN_005d3970): the
//   skill's elemental damage (FUN_0056e0c0).
// ponytail: FUN_004e6ca0 (Cobra's steal) is read as ln12 from its args
// not being shown; prgdam 4's third-charge freeze (cold length / an
// untraced divisor) and its calc1 physical-to-element share aren't applied.
struct ChargeBonus { int ed_pct = 0, life_steal = 0, mana_steal = 0, etype = -1, elem_lo = 0, elem_hi = 0, elem_len = 0; };
inline ChargeBonus charge_bonus(const SkillTables& t, const Skill& s, const CalcEnv& env, int lvl, int n) {
    ChargeBonus b;
    if (lvl < 1 || n < 1) return b;
    n = std::min(n, 3);
    switch (s.prgdam) {
        case 1: b.ed_pct = eval_calc(t, s.calc[0], env, s.id, lvl) * n; break;
        case 2: {
            const int steal = calc_ln(s.par[0], s.par[1], lvl) * (n == 3 ? 2 : 1);
            b.life_steal = steal;
            b.mana_steal = n >= 2 ? steal : 0;
            break;
        }
        case 4:
            b.etype = s.etype;
            b.elem_lo = elem_damage(t, s, env, lvl, false) >> 8;
            b.elem_hi = elem_damage(t, s, env, lvl, true) >> 8;
            b.elem_len = elem_length(t, s, env, lvl);
            break;
        default: break;
    }
    return b;
}

// ---- a character's skill levels

// Item bonuses to one skill, over the item properties the character wears
// (ItemStatCost ids): 127 +all skills, 83 +class skills (param = class),
// 188 +skill tab (param = class x 8 + tab, tab = SkillDesc page - 1),
// 107 +single class skill and 97 +skill as "oskill" (param = skill id).
inline int item_skill_bonus(const Skill& s, int cls, std::span<const d2d::d2s::ItemProp> props) {
    int b = 0;
    const bool own = !s.cls.empty() && s.cls == kClassCode[std::size_t(cls)];
    for (const auto& p : props) {
        switch (p.stat) {
            case 127: if (own) b += p.value; break;
            case 83: if (own && p.param == cls) b += p.value; break;
            case 188: if (own && s.page > 0 && p.param == cls * 8 + s.page - 1) b += p.value; break;
            case 107: case 97: if (p.param == s.id) b += p.value; break;
            default: break;
        }
    }
    return b;
}

}  // namespace d2d::rules
