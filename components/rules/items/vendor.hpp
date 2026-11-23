// SPDX-License-Identifier: GPL-3.0-or-later
// A vendor's store: its stock as game.exe fills it, buying, selling, gambling and the repair services.
// The tables these read are rules.hpp's.
#pragma once

#include "rules.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

struct Wearer;

// A vendor's window: its stock (game::open_store fills it as game.exe,
// rules::store_fill / make_store_item) by page.
struct Store {
    int npc = -1, vendor = -1, tab = 0;        // npc: the caller's NPC index
    int hc_idx = -1;                            // the vendor's MonStats hcIdx
    std::string npc_id;                         // MonStats Id, for prices
    int mode = 0;                               // 1 buy, 2 sell: the button toggled on, the next click trades
    d2d::d2s::Header header;                    // the player's (quest flags, difficulty) for prices
    std::array<std::vector<d2d::d2s::Item>, 4> tabs;
    std::array<bool, 4> pressed{};
    std::vector<std::string> perm;              // PermStoreItems: buying doesn't use them up
    bool gamble = false;                        // Gheed's gamble list: buying rolls a new item
    int reduced = 0;                            // the player's item_reducedprices (stat 87), 0..99: off buy, gamble and repair
    // A refused buy, sale or repair's box (FUN_004b53f0): the panel goes to
    // mode 7 under it (FUN_00487d30: 0x7bcbf0, no button down, no clicks),
    // and Continue (LAB_004b4cf0 -> FUN_00487d40) back to 1, the plain
    // store: a buy, sell or repair button toggled on is off again.
    void refused() { mode = 0; pressed = {}; }
    // A store opens on its first tab with anything on it.
    void first_tab() { tab = int(std::ranges::find_if(tabs, [](const auto& page) { return !page.empty(); }) - tabs.begin()) % 4; }
};

// What a vendor's fill asks the item maker for (FUN_00576980 -> FUN_00576330):
// a base, its quality (1 low, 2 normal, 3 superior, 4 magic) at the store
// level; `fill` a permanent quiver's quantity set to its max (stat 0x46).
struct StoreAsk { std::string code; int quality = 2, ilvl = 0; bool fill = false; };

// The store level: clvl + 5, capped in Normal by the act (12, 20, 28, 36, 45).
[[nodiscard]] int store_level(int clvl, int act, int difficulty);

// FUN_004bc500: low..high - 1 on the seed (no draw when high <= low).
inline int store_range(Rng& seed, int low, int high) { return high <= low ? low : low + seed(high - low); }

// FUN_00576900: a normal copy's quality by the store level.
int store_quality(Rng& seed, int level);

// The fill (FUN_00576980, SUNITNPC.CPP) on the proxy seed: each candidate
// (Items table order, weapons, armor, misc) up to the store level rolls
// Min..Max normal copies (none from store level 25), each its quality;
// a base that can be magic and whose MagicLvl is reached rolls
// MagicMin..MagicMax (+ 1 or 2 more from store level 25) magic ones; an
// expansion base only in an expansion game. Then each permanent item once,
// normal (quivers filled). `make` makes one and says whether it took; over
// 32 failures end the fill.
template <class Make>
void store_fill(const Tables& tables, int vendor, int level, bool expansion, Rng& seed, Make&& make) {
    if (vendor < 0 || vendor >= 17) return;
    int failed = 0;
    auto ask = [&](const StoreAsk& request) { if (!make(request) && ++failed > 32) return false; return true; };
    for (const auto& item : tables.vendor_items[std::size_t(vendor)]) {
        if (item.perm || item.level > level) continue;
        const int normals = level < 25 ? store_range(seed, item.min, item.max + 1) : 0;
        if (item.version >= 100 && !expansion) continue;
        for (int copy = 0; copy < normals; ++copy)
            if (!ask({ item.code, store_quality(seed, level), level })) return;
        if (!item.magic || item.magic_lvl > level) continue;
        const int extra = level > 24 ? store_range(seed, 1, 3) + 1 : 1;
        for (int copy = store_range(seed, item.magic_min, item.magic_max + extra); copy > 0; --copy)
            if (!ask({ item.code, 4, level })) return;
    }
    for (const auto& item : tables.vendor_items[std::size_t(vendor)])
        if (item.perm && !ask({ item.code, 2, level, item.code == "cqv" || item.code == "aqv" })) return;
}

// Puts an item into the store grid from tab on (weapons spill 1 -> 2).
bool store_place(const Tables& tables, Store& store, int tab, d2d::d2s::Item item);

int store_tab_for(const Tables& tables, const std::string& code);

// MonStats hcIdx -> game.exe vendor index, -1 if not a vendor.
int vendor_index(int hc_idx);

