// SPDX-License-Identifier: GPL-3.0-or-later
// Item text: an item's hover lines in D2's quality colours, its property
// lines, D2's string formatting. The World names ground items with them;
// the client draws them (items.hpp).
#pragma once

#include "character.hpp"
#include "gamedata.hpp"

#include <d2s_items.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// An item's hover text, top line first, in D2's quality colours. Name IDs
// resolve as verified against 19 real saves (test data): unique/set ID =
// UniqueItems/SetItems row without "Expansion" separators; magic
// prefix/suffix ID = MagicPrefix/MagicSuffix raw data row (row 0 is the
// blank "none"); rare names = RarePrefix row (id - 156) + RareSuffix row
// (id - 1); runeword ID = rank of its RunewordN + 27.
// A line can change colour once, at `split` (game.exe's mid-line "\xffc3"
// before a modified number): text[split..] is drawn in `tail`.
struct TextLine {
    std::string text;
    std::array<std::uint8_t, 3> rgb;
    std::size_t split = std::string::npos;
    std::array<std::uint8_t, 3> tail{};
};

// D2's printf subset in its strings: %d, %+d, %s, %%. Args in order.
inline std::string d2_format(std::string_view format, std::initializer_list<std::variant<std::int64_t, std::string>> args) {
    std::string out;
    auto arg = args.begin();
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%' || i + 1 == format.size()) { out += format[i]; continue; }
        if (format[i + 1] == '%') { out += '%'; ++i; continue; }
        const bool plus = format[i + 1] == '+';
        const std::size_t after = i + (plus ? 2 : 1);
        if (after >= format.size() || (format[after] != 'd' && format[after] != 's' && format[after] != 'i')) { out += format[i]; continue; }
        if (arg != args.end()) {
            if (const auto* number = std::get_if<std::int64_t>(&*arg)) out += (plus && *number >= 0 ? "+" : "") + std::to_string(*number);
            else out += std::get<std::string>(*arg);
            ++arg;
        }
        i = after;
    }
    return out;
}

