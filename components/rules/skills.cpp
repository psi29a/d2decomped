// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of skills.hpp's functions; their notes are in the header.
#include "skills.hpp"

#include "combat.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

//   `!` alone) ends the expression there.
// - A name is looked up by its first four characters among the operands
//   (FUN_006119f0), so Bone Wall's calc2 "par34" reads par3; an unknown
//   name is 0. Directly inside skill() / sklvl() a name (quoted or not)
//   is first a skill, inside stat() a stat (then `base` 1, `mod` 2, else
//   0), as constants; `.name` is the same lookup but always a constant,
//   and an unknown one ends the expression.
// - Precedence is the in-stack table at 0x6fc874 against the incoming
//   op's byte: comparisons all one level, then + -, * /, ^ (power), unary
//   minus, then `?`, which only an operator after its third operand pops:
//   `lvl < 4 ? 1 : 2` is lvl < (4 ? 1 : 2); `:` does nothing; `,` only
//   flushes. `-` is unary unless an operand or a function's "(" came
//   last, where a "," or "?" doesn't count.
// - At the end, a "(" still open stops the program there (the VM ends on
//   its mark byte), so "1+(2" is 2; a function left open drops the rest.
// - Too few operands for an op, a ")" with no "(", or more than 64
//   pending ops: game.exe stores no calc (0xffffffff); here an empty
//   calc, which evaluates to 0, and `error` says why.
// Constant folding (literal-only text runs through the VM once) isn't
// done: it doesn't change a value. Checked against game.exe's own
// compiler in tools/emu on every Skills.txt / SkillDesc.txt calc and 4000
// random strings (skills.md).
// - Inside miss() (FUN_006119f0 function 4) a name is first a Missiles.txt
//   row, as a constant, else a misscalc.txt operand by its first four.
Calc compile_calc(std::string_view src, const CalcNames& names, std::string* error) {
    Calc out;
    struct Pending { std::uint8_t kind; int value; };      // kind: 1 a function's "(" (value its tag), 2 a "(", else a Calc::Op
    std::vector<Pending> pending;
    int depth = 0;                                         // values on the VM stack so far
    bool operand_last = false, failed = false;
    auto fail = [&](std::string why) { if (!failed && error) *error = std::move(why); failed = true; };
    auto lower = [](std::string_view text) { std::string folded(text); for (auto& letter : folded) letter = char(std::tolower((unsigned char)letter)); return folded; };
    auto find_name = [&](const std::unordered_map<std::string, int>& table, std::string_view name) {
        if (const auto found = table.find(std::string(name)); found != table.end()) return found->second;
        for (const auto& [key, id] : table) if (lower(key) == lower(name)) return id;
        return -1;
    };
    auto arity = [](const Pending& entry) { return entry.kind == 1 ? (entry.value == kCalcSkillLevel ? 3 : 2) : entry.kind == Calc::Neg ? 1 : entry.kind == Calc::Cond ? 3 : 2; };
    auto emit_pending = [&](const Pending& entry, bool write = true) {
        if (depth < arity(entry)) { fail("an operator is missing an operand"); return; }
        depth += 1 - arity(entry);
        if (write) out.code.push_back(entry.kind == 1 ? Calc::Ins{ Calc::Call, entry.value } : Calc::Ins{ Calc::Op(entry.kind) });
    };
    auto value = [&](Calc::Op opcode, int operand) { out.code.push_back({ opcode, operand }); ++depth; operand_last = true; };
    // FUN_006c18a0: pop while the top's in-stack precedence (0x6fc874) is
    // at least the incoming op's byte, then push it (not "," or ":").
    auto push_op = [&](std::uint8_t incoming) {
        static constexpr std::array<std::uint8_t, 13> kStackPrecedence{ 15, 15, 15, 15, 15, 15, 17, 17, 19, 19, 20, 21, 22 };
        while (!failed && !pending.empty() && pending.back().kind >= Calc::Lt && kStackPrecedence[pending.back().kind - Calc::Lt] >= incoming) {
            emit_pending(pending.back());
            pending.pop_back();
        }
        if (incoming >= Calc::Lt && incoming <= Calc::Cond) {
            if (pending.size() >= 64) fail("too many pending operators");
            pending.push_back({ incoming, 0 });
        }
    };
    // FUN_006119f0: {id, is a constant} or {-1, _} when unknown, by the
    // function whose "(" is on top of the pending stack.
    auto lookup = [&](std::string_view name) -> std::pair<int, bool> {
        const int function = !pending.empty() && pending.back().kind == 1 ? pending.back().value : -1;
        if (function == kCalcSkill || function == kCalcSkillLevel) {
            if (const int skill = find_name(names.skills, name); skill >= 0) return { skill, true };
        } else if (function == kCalcMissile) {
            if (const int missile = find_name(names.missiles, name); missile >= 0) return { missile, true };
            const auto key = name.substr(0, 4);
            for (std::size_t code = 0; code < names.miss_operands.size(); ++code) if (!key.empty() && names.miss_operands[code] == key) return { int(code), false };
            return { -1, false };
        } else if (function == kCalcStat) {
            if (const int stat = find_name(names.stats, name); stat >= 0) return { stat, true };
            return { lower(name) == "base" ? 1 : lower(name) == "mod" ? 2 : 0, false };
        }
        const auto key = name.substr(0, 4);
        for (std::size_t code = 0; code < names.operands.size(); ++code) if (!key.empty() && names.operands[code] == key) return { int(code), false };
        return { -1, false };
    };
    auto is_name_char = [](char letter) { return std::isalnum((unsigned char)letter) != 0; };
    std::size_t offset = 0;
    while (!failed) {
        while (offset < src.size() && (std::isspace((unsigned char)src[offset]) || src[offset] == '"')) ++offset;
        if (offset >= src.size()) break;
        const char letter = src[offset];
        const char next = offset + 1 < src.size() ? src[offset + 1] : '\0';
        if (std::isdigit((unsigned char)letter)) {
            std::uint32_t number = 0;
            while (offset < src.size() && std::isdigit((unsigned char)src[offset])) number = number * 10 + std::uint32_t(src[offset++] - '0');
            value(Calc::Const, int(number));
        } else if (letter == '\'') {
            const auto end_quote = std::min(src.find('\'', offset + 1), src.size());
            const auto [id, constant] = lookup(src.substr(offset + 1, end_quote - offset - 1));
            offset = std::min(end_quote + 1, src.size());
            value(id < 0 || constant ? Calc::Const : Calc::Operand, std::max(id, 0));
        } else if (letter == '.') {
            std::size_t end = offset + 2;
            while (end < src.size() && is_name_char(src[end])) ++end;
            const int id = next == '\0' ? -1 : lookup(src.substr(offset + 1, std::min(end, src.size()) - offset - 1)).first;
            if (id < 0) break;
            offset = end;
            value(Calc::Const, id);
        } else if (std::isalpha((unsigned char)letter)) {
            std::size_t end = offset;
            while (end < src.size() && is_name_char(src[end])) ++end;
            const auto name = src.substr(offset, end - offset);
            offset = end;
            std::size_t paren = end;
            while (paren < src.size() && std::isspace((unsigned char)src[paren])) ++paren;
            static constexpr std::array<std::string_view, 7> kFunctions{ "min", "max", "rand", "skill", "miss", "stat", "sklvl" };   // LAB_00611930
            const auto function = std::ranges::find(kFunctions, lower(name));
            if (paren < src.size() && src[paren] == '(' && function != kFunctions.end()) {
                if (pending.size() >= 64) fail("too many pending operators");
                pending.push_back({ 1, int(function - kFunctions.begin()) });
                offset = paren + 1;
                operand_last = true;
            } else {
                const auto [id, constant] = lookup(name);
                value(id < 0 || constant ? Calc::Const : Calc::Operand, std::max(id, 0));
            }
        } else {
            ++offset;
            const bool two = next == '=';
            switch (letter) {
                case '(': if (pending.size() >= 64) fail("too many pending operators"); pending.push_back({ 2, 0 }); break;
                case ')':
                    while (!failed && !pending.empty() && pending.back().kind != 2 && pending.back().kind != 1) { emit_pending(pending.back()); pending.pop_back(); }
                    if (pending.empty()) { fail("a ) with no ("); break; }
                    if (pending.back().kind == 1) emit_pending(pending.back());
                    pending.pop_back();
                    break;
                case ',': push_op(3); break;
                case '?': push_op(Calc::Cond); break;
                case ':': break;
                case '+': push_op(Calc::Add); operand_last = false; break;
                case '-': push_op(operand_last ? Calc::Sub : Calc::Neg); operand_last = false; break;
                case '*': push_op(Calc::Mul); operand_last = false; break;
                case '/': push_op(Calc::Div); operand_last = false; break;
                case '^': push_op(Calc::Pow); operand_last = false; break;
                case '<': push_op(two ? Calc::Le : Calc::Lt); offset += two; operand_last = false; break;
                case '>': push_op(two ? Calc::Ge : Calc::Gt); offset += two; operand_last = false; break;
                case '=': case '!':
                    if (!two) { offset = src.size(); break; }
                    push_op(letter == '=' ? Calc::Eq : Calc::Ne); ++offset; operand_last = false; break;
                default: offset = src.size(); break;
            }
        }
    }
    // The end: pop down to a function left open. A "(" still open is
    // written as its mark byte, where the VM stops (so "1+(2" is 2), but
    // counts as a value for the ops under it, which are still checked.
    bool open_paren = false;
    while (!failed && !pending.empty() && pending.back().kind != 1) {
        if (pending.back().kind == 2) { open_paren = true; ++depth; }
        else emit_pending(pending.back(), !open_paren);
        pending.pop_back();
    }
    if (!failed && out.code.empty()) {
        if (open_paren) out.code.push_back({ Calc::Const, 0 });   // the VM's empty stack
        else fail("nothing to compute");
    }
    if (failed) out.code.clear();
    return out;
}

