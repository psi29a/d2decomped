// Item text: an item's hover lines in D2's quality colours, its property
// lines, D2's string formatting. The World names ground items with them;
// the client draws them (items.hpp).
#pragma once

#include "character.hpp"
#include "gamedata.hpp"

namespace d2d::game {

// An item's hover text, top line first, in D2's quality colours. Name IDs
// resolve as verified against 19 real saves (test data): unique/set ID =
// UniqueItems/SetItems row without "Expansion" separators; magic
// prefix/suffix ID = MagicPrefix/MagicSuffix raw data row (row 0 is the
// blank "none"); rare names = RarePrefix row (id - 156) + RareSuffix row
// (id - 1); runeword ID = rank of its RunewordN + 27.
// ponytail: no requirements, durability, set bonus lines or weapon damage.
struct TextLine { std::string text; std::array<std::uint8_t, 3> rgb; };

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

inline std::vector<TextLine> item_lines(const GameData& game_data, const d2d::d2s::Item& item, int clvl) {
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
    if (item.defense >= 0) {
        // Shown with the item's own +% and flat defence applied (16, 31).
        std::int64_t enhanced_defense = 0, flat = 0;
        for (const auto& prop : item.props) {
            if (prop.stat == 16) enhanced_defense += prop.value;
            if (prop.stat == 31) flat += prop.value;
            if (prop.stat == 214) flat += prop.value * clvl / 8;         // armor per level
        }
        const auto def = std::int64_t(item.defense) * (100 + enhanced_defense) / 100 + flat;
        out.push_back({ str("ItemStats1h") + " " + std::to_string(def), def != item.defense ? kTxtBlue : kTxtWhite });
    }
    if (item.max_durability > 0 && !d2d::rules::indestructible(item))
        out.push_back({ str("ItemStats1d") + " " + std::to_string(item.durability) + " " + str("ItemStats1j") + " "
                        + std::to_string(d2d::rules::max_durability(item)), kTxtWhite });
    if (item.quantity >= 0) out.push_back({ str("ItemStats1i") + " " + std::to_string(item.quantity), kTxtWhite });
    if (unid) {
        const auto unidentified = str("ItemStats1b");                // "Unidentified"
        out.push_back({ unidentified.empty() || unidentified == "ItemStats1b" ? "Unidentified" : unidentified, kTxtRed });
        return out;
    }
    auto props = item.props;
    for (const auto& socketed : item.socketed_items) {
        const auto gem_props = socket_props(game_data, item, socketed);
        props.insert(props.end(), gem_props.begin(), gem_props.end());
    }
    for (auto& line : prop_lines(game_data, std::move(props), clvl)) out.push_back({ std::move(line), kTxtBlue });
    if (item.ethereal) out.push_back({ "Ethereal", kTxtBlue });
    if (item.socketed) out.push_back({ "Socketed (" + std::to_string(item.sockets) + ")", kTxtBlue });
    return out;
}

}  // namespace d2d::game
