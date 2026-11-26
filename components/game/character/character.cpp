// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for character.hpp.
#include "character.hpp"

#include "gamedata.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <item_gen.hpp>
#include <item_price.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <set_list.hpp>
#include <skills.hpp>
#include <wear.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::game {

bool type_is(const GameData& game_data, const std::string& type, std::string_view want) {
    return d2d::rules::type_is(game_data.rules, type, want);
}

std::vector<d2d::d2s::ItemProp> socket_props(const GameData& game_data, const d2d::d2s::Item& parent,
                                             const d2d::d2s::Item& filled) {
    return d2d::rules::socket_props(game_data.rules, parent, filled);
}

// FUN_00663a20 (ECX item, EDX FUN_0062a370's mask of its set's members worn,
// itself in it): add func (SetItems +0x87) 1 toggles list j or j - 1 (past
// its own index, +0x2e) by mask bit j, j 0..5; 2 turns on the first
// 0x6ee440[mask] - 1 lists (a popcount table, 0 from 0x40 up), the rest off.
// The lists are the states at 0x6ee424 (0xa5..0xa9), toggled by
// FUN_006279a0 (flag 0x2000 = off). FUN_00663cc0 runs it on stats on, and
// FUN_0055dbc0 redoes every worn set piece, so a worn piece's lists follow
// what's worn (combat.md "A set item's own lists"; tools/emu/set_item_lists.py).
// ponytail: a piece taken off keeps its lists as last set (FUN_00663cc0
// (.., 1, 0) skips FUN_00663a20); d2d recomputes them from the others worn,
// which only the tooltip of an unworn piece shows. Not traced client-side.
std::vector<d2d::d2s::ItemProp> set_bonus_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items,
                                                const d2d::d2s::Item& item) {
    std::vector<d2d::d2s::ItemProp> out;
    const auto& sets = game_data.rules.sets;
    auto set_of = [&](const d2d::d2s::Item& piece) {
        return piece.quality == 5 && piece.set_id >= 0 && std::size_t(piece.set_id) < sets.size() ? sets[std::size_t(piece.set_id)].set : -1;
    };
    const int set = set_of(item);
    if (set < 0 || item.set_props.empty()) return out;
    unsigned worn = 0;                                   // members worn, by place (flags 0x4000 / 0x100 left out)
    for (const auto& piece : items)
        if (piece.location == d2d::d2s::item_location::kEquipped && piece.slot >= d2d::d2s::body_location::kFirst && piece.slot <= d2d::d2s::body_location::kLast
            && set_of(piece) == set && (piece.flags & 0x4100u) == 0 && sets[std::size_t(piece.set_id)].member >= 0)
            worn |= 1u << sets[std::size_t(piece.set_id)].member;
    const auto& row = sets[std::size_t(item.set_id)];
    const unsigned others = row.member >= 0 ? worn & ~(1u << row.member) : worn;
    std::size_t offset = 0, list = 0;
    for (int bit = 0; bit < 5; ++bit) {
        if (!(item.set_lists >> bit & 1) || list >= item.set_list_sizes.size()) continue;
        const std::size_t size = std::min(item.set_list_sizes[list++], item.set_props.size() - offset);
        const int place = bit < row.member ? bit : bit + 1;
        const bool active = row.add_func == 1 ? (others >> place & 1) != 0 : row.add_func == 2 && std::popcount(worn) >= bit + 2;
        if (active) out.insert(out.end(), item.set_props.begin() + std::ptrdiff_t(offset), item.set_props.begin() + std::ptrdiff_t(offset + size));
        offset += size;
    }
    return out;
}

