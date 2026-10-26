// SPDX-License-Identifier: GPL-3.0-or-later
// Skill tooltips as game.exe builds them (docs/research/re/skill-tooltips.md):
// the skill tree's (FUN_004eef00) and the skill buttons' (FUN_004ef990),
// from SkillDesc.txt's 17 lines through the line formats of FUN_004edea0.
// game.exe appends the text bottom line first, '\n' between lines, colour
// codes (0xff 'c' n) carrying on through what follows; that string is
// kept here as built and turned into lines top to bottom at the end.
#pragma once

#include "gamedata.hpp"
#include "item_text.hpp"

#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::game {

namespace skill_text {

inline constexpr std::uint16_t kNewline = 0xf9e, kColon = 0xf9d, kSpace = 0xf9b, kPercent = 0xfa1;
inline constexpr std::uint16_t kSecond = 0x10ab, kSeconds = 0x10ac, kYard = 0x10c0, kYards = 0x10b6, kPerSecond = 0x10be;
inline constexpr std::uint16_t kPercentWord = 0x10b3, kAverage = 0x10e2, kOver = 0x2b0e, kDuration = 0x10b0;

// A SkillDesc line's text (a tbl key), "" for none (game.exe's 0x1506).
inline std::string key_text(const GameData& game_data, std::string_view key) {
    if (key.empty()) return {};
    const auto found = lookup_string(game_data, key);
    return found ? u16_to_latin1(*found) : std::string{};
}

inline std::string signed_number(int value, bool plus) { return (plus && value >= 0 ? "+" : "") + std::to_string(value); }

// FUN_004e7430 / FUN_004e73e0: whole[.tenth].
inline std::string tenths(int whole, int tenth, bool plus) {
    return signed_number(whole, plus) + (tenth != 0 ? "." + std::to_string(tenth) : std::string{});
}

// An element's damage label (FUN_004e6f60 by game.exe's EType; d2d's etype
// order is fire, lightning, cold, poison, magic).
inline std::uint16_t element_label(int etype) {
    static constexpr std::array<std::uint16_t, 5> kLabel{ 0x10a1, 0x10a3, 0x10a2, 0x10a4, 0x10c3 };
    return etype >= 0 && etype < 5 ? kLabel[std::size_t(etype)] : std::uint16_t(0x10a1);
}

inline std::string range(int low, int high) { return low == high || (low == 0 && high == 0) ? std::to_string(low) : std::to_string(low) + "-" + std::to_string(high); }

struct Builder {
    const GameData& game_data;
    std::string text;
    std::string corner;   // formats 53..56's box, drawn apart from the tooltip (FUN_004ed3f0)
    void add(std::string_view piece) { text += piece; }
    void id(std::uint16_t string) { text += string_id(game_data, string); }
    void key(std::string_view name) { text += key_text(game_data, name); }
    void newline() { id(kNewline); }
    void colour(int code) { text += "\xff" "c"; text += char('0' + code); }