// An item's property list as the tooltip's text lines, by ItemStatCost
// descfunc (the column semantics the game's item-description code uses),
// highest descpriority first. dgrp groups (all resistances, all
// attributes) collapse into one line when every member is present with
// the same value; min/max damage pairs become "Adds X-Y ..." and 17/18
// "+X% Enhanced Damage", as the game hard-codes them.
// ponytail: no descfunc 17/18 (time-of-day), 22/23 (monster types);
// charges/skill lines use the skill's string key.
inline std::vector<std::string> prop_lines(const GameData& game_data, std::vector<d2d::d2s::ItemProp> props, int clvl) {
    auto str = [&](std::string_view key) {
        if (key.empty()) return std::string{};
        const auto found = lookup_string(game_data, key);
        std::string text = found ? u16_to_latin1(*found) : std::string(key);
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
        return text;
    };
    auto desc = [&](int stat) -> const GameData::StatDesc* {
        return stat >= 0 && std::size_t(stat) < game_data.stat_desc.size() ? &game_data.stat_desc[std::size_t(stat)] : nullptr;
    };
    auto skill = [&](int id) {
        return id >= 0 && std::size_t(id) < game_data.skill_name.size() ? str(game_data.skill_name[std::size_t(id)]) : std::string{};
    };
    // One stat list, like the unit's: repeats of a (stat, param) add up.
    {
        std::vector<d2d::d2s::ItemProp> merged;
        for (const auto& prop : props) {
            auto merged_prop = std::ranges::find_if(merged, [&](const auto& other) { return other.stat == prop.stat && other.param == prop.param; });
            if (merged_prop != merged.end() && prop.stat != 204) merged_prop->value += prop.value;      // 204: charges don't add
            else merged.push_back(prop);
        }
        props = std::move(merged);
    }
    struct Out { int prio; std::string text; };
    std::vector<Out> out;
    auto value_of = [&](int stat) -> std::optional<std::int64_t> {
        for (const auto& prop : props) if (prop.stat == stat) return prop.value;
        return std::nullopt;
    };
    std::unordered_set<int> done;
    // Hard-coded pairs.
    auto range = [&](int low, int high, const char* range_key, const char* single_key, int prio, int len_stat = -1) {
        const auto low_value = value_of(low), high_value = value_of(high);
        if (!low_value || !high_value) return;
        std::int64_t x = *low_value, y = *high_value;
        std::optional<std::int64_t> secs;
        if (len_stat >= 0) {
            const auto len = value_of(len_stat).value_or(0);
            x = x * len / 256; y = y * len / 256; secs = len / 25;
            done.insert(len_stat);
        }
        std::string line_text = x == y && single_key ? (secs ? d2_format(str(single_key), { x, *secs })
                                                     : d2_format(str(single_key), { x }))
                      : secs ? d2_format(str(range_key), { x, y, *secs }) : d2_format(str(range_key), { x, y });
        out.push_back({ prio, std::move(line_text) });
        done.insert(low); done.insert(high);
    };
    range(21, 22, "strModMinDamageRange", nullptr, 127);
    // Two-handed/thrown copies of min/max damage only show without the base.
    if (value_of(21)) { done.insert(23); done.insert(159); }
    if (value_of(22)) { done.insert(24); done.insert(160); }
    range(48, 49, "strModFireDamageRange", "strModFireDamage", 102);
    range(50, 51, "strModLightningDamageRange", "strModLightningDamage", 99);
    range(52, 53, "strModMagicDamageRange", "strModMagicDamage", 104);
    range(54, 55, "strModColdDamageRange", "strModColdDamage", 96);
    if (done.contains(54)) done.insert(56);
    range(57, 58, "strModPoisonDamageRange", "strModPoisonDamage", 92, 59);
    if (const auto min_damage = value_of(17), max_damage = value_of(18); min_damage && max_damage && *min_damage == *max_damage) {
        out.push_back({ 130, "+" + std::to_string(*min_damage) + "% " + str("strModEnhancedDamage") });
        done.insert(17); done.insert(18);
    }
    // dgrp groups.
    std::unordered_map<int, std::vector<int>> groups;
    for (std::size_t i = 0; i < game_data.stat_desc.size(); ++i)
        if (game_data.stat_desc[i].dgrp) groups[game_data.stat_desc[i].dgrp].push_back(int(i));
    for (const auto& [group, members] : groups) {
        std::optional<std::int64_t> value;
        bool same = true;
        for (int member : members) {
            const auto x = value_of(member);
            if (!x || (value && *value != *x)) { same = false; break; }
            value = x;
        }
        if (!same || !value) continue;
        const auto& stat_desc = *desc(members.front());
        const std::string first = str(*value < 0 ? stat_desc.dgrp_neg : stat_desc.dgrp_pos);
        std::string line_text;
        switch (stat_desc.dgrp_func) {
            case 1: case 6: case 12: line_text = stat_desc.dgrp_val == 2 ? first + " " + d2_format("%+d", { *value }) : d2_format("%+d", { *value }) + " " + first; break;
            case 3: case 9: line_text = stat_desc.dgrp_val == 2 ? first + " " + std::to_string(*value) : std::to_string(*value) + " " + first; break;
            case 4: case 8: line_text = stat_desc.dgrp_val == 2 ? first + " " + d2_format("%+d%%", { *value }) : d2_format("%+d%%", { *value }) + " " + first; break;
            case 19: line_text = d2_format(first, { *value }); break;
            default: line_text = d2_format(first, { *value }); break;
        }
        out.push_back({ stat_desc.prio, std::move(line_text) });
        for (int member : members) done.insert(member);
    }
    for (const auto& prop : props) {
        if (done.contains(prop.stat)) continue;
        const auto* stat_desc = desc(prop.stat);
        if (!stat_desc || stat_desc->func == 0) continue;
        std::int64_t value = prop.value;
        // Per-level stats (op 2..5 with op param): value * clvl >> param.
        if (stat_desc->operation >= 2 && stat_desc->operation <= 5 && stat_desc->op_param > 0) value = value * clvl >> stat_desc->op_param;
        const std::string first = str(value < 0 ? stat_desc->neg : stat_desc->pos), second = str(stat_desc->str2);
        auto place = [&](const std::string& num) {
            if (stat_desc->val == 0) return first;
            return stat_desc->val == 2 ? first + " " + num : num + " " + first;
        };
        auto with2 = [&](std::string line_text) { return second.empty() ? line_text : line_text + " " + second; };
        std::string line_text;
        switch (stat_desc->func) {
            case 1: case 12: line_text = place(d2_format("%+d", { value })); break;
            case 2: line_text = place(std::to_string(value) + "%"); break;
            case 3: line_text = place(std::to_string(value)); break;
            case 4: line_text = place(d2_format("%+d%%", { value })); break;
            case 5: line_text = place(std::to_string(value * 100 / 128) + "%"); break;
            case 6: line_text = with2(place(d2_format("%+d", { value }))); break;
            case 7: line_text = with2(place(std::to_string(value) + "%")); break;
            case 8: line_text = with2(place(d2_format("%+d%%", { value }))); break;
            case 9: line_text = with2(place(std::to_string(value))); break;
            case 10: line_text = with2(place(std::to_string(value * 100 / 128) + "%")); break;
            case 11: line_text = d2_format(first, { std::int64_t(1), value ? 100 / value : 0 }); break;
            case 13: line_text = d2_format("%+d", { value }) + " " + (prop.param >= 0 && prop.param < 7
                         ? str(game_data.class_strs[std::size_t(prop.param)].all_skills) : std::string{}); break;
            case 14: {
                const int cls = prop.param >> 3, tab = prop.param & 7;
                if (cls < 0 || cls >= 7 || tab > 2) break;
                const auto& class_strings = game_data.class_strs[std::size_t(cls)];
                line_text = d2_format(str(class_strings.tab[tab]), { value }) + " " + str(class_strings.only);
                break;
            }
            case 15: line_text = d2_format(first, { value, std::int64_t(prop.param & 63), skill(prop.param >> 6) }); break;
            case 16: line_text = d2_format(first, { value, skill(prop.param) }); break;
            case 19: line_text = d2_format(first, { value }); break;
            case 20: line_text = place(std::to_string(-value) + "%"); break;
            case 21: line_text = place(std::to_string(-value)); break;
            case 24:                                       // descstr is just "(%d/%d Charges)"
                line_text = "Level " + std::to_string(prop.param & 63) + " " + skill(prop.param >> 6) + " "
                  + d2_format(first, { value & 255, value >> 8 });
                break;
            case 27: {
                const int skill_class = std::size_t(prop.param) < game_data.skill_class.size() ? game_data.skill_class[std::size_t(prop.param)] : -1;
                line_text = d2_format("%+d", { value }) + " to " + skill(prop.param)
                  + (skill_class >= 0 ? " " + str(game_data.class_strs[std::size_t(skill_class)].only) : "");
                break;
            }
            case 28: line_text = d2_format("%+d", { value }) + " to " + skill(prop.param); break;
            default: break;
        }
        if (!line_text.empty()) out.push_back({ stat_desc->prio, std::move(line_text) });
    }
    std::ranges::stable_sort(out, [](const Out& left, const Out& right) { return left.prio > right.prio; });
    std::vector<std::string> lines;
    for (auto& line : out) lines.push_back(std::move(line.text));
    return lines;
}
constexpr std::array<std::uint8_t, 3> kTxtWhite{ 255, 255, 255 }, kTxtBlue{ 105, 105, 255 },
    kTxtGreen{ 0, 255, 0 }, kTxtGold{ 199, 179, 119 }, kTxtYellow{ 255, 255, 100 },
    kTxtOrange{ 255, 168, 0 }, kTxtGrey{ 105, 105, 105 }, kTxtRed{ 255, 77, 77 };

