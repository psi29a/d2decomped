// Skills: Skills.txt rows, the calc language they're written in (compiled
// here to a small stack program, run like game.exe's FUN_006c0bc0), the
// per-level values game.exe derives from a row (level brackets, elemental
// damage and length, to-hit, mana cost) and a character's level in each
// skill (points + item bonuses). docs/research/re/skills.md.
#pragma once

#include "combat.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

// ---- the calc language

// A compiled calc: postfix instructions for a stack machine. Operand codes
// are skillcalc.txt rows (FUN_00646460's switch: 0 ln12 .. 72 skpt).
struct Calc {
    enum Op : std::uint8_t { Const, Operand, SkillRef, StatRef, Min, Max, Rand,
                             Add, Sub, Mul, Div, Lt, Gt, Le, Ge, Eq, Ne, Neg, Cond };
    struct Ins { Op opcode; int operand1 = 0, operand2 = 0; };
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
    std::size_t offset = 0;
    bool bad = false;
    auto fail = [&](std::string why) { if (!bad && error) *error = std::move(why); bad = true; };
    auto skip_space = [&] { while (offset < src.size() && std::isspace((unsigned char)src[offset])) ++offset; };
    auto eat = [&](std::string_view token) {
        skip_space();
        if (src.substr(offset, token.size()) != token) return false;
        offset += token.size();
        return true;
    };
    auto ident = [&] {
        skip_space();
        std::size_t end = offset;
        while (end < src.size() && (std::isalnum((unsigned char)src[end]) || src[end] == '_')) ++end;
        std::string name(src.substr(offset, end - offset));
        offset = end;
        return name;
    };
    auto quoted = [&] {
        skip_space();
        if (offset >= src.size() || src[offset] != '\'') { fail("expected a quoted name"); return std::string{}; }
        const auto end_quote = src.find('\'', offset + 1);
        if (end_quote == std::string_view::npos) { fail("unterminated name"); return std::string{}; }
        std::string name(src.substr(offset + 1, end_quote - offset - 1));
        offset = end_quote + 1;
        return name;
    };
    auto operand = [&](const std::string& name) {
        for (std::size_t k = 0; k < names.operands.size(); ++k) if (names.operands[k] == name) return int(k);
        return -1;
    };
    std::function<void()> ternary;
    auto emit = [&](Calc::Op opcode, int operand1 = 0, int operand2 = 0) { out.code.push_back({ opcode, operand1, operand2 }); };
    std::function<void()> unary;
    auto primary = [&] {
        skip_space();
        if (offset >= src.size()) { fail("unexpected end"); return; }
        // A ( left open at the very end counts as closed (Fire Wall's
        // EDmgSymPerCalc is missing its last ")"; the game reads it).
        if (eat("(")) { ternary(); skip_space(); if (!eat(")") && offset < src.size()) fail("expected )"); return; }
        if (std::isdigit((unsigned char)src[offset])) {
            int value = 0;
            while (offset < src.size() && std::isdigit((unsigned char)src[offset])) value = value * 10 + (src[offset++] - '0');
            emit(Calc::Const, value);
            return;
        }
        const auto name = ident();
        if (name.empty()) { fail(std::string("unexpected '") + src[offset] + "'"); ++offset; return; }
        if (name == "min" || name == "max" || name == "rand") {
            if (!eat("(")) { fail("expected ( after " + name); return; }
            ternary();
            if (!eat(",")) { fail("expected , in " + name); return; }
            ternary();
            if (!eat(")")) { fail("expected ) in " + name); return; }
            emit(name == "min" ? Calc::Min : name == "max" ? Calc::Max : Calc::Rand);
            return;
        }
        if (name == "skill" || name == "stat") {
            if (!eat("(")) { fail("expected ( after " + name); return; }
            const auto what = quoted();
            if (!eat(".")) { fail("expected . in " + name + "()"); return; }
            const auto field = ident();
            if (!eat(")")) { fail("expected ) in " + name + "()"); return; }
            if (name == "skill") {
                const auto found = names.skills.find(what);
                const int code = operand(field);
                if (found == names.skills.end()) { fail("unknown skill '" + what + "'"); return; }
                if (code < 0) { fail("unknown operand '" + field + "'"); return; }
                emit(Calc::SkillRef, found->second, code);
            } else {
                const auto found = names.stats.find(what);
                if (found == names.stats.end()) { fail("unknown stat '" + what + "'"); return; }
                emit(Calc::StatRef, found->second);
            }
            return;
        }
        if (const int code = operand(name); code >= 0) { emit(Calc::Operand, code); return; }
        fail("unknown name '" + name + "'");
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
    skip_space();
    if (offset < src.size()) fail("trailing '" + std::string(src.substr(offset)) + "'");
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
    std::array<std::string, 5> sumskill;   // +0xc4: the pet's skills
    std::array<Calc, 5> sumsk_calc;        // +0xd0: their levels
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
    std::string aurastate, auratarget;     // aurastate (+0x80), auratargetstate (+0x82): States.txt names
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
inline int calc_ln(int base, int per_level, int lvl) { return lvl > 0 ? base + per_level * (lvl - 1) : 0; }
inline int calc_dm(int base, int per_level, int lvl) {
    if (lvl < 1) return 0;
    return std::min((110 * lvl / (lvl + 6)) * (per_level - base) / 100 + base, per_level);
}
// Mana cost in 256ths (FUN_0056c160): (mana + lvlmana x (lvl - 1)) << manashift,
// at least minmana points.
inline int mana_cost(const Skill& skill, int lvl) {
    if (lvl < 1) return 0;
    return std::max((skill.mana + skill.lvlmana * (lvl - 1)) << (skill.manashift & 31), skill.minmana * 256);
}

// What calcs may ask of the unit using the skill.
struct CalcEnv {
    std::function<int(int skill)> base_level;   // blvl: points in the skill
    std::function<int(int skill)> level;        // lvl: with item bonuses
    std::function<int(int stat)> stat;          // stat('x'.accr)
    int clvl = 1;                               // ulvl
    Rng* rng = nullptr;                         // rand()
};

inline int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth = 0);

// Elemental damage in 256ths (FUN_00644d50 / FUN_00644e40): (EMin + brackets)
// << HitShift, plus EDmgSymPerCalc percent of that; with `mastery` (the
// flag), plus the element's mastery % of the sum (FUN_00644c90: fire 329,
// lightning 330, cold 331, poison 332; env.stat).
inline int elem_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth = 0,
                       bool mastery = false) {
    if (lvl < 1) return 0;
    int damage = ((max ? skill.emax : skill.emin) + level_bonus(max ? skill.emax_lev : skill.emin_lev, lvl)) << (skill.hitshift & 31);
    if (!skill.edmg_sym.empty()) damage += damage * eval_calc(skill_tables, skill.edmg_sym, env, skill.id, lvl, depth + 1) / 100;
    if (mastery && env.stat && skill.etype >= 0 && skill.etype <= 3) damage += int(std::int64_t(damage) * env.stat(329 + skill.etype) / 100);
    return damage;
}
// Elemental length in ticks (FUN_00644f20): ELen + ELevLen1..3 over levels
// 2..8, 9..16, 17 up, plus ELenSymPerCalc percent.
inline int length_bonus(const std::array<int, 3>& lengths, int lvl) {
    return lvl < 2 ? 0 : lvl < 9 ? (lvl - 1) * lengths[0] : lvl < 17 ? (lvl - 8) * lengths[1] + lengths[0] * 7
                      : (lvl - 16) * lengths[2] + lengths[1] * 8 + lengths[0] * 7;
}
inline int elem_length(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth = 0) {
    if (lvl < 1) return 0;
    int length = skill.elen + length_bonus(skill.elen_lev, lvl);
    if (!skill.elen_sym.empty()) length += length * eval_calc(skill_tables, skill.elen_sym, env, skill.id, lvl, depth + 1) / 100;
    return length;
}
// The skill's own physical damage in 256ths (FUN_00647bc0 without the
// weapon share): (MinDam + brackets) plus DmgSymPerCalc percent, << HitShift.
inline int skill_phys(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth = 0) {
    if (lvl < 1) return 0;
    int damage = (max ? skill.maxdam : skill.mindam) + level_bonus(max ? skill.maxdam_lev : skill.mindam_lev, lvl);
    if (!skill.dmg_sym.empty()) damage += damage * eval_calc(skill_tables, skill.dmg_sym, env, skill.id, lvl, depth + 1) / 100;
    return damage << (skill.hitshift & 31);
}