// FUN_00663b40 -> FUN_00660120 builds a set's list with the piece going on
// (set_list.cpp), and FUN_0065fe10 hands that piece (ESI) to the
// Properties funcs as their item (FUN_0065fd70): dmg-min / dmg-max
// (FUN_0065ecf0 / FUN_0065ee80) on all three hands for a piece that isn't a
// weapon (Hsarus' FCode1, Death's Disguise's belt or gloves), the weapon's
// own for one (Death's Touch: one-hand). Sets.txt uses no func that writes
// the item (2 / 4's defence, 13, 14, 19, 20, 23), so a copy goes in.
std::vector<d2d::d2s::ItemProp> set_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items, int only) {
    std::vector<d2d::d2s::ItemProp> out;
    const auto& rules = game_data.rules;
    d2d::rules::Rng rng;
    for (std::size_t set = 0; set < rules.set_bonuses.size(); ++set) {
        if (only >= 0 && int(set) != only) continue;
        const int count = d2d::rules::set_pieces_worn(rules, items, int(set));
        const auto* worn_piece = count > 0 ? d2d::rules::set_list_piece(rules, items, int(set)) : nullptr;
        if (!worn_piece) continue;
        auto piece = *worn_piece;
        const auto base = rules.item_base.find(piece.code);
        const auto info = rules.item_info.find(piece.code);
        const auto type = info != rules.item_info.end() ? rules.types.find(info->second.type) : rules.types.end();
        const d2d::rules::ModItem target{ &piece, base != rules.item_base.end() ? &base->second : nullptr,
                                          info != rules.item_info.end() ? info->second.kind : 0,
                                          type != rules.types.end() && type->second.throwable, false };
        const auto& bonus = rules.set_bonuses[set];
        for (const int row : d2d::rules::set_list_rows(bonus, count))
            d2d::rules::apply_mod(rules, row < 8 ? bonus.partial[std::size_t(row)] : bonus.full[std::size_t(row - 8)], out, rng,
                                  target.base ? &target : nullptr);
    }
    return out;
}

bool gear_active(const d2d::d2s::Item& item) {
    const bool worn = item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast;
    const bool charm = item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && (item.code == "cm1" || item.code == "cm2" || item.code == "cm3")
        && !item.charm_short;
    return (worn || charm) && d2d::rules::item_usable(item);
}

std::vector<d2d::d2s::ItemProp> gear_props(const GameData& game_data, const std::vector<d2d::d2s::Item>& items) {
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

ChargedSkill charged_skill(int item_id, const d2d::d2s::ItemProp& prop) {
    return { item_id, prop.param >> 6, prop.param & 63, prop.value & 255, prop.value >> 8 };
}

std::vector<ChargedSkill> charged_skills(const std::vector<d2d::d2s::Item>& items) {
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

bool use_charge(std::vector<d2d::d2s::Item>& items, const ChargedSkill& entry) {
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

PanelStats panel_stats(const GameData& game_data, const d2d::d2s::Header& header,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& stats,
                       const std::vector<d2d::rules::PassiveStat>* passives) {
    PanelStats panel;
    const auto lvl = stats.get(d2d::d2s::kLevel);
    if (lvl >= 0 && std::size_t(lvl) + 1 < game_data.exp_next.size()) panel.next = game_data.exp_next[std::size_t(lvl)];
    std::array<std::int64_t, d2d::d2s::kMaxManaPercent + 1> sum{};
    auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& prop : props) if (prop.stat >= 0 && std::size_t(prop.stat) < sum.size()) sum[std::size_t(prop.stat)] += prop.value;
    };
    std::int64_t item_def = 0, per_level = 0;
    for (const auto& item : items) {
        if (!gear_active(item)) continue;                 // worn, or a charm meeting its requirements
        add(item.props);
        for (const auto& socketed : item.socketed_items) add(socket_props(game_data, item, socketed));
        const auto bonus = set_bonus_props(game_data, items, item);
        add(bonus);
        for (const auto& prop : bonus) if (prop.stat == d2d::d2s::kArmorPerLevel) per_level += prop.value;
        std::int64_t enhanced_defense = 0;
        auto own = item.props;                            // its sockets' too (Pul's ac%)
        for (const auto& socketed : item.socketed_items) for (const auto& prop : socket_props(game_data, item, socketed)) own.push_back(prop);
        for (const auto& prop : own) {
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
    // The Scroll of Resistance's stat list (FUN_00589ff0), in an expansion game only (0x539a1b).
    if (header.expansion())
        for (const int stat : kRes) sum[std::size_t(stat)] += d2d::rules::PrisonQuest::scroll_resist(header.quests);
    for (int i = 0; i < 4; ++i) {
        const auto cap = std::min<std::int64_t>(75 + sum[std::size_t(kRes[i] + 1)], 95);
        panel.res[std::size_t(i)] = std::clamp<std::int64_t>(sum[std::size_t(kRes[i])] + penalty, -100, cap);
        panel.res_cap[std::size_t(i)] = cap;
    }
    return panel;
}

}  // namespace d2d::game