int release_ring_hops(int param2, int missile_id, bool second_scan) {
    if (second_scan && missile_id != kReleaseHopMissile) return 0;
    return param2 + 1;
}

// It floors that whole stat at 50, adds Param1, then uses FUN_00483360
// MulDiv on the class's 8.8 RunVelocity.
int charge_velocity(int walk_velocity, int run_velocity, int velocity_percent, bool chilled, int param1) {
    const int run_bonus = walk_velocity > 0 ? run_velocity * 100 / walk_velocity - 100 : 0;
    const int velocity = std::max(100 + velocity_percent + run_bonus + (chilled ? -50 : 0), 50);
    return mul_div(run_velocity << 8, velocity + param1, 100);
}

int level_bonus(const std::array<int, 5>& lev, int lvl) {
    if (lvl < 2) return 0;
    if (lvl > 28) return (lvl - 28) * lev[4] + (lev[3] + lev[2]) * 6 + lev[1] * 8 + lev[0] * 7;
    if (lvl > 22) return (lvl - 22) * lev[3] + lev[2] * 6 + lev[1] * 8 + lev[0] * 7;
    if (lvl > 16) return (lvl - 16) * lev[2] + lev[1] * 8 + lev[0] * 7;
    if (lvl > 8) return (lvl - 8) * lev[1] + lev[0] * 7;
    return (lvl - 1) * lev[0];
}