    // FUN_004edb30: frames as seconds, "<label>N[.N] second(s)".
    bool seconds(int frames, const std::string& label, bool plus) {
        const int whole = frames / 25, tenth = frames % 25 * 10 / 25;
        if (whole == 0 && tenth == 0) return false;
        add(label); add(tenths(whole, tenth, plus));
        id(whole == 1 && tenth == 0 ? kSecond : kSeconds);
        newline();
        return true;
    }
};

inline const GameData::MissileInfo* missile_row(const GameData& game_data, const std::string& name) {
    const auto found = name.empty() ? game_data.missiles.end() : game_data.missiles.find(name);
    return found == game_data.missiles.end() ? nullptr : &found->second;
}

// The SkillDesc missiles' calc operands (FUN_00646460 codes 26..37,
// 43..48): m1en / m1ex / m1el (>> 8, length FUN_0064b2a0), m2.., m3..,
// m1rn.. (Range + LevRange x lvl), m1eo / m1ey .. in 256ths. game.exe
// picks the 256ths' missile by (code - 43) / 3, so m2eo / m2ey read
// missile 1 and me3o / me3y missile 2.
inline int desc_missile_operand(const GameData& game_data, const d2d::rules::CalcEnv& env, const d2d::rules::Skill& skill, int code, int lvl) {
    const auto missile_at = [&](int slot) { return missile_row(game_data, skill.desc_missile[std::size_t(slot)]); };
    if (code >= 35 && code <= 37) {
        const auto* missile = missile_at(code - 35);
        return missile ? missile->lev_range * lvl + missile->range : 0;
    }
    if (lvl < 1) return 0;
    if (code >= 26 && code <= 34) {
        const auto* missile = missile_at((code - 26) / 3);
        if (!missile) return 0;
        if ((code - 26) % 3 == 2) return missile->elen + d2d::rules::length_bonus(missile->elen_lev, lvl);
        return missile_elem(game_data, *missile, env, skill.id, lvl, (code - 26) % 3 == 1) >> 8;
    }
    const bool max = (code - 43) % 2 == 1;
    const auto* missile = missile_at((code - (max ? 44 : 43)) / 3);
    return missile ? missile_elem(game_data, *missile, env, skill.id, lvl, max) : 0;
}

// A misscalc.txt operand of a Missiles.txt row (FUN_0064b340): par1 / par2,
// lvl, edmn / edmx (>> 8), edln, edns / edxs (256ths).
// ponytail: the other operands (par3.., client and hit params, damn..) read 0;
// SkillDesc's one miss() line (Royal Strike's) wants edns / edxs.
inline int miss_operand(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int missile_id, int code, int lvl) {
    const auto& names = game_data.skills.names.missiles;
    const auto named = std::ranges::find_if(names, [&](const auto& entry) { return entry.second == missile_id; });
    const auto* missile = named == names.end() ? nullptr : missile_row(game_data, named->first);
    if (!missile) return 0;
    switch (code) {
        case 0: return missile->param1;
        case 1: return missile->param2;
        case 18: return lvl;
        case 19: case 20: return missile_elem(game_data, *missile, env, skill_id, lvl, code == 20) >> 8;
        case 21: return missile->elen + d2d::rules::length_bonus(missile->elen_lev, lvl);
        case 22: case 23: return missile_elem(game_data, *missile, env, skill_id, lvl, code == 23);
        default: return 0;
    }
}

// One SkillDesc line (FUN_004edea0): whether it counts as written. game.exe
// says yes for every format it knows, whatever the line appended, except
// a few with no missile or text (and 72 / 73 at 0); the tree's headers
// hang on that.
inline bool desc_line(Builder& out, const d2d::rules::SkillTables& skill_tables, const d2d::rules::Skill& skill, const d2d::rules::CalcEnv& env,
                      int lvl, const d2d::rules::Skill::DescLine& line, int line_colour) {
    using d2d::rules::eval_calc;
    const auto& game_data = out.game_data;
    const int calc_a = line.calc_a.empty() ? 0 : eval_calc(skill_tables, line.calc_a, env, skill.id, lvl);
    const int calc_b = line.calc_b.empty() ? 0 : eval_calc(skill_tables, line.calc_b, env, skill.id, lvl);
    const std::string text_a = key_text(game_data, line.text_a), text_b = key_text(game_data, line.text_b);
    const auto both = [&] { out.add(text_a); out.add(text_b); };
    // FUN_004e7e70 / FUN_004e79e0 / FUN_004e7c30: a number between, after or before texts.
    const auto number_between = [&](int value, const std::string& before, const std::string& after, bool plus) {
        if (value == 0) return;
        out.add(before); out.add(signed_number(value, plus)); out.add(after); out.newline();
    };
    const auto number_before = [&](int value, const std::string& after, bool plus) {
        if (value == 0) return;
        out.add(signed_number(value, plus)); out.add(after); out.newline();
    };
    // FUN_004e7f40 / FUN_004e8020: a range with texts.
    const auto range_line = [&](int low, int high, const std::string& before, const std::string& after, bool plus) {
        if (low == high) return number_between(low, before, after, false);
        out.add(before); out.add((plus ? "+" : "") + std::to_string(low) + "-" + std::to_string(high)); out.add(after); out.newline();
    };
    // FUN_004e80f0: "<label>[ ]N-M" (FUN_004e79e0 when they're equal).
    const auto damage_range = [&](int low, int high, const std::string& label, bool plus, bool space) {
        if (low == high) return number_between(low, label, "", plus);
        out.add(label);
        if (space) out.id(kSpace);
        out.add((plus ? "+" : "") + std::to_string(low) + "-" + std::to_string(high)); out.newline();
    };
    // FUN_004ec140: "<label>N[-M]", the label by EType when none is given.
    const auto element_range = [&](const std::string& label, int low, int high) {
        out.add(label.empty() ? string_id(game_data, element_label(skill.etype)) : label); out.add(range(low, high)); out.newline();
    };
    // FUN_004ec220: the skill's elemental damage (mastery in).
    const auto element = [&](const std::string& label) {
        const int low = d2d::rules::elem_damage(skill_tables, skill, env, lvl, false, 0, true) >> 8;
        const int high = d2d::rules::elem_damage(skill_tables, skill, env, lvl, true, 0, true) >> 8;
        if (low != 0 || high != 0) element_range(label, low, high);
    };
    // FUN_004ec540: "Average <label>N-M per second".
    const auto average_line = [&](std::uint16_t label, int low, int high) {
        if (low == 0 && high == 0) return;
        out.id(kAverage); out.id(label); out.add(low == high ? std::to_string(low) : std::to_string(low) + "-" + std::to_string(high)); out.id(kPerSecond); out.newline();
    };
    // FUN_004ec740 / FUN_004ec6f0: the skill's elemental damage, 25 / 75 frames' worth.
    const auto average = [&](std::uint16_t label, int frames) {
        average_line(label, d2d::rules::elem_damage(skill_tables, skill, env, lvl, false, 0, true) * frames >> 8,
                     d2d::rules::elem_damage(skill_tables, skill, env, lvl, true, 0, true) * frames >> 8);
    };
    // FUN_004ec2e0: the skill's own damage, + pct % + flat, "Damage: ".
    const auto physical = [&](int pct, int flat) {
        const int low = d2d::rules::skill_phys(skill_tables, skill, env, lvl, false) >> 8, high = d2d::rules::skill_phys(skill_tables, skill, env, lvl, true) >> 8;
        const int min = low + low * pct / 100 + flat, max = high + high * pct / 100 + flat;
        if (min == 0 && max == 0) return;
        out.id(0x10a0); out.add(min == max ? signed_number(min, true) : std::to_string(min) + "-" + std::to_string(max)); out.newline();
    };
    // FUN_004edc40: cold / poison length.
    const auto length_line = [&] {
        const int length = d2d::rules::elem_length(skill_tables, skill, env, lvl);
        if (length != 0 && skill.etype == 2) out.seconds(length, string_id(game_data, 0x10a9), false);
        if (length != 0 && skill.etype == 3) out.seconds(length, string_id(game_data, 0x10aa), false);
    };
    // FUN_004ec7e0: "<a>: +N[%] <b>".
    const auto bonus = [&](bool percent) {
        if (calc_a == 0) return;
        if (!text_a.empty()) { out.add(text_a); out.id(kColon); out.id(kSpace); }
        out.add(signed_number(calc_a, true));
        if (percent) out.id(kPercent);
        out.id(kSpace); out.add(text_b); out.newline();
    };
    // case 18 / 25 / 40: a block of text in colour calc a, then a newline in the line's own colour.
    const auto coloured = [&](std::string block) {
        if (calc_a > 0) block = "\xff" "c" + std::string(1, char('0' + calc_a)) + block;
        out.add(block);
        out.colour(line_colour); out.newline();
    };
    // FUN_004e82a0 / FUN_004e83e0: "<label>N[.N] yard(s)", value x 20 / 3 or x 10 / 3
    // tenths, at least 1 whole; " yard" for 1 whole, whatever the tenth.
    const auto yards = [&](int value, const std::string& label) {
        const int whole = std::max(value / 10, 1), tenth = value % 10;
        out.add(label); out.add(std::to_string(whole) + (tenth != 0 ? "." + std::to_string(tenth) : std::string{}));
        out.id(whole == 1 ? kYard : kYards); out.newline();
    };
    // FUN_004e7a90 / FUN_004e77a0: 256ths as whole.fraction, the fraction x scale >> 8.
    const auto fraction = [](int value, int scale) { return std::pair{ value / 256, (value % 256) * scale / 256 }; };
    // FUN_006538a0 for the skill's summon (or `type`), at its own level.
    const auto summon = [&](int type) { return d2d::rules::monster_ranges(game_data.monsters, type, game_data.difficulty); };
    // FUN_004ed150 / FUN_004eccf0: the summon's A1 damage, + its aurastats' and
    // passivestats' damage % (stat 25, at least -90) and with `flat`, damage (111).
    const auto summon_damage = [&](bool flat) {
        const int type = game_data.monsters.row(skill.summon);
        if (type < 0) return;
        const auto ranges = summon(type);
        int low = ranges.a1_min, high = ranges.a1_max, pct = 0;
        const auto add = [&](int stat, const d2d::rules::Calc& calc) {
            if (stat == 25) pct += eval_calc(skill_tables, calc, env, skill.id, lvl);
            if (stat == 111 && flat) { const int value = eval_calc(skill_tables, calc, env, skill.id, lvl); low += value; high += value; }
        };
        for (std::size_t k = 0; k < 6; ++k) add(skill.aurastat[k], skill.aura_calc[k]);
        for (std::size_t k = 0; k < 5; ++k) add(skill.passive_stat[k], skill.passive_calc[k]);
        if (pct != 0) {
            pct = std::max(pct, -90);
            low += int(std::int64_t(low) * pct / 100); high += int(std::int64_t(high) * pct / 100);
        }
        element_range(string_id(game_data, 0x10a0), low, high);
    };
    const auto* missile1 = missile_row(game_data, skill.desc_missile[0]);
    switch (line.func) {
        case 1: {                                             // FUN_004e85c0: the mana cost
            if (skill.mana == 0 && skill.lvlmana == 0) break;
            const int mana = d2d::rules::mana_cost(skill, lvl);
            const std::string label = key_text(game_data, skill.str_mana);
            if (skill.manashift != 8 && (skill.lvlmana << (skill.manashift & 31)) <= 0x100 && mana <= 0x1900) {
                const int scale = calc_a > 10 ? calc_a : 10;  // FUN_004e7b90 tenths, FUN_004e7a90 by calc a
                const int whole = mana >> 8, part = (mana & 0xff) * scale >> 8;
                if (whole == 0 && part == 0) break;
                out.add(label); out.add(tenths(whole, part, false)); out.newline();
                break;
            }
            number_between(mana >> 8, label, "", false);
            break;
        }
        case 2: case 3: number_between(calc_a, text_a, text_b, line.func == 2); break;
        case 4: case 5: number_between(calc_a, text_a, "", line.func == 4); break;
        case 6: case 7: number_before(calc_a, text_a, line.func == 6); break;
        case 8:                                               // FUN_004ec510: the skill's attack rating %
            both();
            number_between(d2d::rules::skill_tohit(skill_tables, skill, env, lvl), string_id(game_data, 0x10a6), string_id(game_data, kPercentWord), true);
            break;
        case 9: both(); physical(calc_a, calc_b); break;
        case 10: both(); element({}); break;
        case 11: both(); length_line(); break;
        case 12: out.add(text_b); out.seconds(calc_a, text_a, false); break;
        case 13: {                                            // FUN_004ec090: the summon's average life + calc a % + calc b
            const int type = game_data.monsters.row(skill.summon);
            if (type < 0) break;
            const auto ranges = summon(type);
            const int life = (ranges.min_hp + ranges.max_hp) / 2;
            number_between(life + life * calc_a / 100 + calc_b, string_id(game_data, 0x10c6), "", false);
            break;
        }
        case 14: {                                            // FUN_004edca0: poison over its length
            both();
            const int length = d2d::rules::elem_length(skill_tables, skill, env, lvl);
            const int low = d2d::rules::elem_damage(skill_tables, skill, env, lvl, false, 0, true) * length >> 8;
            const int high = d2d::rules::elem_damage(skill_tables, skill, env, lvl, true, 0, true) * length >> 8;
            out.seconds(length, string_id(game_data, kOver), false);
            out.id(element_label(skill.etype)); out.add(range(low, high)); out.newline();
            break;
        }
        case 15: out.add(text_a); out.id(kColon); out.id(kSpace); out.add(text_b); out.newline(); break;
        case 16: {                                            // FUN_004e7650: "Duration: a-b seconds"
            const int low_whole = calc_a / 25, low_tenth = calc_a % 25 * 10 / 25, high_whole = calc_b / 25, high_tenth = calc_b % 25 * 10 / 25;
            if (low_whole == 0 && low_tenth == 0 && high_whole == 0) break;
            out.id(kDuration);
            out.add(low_tenth == 0 && high_tenth == 0 ? std::to_string(low_whole) + "-" + std::to_string(high_whole)
                                                      : tenths(low_whole, low_tenth, false) + "-" + tenths(high_whole, high_tenth, false));
            out.id(kSeconds); out.newline();
            break;
        }
        case 17: out.add(text_b); range_line(calc_a, calc_b, text_a, string_id(game_data, kPerSecond), false); break;
        case 18: coloured(text_a); break;
        case 19: out.add(text_b); yards(calc_a * 20 / 3, text_a); break;
        case 20: case 21:                                     // FUN_004e81c0: "<a>N percent<b>"
            if (calc_a == 0) break;
            out.add(text_a); out.add(signed_number(calc_a, line.func == 20)); out.id(kPercentWord); out.add(text_b); out.newline();
            break;
        case 22: {                                            // FUN_004ec620: missile 1's damage, 75 frames' worth, the skill's mastery in
            if (!missile1) break;
            std::int64_t low = std::int64_t(missile_elem(game_data, *missile1, env, skill.id, lvl, false)) * 75;
            std::int64_t high = std::int64_t(missile_elem(game_data, *missile1, env, skill.id, lvl, true)) * 75;
            if (env.stat && (skill.etype == 0 || skill.etype == 1)) {   // FUN_004e6ea0 / FUN_004e6f00: fire 329, lightning 330
                const int mastery = env.stat(d2d::d2s::kPassiveFireMastery + skill.etype);
                low += low * mastery / 100; high += high * mastery / 100;
            }
            average_line(element_label(missile1->etype), int(low >> 8), int(high >> 8));
            break;
        }
        case 23: case 30: {                                   // missile 1's (30: its SubMissile1's) life in frames, as seconds
            const auto* missile = line.func == 23 || !missile1 ? missile1 : missile_row(game_data, missile1->sub);
            if (!missile) return false;
            out.seconds(missile->range + missile->lev_range * lvl, text_a, false);
            break;
        }
        case 24: out.add(text_b); element(text_a); break;
        case 25: coloured(text_a + text_b); break;
        case 26: both(); average(element_label(skill.etype), 25); break;
        case 27: both(); average(0x10a1, 75); break;
        case 28:                                              // missile 1's Param1 as a radius
            if (!missile1) return false;
            yards(missile1->param1 * 20 / 3, string_id(game_data, 0x10ae));
            break;
        case 29:                                              // missile 1's life x 2 / 3 as yards
            if (!missile1) return false;
            number_before((missile1->range + missile1->lev_range * (lvl - 1)) * 2 / 3, string_id(game_data, kYards), false);
            break;
        case 31: {                                            // calc a over DifficultyLevels AiCurseDivisor, as seconds
            const int divisor = game_data.curse_divisor[std::size_t(std::clamp(game_data.difficulty, 0, 2))];
            out.seconds(divisor > 0 ? calc_a / divisor : calc_a, text_a, false);
            break;
        }
        case 32: out.add(text_a); number_between(calc_a, text_b, string_id(game_data, kPercentWord), true); break;
        case 33: if (lvl > 1) { out.add(text_a); out.add(text_b); out.newline(); } break;
        case 34: summon_damage(true); break;
        case 35:
            out.add(text_a); out.id(kColon); out.id(kSpace);
            out.add(std::to_string(calc_a) + "-" + std::to_string(calc_b)); out.newline();
            break;
        case 36:                                              // FUN_004e7db0: N, then a for 1 or b
            if (calc_a == 0) break;
            out.add(std::to_string(calc_a)); out.add(calc_a == 1 ? text_a : text_b); out.newline();
            break;
        case 37: yards(calc_a * 10 / 3, text_a); break;
        case 38: case 52: range_line(calc_a, calc_b, text_a, text_b, line.func == 52); break;
        case 39: summon_damage(false); break;
        case 40:                                              // "<a>" with <b> for %s, e.g. "X Receives Bonuses From:"
            if (text_a.empty()) return false;
            coloured(d2_format(text_a, { text_b }));
            break;
        case 41: {                                            // FUN_004ecf50: the Fire Golem's (MonStats 292) A1 damage + calc a - calc b
            if (game_data.monsters.row(skill.summon) < 0) return false;
            const auto golem = summon(game_data.monsters.row("firegolem"));
            const int low = golem.a1_min + calc_a, high = golem.a1_max + calc_b;
            if (skill.etype >= 0 && skill.etype < 5) element_range({}, low, high);
            break;
        }
        case 42:                                              // FUN_004ec900: "<a>: +calc a.calc b <b>"
            if (text_a.empty() || (calc_a == 0 && calc_b == 0)) break;
            out.add(text_a); out.id(kColon); out.id(kSpace); out.add(signed_number(calc_a, true) + "." + std::to_string(calc_b));
            out.id(kSpace); out.add(text_b); out.newline();
            break;
        case 43: {                                            // FUN_004e77a0: 256ths as tenths, "<a>N.N-M.M<b>"
            const auto [low, low_part] = fraction(calc_a, 10);
            const auto [high, high_part] = fraction(calc_b, 10);
            if (low == 0 && high == 0 && low_part == 0 && high_part == 0) break;
            out.add(text_a);
            out.add(low_part == 0 && high_part == 0 ? std::to_string(low) + "-" + std::to_string(high)
                                                    : std::to_string(low) + "." + std::to_string(low_part) + "-" + std::to_string(high) + "." + std::to_string(high_part));
            out.add(text_b); out.newline();
            break;
        }
        case 47: damage_range(calc_a, calc_b, text_a, false, false); break;
        case 48: {                                            // FUN_004eddd0: length, element, damage and attack rating
            length_line();
            element({});
            physical(0, 0);
            number_between(d2d::rules::skill_tohit(skill_tables, skill, env, lvl), string_id(game_data, 0x10a6), string_id(game_data, kPercentWord), true);
            break;
        }
        case 49:                                              // the skill's own damage, "<a> +N-M"
            damage_range(d2d::rules::skill_phys(skill_tables, skill, env, lvl, false) >> 8, d2d::rules::skill_phys(skill_tables, skill, env, lvl, true) >> 8, text_a, true, true);
            break;
        case 50: {                                            // FUN_004ec280: missile 1's damage under a
            if (!missile1) break;
            const int low = missile_elem(game_data, *missile1, env, skill.id, lvl, false) >> 8, high = missile_elem(game_data, *missile1, env, skill.id, lvl, true) >> 8;
            if (low != 0 || high != 0) element_range(text_a.empty() ? string_id(game_data, element_label(missile1->etype)) : text_a, low, high);
            break;
        }
        case 51: case 66: if (!text_a.empty()) { out.add(d2_format(text_a, { std::int64_t(calc_a) })); out.newline(); } break;
        case 53: case 54: case 55: case 56:                   // FUN_004ed3f0: the scroll / tome count, drawn on its own
            // FUN_004a9a90..: its search for the item ends in XOR EAX,EAX, so the count is always 0 (bugs.md).
            out.corner = d2_format(string_id(game_data, 0x1058), { std::int64_t(0) }) + "\n" + text_a;
            break;
        case 57: out.seconds(calc_a, text_a, true); break;
        case 58: case 62:
            if (calc_a == calc_b) { number_between(calc_a, text_a, text_b, false); break; }
            out.add(text_a); out.add(text_b); out.add((line.func == 58 ? "+" : "") + std::to_string(calc_a) + "-" + std::to_string(calc_b)); out.newline();
            break;
        case 59: out.add(text_b); if (calc_a != 0 || calc_b != 0) element_range(text_a, calc_a, calc_b); break;
        case 60: case 61: {                                   // FUN_004e7a90: calc a in 256ths, its fraction x (calc b or 10) >> 8
            if (calc_a == 0) break;
            const auto [whole, part] = fraction(calc_a, calc_b != 0 ? calc_b : 10);
            out.add(text_a);
            if (whole != 0 || part != 0) out.add(part == 0 ? signed_number(whole, line.func == 60) : signed_number(whole, line.func == 60) + "." + std::to_string(part));
            out.add(text_b); out.newline();
            break;
        }
        case 63: bonus(true); break;
        case 67: bonus(false); break;
        case 68: if (calc_a != 0) { out.add(std::to_string(calc_a)); out.add(text_a); out.add(text_b); out.newline(); } break;   // FUN_004e7ce0
        case 70: out.add(text_a); number_between(calc_a, text_b, "", true); break;
        case 71:                                              // "<a>: " then b as a format with calc a
            if (text_a.empty() || text_b.empty()) return false;
            out.add(text_a); out.id(kColon); out.id(kSpace); out.add(d2_format(text_b, { std::int64_t(calc_a) })); out.newline();
            break;
        case 72: case 73:                                     // "[+]a/b <a>"
            if (calc_a == 0 || calc_b < 1) return false;
            out.add(signed_number(calc_a, line.func == 72)); out.id(0xfa0); out.add(std::to_string(calc_b)); out.id(kSpace); out.add(text_a); out.newline();
            break;
        default: return false;   // ponytail: 44..46, 64, 65, 69, 74, 75: no SkillDesc line uses them
    }
    return true;
}

// Turns game.exe's string into lines top to bottom, each in the colour it
// starts in (one change mid-line kept, TextLine's split).
inline std::vector<TextLine> to_lines(const std::string& text) {
    static constexpr std::array<std::array<std::uint8_t, 3>, 10> kColour{ { kTxtWhite, kTxtRed, kTxtGreen, kTxtBlue, kTxtGold, kTxtGrey,
                                                                           { 0, 0, 0 }, kTxtGold, kTxtOrange, kTxtYellow } };
    std::vector<TextLine> lines;
    auto colour = kTxtWhite;
    TextLine line{ {}, colour };
    bool started = false;
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '\xff' && at + 2 < text.size() && text[at + 1] == 'c' && text[at + 2] >= '0' && text[at + 2] <= '9') {
            colour = kColour[std::size_t(text[at + 2] - '0')];
            if (!started) line.rgb = colour;
            else if (line.split == std::string::npos) { line.split = line.text.size(); line.tail = colour; }
            at += 2;
            continue;
        }
        if (text[at] == '\n') { lines.push_back(std::move(line)); line = TextLine{ {}, colour }; started = false; continue; }
        line.text.push_back(text[at]);
        started = true;
    }
    lines.push_back(std::move(line));
    while (!lines.empty() && lines.back().text.empty()) lines.pop_back();
    std::vector<TextLine> top_down(lines.rbegin(), lines.rend());
    while (!top_down.empty() && top_down.back().text.empty()) top_down.pop_back();
    return top_down;
}

