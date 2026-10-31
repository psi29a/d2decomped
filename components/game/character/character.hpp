// SPDX-License-Identifier: GPL-3.0-or-later
// The character the World plays: what a save holds (header, stats,
// items, corpse), its look, and what the char panel computes from them.
// The client's CharCreateUI (ui.hpp) adds the create screen's controls.
#pragma once

#include "gamedata.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::game {

// Is item type `t` (or an Equiv ancestor) `want`?
inline bool type_is(const GameData& game_data, const std::string& type, std::string_view want) {
    return d2d::rules::type_is(game_data.rules, type, want);
}

// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour).
inline std::vector<d2d::d2s::ItemProp> socket_props(const GameData& game_data, const d2d::d2s::Item& parent,
                                             const d2d::d2s::Item& filled) {
    auto out = filled.props;
    const auto found = game_data.gem_props.find(filled.code);
    const auto info = game_data.rules.item_info.find(parent.code);
    if (found == game_data.gem_props.end() || info == game_data.rules.item_info.end()) return out;
    const int slot_kind = info->second.kind == 2 ? 0 : type_is(game_data, info->second.type, "shld") ? 2 : 1;
    const auto& add = found->second[std::size_t(slot_kind)];
    out.insert(out.end(), add.begin(), add.end());
    return out;
}

// A worn set item's bonus lists that are on: list i (aprop(i+1)a / b,
// FUN_0065fec0 kind 4) with i + 2 pieces of its set worn.
// ponytail: add func 1 lists (by which other piece is worn) count pieces
// too.
inline std::vector<d2d::d2s::ItemProp> set_bonus_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items,
                                                const d2d::d2s::Item& item) {
    std::vector<d2d::d2s::ItemProp> out;
    const auto& sets = game_data.rules.sets;
    auto set_of = [&](const d2d::d2s::Item& piece) {
        return piece.quality == 5 && piece.set_id >= 0 && std::size_t(piece.set_id) < sets.size() ? sets[std::size_t(piece.set_id)].set : -1;
    };
    const int set = set_of(item);
    if (set < 0 || item.set_props.empty()) return out;
    std::vector<int> pieces;
    for (const auto& piece : items)
        if (piece.location == d2d::d2s::item_location::kEquipped && piece.slot >= d2d::d2s::body_location::kFirst && piece.slot <= d2d::d2s::body_location::kLast && set_of(piece) == set && std::ranges::find(pieces, piece.set_id) == pieces.end())
            pieces.push_back(piece.set_id);
    std::size_t offset = 0, list = 0;
    for (int bit = 0; bit < 5; ++bit) {
        if (!(item.set_lists >> bit & 1) || list >= item.set_list_sizes.size()) continue;
        const std::size_t size = std::min(item.set_list_sizes[list++], item.set_props.size() - offset);
        if (int(pieces.size()) >= bit + 2) out.insert(out.end(), item.set_props.begin() + std::ptrdiff_t(offset), item.set_props.begin() + std::ptrdiff_t(offset + size));
        offset += size;
    }
    return out;
}