int calc_dm(int base, int per_level, int lvl) {
    if (lvl < 1) return 0;
    return std::min((110 * lvl / (lvl + 6)) * (per_level - base) / 100 + base, per_level);
}

int mana_formula(const Skill& skill, int lvl) {
    if (lvl < 1) return 0;
    return (skill.mana + skill.lvlmana * (lvl - 1)) << (skill.manashift & 31);
}

int mana_cost(const Skill& skill, int lvl) {
    if (lvl < 1) return 0;
    return std::max(mana_formula(skill, lvl), skill.minmana * 256);
}

// A unit in state 0x72 pays through FUN_005d2b60 instead
// (Fight::blood_pay); srvdofunc 116 passes the check at once (not
// modelled).
int mana_paid(const Skill& skill, int lvl, std::int64_t mana) {
    if (skill.mana == 0 && skill.lvlmana == 0) return 0;
    const int cost = mana_cost(skill, lvl);
    return mana < cost ? 0 : cost;
}

// The min takes its synergy only above one point or with an EMinLev1
// (FUN_00644d50 at 0x644db3; the max always): a lightning skill's 1-x stays
// 1 whatever its synergies.
int elem_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth,
                bool mastery) {
    if (lvl < 1) return 0;
    int damage = ((max ? skill.emax : skill.emin) + level_bonus(max ? skill.emax_lev : skill.emin_lev, lvl)) << (skill.hitshift & 31);
    if (!skill.edmg_sym.empty() && (max || damage > 0x100 || skill.emin_lev[0] != 0))
        damage += mul_div(damage, eval_calc(skill_tables, skill.edmg_sym, env, skill.id, lvl, depth + 1), 100);
    if (mastery && env.stat && skill.etype >= 0 && skill.etype <= 3) damage += mul_div(damage, env.stat(d2d::d2s::kPassiveFireMastery + skill.etype), 100);
    return damage;
}

