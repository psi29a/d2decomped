// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of rules.hpp's functions; their notes are in the header.
#include "rules.hpp"

#include "drink.hpp"
#include "drops.hpp"
#include "hire.hpp"
#include "item_gen.hpp"
#include "item_price.hpp"
#include "monster_ids.hpp"
#include "set_list.hpp"
#include "vendor.hpp"
#include "wear.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2d::rules {

// The store level (FUN_00576890): clvl + 5, capped in Normal by the act
// (12, 20, 28, 36, 45).
int store_level(int clvl, int act, int difficulty) {
    static constexpr std::array<int, 5> kCap = { 12, 20, 28, 36, 45 };
    const int level = clvl + 5;
    return difficulty == 0 && act >= 0 && act < 5 ? std::min(level, kCap[std::size_t(act)]) : level;
}

int store_quality(Rng& seed, int level) {
    const int roll = int(seed.next() % 100);
    if (level < 5) return roll > 90 ? 1 : 2;
    return roll > (level < 10 ? 85 : 74) ? 3 : 2;
}

long long price_reduced(long long price, int reduced) {
    return std::max<long long>(price - price * std::clamp(reduced, 0, 99) / 100, 1);
}

std::pair<int, int> item_size(const Tables& tables, const std::string& code) {
    const auto info = tables.item_info.find(code);
    return info != tables.item_info.end() ? std::pair{ info->second.width, info->second.height } : std::pair{ 1, 1 };
}

// Where an inventory grid takes an item it's given (FUN_00560200 ->
// FUN_0063b950 -> FUN_0063b850): a w x h spot in the cols x rows grid of
// placed items, wholly free (FUN_0063a8a0). {-1, -1} if none.

// A player's grid (its owner unit type 0: `players`) takes the best fit,
// the most edges and items touching the spot's sides (FUN_0063b340; all
// of them, 2 x (w + h), stops the search; none touching never wins), the
// first of a score in its order; another's (a store's) the first fit.
// 1-high items search from the right column, a player's from the bottom
// row up (FUN_0063b490), else from the top (FUN_0063b540); the rest a
// player's row by row from the top left (FUN_0063b620), else column by
// column from the left (FUN_0063b6e0). tests/test_rules.cpp checks the
// orders.
std::pair<int, int> free_spot(const Tables& tables, const std::vector<const d2d::d2s::Item*>& placed,
                              int cols, int rows, int width, int height, bool players) {
    std::vector<bool> used(std::size_t(cols * rows));
    for (const auto* placed_item : placed) {
        const auto [item_width, item_height] = item_size(tables, placed_item->code);
        for (int row = placed_item->row; row < std::min(placed_item->row + item_height, rows); ++row)
            for (int column = placed_item->column; column < std::min(placed_item->column + item_width, cols); ++column) used[std::size_t(row * cols + column)] = true;
    }
    const auto taken = [&](int column, int row) { return bool(used[std::size_t(row * cols + column)]); };
    const auto fits = [&](int start_column, int start_row) {
        if (taken(start_column, start_row) || start_column + width > cols || start_row + height > rows) return false;
        for (int row = start_row; row < start_row + height; ++row)
            for (int column = start_column; column < start_column + width; ++column)
                if (taken(column, row)) return false;
        return true;
    };
    const auto score = [&](int start_column, int start_row) {                     // FUN_0063b340
        int touching = 0;
        for (int row = start_row; row < start_row + height; ++row) {
            touching += start_column < 1 ? 1 : taken(start_column - 1, row);
            touching += start_column + width < cols ? taken(start_column + width, row) : 1;
        }
        for (int column = start_column; column < start_column + width; ++column) {
            touching += start_row < 1 ? 1 : taken(column, start_row - 1);
            touching += start_row + height < rows ? taken(column, start_row + height) : 1;
        }
        return touching >= 2 * (width + height) ? 0xff : touching;
    };
    int best = 0;
    std::pair spot{ -1, -1 };
    const auto consider = [&](int start_column, int start_row) {    // true: stop
        if (!fits(start_column, start_row)) return false;
        if (!players) { spot = { start_column, start_row }; return true; }
        if (const int fit = score(start_column, start_row); fit > best) { best = fit; spot = { start_column, start_row }; return fit == 0xff; }
        return false;
    };
    if (height == 1) {
        for (int column = cols - 1; column >= 0; --column)
            for (int step = 0; step < rows; ++step)
                if (consider(column, players ? rows - 1 - step : step)) return spot;
    } else if (players) {
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < cols; ++column)
                if (consider(column, row)) return spot;
    } else {
        for (int column = 0; column < cols; ++column)
            for (int row = 0; row < rows; ++row)
                if (consider(column, row)) return spot;
    }
    return spot;
}

bool store_place(const Tables& tables, Store& store, int tab, d2d::d2s::Item item) {
    const auto [width, height] = item_size(tables, item.code);
    for (int page = tab; page < 4; ++page) {
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& placed_item : store.tabs[std::size_t(page)]) placed.push_back(&placed_item);
        if (const auto [column, row] = free_spot(tables, placed, 10, 10, width, height, false); column >= 0) {
            item.column = column; item.row = row; item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory;
            store.tabs[std::size_t(page)].push_back(std::move(item));
            return true;
        }
        if (page != 1) break;
    }
    return false;
}

int store_tab_for(const Tables& tables, const std::string& code) {
    const auto info = tables.item_info.find(code);
    const int kind = info != tables.item_info.end() ? info->second.kind : 0;
    return kind == 1 ? 0 : kind == 2 ? 1 : 3;
}

int vendor_index(int hc_idx) {
    switch (hc_idx) {
        case 0x94: return 0;  case 0x93: return 1;  case 0x9a: return 2;  case 0xb2: return 3;
        case 0xca: return 4;  case 0xb1: return 5;  case 0xfd: return 6;  case 0xfe: return 7;
        case 0xff: return 8;  case 199:  return 9;  case 0xfc: return 10; case 0x101: return 12;
        case 0x195: return 13; case 0x201: return 14; case 0x1ff: return 15; case 0x200: case 0x202: return 16;
        default: return -1;
    }
}

bool is_repair_vendor(int hc_idx) {
    using namespace monster_ids;
    return hc_idx == kCharsi || hc_idx == kFara || hc_idx == kHratli || hc_idx == kHalbu || hc_idx == kLarzuk;
}

// The price base of an armor (ItemTypes armo, FUN_0062efb0 at 0062f0f2):
// cost x its own defence (stat 31, not the stat list's) / maxac, for buy,
// sell and repair alike; any other item keeps its cost.
int armor_scaled(const Tables& tables, const d2d::d2s::Item& item, int cost) {
    const auto base = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    if (base == tables.item_base.end() || info == tables.item_info.end() || !type_is(tables, info->second.type, "armo")) return cost;
    if (base->second.maxac == 0 || base->second.maxac - base->second.minac + 1 == 0) return cost;
    return int(std::int64_t(cost) * item.defense / base->second.maxac);
}

int price_term(int mul, int add, int price, int value, int limit, int shift) {
    const auto wrap = [](long long number) { return std::int32_t(std::uint32_t(number)); };
    const int scale = 1 << shift;
    return limit < 0x10000 || mul == 0 ? wrap(1LL * wrap(1LL * wrap(1LL * mul * price) * value) / scale + add)
                                       : wrap(1LL * wrap(1LL * price * value) / scale * mul + add);
}

int stat_costs(const Tables& tables, const d2d::d2s::Item& item, int buy, int price, bool sell) {
    std::map<std::pair<int, int>, int> stats;
    for (const auto& prop : item.props) stats[{ prop.stat, prop.param }] += prop.value;
    for (const auto& filled : item.socketed_items)
        for (const auto& prop : socket_props(tables, item, filled)) stats[{ prop.stat, prop.param }] += prop.value;
    int total = 0;
    for (const auto& [key, value] : stats) {
        const auto [stat, param] = key;
        if (value == 0 || stat < 0 || std::size_t(stat) >= tables.stat_cost.size()) continue;
        const auto& cost = tables.stat_cost[std::size_t(stat)];
        if (cost.encode >= 1 && cost.encode <= 3) {
            const int skill = cost.encode == 1 ? param : param >> 6, level = cost.encode == 1 ? value : param & 63;
            if (skill < 0 || std::size_t(skill) >= tables.skill_costs.size()) continue;
            const auto [mul, add] = tables.skill_costs[std::size_t(skill)];
            total += price_term(mul, add, price, level, buy * level, sell ? 12 : 10);
        } else {
            total += price_term(cost.mul, cost.add, price, value, buy * value, 10);
        }
    }
    return total;
}

// What an item type's staff mods add (FUN_0062edd0): each item_singleskill
// (stat 107) costs (2 x level - 1) x (the skill's cost mult x price / 1024 +
// cost add), a sale's / 4096; above 0xffff the mult is divided, not the price.
// `buy` picks the branch.

// Any item type: the check against ItemTypes StaffMods (+0x1f != 7) never
// fails, as an empty StaffMods loads as 0xff (emu-read).
int staff_mod_costs(const Tables& tables, const d2d::d2s::Item& item, int buy, int price, bool sell) {
    int total = 0;
    for (const auto& prop : item.props) {
        if (prop.stat != 107 || prop.param < 0 || std::size_t(prop.param) >= tables.skill_costs.size()) continue;
        const auto [mul, add] = tables.skill_costs[std::size_t(prop.param)];
        const int scale = sell ? 4096 : 1024;
        const int each = buy < 0x10000 || mul == 0 ? mul * price / scale + add : mul / scale * price + add;
        total += each * (prop.value * 2 - 1);
    }
    return total;
}

int stack_most(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_base.find(item.code);
    int most = found != tables.item_base.end() ? found->second.max_stack : 0;
    for (const auto& prop : item.props)
        if (prop.stat == kExtraStack) most += int(prop.value);
    return std::min(most, 0x1ff);
}

bool priced_by_contents(const Tables& tables, const d2d::d2s::Item& item) {
    const auto info = tables.item_info.find(item.code);
    const std::string type = info != tables.item_info.end() ? info->second.type : std::string{};
    return type == "book" || type == "bowq" || type == "xboq";
}

// A stack's share of its extras (FUN_0062efb0's local_1c): a stackable
// (Items.txt +0x132, FUN_006289f0) priced as a stack takes its quality
// extras, stat costs (FUN_00628e70) and staff mods (FUN_0062edd0), each
// divided by its most when that is 2 or more. FUN_00628e70 and FUN_0062edd0
// divide unsigned.
bool priced_as_stack(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_base.find(item.code);
    return found != tables.item_base.end() && found->second.stackable && !priced_by_contents(tables, item);
}

int stack_divisor(const Tables& tables, const d2d::d2s::Item& item) {
    return priced_as_stack(tables, item) ? std::max(stack_most(tables, item), 1) : 1;
}

int unsigned_share(int total, int divisor) {
    return int(std::uint32_t(total) / std::uint32_t(divisor));
}

// What a stack lacks of its most (0062fc97): none when it's full or it
// replenishes itself (item_replenish_quantity, stat 0xfd: 0062fca4).
int stack_missing(const Tables& tables, const d2d::d2s::Item& item) {
    const int count = std::max(item.quantity, 1), most = stack_most(tables, item);
    const bool replenishes = std::ranges::any_of(item.props, [](const auto& prop) { return prop.stat == kReplenishQuantity && prop.value != 0; });
    return count < most && !replenishes ? most - count : 0;
}

// Base = the item's cost (items record +0xe0), plus per quality (mult,
// add) extras: low quality -base/2, magic prefix + suffix, rare/crafted
// its six affixes, set/unique their cost mult/add (extra = base*mult/1024
// + add); + half the cost of each socketed item (FUN_006292f0); ethereal
// sells at a quarter. Then npc.txt: * buy/sell mult / 1024 and each
// questflag's mult when that quest is done; * quantity; selling caps at
// "max buy" for the difficulty. npc.txt's mults are the vendor's side:
// its "sell mult" is our buy price (gamedata_load.cpp swaps them in).
int item_price(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, bool sell,
               const d2d::d2s::Header& header, int reduced) {
    const auto found = tables.item_base.find(item.code);
    int base = found != tables.item_base.end() ? found->second.cost : 0;
    // FUN_0062efb0 prices two kinds of item by their quantity (stat 70, at least
    // 1) before the NPC multipliers: a tome (ItemTypes book) is its cost plus
    // books.txt CostPerCharge per scroll inside; an arrow or bolt quiver
    // (bowq / xboq) is cost x quantity / 1024. Anything else is priced one
    // (a stack's extras divided by its most: stack_divisor) and multiplied
    // by its quantity after the NPC multipliers (buy 0062fc75, sell 0062fcd0).
    const auto info = tables.item_info.find(item.code);
    const std::string& type = info != tables.item_info.end() ? info->second.type : std::string{};
    const int count = std::max(item.quantity, 1);
    const int divisor = stack_divisor(tables, item);
    if (type == "book") {
        const auto charge = tables.book_charge_cost.find(item.code);
        base += (charge != tables.book_charge_cost.end() ? charge->second : 0) * count;
    } else if (type == "bowq" || type == "xboq") {
        base = int(std::int64_t(base) * count / 1024);
    }
    base = armor_scaled(tables, item, base);
    // Staff mods count on the base below magic (FUN_0062a0f0: quality 4 .. 9),
    // identified or not (0062f166), and on the price with its extras from magic up.
    // The buy side's base picks every branch below, a sale's too.
    const bool magic = item.quality >= 4;
    const int buy_base = magic ? base : base + unsigned_share(staff_mod_costs(tables, item, base, base, false), divisor);
    if (!magic) base += unsigned_share(staff_mod_costs(tables, item, base, base, sell), divisor);
    auto extra = [&](const std::vector<std::pair<int, int>>& costs, int index) {
        if (index < 0 || std::size_t(index) >= costs.size()) return 0;
        const auto [mul, add] = costs[std::size_t(index)];
        return (buy_base < 0x10000 ? mul * base / 1024 : base / 1024 * mul) + add;
    };
    // The quality extras count only once the item is identified (0062f182):
    // an unidentified magic item sells as a normal one, a low quality one isn't halved.
    // The class automod's (item data +0x36, 0062f1b8) comes first, any quality;
    // low quality's half replaces it. Superior, magic, rare and crafted add their stats' costs (0062f814).
    // A stack's affix extras and stat costs are each divided by its divisor (0062f83f, FUN_00628e70).
    auto extras = [&](bool sale) {
        int x = item.identified && item.class_affix > 0 ? extra(tables.automagic_cost, item.class_affix - 1) : 0;
        int stats = 0;
        switch (item.identified ? item.quality : 0) {
            case 1: x = -(base / 2); break;
            case 3: stats = stat_costs(tables, item, buy_base, base, sale); break;
            case 4: x += extra(tables.prefix_cost, item.prefix) + extra(tables.suffix_cost, item.suffix); stats = stat_costs(tables, item, buy_base, base, sale); break;
            case 5: x += extra(tables.set_cost, item.set_id); break;
            case 7: x += extra(tables.unique_cost, item.unique_id); break;
            case 6: case 8:
                for (int i = 0; i < 6; ++i) x += extra(i % 2 == 0 ? tables.prefix_cost : tables.suffix_cost, item.affixes[std::size_t(i)]);
                stats = stat_costs(tables, item, buy_base, base, sale);
                break;
            default: break;
        }
        return x / divisor + unsigned_share(stats, divisor);
    };
    long long price = base + extras(sell);
    if (magic && item.identified) price += unsigned_share(staff_mod_costs(tables, item, base + extras(false), int(price), sell), divisor);
    for (const auto& socketed : item.socketed_items)
        if (const auto socketed_base = tables.item_base.find(socketed.code); socketed_base != tables.item_base.end()) price += socketed_base->second.cost / 2;
    if (sell && item.ethereal) price /= 4;
    // A class's own item (its ItemTypes Class set, FUN_0062c060) sells for a quarter again (0062f99d).
    if (const auto own = tables.types.find(type); sell && own != tables.types.end() && !own->second.cls.empty()) price /= 4;
    // An ethereal item with durability (FUN_00629930) worn down to 0 sells for nothing, so the minimum 1 (0062fa60).
    if (sell && item.ethereal && found != tables.item_base.end() && !found->second.no_durability && found->second.durability > 0
        && max_durability(item) != 0 && !indestructible(tables, item) && item.durability <= 0)
        price = 0;
    const auto price_row = tables.npc_prices.find(npc_id);
    const int diff = header.active_difficulty();
    if (price_row != tables.npc_prices.end()) {
        const auto& prices = price_row->second;
        // Above 0xffff the price is divided by 1024 before the mult, as the quality extras (0062faaf).
        auto scale = [&price](int mult) { price = price < 0x10000 || mult == 0 ? price * mult / 1024 : price / 1024 * mult; };
        scale(sell ? prices.sell : prices.buy);
        for (int quest = 0; quest < 3; ++quest)
            if (prices.qflag[std::size_t(quest)] && (header.quest_flag(diff, prices.qflag[std::size_t(quest)], 0) || header.quest_flag(diff, prices.qflag[std::size_t(quest)], 1)))
                scale(sell ? prices.qsell[std::size_t(quest)] : prices.qbuy[std::size_t(quest)]);
    }
    // A stack its repair refills (stackable, and FUN_0062e660 passes) sells
    // as a full one less the repair of what it lacks (0062fc85..0062fcbb).
    // The products are game.exe's 32 bits: one past 2^31 goes negative, so
    // the minimum 1.
    const auto wrap = [](long long number) { return std::int32_t(std::uint32_t(number)); };
    if (sell && priced_as_stack(tables, item) && repairable(tables, item))
        price = wrap(wrap(stack_most(tables, item) * price) - wrap(repair_each(tables, item, npc_id, header, false) * stack_missing(tables, item)));
    else if (!priced_by_contents(tables, item)) price = wrap(price * count);
    if (sell && price_row != tables.npc_prices.end()) price = std::min<long long>(price, price_row->second.max_buy[std::size_t(diff)]);
    return int(sell ? std::max<long long>(price, 1) : price_reduced(price, reduced));
}

int max_durability(const d2d::d2s::Item& item) {
    int flat = 0, pct = 0;
    for (const auto& prop : item.props) {
        if (prop.stat == d2d::d2s::kMaxDurability) flat += int(prop.value);
        if (prop.stat == d2d::d2s::kMaxDurabilityPercent) pct += int(prop.value);
    }
    return (item.max_durability + flat) * (100 + pct) / 100;
}

std::vector<d2d::d2s::ItemProp> socket_props(const Tables& tables, const d2d::d2s::Item& parent, const d2d::d2s::Item& filled) {
    auto out = filled.props;
    const auto found = tables.gem_props.find(filled.code);
    const auto info = tables.item_info.find(parent.code);
    if (found == tables.gem_props.end() || info == tables.item_info.end()) return out;
    const int slot_kind = info->second.kind == 2 ? 0 : type_is(tables, info->second.type, "shld") ? 2 : 1;
    const auto& add = found->second[std::size_t(slot_kind)];
    out.insert(out.end(), add.begin(), add.end());
    return out;
}

bool indestructible(const Tables& tables, const d2d::d2s::Item& item) {
    const auto has = [](const std::vector<d2d::d2s::ItemProp>& props) { return std::ranges::any_of(props, [](const auto& prop) { return prop.stat == d2d::d2s::kIndestructible && prop.value; }); };
    return has(item.props) || std::ranges::any_of(item.socketed_items, [&](const d2d::d2s::Item& filled) { return has(socket_props(tables, item, filled)); });
}