// What a skill's missile carries (FUN_0064b860, the missile's Skill set):
// combat.hpp's MissileDamage.
// A row with no Skill carries its own element at the missile's level
// (FUN_0064b100 / 0064b1d0 / 0064b2a0): (EMin + MinELev brackets) <<
// HitShift, the same for the max, ELen + ELevLen brackets.
inline MissileDamage row_damage(int etype, int emin, int emax, const std::array<int, 5>& emin_lev,
                                const std::array<int, 5>& emax_lev, int hitshift, int elen,
                                const std::array<int, 3>& elen_lev, int lvl) {
    MissileDamage damage;
    if (etype < 0 || lvl < 1) return damage;
    damage.etype = etype;
    damage.elo = (emin + level_bonus(emin_lev, lvl)) << (hitshift & 31);
    damage.ehi = std::max((emax + level_bonus(emax_lev, lvl)) << (hitshift & 31), damage.elo);
    damage.elen = elen + length_bonus(elen_lev, lvl);
    return damage;
}
inline MissileDamage missile_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl) {
    MissileDamage damage;
    damage.phys_lo = skill_phys(skill_tables, skill, env, lvl, false);
    damage.phys_hi = std::max(skill_phys(skill_tables, skill, env, lvl, true), damage.phys_lo);
    damage.etype = skill.etype;
    if (skill.etype >= 0) {
        damage.elo = elem_damage(skill_tables, skill, env, lvl, false, 0, true);
        damage.ehi = std::max(elem_damage(skill_tables, skill, env, lvl, true, 0, true), damage.elo);
        damage.elen = elem_length(skill_tables, skill, env, lvl);
    }
    damage.srcdam = skill.srcdam_raw;
    return damage;
}
// Attack rating bonus % (FUN_006449f0): ToHitCalc, else ToHit + LevToHit x (lvl - 1).
inline int skill_tohit(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth = 0) {
    if (lvl < 1) return 0;
    if (!skill.tohit_calc.empty()) return eval_calc(skill_tables, skill.tohit_calc, env, skill.id, lvl, depth + 1);
    return skill.tohit + skill.levtohit * (lvl - 1);
}