int length_bonus(const std::array<int, 3>& lengths, int lvl) {
    return lvl < 2 ? 0 : lvl < 9 ? (lvl - 1) * lengths[0] : lvl < 17 ? (lvl - 8) * lengths[1] + lengths[0] * 7
                      : (lvl - 16) * lengths[2] + lengths[1] * 8 + lengths[0] * 7;
}

int elem_length(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth) {
    if (lvl < 1) return 0;
    int length = skill.elen + length_bonus(skill.elen_lev, lvl);
    if (!skill.elen_sym.empty()) length += length * eval_calc(skill_tables, skill.elen_sym, env, skill.id, lvl, depth + 1) / 100;
    return length;
}

int skill_phys(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth) {
    if (lvl < 1) return 0;
    int damage = (max ? skill.maxdam : skill.mindam) + level_bonus(max ? skill.maxdam_lev : skill.mindam_lev, lvl);
    if (!skill.dmg_sym.empty()) damage += damage * eval_calc(skill_tables, skill.dmg_sym, env, skill.id, lvl, depth + 1) / 100;
    return damage << (skill.hitshift & 31);
}

MissileDamage row_damage(int etype, int emin, int emax, const std::array<int, 5>& emin_lev,
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

MissileDamage missile_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl) {
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

int skill_tohit(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth) {
    if (lvl < 1) return 0;
    if (!skill.tohit_calc.empty()) return eval_calc(skill_tables, skill.tohit_calc, env, skill.id, lvl, depth + 1);
    return skill.tohit + skill.levtohit * (lvl - 1);
}

AttackLine attack_line(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl,
                       const Fighter& fighter, int mastery_tohit, std::uint8_t blue, std::uint8_t red,
                       int usable) {
    AttackLine line;
    line.skill = skill.id;
    if (usable != 0 && usable != 1 && usable != 6 && usable != 8) return line;   // FUN_004d9fc0
    constexpr int kElemColour[5] = { 1, 9, 3, 2, 3 };    // Fighter::elem order: fire, lightning, cold, poison, magic
    const auto calc_of = [&](const Calc& calc) { return calc.empty() ? 0 : eval_calc(skill_tables, calc, env, skill.id, lvl); };
    const auto weapon = [&](int pct, int flat) {
        const int total = std::max(fighter.phys_pct + pct, -90);
        line.min = int((std::int64_t(fighter.phys_lo) * (100 + total) / 100) >> 8) + flat;
        line.max = int((std::int64_t(fighter.phys_hi) * (100 + total) / 100) >> 8) + flat;
        for (std::size_t elem : { 0u, 1u, 2u, 4u, 3u }) {
            const auto& [low, high] = fighter.elem[elem];
            line.min += std::min(low, high); line.max += high;
            if (low != 0 || high != 0) line.damage_colour = kElemColour[elem];
        }
        if (line.max > 0) { line.min = std::max(line.min, 1); line.max = std::max(line.max, line.min + 1); }
    };
    const auto state_colour = [&](PanelColour bit, int colour) { return (red & bit) ? 1 : (blue & bit) ? 3 : colour; };
    const int etype_colour = skill.etype >= 0 && skill.etype < 5 ? kElemColour[skill.etype] : 0;   // FUN_004e6fb0
    // FUN_004e9660: SrcDam / 128 of the weapon's damage joins the line; the
    // weapon's elements colour it, then damblue / damred (FUN_004e89a0's end).
    const auto weapon_share = [&] {
        if (skill.srcdam_raw == 0) return;
        const int base_min = line.min, base_max = line.max;
        weapon(0, 0);
        line.min = base_min + line.min * skill.srcdam_raw / 128; line.max = base_max + line.max * skill.srcdam_raw / 128;
        line.damage_colour = state_colour(kPanelDamage, line.damage_colour);
    };
    const auto own_damage = [&](int poison_len) {
        line.min += skill_phys(skill_tables, skill, env, lvl, false) >> 8;
        line.max += skill_phys(skill_tables, skill, env, lvl, true) >> 8;
        line.min += elem_damage(skill_tables, skill, env, lvl, false, 0, true) * poison_len >> 8;
        line.max += elem_damage(skill_tables, skill, env, lvl, true, 0, true) * poison_len >> 8;
    };
    if (skill.descdam == 1 || skill.descdam == 7) {
        line.damage = true;
        weapon(calc_of(skill.ddam_calc1), calc_of(skill.ddam_calc2));
        line.damage_colour = state_colour(kPanelDamage, line.damage_colour);
        own_damage(1);
    } else if (skill.descdam == 5 || skill.descdam == 24) {   // FUN_004ead60, FUN_004eae70: the same sum in another order
        line.damage = true;
        line.damage_colour = etype_colour;
        weapon_share();
        own_damage(skill.etype == 3 ? elem_length(skill_tables, skill, env, lvl) : 1);   // poison x ELen (FUN_00644f20), 0 too
    } else if (skill.descdam == 9) {                          // FUN_004eb390 (Blaze, Fire Wall, Firestorm)
        line.damage = true;
        const int shift = skill.hitshift & 31;
        line.min = ((skill.mindam << shift) + elem_damage(skill_tables, skill, env, lvl, false, 0, true)) * 75 >> 8;
        line.max = ((skill.maxdam << shift) + elem_damage(skill_tables, skill, env, lvl, true, 0, true)) * 75 >> 8;
        line.damage_colour = etype_colour;
        weapon_share();
    } else if (skill.descdam == 8) {                          // FUN_004eb290 (Inferno, Arctic Blast, Inferno Sentry)
        line.damage = true;
        const int scale = calc_of(skill.ddam_calc1), over = calc_of(skill.ddam_calc2);
        const int times = scale != 0 ? scale : 1, per = over != 0 ? over : 1;
        line.min = elem_damage(skill_tables, skill, env, lvl, false, 0, true) * times * 25 / per >> 8;
        line.max = elem_damage(skill_tables, skill, env, lvl, true, 0, true) * times * 25 / per >> 8;
        line.damage_colour = etype_colour;
    }
    if (skill.descatt == 1 || skill.descatt == 2 || skill.descatt == 5) {
        const int pct = fighter.ar_pct - (skill.descatt == 2 ? 0 : mastery_tohit) + skill_tohit(skill_tables, skill, env, lvl);
        line.attack_rating = int(std::int64_t(fighter.ar_base) * pct / 100 + fighter.ar_base);
        line.ar_colour = state_colour(kPanelAttack, line.ar_colour);
    }
    return line;
}

// skpt (+0x170, the skill points calc) reads 0: no 1.14d calc (Skills.txt,
// Missiles.txt) names it.
int calc_operand(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int code, int depth) {
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
        case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
        case 43: case 44: case 45: case 46: case 47: case 48:
            return env.desc_missile ? env.desc_missile(skill, code, lvl) : 0;
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
        case 54: return eval_calc(skill_tables, skill.auralen, env, skill.id, lvl, depth + 1);      // len
        case 59: return eval_calc(skill_tables, skill.aurarange, env, skill.id, lvl, depth + 1);    // rng
        case 71: return eval_calc(skill_tables, skill.petmax, env, skill.id, lvl, depth + 1);       // pets
        case 55: case 56: case 57: case 58: return eval_calc(skill_tables, skill.calc[std::size_t(code - 55)], env, skill.id, lvl, depth + 1);
        case 60: case 61: case 62: case 63: case 64: case 65:
            return eval_calc(skill_tables, skill.aura_calc[std::size_t(code - 60)], env, skill.id, lvl, depth + 1);
        case 66: case 67: case 68: case 69: case 70:
            return eval_calc(skill_tables, skill.passive_calc[std::size_t(code - 66)], env, skill.id, lvl, depth + 1);
        default: return 0;
    }
}

