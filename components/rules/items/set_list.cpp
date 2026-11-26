// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of set_list.hpp's functions; the research is here.
#include "set_list.hpp"

#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::rules {

// game.exe keeps one stat list per set worn on the owner, in one of six
// states (0x6ee424), found by its stat 0x47 (the Sets.txt row). An item's
// stats going on (FUN_0055c2c0) run, for a set item,
// FUN_00663cc0(owner, item, 0, 0) -> FUN_00663b40 with 0: the set's list is
// cleared (FUN_00627340), or made in the first free state (FUN_006251f0,
// FUN_00626e10, FUN_006252d0), its 0x47 set (FUN_00627030), then filled by
// FUN_00660120 with that item (EDX at 0x663c98, ESI from 0x66012e on).
// FUN_0055c2c0's callers (xrefs) are ItemMode.cpp's server moves: a body item on
// (FUN_005606b0, the load's too, FUN_00560f00, FUN_00561220, FUN_00563d20),
// the weapon switch (FUN_005616a0), a stack refill (FUN_00562a30), the corpse
// (FUN_00562f30), the requirements pass (FUN_0055dbc0); a mend calls
// FUN_00663cc0(.., 0, 0) itself (FUN_0055f900). d2d stamps the item at
// each of those: wield_enters (FUN_0063d1d0 runs on the same moves) and
// equip_from_bag.
// Taking a set piece off (FUN_0055c730 -> FUN_00663cc0(.., 1, 0)) frees the
// set's whole list (FUN_006277e0, FUN_00626cd0), but each server move then
// runs the requirements pass (FUN_0055dbc0), whose last loop (0x55de23..
// 0x55de99) builds it again from the pieces still worn, highest slot last.
// ponytail: d2d skips that last loop (the list's piece is the last one put
// on, not the highest slot's). A break (FUN_0055f850) frees the list after
// its pass (0x55f8e3), and FUN_0057f410 (the corpse's FUN_0057f700) with
// (.., 1, 1): the pieces still worn give no set bonus until a piece's stats
// go on again; d2d keeps them (set_props counts what's worn).
void stats_on(std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item) {
    std::uint32_t last = 0;
    for (const auto& each : items) last = std::max(last, each.set_stamp);
    item.set_stamp = last + 1;
}

int worn_set(const Tables& tables, const d2d::d2s::Item& item) {
    if (item.location != d2d::d2s::item_location::kEquipped || item.slot < d2d::d2s::body_location::kFirst
        || item.slot > d2d::d2s::body_location::kLast || item.quality != 5 || item.set_id < 0
        || std::size_t(item.set_id) >= tables.sets.size() || (item.flags & 0x4100u) != 0)
        return -1;
    return tables.sets[std::size_t(item.set_id)].set;
}

int set_pieces_worn(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int set) {
    std::vector<int> rows;
    for (const auto& item : items)
        if (worn_set(tables, item) == set && set >= 0 && !std::ranges::contains(rows, item.set_id)) rows.push_back(item.set_id);
    return int(rows.size());
}

const d2d::d2s::Item* set_list_piece(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int set) {
    const d2d::d2s::Item* piece = nullptr;
    for (const auto& item : items)
        if (set >= 0 && worn_set(tables, item) == set && (!piece || item.set_stamp >= piece->set_stamp)) piece = &item;
    return piece;
}

// FUN_00660120 (Sets.txt record from FUN_00483410: +0xc the pieces, +0x10
// the 8 partial rows, +0x90 the 8 full rows, 16 bytes each, the property
// at +0 and -1 unused): n = 0x6eda40[mask], a bit count; the partial rows
// 0 .. min(n, pieces - 1) * 2 - 3 whose property is set; with n >= pieces
// the full rows in order, stopping at the first unused one (0x660206).
std::vector<int> set_list_rows(const SetBonus& bonus, int count) {
    std::vector<int> rows;
    const int partial = std::min(count, bonus.pieces - 1) * 2 - 2;
    for (int index = 0; index < partial && std::size_t(index) < bonus.partial.size(); ++index)
        if (!bonus.partial[std::size_t(index)].code.empty()) rows.push_back(index);
    if (count >= bonus.pieces)
        for (std::size_t index = 0; index < bonus.full.size() && index < 8 && !bonus.full[index].code.empty(); ++index)
            rows.push_back(8 + int(index));
    return rows;
}

}   // namespace d2d::rules