bool is_repair_vendor(int hc_idx);

// Repairs item i (gold first from the inventory, then the stash, as a buy).
bool store_repair(const Tables& tables, const Store& store, d2d::d2s::Item& item, d2d::d2s::Stats& stats);

// What repair all would cost: each item it takes, at its repair price.
int repair_all_cost(const Tables& tables, const Store& store, const std::vector<d2d::d2s::Item>& items);

// Repair all: the sum is paid at once.
std::optional<int> store_repair_all(const Tables& tables, const Store& store, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats);

// The vendor's perm items: its PermStoreItems, and in an expansion game greater and super potions.
bool store_perm(const Store& store, const std::string& code);

Trade store_buy(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats, int boxes = 4,
                bool fill = false, const Wearer* wearer = nullptr, int required_level = -1);

// Sells item i: gold up by the sell value, what passes the carried cap (clvl x 10000) returned to drop at the
// player's feet.
std::int64_t store_sell(const Tables& tables, Store& store, std::size_t index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats);

// Exceptional / elite upgrade weights out of 10000 for a gamble at clvl: (clvl - qlvl) * 100 / 2 + 1 and * 100 /
// 4 + 1.
std::pair<int, int> gamble_upgrade(const Tables& tables, const std::string& code, int clvl);

// Gheed's price for gambling on `item_code`: rings and amulets cost their "gamble cost"; the rest mix the base,
// exceptional and elite costs by upgrade odds and scale with character level.
int gamble_price(const Tables& tables, const std::string& item_code, int clvl, int reduced = 0);

// game.exe's qsort (MSVC CRT: median of three, a selection sort under 9,
// the bigger half kept for later): not stable, so equal keys come out in its
// own order. `less_than` stands for the comparator's < 0 (> 0 by swapping).
template <class Value, class Less>
void msvc_qsort(std::vector<Value>& values, Less less_than) {
    if (values.size() < 2) return;
    auto compare = [&](std::size_t left, std::size_t right) { return less_than(values[right], values[left]) ? 1 : less_than(values[left], values[right]) ? -1 : 0; };
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    std::size_t low = 0, high = values.size() - 1;
    for (;;) {
        if (high - low + 1 <= 8) {
            for (std::size_t last = high; last > low; --last) {
                std::size_t most = low;
                for (std::size_t at = low + 1; at <= last; ++at) if (compare(at, most) > 0) most = at;
                std::swap(values[most], values[last]);
            }
        } else {
            std::size_t mid = low + (high - low + 1) / 2;
            if (compare(low, mid) > 0) std::swap(values[low], values[mid]);
            if (compare(low, high) > 0) std::swap(values[low], values[high]);
            if (compare(mid, high) > 0) std::swap(values[mid], values[high]);
            std::size_t low_guy = low, high_guy = high;
            for (;;) {
                if (mid > low_guy) do ++low_guy; while (low_guy < mid && compare(low_guy, mid) <= 0);
                if (mid <= low_guy) do ++low_guy; while (low_guy <= high && compare(low_guy, mid) <= 0);
                do --high_guy; while (high_guy > mid && compare(high_guy, mid) > 0);
                if (high_guy < low_guy) break;
                std::swap(values[low_guy], values[high_guy]);
                if (mid == high_guy) mid = low_guy;
            }
            ++high_guy;
            if (mid < high_guy) do --high_guy; while (high_guy > mid && compare(high_guy, mid) == 0);
            if (mid >= high_guy) do --high_guy; while (high_guy > low && compare(high_guy, mid) == 0);
            if (high_guy - low >= high - low_guy) {
                if (low < high_guy) pending.emplace_back(low, high_guy);
                if (low_guy < high) { low = low_guy; continue; }
            } else {
                if (low_guy < high) pending.emplace_back(low_guy, high);
                if (low < high_guy) { high = high_guy; continue; }
            }
        }
        if (pending.empty()) return;
        std::tie(low, high) = pending.back();
        pending.pop_back();
    }
}

// The gamble table: gamble.txt's bases sorted by level with game.exe's qsort.
std::vector<std::string> gamble_by_level(const Tables& tables);

// The gamble upgrade.
std::string gamble_upgrade_code(const Tables& tables, const std::string& code, int level, const GambleRates& rates, Rng& proxy);

// Gheed's gamble screen: 14 items on the proxy seed, each a level clvl - 5 + rand(10) (5 ..
Store open_gamble(const Tables& tables, std::string npc_id, int clvl, int difficulty, bool expansion, Rng& proxy, Rng& game,
                  std::vector<bool>* found_uniques = nullptr);

// Gambles on stock item i of the open tab: pays its gamble price and takes it into the inventory, identified.
Trade store_gamble(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats);

}  // namespace d2d::rules