// A stack a repair refills (FUN_005761c0): stackable (ItemsTxt +0x132,
// FUN_006289f0) and throwable (FUN_0062ba80) — every stacking weapon.
bool restocks(const Tables& tables, const d2d::d2s::Item& item) {
    const auto base = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    return base != tables.item_base.end() && base->second.stackable && info != tables.item_info.end() && info->second.kind == 2;
}

// What an item's missing charges add to its repair (FUN_00628d30, called
// with 10000 by FUN_0062efb0): each stat 204 below its max, (max - left) x
// (cost mult x (level + 2 + reqlevel / 6 x 2) x 10000 / 1024 + cost add)
// / max, Skills.txt's cost mult / cost add (+0x234 / +0x238) and reqlevel
// (+0x174).

// Not an ethereal item's (FUN_0062a8d0, item flag 0x400000).
int charges_repair_cost(const Tables& tables, const d2d::d2s::Item& item) {
    if (item.ethereal) return 0;
    int cost = 0;
    for (const auto& prop : item.props) {
        const int skill = prop.param >> 6, level = prop.param & 63, left = prop.value & 255, most = prop.value >> 8;
        if (prop.stat != d2d::d2s::kChargedSkill || left >= most || skill < 0 || std::size_t(skill) >= tables.skill_costs.size()) continue;
        const auto [mult, add] = tables.skill_costs[std::size_t(skill)];
        const int reqlevel = std::size_t(skill) < tables.skill_levels.size() ? tables.skill_levels[std::size_t(skill)].first : 0;
        const long long scaled = (level + 2 + reqlevel / 6 * 2) * 10000LL;
        const long long each = (scaled < 0x10000 || mult == 0 ? mult * scaled / 1024 : scaled / 1024 * mult) + add;
        cost += int((most - left) * each / most);
    }
    return cost;
}

// A repair's recharge (FUN_0055fe80, after FUN_005761c0's durability; the
// cube's recharge flag 0x400, FUN_00565ab0): each stat 204 below its max
// back to max (FUN_0065c940). True if any was.

// game.exe lists a stat's entries with FUN_006261d0, which reads only a
// list with the expanded flag 0x80000000 (tools/emu/stats_unit_list.py);
// d2d keeps every prop, so it has no such gate.
bool recharge(d2d::d2s::Item& item) {
    bool any = false;
    for (auto& prop : item.props)
        if (const int left = prop.value & 255, most = prop.value >> 8; prop.stat == d2d::d2s::kChargedSkill && left < most) { prop.value = most << 8 | most; any = true; }
    return any;
}

bool charge_missing(const d2d::d2s::Item& item) {
    return std::ranges::any_of(item.props, [](const auto& prop) { return prop.stat == d2d::d2s::kChargedSkill && (prop.value & 255) < (prop.value >> 8); });
}

bool has_durability(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_base.find(item.code);
    return found != tables.item_base.end() && !found->second.no_durability && found->second.durability > 0 && max_durability(item) != 0 && !indestructible(tables, item);
}

// The item's ItemTypes row has Repair (FUN_0062b400 -> FUN_00555c90 +8).
bool type_mends(const Tables& tables, const d2d::d2s::Item& item) {
    const auto info = tables.item_info.find(item.code);
    const auto kind = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end();
    return kind != tables.types.end() && kind->second.repair;
}

bool repairable(const Tables& tables, const d2d::d2s::Item& item) {
    if (item.ethereal || !item.identified) return false;
    return charge_missing(item) || (type_mends(tables, item) && (restocks(tables, item) || has_durability(tables, item)));
}

// What Repair All counts (FUN_0062fe60): a repairable item with durability
// below or above its max, a restocking stack (FUN_0062e5d0: its type has
// Repair, not ethereal) short of its max stack, or a charge missing.
bool needs_repair(const Tables& tables, const d2d::d2s::Item& item) {
    const int max = max_durability(item);
    if (repairable(tables, item) && has_durability(tables, item) && item.durability != max) return true;
    if (type_mends(tables, item) && restocks(tables, item) && !item.ethereal && item.quantity < stack_most(tables, item)) return true;   // FUN_006295b0
    return charge_missing(item);
}

long long repair_each(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, bool worn_share) {
    const int max = max_durability(item);
    const auto found = tables.item_base.find(item.code);
    const int divisor = stack_divisor(tables, item);
    int base = armor_scaled(tables, item, found != tables.item_base.end() ? found->second.cost : 0);
    if (item.quality < 4) base += unsigned_share(staff_mod_costs(tables, item, base, base, false), divisor);
    auto extra = [&](const std::vector<std::pair<int, int>>& costs, int index) {
        if (index < 0 || std::size_t(index) >= costs.size()) return 0;
        const auto [mul, add] = costs[std::size_t(index)];
        return (base < 0x10000 ? mul * base / 1024 : base / 1024 * mul) + add;
    };
    int affixes = item.quality != 1 && item.class_affix > 0 ? extra(tables.automagic_cost, item.class_affix - 1) : 0;   // the class automod, as item_price
    int stats = 0;
    switch (item.quality) {
        case 1: affixes = -(base / 2); break;
        case 3: stats = stat_costs(tables, item, base, base, false); break;
        case 4: affixes += extra(tables.prefix_cost, item.prefix) + extra(tables.suffix_cost, item.suffix); stats = stat_costs(tables, item, base, base, false); break;
        case 5: affixes += extra(tables.set_cost, item.set_id); break;
        case 7: affixes += extra(tables.unique_cost, item.unique_id); break;
        case 6: case 8:
            for (int i = 0; i < 6; ++i) affixes += extra(i % 2 == 0 ? tables.prefix_cost : tables.suffix_cost, item.affixes[std::size_t(i)]);
            stats = stat_costs(tables, item, base, base, false);
            break;
        default: break;
    }
    long long x = base + affixes / divisor + unsigned_share(stats, divisor);
    if (item.quality >= 4) x += unsigned_share(staff_mod_costs(tables, item, int(x), int(x), false), divisor);
    for (const auto& socketed : item.socketed_items)
        if (const auto socketed_base = tables.item_base.find(socketed.code); socketed_base != tables.item_base.end()) x += socketed_base->second.cost / 2;
    // The missing share's product is game.exe's 32 bits (0062fa45). An item
    // that replenishes its durability (stat 0xfc, 0062fa13) pays (max - 1) /
    // max of it below max - 1 (0062fa35), else nothing (0062fa2c). A
    // throwable skips it (FUN_0062ba80; `restocks` stands for it).
    long long cost = x;
    if (worn_share && !restocks(tables, item) && has_durability(tables, item)) {
        const bool replenishes = std::ranges::any_of(item.props, [](const auto& prop) { return prop.stat == kReplenishDurability && prop.value != 0; });
        const int missing = item.durability >= max ? 0 : !replenishes ? max - item.durability : item.durability < max - 1 ? max - 1 : 0;
        cost = missing > 0 ? std::int32_t(std::uint32_t(missing * x)) / max : 0;
    }
    if (const auto price_row = tables.npc_prices.find(npc_id); price_row != tables.npc_prices.end()) {
        const auto& prices = price_row->second;
        // Above 0xffff the cost is divided by 1024 before the mult, as a price's (0062fb1d).
        auto scale = [&cost](int mult) { cost = std::int32_t(std::uint32_t(cost < 0x10000 || mult == 0 ? cost * mult / 1024 : cost / 1024 * mult)); };
        scale(prices.rep);
        const int diff = header.active_difficulty();
        for (int quest = 0; quest < 3; ++quest)
            if (prices.qflag[std::size_t(quest)] && (header.quest_flag(diff, prices.qflag[std::size_t(quest)], 0) || header.quest_flag(diff, prices.qflag[std::size_t(quest)], 1)))
                scale(prices.qrep[std::size_t(quest)]);
    }
    return cost;
}

int repair_cost(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, int reduced) {
    if (!repairable(tables, item)) return 0;
    long long cost = repair_each(tables, item, npc_id, header, true);
    if (priced_as_stack(tables, item)) cost *= stack_missing(tables, item);   // 0062fcb6, none past 0062fcc0
    return int(price_reduced(cost + charges_repair_cost(tables, item), reduced));
}

// A cost the purse covers, a negative one too (Repair All's wrapped
// sum, bugs.md 50), goes through FUN_0053ff00(-cost): a purse that
// would pass its cap (clvl x 10000, FUN_00622e70) is set to 0 instead.
bool pay(d2d::d2s::Stats& stats, std::int64_t cost) {
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < cost) return false;
    const auto from_purse = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), cost);
    const auto purse = stats.get(d2d::d2s::kGold) - from_purse;
    stats.values[d2d::d2s::kGold] = from_purse < 0 && purse > stats.get(d2d::d2s::kLevel) * 10000 ? 0 : purse;
    stats.values[d2d::d2s::kGoldBank] -= cost - from_purse;
    return true;
}

// Mends an item (FUN_005761c0): only one its gate (FUN_0062e660) passes. A
// restocking stack back to its max, durability to its max, charges full.

// A broken one goes through FUN_0055f900: flag 0x200 on, 0x100 off; its
// hand-skill pass (FUN_0055c5c0) is World::hand_skills, run by the repair.
bool mend(const Tables& tables, d2d::d2s::Item& item) {
    if (!repairable(tables, item)) return false;
    if (restocks(tables, item)) item.quantity = stack_most(tables, item);   // FUN_006295b0
    item.durability = max_durability(item);
    if (broken(item)) item.flags |= kItemRepaired;   // FUN_0055f900
    item.flags &= ~0x100u;
    recharge(item);
    return true;
}

// Repairs item i (gold first from the inventory, then the stash, as a
// buy). False if it needs none (`needs_repair`: a whole item's price is
// still 1), it's priced 0 or you can't pay.
bool store_repair(const Tables& tables, const Store& store, d2d::d2s::Item& item, d2d::d2s::Stats& stats) {
    if (!needs_repair(tables, item)) return false;
    const int cost = repair_cost(tables, item, store.npc_id, store.header, store.reduced);
    return cost > 0 && pay(stats, cost) && mend(tables, item);
}

// An item Repair All takes (FUN_0062fe60): a worn one, the 13 body slots
// (FUN_0063bde0(inventory, 0..0xc)), never the inventory grid, that
// `needs_repair`.
bool repair_all_takes(const Tables& tables, const d2d::d2s::Item& item) {
    return item.location == d2d::d2s::item_location::kEquipped && needs_repair(tables, item);
}

// What repair all would cost (Repair all's hover, FUN_004886c0 -> FUN_0062fe60):
// each item it takes, at its repair price.

// game.exe sums them in a 32-bit int (FUN_0062fe60's local_8), so past
// 2^31 the sum wraps negative and paying it credits the gold (bugs.md 50).
// Summed wide here and wrapped as game.exe's, without C++'s signed
// overflow.
int repair_all_cost(const Tables& tables, const Store& store, const std::vector<d2d::d2s::Item>& items) {
    std::int64_t cost = 0;
    for (const auto& item : items)
        if (repair_all_takes(tables, item)) cost += repair_cost(tables, item, store.npc_id, store.header, store.reduced);
    return std::int32_t(std::uint32_t(cost));
}

// Which corpse a save keeps (FUN_005697f0, PlrSave2's corpse list): over
// them oldest first, one with items scores 1 + each non-gold item's repair
// cost at `npc_id` (Malah: FUN_0062fdc0 -> FUN_0062efb0 mode 3, args at
// 0x5698a8..0x5698b4; a whole repairable item 1, as repair_cost has it);
// the highest, a tie the later (`best <= score`, jb at 0x5698d1). -1: none
// has items.

// combat.md "Saving".
int corpse_to_save(const Tables& tables, const std::vector<const std::vector<d2d::d2s::Item>*>& corpses, const std::string& npc_id,
                   const d2d::d2s::Header& header, int reduced) {
    int best = -1;
    std::uint32_t best_score = 0;
    for (std::size_t index = 0; index < corpses.size(); ++index) {
        if (corpses[index]->empty()) continue;
        std::uint32_t score = 1;
        for (const auto& item : *corpses[index]) {
            if (item.code == "gld") continue;
            score += std::uint32_t(repair_cost(tables, item, npc_id, header, reduced));
        }
        if (best < 0 || best_score <= score) { best = int(index); best_score = score; }
    }
    return best;
}

// Repair all (FUN_00578050, flag 0x80000000): the sum is paid at once
// (FUN_00576d90).

// Short of it, nothing is repaired and the client is told it hasn't
// the gold (12, line 0x9e4): nullopt. Paid, a second FUN_0062fe60
// pass mends each item it takes (FUN_005762b0 -> FUN_005761c0). The
// count mended; a sum of 0 mends none (0x9dd).
std::optional<int> store_repair_all(const Tables& tables, const Store& store, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    const int cost = repair_all_cost(tables, store, items);
    if (cost == 0) return 0;
    if (!pay(stats, cost)) return std::nullopt;
    int mended = 0;
    for (auto& item : items) {
        if (!repair_all_takes(tables, item)) continue;
        const bool was_broken = broken(item);
        mended += int(mend(tables, item));
        if (was_broken && !broken(item)) wield_enters(tables, items, item);   // FUN_0055f900 -> FUN_0063d1d0
    }
    return mended;
}

// A scroll's tome (Books.txt pairs tsc / tbk, isc / ibk) for `scroll`:
// true when `book` is one with room (FUN_0055ef20: quantity, stat 70,
// under its max).
bool tome_takes(const Tables& tables, const d2d::d2s::Item& scroll, const d2d::d2s::Item& book) {
    const std::string tome = scroll.code == "tsc" ? "tbk" : scroll.code == "isc" ? "ibk" : "";
    const auto base = tables.item_base.find(book.code);
    return !tome.empty() && book.code == tome && book.location == d2d::d2s::item_location::kStored && base != tables.item_base.end() && book.quantity < base->second.max_stack;
}

// The vendor's perm items (FUN_00576ed0): its PermStoreItems, and in an
// expansion game greater and super potions.
bool store_perm(const Store& store, const std::string& code) {
    static constexpr std::array<std::string_view, 4> kStrongPotions = { "hp4", "hp5", "mp4", "mp5" };
    return std::ranges::find(store.perm, code) != store.perm.end() || (store.header.expansion() && std::ranges::find(kStrongPotions, code) != kStrongPotions.end());
}

Trade store_buy(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats, int boxes,
                bool fill, const Wearer* wearer, int required_level) {
    using d2d::d2s::Item;
    auto& tab = store.tabs[std::size_t(store.tab)];
    const Item item = tab[std::size_t(index)];   // a copy: the loop below erases tab[index], then reads it again
    const auto purse = [&] { return stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank); };
    const auto pay = [&](std::int64_t cost) {
        const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), cost);
        stats.values[d2d::d2s::kGold] -= from_inv;
        stats.values[d2d::d2s::kGoldBank] -= cost - from_inv;
    };
    const auto in_inventory = [](const Item& carried) { return carried.location == d2d::d2s::item_location::kStored && carried.panel == d2d::d2s::item_panel::kInventory; };
    const auto weapon = [&](const Item& carried) {
        const auto found = tables.item_info.find(carried.code);
        return found != tables.item_info.end() && type_is(tables, found->second.type, "weap");
    };
    const auto in_hand = [&](const Item& carried) {
        return carried.location == d2d::d2s::item_location::kEquipped && weapon(carried)
            && (carried.slot == d2d::d2s::body_location::kRightArm || carried.slot == d2d::d2s::body_location::kLeftArm);
    };
    std::int64_t price = item_price(tables, item, store.npc_id, false, store.header, store.reduced);
    if (purse() < price) return Trade::no_gold;
    fill = fill && store_perm(store, item.code);
    const auto book = std::ranges::find_if(items, [&](const Item& carried) { return carried.panel == d2d::d2s::item_panel::kInventory && tome_takes(tables, item, carried); });
    if (book != items.end()) {
        const auto room = std::int64_t(tables.item_base.at(book->code).max_stack - book->quantity);
        const auto count = fill ? std::min(purse() / price, room) : 1;
        pay(count * price);
        book->quantity += int(count);
        return Trade::done;
    }
    int quantity = item.quantity;   // local_c
    const auto info = tables.item_info.find(item.code);
    const auto type_row = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end();
    if (fill && type_row != tables.types.end() && type_row->second.autostack) {
        auto one = item;
        one.quantity = 1;
        const std::int64_t unit = item_price(tables, one, store.npc_id, false, store.header, store.reduced);
        Item* stack = nullptr;
        std::int64_t room = 1;
        const auto base = tables.item_base.find(item.code);
        if (base != tables.item_base.end() && base->second.stackable) {
            room = stack_most(tables, item);
            const auto open = [&](const Item& carried) { return stacks_with(carried, item) && carried.quantity < stack_most(tables, carried); };
            for (const bool worn : { true, false }) {
                const auto found = std::ranges::find_if(items, [&](const Item& carried) { return (worn ? carried.location == d2d::d2s::item_location::kEquipped : in_inventory(carried)) && open(carried); });
                if (found == items.end()) continue;
                stack = &*found;
                room = stack_most(tables, *found) - found->quantity;
                break;
            }
        }
        const auto count = std::min(purse() / unit, room);
        if (stack) {
            if (count < 1) return Trade::no_gold;
            pay(count * unit);
            stack->quantity += int(count);
            return Trade::done;
        }
        quantity = int(std::max<std::int64_t>(count, 1));
        price = quantity * unit;
    }
    for (bool bought = false;; bought = true) {
        if (bought && !fill) return Trade::done;
        if (purse() < price) return Trade::no_gold;
        auto copy = item;
        copy.quantity = quantity;
        if (store_perm(store, item.code)) copy.id = -1;   // a perm item stays: the copy is a new unit (the stock item moves otherwise)
        if (const int box = belt_box(tables, items, copy, boxes); box >= 0) {
            copy.location = d2d::d2s::item_location::kBelt; copy.column = box; copy.row = 0; copy.panel = 0; copy.slot = 0;
        } else if (const int slot = wearer && !(weapon(copy) && std::ranges::any_of(items, in_hand)) ? auto_equip_slot(tables, items, copy, *wearer, required_level) : -1;
                   slot > 0) {
            fill = false;
            copy.location = d2d::d2s::item_location::kEquipped; copy.slot = slot; copy.column = 0; copy.row = 0; copy.panel = 0;
        } else {
            // One already bought: this copy is dropped and nothing paid
            // (0x577d9c -> LAB_00577e68), so a fill never spills over.
            if (bought) return Trade::done;
            fill = false;
            std::vector<const Item*> inv;
            for (const auto& carried : items) if (in_inventory(carried)) inv.push_back(&carried);
            const auto [width, height] = item_size(tables, item.code);
            const auto [column, row] = free_spot(tables, inv, 10, 4, width, height, true);
            if (column < 0) return Trade::no_room;
            copy.column = column; copy.row = row; copy.location = d2d::d2s::item_location::kStored; copy.panel = d2d::d2s::item_panel::kInventory;
        }
        items.push_back(std::move(copy));
        wield_enters(tables, items, items.back());     // a weapon worn (FUN_00562e00 -> FUN_0063d1d0)
        pay(price);
        if (!store_perm(store, item.code)) tab.erase(tab.begin() + index);
    }
}

// Sells item i (FUN_00579510): gold up by the sell value, what
// passes the carried cap (clvl x 10000) returned to drop at the player's
// feet (FUN_0055b060).