// Sets.txt's bonuses for the sets worn (FUN_00663b40 -> FUN_00660120: a
// stat list per set on the player): with n of its pieces worn, the
// partial bonuses 2a, 2b .. na, nb (at most pieces - 1's); all of them
// worn, the full bonuses FCode1..8 too. Set item rows count as pieces
// once each.
// ponytail: a ranged bonus takes its min (every 1.14d row has min == max).
inline std::vector<d2d::d2s::ItemProp> set_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items) {
    std::vector<d2d::d2s::ItemProp> out;
    const auto& rules = game_data.rules;
    std::vector<std::vector<int>> worn(rules.set_bonuses.size());
    for (const auto& item : items)
        if (item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast
            && item.quality == 5 && item.set_id >= 0 && std::size_t(item.set_id) < rules.sets.size() && !d2d::rules::broken(item))
            if (const int set = rules.sets[std::size_t(item.set_id)].set; set >= 0 && std::size_t(set) < worn.size() && !std::ranges::contains(worn[std::size_t(set)], item.set_id))
                worn[std::size_t(set)].push_back(item.set_id);
    d2d::rules::Rng rng;
    for (std::size_t set = 0; set < worn.size(); ++set) {
        const int count = int(worn[set].size());
        if (count < 2) continue;
        const auto& bonus = rules.set_bonuses[set];
        const int partial = std::min(count, bonus.pieces - 1) * 2 - 2;
        for (int index = 0; index < partial && std::size_t(index) < bonus.partial.size(); ++index)
            if (!bonus.partial[std::size_t(index)].code.empty()) {
                auto mod = bonus.partial[std::size_t(index)]; mod.max = mod.min;
                d2d::rules::apply_mod(rules, mod, out, rng);
            }
        if (count >= bonus.pieces)
            for (auto mod : bonus.full)
                if (!mod.code.empty()) { mod.max = mod.min; d2d::rules::apply_mod(rules, mod, out, rng); }
    }
    return out;
}

// An item whose stats reach the character: worn (slots 1..10, the weapon
// set in use) or an inventory charm, and not broken.
inline bool gear_active(const d2d::d2s::Item& item) {
    const bool worn = item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast;
    const bool charm = item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && (item.code == "cm1" || item.code == "cm2" || item.code == "cm3");
    return (worn || charm) && !d2d::rules::broken(item);
}

// Everything the character's gear gives: worn items (slots 1..10), what's
// socketed in them, their set bonuses on, inventory charms.
inline std::vector<d2d::d2s::ItemProp> gear_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items) {
    std::vector<d2d::d2s::ItemProp> out;
    for (const auto& item : items) {
        if (!gear_active(item)) continue;
        out.insert(out.end(), item.props.begin(), item.props.end());
        for (const auto& socketed : item.socketed_items) {
            const auto filled = socket_props(game_data, item, socketed);
            out.insert(out.end(), filled.begin(), filled.end());
        }
        const auto bonus = set_bonus_props(game_data, items, item);
        out.insert(out.end(), bonus.begin(), bonus.end());
    }
    const auto sets = set_props(game_data, items);
    out.insert(out.end(), sets.begin(), sets.end());
    return out;
}

// A skill an item's charges give (stat 204, item_charged_skill: param
// skill << 6 | level, value max << 8 | left). game.exe keeps one skill
// entry per item and skill (SKILLS_AddOrRemoveItemSkill, keyed by the
// item's unit id, skill entry +0x34), added by the stat hook while the
// item's stats reach its owner and it has charges left, removed at 0.
struct ChargedSkill {
    int item = -1;                         // the holder's Item::id (its socketed items' charges are its own, FUN_0065c820)
    int skill = -1, level = 0, left = 0, max = 0;
    friend bool operator==(const ChargedSkill&, const ChargedSkill&) = default;
};
[[nodiscard]] inline ChargedSkill charged_skill(int item_id, const d2d::d2s::ItemProp& prop) {
    return { item_id, prop.param >> 6, prop.param & 63, prop.value & 255, prop.value >> 8 };
}
// The character's charged skill entries, in item order: each active item's
// (gear_active) and its socketed items' stat 204 with a level of 1..63
// (FUN_0065c820) and charges left.
inline std::vector<ChargedSkill> charged_skills(const std::vector<d2d::d2s::Item>& items) {
    std::vector<ChargedSkill> out;
    const auto add = [&](int item_id, const d2d::d2s::Item& holder) {
        for (const auto& prop : holder.props)
            if (const auto entry = charged_skill(item_id, prop); prop.stat == d2d::d2s::kChargedSkill && entry.level >= 1 && entry.left > 0
                && std::ranges::none_of(out, [&](const ChargedSkill& had) { return had.item == item_id && had.skill == entry.skill; }))
                out.push_back(entry);
    };
    for (const auto& item : items) {
        if (!gear_active(item)) continue;
        add(item.id, item);
        for (const auto& socketed : item.socketed_items) add(item.id, socketed);
    }
    return out;
}