// `env` with the SkillDesc missiles' calc operands (desc_missile_operand).
inline d2d::rules::CalcEnv with_missiles(const GameData& game_data, const d2d::rules::CalcEnv& env) {
    auto with = env;
    auto base = std::make_shared<const d2d::rules::CalcEnv>(env);  // one copy both operands share
    with.desc_missile = [&game_data, base](const d2d::rules::Skill& skill, int code, int lvl) { return desc_missile_operand(game_data, *base, skill, code, lvl); };
    with.missile = [&game_data, shared = std::move(base)](int skill, int missile, int code, int lvl) { return miss_operand(game_data, *shared, skill, missile, code, lvl); };
    return with;
}

}  // namespace skill_text

// The skill tree's tooltip (FUN_004eef00) for `skill` at `lvl` (its level
// with item bonuses; 0 unlearned): the synergies (lines 10..16 at the next
// level), the next level's lines unless `maxed` ("Next Level", "First
// Level" from 0), the current level's ("Current Skill Level: N"), lines
// 6..9, the name in green and the description. `item_granted` (a level
// with no points of its own, FUN_006442a0 flag 1 vs 0) adds " (item)" to the
// level and "You have been granted this skill by an item"; "You have not
// learned this skill yet" shows while `learned` is false, which game.exe
// (FUN_004ed480) takes as no points in any of the class's skills.
inline std::vector<TextLine> skill_tree_tip(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool maxed,
                                            bool item_granted, bool learned) {
    using namespace skill_text;
    const auto* skill = game_data.skills.get(skill_id);
    if (!skill) return {};
    Builder out{ game_data, {}, {} };
    const auto line_env = with_missiles(game_data, env);
    const auto block = [&](int from, int end, int step, int at_level) {
        bool any = false;
        for (int index = from; index != end; index += step)
            any = desc_line(out, game_data.skills, *skill, line_env, at_level, skill->desc_lines[std::size_t(index)], 0) || any;
        return any;
    };
    if (block(16, 9, -1, lvl + 1)) out.newline();
    if (!maxed && block(0, 6, 1, lvl + 1)) { out.id(lvl > 0 ? 0x109d : 0x10ad); out.newline(); out.newline(); }
    if (lvl > 0 && block(0, 6, 1, lvl)) {
        out.id(0x109e); out.add(std::to_string(lvl));
        if (item_granted) out.id(0x1112);
        out.newline(); out.newline();
    }
    if (block(6, 10, 1, lvl + 1)) out.newline();
    out.add(key_text(game_data, skill->str_long)); out.newline();
    out.colour(2); out.add(key_text(game_data, skill->str_name));
    if (item_granted) { out.newline(); out.newline(); out.id(0x1113); }
    if (!learned) { out.newline(); out.newline(); out.id(0x1114); }
    return to_lines(out.text);
}

// The skill buttons' tooltip (FUN_004ef990): lines 0..9 at `lvl`, "Current
// Skill Level: N", the short description and the name. A scroll's or
// tome's count goes to `corner` (FUN_004ed3f0's own box).
inline std::vector<TextLine> skill_button_tip(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int lvl,
                                              std::vector<TextLine>* corner = nullptr) {
    using namespace skill_text;
    const auto* skill = game_data.skills.get(skill_id);
    if (!skill) return {};
    Builder out{ game_data, {}, {} };
    const auto line_env = with_missiles(game_data, env);
    bool any = false;
    for (std::size_t index = 0; index < 10; ++index) any = desc_line(out, game_data.skills, *skill, line_env, lvl, skill->desc_lines[index], 0) || any;
    if (any) { out.id(0x109e); out.add(std::to_string(lvl)); out.newline(); out.newline(); }
    out.add(key_text(game_data, skill->str_short)); out.newline();
    out.add(key_text(game_data, skill->str_name));
    if (corner) *corner = out.corner.empty() ? std::vector<TextLine>{} : to_lines(out.corner);
    return to_lines(out.text);
}

}  // namespace d2d::game