// The vendor stocks a copy at full durability and stack (stats 72 / 70)
// unless it's Cracked (LowQualityItems row 1), broken (0x100), unique,
// personalized, ethereal, holds something in its sockets (FUN_0055f590:
// Items hasinv, +0x137, with its inventory not empty), sold while gambling
// (the player's proxy at the vendor, FUN_00536cb0: +4 set by the gamble
// open, FUN_00578b30), a greater or super potion in an expansion game, or
// one of the vendor's perm items (FUN_00576ed0). The item is a stored one,
// the cursor's or a worn one (the World's cmd::Sell: off its body slot by
// FUN_00560cd0; a belt item refused).
std::int64_t store_sell(const Tables& tables, Store& store, std::size_t index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    const auto& sold = items[index];
    const int price = item_price(tables, sold, store.npc_id, true, store.header);
    const auto cap = stats.get(d2d::d2s::kLevel) * 10000, purse = stats.get(d2d::d2s::kGold);
    const auto over = std::max<std::int64_t>(purse + price - std::max(cap, purse), 0);
    stats.values[d2d::d2s::kGold] = purse + price - over;
    const bool kept = !(sold.quality == 1 && sold.qsub == 1) && !(sold.flags & 0x100u) && sold.quality != 7 && !sold.personalized && !sold.ethereal
                      && sold.socketed_items.empty() && !store.gamble && !store_perm(store, sold.code);
    if (kept) {
        auto copy = sold;
        copy.durability = max_durability(copy);
        if (const auto base = tables.item_base.find(copy.code); base != tables.item_base.end() && copy.quantity >= 0) copy.quantity = std::min(base->second.max_stack, 511);
        (void)store_place(tables, store, store_tab_for(tables, copy.code), copy);   // no room: sold all the same
    }
    items.erase(items.begin() + std::ptrdiff_t(index));
    return over;
}

bool type_is(const Tables& tables, const std::string& type, std::string_view want, int depth) {
    if (type.empty() || depth > 8) return false;
    if (type == want) return true;
    const auto found = tables.types.find(type);
    return found != tables.types.end()
        && (type_is(tables, found->second.equiv[0], want, depth + 1) || type_is(tables, found->second.equiv[1], want, depth + 1));
}

int body_slot(std::string_view code) {
    static constexpr std::string_view kCodes[11] = { "", "head", "neck", "tors", "rarm", "larm",
                                                     "rrin", "lrin", "belt", "feet", "glov" };
    for (int i = 1; i < 11; ++i) if (code == kCodes[i]) return i;
    return 0;
}

std::string type_class(const Tables& tables, const std::string& type, int depth) {
    const auto found = tables.types.find(type);
    if (found == tables.types.end() || depth > 8) return {};
    if (!found->second.cls.empty()) return found->second.cls;
    for (const auto& equivalent : found->second.equiv)
        if (auto class_code = type_class(tables, equivalent, depth + 1); !class_code.empty()) return class_code;
    return {};
}

const ItemInfo* info_of(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_info.find(item.code);
    return found != tables.item_info.end() ? &found->second : nullptr;
}

// game.exe's value x times / over (FUN_00483360), truncated: over 0 gives 0;
// value past 0x100000 goes value / over x times when over <= value >> 4, times
// past 0x10000 times / over x value when over <= times >> 4; else in 64 bits when
// either is that large, 32 (wrapping) when neither is.
int scale(int value, int times, int over) {
    if (over == 0) return 0;
    if (value > 0x100000) return over <= value >> 4 ? value / over * times : int(std::int64_t(value) * times / over);
    if (times > 0x10000) return over <= times >> 4 ? times / over * value : int(std::int64_t(value) * times / over);
    return std::int32_t(std::uint32_t(value) * std::uint32_t(times)) / over;
}

// `required_level`: the item's full requirement (FUN_0062b5b0; stat 92 can
// take it below the base's), or -1 for the base's levelreq. Stat 91 is the
// item's full stat (FUN_00625500), what its sockets hold included (a Hel
// rune): pass `tables` for those.
bool meets_stats(const ItemInfo& info, const d2d::d2s::Item& item, int str, int dex, int lvl, int required_level,
                 const Tables* tables) {
    int percent = 0;
    const auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& prop : props) if (prop.stat == d2d::d2s::kRequirementPercent) percent += prop.value;
    };
    add(item.props);
    if (tables) for (const auto& filled : item.socketed_items) add(socket_props(*tables, item, filled));
    const int ethereal = item.ethereal ? 10 : 0;
    const int req_str = info.req_str + (percent ? scale(info.req_str, percent, 100) : 0) - ethereal;
    const int req_dex = info.req_dex + (percent ? scale(info.req_dex, percent, 100) : 0) - ethereal;
    return str >= 1 && str >= req_str && dex >= 1 && dex >= req_dex && lvl >= (required_level >= 0 ? required_level : info.req_lvl);
}

// The class, stat and level requirements alone (FUN_0062eaf0's, any slot).
bool meets_requirements(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer, int required_level) {
    const auto* info = info_of(tables, item);
    if (!info) return false;
    if (const auto class_code = type_class(tables, info->type); !class_code.empty() && (wearer.cls < 0 || wearer.cls > 6 || class_code != kClassCode[std::size_t(wearer.cls)]))
        return false;
    return meets_stats(*info, item, wearer.str, wearer.dex, wearer.lvl, required_level, &tables);
}

// Can `it` be worn in body slot `slot` by `w` at all (slot, class, stat and
// level requirements, identified: FUN_0062eaf0), ignoring what's already
// equipped?
bool can_wear(const Tables& tables, const d2d::d2s::Item& item, int slot, const Wearer& wearer, int required_level) {
    const auto* info = info_of(tables, item);
    if (!info || !item.identified) return false;
    const auto found = tables.types.find(info->type);
    if (found == tables.types.end() || slot < d2d::d2s::body_location::kFirst || (found->second.body[0] != slot && found->second.body[1] != slot)) return false;
    return meets_requirements(tables, item, wearer, required_level);
}

// A two-handed weapon fills both hands (a Barbarian swings 1or2handed
// ones in one, FUN_0062a1e0); only a bow's own quiver joins it (hands_ok).
bool blocks_other_hand(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer) {
    const auto* info = info_of(tables, item);
    return info && info->two_handed && !(wearer.cls == d2d::d2s::kBarbarian && info->one_or_two);
}

// Can a and b be held in the two hands together (FUN_0063dbc0)? A bow or
// crossbow with what its type Shoots (bowq / xboq); any other pair with a
// quiver or a two-hander, never; two weapons only for a Barbarian (or an
// Assassin's two claws); else a weapon with a shield.
bool hands_ok(const Tables& tables, const d2d::d2s::Item& first, const d2d::d2s::Item& second, const Wearer& wearer) {
    const auto* first_info = info_of(tables, first);
    const auto* second_info = info_of(tables, second);
    if (!first_info || !second_info) return false;
    const auto first_type = tables.types.find(first_info->type), second_type = tables.types.find(second_info->type);
    const ItemType* types[2] = { first_type != tables.types.end() ? &first_type->second : nullptr, second_type != tables.types.end() ? &second_type->second : nullptr };
    if (types[0] && !types[0]->shoots.empty() && type_is(tables, second_info->type, types[0]->shoots)) return true;
    if (types[1] && !types[1]->shoots.empty() && type_is(tables, first_info->type, types[1]->shoots)) return true;
    if ((types[0] && !types[0]->quiver.empty()) || (types[1] && !types[1]->quiver.empty())) return false;
    if (blocks_other_hand(tables, first, wearer) || blocks_other_hand(tables, second, wearer)) return false;
    const bool first_weapon = type_is(tables, first_info->type, "weap"), second_weapon = type_is(tables, second_info->type, "weap");
    if (first_weapon && second_weapon) return wearer.cls == d2d::d2s::kBarbarian || (wearer.cls == d2d::d2s::kAssassin && type_is(tables, first_info->type, "h2h") && type_is(tables, second_info->type, "h2h"));
    return first_weapon != second_weapon;
}

// A body slot's other side (FUN_0055f240): the arms 4 <-> 5, the rings
// 6 <-> 7, the switch arms 11 <-> 12; 0 for the rest.
int other_side(int slot) {
    switch (slot) {
    case 4: return 5; case 5: return 4; case 6: return 7; case 7: return 6;
    case 11: return 12; case 12: return 11;
    default: return 0;
    }
}

// Where a corpse's item from `slot` goes (FUN_0055f2d0): `taken` the
// player's slot, `other_taken` its other side's. An arm then pairs with
// what the other hand holds (hands_ok), a ring with nothing.

// Both free: its own; both taken: none (-1, the inventory); its own taken,
// the other free: the other side (0x55f333..0x55f33a); else its own.
int corpse_slot(int slot, bool taken, bool other_taken) {
    if (taken && other_taken) return -1;
    return taken ? other_side(slot) : slot;
}

// The item cursor. D2 moves one item at a time: picking up takes it out
// of the list into `held`; putting down places it, swapping with the one
// item it lands on (into `held`), and fails when it would overlap two or
// more, doesn't fit, or isn't allowed there. Each put returns whether it
// changed anything.
bool put_in_grid(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                 int panel, int cols, int rows, int col, int row) {
    if (!held || (panel == d2d::d2s::item_panel::kCube && held->code == "box")) return false;   // the cube can't go in itself
    const auto [width, height] = item_size(tables, held->code);
    if (col < 0 || row < 0 || col + width > cols || row + height > rows) return false;
    int hit = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& placed_item = items[i];
        if (placed_item.location != d2d::d2s::item_location::kStored || placed_item.panel != panel) continue;
        const auto [item_width, item_height] = item_size(tables, placed_item.code);
        if (placed_item.column < col + width && col < placed_item.column + item_width && placed_item.row < row + height && row < placed_item.row + item_height) {
            if (hit >= 0) return false;
            hit = int(i);
        }
    }
    auto put = std::move(*held);
    held.reset();
    if (hit >= 0) { held = std::move(items[std::size_t(hit)]); items.erase(items.begin() + hit); }
    put.location = d2d::d2s::item_location::kStored; put.panel = panel; put.column = col; put.row = row; put.slot = 0;
    items.push_back(std::move(put));
    return true;
}

// A hand's stack `held` joins (FUN_0063dd90's first test, code 6, C->S
// 0x21 -> FUN_0055e7c0): its index in `items`, -1 none.

// FUN_0062c850 (a Stackable base, FUN_006289f0, and stacks_with).
int hand_stack(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& held, int slot) {
    if (slot != d2d::d2s::body_location::kRightArm && slot != d2d::d2s::body_location::kLeftArm) return -1;
    const auto base = tables.item_base.find(held.code);
    if (base == tables.item_base.end() || !base->second.stackable) return -1;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].location == d2d::d2s::item_location::kEquipped && items[i].slot == slot && stacks_with(items[i], held)) return int(i);
    return -1;
}

// The cursor's stack onto `target` (FUN_0055e590): past the target's most
// (stack_most, FUN_006295b0) the target fills and the rest stays held;
// else the target takes it all, the lower durability of the two (stat
// 0x48, an item with durability: FUN_00629930), and nothing is held.
void stack_onto(const Tables& tables, d2d::d2s::Item& target, std::optional<d2d::d2s::Item>& held) {
    const int most = stack_most(tables, target), total = held->quantity + target.quantity;
    if (total > most) {
        held->quantity = total - most;
        target.quantity = most;
        return;
    }
    if (has_durability(tables, *held) && held->durability < target.durability) target.durability = held->durability;
    target.quantity = total;
    held.reset();
}

bool in_a_hand(const d2d::d2s::Item& item) {
    return item.location == d2d::d2s::item_location::kEquipped
        && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm);
}

bool weapon_item(const Tables& tables, const d2d::d2s::Item& item) {
    const auto* info = info_of(tables, item);
    return info && type_is(tables, info->type, "weap");
}

// The item in hand `slot`, if any (FUN_0063bde0 on cell 4 or 5).
int hand_index(const std::vector<d2d::d2s::Item>& items, int slot) {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].location == d2d::d2s::item_location::kEquipped && items[i].slot == slot) return int(i);
    return -1;
}

const d2d::d2s::Item* wielded_weapon(const Tables& tables, const std::vector<d2d::d2s::Item>& items) {
    for (const int slot : { d2d::d2s::body_location::kLeftArm, d2d::d2s::body_location::kRightArm })
        if (const int index = hand_index(items, slot); index >= 0 && items[std::size_t(index)].wielded && weapon_item(tables, items[std::size_t(index)]))
            return &items[std::size_t(index)];
    return nullptr;
}

void wield(std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item) {
    for (auto& each : items) each.wielded = false;
    item.wielded = true;
}

// Each move that runs FUN_0063d1d0 puts the item's stats on too
// (FUN_0055c2c0; set_list.cpp): a worn item takes its stamp first.
void wield_enters(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item) {
    if (item.location == d2d::d2s::item_location::kEquipped) stats_on(items, item);
    if (!in_a_hand(item) || !weapon_item(tables, item)) return;
    item.wielded = false;
    if (!item_usable(item)) return;
    const auto* current = wielded_weapon(tables, items);
    const auto* info = current ? info_of(tables, *current) : nullptr;
    if (!current || (info && type_is(tables, info->type, "tpot"))) wield(items, item);
}

void wield_leaves(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& leaving) {
    if (!in_a_hand(leaving) || !weapon_item(tables, leaving)) return;
    leaving.wielded = false;
    const int other = d2d::d2s::body_location::kRightArm + d2d::d2s::body_location::kLeftArm - leaving.slot;
    if (const int index = hand_index(items, other); index >= 0 && item_usable(items[std::size_t(index)])) wield(items, items[std::size_t(index)]);
}

// A loaded character's hands: each item enters in the save's order
// (FUN_005335e0 -> FUN_00531210, a worn one through FUN_005606b0 with the
// requirements skipped, which calls FUN_0063d1d0 for slots 4 / 5); a new
// character's starting gear the same way, in CharStats order (FUN_00534d60
// -> FUN_00534bb0 -> FUN_005606b0).

// Before each worn item but the switch's (0xb / 0xc) goes in, FUN_005606b0
// keeps the weapon wielded then (FUN_0063bef0 -> FUN_006233a0, the
// player's +0x90): the last one kept is answered, an index into `items`,
// -1 none (FUN_0057ff70 reads it).
int wield_settle(const Tables& tables, std::vector<d2d::d2s::Item>& items) {
    for (auto& item : items) item.wielded = false;
    int kept = -1;
    for (auto& item : items) {
        if (item.location == d2d::d2s::item_location::kEquipped && item.slot != d2d::d2s::body_location::kRightArmSwitch
            && item.slot != d2d::d2s::body_location::kLeftArmSwitch)
            if (const auto* before = wielded_weapon(tables, items)) kept = int(before - items.data());
        wield_enters(tables, items, item);
    }
    return kept;
}

bool requirements_hold(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                       int required_level) {
    if (!item.identified || !meets_requirements(tables, item, wearer, required_level)) return false;
    const auto type = tables.types.find(info_of(tables, item)->type);
    if (type == tables.types.end() || type->second.quiver.empty()) return true;
    int other = hand_index(items, d2d::d2s::body_location::kRightArm);
    if (other >= 0 && &items[std::size_t(other)] == &item) other = hand_index(items, d2d::d2s::body_location::kLeftArm);
    if (other < 0) return false;
    const auto& bow = items[std::size_t(other)];
    const auto* bow_info = info_of(tables, bow);
    const auto base = tables.item_base.find(bow.code);
    return (bow_info && type_is(tables, bow_info->type, type->second.quiver))
        || (base != tables.item_base.end() && type_is(tables, base->second.type2, type->second.quiver));
}

bool inventory_charm(const Tables& tables, const d2d::d2s::Item& item) {
    if (item.location != d2d::d2s::item_location::kStored || item.panel != d2d::d2s::item_panel::kInventory || !item_usable(item)) return false;
    const auto* info = info_of(tables, item);
    return info && type_is(tables, info->type, "char");
}

int throw_hand(const Tables& tables, const std::vector<d2d::d2s::Item>& items, bool* wielded) {
    if (wielded) *wielded = false;
    for (const int slot : { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm }) {
        const int index = hand_index(items, slot);
        if (index < 0 || !item_usable(items[std::size_t(index)]) || !weapon_item(tables, items[std::size_t(index)])) continue;
        if (&items[std::size_t(index)] == wielded_weapon(tables, items)) {
            if (wielded) *wielded = true;
            return index;
        }
        return hand_index(items, d2d::d2s::body_location::kRightArm + d2d::d2s::body_location::kLeftArm - slot);
    }
    return -1;
}

// In the hands, `held` and the other hand's item must go together
// (FUN_0063dd90 -> FUN_0063dbc0: rules::hands_ok); when they can't: - this
// hand empty: the other hand's item comes off to the cursor (code 2,
//
//   C->S 0x1b, FUN_00563d20);
// - this hand full: the other hand's item goes to the inventory's free spot
//   (code 7, C->S 0x1e, FUN_00561220: FUN_0063cb00's room test, then
//   FUN_0063b950, a player's best fit) and this hand's to the cursor. No
//   room in the `cols` x `rows` inventory: code 0, refused.
// `kept_weapon`: an empty slot (code 1, FUN_005606b0), code 2 and a swap
// (code 5, FUN_00560f00) first keep the weapon wielded then, its unit id
// (FUN_0063bef0 -> FUN_006233a0, the player's +0x90; none wielded leaves it
// as it was). Code 7 (FUN_00561220) and a stack joined don't.
bool equip(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
           int slot, const Wearer& wearer, int required_level, int cols, int rows, int* kept_weapon) {
    if (!held || !can_wear(tables, *held, slot, wearer, required_level)) return false;   // FUN_0063de60 asks first
    if (const int stack = hand_stack(tables, items, *held, slot); stack >= 0) {
        stack_onto(tables, items[std::size_t(stack)], held);
        return true;
    }
    const bool hand = slot == d2d::d2s::body_location::kRightArm || slot == d2d::d2s::body_location::kLeftArm;
    const int other = hand ? d2d::d2s::body_location::kRightArm + d2d::d2s::body_location::kLeftArm - slot : 0;
    int off = -1, other_off = -1;                      // this slot's item; the other hand's, when it can't stay
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& worn = items[i];
        if (worn.location != d2d::d2s::item_location::kEquipped) continue;
        if (worn.slot == slot) off = int(i);
        else if (hand && worn.slot == other && !hands_ok(tables, *held, worn, wearer)) other_off = int(i);
    }
    if (off >= 0 && other_off >= 0) {                  // code 7: the other hand's to the inventory
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& carried : items)
            if (carried.location == d2d::d2s::item_location::kStored && carried.panel == d2d::d2s::item_panel::kInventory) placed.push_back(&carried);
        auto& moved = items[std::size_t(other_off)];
        const auto [width, height] = item_size(tables, moved.code);
        const auto [column, row] = free_spot(tables, placed, cols, rows, width, height, true);
        if (column < 0) return false;
        wield_leaves(tables, items, moved);            // FUN_00561220: the other hand's comes off first
        moved.location = d2d::d2s::item_location::kStored; moved.panel = d2d::d2s::item_panel::kInventory;
        moved.column = column; moved.row = row; moved.slot = 0;
        wield_leaves(tables, items, items[std::size_t(off)]);   // then this hand's, the other hand empty
    } else {
        if (const auto* before = wielded_weapon(tables, items); before && kept_weapon) *kept_weapon = before->id;   // -> FUN_006233a0
        if (off < 0) {
            off = other_off;                           // code 2: the other hand's to the cursor
            if (off >= 0) wield_leaves(tables, items, items[std::size_t(off)]);   // FUN_00563d20
        } else {
            wield_leaves(tables, items, items[std::size_t(off)]);   // code 5, a swap (FUN_00560f00)
        }
    }
    auto put = std::move(*held);
    held.reset();
    if (off >= 0) { held = std::move(items[std::size_t(off)]); items.erase(items.begin() + off); }
    put.location = d2d::d2s::item_location::kEquipped; put.slot = slot; put.panel = 0; put.column = 0; put.row = 0;
    items.push_back(std::move(put));
    wield_enters(tables, items, items.back());         // FUN_0063d1d0, once it's in
    return true;
}

