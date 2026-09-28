// The character's items as the rules move them: what an equip check
// sees, the belt in use, a held item put away, a vendor's store.
#pragma once

#include "gamedata.hpp"

namespace d2d::game {

using d2d::rules::Store;

// The strength, dexterity and level an equip check sees: base stats plus
// what the worn gear adds (stats 0 strength, 2 dexterity).
// ponytail: worn items' own props only (no sockets, sets, charms).
inline d2d::rules::Wearer wearer(int save_cls, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& st) {
    d2d::rules::Wearer w{ save_cls, int(st.get(d2d::d2s::kStr)), int(st.get(d2d::d2s::kDex)),
                          int(st.get(d2d::d2s::kLevel)) };
    for (const auto& it : items)
        if (it.location == 1)
            for (const auto& p : it.props) {
                if (p.stat == 0) w.str += p.value;
                if (p.stat == 2) w.dex += p.value;
            }
    return w;
}

// Puts a held item back when the game is left: the first free inventory
// spot, else it's kept as d2s location 4 (on the cursor) so it isn't lost.
inline void stow_held(const GameData& s, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held) {
    if (!held) return;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& it : items) if (it.location == 0 && it.panel == 1) inv.push_back(&it);
    const auto [w, h] = d2d::rules::item_size(s.rules, held->code);
    if (const auto [x, y] = d2d::rules::free_spot(s.rules, inv, 10, 4, w, h); x >= 0) {
        d2d::rules::put_in_grid(s.rules, items, held, 1, 10, 4, x, y);
        return;
    }
    held->location = 4;
    items.push_back(std::move(*held));
    held.reset();
}

// The equipped belt's belts.txt index (armor.txt `belt`), 2 ("default":
// one row) without one — as the popup code picks it (0x49906b).
inline int belt_index(const GameData& s, const std::vector<d2d::d2s::Item>& items) {
    for (const auto& it : items)
        if (it.location == 1 && it.slot == 8)
            if (const auto i = s.rules.item_info.find(it.code); i != s.rules.item_info.end() && i->second.belt >= 0
                && i->second.belt < 7)
                return i->second.belt;
    return 2;
}

// The store for world NPC npc (stock rolled from rng).
inline Store open_store(const GameData& s, const Level& L, int npc, d2d::rules::Rng& rng) {
    const auto& n = L.npcs[std::size_t(npc)];
    Store st = d2d::rules::open_store(s.rules, n.hc_idx, n.id, rng);
    st.npc = npc;
    return st;
}

}  // namespace d2d::game
