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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

// One SkillDesc line (FUN_004edea0): true when it wrote something.
// ponytail: formats 13, 22, 23, 28..31, 33..37, 39, 41..50, 53..56, 60, 61,
// 64, 65, 68, 69 and 71..75 (about 60 of SkillDesc's 1170 lines: summon
// life, missile ranges, charge-ups, auras' averages) aren't ported and write
// nothing; the calcs' missile operands read 0 (rules::calc_operand).
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
        if (value == 0) return false;
        out.add(before); out.add(signed_number(value, plus)); out.add(after); out.newline();
        return true;
    };
    const auto number_before = [&](int value, const std::string& after, bool plus) {
        if (value == 0) return false;
        out.add(signed_number(value, plus)); out.add(after); out.newline();
        return true;
    };
    // FUN_004e7f40 / FUN_004e8020: a range with texts.
    const auto range_line = [&](int low, int high, const std::string& before, const std::string& after, bool plus) {
        if (low == high) return number_between(low, before, after, false);
        out.add(before); out.add((plus ? "+" : "") + std::to_string(low) + "-" + std::to_string(high)); out.add(after); out.newline();
        return true;
    };
    // FUN_004ec220 -> FUN_004ec140: the skill's elemental damage (mastery in), "<label>N-M".
    const auto element = [&](std::uint16_t label) {
        const int low = d2d::rules::elem_damage(skill_tables, skill, env, lvl, false, 0, true) >> 8;
        const int high = d2d::rules::elem_damage(skill_tables, skill, env, lvl, true, 0, true) >> 8;
        if (low == 0 && high == 0) return false;
        out.id(label != 0 ? label : element_label(skill.etype)); out.add(range(low, high)); out.newline();
        return true;
    };
    // FUN_004ec740 / FUN_004ec540: "Average <label>N-M per second", 25 frames' worth.
    const auto average = [&](std::uint16_t label) {
        const int low = d2d::rules::elem_damage(skill_tables, skill, env, lvl, false, 0, true) * 25 >> 8;
        const int high = d2d::rules::elem_damage(skill_tables, skill, env, lvl, true, 0, true) * 25 >> 8;
        if (low == 0 && high == 0) return false;
        out.id(kAverage); out.id(label); out.add(range(low, high)); out.id(kPerSecond); out.newline();
        return true;
    };
    // FUN_004ec7e0: "<a>: +N[%] <b>".
    const auto bonus = [&](bool percent) {
        if (calc_a == 0) return false;
        if (!text_a.empty()) { out.add(text_a); out.id(kColon); out.id(kSpace); }
        out.add(signed_number(calc_a, true));
        if (percent) out.id(kPercent);
        out.id(kSpace); out.add(text_b); out.newline();
        return true;
    };
    // case 18 / 25 / 40: a block of text in colour calc a, then a newline in the line's own colour.
    const auto coloured = [&](std::string block) {
        if (calc_a > 0) block = "\xff" "c" + std::string(1, char('0' + calc_a)) + block;
        out.add(block);
        out.colour(line_colour); out.newline();
        return true;
    };
    switch (line.func) {
        case 1: {                                             // FUN_004e85c0: the mana cost
            if (skill.mana == 0 && skill.lvlmana == 0) return false;
            const int mana = d2d::rules::mana_cost(skill, lvl);
            const std::string label = key_text(game_data, skill.str_mana);
            if (skill.manashift != 8 && (skill.lvlmana << (skill.manashift & 31)) <= 0x100 && mana <= 0x1900) {
                const int scale = calc_a > 10 ? calc_a : 10;  // FUN_004e7b90 tenths, FUN_004e7a90 by calc a
                const int whole = mana >> 8, part = (mana & 0xff) * scale >> 8;
                if (whole == 0 && part == 0) return false;
                out.add(label); out.add(tenths(whole, part, false)); out.newline();
                return true;
            }
            return number_between(mana >> 8, label, "", false);
        }
        case 2: case 3: return number_between(calc_a, text_a, text_b, line.func == 2);
        case 4: case 5: return number_between(calc_a, text_a, "", line.func == 4);
        case 6: case 7: return number_before(calc_a, text_a, line.func == 6);
        case 8: {                                             // FUN_004ec510: the skill's attack rating %
            both();
            return number_between(d2d::rules::skill_tohit(skill_tables, skill, env, lvl), string_id(game_data, 0x10a6), string_id(game_data, kPercentWord), true);
        }
        case 9: {                                             // FUN_004ec2e0: its own damage, + calc a % + calc b
            both();
            const int low = d2d::rules::skill_phys(skill_tables, skill, env, lvl, false) >> 8, high = d2d::rules::skill_phys(skill_tables, skill, env, lvl, true) >> 8;
            const int min = low + low * calc_a / 100 + calc_b, max = high + high * calc_a / 100 + calc_b;
            if (min == 0 && max == 0) return false;
            out.id(0x10a0); out.add(min == max ? signed_number(min, true) : std::to_string(min) + "-" + std::to_string(max)); out.newline();
            return true;
        }
        case 10: both(); element(0); return true;
        case 11: {                                            // FUN_004edc40: cold / poison length
            both();
            const int length = d2d::rules::elem_length(skill_tables, skill, env, lvl);
            if (length != 0 && skill.etype == 2) out.seconds(length, string_id(game_data, 0x10a9), false);
            if (length != 0 && skill.etype == 3) out.seconds(length, string_id(game_data, 0x10aa), false);
            return true;
        }
        case 12: out.add(text_b); out.seconds(calc_a, text_a, false); return true;
        case 14: {                                            // FUN_004edca0: poison over its length
            both();
            const int length = d2d::rules::elem_length(skill_tables, skill, env, lvl);
            const int low = d2d::rules::elem_damage(skill_tables, skill, env, lvl, false, 0, true) * length >> 8;
            const int high = d2d::rules::elem_damage(skill_tables, skill, env, lvl, true, 0, true) * length >> 8;
            out.seconds(length, string_id(game_data, kOver), false);
            out.id(element_label(skill.etype)); out.add(range(low, high)); out.newline();
            return true;
        }
        case 15: out.add(text_a); out.id(kColon); out.id(kSpace); out.add(text_b); out.newline(); return true;
        case 16: {                                            // FUN_004e7650: "Duration: a-b seconds"
            const int low_whole = calc_a / 25, low_tenth = calc_a % 25 * 10 / 25, high_whole = calc_b / 25, high_tenth = calc_b % 25 * 10 / 25;
            if (low_whole == 0 && low_tenth == 0 && high_whole == 0) return false;
            out.id(kDuration);
            out.add(low_tenth == 0 && high_tenth == 0 ? std::to_string(low_whole) + "-" + std::to_string(high_whole)
                                                      : tenths(low_whole, low_tenth, false) + "-" + tenths(high_whole, high_tenth, false));
            out.id(kSeconds); out.newline();
            return true;
        }
        case 17: out.add(text_b); return range_line(calc_a, calc_b, text_a, string_id(game_data, kPerSecond), false);
        case 18: return coloured(text_a);
        case 19: {                                            // FUN_004e82a0: a radius in yards (subtiles x 2/3)
            out.add(text_b);
            const int value = calc_a * 20 / 3, whole = std::max(value / 10, 1), tenth = value % 10;
            out.add(text_a); out.add(tenths(whole, tenth, false));
            out.id(whole == 1 && tenth == 0 ? kYard : kYards); out.newline();
            return true;
        }
        case 20: case 21:                                     // FUN_004e81c0: "<a>N percent<b>"
            if (calc_a == 0) return false;
            out.add(text_a); out.add(signed_number(calc_a, line.func == 20)); out.id(kPercentWord); out.add(text_b); out.newline();
            return true;
        case 24: out.add(text_b); return element(0) || true;   // ponytail: text a's label isn't a number id here; the element's own
        case 25: return coloured(text_a + text_b);
        case 26: both(); average(element_label(skill.etype)); return true;
        case 27: both(); average(0x10a1); return true;
        case 32: out.add(text_a); return number_between(calc_a, text_b, string_id(game_data, kPercentWord), true);
        case 38: case 52: return range_line(calc_a, calc_b, text_a, text_b, line.func == 52);
        case 40: {                                            // "<a>" with <b> for %s, e.g. "X Receives Bonuses From:"
            if (text_a.empty()) return false;
            return coloured(d2_format(text_a, { text_b }));
        }
        case 51: if (text_a.empty()) return false; out.add(d2_format(text_a, { std::int64_t(calc_a) })); out.newline(); return true;
        case 57: return out.seconds(calc_a, text_a, true);
        case 58: case 62:
            if (calc_a == calc_b) return number_between(calc_a, text_a, text_b, false);
            out.add(text_a); out.add(text_b); out.add((line.func == 58 ? "+" : "") + std::to_string(calc_a) + "-" + std::to_string(calc_b)); out.newline();
            return true;
        case 59: {                                            // FUN_004ec140: calc a - calc b under the element's label
            out.add(text_b);
            if (calc_a == 0 && calc_b == 0) return false;
            out.add(text_a.empty() ? string_id(game_data, element_label(skill.etype)) : text_a); out.add(range(calc_a, calc_b)); out.newline();
            return true;
        }
        case 63: return bonus(true);
        case 66: if (text_a.empty()) return false; out.add(d2_format(text_a, { std::int64_t(calc_a) })); out.newline(); return true;
        case 67: return bonus(false);
        case 70: out.add(text_a); return number_between(calc_a, text_b, "", true);
        default: return false;
    }
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

}  // namespace skill_text