// The slot is only head, neck, torso, feet or gloves: one body location,
// so a weapon, a shield, a ring or a belt is refused. The old piece moves
// into the inventory only when a free cell remains after the new item has
// left its own cells.
int bag_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                   int id, const Wearer& wearer, int required_level) {
    if (held) return -1;
    const auto found = std::ranges::find(items, id, &d2d::d2s::Item::id);
    if (found == items.end() || found->location != d2d::d2s::item_location::kStored || found->panel != d2d::d2s::item_panel::kInventory) return -1;
    const auto* info = info_of(tables, *found);
    const auto type = info ? tables.types.find(info->type) : tables.types.end();
    if (type == tables.types.end()) return -1;
    const int first = type->second.body[0], second = type->second.body[1];
    if (first != 0 && second != 0 && first != second) return -1;
    const int slot = first != 0 ? first : second;
    const bool allowed = slot == d2d::d2s::body_location::kHead || slot == d2d::d2s::body_location::kNeck || slot == d2d::d2s::body_location::kTorso
                         || slot == d2d::d2s::body_location::kFeet || slot == d2d::d2s::body_location::kGloves;
    if (!allowed || !can_wear(tables, *found, slot, wearer, required_level)) return -1;
    return slot;
}

bool equip_from_bag(const Tables& tables, std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                    int id, const Wearer& wearer, int required_level, int cols, int rows, int* kept_weapon) {
    const int slot = bag_equip_slot(tables, items, held, id, wearer, required_level);
    if (slot < 0) return false;
    const auto found = std::ranges::find(items, id, &d2d::d2s::Item::id);
    int worn = -1;
    for (std::size_t index = 0; index < items.size(); ++index)
        if (items[index].location == d2d::d2s::item_location::kEquipped && items[index].slot == slot) worn = int(index);
    int column = 0, row = 0;
    if (worn >= 0) {
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& carried : items)
            if (&carried != &*found && carried.location == d2d::d2s::item_location::kStored && carried.panel == d2d::d2s::item_panel::kInventory)
                placed.push_back(&carried);
        const auto [width, height] = item_size(tables, items[std::size_t(worn)].code);
        const auto spot = free_spot(tables, placed, cols, rows, width, height, true);
        if (spot.first < 0) return false;
        column = spot.first;
        row = spot.second;
    }
    if (const auto* before = wielded_weapon(tables, items); before && kept_weapon) *kept_weapon = before->id;   // FUN_006233a0
    if (worn >= 0) {
        auto& old_item = items[std::size_t(worn)];
        old_item.location = d2d::d2s::item_location::kStored;
        old_item.panel = d2d::d2s::item_panel::kInventory;
        old_item.column = column;
        old_item.row = row;
        old_item.slot = 0;
    }
    auto& put = *found;
    put.location = d2d::d2s::item_location::kEquipped;
    put.slot = slot;
    put.panel = put.column = put.row = 0;
    stats_on(items, put);                                // FUN_0055c2c0
    return true;
}

// An empty hand clicked with nothing held, beside a two-hander (Items
// +0x11c, FUN_006289c0): FUN_0063dd90's code 4, and the client takes the
// two-hander (C->S 0x1c with the other hand's slot, FUN_00490780 /
// FUN_00490ba0). Its unit id, -1 none.
int two_hander_beside(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int slot) {
    if (slot != d2d::d2s::body_location::kRightArm && slot != d2d::d2s::body_location::kLeftArm) return -1;
    const int other = d2d::d2s::body_location::kRightArm + d2d::d2s::body_location::kLeftArm - slot;
    for (const auto& item : items)
        if (item.location == d2d::d2s::item_location::kEquipped && item.slot == other)
            if (const auto info = tables.item_info.find(item.code); info != tables.item_info.end() && info->second.two_handed) return item.id;
    return -1;
}

bool put_in_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                 int box, int boxes) {
    if (!held || box < 0 || box >= boxes) return false;
    const auto* info = info_of(tables, *held);
    if (!info) return false;
    auto beltable = [&](auto&& self, const std::string& type, int depth) -> bool {
        const auto found = tables.types.find(type);
        if (found == tables.types.end() || depth > 8) return false;
        return found->second.beltable || self(self, found->second.equiv[0], depth + 1) || self(self, found->second.equiv[1], depth + 1);
    };
    if (!beltable(beltable, info->type, 0)) return false;
    auto put = std::move(*held);
    held.reset();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].location == d2d::d2s::item_location::kBelt && items[i].column == box) { held = std::move(items[i]); items.erase(items.begin() + std::ptrdiff_t(i)); break; }
    put.location = d2d::d2s::item_location::kBelt; put.column = box; put.row = 0; put.panel = 0; put.slot = 0;
    items.push_back(std::move(put));
    return true;
}

std::vector<d2d::d2s::Item> spill_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int boxes, int cols, int rows) {
    std::vector<d2d::d2s::Item> dropped;
    for (int box = std::max(boxes, 0); box < 16; ++box) {
        const auto found = std::ranges::find_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kBelt && item.column == box; });
        if (found == items.end()) continue;
        std::optional<d2d::d2s::Item> spilt = std::move(*found);
        items.erase(found);
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& item : items)
            if (item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) placed.push_back(&item);
        const auto [width, height] = item_size(tables, spilt->code);
        if (const auto [column, row] = free_spot(tables, placed, cols, rows, width, height, true);
            column >= 0 && put_in_grid(tables, items, spilt, d2d::d2s::item_panel::kInventory, cols, rows, column, row)) continue;
        spilt->location = d2d::d2s::item_location::kStored; spilt->column = spilt->row = spilt->panel = 0;
        dropped.push_back(std::move(*spilt));
    }
    return dropped;
}

// game.exe's MulDiv (FUN_00483360): value x times / over, truncated, 0 over
// 0.

// Up to 0x100000 with times up to 0x10000 it multiplies in 32 bits (the
// product wraps); a bigger times over at most times / 16 divides it first,
// as a bigger value over at most value / 16 does the value (each multiply
// wrapping); else 64 bits.
int mul_div(int value, int times, int over) {
    if (over == 0) return 0;
    if (value <= 0x100000) {
        if (times <= 0x10000) return int(std::uint32_t(value) * std::uint32_t(times)) / over;
        if (over <= times >> 4) return int(std::uint32_t(times / over) * std::uint32_t(value));
    } else if (over <= value >> 4) {
        return int(std::uint32_t(value / over) * std::uint32_t(times));
    }
    return int(std::int64_t(value) * times / over);
}

// The player's mana a frame (FUN_005806f0 up to 0x58079e, 256ths), in its
// 32-bit math: unless state 0x55 (nomanaregen, `no_regen`: 0x580724) is
// on, the max (FUN_00625d60) over the CharStats ManaRegen byte x 25 frames
// (7500 when it's 0; idiv), at least 1, then MulDiv(that, 100 +
// manarecoverybonus (27), 100) (FUN_00483360); plus manarecovery (26),
// flat, the add wrapping. The caller caps it at what's missing.

// tools/emu/mana_frame.py runs it against game.exe
// (tests/test_mana_frame.cpp).
std::int64_t mana_per_frame(std::int64_t max_mana, int regen_seconds, int bonus_pct, int flat, bool no_regen) {
    std::uint32_t amount = 0;
    if (!no_regen) {
        const int frames = regen_seconds * 25 != 0 ? regen_seconds * 25 : 7500;
        const int base = std::max(int(std::uint32_t(max_mana)) / frames, 1);
        amount = std::uint32_t(mul_div(base, int(std::uint32_t(bonus_pct) + 100u), 100));
    }
    return int(amount + std::uint32_t(flat));
}

// The client sends a Shift-spend as packets of at most 32 (FUN_004a78c0),
// so `count` (d2d's command carries the total) is spent whole; the
// server's handler (FUN_0054bd10) takes a stat under 16 and 1..100 points
// a packet, one at a time while stat 4 lasts (FUN_00570d60: strength and
// dexterity + 1; energy FUN_00570a80, vitality FUN_00570b60: CharStats
// ManaPerMagic +0x48, LifePerVitality +0x46, StaminaPerVitality +0x47, in
// quarters, to the current and max); a stat past 3 spends none.
int spend_stat_points(d2d::d2s::Stats& stats, int stat, int count, const ClassGains& gains) {
    using namespace d2d::d2s;
    count = int(std::min<std::int64_t>(count, stats.get(kStatPts)));
    if (count <= 0 || stat < 0 || stat > 3) return 0;
    stats.values[std::size_t(stat)] += count;
    stats.values[kStatPts] -= count;
    auto both = [&](int cur, int max, int quarters) {
        stats.values[std::size_t(cur)] += std::int64_t(quarters) * 64 * count;
        stats.values[std::size_t(max)] += std::int64_t(quarters) * 64 * count;
    };
    if (stat == kVit) { both(kLife, kMaxLife, gains.life_per_vit); both(kStamina, kMaxStamina, gains.stamina_per_vit); }
    if (stat == kEne) both(kMana, kMaxMana, gains.mana_per_energy);
    return count;
}

// Akara's Reset Stat/Skill Points (FUN_00570360, FUN_00570c80): every
// skill's points come back (stat 5) and its level goes to 0; each stat
// goes back to the class's CharStats base (str, energy, dex, vit — `base`
// in stat order), the difference to or from stat 4; energy and vitality
// take their mana / life and stamina max with them (current too when they
// rise, else clamped to the new max).
void respec(d2d::d2s::Stats& stats, const std::array<int, 4>& base, const ClassGains& gains) {
    using namespace d2d::d2s;
    for (auto& skill_level : stats.skills) { stats.values[kSkillPts] += skill_level; skill_level = 0; }
    for (int stat = 0; stat < 4; ++stat) {
        const std::int64_t delta = base[std::size_t(stat)] - stats.values[std::size_t(stat)];
        stats.values[kStatPts] -= delta;
        stats.values[std::size_t(stat)] += delta;
        auto pool = [&](int cur, int max, int quarters) {
            stats.values[std::size_t(max)] += std::int64_t(quarters) * delta * 64;
            if (delta > 0) stats.values[std::size_t(cur)] += std::int64_t(quarters) * delta * 64;
            stats.values[std::size_t(cur)] = std::min(stats.values[std::size_t(cur)], stats.values[std::size_t(max)]);
        };
        if (stat == kEne) pool(kMana, kMaxMana, gains.mana_per_energy);
        if (stat == kVit) { pool(kLife, kMaxLife, gains.life_per_vit); pool(kStamina, kMaxStamina, gains.stamina_per_vit); }
    }
}

// Can skill i (0..29 of class cls) take a point: a level to go, the
// character level, and every prerequisite learned? (FUN_004ac200 greys
// out the icons that can't.)
// The base levels (points spent), no item +skills: its max test reads
// FUN_006442a0(.., 0) (+0x28 alone), a prerequisite +0x28 >= 1 (FUN_006447d0).
bool can_learn(const Tables& tables, int cls, int skill_index, const std::array<std::uint8_t, 30>& levels, int clvl) {
    if (cls < 0 || cls > 6 || skill_index < 0 || std::size_t(skill_index) >= tables.class_skills[std::size_t(cls)].size()) return false;
    const auto& class_skill = tables.class_skills[std::size_t(cls)][std::size_t(skill_index)];
    if (levels[std::size_t(skill_index)] >= class_skill.max_level || clvl < class_skill.req_level) return false;
    for (const int required : class_skill.req) if (required >= 0 && levels[std::size_t(required)] == 0) return false;
    return true;
}

bool learn_skill(const Tables& tables, int cls, int skill_index, std::array<std::uint8_t, 30>& levels, d2d::d2s::Stats& stats) {
    if (stats.get(d2d::d2s::kSkillPts) <= 0 || !can_learn(tables, cls, skill_index, levels, int(stats.get(d2d::d2s::kLevel)))) return false;
    ++levels[std::size_t(skill_index)];
    --stats.values[d2d::d2s::kSkillPts];
    return true;
}

// Healers restore life and mana when you talk to them (FUN_00578e70's
// hcIdx list): Akara, Atma, Fara, Ormus, Jamella, Malah.
// The cures are Fight::cure (FUN_00578d30). The save's max
// life/mana are base values without gear (current can be higher), so
// healing only raises to them; the true max needs item stat totals.
bool is_healer(int hc_idx) {
    using namespace monster_ids;
    return hc_idx == kAkara || hc_idx == kAtma || hc_idx == kFara || hc_idx == kOrmus || hc_idx == kJamella || hc_idx == kMalah;
}

void heal(d2d::d2s::Stats& stats) {
    using namespace d2d::d2s;
    stats.values[kLife] = std::max(stats.values[kLife], stats.values[kMaxLife]);
    stats.values[kMana] = std::max(stats.values[kMana], stats.values[kMaxMana]);
}

bool pick_up(std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held, std::size_t index) {
    if (held || index >= items.size()) return false;
    held = std::move(items[index]);
    items.erase(items.begin() + std::ptrdiff_t(index));
    return true;
}

std::string lower_case(std::string_view text) {
    std::string out(text);
    for (auto& letter : out) letter = char(std::tolower(static_cast<unsigned char>(letter)));
    return out;
}

void apply_mod(const Tables& tables, const Mod& mod, std::vector<d2d::d2s::ItemProp>& out, Rng& rng, const ModItem* target) {
    using namespace d2d::d2s;
    if (target && target->item && target->item->version == 0) {   // FUN_0065fe10: version 0 takes its own table
        apply_classic_mod(tables, mod, out, rng, *target);
        return;
    }
    const auto found = tables.properties.find(lower_case(mod.code));
    if (found == tables.properties.end()) return;
    auto skill = [&](const std::string& name) {
        if (!name.empty() && name[0] >= '0' && name[0] <= '9') return std::atoi(name.c_str());
        const auto skill_found = tables.skill_id.find(name);
        return skill_found != tables.skill_id.end() ? skill_found->second : 0;
    };
    const int par = mod.param.empty() ? 0 : (mod.param[0] >= '0' && mod.param[0] <= '9') || mod.param[0] == '-'
                                               ? std::atoi(mod.param.c_str()) : skill(mod.param);
    auto draw = [&] { return rng.range(std::min(mod.min, mod.max), std::max(mod.min, mod.max)); };
    auto put = [&](int stat, int param, int amount) {     // FUN_0065ea50
        if (stat < 0 || amount == 0) return 0;
        out.push_back({ stat, param, amount });
        if (stat == kPoisonMaxDamage) out.push_back({ kPoisonCount, 0, 1 });      // poison max damage counts a poison (poison_count)
        return amount;
    };
    // `hold`: 5 / 6's rule (FUN_0065ecf0 / FUN_0065ee80), a nonzero base the
    // amount takes below 1 kept at 1 (min) or 0 (max); the hands by the
    // base's min bytes for a min, max bytes for a max.
    auto damage = [&](int one_hand, int two_hand, int thrown, int amount, bool hold = false) {
        const bool weapon = !target || target->kind == 2, low = one_hand == kMinDamage;
        const ItemBase* item_base = target ? target->base : nullptr;
        const int base_one = item_base ? (low ? item_base->min_1h : item_base->max_1h) : 0;
        const int base_two = item_base ? (low ? item_base->min_2h : item_base->max_2h) : 0;
        const int base_throw = item_base ? (low ? item_base->min_misdam : item_base->max_misdam) : 0;
        const auto put_held = [&](int stat_id, int base_damage) {
            const bool under = hold && base_damage != 0 && base_damage + amount < 1;
            put(stat_id, 0, under ? (low ? 1 : 0) - base_damage : amount);
        };
        if (!weapon || !item_base || base_one != 0 || base_two == 0) put_held(one_hand, base_one);
        if (item_base && (!weapon || base_two != 0 || base_one == 0)) put_held(two_hand, base_two);
        if (item_base && (!weapon || target->throwable)) put_held(thrown, base_throw);
        return amount;
    };
    auto skill_level = [&] {                               // FUN_0065f470 / FUN_0065f6a0
        const bool known = par >= 0 && std::size_t(par) < tables.skill_levels.size();
        const int req = known ? tables.skill_levels[std::size_t(par)].first : 0x7fffffff;
        const int ilvl = target ? target->item->ilvl : 1;
        if (mod.max > 0) return mod.max;
        if (mod.max < 0) return std::max((ilvl - req) / std::max(-(std::max(99 - req, 1) / mod.max), 1), 1);
        const int most = known && tables.skill_levels[std::size_t(par)].second >= 1 ? tables.skill_levels[std::size_t(par)].second : 20;
        return std::min(std::max((ilvl - req) / 4 + 1, 1), most);
    };
    int first = 0;                                         // the first func's result, FUN_0065fd70's param_8
    for (std::size_t k = 0; k < found->second.size(); ++k) {
        const auto& property_func = found->second[k];
        const int stat = property_func.stat, func = property_func.func;
        auto value = [&] { return first != 0 ? first : draw(); };
        if (target && target->kind == 1 && target->base->maxac != 0 && (stat == kArmorPercent || stat == kArmorClass)
            && (func == 2 || func == 4 || (target->raise && (func == 1 || func == 3 || func == 13))))
            target->item->defense = std::max(target->item->defense + 1, target->base->maxac + 1);
        int result = 0;
        switch (func) {
            case 1: case 2: result = put(stat, 0, draw()); break;
            case 3: case 4: case 8: result = put(stat, 0, value()); break;
            case 5: result = damage(kMinDamage, kSecondaryMinDamage, kThrowMinDamage, value(), true); break;
            case 6: result = damage(kMaxDamage, kSecondaryMaxDamage, kThrowMaxDamage, value(), true); break;
            case 7:
                result = value();
                if (target && target->kind == 2 && std::max(target->base->max_1h, target->base->max_2h) * result / 100 == 0) damage(kMaxDamage, kSecondaryMaxDamage, kThrowMaxDamage, 1);
                else { put(kMinDamagePercent, 0, result); put(kMaxDamagePercent, 0, result); }
                break;
            case 9: if (const int amount = value(); amount != 0) result = put(stat, par, amount); break;
            case 10: if (const int amount = value(); amount != 0) result = put(stat, par % 3 + par / 3 * 8, amount); break;
            case 11: result = put(stat, par * 64 + (skill_level() & 63), mod.min < 1 ? 5 : mod.min); break;
            case 12: result = put(stat, draw(), par); break;
            case 13:
                result = put(stat, 0, draw());
                if (target && result != 0) {
                    int percent = 0, more = target->item->max_durability;
                    for (const auto& prop : out) {
                        if (prop.stat == kMaxDurabilityPercent) percent += prop.value;
                        if (prop.stat == kMaxDurability) more += prop.value;
                    }
                    if (const int most = more + more * percent / 100; most > 0) target->item->durability = most;
                }
                break;
            case 14: {                                     // FUN_0065f590: sockets, at most the grid (6) and FUN_0062bc20
                if (!target) break;
                const auto info = tables.item_info.find(target->item->code);
                const int grid = info != tables.item_info.end() ? info->second.width * info->second.height : 0;
                if (grid == 0) break;
                int most = std::min(grid, 6);
                if (const auto type_found = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end(); type_found != tables.types.end()) {
                    const int ilvl = target->item->ilvl;
                    const int sockets_max = std::min(target->base->gem_sockets, type_found->second.max_sock[std::size_t(ilvl <= 25 ? 0 : ilvl <= 40 ? 1 : 2)]);
                    if (sockets_max <= most) most = sockets_max;
                }
                int amount = first >= 1 ? first : draw();
                if (amount < 1) amount = par;
                const int sockets = std::max(amount, 1) < most ? std::max(amount, 1) : most;
                if (sockets < 1) break;
                target->item->socketed = true;
                target->item->sockets = sockets;
                result = sockets;
                break;
            }
            case 15: result = mod.min; if (stat == kMinDamage) damage(kMinDamage, kSecondaryMinDamage, kThrowMinDamage, result); else put(stat, 0, result); break;
            case 16: result = mod.max; if (stat == kMaxDamage) damage(kMaxDamage, kSecondaryMaxDamage, kThrowMaxDamage, result); else put(stat, 0, result); break;
            case 17:
                result = par != 0 ? par : draw();
                if (result != 0 && stat == kMaxDamage) damage(kMaxDamage, kSecondaryMaxDamage, kThrowMaxDamage, result);
                else put(stat, 0, result);
                break;
            case 18: {                                     // FUN_0065f870: by time, at param 0
                if (stat < 0) break;
                const int low = std::clamp(mod.min + 256, 0, 1023), high = std::clamp(mod.max + 256, 0, 1023);
                put(stat, 0, std::clamp(par, 0, 3) + (high * 1024 + low) * 4);
                result = high;
                break;
            }
            case 19: {
                if (stat < 0) break;
                const int level = skill_level();
                const int charges = mod.min == 0 ? 5 : std::clamp(mod.min < 0 ? -mod.min + -mod.min * level / 8 : mod.min, 1, 255);
                const int now = target ? (rng(charges - charges / 8) + charges / 8 + 1) & 0xff : charges;
                out.push_back({ stat, par << 6 | (level & 63), now + charges * 256 });
                result = charges;
                break;
            }
            case 20: if (target) result = put(kIndestructible, 0, 1); break;
            case 21: result = put(stat, property_func.val, draw()); break;
            case 22: result = put(stat, par, draw()); break;
            case 23:
                if (target && !target->item->ethereal && target->item->max_durability > 0) {   // FUN_0065e4d0
                    target->item->ethereal = true;
                    if (target->kind == 1) target->item->defense = target->item->defense * 3 / 2;   // durability as it was (FUN_00556ca0 halves it, not this)
                    result = 1;
                }
                break;
            case 24: result = put(stat, par, value()); break;
            case 36: result = put(stat, draw(), property_func.val); break;
            default: break;
        }
        if (k == 0) first = result;
    }
}