// A cast from charged entry `entry` spends one charge (FUN_0056bec0, the
// skill's cost in place of mana, FUN_0056bfe0): the stat on the entry's
// item, else on an item socketed in it (FUN_0065c820), goes to max << 8 |
// max(left - 1, 0). False when nothing is left to spend.
inline bool use_charge(std::vector<d2d::d2s::Item>& items, const ChargedSkill& entry) {
    const auto spend = [&](d2d::d2s::Item& holder) {
        for (auto& prop : holder.props)
            if (const auto had = charged_skill(entry.item, prop); prop.stat == d2d::d2s::kChargedSkill && had.skill == entry.skill && had.level == entry.level && had.left > 0) {
                prop.value = had.max << 8 | std::min(had.left - 1, had.max);
                return true;
            }
        return false;
    };
    const auto item = std::ranges::find(items, entry.item, &d2d::d2s::Item::id);
    if (entry.item < 0 || item == items.end()) return false;
    return spend(*item) || std::ranges::any_of(item->socketed_items, spend);
}

// The char panel's computed values (stat 30 next level, 31 defence,
// resistances 39/43/41/45), as FUN_004a7d00 shows them. From the save's
// base stats and gear.
// Equipped slots 1..10: the weapon set in use (a switch moves its hands
// into 4 / 5, weapon-switch.md).
// ponytail: socket bonuses, set bonus lists (set_bonus_props) and charms,
// the passives with no weapon type (Iron Skin's defense %, Natural Resistance); no auras.
struct PanelStats {
    std::int64_t next = -1, defense = 0;
    std::array<std::int64_t, 4> res{};           // fire, cold, lightning, poison
    std::array<std::int64_t, 4> res_cap{ 75, 75, 75, 75 };   // 75 + max resist, at most 95: gold at it
    // What items and passives add to the character's own stats 0..11
    // (strength .. max stamina, whole points): the panel shows the sum,
    // blue when it's more, red when less (FUN_004a7d00).
    std::array<std::int64_t, 12> bonus{};
    // item_maxhp_percent / item_maxmana_percent (76, 77; ItemStatCost op 11):
    // a share of the own maximum plus the items' flat +life / +mana, not of
    // the attributes' (op 8) share. Fight::update_fighters adds it.
    std::array<std::int64_t, 2> max_percent{}, max_flat{};
    std::array<d2d::rules::AttackLine, 2> attack{};   // the left / right skill's block, as the server sends it
};