// FUN_0062efb0 (D2Common's transaction cost), for buying (sell == false,
// the vendor's price) and selling (the vendor pays). Base = the item's
// cost (items record +0xe0), plus per quality (mult, add) extras:
// low quality -base/2, magic prefix + suffix, rare/crafted its six
// affixes, set/unique their cost mult/add (extra = base*mult/1024 + add);
// + half the cost of each socketed item (FUN_006292f0); ethereal sells
// at a quarter. Then npc.txt: * buy/sell mult / 1024 and each questflag's
// mult when that quest is done; * quantity; selling caps at "max buy"
// for the difficulty.
// ponytail: no charges/books/ammo branches, automagic affix, durability
// or the reduced-prices stat.

// game.exe's attack speed bands (DAT_00721f10, FUN_004861d0): row speed
// 10..27 (frames, below), column by class and bow/crossbow (0x722078);
// 1 Very Fast .. 5 Very Slow. Under 10 is 1, 28 and over 5.
inline constexpr std::array<std::uint8_t, 90> kSpeedBand{
    1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  1, 1, 2, 1, 1,  2, 1, 2, 2, 1,  2, 1, 2, 2, 2,
    2, 2, 3, 2, 2,  3, 2, 3, 3, 2,  3, 2, 3, 3, 3,  3, 2, 4, 3, 3,  4, 3, 4, 4, 3,  4, 3, 4, 4, 4,
    4, 3, 5, 4, 4,  5, 4, 5, 5, 4,  5, 4, 5, 5, 5,  5, 4, 5, 5, 5,  5, 5, 5, 5, 5,  5, 5, 5, 5, 5 };