// Affix level for an item of level ilvl with base qlvl (FUN_005c1560):
// max(ilvl, qlvl) plus the base's "magic lvl" (wands, staves, circlets),
// or without one less qlvl/2 below 99 - qlvl/2, else 2*level - 99; 1..99.
int affix_level(int ilvl, int qlvl, int magic_lvl) {
    const int level = std::max(ilvl, qlvl);
    return std::clamp(magic_lvl ? level + magic_lvl : level < 99 - qlvl / 2 ? level - qlvl / 2 : 2 * level - 99, 1, 99);
}

// A random affix row for an item of `type` at affix level alvl, weighted
// by frequency (x the row's level with a magic lvl, FUN_005c1560); skips
// groups already taken. 0 = none fits.
int pick_affix(const Tables& tables, const std::vector<Affix>& list, const std::string& type, int alvl, bool rare,
               const std::vector<int>& groups, Rng& rng, bool by_level) {
    std::vector<std::pair<int, int>> ok;                   // (row, frequency)
    int total = 0;
    for (std::size_t row = 1; row < list.size(); ++row) {
        const auto& affix = list[row];
        if (!affix.spawnable || affix.frequency <= 0 || affix.level > alvl || (affix.max_level > 0 && affix.max_level < alvl) || (rare && !affix.rare))
            continue;
        if (std::ranges::find(groups, affix.group) != groups.end()) continue;
        if (std::ranges::none_of(affix.itypes, [&](const auto& item_type) { return type_is(tables, type, item_type); })) continue;
        if (std::ranges::any_of(affix.etypes, [&](const auto& excluded_type) { return type_is(tables, type, excluded_type); })) continue;
        const int weight = by_level ? affix.frequency * affix.level : affix.frequency;
        ok.push_back({ int(row), weight });
        total += weight;
    }
    if (ok.empty()) return 0;
    int roll = rng(total);
    for (auto [affix_row, frequency] : ok) { if (roll < frequency) return affix_row; roll -= frequency; }
    return ok.back().first;
}

// A scroll's or tome's Books.txt row (FUN_005c2540, EDX 1 for a scroll):
// the first whose ScrollSpellCode (`scroll`) or BookSpellCode is `code`,
// the row count when none is (0sc, the plain Scroll, gets 3).
int books_row(const Tables& tables, const std::string& code, bool scroll) {
    for (std::size_t row = 0; row < tables.books.size(); ++row)
        if ((scroll ? tables.books[row].first : tables.books[row].second) == code) return int(row);
    return int(tables.books.size());
}

// A classic item's set (FUN_005c2740, its tail LAB_005c276a): both durabilities
// x2 (at most 255), n = the Sets rows before the first of version 100+, a start
// of rand(n) on the item's own seed (none for n < 1), then all n rows round,
// each one's first member of the item's code, the last row's find winning.
// None puts the seed back to (its low at entry, 666) (FUN_00650e40).
int classic_set_pick(const Tables& tables, const std::string& code, Rng& rng, int& durability, int& max_durability) {
    const std::uint32_t entry_low = rng.low;
    durability = std::min(durability * 2, 255);
    max_durability = std::min(max_durability * 2, 255);
    int count = 0;
    while (std::size_t(count) < tables.set_bonuses.size() && tables.set_bonuses[std::size_t(count)].version < 100) ++count;
    const int first = count < 1 ? 0 : rng(count);
    int picked = -1;
    for (int step = 0; step < count; ++step) {
        const int set = (step + first) % count;
        const auto member = std::ranges::find_if(tables.sets, [&](const Special& row) { return row.set == set && row.member >= 0 && row.code == code; });
        if (member != tables.sets.end()) picked = int(member - tables.sets.begin());
    }
    if (picked < 0) rng = Rng(entry_low);
    return picked;
}

// A new item of `code` at level ilvl and quality (4 magic, 5 set, 6 rare,
// 7 unique; a set / unique none fits is remade, below), identified, with
// its defence and durability rolled. A set 29 (the Cow King's) drops only
// from a Hell Bovine (`bovine`).