// The skill tree's tooltip (FUN_004eef00) for `skill` at `lvl` (its level
// with item bonuses; 0 unlearned): the synergies (lines 10..16 at the next
// level), the next level's lines unless `maxed` ("Next Level", "First
// Level" from 0), the current level's ("Current Skill Level: N"), lines
// 6..9, the name in green and the description, and "You have not learned
// this skill yet" when it has no points.
// ponytail: not the item-granted lines (0x1112 " (item)", 0x1113).
inline std::vector<TextLine> skill_tree_tip(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool maxed, bool learned) {
    using namespace skill_text;
    const auto* skill = game_data.skills.get(skill_id);
    if (!skill) return {};
    Builder out{ game_data, {} };
    const auto block = [&](int from, int end, int step, int at_level) {
        bool any = false;
        for (int index = from; index != end; index += step)
            any = desc_line(out, game_data.skills, *skill, env, at_level, skill->desc_lines[std::size_t(index)], 0) || any;
        return any;
    };
    if (block(16, 9, -1, lvl + 1)) out.newline();
    if (!maxed && block(0, 6, 1, lvl + 1)) { out.id(lvl > 0 ? 0x109d : 0x10ad); out.newline(); out.newline(); }
    if (lvl > 0 && block(0, 6, 1, lvl)) { out.id(0x109e); out.add(std::to_string(lvl)); out.newline(); out.newline(); }
    if (block(6, 10, 1, lvl + 1)) out.newline();
    out.add(key_text(game_data, skill->str_long)); out.newline();
    out.colour(2); out.add(key_text(game_data, skill->str_name));
    if (!learned) { out.newline(); out.newline(); out.id(0x1114); }
    return to_lines(out.text);
}

// The skill buttons' tooltip (FUN_004ef990): lines 0..9 at `lvl`, "Current
// Skill Level: N", the short description and the name.
inline std::vector<TextLine> skill_button_tip(const GameData& game_data, const d2d::rules::CalcEnv& env, int skill_id, int lvl) {
    using namespace skill_text;
    const auto* skill = game_data.skills.get(skill_id);
    if (!skill) return {};
    Builder out{ game_data, {} };
    bool any = false;
    for (std::size_t index = 0; index < 10; ++index) any = desc_line(out, game_data.skills, *skill, env, lvl, skill->desc_lines[index], 0) || any;
    if (any) { out.id(0x109e); out.add(std::to_string(lvl)); out.newline(); out.newline(); }
    out.add(key_text(game_data, skill->str_short)); out.newline();
    out.add(key_text(game_data, skill->str_name));
    return to_lines(out.text);
}

}  // namespace d2d::game