inline constexpr std::uint8_t kSpeedColumn[7][2] = { { 0, 2 }, { 1, 4 }, { 1, 4 }, { 0, 3 }, { 0, 3 }, { 1, 4 }, { 0, 3 } };

// An item's required level (FUN_0062b5b0): its affixes' / set item's /
// unique's levelreq (crafted: the highest affix + 10 + 3 per affix, at
// most 98), at least the base's and each socketed item's, plus
// item_levelreq (stat 92); never below 0.
// ponytail: no classlevelreq, automagic affix or the charged / oskill
// skills' levels (stats 97, 107).
inline int required_level(const GameData& game_data, const d2d::d2s::Item& item) {
    auto at = [](const std::vector<int>& levels, int index) { return index >= 0 && std::size_t(index) < levels.size() ? levels[std::size_t(index)] : 0; };
    int level = 0;
    switch (item.quality) {
        case 4: level = std::max(at(game_data.prefix_req, item.prefix), at(game_data.suffix_req, item.suffix)); break;
        case 5: level = at(game_data.set_req, item.set_id); break;
        case 7: level = at(game_data.unique_req, item.unique_id); break;
        case 6: case 8: {
            int count = 0;
            for (std::size_t i = 0; i < 6; ++i)
                if (item.affixes[i] > 0) { level = std::max(level, at(i % 2 == 0 ? game_data.prefix_req : game_data.suffix_req, item.affixes[i])); ++count; }
            if (item.quality == 8) level = std::min(level + 10 + 3 * count, 98);
            break;
        }
        default: break;
    }
    if (const auto info = game_data.rules.item_info.find(item.code); info != game_data.rules.item_info.end()) level = std::max(level, info->second.req_lvl);
    for (const auto& socketed : item.socketed_items) level = std::max(level, required_level(game_data, socketed));
    for (const auto& prop : item.props) if (prop.stat == 92) level += int(prop.value);
    return std::max(level, 0);
}