// A made drop (FUN_00558d90) passes its unit seed (+0x20): the base rolls
// (FUN_00557ab0) come off it: arrows / bolts (ItemTypes Quiver) min +
// rand(max - min); armor durability rand(dur / 2) + dur / 2 (at most
// 255), then defence minac + rand(maxac - minac + 1); a weapon's stack
// then its durability; misc stacks min + rand(spawnstack - min). `rng` is
// then its own seed (item data +4): the unique (FUN_005566b0) / set
// (FUN_005c25c0) pick is its first draw. `found_uniques` is the game's
// one-per-game list (+0x1b24): a unique found already fails (but a quest
// item's); one found is added unless nolimit, a quest item too. A failed
// unique gets 3x durability and is remade rare (magic when its type can't
// be rare), a failed set 2x and magic (FUN_00557450); off its own seed's
// start, each falls on to superior and normal as magic does. With a unit
// seed (a made drop) the rest of FUN_00557ab0 / FUN_00557450 too, in its
// order: the picture (ItemTypes VarInvGfx) off the unit seed; low
// (FUN_005c2d40): sub rand(4), durability dur*33/100, defence 75%;
// superior (FUN_005c2970): a QualityItems row rand(8) (4 if throwable or
// nodurability) redrawn until FUN_0065e7d0 fits, its mods (ac% sets the
// defence to maxac + 1 first, dur% raises durability); staffmods
// (FUN_005c0f90) on low / normal / superior; then ethereal (FUN_00556ca0:
// rand(100) < 5, not low / set / quest: defence 3/2, max durability / 2 +
// 1), sockets (FUN_00556b60, normal / superior: rand(100) < 33, the own
// seed's first low % the most + 1, the most = gemsockets / MaxSock by
// ilvl / 3, 4, 6 by difficulty) and the AutoMagic class affix
// (FUN_005c1560 off the base's auto prefix group). `special_wanted`: a
// unique / set row + 1 that wins with no draw when it fits (struct +0x40:
// FUN_005566b0, FUN_005c25c0).
// A classic item (`classic`: version 0, game +0x78 in a classic game,
// FUN_0055a550) takes the version-0 branches: low (FUN_005c2af0) durability
// dur / 3 and no staff mods; staff mods FUN_005c0d70 (tiers to 4 at ilvl
// 25, no item-type test or tries, skill 0x49 redrawn); a normal one's
// fix-ups FUN_00556d80 (a book or scroll by its own type, else the staff
// mods and a socket roll); no ethereal or class affix (FUN_00557450's
// tail, version 100+ only); sockets (FUN_00556b60) never on ItemTypes row 3
// (tors), else min(most, 3), 2 on a helm (row 0x25), with no draw for it.
// A rare's and a magic item's affixes (FUN_005c1bf0, FUN_005565e0) come off
// the own seed alone, so they are rolled the same with or without a unit
// seed (an imbue, tests); only the base rolls and the FUN_00557450 tail
// need the unit seed.
// ponytail: without a unit seed none of the other rolls above is made;
// base damage isn't kept on the item, so low's 75%, ethereal's 3/2 and
// dmg%'s +1 max damage on a weak weapon only reach its props. A code with
// no ItemBase row still gets the old stand-in affix counts (game.exe has
// no such item).
d2d::d2s::Item roll_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                         Rng* unit_seed, std::vector<bool>* found_uniques, bool bovine,
                         int difficulty, int special_wanted, bool classic) {
    d2d::d2s::Item item;
    item.code = code;
    item.version = classic ? 0 : 101;                      // item data +0x30 (FUN_0055a550: game +0x78)
    item.identified = true;
    item.ilvl = std::max(ilvl, 1);                         // FUN_00628220: at least 1, no cap (Uber Diablo's are 110)
    const auto* info = info_of(tables, item);
    const auto found = tables.item_base.find(code);
    const int qlvl = found != tables.item_base.end() ? found->second.level : 1;
    const std::string type = info ? info->type : std::string{};
    if (found != tables.item_base.end() && unit_seed) {
        const auto& base = found->second;
        auto& seed = *unit_seed;
        auto stack = [&](int bound) { item.quantity = std::max(base.min_stack + seed(bound - base.min_stack), 1); };
        auto durability = [&] {
            item.durability = std::min(seed(base.durability >> 1) + (base.durability >> 1), 255);
            item.max_durability = std::min(base.durability, 255);
        };
        if (type == "bowq" || type == "xboq") stack(base.max_stack);
        else if (info && info->kind == 1) { durability(); item.defense = base.minac + seed(base.maxac - base.minac + 1); }   // FUN_00556360: draws when minac == maxac too
        else if (info && info->kind == 2) { if (base.stackable) stack(base.max_stack); durability(); }
        else if (base.stackable) stack(base.spawn_stack < base.min_stack || base.spawn_stack == 0 ? std::max(base.min_stack, base.max_stack) : base.spawn_stack);
    } else if (found != tables.item_base.end()) {
        if (info && info->kind == 1) item.defense = rng.range(found->second.minac, found->second.maxac);
        if ((item.max_durability = found->second.durability) > 0) item.durability = item.max_durability;
    }
    const ItemBase* base = found != tables.item_base.end() ? &found->second : nullptr;
    const auto type_found = tables.types.find(type);
    const ItemType* item_type = type_found != tables.types.end() ? &type_found->second : nullptr;
    const int kind = info ? info->kind : 0;                // 1 armor, 2 weapon
    const std::uint32_t start = rng.low;                   // data+0x10 (FUN_00627df0): the socket count
    if (unit_seed && item_type && item_type->var_inv_gfx > 0) item.picture = (*unit_seed)(item_type->var_inv_gfx);
    auto stat_total = [&](int stat) {
        int total = 0;
        for (const auto& prop : item.props) if (prop.stat == stat) total += prop.value;
        return total;
    };
    auto max_durability = [&] { const int most = item.max_durability + stat_total(d2d::d2s::kMaxDurability); return most + most * stat_total(d2d::d2s::kMaxDurabilityPercent) / 100; };   // FUN_00625e00
    auto has_durability = [&] { return base && !base->no_durability && base->durability > 0 && item.max_durability > 0 && stat_total(d2d::d2s::kIndestructible) < 1; };
    // Staffmods (FUN_005c0f90): 0-3 class skills of the type's class off
    // tiers by ilvl, at level 1-3 (low: 1). A drop's percent add is 0.
    auto staff_mods = [&] {
        if (!item_type || item_type->staff_mods >= 7) return;
        auto percent = [&] { return int(rng.next() % 100); };
        const int roll = percent(), count = roll < 31 ? 0 : roll < 71 ? 1 : roll < 91 ? 2 : 3;
        if (classic) {                                     // FUN_005c0d70
            const int tier = item.ilvl >= 25 ? 4 : item.ilvl >= 19 ? 3 : item.ilvl >= 12 ? 2 : 1;
            const int first = tables.class_first_skill[std::size_t(item_type->staff_mods)];
            std::array<int, 3> picks{ -1, -1, -1 };
            for (int k = 0; k < count; ++k) {
                const int tier_roll = percent();
                const int skill_tier = std::max(tier_roll < 11 ? tier - 2 : tier_roll < 31 ? tier - 1 : tier_roll < 81 ? tier : tier + 1, 1);
                int skill = 0;
                do skill = int(rng.next() % 5) + (skill_tier - 1) * 5 + first;
                while (skill == 0x49 || std::ranges::find(picks, skill) != picks.end());
                picks[std::size_t(k)] = skill;
                const int level_roll = percent(), level = level_roll < 60 ? 1 : level_roll < 90 ? 2 : 3;
                const auto old = std::ranges::find_if(item.props, [&](const auto& prop) { return prop.stat == d2d::d2s::kSingleSkill && prop.param == skill; });
                if (old != item.props.end()) old->value = level;
                else item.props.push_back({ d2d::d2s::kSingleSkill, skill, level });
            }
            return;
        }
        const int tier = item.ilvl >= 37 ? 5 : item.ilvl >= 25 ? 4 : item.ilvl >= 19 ? 3 : item.ilvl >= 12 ? 2 : 1;
        const int first = tables.class_first_skill[std::size_t(item_type->staff_mods)];
        std::array<int, 3> picks{ -1, -1, -1 };
        for (int k = 0; k < count; ++k) {
            const int tier_roll = percent();
            int skill_tier = std::max(tier_roll < 11 ? tier - 2 : tier_roll < 31 ? tier - 1 : tier_roll < 81 ? tier : tier + 1, 1);
            if (item.quality == 1) skill_tier = std::min(skill_tier, 4);
            int skill = 0;
            for (int tries = 0; tries < 6; ++tries) {
                skill = int(rng.next() % 5) + (skill_tier - 1) * 5 + first;
                const bool usable = skill < 0 || std::size_t(skill) >= tables.skill_itype.size() || tables.skill_itype[std::size_t(skill)].empty()
                                 || type_is(tables, type, tables.skill_itype[std::size_t(skill)]);
                if (usable && std::ranges::find(picks, skill) == picks.end()) { picks[std::size_t(k)] = skill; break; }
            }
            int level = 1;
            if (item.quality != 1) { const int level_roll = percent(); level = level_roll < 60 ? 1 : level_roll < 90 ? 2 : 3; }
            const auto old = std::ranges::find_if(item.props, [&](const auto& prop) { return prop.stat == d2d::d2s::kSingleSkill && prop.param == skill; });
            if (old != item.props.end()) old->value = level;
            else item.props.push_back({ d2d::d2s::kSingleSkill, skill, level });
        }
    };
    // FUN_00556b60: a normal or superior item's sockets.
    auto roll_sockets = [&] {
        if (item.quality < 2 || item.quality > 3 || !base || !base->has_inv || base->stackable || !item_type || !info) return;
        const int by_ilvl = item_type->max_sock[std::size_t(item.ilvl <= 25 ? 0 : item.ilvl <= 40 ? 1 : 2)];
        const int most = std::min(base->gem_sockets, by_ilvl);                             // FUN_0062bc20
        const int capped = std::min(most, std::array{ 3, 4, 6 }[std::size_t(std::clamp(difficulty, 0, 2))]);
        if (capped <= 0 || (classic && type_is(tables, type, "tors")) || rng(100) >= 33) return;
        const int count = !classic ? int(start % std::uint32_t(capped)) + 1 : std::min(capped, type_is(tables, type, "helm") ? 2 : 3);
        item.socketed = true;                              // FUN_0062bcb0: at most the grid, 6
        item.sockets = std::min({ count, info->width * info->height, 6 });
    };
    // FUN_00557450's tail on every quality: ethereal, sockets, the class
    // affix (a classic item: the sockets only).
    auto finish = [&] {
        if (!unit_seed || !base) return std::move(item);
        if (!classic && (kind == 1 || kind == 2) && item.quality != 1 && item.quality != 5 && !base->quest && has_durability() && rng(100) < 5) {
            item.ethereal = true;                          // FUN_0065e4d0
            if (kind == 1) item.defense = item.defense * 3 / 2;
            if (has_durability()) { item.max_durability = item.max_durability / 2 + 1; item.durability = max_durability(); }
        }
        roll_sockets();
        static constexpr std::array kAuto{ 1, 2, 3, 4, 6, 8, 9 };
        if (!classic && base->auto_prefix != 0 && std::ranges::find(kAuto, item.quality) != kAuto.end()) {
            rng.next();                                    // FUN_005c1560's coin (forced on)
            const int alvl = affix_level(item.ilvl, qlvl, base->magic_lvl);
            std::vector<std::pair<int, int>> fits;         // (row, weight)
            int total = 0;
            for (std::size_t row = 0; row < tables.automagic.size(); ++row) {
                const auto& affix = tables.automagic[row];
                if (!affix.spawnable || affix.frequency <= 0 || affix.group != base->auto_prefix || affix.level > alvl || (affix.max_level > 0 && affix.max_level < alvl))
                    continue;
                if (!affix.rare && (item.quality == 6 || item.quality == 8 || item.quality == 9)) continue;   // FUN_005c1560: the rare column on a rare, crafted or tempered
                if (std::ranges::none_of(affix.itypes, [&](const auto& wanted_type) { return type_is(tables, type, wanted_type); })) continue;
                if (std::ranges::any_of(affix.etypes, [&](const auto& excluded_type) { return type_is(tables, type, excluded_type); })) continue;
                const int weight = base->magic_lvl ? affix.frequency * affix.level : affix.frequency;
                fits.push_back({ int(row), weight });
                total += weight;
            }
            if (!fits.empty()) {
                int roll = rng(total + 1), chosen = fits.back().first;   // FUN_00472280: 0..total, total the last
                for (const auto& [fit_row, weight] : fits) if ((roll -= weight) < 0) { chosen = fit_row; break; }
                item.class_affix = chosen + 1;
                const ModItem target{ &item, base, kind, item_type && item_type->throwable, false };
                for (const auto& mod : tables.automagic[std::size_t(chosen)].mods) apply_mod(tables, mod, item.props, rng, &target);
            }
        }
        if (base->quest && base->quest_diff) item.props.push_back({ d2d::d2s::kQuestItemDifficulty, 0, difficulty });   // FUN_00557ab0 after FUN_00557450
        return std::move(item);
    };
    // Low / normal / superior: no affixes; stacks (arrows, bolts, keys) roll
    // their quantity.
    // A normal item's fix-ups (FUN_00556f30, and FUN_005572a0 when another
    // quality doesn't take: FUN_00556e80 on an item of version 100+): a scroll's
    // (ItemTypes row 0x16) or a tome's (0x12) Books row into suffix slot
    // 0, the save's 5 bits (FUN_005c2540; the tome's test runs last).
    // A charm's affix (FUN_00556a60) and a body part's monster (FUN_00629df0)
    // aren't set: stubs.md, suspected unreachable. A classic item (version
    // 0) takes FUN_00556d80: the same Books row by its own type (0x16, 0x12),
    // any other the staff mods and a socket roll (`classic_rest`).
    auto fix_ups = [&] {
        if (item.quality != 2) return;
        if (classic ? type == "scro" : type_is(tables, type, "scro")) item.tome = books_row(tables, code, true);
        if (classic ? type == "book" : type_is(tables, type, "book")) item.tome = books_row(tables, code, false);
    };
    auto classic_rest = [&] { return classic && type != "scro" && type != "book" && type != "char" && type != "body" && type != "play"; };
    // Low / normal / superior (also magic's fallback, FUN_00557320).
    auto plain = [&](int plain_quality) {
        quality = plain_quality;
        item.quality = std::max(quality, 1);
        if (!unit_seed && base && base->stackable) item.quantity = rng.range(base->min_stack, base->max_stack);
        if (!unit_seed || !base) { fix_ups(); return std::move(item); }
        bool made = true;                                  // low / superior that doesn't take: FUN_005572a0, normal off a fresh own seed
        if (item.quality == 1 && kind != 1 && kind != 2) {
            made = false;
        } else if (item.quality == 1) {
            item.qsub = rng(4);
            if (has_durability()) {                        // a classic item's dur / 3 (FUN_005c2af0)
                const int most = std::max(classic ? base->durability / 3 : base->durability * 33 / 100, 1);
                item.durability = std::max((*unit_seed)(most >> 1) + (most >> 1), 1);
                item.max_durability = most;
            }
            if (kind == 1) item.defense = std::max(item.defense * 75 / 100, 1);
            if (!classic) staff_mods();
        } else if (item.quality == 3) {
            made = false;
            const bool four = (item_type && item_type->throwable) || base->no_durability;
            const int rows = four ? std::min(4, int(tables.superior.size())) : int(tables.superior.size());
            auto fits = [&](const Tables::Superior& row) {       // FUN_0065e7d0
                const auto& flag = row.fits;
                auto one_of = [&](std::initializer_list<std::string_view> codes) { return std::ranges::find(codes, std::string_view(type)) != codes.end(); };
                if (flag[1] && type_is(tables, type, "weap") && !one_of({ "staf", "bow", "xbow", "scep", "wand" })) return true;
                if (flag[0] && type_is(tables, type, "armo") && !one_of({ "shie", "boot", "glov", "belt" })) return true;
                return (type == "shie" && flag[2]) || (type == "scep" && flag[3]) || (type == "wand" && flag[4]) || (type == "staf" && flag[5])
                    || ((type == "bow" || type == "xbow") && flag[6]) || (type == "boot" && flag[7]) || (type == "glov" && flag[8]) || (type == "belt" && flag[9]);
            };
            std::vector<bool> tried(std::size_t(rows), false);
            for (int left = rows; left > 0; --left) {
                int pick = 0;
                do pick = rng(rows); while (tried[std::size_t(pick)]);
                const auto& row = tables.superior[std::size_t(pick)];
                if (!fits(row)) { tried[std::size_t(pick)] = true; continue; }
                item.qsub = pick;
                made = true;
                const ModItem target{ &item, base, kind, item_type && item_type->throwable, true };
                for (const auto& mod : row.mods) apply_mod(tables, mod, item.props, rng, &target);   // FUN_0065fec0 kind 1
                staff_mods();
                break;
            }
        }
        if (!made) { rng = Rng(start); item.quality = 2; item.qsub = -1; }
        fix_ups();                                         // before the staff mods, as FUN_00556e80 ends in FUN_005c1260
        if (item.quality == 2 && (!classic || classic_rest())) staff_mods();
        if (item.quality == 2 && classic_rest()) roll_sockets();
        return finish();
    };
    if (quality <= 3) return plain(quality);
    auto special = [&](const std::vector<Special>& list) {
        auto fits = [&](const Special& row) { return row.enabled && !row.ladder && row.code == code && row.level <= item.ilvl && (row.set != 29 || bovine); };
        int total = 0, pick = -1;
        for (const auto& row : list) if (fits(row)) total += std::max(1, row.rarity);
        if (total == 0) return -1;
        // The forced row (struct +0x40, row + 1: a TC's unique / set entry) when it fits, no draw.
        if (special_wanted > 0 && std::size_t(special_wanted - 1) < list.size() && fits(list[std::size_t(special_wanted - 1)])) return special_wanted - 1;
        int roll = rng(total);
        for (std::size_t i = 0; i < list.size() && pick < 0; ++i) {
            if (!fits(list[i])) continue;
            if (roll < std::max(1, list[i].rarity)) pick = int(i);
            else roll -= std::max(1, list[i].rarity);
        }
        return pick;
    };
    // A classic item's (version 0) unique (FUN_005566b0's branch): its
    // durability x5 first, then the first row of version < 100, enabled,
    // of its code, not ladder (outside a ladder game) and not found yet
    // (but for a quest item), no draw and no level test; none marks it
    // found. Its set (d2d_set_pick_old, 0x5c2740): durability x2, then
    // rand(n) over the n leading Sets rows of version < 100, and from that
    // set on, round all n, each set's first member of the item's code: the
    // last one found wins. Failing either adds no durability in
    // FUN_00557450 (x3 / x2 there are version 100+ only).
    auto classic_special = [&]() -> int {
        auto times = [&](int factor) {
            item.durability = std::min(item.durability * factor, 255);
            item.max_durability = std::min(item.max_durability * factor, 255);
        };
        if (quality == 7) {
            if (has_durability()) times(5);
            for (std::size_t row = 0; row < tables.uniques.size(); ++row) {
                const auto& unique = tables.uniques[row];
                const bool found_already = found_uniques && row < found_uniques->size() && (*found_uniques)[row];
                if (unique.enabled && unique.version < 100 && !unique.ladder && unique.code == code && (!found_already || (base && base->quest))) return int(row);
            }
            return -1;
        }
        const int picked = classic_set_pick(tables, code, rng, item.durability, item.max_durability);
        if (picked >= 0) item.identified = false;          // FUN_006280d0(item, 0x10, 0) clears the identified flag
        return picked;
    };
    if (quality == 7 || quality == 5) {
        const auto& list = quality == 7 ? tables.uniques : tables.sets;
        const int picked = classic ? classic_special() : special(list);
        int special_row = picked;
        // FUN_005566b0 tests the one-per-game bit (FUN_005564a0: game +0x1b24;
        // an id over 0x1000, past UniqueItems' rows, reads as found), but for a quest item;
        // FUN_00556530 then sets it unless nolimit (UniqueItems +0x2c bit
        // 2), a quest item's too.
        if (!classic && quality == 7 && special_row >= 0 && found_uniques && base) {
            if (found_uniques->size() <= std::size_t(special_row)) found_uniques->resize(std::size_t(special_row) + 1);
            if (!base->quest && (*found_uniques)[std::size_t(special_row)]) special_row = -1;
            else if (!list[std::size_t(special_row)].nolimit) (*found_uniques)[std::size_t(special_row)] = true;
        }
        if (special_row >= 0) {
            item.quality = quality;
            (quality == 7 ? item.unique_id : item.set_id) = special_row;
            const ModItem target{ &item, base, kind, item_type && item_type->throwable, false };
            const auto& row = list[std::size_t(special_row)];
            for (const auto& mod : row.mods) apply_mod(tables, mod, item.props, rng, base ? &target : nullptr);
            // A set's bonuses: aprop(i)a / b to list i - 1 (the d2s set lists,
            // on with i + 1 pieces worn), or the item's own with no add func.
            std::array<std::vector<d2d::d2s::ItemProp>, 5> lists;
            for (std::size_t k = 0; k < row.bonus.size(); ++k)
                apply_mod(tables, row.bonus[k], row.bonus_apart ? lists[k / 2 % 5] : item.props, rng, base ? &target : nullptr);
            for (std::size_t i = 0; i < lists.size(); ++i)
                if (!lists[i].empty()) {
                    item.set_lists |= 1 << i;
                    item.set_list_sizes.push_back(lists[i].size());
                    item.set_props.insert(item.set_props.end(), lists[i].begin(), lists[i].end());
                }
            return finish();
        }
        if (!classic && quality == 7 && picked < 0 && base && base->only_unique) {    // no row (the Malus, Gidbinn): FUN_005566b0 keeps it unique, id unset
            item.quality = 7;
            return finish();
        }
        if (!classic && has_durability()) {                // FUN_00629930: durability, not nodurability (6hx has both), not indestructible
            const int times = quality == 7 ? 3 : 2;
            item.durability = std::min(item.durability * times, 255);
            item.max_durability = std::min(item.max_durability * times, 255);
        }
        // Remade (FUN_005573f0 rare after a unique, FUN_00557380 magic after
        // a set or a rare that won't take): the affixes cleared, the own seed
        // back to {start, 666} (FUN_00627e20, FUN_00650e40). FUN_005c21a0
        // refuses a type that can't be rare (a charm), so that one goes magic.
        rng = Rng(start);
        item.qsub = -1;                                    // FUN_00557250: +0x28 to -1
        quality = quality == 7 && item_type && item_type->can_rare ? 6 : 4;
    }
    const int magic_lvl = found != tables.item_base.end() ? found->second.magic_lvl : 0;
    const int alvl = affix_level(item.ilvl, qlvl, magic_lvl);
    std::vector<int> groups;
    auto add = [&](bool prefix) {
        const auto& list = prefix ? tables.prefixes : tables.suffixes;
        const int affix_row = pick_affix(tables, list, type, alvl, quality == 6, groups, rng, magic_lvl != 0);
        if (affix_row <= 0) return 0;
        groups.push_back(list[std::size_t(affix_row)].group);
        const ModItem target{ &item, base, kind, item_type && item_type->throwable, false };   // FUN_0065fec0 with the item, as a unique's
        for (const auto& mod : list[std::size_t(affix_row)].mods) apply_mod(tables, mod, item.props, rng, base ? &target : nullptr);
        return affix_row;
    };
    // FUN_005c1560 (version 100+), the affix pick of magic and rare off the
    // own seed: a coin first (one step: even and not forced, none); the
    // affix level (as the class affix'); the rows spawnable, in the level
    // window, of the type, the item's class or none, frequency > 0, on a
    // rare (crafted, tempered) the rare column, no group the item has
    // (FUN_005c1500); weight
    // frequency (x level with MagicLvl); rand(total + 1), the last when it
    // runs past. Its mods are the caller's (apply_affix).
    static constexpr std::array<std::string_view, 7> kClass = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
    const int item_class = item_type ? int(std::ranges::find(kClass, std::string_view(item_type->cls)) - kClass.begin()) : 7;
    const int magic_alvl = affix_level(item.ilvl, qlvl, base ? base->magic_lvl : 0);
    // FUN_0065e620: an affix whose first mod sets the sockets (stat 0xc2)
    // only on an item that can hold some (hasinv and a socket count).
    const int by_ilvl = item_type ? item_type->max_sock[std::size_t(item.ilvl <= 25 ? 0 : item.ilvl <= 40 ? 1 : 2)] : 0;
    const bool socketable = base && base->has_inv && std::min(base->gem_sockets, by_ilvl) > 0;
    auto sockets_mod = [&](const Affix& affix) {
        if (affix.mods.empty()) return false;
        const auto property = tables.properties.find(lower_case(affix.mods.front().code));
        return property != tables.properties.end() && !property->second.empty() && property->second.front().stat == 0xc2;
    };
    auto choose = [&](const std::vector<Affix>& list, bool forced) {
        const auto coin = rng.next();
        if (!(coin & 1) && !forced) return 0;
        std::vector<std::pair<int, int>> fits;
        int total = 0;
        for (std::size_t row = 0; row < list.size(); ++row) {
            const auto& affix = list[row];
            if (!affix.spawnable || affix.frequency <= 0 || affix.level > magic_alvl || (affix.max_level > 0 && affix.max_level < magic_alvl)) continue;
            if (!affix.rare && (item.quality == 6 || item.quality == 8 || item.quality == 9)) continue;
            if (affix.class_specific >= 0 && item_class != 7 && affix.class_specific != item_class) continue;
            if (std::ranges::none_of(affix.itypes, [&](const auto& wanted_type) { return type_is(tables, type, wanted_type); })) continue;
            if (std::ranges::any_of(affix.etypes, [&](const auto& excluded_type) { return type_is(tables, type, excluded_type); })) continue;
            if (std::ranges::find(groups, affix.group) != groups.end()) continue;
            if (!socketable && sockets_mod(affix)) continue;
            const int weight = base && base->magic_lvl ? affix.frequency * affix.level : affix.frequency;
            fits.push_back({ int(row), weight });
            total += weight;
        }
        if (fits.empty()) return 0;
        int roll = rng(total + 1), chosen = fits.back().first;
        for (const auto& [fit_row, weight] : fits) if ((roll -= weight) < 0) { chosen = fit_row; break; }
        groups.push_back(list[std::size_t(chosen)].group);
        return chosen;
    };
    // FUN_005c12f0, a classic item's (version 0) pick: the coin as
    // FUN_005c1560's; then the rows spawnable, of version < 100, of level
    // at most ilvl + 2 (99 at most), that FUN_0065e620 takes (a version-0
    // item: none on a stackable or throwable one), at most 0x1ff of them;
    // rand(count) over them, no weights, no max level, rare column, class
    // or group test.
    auto choose_classic = [&](const std::vector<Affix>& list, bool forced) {
        const auto coin = rng.next();
        if (!(coin & 1) && !forced) return 0;
        const int most = std::min(item.ilvl + 2, 99);
        const bool takes_none = (base && base->stackable) || (item_type && item_type->throwable);
        std::vector<int> fits;
        for (std::size_t row = 0; row < list.size() && !takes_none; ++row) {
            const auto& affix = list[row];
            if (!affix.spawnable || affix.version >= 100 || affix.level > most) continue;
            if (std::ranges::none_of(affix.itypes, [&](const auto& wanted_type) { return type_is(tables, type, wanted_type); })) continue;
            if (std::ranges::any_of(affix.etypes, [&](const auto& excluded_type) { return type_is(tables, type, excluded_type); })) continue;
            if (!socketable && sockets_mod(affix)) continue;
            if (fits.size() < 0x1ff) fits.push_back(int(row));
        }
        return fits.empty() ? 0 : fits[std::size_t(rng(int(fits.size())))];
    };
    auto pick = [&](const std::vector<Affix>& list, bool forced) { return classic ? choose_classic(list, forced) : choose(list, forced); };
    auto apply_affix = [&](const std::vector<Affix>& list, int row) {   // FUN_0065fec0 with the item, as a unique's
        const ModItem target{ &item, base, kind, item_type && item_type->throwable, false };
        for (const auto& mod : list[std::size_t(row)].mods) apply_mod(tables, mod, item.props, rng, &target);
    };
    if (quality == 6 && base) {
        // Rare (FUN_00557450 case 6 -> FUN_005c21a0 -> FUN_005c1bf0): two
        // names, a RarePrefix then a RareSuffix row (FUN_005c1ab0: rand over
        // the rows of one of the item's itypes and none of its etypes,
        // FUN_0065e710; a prefix's id counts on past the suffixes); with
        // both, the count: rand(2) + 3 for a jewel (ItemTypes row 0x3a, the
        // item's own type), else {3, 4, 4, 5, 5, 5, 6, 6}[next & 7]
        // (0x6e3014). Each slot a coin (odd: a suffix) while neither side
        // is full, at most 3 a side, picked forced (FUN_005c1560); a side
        // that finds none is full and the slot goes again. Then each slot's
        // mods (prefix 0, suffix 0, prefix 1 ..), the staff mods
        // (FUN_005c1260) and the tail.
        item.quality = 6;
        auto rare_name = [&](const std::vector<Tables::RareName>& names, int first) {
            std::vector<int> fits;
            for (std::size_t row = 0; row < names.size(); ++row) {
                const auto& name = names[row];
                if (std::ranges::any_of(name.etypes, [&](const auto& excluded_type) { return type_is(tables, type, excluded_type); })) continue;
                if (std::ranges::any_of(name.itypes, [&](const auto& wanted_type) { return type_is(tables, type, wanted_type); })) fits.push_back(int(row));
            }
            return fits.empty() ? 0 : first + fits[std::size_t(rng(int(fits.size())))] + 1;
        };
        item.rare1 = rare_name(tables.rare_prefix_names, int(tables.rare_suffix_names.size()));
        item.rare2 = rare_name(tables.rare_suffix_names, 0);
        int prefixes = 0, suffixes = 0;
        if (item.rare1 && item.rare2 && classic) {
            // FUN_005c1e80 (version 0): the count rand(2) + 3 for a jewel,
            // else rand(3) + 4; each slot a coin (odd: a suffix) while
            // neither side has 3, a suffix when the prefixes have; up to
            // 252 forced picks (FUN_005c12f0) until one shares no row or
            // group with that side's; a slot whose pick finds none, or
            // whose tries run out, is spent.
            const int count = type == "jewl" ? 3 + rng(2) : 4 + rng(3);
            for (int slot = 0; slot < count; ++slot) {
                const bool suffix = prefixes == 3 || (suffixes != 3 && (rng.next() & 1) != 0);
                const auto& list = suffix ? tables.suffixes : tables.prefixes;
                int& taken = suffix ? suffixes : prefixes;
                const std::size_t side = suffix ? 1 : 0;
                for (int tries = 0; tries < 252 && taken < 3; ++tries) {
                    const int row = choose_classic(list, true);
                    if (!row) break;
                    bool clash = false;
                    for (std::size_t held_slot = 0; held_slot < 3; ++held_slot)
                        if (const int held = item.affixes[held_slot * 2 + side]; held && (held == row || list[std::size_t(held)].group == list[std::size_t(row)].group)) clash = true;
                    if (!clash) { item.affixes[std::size_t(taken++) * 2 + side] = row; break; }
                }
            }
        } else if (item.rare1 && item.rare2) {
            static constexpr std::array kRareCount{ 3, 4, 4, 5, 5, 5, 6, 6 };
            const int count = type == "jewl" ? 3 + rng(2) : kRareCount[rng.next() & 7];
            bool prefixes_full = false, suffixes_full = false;
            for (int slot = 0; slot < count; ++slot) {
                if (prefixes_full && suffixes_full) break;
                const bool suffix = prefixes_full || (!suffixes_full && (rng.next() & 1) != 0);
                const int row = choose(suffix ? tables.suffixes : tables.prefixes, true);
                if (!row) { (suffix ? suffixes_full : prefixes_full) = true; --slot; continue; }
                int& taken = suffix ? suffixes : prefixes;
                item.affixes[std::size_t(taken * 2 + (suffix ? 1 : 0))] = row;
                if (++taken > 2) (suffix ? suffixes_full : prefixes_full) = true;
            }
        }
        if (prefixes || suffixes) {
            for (std::size_t slot = 0; slot < item.affixes.size(); ++slot)
                if (item.affixes[slot]) apply_affix(slot % 2 ? tables.suffixes : tables.prefixes, item.affixes[slot]);
            staff_mods();
            return finish();
        }
        // None (FUN_00557380): the names and affixes cleared, the own seed
        // back to its start, remade magic.
        item.rare1 = item.rare2 = 0;
        item.affixes = {};
        groups.clear();
        rng = Rng(start);
        quality = 4;
    }
    if (quality == 6) {
        // Rare of a code with no ItemBase row: not game.exe's roll.
        item.quality = 6;
        item.rare1 = tables.rare_prefixes > 0 ? 156 + rng(tables.rare_prefixes) : 0;
        item.rare2 = tables.rare_suffixes > 0 ? 1 + rng(tables.rare_suffixes) : 0;
        const int sockets = 3 + rng(4);
        for (int k = 0; k < sockets; ++k) {
            const bool prefix = k % 2 == 0;
            item.affixes[std::size_t(k / 2 * 2 + (prefix ? 0 : 1))] = add(prefix);
        }
        return item;
    }
    if (quality == 4 && base) {
        // Magic (FUN_00557450 case 4 -> FUN_005565e0): a prefix, then a
        // suffix (one at least: the suffix is forced when no prefix came),
        // each FUN_005c1560 (choose) with its mods at once. Then the staff
        // mods (FUN_005c1260) and the tail.
        item.quality = 4;
        if ((item.prefix = pick(tables.prefixes, false))) apply_affix(tables.prefixes, item.prefix);
        if ((item.suffix = pick(tables.suffixes, item.prefix == 0))) apply_affix(tables.suffixes, item.suffix);
        // Neither (FUN_00557320): the affixes cleared, the own seed back to
        // its start, remade superior (and normal when that doesn't take).
        if (!item.prefix && !item.suffix) { rng = Rng(start); return plain(3); }
        staff_mods();
        return finish();
    }
    // Magic of a code with no ItemBase row: not game.exe's, and no game
    // path comes here. Tests only.
    item.quality = 4;
    const int shape = rng(4);                              // 0 prefix, 1 suffix, 2-3 both
    if (shape != 1) item.prefix = add(true);
    if (shape != 0) item.suffix = add(false);
    if (!item.prefix && !item.suffix) return roll_item(tables, code, ilvl, 3, rng, unit_seed, found_uniques, bovine, difficulty, 0, classic);
    return item;
}