inline PanelStats panel_stats(const GameData& game_data, const d2d::d2s::Header& header,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& stats,
                       const std::vector<d2d::rules::PassiveStat>* passives = nullptr) {
    PanelStats panel;
    const auto lvl = stats.get(d2d::d2s::kLevel);
    if (lvl >= 0 && std::size_t(lvl) + 1 < game_data.exp_next.size()) panel.next = game_data.exp_next[std::size_t(lvl)];
    std::array<std::int64_t, d2d::d2s::kMaxManaPercent + 1> sum{};
    auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& prop : props) if (prop.stat >= 0 && std::size_t(prop.stat) < sum.size()) sum[std::size_t(prop.stat)] += prop.value;
    };
    std::int64_t item_def = 0, per_level = 0;
    for (const auto& item : items) {
        const bool worn = item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast;
        const bool charm = item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory
                        && (item.code == "cm1" || item.code == "cm2" || item.code == "cm3");
        if ((!worn && !charm) || d2d::rules::broken(item)) continue;
        add(item.props);
        for (const auto& socketed : item.socketed_items) add(socket_props(game_data, item, socketed));
        const auto bonus = set_bonus_props(game_data, items, item);
        add(bonus);
        for (const auto& prop : bonus) if (prop.stat == d2d::d2s::kArmorPerLevel) per_level += prop.value;
        std::int64_t enhanced_defense = 0;
        for (const auto& prop : item.props) {
            if (prop.stat == d2d::d2s::kArmorPercent) enhanced_defense += prop.value;            // item_armor_percent: this item's base
            if (prop.stat == d2d::d2s::kArmorPerLevel) per_level += prop.value;    // item_armor_perlevel, 1/8 per level
        }
        if (item.defense > 0) item_def += item.defense * (100 + enhanced_defense) / 100;
    }
    add(set_props(game_data, items));
    std::int64_t skill_def = 0;                           // 171 skill_armor_percent
    if (passives) for (const auto& passive : *passives) {
        if (!passive.itype.empty()) continue;
        if (passive.stat >= 0 && passive.stat < 64) sum[std::size_t(passive.stat)] += passive.value;
        if (passive.stat == d2d::d2s::kSkillArmorPercent) skill_def += passive.value;
    }
    for (std::size_t i = 0; i < 12; ++i) panel.bonus[i] = sum[i];
    panel.max_percent = { sum[d2d::d2s::kMaxLifePercent], sum[d2d::d2s::kMaxManaPercent] };
    panel.max_flat = { sum[d2d::d2s::kMaxLife], sum[d2d::d2s::kMaxMana] };
    {                                                     // the attributes' share of life, stamina, mana (quarter points)
        const auto& gains = game_data.class_gains[std::size_t(header.cls % 7)];
        panel.bonus[d2d::d2s::kMaxLife] += sum[d2d::d2s::kVit] * gains.life_per_vit / 4;
        panel.bonus[d2d::d2s::kMaxStamina] += sum[d2d::d2s::kVit] * gains.stamina_per_vit / 4;
        panel.bonus[d2d::d2s::kMaxMana] += sum[d2d::d2s::kEne] * gains.mana_per_energy / 4;
    }
    panel.defense = item_def + sum[d2d::d2s::kArmorClass] + per_level * lvl / 8 + (stats.get(d2d::d2s::kDex) + sum[d2d::d2s::kDex]) / 4;
    panel.defense += panel.defense * skill_def / 100;
    const int diff = header.active_difficulty();
    const std::int64_t penalty = header.expansion() ? game_data.resist_penalty[std::size_t(diff)]
                                               : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(diff)];
    constexpr int kRes[4] = { 39, 43, 41, 45 };
    for (int i = 0; i < 4; ++i) {
        const auto cap = std::min<std::int64_t>(75 + sum[std::size_t(kRes[i] + 1)], 95);
        panel.res[std::size_t(i)] = std::clamp<std::int64_t>(sum[std::size_t(kRes[i])] + penalty, -100, cap);
        panel.res_cap[std::size_t(i)] = cap;
    }
    return panel;
}

struct Character {
    int character_class = -1;    // the save's class (0 Amazon .. 6 Assassin, d2s order), -1 none yet
    // Gear the in-game character wears: a loaded save's appearance bytes,
    // or unset for a fresh character (starting gear).
    std::optional<std::array<std::uint8_t, 32>> appearance;   // + the tints
    std::vector<d2d::d2s::Item> items;   // a loaded save's items
    d2d::d2s::Stats stats;               // ... and attributes
    std::vector<d2d::d2s::Item> corpse;  // ... and its corpse's items (d2s corpse list)
    std::vector<d2d::d2s::Item> merc_items;   // ... and what its merc wears (d2s "jf" list)
    PanelStats panel;                    // ... and what the char panel computes
    bool expansion = true;               // the save's expansion flag (stash size)
    d2d::d2s::Header header;             // the loaded save's header (quest flags ...)
    std::string name;                    // the character's name
};

}  // namespace d2d::game
