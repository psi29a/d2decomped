// Game rules that don't need assets or a screen: item sizes and grid
// placement, vendor stock, prices, buying and selling. Everything reads
// the excel-derived Tables (filled by d2d's loader, or by hand in tests).
#pragma once

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

struct ItemInfo {
    std::string invfile;
    int w = 1, h = 1;
    std::string namestr, type;
    int kind = 0;                      // 0 misc, 1 armor, 2 weapon
    int belt = -1;                     // armor.txt belt: belts.txt index
};
struct ItemBase { int minac = 0, maxac = 0, cost = 0; bool stackable = false; };
// Prices (FUN_0062efb0, docs/research/re/store.md): npc.txt by MonStats Id.
struct NpcPrice { int buy = 1024, sell = 1024, rep = 1024; std::array<int, 3> qflag{}, qbuy{}, qsell{}, qrep{}, max_buy{}; };
struct VendorItem { std::string code; int min = 0, max = 0, magic_min = 0, magic_max = 0, magic_lvl = 0; bool perm = false; };

struct Tables {
    std::unordered_map<std::string, ItemInfo> item_info;   // by item code
    std::unordered_map<std::string, ItemBase> item_base;   // by item code
    std::unordered_map<std::string, NpcPrice> npc_prices;  // by MonStats Id
    // The (multiply, add) cost pairs of affixes (raw rows, like the
    // names), uniques and set items.
    std::vector<std::pair<int, int>> prefix_cost, suffix_cost, unique_cost, set_cost;
    // Vendors (docs/research/re/store.md): per game.exe vendor index (0
    // Akara, 1 Gheed, 2 Charsi, 3 Fara, 4 Lysander, 5 Drognan, 6 Hratli,
    // 7 Alkor, 8 Ormus, 9 Elzix, 10 Asheara, 11 Cain, 12 Halbu, 13
    // Jamella, 14 Malah, 15 Larzuk, 16 Drehya) the items FUN_00536d50
    // lists: spawnable, <Vendor>Max or <Vendor>MagicMax > 0.
    std::array<std::vector<VendorItem>, 17> vendor_items;
};

// ponytail: stock = each listed item <Vendor>Min..Max times plus the
// PermStoreItems once, packed first-fit by kind (armour tab 0, weapons
// 1 then 2, misc 3) — the server's roll isn't located yet, no magic
// stock.
struct Store {
    int npc = -1, vendor = -1, tab = 0;        // npc: the caller's NPC index
    std::string npc_id;                         // MonStats Id, for prices
    int mode = 0;                               // 1 buy, 2 sell: the button toggled on, the next click trades
    d2d::d2s::Header header;                    // the player's (quest flags, difficulty) for prices
    std::array<std::vector<d2d::d2s::Item>, 4> tabs;
    std::array<bool, 4> pressed{};
    std::vector<std::string> perm;              // PermStoreItems: buying doesn't use them up
};

inline std::pair<int, int> item_size(const Tables& t, const std::string& code) {
    const auto info = t.item_info.find(code);
    return info != t.item_info.end() ? std::pair{ info->second.w, info->second.h } : std::pair{ 1, 1 };
}

// First free w x h spot in a cols x rows grid of placed items, scanning
// column by column (where D2 autoplaces pickups); {-1, -1} if full.
inline std::pair<int, int> free_spot(const Tables& t, const std::vector<const d2d::d2s::Item*>& placed,
                                     int cols, int rows, int w, int h) {
    std::vector<bool> used(std::size_t(cols * rows));
    for (const auto* it : placed) {
        const auto [iw, ih] = item_size(t, it->code);
        for (int y = it->row; y < std::min(it->row + ih, rows); ++y)
            for (int x = it->column; x < std::min(it->column + iw, cols); ++x) used[std::size_t(y * cols + x)] = true;
    }
    for (int x = 0; x + w <= cols; ++x)
        for (int y = 0; y + h <= rows; ++y) {
            bool free = true;
            for (int yy = y; yy < y + h && free; ++yy)
                for (int xx = x; xx < x + w && free; ++xx) free = !used[std::size_t(yy * cols + xx)];
            if (free) return { x, y };
        }
    return { -1, -1 };
}

// Puts an item into the store grid from tab on (weapons spill 1 -> 2).
inline bool store_place(const Tables& t, Store& st, int tab, d2d::d2s::Item it) {
    const auto [w, h] = item_size(t, it.code);
    for (int i = tab; i < 4; ++i) {
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& p : st.tabs[std::size_t(i)]) placed.push_back(&p);
        if (const auto [x, y] = free_spot(t, placed, 10, 10, w, h); x >= 0) {
            it.column = x; it.row = y; it.location = 0; it.panel = 1;
            st.tabs[std::size_t(i)].push_back(std::move(it));
            return true;
        }
        if (i != 1) break;
    }
    return false;
}

inline int store_tab_for(const Tables& t, const std::string& code) {
    const auto info = t.item_info.find(code);
    const int kind = info != t.item_info.end() ? info->second.kind : 0;
    return kind == 1 ? 0 : kind == 2 ? 1 : 3;
}

// MonStats hcIdx -> game.exe vendor index, -1 if not a vendor.
inline int vendor_index(int hc_idx) {
    switch (hc_idx) {
        case 0x94: return 0;  case 0x93: return 1;  case 0x9a: return 2;  case 0xb2: return 3;
        case 0xca: return 4;  case 0xb1: return 5;  case 0xfd: return 6;  case 0xfe: return 7;
        case 0xff: return 8;  case 199:  return 9;  case 0xfc: return 10; case 0x101: return 12;
        case 0x195: return 13; case 0x201: return 14; case 0x1ff: return 15; case 0x200: case 0x202: return 16;
        default: return -1;
    }
}