// The item as roll_item makes it, with its unit's seed: the fingerprint
// (unit +0x28, what a save keeps) is the low word the unit seed starts from
// (FUN_00552df0: a step of the game seed, then FUN_00650e40 {it, 666}), and
// the seed goes on from where the making left it (Item::seed_kept).
d2d::d2s::Item generate_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                             Rng* unit_seed, std::vector<bool>* found_uniques, bool bovine,
                             int difficulty, int special_wanted, bool classic) {
    const std::uint32_t fingerprint = unit_seed ? unit_seed->low : 0;
    auto item = roll_item(tables, code, ilvl, quality, rng, unit_seed, found_uniques, bovine, difficulty, special_wanted, classic);
    if (unit_seed) {
        item.uid = fingerprint;
        item.seed_low = unit_seed->low;
        item.seed_high = unit_seed->high;
        item.seed_kept = true;
    }
    return item;
}

// The item's unit seed (+0x20): where the making left it while it's in play,
// else fresh from its fingerprint, as a loaded item's (FUN_0062cbe0 at
// 0x62cd16 seeds the unit {fingerprint, 666}).
Rng item_seed(const d2d::d2s::Item& item) {
    if (!item.seed_kept) return Rng{ item.uid };
    Rng seed;
    seed.low = item.seed_low;
    seed.high = item.seed_high;
    return seed;
}

bool sellable(const Tables& tables, const d2d::d2s::Item& item) {
    if (item.flags & kItemNoService) return false;
    const auto base = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    return !(base != tables.item_base.end() && base->second.quest) && !(info != tables.item_info.end() && info->second.type == "ques");
}

// Whether Charsi's imbue takes `item` (FUN_0062c590): Items bitfield1
// bit 0, not gold, no flag 0x1000, not socketed (flag 0x800) nor holding
// anything (FUN_0062a270), not a quest item but Wirt's leg, quality low /
// normal / superior.

// A throwable type (FUN_0062ba80) is refused in a classic game only: the
// unit's +0xc8 bit 0x2000000 is the game's expansion flag (game +0x70,
// set by FUN_00555230).
bool imbuable(const Tables& tables, const d2d::d2s::Item& item, bool expansion) {
    const auto found = tables.item_base.find(item.code);
    if (item.code == "gld" || (item.flags & kItemNoService) || found == tables.item_base.end() || !(found->second.bitfield1 & 1)) return false;
    if (!expansion) {
        const auto info = tables.item_info.find(item.code);
        const auto type = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end();
        if (type != tables.types.end() && type->second.throwable) return false;
    }
    if (found->second.quest && item.code != "leg") return false;
    return !item.socketed && item.socketed_items.empty() && item.quality >= 1 && item.quality <= 3;
}

// The imbued item (FUN_00579d60, kind 0 at Charsi): the same base made
// anew, rare (+0x30 = 6), item level player_ilvl plus 4 when that is past
// 5, a fresh seed; ethereal stays so (flag 4 / 2) and a personalized name is kept.
d2d::d2s::Item imbue_item(const Tables& tables, const d2d::d2s::Item& item, int clvl, Rng& rng) {
    const int ilvl = player_ilvl(clvl);
    auto made = generate_item(tables, item.code, ilvl > 5 ? ilvl + 4 : ilvl, 6, rng);
    made.ethereal = item.ethereal;
    made.personalized = item.personalized;
    made.owner = item.owner;
    return made;
}

// Exceptional / elite upgrade weights out of 10000 for a gamble at clvl
// (FUN_00629370): (clvl - qlvl) * 100 / 2 + 1 and * 100 / 4 + 1.
std::pair<int, int> gamble_upgrade(const Tables& tables, const std::string& code, int clvl) {
    const auto found = tables.item_base.find(code);
    if (found == tables.item_base.end()) return { 0, 0 };
    auto weight = [&](const std::string& upgrade_code, int div) {
        const auto upgrade_base = tables.item_base.find(upgrade_code);
        if (upgrade_code.empty() || upgrade_code == code || upgrade_base == tables.item_base.end()) return 0;
        return std::max(0, (clvl - upgrade_base->second.level) * 100 / div + 1);
    };
    return { weight(found->second.ubercode, 2), weight(found->second.ultracode, 4) };
}

// The most sockets `item` may have (FUN_0062bc20): its gemsockets, capped
// by its type's MaxSock1 / 25 / 40 for item level <= 25, <= 40, above.
int most_sockets(const Tables& tables, const d2d::d2s::Item& item) {
    const auto base = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    if (base == tables.item_base.end() || info == tables.item_info.end()) return 0;
    const auto type = tables.types.find(info->second.type);
    if (type == tables.types.end()) return 0;
    return std::min(base->second.gem_sockets, type->second.max_sock[std::size_t(item.ilvl <= 25 ? 0 : item.ilvl <= 40 ? 1 : 2)]);
}

// Whether Larzuk's socketing takes `item` (FUN_0062c770): not gold, no
// flag 0x1000, not a quest item but Wirt's leg, not broken (flag 0x100),
// holding nothing (FUN_0062a270), not socketed (flag 0x800), most_sockets
// above 0 (FUN_0062bc20) and no item_numsockets (stat 194, FUN_006299b0).
bool socketable(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_base.find(item.code);
    if (item.code == "gld" || (item.flags & kItemNoService) || found == tables.item_base.end() || (found->second.quest && item.code != "leg")) return false;
    if (broken(item) || !item.socketed_items.empty()) return false;
    const bool numsockets = std::ranges::any_of(item.props, [](const auto& prop) { return prop.stat == 194 && prop.value != 0; });
    return !item.socketed && item.sockets == 0 && !numsockets && most_sockets(tables, item) > 0;
}

// Larzuk's sockets (FUN_00579d60 at hcIdx 0x1ff): magic 1 + rand(the most,
// at most 2) on the item's seed; set, rare, unique, crafted (5..9) 1;
// the rest the most.

// FUN_0062bcb0 caps them at the grid and 6. The item's seed is its unit's
// (item_seed): fresh from its fingerprint for one loaded from a save,
// where its making left it for one made in play.
d2d::d2s::Item socket_item(const Tables& tables, const d2d::d2s::Item& item) {
    auto made = item;
    int sockets = most_sockets(tables, item);
    if (item.quality == 4) {
        auto seed = item_seed(item);
        sockets = seed(std::min(sockets, 2)) + 1;
        made.seed_low = seed.low; made.seed_high = seed.high; made.seed_kept = true;
    }
    else if (item.quality >= 5 && item.quality <= 9 && sockets > 0) sockets = 1;
    const auto [width, height] = item_size(tables, item.code);
    made.socketed = true;
    made.sockets = std::min({ sockets, width * height, 6 });
    return made;
}

// Whether Anya's personalizing takes `item` (FUN_0062c6a0): not gold, a
// quiver or an ear (types 5..7), not broken or personalized already, not a
// compact-save item (Items +0x143) and nameable (+0x145); no flag 0x1000,
// not broken (0x100).
bool nameable(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    if (item.code == "gld" || item.code == "ear" || (item.flags & kItemNoService) || broken(item) || found == tables.item_base.end() || info == tables.item_info.end()) return false;
    if (info->second.type == "bowq" || info->second.type == "xboq") return false;
    return found->second.nameable && !item.personalized && !item.simple;
}

// Gheed's price for gambling on `item_code` (FUN_00629370): rings and amulets
// cost their "gamble cost"; the rest mix the base, exceptional and elite
// costs by upgrade odds and scale with character level.
int gamble_price(const Tables& tables, const std::string& item_code, int clvl, int reduced) {
    // An upgraded base (exceptional or elite) is priced as its normal one: FUN_006287d0's normcode.
    const auto item = tables.item_base.find(item_code);
    if (item == tables.item_base.end()) return 0;
    const std::string& code = item->second.normcode.empty() ? item_code : item->second.normcode;
    const auto found = tables.item_base.find(code);
    if (found == tables.item_base.end()) return 0;
    const auto& base = found->second;
    if (code == "rin" || code == "amu") return int(price_reduced(base.gamble_cost, reduced));
    const auto [exceptional_chance, elite_chance] = gamble_upgrade(tables, code, clvl);
    auto cost_of = [&](const std::string& upgrade_code) { const auto upgrade_base = tables.item_base.find(upgrade_code); return upgrade_base != tables.item_base.end() ? upgrade_base->second.cost : 0; };
    const long long stack = std::max(1, (base.min_stack + base.max_stack) / 2);
    const int level = std::max(clvl, 5);
    const long long mix = ((10000LL - elite_chance - exceptional_chance) * base.cost * stack + (long long)cost_of(base.ultracode) * elite_chance
                           + (long long)cost_of(base.ubercode) * exceptional_chance) / 10000;
    const long long lvl = ((std::max(base.level - 45, 0) - base.level / 2 + level) * 250LL) / 3;
    return int(price_reduced((lvl + mix) * ((level * 2 + 1) / 3 + 20) / 15, reduced));
}

// The gamble table (FUN_00638cc0, built at load): gamble.txt's bases sorted
// by level with game.exe's qsort (FUN_00638ac0 compares Items +0xfd).
std::vector<std::string> gamble_by_level(const Tables& tables) {
    std::vector<std::string> codes;
    for (const auto& code : tables.gamble) if (tables.item_base.contains(code)) codes.push_back(code);
    msvc_qsort(codes, [&](const std::string& left, const std::string& right) { return tables.item_base.at(left).level < tables.item_base.at(right).level; });
    return codes;
}

// The gamble upgrade (FUN_005786a0, expansion games only): the ubercode at
// (level - its level) x GambleUber + 1 of 10000 on the proxy seed, failing
// that the ultracode at GambleUltra's; no roll when that weight is under 1
// or the code is none (the ultracode isn't tried then either).
std::string gamble_upgrade_code(const Tables& tables, const std::string& code, int level, const GambleRates& rates, Rng& proxy) {
    const auto found = tables.item_base.find(code);
    if (found == tables.item_base.end()) return code;
    auto weight = [&](const std::string& upgrade, int per_level) {
        const auto upgrade_base = tables.item_base.find(upgrade);
        return upgrade_base == tables.item_base.end() ? 0 : (level - upgrade_base->second.level) * per_level + 1;
    };
    const int uber = weight(found->second.ubercode, rates.uber);
    if (uber < 1) return code;
    if (proxy(10000) < uber) return found->second.ubercode;
    const int ultra = weight(found->second.ultracode, rates.ultra);
    return ultra >= 1 && proxy(10000) < ultra ? found->second.ultracode : code;
}

// Gheed's gamble screen (FUN_00578790, rolled when it opens): 14 items on
// the proxy seed, each a level clvl - 5 + rand(10) (5 .. 99), a base
// rand(the bases up to that level) from gamble_by_level (a classic game
// rolls again past expansion bases), the first two a ring and an amulet
// instead, then the upgrade, then the quality: rand(100000) under rare +
// set + unique is unique / set / rare by those odds, else magic.

// Each is made as a drop (FUN_00559ce0: two game seed steps,
// generate_item), unidentified, and put on its type's page; one that
// doesn't fit ends the roll. The list is the player's (FUN_00578ae0 rolls
// when the vendor holds none for them) and lasts the talk: ending it
// (C->S 0x30, FUN_00572f20 -> FUN_00537190) frees it, so each talk rolls
// a new one; FUN_00572f20 frees it only when no other player is still
// talking to the vendor, and a list kept that way comes back at the
// player's next talk (World::deal).
Store open_gamble(const Tables& tables, std::string npc_id, int clvl, int difficulty, bool expansion, Rng& proxy, Rng& game,
                  std::vector<bool>* found_uniques) {
    Store store;
    store.npc_id = std::move(npc_id);
    store.gamble = true;
    const auto codes = gamble_by_level(tables);
    const auto& rates = tables.gamble_rates[std::size_t(std::clamp(difficulty, 0, 2))];
    for (int made = 0; made < 14 && !codes.empty();) {
        int level = int(proxy.next() % 10) - 5 + clvl;
        level = level < 6 ? 5 : level > 98 ? 99 : level;
        const auto up_to = std::ranges::count_if(codes, [&](const std::string& code) { return tables.item_base.at(code).level <= level; });
        std::string code = codes[std::size_t(proxy(int(up_to)))];
        if (!expansion && tables.item_base.at(code).version >= 100) continue;
        if (made < 2) code = made == 0 ? "rin" : "amu";
        if (expansion) code = gamble_upgrade_code(tables, code, level, rates, proxy);
        int quality = 4;
        if (const int odds = rates.rare + rates.set + rates.unique; odds > 0) {
            const int roll = int(proxy.next() % 100000);
            if (roll < odds) quality = roll < rates.unique ? 7 : roll < rates.unique + rates.set ? 5 : 6;
        }
        ++made;
        Rng unit_seed{ game.next() }, item_seed{ game.next() };
        if (!expansion) (void)stand_quality(tables, code, level, item_seed, true);   // FUN_00557450: a classic item's roll draws all the same
        auto item = generate_item(tables, code, level, quality, item_seed, &unit_seed, found_uniques, false, difficulty, 0, !expansion);
        item.identified = false;
        const auto info = tables.item_info.find(item.code);
        const auto type = info != tables.item_info.end() ? tables.types.find(info->second.type) : tables.types.end();
        const int page = type != tables.types.end() ? type->second.store_page : -1;
        if (page < 0 || !store_place(tables, store, page, std::move(item))) break;
    }
    store.first_tab();
    return store;
}

// Gambles on stock item i of the open tab: pays its gamble price
// (FUN_00629370 on its code; carried gold, then the stash) and takes it
// into the inventory, identified (C->S 37 -> FUN_00578640). False if it
// can't be paid for or doesn't fit.
Trade store_gamble(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    auto& tab = store.tabs[std::size_t(store.tab)];
    auto item = tab[std::size_t(index)];
    const int price = gamble_price(tables, item.code, int(stats.get(d2d::d2s::kLevel)), store.reduced);
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < price) return Trade::no_gold;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& x : items) if (x.location == d2d::d2s::item_location::kStored && x.panel == d2d::d2s::item_panel::kInventory) inv.push_back(&x);
    const auto [width, height] = item_size(tables, item.code);
    const auto [column, row] = free_spot(tables, inv, 10, 4, width, height, true);
    if (column < 0) return Trade::no_room;
    item.identified = true;
    item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory; item.column = column; item.row = row;
    items.push_back(std::move(item));
    tab.erase(tab.begin() + index);
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), price);
    stats.values[d2d::d2s::kGold] -= from_inv;
    stats.values[d2d::d2s::kGoldBank] -= price - from_inv;
    return Trade::done;
}

// The list: the entries in name order, each a game seed step, then ten
// picks of rand(names) on the game seed, each moving on past a taken
// entry (wrapping; back at its start the picking ends).
std::vector<HireEntry> hire_entries(int names, Rng& game) {
    std::vector<HireEntry> entries;
    for (int name = 0; name < names; ++name) entries.push_back({ name, game.next() });
    for (int pick = 0; pick < 10 && names > 0; ++pick) {
        const int start = game(names);
        int slot = start;
        while (entries[std::size_t(slot)].offered || entries[std::size_t(slot)].hired) {
            slot = (slot + 1) % names;
            if (slot == start) return entries;
        }
        entries[std::size_t(slot)].offered = true;
    }
    return entries;
}

// The seller's names (FUN_00575ff0: its first row's NameFirst..NameLast)
// for act (0-based), difficulty and version.
int hire_names(const Tables& tables, bool expansion, int act, int diff) {
    for (const auto& hireling : tables.hirelings)
        if (hireling.act == act + 1 && hireling.difficulty == diff + 1 && hireling.version == (expansion ? 100 : 0)) return hireling.names;
    return 0;
}

// An entry's offer (FUN_006637f0) for act (0-based) and difficulty (0-2),
// on its own seed {seed, 666}: FUN_00656580 lists
// the rows of that act, difficulty and version (100 = LoD) sharing the
// first one's Level; one is picked, the level is clvl - 5 + rand(5)
// (at least 2), and with d = level - the row's Level:

//   life = HP + HP/Lvl * d (>= 40), str/dex = base + (per-level * d >> 3)
//   (>= 10), cost = Gold * (15d + 100) / 100 (>= Gold), exp =
//   (level + 1) * Exp/Lvl * level^2, def = Def + Def/Lvl * d, damage =
//   Dmg-Min/Max + (Dmg/Lvl * d >> 3).
std::optional<MercOffer> merc_offer(const Tables& tables, bool expansion, int act, int diff, int clvl, const HireEntry& entry) {
    Rng rng{ entry.seed };
    std::vector<const Hireling*> rows;
    for (const auto& hireling : tables.hirelings)
        if (hireling.act == act + 1 && hireling.difficulty == diff + 1 && hireling.version == (expansion ? 100 : 0)
            && (rows.empty() || hireling.level == rows.front()->level))
            rows.push_back(&hireling);
    if (rows.empty()) return std::nullopt;
    const auto& hireling = *rows[std::size_t(rng(int(rows.size())))];
    MercOffer offer;
    offer.id = hireling.id;
    offer.seed = entry.seed;
    offer.level = std::max(2, clvl - 5 + rng(5));
    const int level_delta = offer.level - hireling.level;
    offer.life = std::max(40, hireling.hit_points + hireling.hp_per_level * level_delta);
    offer.str = std::max(10, hireling.str + (hireling.str_per_level * level_delta >> 3));
    offer.dex = std::max(10, hireling.dex + (hireling.dex_per_level * level_delta >> 3));
    offer.cost = std::max(hireling.gold, hireling.gold * (level_delta * 15 + 100) / 100);
    offer.exp = std::uint32_t(std::max<long long>(0, (long long)(offer.level + 1) * hireling.exp_per_level * offer.level * offer.level));
    offer.def = std::max(0, hireling.def + hireling.def_per_level * level_delta);
    offer.dmg_min = std::max(0, hireling.dmg_min + (hireling.dmg_per_level * level_delta >> 3));
    offer.dmg_max = std::max(1, hireling.dmg_max + (hireling.dmg_per_level * level_delta >> 3));
    offer.name = entry.name;
    return offer;
}

