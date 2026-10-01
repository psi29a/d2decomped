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

// The char panel's computed values (stat 30 next level, 31 defence,
// resistances 39/43/41/45), as FUN_004a7d00 shows them. From the save's
// base stats and gear.
// ponytail: equipped slots 1..10 (the primary weapon set), socket
// bonuses and charms, the passives with no weapon type (Iron Skin's
// defense %, Natural Resistance); no set bonuses or auras.
struct PanelStats {
    std::int64_t next = -1, defense = 0;
    std::array<std::int64_t, 4> res{};           // fire, cold, lightning, poison
    std::array<std::int64_t, 4> res_cap{ 75, 75, 75, 75 };   // 75 + max resist, at most 95: gold at it
    // What items and passives add to the character's own stats 0..11
    // (strength .. max stamina, whole points): the panel shows the sum,
    // blue when it's more, red when less (FUN_004a7d00).
    std::array<std::int64_t, 12> bonus{};
    std::array<d2d::rules::AttackLine, 2> attack{};   // the left / right skill's block, as the server sends it
};

inline PanelStats panel_stats(const GameData& game_data, const d2d::d2s::Header& header,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& stats,
                       const std::vector<d2d::rules::PassiveStat>* passives = nullptr) {
    PanelStats panel;
    const auto lvl = stats.get(d2d::d2s::kLevel);
    if (lvl >= 0 && std::size_t(lvl) + 1 < game_data.exp_next.size()) panel.next = game_data.exp_next[std::size_t(lvl)];
    std::array<std::int64_t, 64> sum{};
    auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& prop : props) if (prop.stat >= 0 && prop.stat < 64) sum[std::size_t(prop.stat)] += prop.value;
    };
    std::int64_t item_def = 0, per_level = 0;
    for (const auto& item : items) {
        const bool worn = item.location == 1 && item.slot >= 1 && item.slot <= 10;
        const bool charm = item.location == 0 && item.panel == 1
                        && (item.code == "cm1" || item.code == "cm2" || item.code == "cm3");
        if (!worn && !charm) continue;
        add(item.props);
        for (const auto& socketed : item.socketed_items) add(socket_props(game_data, item, socketed));
        std::int64_t enhanced_defense = 0;
        for (const auto& prop : item.props) {
            if (prop.stat == 16) enhanced_defense += prop.value;            // item_armor_percent: this item's base
            if (prop.stat == 214) per_level += prop.value;    // item_armor_perlevel, 1/8 per level
        }
        if (item.defense > 0) item_def += item.defense * (100 + enhanced_defense) / 100;
    }
    std::int64_t skill_def = 0;                           // 171 skill_armor_percent
    if (passives) for (const auto& passive : *passives) {
        if (!passive.itype.empty()) continue;
        if (passive.stat >= 0 && passive.stat < 64) sum[std::size_t(passive.stat)] += passive.value;
        if (passive.stat == 171) skill_def += passive.value;
    }
    for (std::size_t i = 0; i < 12; ++i) panel.bonus[i] = sum[i];
    {                                                     // the attributes' share of life, stamina, mana (quarter points)
        const auto& gains = game_data.class_gains[std::size_t(header.cls % 7)];
        panel.bonus[7] += sum[3] * gains.life_per_vit / 4;
        panel.bonus[11] += sum[3] * gains.stamina_per_vit / 4;
        panel.bonus[9] += sum[1] * gains.mana_per_energy / 4;
    }
    panel.defense = item_def + sum[31] + per_level * lvl / 8 + (stats.get(d2d::d2s::kDex) + sum[2]) / 4;
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
    PanelStats panel;                    // ... and what the char panel computes
    bool expansion = true;               // the save's expansion flag (stash size)
    d2d::d2s::Header header;             // the loaded save's header (quest flags ...)
    std::string name;                    // the character's name
};

}  // namespace d2d::game