std::vector<PassiveStat> passive_stats(const SkillTables& skill_tables, const CalcEnv& env) {
    std::vector<PassiveStat> out;
    for (const auto& skill : skill_tables.rows) {
        if (skill.id < 0 || skill.passive_stat[0] < 0 || !env.level) continue;
        // A summoner's passive stats are its pet's (FUN_005c4470 puts
        // them on the pet: Raise Skeleton's maxhp from Skeleton Mastery,
        // the golems' speed, the spirits' resists), not the player's.
        // FUN_00646f20 skips them too: it takes only a skill with a
        // passivestate (+0x94 > 0), and none of these 17 or Revive has one.
        if (!skill.summon.empty() || skill.srvdofunc == ServerDoFunction::kRevive) continue;
        const int lvl = env.level(skill.id);
        if (lvl < 1) continue;
        for (std::size_t k = 0; k < 5 && skill.passive_stat[k] >= 0; ++k)
            out.push_back({ skill.passive_stat[k], eval_calc(skill_tables, skill.passive_calc[k], env, skill.id, lvl, 0), skill.passive_itype });
    }
    return out;
}

SummonStats summon_stats(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl) {
    SummonStats out;
    for (std::size_t k = 0; k < 5; ++k)
        if (skill.passive_stat[k] >= 0) out.passive[skill.passive_stat[k]] += eval_calc(skill_tables, skill.passive_calc[k], env, skill.id, lvl);
    for (std::size_t k = 0; k < 6; ++k)
        if (skill.aurastat[k] >= 0) out.aura[skill.aurastat[k]] += eval_calc(skill_tables, skill.aura_calc[k], env, skill.id, lvl);
    return out;
}