MercStats merc_stats(const Tables& tables, int id, std::uint32_t exp) {
    MercStats merc;
    const Hireling* first = nullptr;
    for (const auto& hireling : tables.hirelings)
        if (hireling.version == 100 && hireling.id == id) { first = &hireling; break; }
    if (!first) return merc;
    for (int level = 1; level < 99; ++level)
        if ((long long)(level + 1) * first->exp_per_level * level * level <= (long long)exp) merc.level = level;
    const Hireling* row = first;
    for (const auto& hireling : tables.hirelings) {
        if (hireling.version != 100 || hireling.id != id) continue;
        if (&hireling != first && merc.level < hireling.level) break;
        row = &hireling;
    }
    const auto& hireling = *row;
    const int level_delta = merc.level - hireling.level;
    merc.life = std::max(40, hireling.hit_points + hireling.hp_per_level * level_delta);
    merc.def = std::max(0, hireling.def + hireling.def_per_level * level_delta);
    merc.dmg_min = std::max(0, hireling.dmg_min + hireling.dmg_per_level * level_delta / 8);
    merc.dmg_max = std::max(1, hireling.dmg_max + hireling.dmg_per_level * level_delta / 8);
    merc.attack_rating = std::max(0, hireling.attack_rating + hireling.ar_per_level * level_delta);
    merc.resist = std::max(0, hireling.resist + hireling.resist_per_level * level_delta / 4);
    merc.str = std::max(10, hireling.str + hireling.str_per_level * level_delta / 8);         // stats 0 / 2
    merc.dex = std::max(10, hireling.dex + hireling.dex_per_level * level_delta / 8);
    return merc;
}

// The merc's share of its owner's kill (FUN_0057e990 -> FUN_0057e860):
// `award` is the kill's experience at the merc's level (FUN_0057e480);
// 86 / 256 of it (to zero) unless the merc struck the blow, then twice that
// added while the merc is under its owner's level and level 98
// (Experience.txt MaxLvl - 1, FUN_00611830(0)). The new experience; the
// level follows from it (merc_stats).
std::uint32_t merc_kill_exp(std::uint32_t merc_exp, std::int64_t award, bool merc_blow, int merc_level, int owner_level) {
    if (!merc_blow) award = award * 0x56 / 256;
    if (award <= 0 || merc_level >= owner_level || merc_level >= 98) return merc_exp;
    return merc_exp + std::uint32_t(award * 2);
}

bool hire(const MercOffer& offer, d2d::d2s::Header& header, d2d::d2s::Stats& stats) {
    using namespace d2d::d2s;
    if (stats.get(kGold) + stats.get(kGoldBank) < offer.cost) return false;
    const auto from_inv = std::min<std::int64_t>(stats.get(kGold), offer.cost);
    stats.values[kGold] -= from_inv;
    stats.values[kGoldBank] -= offer.cost - from_inv;
    header.merc_dead = false;
    header.merc_seed = offer.seed;
    header.merc_type = std::uint16_t(offer.id);
    header.merc_name = std::uint16_t(offer.name);
    header.merc_exp = offer.exp;
    return true;
}

// A dead merc's resurrection (FUN_006637b0): level^2 / 2 * 15, at most
// 50,000.
int merc_resurrect_cost(int merc_level) {
    return std::min((merc_level * merc_level / 2) * 15, 50000);
}

// Its S->C 0x2a result: 5 (hireling) back, 9 (refused) with no dead
// merc, 0xc (no_gold) short.
Trade resurrect_merc(int merc_level, d2d::d2s::Header& header, d2d::d2s::Stats& stats) {
    using namespace d2d::d2s;
    const int cost = merc_resurrect_cost(merc_level);
    if (!header.merc_seed || !header.merc_dead) return Trade::refused;
    if (stats.get(kGold) + stats.get(kGoldBank) < cost) return Trade::no_gold;
    const auto from_inv = std::min<std::int64_t>(stats.get(kGold), cost);
    stats.values[kGold] -= from_inv;
    stats.values[kGoldBank] -= cost - from_inv;
    header.merc_dead = false;
    return Trade::hireling;
}

// Drinks the potion at the bottom of belt column `col` (box col, 0..3):
// the item goes, those stacked above it (boxes col + 4, + 8, + 12) drop
// a row. Returns its code, "" when there's no potion there.
// A potion drunk from where it's carried: gone, and a belt one's column
// moves down. Returns its code, "" if it isn't a potion.
std::string drink_at(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::vector<d2d::d2s::Item>::iterator potion) {
    if (potion == items.end() || !tables.potions.contains(potion->code) || potion->location == d2d::d2s::item_location::kEquipped) return {};
    std::string code = potion->code;
    const int col = potion->location == d2d::d2s::item_location::kBelt ? potion->column : -1;
    items.erase(potion);
    if (col >= 0)
        for (int box = col + 4; box < 16; box += 4)
            for (auto& item : items) if (item.location == d2d::d2s::item_location::kBelt && item.column == box) item.column = box - 4;
    return code;
}

// The runeword `item`'s sockets spell (FUN_0062bed0), or null: not magic
// .. tempered (quality 4..9) nor a quest item (Items +0x12a), every socket
// filled, its fillers Rune1.. in order, a `complete` row, none of its
// etypes, one of its itypes.

// A `ladder` row only in a ladder game (FUN_00562660: game +0x74), which
// d2d's never are.
const Tables::Runeword* runeword_row(const Tables& tables, const d2d::d2s::Item& item) {
    const auto base = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    if ((item.quality >= 4 && item.quality <= 9) || base == tables.item_base.end() || base->second.quest || info == tables.item_info.end()
        || item.socketed_items.empty() || int(item.socketed_items.size()) != item.sockets)
        return nullptr;
    for (const auto& row : tables.runewords) {
        if (!row.complete || row.ladder || row.runes.size() != item.socketed_items.size()) continue;
        if (!std::ranges::equal(row.runes, item.socketed_items, {}, {}, &d2d::d2s::Item::code)) continue;
        if (std::ranges::any_of(row.etypes, [&](const std::string& type) { return type_is(tables, info->second.type, type); })) continue;
        if (std::ranges::any_of(row.itypes, [&](const std::string& type) { return type_is(tables, info->second.type, type); })) return &row;
    }
    return nullptr;
}

// The last filler in (FUN_00562660 -> FUN_006600a0): a runeword its
// sockets spell, not one already, takes the runeword flag (0x4000000)
// and its T1 mods as the runeword list (stat list 0xab / 0x40), each
// through FUN_0065fd70 kind 6 on the item's seed.

// The seed is the item's unit's (item_seed): fresh from its fingerprint
// for one loaded from a save, where its making left it for one made in
// play.
bool make_runeword(const Tables& tables, d2d::d2s::Item& item) {
    const auto* row = item.runeword ? nullptr : runeword_row(tables, item);
    if (!row) return false;
    const auto base = tables.item_base.find(item.code);
    const auto info = tables.item_info.find(item.code);
    if (base == tables.item_base.end() || info == tables.item_info.end()) return false;
    const auto type = tables.types.find(info->second.type);
    item.runeword = true;
    item.runeword_id = row->save_id;
    item.main_props = item.props.size();
    auto rng = item_seed(item);
    const ModItem target{ &item, &base->second, info->second.kind, type != tables.types.end() && type->second.throwable, false };
    for (const auto& mod : row->mods) apply_mod(tables, mod, item.props, rng, &target);
    item.seed_low = rng.low; item.seed_high = rng.high; item.seed_kept = true;
    return true;
}

bool takes_socket(const Tables& tables, const d2d::d2s::Item& filler, const d2d::d2s::Item& target) {
    const auto info = tables.item_info.find(filler.code);
    return info != tables.item_info.end() && type_is(tables, info->second.type, "sock") && filler.identified
        && target.identified && target.socketed && int(target.socketed_items.size()) < target.sockets;
}

std::string drink_item(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id) {
    return drink_at(tables, items, std::ranges::find(items, id, &d2d::d2s::Item::id));
}

std::string drink_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int col) {
    return drink_at(tables, items, std::ranges::find_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kBelt && item.column == col; }));
}

// Find Potion's potion (FUN_005d8100): a row by the act (the level id
// against 0x6eb2f0's starts 1, 40, 75, 103, 109) + difficulty x 5 of the
// table at 0x741b58 {healing, mana, rejuvenation}; the caster's roll
// (rand(100)) below Param3 takes the mana potion, below Param3 + Param4 the
// rejuvenation, else the healing one.
std::string find_potion(int level_id, int difficulty, int roll, int mana_chance, int rejuv_chance) {
    static constexpr std::array<std::array<const char*, 3>, 15> kPotions{ {
        { "hp2", "mp2", "rvs" }, { "hp3", "mp3", "rvs" }, { "hp3", "mp3", "rvs" }, { "hp4", "mp4", "rvl" }, { "hp4", "mp4", "rvl" },
        { "hp4", "mp4", "rvl" }, { "hp4", "mp5", "rvl" }, { "hp5", "mp5", "rvl" }, { "hp5", "mp5", "rvl" }, { "hp5", "mp5", "rvl" },
        { "hp5", "mp5", "rvl" }, { "hp5", "mp5", "rvl" }, { "hp5", "mp5", "rvl" }, { "hp5", "mp5", "rvl" }, { "hp5", "mp5", "rvl" } } };
    static constexpr std::array<int, 5> kActStart{ 1, 40, 75, 103, 109 };
    int act = 0;
    while (act + 1 < int(kActStart.size()) && level_id >= kActStart[std::size_t(act) + 1]) ++act;
    const int column = roll < mana_chance ? 1 : roll < mana_chance + rejuv_chance ? 2 : 0;
    return kPotions[std::size_t(std::clamp(act + std::clamp(difficulty, 0, 2) * 5, 0, 14))][std::size_t(column)];
}

int potion_amount(int calc, int cls, bool life, int stat, Rng& rng) {
    int amount = potion_bonus(calc << 8, cls, life);
    if (stat > 0) {
        const int half = rng(stat) >> 1;
        if (rng(100) < half) amount *= 2;
    }
    return amount;
}

// The potion's state (healthpot / manapot) gives it over `len` frames on
// top of what's left of the last one: per frame (old x left + amount) /
// (left + len), for left + len frames (FUN_005be3f0).
int potion_rate(int old_rate, int left, int amount, int len) {
    return left + len > 0 ? (old_rate * left + amount) / (left + len) : 0;
}

int auto_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                    int required_level) {
    const auto* info = info_of(tables, item);
    if (!info) return -1;
    const auto type = tables.types.find(info->type);
    if (!item.identified || broken(item) || (item.flags & 0x4000) || type == tables.types.end() || info->type == "tpot") return -1;
    if (type->second.body[0] == 0 && type->second.body[1] == 0) return -1;
    if (!meets_requirements(tables, item, wearer, required_level)) return -1;
    const auto worn = [&](int slot) -> const d2d::d2s::Item* {
        const auto found = std::ranges::find_if(items, [&](const auto& carried) { return carried.location == d2d::d2s::item_location::kEquipped && carried.slot == slot; });
        return found != items.end() ? &*found : nullptr;
    };
    if (!type->second.quiver.empty()) {
        const auto shoots = [&](const d2d::d2s::Item* weapon) {
            const auto* weapon_info = weapon ? info_of(tables, *weapon) : nullptr;
            const auto weapon_type = weapon_info ? tables.types.find(weapon_info->type) : tables.types.end();
            return weapon_type != tables.types.end() && !weapon_type->second.shoots.empty() && type_is(tables, info->type, weapon_type->second.shoots);
        };
        if (!shoots(worn(d2d::d2s::body_location::kRightArm)) && !shoots(worn(d2d::d2s::body_location::kLeftArm))) return -1;
    }
    const int first = type->second.body[0], second = type->second.body[1];
    const auto* first_worn = worn(first);
    if (first == second) return first_worn ? -1 : first;
    const auto* second_worn = worn(second);
    const auto goes_with = [&](const d2d::d2s::Item& other) { return info->type == "ring" ? type_is(tables, info_of(tables, other) ? info_of(tables, other)->type : std::string{}, "ring") : hands_ok(tables, item, other, wearer); };
    if (!first_worn) return !second_worn || goes_with(*second_worn) ? first : -1;
    return !second_worn && goes_with(*first_worn) ? second : -1;
}

// The item's own ItemTypes row is Beltable (FUN_0062bad0; no Equiv chain).
bool beltable(const Tables& tables, const d2d::d2s::Item& item) {
    const auto* info = info_of(tables, item);
    const auto type_row = info ? tables.types.find(info->type) : tables.types.end();
    return type_row != tables.types.end() && type_row->second.beltable;
}

int belt_box(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, int boxes) {
    const auto [width, height] = item_size(tables, item.code);
    if (!beltable(tables, item) || width != 1 || height != 1) return -1;
    const auto base = tables.item_base.find(item.code);
    const bool autobelt = base != tables.item_base.end() && base->second.autobelt;
    std::array<const d2d::d2s::Item*, 16> box{};
    for (const auto& carried : items) if (carried.location == d2d::d2s::item_location::kBelt && carried.column >= 0 && carried.column < 16) box[std::size_t(carried.column)] = &carried;
    const auto group = [](const std::string& code) {
        for (std::size_t i = 0; i < kGroups.size(); ++i)
            for (std::size_t at = 0; code.size() == 3 && at < kGroups[i].size(); at += 3) if (kGroups[i].substr(at, 3) == code) return int(i);
        return -1;
    };
    for (int col = 0; col < 4; ++col) {
        const auto* bottom = box[std::size_t(col)];
        if (!bottom || col >= boxes || (bottom->code != item.code && (group(item.code) < 0 || group(item.code) != group(bottom->code)))) continue;
        for (int up = col; up < boxes && up < 16; up += 4) if (!box[std::size_t(up)]) return up;
    }
    for (int col = 0; col < 4 && autobelt; ++col) if (!box[std::size_t(col)]) return col;
    return -1;
}

// Shift + left click on an inventory item (C->S 0x63, FUN_0054d520), the
// hand empty: an item stored in the inventory (FUN_00628250 page 0; not
// the stash, cube or belt) moves to belt_box's box. No box: it stays where
// it is (not into the hand).

// No tome or stack step comes first, so a scroll goes only to a column
// whose bottom is the same scroll. FUN_00535610's trade gate passes for
// every d2d player: at an NPC (interaction kind 1) FUN_00573140 holds
// while the NPC keeps the player's interaction, and outside one
// FUN_00567620 wants the player trade state (player data +0x50) under 5,
// which only a player-to-player trade (PlrTrade.cpp) raises: d2d has none.
bool auto_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id, int boxes) {
    const auto found = std::ranges::find(items, id, &d2d::d2s::Item::id);
    if (found == items.end() || found->location != d2d::d2s::item_location::kStored || found->panel != d2d::d2s::item_panel::kInventory) return false;
    const int slot = belt_box(tables, items, *found, boxes);
    if (slot < 0) return false;
    found->location = d2d::d2s::item_location::kBelt; found->column = slot; found->row = 0; found->panel = 0; found->slot = 0;
    return true;
}

bool stacks_with(const d2d::d2s::Item& one, const d2d::d2s::Item& other) {
    const auto plain = [](int quality) { return quality != 0 && (quality < 4 || quality > 9); };
    return one.code == other.code && one.quality == other.quality && one.qsub == other.qsub && one.ethereal == other.ethereal &&
           plain(one.quality) && one.sockets == 0 && other.sockets == 0;
}

Pickup pick_up(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item, int cols, int rows, int boxes,
               const Wearer* wearer, int required_level) {
    using d2d::d2s::Item;
    const auto in_inventory = [](const Item& carried) { return carried.location == d2d::d2s::item_location::kStored && carried.panel == d2d::d2s::item_panel::kInventory; };
    const auto max_stack = [&](const std::string& code) { const auto found = tables.item_base.find(code); return found != tables.item_base.end() ? found->second.max_stack : 0; };
    const auto* info = info_of(tables, item);
    const std::string type = info ? info->type : std::string{};
    const auto type_row = tables.types.find(type);
    const auto base = tables.item_base.find(item.code);
    const bool scroll = type_is(tables, type, "scro");
    if (scroll || type_is(tables, type, "book")) {
        const std::string tome = !scroll ? item.code : item.code == "tsc" ? "tbk" : item.code == "isc" ? "ibk" : "";
        const auto book = std::ranges::find_if(items, [&](const Item& carried) { return in_inventory(carried) && carried.code == tome && carried.quantity < max_stack(tome); });
        if (book != items.end()) {
            if (scroll) { ++book->quantity; return Pickup::kGone; }
            const int total = book->quantity + item.quantity, cap = max_stack(tome);
            if (total <= cap) { book->quantity = total; return Pickup::kGone; }
            book->quantity = cap; item.quantity = total - cap;
            return Pickup::kStays;
        }
    } else if (base != tables.item_base.end() && base->second.stackable && type_row != tables.types.end() && type_row->second.autostack) {
        for (auto& stack : items) {
            if (!in_inventory(stack) || !stacks_with(stack, item) || stack.quantity >= max_stack(stack.code)) continue;
            const int moved = std::min(max_stack(stack.code) - stack.quantity, item.quantity);
            stack.quantity += moved; item.quantity -= moved;
            if (item.quantity <= 0) return Pickup::kGone;
        }
    }
    if (const int slot = wearer ? auto_equip_slot(tables, items, item, *wearer, required_level) : -1; slot > 0) {
        item.location = d2d::d2s::item_location::kEquipped; item.slot = slot; item.panel = 0; item.column = 0; item.row = 0;
        items.push_back(std::move(item));
        wield_enters(tables, items, items.back());     // FUN_00562e00 -> FUN_0063d1d0
        return Pickup::kGone;
    }
    if (const int slot = belt_box(tables, items, item, boxes); slot >= 0) {
        item.location = d2d::d2s::item_location::kBelt; item.column = slot; item.row = 0; item.panel = 0; item.slot = 0;
        items.push_back(std::move(item));
        return Pickup::kGone;
    }
    std::vector<const Item*> inventory;
    for (const auto& carried : items) if (in_inventory(carried)) inventory.push_back(&carried);
    const auto [width, height] = item_size(tables, item.code);
    const auto [column, row] = free_spot(tables, inventory, cols, rows, width, height, true);
    if (column < 0) return Pickup::kNoRoom;
    item.location = d2d::d2s::item_location::kStored; item.panel = d2d::d2s::item_panel::kInventory; item.column = column; item.row = row;
    items.push_back(std::move(item));
    return Pickup::kGone;
}

// Cain's "Identify Items" (FUN_0062a530 counts, FUN_00578460 identifies):
// the unidentified ones worn (node 3) or stored in the inventory or the cube
// (node 1, page 0 / 3); not the belt (node 2) or the stash.
bool cain_identifies(const d2d::d2s::Item& item) {
    using namespace d2d::d2s;
    return !item.identified && (item.location == item_location::kEquipped
        || (item.location == item_location::kStored && (item.panel == item_panel::kInventory || item.panel == item_panel::kCube)));
}

int identify_all(std::vector<d2d::d2s::Item>& items) {
    int count = 0;
    for (auto& item : items)
        if (cain_identifies(item)) {
            item.identified = true;
            ++count;
        }
    return count;
}

int ring_pick(std::span<const int> ids, int last) {
    int above = -1, lowest = -1;
    for (const int id : ids) {
        if (std::uint32_t(id) > std::uint32_t(last)) { if (above < 0 || std::uint32_t(id) < std::uint32_t(above)) above = id; }
        else if (lowest < 0 || std::uint32_t(id) <= std::uint32_t(lowest)) lowest = id;
    }
    return above >= 0 ? above : lowest;
}


}
