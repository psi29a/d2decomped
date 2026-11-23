// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of merc.hpp's functions; their notes are in the header.
#include "merc.hpp"

#include "item_price.hpp"
#include "rules.hpp"
#include "wear.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace d2d::rules {

int merc_gap(int x, int y, int size, int to_x, int to_y) {
    const int across = std::max(std::abs(x - to_x) - size, 0), down = std::max(std::abs(y - to_y) - size, 0);
    return (across > down ? down + across * 2 : across + down * 2) / 2;
}

const Hireling* hireling_row(const Tables& tables, int id, int level) {
    const Hireling* found = nullptr;
    for (const auto& row : tables.hirelings) {
        if (row.version != 100 || row.id != id) continue;
        if (found && level < row.level) return found;
        found = &row;
    }
    return found;
}

int merc_slot(const Tables& tables, const d2d::d2s::Item& item, int merc_class, int str, int dex, int level) {
    namespace body = d2d::d2s::body_location;
    const auto* info = info_of(tables, item);
    if (!info || !item.identified || (item.max_durability > 0 && item.durability == 0)) return 0;
    const auto of_type = [&](std::string_view type) { return type_is(tables, info->type, type); };
    int slot = 0;
    if (of_type("helm")) slot = body::kHead;
    else if (of_type("tors")) slot = body::kTorso;
    else if (merc_class == 271 && of_type("bow")) slot = body::kRightArm;
    else if (merc_class == 338 && (of_type("spea") || of_type("pole"))) slot = body::kRightArm;
    else if (merc_class == 359 && of_type("shie")) slot = body::kLeftArm;
    else if (merc_class == 359 && of_type("swor") && !info->two_handed) slot = body::kRightArm;
    else if (merc_class == 560 && of_type("axe") && !info->two_handed) slot = body::kRightArm;
    else if (merc_class == 561 && of_type("swor")) slot = body::kRightArm;
    if ((merc_class == 560 || merc_class == 561) && of_type("phlm")) slot = body::kHead;
    if (slot == 0 || !meets_stats(*info, item, str, dex, level, -1, &tables)) return 0;
    return slot;
}

}