int summon_life(int max_fine, int pct) {
    return int(std::uint32_t(max_fine) + std::uint32_t(mul_div(max_fine, pct, 100)));
}

std::vector<std::pair<int, int>> summon_skill_levels(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl) {
    std::vector<std::pair<int, int>> out;
    const auto named = [&](const std::string& name) -> const Skill* {
        if (name.empty()) return nullptr;
        if (const auto found = skill_tables.by_name.find(name); found != skill_tables.by_name.end()) return skill_tables.get(found->second);
        for (const auto& [skill_name, id] : skill_tables.by_name)
            if (std::ranges::equal(skill_name, name, [](char left, char right) { return std::tolower(std::uint8_t(left)) == std::tolower(std::uint8_t(right)); }))
                return skill_tables.get(id);
        return nullptr;
    };
    for (std::size_t k = 0; k < 5; ++k)
        if (const auto* given = named(skill.sumskill[k]))
            if (const int given_level = eval_calc(skill_tables, skill.sumsk_calc[k], env, skill.id, lvl); given_level >= 1) out.emplace_back(given->id, given_level);
    return out;
}

std::vector<std::pair<int, int>> summon_resist(int value, int absorb_fire, int absorb_light, int absorb_cold) {
    std::vector<std::pair<int, int>> out;
    if (value == 0) return out;
    if (absorb_fire <= 0) out.emplace_back(d2d::d2s::kFireResist, value);
    if (absorb_light <= 0) out.emplace_back(d2d::d2s::kLightningResist, value);
    if (absorb_cold <= 0) out.emplace_back(d2d::d2s::kColdResist, value);
    out.emplace_back(d2d::d2s::kPoisonResist, value);
    return out;
}

