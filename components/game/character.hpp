// The character the World plays: what a save holds (header, stats,
// items, corpse), its look, and what the char panel computes from them.
// The client's CharCreateUI (ui.hpp) adds the create screen's controls.
#pragma once

#include "gamedata.hpp"

namespace d2d::game {

// Is item type `t` (or an Equiv ancestor) `want`?
inline bool type_is(const GameData& s, const std::string& t, std::string_view want) {
    return d2d::rules::type_is(s.rules, t, want);
}

// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour).
inline std::vector<d2d::d2s::ItemProp> socket_props(const GameData& s, const d2d::d2s::Item& parent,
                                             const d2d::d2s::Item& filled) {
    auto out = filled.props;
    const auto g = s.gem_props.find(filled.code);
    const auto info = s.rules.item_info.find(parent.code);
    if (g == s.gem_props.end() || info == s.rules.item_info.end()) return out;
    const int k = info->second.kind == 2 ? 0 : type_is(s, info->second.type, "shld") ? 2 : 1;
    const auto& add = g->second[std::size_t(k)];
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
};

inline PanelStats panel_stats(const GameData& s, const d2d::d2s::Header& h,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& st,
                       const std::vector<d2d::rules::PassiveStat>* passives = nullptr) {
    PanelStats p;
    const auto lvl = st.get(d2d::d2s::kLevel);
    if (lvl >= 0 && std::size_t(lvl) + 1 < s.exp_next.size()) p.next = s.exp_next[std::size_t(lvl)];
    std::array<std::int64_t, 64> sum{};
    auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& pr : props) if (pr.stat >= 0 && pr.stat < 64) sum[std::size_t(pr.stat)] += pr.value;
    };
    std::int64_t item_def = 0, per_level = 0;
    for (const auto& it : items) {
        const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
        const bool charm = it.location == 0 && it.panel == 1
                        && (it.code == "cm1" || it.code == "cm2" || it.code == "cm3");
        if (!worn && !charm) continue;
        add(it.props);
        for (const auto& j : it.socketed_items) add(socket_props(s, it, j));
        std::int64_t ed = 0;
        for (const auto& pr : it.props) {
            if (pr.stat == 16) ed += pr.value;            // item_armor_percent: this item's base
            if (pr.stat == 214) per_level += pr.value;    // item_armor_perlevel, 1/8 per level
        }
        if (it.defense > 0) item_def += it.defense * (100 + ed) / 100;
    }
    std::int64_t skill_def = 0;                           // 171 skill_armor_percent
    if (passives) for (const auto& ps : *passives) {
        if (!ps.itype.empty()) continue;
        if (ps.stat >= 0 && ps.stat < 64) sum[std::size_t(ps.stat)] += ps.value;
        if (ps.stat == 171) skill_def += ps.value;
    }
    for (std::size_t i = 0; i < 12; ++i) p.bonus[i] = sum[i];
    {                                                     // the attributes' share of life, stamina, mana (quarter points)
        const auto& g = s.class_gains[std::size_t(h.cls % 7)];
        p.bonus[7] += sum[3] * g.life_per_vit / 4;
        p.bonus[11] += sum[3] * g.stamina_per_vit / 4;
        p.bonus[9] += sum[1] * g.mana_per_energy / 4;
    }
    p.defense = item_def + sum[31] + per_level * lvl / 8 + (st.get(d2d::d2s::kDex) + sum[2]) / 4;
    p.defense += p.defense * skill_def / 100;
    const int diff = h.active_difficulty();
    const std::int64_t penalty = h.expansion() ? s.resist_penalty[std::size_t(diff)]
                                               : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(diff)];
    constexpr int kRes[4] = { 39, 43, 41, 45 };
    for (int i = 0; i < 4; ++i) {
        const auto cap = std::min<std::int64_t>(75 + sum[std::size_t(kRes[i] + 1)], 95);
        p.res[std::size_t(i)] = std::clamp<std::int64_t>(sum[std::size_t(kRes[i])] + penalty, -100, cap);
        p.res_cap[std::size_t(i)] = cap;
    }
    return p;
}

struct Character {
    int selected = -1;           // index of currently-selected class or -1
    // Gear the in-game character wears: a loaded save's appearance bytes,
    // or unset for a fresh character (starting gear).
    std::optional<std::array<std::uint8_t, 32>> appearance;   // + the tints
    std::vector<d2d::d2s::Item> items;   // a loaded save's items
    d2d::d2s::Stats stats;               // ... and attributes
    std::vector<d2d::d2s::Item> corpse;  // ... and its corpse's items (d2s corpse list)
    PanelStats panel;                    // ... and what the char panel computes
    bool expansion = true;               // the save's expansion flag (stash size)
    d2d::d2s::Header header;             // the loaded save's header (quest flags ...)
    // Name entry — SDL text-input feeds this buffer, capped at 15 chars
    // to match D2's char-name limit (per D2's actual character record
    // struct). Left/right arrows and non-printable keys are ignored.
    std::string input_name;
    // Hardcore checkbox — the char-create master-table record at 0x70b0b0
    // (kind=6 button, x=319, y=560, w=15, h=16, handle=DAT_007797c0
    // (clickbox.dc6), on_click=FUN_00430730 which sets bit 0x04 of the
    // character-struct flags word at [0x7795d4]+0x1ef — that's the D2S
    // "Character Status" hardcore bit). Label from patchstring.tbl id
    // 0x1406 ("Hardcore"). See docs/research/re/char-create-table.md for
    // the full 33-record breakdown, including the Ladder (bit 0x40) and
    // Expansion (bit 0x20) checkbox records also present in the table.
    bool hardcore = false;
};

}  // namespace d2d::game