// An item's hover text as the client's FUN_0048dd90 (UI\inv.cpp) builds
// it. The string is drawn bottom-up, so its segments, top line first:
// name (FUN_0048c060), defense (FUN_00485ee0), block (FUN_00485be0),
// smite/kick (FUN_00485d40), damage (FUN_00485410), quantity / spelldesc
// (FUN_00486100 / FUN_00486370), charm, socket filler (FUN_004865d0),
// durability (FUN_00484e90), class only, required dexterity / strength /
// level (FUN_00485170 / FUN_004850a0 / FUN_00484ff0, red when unmet,
// FUN_0062eaf0), weapon speed (FUN_004861d0), "Unidentified" (red),
// properties (blue, FUN_004e6410), ethereal / sockets (blue, FUN_00484b10).
// A number the item's mods changed is blue. `wearer` is the hovering
// player (class, strength, dexterity, level); without one there are no
// red requirements, class bonuses, speed or spelldesc lines, as game.exe
// without a player unit. docs/research/re/item-names.md.
// ponytail: no set bonus lists, Holy Shield's block / smite, time-of-day
// damage (272/273), the gold quest-item line (FUN_00486670) or
// throwing-potion damage.
inline std::vector<TextLine> item_lines(const GameData& game_data, const d2d::d2s::Item& item, int clvl,
                                        const d2d::rules::Wearer* wearer = nullptr) {
    auto str = [&](std::string_view key) {
        if (key.empty()) return std::string{};
        const auto found = lookup_string(game_data, key);
        return found ? u16_to_latin1(*found) : std::string(key);
    };
    auto name_at = [](const std::vector<std::string>& names, int index) -> std::string_view {
        return index >= 0 && std::size_t(index) < names.size() ? std::string_view(names[std::size_t(index)]) : std::string_view{};
    };
    const auto& names = game_data.item_names;
    const auto info = game_data.rules.item_info.find(item.code);
    const std::string base = str(info != game_data.rules.item_info.end() && !info->second.namestr.empty()
                                     ? std::string_view(info->second.namestr) : std::string_view(item.code));
    std::vector<TextLine> out;
    auto two = [&](std::string name, std::array<std::uint8_t, 3> colour) {
        if (name.empty()) { out.push_back({ base, colour }); return; }
        out.push_back({ std::move(name), colour });
        out.push_back({ base, colour });
    };
    auto join = [](std::string first, const std::string& second) {
        if (first.empty()) return second;
        if (second.empty()) return first;
        return first + " " + second;
    };
    if (item.runeword) {
        const int rank = item.runeword_id == 2718 ? 48 - 27 : item.runeword_id - 27;   // 2718: Delirium's odd ID
        out.push_back({ str(name_at(names.runeword, rank)), kTxtGold });
        out.push_back({ base, kTxtGrey });
    } else switch (item.quality) {
        case 1: {
            static constexpr const char* kLow[] = { "Crude", "Cracked", "Damaged", "Low Quality" };
            out.push_back({ join(str(item.qsub >= 0 && item.qsub < 4 ? kLow[item.qsub] : ""), base), kTxtWhite });
            break;
        }
        case 3: out.push_back({ join(str("Hiquality"), base), kTxtWhite }); break;
        case 4: out.push_back({ join(join(str(name_at(names.prefix, item.prefix)), base), str(name_at(names.suffix, item.suffix))),
                                kTxtBlue }); break;
        case 5: two(str(name_at(names.set, item.set_id)), kTxtGreen); break;
        case 7: two(str(name_at(names.unique, item.unique_id)), kTxtGold); break;
        case 6: case 8:
            two(join(str(name_at(names.rare_pre, item.rare1 - 156)), str(name_at(names.rare_suf, item.rare2 - 1))),
                item.quality == 6 ? kTxtYellow : kTxtOrange);
            break;
        default: out.push_back({ base, item.ethereal || item.socketed ? kTxtGrey : kTxtWhite }); break;
    }
    // Unidentified magic and better: the base name in its colour, no properties.
    const bool unid = !item.identified && item.quality >= 4 && !item.runeword;
    if (unid) out = { { base, out.front().rgb } };
    if (item.personalized && !item.owner.empty()) out.front().text = item.owner + "'s " + out.front().text;
    if (info == game_data.rules.item_info.end()) return out;

    // The item's stat list: its own and its sockets' (game.exe links them).
    auto props = item.props;
    for (const auto& socketed : item.socketed_items) {
        const auto gem_props = socket_props(game_data, item, socketed);
        props.insert(props.end(), gem_props.begin(), gem_props.end());
    }
    auto stat = [&](int id) {
        std::int64_t sum = 0;
        for (const auto& prop : props) if (prop.stat == id) sum += prop.value;
        return sum;
    };
    auto has = [&](int id) { return std::ranges::any_of(props, [&](const auto& prop) { return prop.stat == id && prop.value; }); };
    auto line = [](std::string head, std::string tail, bool blue) {
        TextLine text_line{ head + tail, kTxtWhite };
        if (blue) { text_line.split = head.size(); text_line.tail = kTxtBlue; }
        return text_line;
    };
    const auto& type = info->second.type;
    const auto is = [&](std::string_view want) { return d2d::rules::type_is(game_data.rules, type, want); };
    const auto base_row = game_data.rules.item_base.find(item.code);
    const d2d::rules::ItemBase no_base{};
    const auto& item_base = base_row != game_data.rules.item_base.end() ? base_row->second : no_base;
    const auto desc_row = game_data.item_desc.find(item.code);
    const GameData::ItemDesc no_desc{};
    const auto& desc = desc_row != game_data.item_desc.end() ? desc_row->second : no_desc;
    const bool weapon = is("weap"), armor = is("armo"), throwable = game_data.rules.types.contains(type) && game_data.rules.types.at(type).throwable;
    const int cls = wearer && wearer->cls >= 0 && wearer->cls < 7 ? wearer->cls : -1;
    if (wearer) clvl = wearer->lvl;
    const std::string space = " ", to = space + string_id(game_data, 3464) + space;            // "to"

    // Defense: armorclass with its +% (16, on the base) and flat adds.
    if (armor && item.defense >= 0) {
        const auto def = std::int64_t(item.defense) * (100 + stat(16)) / 100 + stat(31) + stat(214) * clvl / 8;
        if (def > 0) out.push_back(line(string_id(game_data, 3461) + space, std::to_string(def), def != item.defense));
    }
    // Chance to block (shields): the item's toblock (the block column +
    // 20s) plus the class's BlockFactor, at most 75; blue over the column.
    if (is("shld")) {
        std::int64_t block = item_base.block + stat(20);
        if (cls >= 0) block += game_data.class_gains[std::size_t(cls)].block;
        block = std::min<std::int64_t>(block, 75);
        if (block) out.push_back(line(string_id(game_data, 11018), std::to_string(block) + "%", block > item_base.block));
    }
    // Smite (a Paladin's shield) / kick damage (an Assassin's boots): the armor.txt mindam / maxdam.
    const std::string class_code = d2d::rules::type_class(game_data.rules, type);
    if ((is("shld") && cls == d2d::d2s::kPaladin && (class_code.empty() || class_code == "pal")) || (is("boot") && cls == d2d::d2s::kAssassin))
        out.push_back({ string_id(game_data, is("shld") ? 3468 : 21782) + space + std::to_string(item_base.mindam) + to + std::to_string(item_base.maxdam), kTxtWhite });
    // Damage: one-hand (21/22), two-hand (23/24) and throw (159/160) from
    // the record (FUN_0062d300 sets them: low quality 3/4, ethereal 3/2),
    // the +% (18 min, 17 max) on that base, then the flat adds.
    if (weapon) {
        auto base_damage = [&](std::size_t index) {
            int value = desc.dam[index];
            if (item.quality == 1 && value) value = std::max(value * 3 / 4, index % 2 ? 2 : 1);
            if (item.ethereal) value = value * 3 / 2;
            return value;
        };
        auto damage = [&](int kind, bool max_over_min, int label, bool blue_mods) {
            static constexpr int kStat[3][2] = { { 21, 22 }, { 23, 24 }, { 159, 160 } };
            const std::int64_t low_base = base_damage(std::size_t(kind) * 2), high_base = base_damage(std::size_t(kind) * 2 + 1);
            const std::int64_t low = low_base * (100 + stat(18)) / 100 + stat(kStat[kind][0]);
            std::int64_t high = high_base * (100 + stat(17) + stat(219) * clvl / 8) / 100 + stat(kStat[kind][1]) + stat(218) * clvl / 8;
            high = std::max(high, max_over_min ? low + 1 : low);
            const bool blue = low_base < low || high_base < high || blue_mods;
            return line(string_id(game_data, std::uint16_t(label)) + space, std::to_string(low) + to + std::to_string(high), blue);
        };
        if (throwable && desc.dam[5]) out.push_back(damage(2, false, 3467, has(17) || has(18) || has(159) || has(160)));
        if (cls == d2d::d2s::kBarbarian && info->second.one_or_two) {
            out.push_back(damage(0, false, 3465, false));
            out.push_back(damage(1, false, 3466, false));
        } else {
            out.push_back(info->second.two_handed ? damage(1, true, 3466, false) : damage(0, true, 3465, false));
        }
    }
    // Quantity (stat 70; anything that stacks), unless unidentified or
    // socketed; a misc.txt spelldesc takes its place (FUN_00486370).
    {
        std::string text;
        if (item.identified && !item.socketed && (item.quantity > 0 || item_base.max_stack > 0))
            text = string_id(game_data, 3462) + space + std::to_string(std::max(item.quantity, 0));
        if (wearer && desc.spell_desc >= 1 && desc.spell_desc <= 4 && !desc.spell_str.empty()) {
            std::int64_t value = desc.spell_calc;
            if (desc.spell_desc == 2 && cls >= 0) {
                // Potions by class (FUN_0062a5d0 life, FUN_0062a620 mana): x1.5 or x2.
                if (desc.spell_stat == 6 || desc.spell_stat == 74) value = d2d::rules::potion_bonus(value, cls, true);
                if (desc.spell_stat == 8 || desc.spell_stat == 26) value = d2d::rules::potion_bonus(value, cls, false);
            }
            const std::string spell = str(desc.spell_str);
            text = desc.spell_desc == 1 ? spell : desc.spell_desc == 4 ? d2_format(spell, { value }) : spell + space + std::to_string(value);
        }
        if (!text.empty()) out.push_back({ text, kTxtWhite });
    }
    if (is("char")) out.push_back({ string_id(game_data, 20438), kTxtWhite });            // "Keep in Inventory to Gain Bonus"
    // Socket fillers (FUN_004e6850): a gem's or rune's gems.txt bonus per
    // kind, each kind's label before its top line (FUN_004e6410).
    if (is("sock")) {
        out.push_back({ string_id(game_data, 11080), kTxtWhite });                            // "Can be Inserted into Socketed Items"
        if (const auto gem = game_data.gem_props.find(item.code); gem != game_data.gem_props.end() && (is("gem") || is("rune"))) {
            out.push_back({ std::string{}, kTxtWhite });
            static constexpr std::pair<int, std::size_t> kKinds[4] = { { 11075, 0 }, { 11076, 1 }, { 11073, 1 }, { 11074, 2 } };
            for (const auto& [label, kind] : kKinds) {
                auto kind_lines = prop_lines(game_data, gem->second[kind], clvl);
                if (kind_lines.empty()) kind_lines.emplace_back();
                kind_lines.front() = string_id(game_data, std::uint16_t(label)) + space + kind_lines.front();
                for (auto& kind_line : kind_lines) out.push_back({ std::move(kind_line), kTxtWhite });
            }
        }
    }
    // Durability (FUN_00629930: a durability column, not nodurability, not
    // indestructible; not throwing weapons): blue max when 75 changed it.
    if (item_base.durability && !desc.nodurability && item.max_durability > 0 && !d2d::rules::indestructible(item) && !throwable)
        out.push_back(line(string_id(game_data, 3457) + space + std::to_string(item.durability) + space + string_id(game_data, 3463) + space,
                           std::to_string(d2d::rules::max_durability(item)), has(75)));
    auto requirement = [&](std::string text, bool met) { out.push_back({ std::move(text), met ? kTxtWhite : kTxtRed }); };
    for (int index = 0; index < 7; ++index)
        if (class_code == d2d::rules::kClassCode[std::size_t(index)]) requirement(string_id(game_data, std::uint16_t(10917 + index)), cls < 0 || cls == index);
    // Strength / dexterity (weapons and armor): the column, item_req_percent
    // (91) on it, 10 less when ethereal.
    if (weapon || armor) {
        auto required = [&](int column) { return column + (stat(91) ? column * stat(91) / 100 : 0) - (item.ethereal ? 10 : 0); };
        const auto dex = required(info->second.req_dex), strength = required(info->second.req_str);
        if (info->second.req_dex && dex > 0) requirement(string_id(game_data, 3459) + space + std::to_string(dex), !wearer || (wearer->dex > 0 && wearer->dex >= dex));
        if (info->second.req_str && strength > 0) requirement(string_id(game_data, 3458) + space + std::to_string(strength), !wearer || (wearer->str > 0 && wearer->str >= strength));
    }
    if (const int level = required_level(game_data, item); item.identified && level > 1)
        requirement(string_id(game_data, 3469) + space + std::to_string(level), !wearer || wearer->lvl >= level);
    // Weapon speed (FUN_0062a710): the class's A1 frames << 8 over the
    // animation speed x (100 + IAS (93) - WSM) / 100, banded; class name first.
    if (weapon && cls >= 0) {
        std::string upper = desc.wclass;
        for (auto& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        const auto anim = game_data.anim_data.find(std::string(kCharCode[cls]) + "A1" + upper);
        const std::int64_t rate = anim != game_data.anim_data.end() ? (100 + stat(93) - item_base.speed) * std::int64_t(anim->second.speed) / 100 : 0;
        const std::int64_t frames = rate > 0 ? (std::int64_t(anim->second.frames) << 8) / rate : 45;
        const int band = frames >= 28 ? 5 : frames < 10 ? 1
                       : kSpeedBand[std::size_t(frames * 5 - 50 + kSpeedColumn[cls][is("bow") || is("xbow") ? 1 : 0])];
        static constexpr std::pair<std::string_view, int> kClassName[] = { { "staf", 4085 }, { "axe", 4078 }, { "swor", 4079 }, { "knif", 4080 },
            { "tpot", 4081 }, { "jave", 4082 }, { "spea", 4083 }, { "bow", 4084 }, { "pole", 4086 }, { "xbow", 4087 }, { "h2h", 21258 },
            { "h2h2", 21258 }, { "orb", 4085 }, { "wand", 4085 }, { "blun", 4077 } };
        std::string head;
        for (const auto& [want, id] : kClassName)
            if (is(want)) { head = string_id(game_data, std::uint16_t(id)) + space + string_id(game_data, 3996) + space; break; }
        out.push_back(line(head, string_id(game_data, std::uint16_t(4088 + band)), has(93)));
    }
    if (unid) out.push_back({ string_id(game_data, 3455), kTxtRed });                         // "Unidentified"
    else for (auto& prop_line : prop_lines(game_data, std::move(props), clvl)) out.push_back({ std::move(prop_line), kTxtBlue });
    if ((weapon || armor) && (item.ethereal || item.socketed)) {
        std::string text = item.ethereal ? string_id(game_data, 22745) : std::string{};          // "Ethereal (Cannot be Repaired)"
        if (item.socketed) text += (item.ethereal ? ", " : "") + string_id(game_data, 3453) + " (" + std::to_string(item.sockets) + ")";
        out.push_back({ text, kTxtBlue });
    }
    return out;
}
}  // namespace d2d::game