inline bool is_repair_vendor(int hc_idx) {
    return hc_idx == 0x9a || hc_idx == 0xb2 || hc_idx == 0xfd || hc_idx == 0x101 || hc_idx == 0x1ff;
}

inline Store open_store(const Tables& t, int hc_idx, std::string npc_id, std::uint32_t& rng) {
    Store st;
    st.npc_id = std::move(npc_id);
    st.vendor = vendor_index(hc_idx);
    if (st.vendor < 0) return st;
    for (const auto& vi : t.vendor_items[std::size_t(st.vendor)]) {
        if (vi.perm) st.perm.push_back(vi.code);
        int n = vi.perm ? 1 : vi.min + (vi.max > vi.min ? int((rng = rng * 0x6ac690c5u + 1u) % std::uint32_t(vi.max - vi.min + 1)) : 0);
        while (n-- > 0) {
            d2d::d2s::Item it;
            it.code = vi.code;
            if (const auto b = t.item_base.find(vi.code); b != t.item_base.end() && store_tab_for(t, vi.code) == 0)
                it.defense = b->second.minac;
            store_place(t, st, store_tab_for(t, vi.code), std::move(it));
        }
    }
    for (int i = 0; i < 4; ++i) if (!st.tabs[std::size_t(i)].empty()) { st.tab = i; break; }
    return st;
}

// Buy (sell = false) or sell price of an item at the NPC with MonStats Id
// npc_id, for the player's header (difficulty, quest flags).
inline int item_price(const Tables& t, const d2d::d2s::Item& it, const std::string& npc_id, bool sell,
                      const d2d::d2s::Header& h) {
    const auto b = t.item_base.find(it.code);
    const int base = b != t.item_base.end() ? b->second.cost : 0;
    auto extra = [&](const std::vector<std::pair<int, int>>& c, int i) {
        if (i < 0 || std::size_t(i) >= c.size()) return 0;
        const auto [mul, add] = c[std::size_t(i)];
        return (base < 0x10000 ? mul * base / 1024 : base / 1024 * mul) + add;
    };
    int x = 0;
    switch (it.quality) {
        case 1: x = -(base / 2); break;
        case 4: x = extra(t.prefix_cost, it.prefix) + extra(t.suffix_cost, it.suffix); break;
        case 5: x = extra(t.set_cost, it.set_id); break;
        case 7: x = extra(t.unique_cost, it.unique_id); break;
        case 6: case 8:
            for (int i = 0; i < 6; ++i) x += extra(i % 2 == 0 ? t.prefix_cost : t.suffix_cost, it.affixes[std::size_t(i)]);
            break;
        default: break;
    }
    long long price = base + x;
    for (const auto& j : it.socketed_items)
        if (const auto jb = t.item_base.find(j.code); jb != t.item_base.end()) price += jb->second.cost / 2;
    if (sell && it.ethereal) price /= 4;
    const auto p = t.npc_prices.find(npc_id);
    const int diff = h.active_difficulty();
    if (p != t.npc_prices.end()) {
        const auto& np = p->second;
        price = price * (sell ? np.sell : np.buy) / 1024;
        for (int q = 0; q < 3; ++q)
            if (np.qflag[std::size_t(q)] && (h.quest_flag(diff, np.qflag[std::size_t(q)], 0) || h.quest_flag(diff, np.qflag[std::size_t(q)], 1)))
                price = price * (sell ? np.qsell[std::size_t(q)] : np.qbuy[std::size_t(q)]) / 1024;
    }
    if (it.quantity > 1 && !(b != t.item_base.end() && b->second.stackable)) price *= it.quantity;
    if (sell && p != t.npc_prices.end()) price = std::min<long long>(price, p->second.max_buy[std::size_t(diff)]);
    return int(std::max<long long>(price, 1));
}

// Buys stock item i of the open tab into the inventory (10x4): gold
// down by the price, the item leaves the stock unless it's a perm one.
// False if it doesn't fit or you can't afford it.
// ponytail: no "not enough gold"/"no room" message, no stacks or quantity.
inline bool store_buy(const Tables& t, Store& st, int i, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    auto& tab = st.tabs[std::size_t(st.tab)];
    const auto& it = tab[std::size_t(i)];
    const int price = item_price(t, it, st.npc_id, false, st.header);
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < price) return false;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& x : items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
    const auto [w, h] = item_size(t, it.code);
    const auto [x, y] = free_spot(t, inv, 10, 4, w, h);
    if (x < 0) return false;
    auto bought = it;
    bought.column = x; bought.row = y; bought.location = 0; bought.panel = 1;
    items.push_back(std::move(bought));
    // Carried gold first, then the stash (the store shows it for that).
    // ponytail: that order is a guess; the server's buy isn't RE'd.
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), price);
    stats.v[d2d::d2s::kGold] -= from_inv;
    stats.v[d2d::d2s::kGoldBank] -= price - from_inv;
    if (std::ranges::find(st.perm, it.code) == st.perm.end()) tab.erase(tab.begin() + i);
    return true;
}

// Sells inventory item i: gold up by the sell value (carried gold caps
// at clvl x 10000), the item joins the stock.
// ponytail: quest items aren't refused, no belt/equipped selling.
inline void store_sell(const Tables& t, Store& st, std::size_t i, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    const int price = item_price(t, items[i], st.npc_id, true, st.header);
    stats.v[d2d::d2s::kGold] = std::min<std::int64_t>(stats.get(d2d::d2s::kGold) + price,
                                                       stats.get(d2d::d2s::kLevel) * 10000);
    store_place(t, st, store_tab_for(t, items[i].code), items[i]);
    items.erase(items.begin() + std::ptrdiff_t(i));
}

}  // namespace d2d::rules