int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth) {
    const Skill* skill_row = skill_tables.get(skill);
    if (calc.empty() || !skill_row || depth > 8) return 0;
    std::vector<int> stack;
    stack.reserve(calc.code.size());
    auto pop = [&] { if (stack.empty()) return 0; const int value = stack.back(); stack.pop_back(); return value; };
    for (const auto& instruction : calc.code) {
        switch (instruction.opcode) {
            case Calc::Const: stack.push_back(instruction.operand1); break;
            case Calc::Operand: stack.push_back(calc_operand(skill_tables, *skill_row, env, lvl, instruction.operand1, depth)); break;
            case Calc::Call: {                         // the function table at 0x745774
                const int third = instruction.operand1 == kCalcSkillLevel ? pop() : 0;
                const int right = pop(), left = pop();
                switch (instruction.operand1) {
                    case kCalcMin: stack.push_back(std::min(left, right)); break;
                    case kCalcMax: stack.push_back(std::max(left, right)); break;
                    case kCalcRand: stack.push_back(env.rng ? env.rng->range(left, right) : left); break;
                    case kCalcSkill: {                 // skill(id, operand): that skill's operand at the unit's level in it
                        const Skill* other = skill_tables.get(left);
                        const int level = right == 41 ? (env.base_level ? env.base_level(left) : 0) : env.level ? env.level(left) : 0;
                        stack.push_back(other && right != 41 ? calc_operand(skill_tables, *other, env, level, right, depth + 1) : level);
                        break;
                    }
                    case kCalcStat: stack.push_back(env.stat ? env.stat(left) : 0); break;
                    case kCalcMissile: stack.push_back(env.missile ? env.missile(skill, left, right, lvl) : 0); break;
                    case kCalcSkillLevel: {            // sklvl(id, at, operand), FUN_00646c60: that skill's operand at this one's `at`
                        const Skill* other = skill_tables.get(left);
                        const int level = calc_operand(skill_tables, *skill_row, env, lvl, right, depth + 1);
                        stack.push_back(other ? calc_operand(skill_tables, *other, env, level, third, depth + 1) : 0);
                        break;
                    }
                    default: stack.push_back(0); break;
                }
                break;
            }
            case Calc::Neg: stack.push_back(int(0u - std::uint32_t(pop()))); break;
            case Calc::Cond: { const int if_false = pop(), if_true = pop(), condition = pop(); stack.push_back(condition ? if_true : if_false); break; }
            default: {
                const int right = pop(), left = pop();
                const auto left_bits = std::uint32_t(left), right_bits = std::uint32_t(right);
                switch (instruction.opcode) {
                    case Calc::Add: stack.push_back(int(left_bits + right_bits)); break;
                    case Calc::Sub: stack.push_back(int(left_bits - right_bits)); break;
                    case Calc::Mul: stack.push_back(int(left_bits * right_bits)); break;
                    case Calc::Div: stack.push_back(right ? left / right : 0); break;
                    case Calc::Pow: {                  // 1 for a power below 1, else repeated multiplies
                        std::uint32_t power = 1;
                        for (int step = 0; step < right; ++step) power *= left_bits;
                        stack.push_back(int(power));
                        break;
                    }
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

int whirlwind_gap(int attack_frames) {
    const int frames = attack_frames;
    return frames < 12 ? 4 : frames < 15 ? 6 : frames < 18 ? 8 : frames < 20 ? 10 : frames < 23 ? 12 : frames > 25 ? 16 : 14;
}

ChargeBonus charge_bonus(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int charges) {
    ChargeBonus bonus;
    if (lvl < 1 || charges < 1) return bonus;
    charges = std::min(charges, 3);
    for (std::size_t k = 0; k < skill.aurastat.size(); ++k)
        if (skill.aurastat[k] == 325) bonus.ar_pct += eval_calc(skill_tables, skill.aura_calc[k], env, skill.id, lvl) * charges;
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
            if (skill.etype == 2 && charges == 3 && skill.par[4] != 0) bonus.freeze_len = bonus.elem_len / skill.par[4];
            bonus.conv_pct = std::max(eval_calc(skill_tables, skill.calc[0], env, skill.id, lvl), 0);
            break;
        default: break;
    }
    return bonus;
}

// Its other two terms: the skill record's +0x2c, which only the client
// fills (the bonus the server sends: FUN_004c6140 / FUN_004d88a0 ->
// FUN_00647aa0), and FUN_00644150's 2 under state 0x86 (the skill shrine),
// which d2d adds as stat 127 (World's fight.skill_level, the client's
// skillbar.extra).
int item_skill_bonus(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props) {
    static constexpr std::array<int, 5> kGameEType{ 1, 2, 4, 5, 3 };   // d2d's fire, lightning, cold, poison, magic
    const bool own = !skill.cls.empty() && skill.cls == kClassCode[std::size_t(cls)];
    const int etype = skill.etype >= 0 && skill.etype < 5 ? kGameEType[std::size_t(skill.etype)] : 0;
    int bonus = 0, oskill = 0;
    for (const auto& prop : props) {
        switch (prop.stat) {
            case 127: bonus += prop.value; break;
            case 83: if (own && prop.param == cls) bonus += prop.value; break;
            case 188: if (own && skill.page > 0 && prop.param == cls * 8 + skill.page - 1) bonus += prop.value; break;
            case 97: if (prop.param == skill.id) oskill += prop.value; break;
            case 126: if (etype != 0 && prop.param == etype) bonus += prop.value; break;
            case 107: if (prop.param == skill.id) bonus += prop.value; break;
            default: break;
        }
    }
    return bonus + (own ? std::min(oskill, 3) : oskill);
}

bool item_grants_skill(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props) {
    const bool own = !skill.cls.empty() && skill.cls == kClassCode[std::size_t(cls)];
    return std::ranges::any_of(props, [&](const d2d::d2s::ItemProp& prop) { return prop.param == skill.id && prop.value > 0 && (prop.stat == 97 || (prop.stat == 107 && own)); });
}

std::vector<std::pair<int, int>> monster_curse_stats(int pick, int param7) {
    switch (pick) {
        case 0: return { { 36, -100 } };
        case 1: return { { 25, -50 } };
        case 4: return { { 67, param7 }, { 25, -50 }, { 36, -50 }, { 68, param7 } };
        default: return {};
    }
}

}