// One operand (FUN_00646460, codes in skillcalc.txt order).
// ponytail: the missile operands (m1en.., 26..37, 43..48), len, rng,
// pets, skpt read 0 until their phase.
inline int calc_operand(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int code, int depth) {
    const auto& params = skill.par;
    switch (code) {
        case 0: return calc_ln(params[0], params[1], lvl);  case 1: return calc_dm(params[0], params[1], lvl);
        case 2: return calc_ln(params[2], params[3], lvl);  case 3: return calc_dm(params[2], params[3], lvl);
        case 4: return calc_ln(params[4], params[5], lvl);  case 5: return calc_dm(params[4], params[5], lvl);
        case 6: return calc_ln(params[6], params[7], lvl);  case 7: return calc_dm(params[6], params[7], lvl);
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: return params[std::size_t(code - 8)];
        case 16: return lvl;
        case 17: return elem_damage(skill_tables, skill, env, lvl, false, depth) >> 8;          // edmn
        case 18: return elem_damage(skill_tables, skill, env, lvl, true, depth) >> 8;           // edmx
        case 19: return elem_length(skill_tables, skill, env, lvl, depth);                      // edln
        case 20: return skill_tohit(skill_tables, skill, env, lvl, depth);                      // toht
        case 21: return mana_cost(skill, lvl) >> 8;                                  // mana
        case 22: return lvl < 1 ? 0 : (((skill.mana + skill.lvlmana * (lvl - 1)) * 25 / 2) << (skill.manashift & 31)) >> 8;   // mps
        case 23: case 24: case 25: {                                             // math / madm / macr
            for (std::size_t k = 0; k < 5; ++k) {
                const int stat = skill.passive_stat[k], want = 0x156 + (code - 23);
                if (stat == want || stat == want + 3) return eval_calc(skill_tables, skill.passive_calc[k], env, skill.id, lvl, depth + 1);
            }
            return 0;
        }
        case 49: return elem_damage(skill_tables, skill, env, lvl, false, depth, true) >> 8;    // enma (FUN_00644d50 flag 1)
        case 50: return elem_damage(skill_tables, skill, env, lvl, true, depth, true) >> 8;     // exma
        case 51: return elem_length(skill_tables, skill, env, lvl, depth);                      // edma (FUN_00644f20 flag 1)
        case 52: return elem_damage(skill_tables, skill, env, lvl, false, depth, true);         // enms
        case 53: return elem_damage(skill_tables, skill, env, lvl, true, depth, true);          // exms
        case 38: return elem_damage(skill_tables, skill, env, lvl, false, depth);               // edns
        case 39: return elem_damage(skill_tables, skill, env, lvl, true, depth);                // edxs
        case 40: return env.clvl;                                                // ulvl
        case 41: return env.base_level ? env.base_level(skill.id) : lvl;             // blvl
        case 42: return lvl < 1 ? 0 : (skill.mana + skill.lvlmana * (lvl - 1)) << (skill.manashift & 31);   // usmc
        case 55: case 56: case 57: case 58: return eval_calc(skill_tables, skill.calc[std::size_t(code - 55)], env, skill.id, lvl, depth + 1);
        case 60: case 61: case 62: case 63: case 64: case 65:
            return eval_calc(skill_tables, skill.aura_calc[std::size_t(code - 60)], env, skill.id, lvl, depth + 1);
        case 66: case 67: case 68: case 69: case 70:
            return eval_calc(skill_tables, skill.passive_calc[std::size_t(code - 66)], env, skill.id, lvl, depth + 1);
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
inline std::vector<PassiveStat> passive_stats(const SkillTables& skill_tables, const CalcEnv& env) {
    std::vector<PassiveStat> out;
    for (const auto& skill : skill_tables.rows) {
        if (skill.id < 0 || skill.passive_stat[0] < 0 || !env.level) continue;
        const int lvl = env.level(skill.id);
        if (lvl < 1) continue;
        for (std::size_t k = 0; k < 5 && skill.passive_stat[k] >= 0; ++k)
            out.push_back({ skill.passive_stat[k], eval_calc(skill_tables, skill.passive_calc[k], env, skill.id, lvl, 0), skill.passive_itype });
    }
    return out;
}

// Runs a calc for `skill` at `lvl` (the stack machine; divide by zero
// gives 0, as FUN_006c0bc0's op 0x13).
inline int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth) {
    const Skill* skill_row = skill_tables.get(skill);
    if (calc.empty() || !skill_row || depth > 8) return 0;
    std::vector<int> stack;
    stack.reserve(calc.code.size());
    auto pop = [&] { if (stack.empty()) return 0; const int value = stack.back(); stack.pop_back(); return value; };
    for (const auto& instruction : calc.code) {
        switch (instruction.opcode) {
            case Calc::Const: stack.push_back(instruction.operand1); break;
            case Calc::Operand: stack.push_back(calc_operand(skill_tables, *skill_row, env, lvl, instruction.operand1, depth)); break;
            case Calc::SkillRef: {                     // that skill's operand at the unit's level in it
                const Skill* other = skill_tables.get(instruction.operand1);
                const int level = instruction.operand2 == 41 ? (env.base_level ? env.base_level(instruction.operand1) : 0) : env.level ? env.level(instruction.operand1) : 0;
                stack.push_back(other && instruction.operand2 != 41 ? calc_operand(skill_tables, *other, env, level, instruction.operand2, depth + 1) : level);
                break;
            }
            case Calc::StatRef: stack.push_back(env.stat ? env.stat(instruction.operand1) : 0); break;
            case Calc::Neg: stack.push_back(-pop()); break;
            case Calc::Cond: { const int if_false = pop(), if_true = pop(), condition = pop(); stack.push_back(condition ? if_true : if_false); break; }
            default: {
                const int right = pop(), left = pop();
                switch (instruction.opcode) {
                    case Calc::Min: stack.push_back(std::min(left, right)); break;
                    case Calc::Max: stack.push_back(std::max(left, right)); break;
                    case Calc::Rand: stack.push_back(env.rng ? env.rng->range(left, right) : left); break;
                    case Calc::Add: stack.push_back(left + right); break;
                    case Calc::Sub: stack.push_back(left - right); break;
                    case Calc::Mul: stack.push_back(left * right); break;
                    case Calc::Div: stack.push_back(right ? left / right : 0); break;
                    case Calc::Lt: stack.push_back(left < right); break;
                    case Calc::Gt: stack.push_back(left > right); break;
                    case Calc::Le: stack.push_back(left <= right); break;
                    case Calc::Ge: stack.push_back(left >= right); break;
                    case Calc::Eq: stack.push_back(left == right); break;
                    case Calc::Ne: stack.push_back(left != right); break;
                    default: stack.push_back(0); break;
                }
            }
        }
    }
    return stack.empty() ? 0 : stack.back();
}

// ---- Whirlwind

// Frames between Whirlwind's hits (FUN_005d9320) by the weapon's attack
// length in frames (FUN_0062a710): under 12 → 4, 15 → 6, 18 → 8, 20 → 10,
// 23 → 12, then 14, and 16 past 25; 10 bare-handed.
inline int whirlwind_gap(int attack_frames) {
    const int frames = attack_frames;
    return frames < 12 ? 4 : frames < 15 ? 6 : frames < 18 ? 8 : frames < 20 ? 10 : frames < 23 ? 12 : frames > 25 ? 16 : 14;
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
inline ChargeBonus charge_bonus(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int charges) {
    ChargeBonus bonus;
    if (lvl < 1 || charges < 1) return bonus;
    charges = std::min(charges, 3);
    switch (skill.prgdam) {
        case 1: bonus.ed_pct = eval_calc(skill_tables, skill.calc[0], env, skill.id, lvl) * charges; break;
        case 2: {
            const int steal = calc_ln(skill.par[0], skill.par[1], lvl) * (charges == 3 ? 2 : 1);
            bonus.life_steal = steal;
            bonus.mana_steal = charges >= 2 ? steal : 0;
            break;
        }
        case 4:
            bonus.etype = skill.etype;
            bonus.elem_lo = elem_damage(skill_tables, skill, env, lvl, false) >> 8;
            bonus.elem_hi = elem_damage(skill_tables, skill, env, lvl, true) >> 8;
            bonus.elem_len = elem_length(skill_tables, skill, env, lvl);
            break;
        default: break;
    }
    return bonus;
}

// ---- a character's skill levels

// Item bonuses to one skill, over the item properties the character wears
// (ItemStatCost ids): 127 +all skills, 83 +class skills (param = class),
// 188 +skill tab (param = class x 8 + tab, tab = SkillDesc page - 1),
// 107 +single class skill and 97 +skill as "oskill" (param = skill id).
inline int item_skill_bonus(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props) {
    int bonus = 0;
    const bool own = !skill.cls.empty() && skill.cls == kClassCode[std::size_t(cls)];
    for (const auto& prop : props) {
        switch (prop.stat) {
            case 127: if (own) bonus += prop.value; break;
            case 83: if (own && prop.param == cls) bonus += prop.value; break;
            case 188: if (own && skill.page > 0 && prop.param == cls * 8 + skill.page - 1) bonus += prop.value; break;
            case 107: case 97: if (prop.param == skill.id) bonus += prop.value; break;
            default: break;
        }
    }
    return bonus;
}

}  // namespace d2d::rules
