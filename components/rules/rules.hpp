// Game rules that don't need assets or a screen: item sizes and grid
// placement, vendor stock, prices, buying and selling. Everything reads
// the excel-derived Tables (filled by d2d's loader, or by hand in tests).
#pragma once

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
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
    bool two_handed = false;           // weapons.txt 2handed
    bool one_or_two = false;           // 1or2handed: a Barbarian wields it in one hand
    int req_str = 0, req_dex = 0, req_lvl = 0;
    std::string flippy;                // flippyfile: the on-the-ground animation
};
// ItemTypes.txt by Code: the Equiv parents, the BodyLocs.txt slots the
// type can be worn in (BodyLoc1/2, not inherited), its class (ama, sor,
// ...: class-only items) and whether it can go in the belt.
struct ItemType {
    std::array<std::string, 2> equiv;
    std::array<int, 2> body{};         // 1 head .. 10 gloves, 0 none
    std::string cls;
    bool beltable = false;
    bool always_magic = false, can_rare = true, always_normal = false;   // Magic / Rare / Normal columns
};
// TreasureClassEx.txt row: picks, the NoDrop weight, quality modifiers
// (Unique, Set, Rare, Magic, in 1024ths off the odds) and the weighted
// entries (item codes, other classes, "gld" or "gld,mul=N").
struct TreasureClass {
    int picks = 1, nodrop = 0;
    std::array<int, 4> mod{};
    std::vector<std::pair<std::string, int>> items;
};
// ItemRatio.txt (LoD rows) per [uber][unique, set, rare, magic, superior, normal]:
// odds base, level divisor, minimum.
struct QualityRatio { int base = 0, divisor = 1, min = 0; };
struct ItemBase {
    int minac = 0, maxac = 0, cost = 0;
    int mindam = 0, maxdam = 0, str_bonus = 0, dex_bonus = 0;   // weapons.txt (2handmindam for two-handers)
    bool stackable = false;
    int level = 0, durability = 0, gamble_cost = 0, min_stack = 0, max_stack = 0;
    std::string normcode, ubercode, ultracode;         // normal / exceptional / elite versions
};
// Prices (FUN_0062efb0, docs/research/re/store.md): npc.txt by MonStats Id.
struct NpcPrice { int buy = 1024, sell = 1024, rep = 1024; std::array<int, 3> qflag{}, qbuy{}, qsell{}, qrep{}, max_buy{}; };
struct VendorItem { std::string code; int min = 0, max = 0, magic_min = 0, magic_max = 0, magic_lvl = 0; bool perm = false; };

// A class skill for the skill tree: Skills.txt (reqlevel, reqskill1..3,
// maxlvl) joined with SkillDesc.txt (SkillPage = tab 1..3, SkillRow 1..6,
// SkillColumn 1..3, IconCel, "str name"). Prerequisites are indices into
// the class's 30 skills, -1 for none.
struct ClassSkill {
    std::string name;                  // string table key
    int page = 0, row = 0, col = 0, icon = 0;
    int req_level = 1, max_level = 20;
    std::array<int, 3> req{ -1, -1, -1 };
};

// An affix / unique / set modifier: Properties.txt code, param, range.
struct Mod { std::string code, param; int min = 0, max = 0; };
// MagicPrefix / MagicSuffix row.
struct Affix {
    std::string name;
    int level = 0, max_level = 0, group = 0, frequency = 0;
    bool spawnable = false, rare = false;
    std::vector<std::string> itypes, etypes;
    std::vector<Mod> mods;
};
// UniqueItems / SetItems row (without separators, as the save's IDs).
struct Special { std::string code; int level = 0, rarity = 1; bool enabled = true; std::vector<Mod> mods; };
// Properties.txt: per code the funcs that turn a mod into stats.
struct PropFunc { int func = 0, stat = -1, val = 0; };
// DifficultyLevels gamble odds, per 100000 (rare/set/unique).
struct GambleRates { int rare = 10000, set = 100, unique = 50; };

// D2's seed (FUN_0045c370 / FUN_0045c3e0): 64 bits as {low, high}; a
// step is low * 0x6AC690C5 + high, split back into low and high. A roll
// below n takes the new low & (n - 1) when n is a power of two, else
// low % n; n < 1 rolls 0. A fresh seed's high is 666 (0x29a).
struct Rng {
    std::uint32_t low = 0, high = 666;
    Rng() = default;
    explicit Rng(std::uint32_t seed) : low(seed) {}
    std::uint32_t next() {
        const std::uint64_t v = std::uint64_t(low) * 0x6AC690C5u + high;
        low = std::uint32_t(v);
        high = std::uint32_t(v >> 32);
        return low;
    }
    int operator()(int n) {
        if (n < 1) return 0;
        next();
        return (n & (n - 1)) == 0 ? int(low & std::uint32_t(n - 1)) : int(low % std::uint32_t(n));
    }
    int range(int lo, int hi) { return hi > lo ? lo + (*this)(hi - lo + 1) : lo; }
};

// hireling.txt row (the stat and cost columns FUN_006637f0 reads).
struct Hireling {
    int version = 0, id = 0, cls = 0, act = 0, difficulty = 0, level = 0, gold = 0, exp_per_level = 0;
    int hp = 0, hp_per_level = 0, def = 0, def_per_level = 0, str = 0, str_per_level = 0, dex = 0, dex_per_level = 0;
    int dmg_min = 0, dmg_max = 0, dmg_per_level = 0;
    int names = 1;                                      // NameFirst..NameLast
    int ar = 0, ar_per_level = 0;
};

struct Tables {
    std::unordered_map<std::string, ItemType> types;       // by type code
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
    std::array<std::vector<ClassSkill>, 7> class_skills;   // by d2s class, Skills.txt order
    // Item generation.
    std::vector<Affix> prefixes, suffixes;                 // raw rows (row 0 blank), = save IDs
    std::vector<Special> uniques, sets;
    std::unordered_map<std::string, std::vector<PropFunc>> properties;
    std::unordered_map<std::string, int> skill_id;         // Skills.txt skill name -> Id
    int rare_prefixes = 0, rare_suffixes = 0;              // RarePrefix / RareSuffix rows
    std::vector<std::string> gamble;                       // gamble.txt codes
    std::array<GambleRates, 3> gamble_rates{};
    std::vector<Hireling> hirelings;                       // hireling.txt rows
    // Drops: TreasureClassEx by name (plus the auto weapN / armoN classes),
    // ItemRatio, and each base's weapons/armor.txt rarity.
    std::unordered_map<std::string, TreasureClass> treasure;
    std::array<std::array<QualityRatio, 6>, 2> quality_ratio{};
    std::unordered_map<std::string, int> item_rarity;
};

// ponytail: stock = each listed item <Vendor>Min..Max times plus the
// PermStoreItems once, packed first-fit by kind (armour tab 0, weapons
// 1 then 2, misc 3) — the server's roll isn't located yet, no magic
// stock.
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

inline Store open_store(const Tables& t, int hc_idx, std::string npc_id, Rng& rng) {
    Store st;
    st.npc_id = std::move(npc_id);
    st.hc_idx = hc_idx;
    st.vendor = vendor_index(hc_idx);
    if (st.vendor < 0) return st;
    for (const auto& vi : t.vendor_items[std::size_t(st.vendor)]) {
        if (vi.perm) st.perm.push_back(vi.code);
        int n = vi.perm ? 1 : rng.range(vi.min, vi.max);
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

// An item's max durability with its modifiers: the save's base, plus
// flat maxdurability (stat 73), times item_maxdurability_percent (75).
inline int max_durability(const d2d::d2s::Item& it) {
    int flat = 0, pct = 0;
    for (const auto& p : it.props) {
        if (p.stat == 73) flat += int(p.value);
        if (p.stat == 75) pct += int(p.value);
    }
    return (it.max_durability + flat) * (100 + pct) / 100;
}
// item_indesctructible (stat 152): never wears, never needs repair.
inline bool indestructible(const d2d::d2s::Item& it) {
    return std::ranges::any_of(it.props, [](const auto& p) { return p.stat == 152 && p.value; });
}

// Repair cost at the NPC (FUN_0062efb0 mode 3): the buy base with its
// quality extras, times missing / max durability, times npc.txt rep mult
// and the quest rep mults / 1024; 0 when there's nothing to repair.
// Ethereal items can't be repaired.
// ponytail: no charge recharging, no socket or "reduced prices" terms.
inline int repair_cost(const Tables& t, const d2d::d2s::Item& it, const std::string& npc_id, const d2d::d2s::Header& h) {
    const int max = max_durability(it);
    if (it.max_durability <= 0 || it.durability >= max || it.ethereal || indestructible(it)) return 0;
    const auto b = t.item_base.find(it.code);
    const int base = b != t.item_base.end() ? b->second.cost : 0;
    auto extra = [&](const std::vector<std::pair<int, int>>& c, int i) {
        if (i < 0 || std::size_t(i) >= c.size()) return 0;
        const auto [mul, add] = c[std::size_t(i)];
        return (base < 0x10000 ? mul * base / 1024 : base / 1024 * mul) + add;
    };
    long long x = base;
    switch (it.quality) {
        case 1: x -= base / 2; break;
        case 4: x += extra(t.prefix_cost, it.prefix) + extra(t.suffix_cost, it.suffix); break;
        case 5: x += extra(t.set_cost, it.set_id); break;
        case 7: x += extra(t.unique_cost, it.unique_id); break;
        case 6: case 8:
            for (int i = 0; i < 6; ++i) x += extra(i % 2 == 0 ? t.prefix_cost : t.suffix_cost, it.affixes[std::size_t(i)]);
            break;
        default: break;
    }
    long long cost = (max - it.durability) * x / max;
    if (const auto p = t.npc_prices.find(npc_id); p != t.npc_prices.end()) {
        const auto& np = p->second;
        cost = cost * np.rep / 1024;
        const int diff = h.active_difficulty();
        for (int q = 0; q < 3; ++q)
            if (np.qflag[std::size_t(q)] && (h.quest_flag(diff, np.qflag[std::size_t(q)], 0) || h.quest_flag(diff, np.qflag[std::size_t(q)], 1)))
                cost = cost * np.qrep[std::size_t(q)] / 1024;
    }
    return int(std::max<long long>(cost, 1));
}

// Repairs item i (gold first from the inventory, then the stash, as a
// buy). False if it's whole or you can't pay.
inline bool store_repair(const Tables& t, const Store& st, d2d::d2s::Item& it, d2d::d2s::Stats& stats) {
    const int cost = repair_cost(t, it, st.npc_id, st.header);
    if (cost <= 0 || stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < cost) return false;
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), cost);
    stats.v[d2d::d2s::kGold] -= from_inv;
    stats.v[d2d::d2s::kGoldBank] -= cost - from_inv;
    it.durability = max_durability(it);
    return true;
}

// Repair all: every worn or carried item, in list order, while the gold lasts.
inline int store_repair_all(const Tables& t, const Store& st, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    int n = 0;
    for (auto& it : items)
        if (it.location == 1 || (it.location == 0 && it.panel == 1)) n += store_repair(t, st, it, stats);
    return n;
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

// Is type t (or one of its Equiv ancestors) the type want?
inline bool type_is(const Tables& t, const std::string& type, std::string_view want, int depth = 0) {
    if (type.empty() || depth > 8) return false;
    if (type == want) return true;
    const auto e = t.types.find(type);
    return e != t.types.end()
        && (type_is(t, e->second.equiv[0], want, depth + 1) || type_is(t, e->second.equiv[1], want, depth + 1));
}

// BodyLocs.txt codes -> body slot (d2s slot numbering: 1 head .. 10 gloves).
inline int body_slot(std::string_view code) {
    static constexpr std::string_view kCodes[11] = { "", "head", "neck", "tors", "rarm", "larm",
                                                     "rrin", "lrin", "belt", "feet", "glov" };
    for (int i = 1; i < 11; ++i) if (code == kCodes[i]) return i;
    return 0;
}

// The class code (ama, sor, nec, pal, bar, dru, ass) an item type is
// restricted to, "" for anyone: ItemTypes Class along the Equiv chain.
inline std::string type_class(const Tables& t, const std::string& type, int depth = 0) {
    const auto e = t.types.find(type);
    if (e == t.types.end() || depth > 8) return {};
    if (!e->second.cls.empty()) return e->second.cls;
    for (const auto& q : e->second.equiv)
        if (auto c = type_class(t, q, depth + 1); !c.empty()) return c;
    return {};
}

inline constexpr std::string_view kClassCode[7] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };   // by d2s class

// What the player brings to an equip check: d2s class and the current
// strength, dexterity and level.
struct Wearer { int cls = 0, str = 0, dex = 0, lvl = 1; };

inline const ItemInfo* info_of(const Tables& t, const d2d::d2s::Item& it) {
    const auto i = t.item_info.find(it.code);
    return i != t.item_info.end() ? &i->second : nullptr;
}

// Can `it` be worn in body slot `slot` by `w` at all (slot, class, stat
// and level requirements), ignoring what's already equipped?
// ponytail: base requirements only; ethereal/"requirements -x%" and the
// unique/set level requirements aren't applied.
inline bool can_wear(const Tables& t, const d2d::d2s::Item& it, int slot, const Wearer& w) {
    const auto* info = info_of(t, it);
    if (!info) return false;
    const auto ty = t.types.find(info->type);
    if (ty == t.types.end() || slot < 1 || (ty->second.body[0] != slot && ty->second.body[1] != slot)) return false;
    if (const auto c = type_class(t, info->type); !c.empty() && (w.cls < 0 || w.cls > 6 || c != kClassCode[std::size_t(w.cls)]))
        return false;
    return w.str >= info->req_str && w.dex >= info->req_dex && w.lvl >= info->req_lvl;
}

// A two-handed weapon fills both hands (a Barbarian swings 1or2handed
// ones in one); only a quiver can join it.
inline bool blocks_other_hand(const Tables& t, const d2d::d2s::Item& it, const Wearer& w) {
    const auto* info = info_of(t, it);
    return info && info->two_handed && !(w.cls == 4 && info->one_or_two);
}

// Can a and b be held in the two hands together? Weapon + shield or
// quiver; two weapons only for a Barbarian (or an Assassin's two claws);
// a two-hander only with a quiver.
inline bool hands_ok(const Tables& t, const d2d::d2s::Item& a, const d2d::d2s::Item& b, const Wearer& w) {
    const auto* ia = info_of(t, a);
    const auto* ib = info_of(t, b);
    if (!ia || !ib) return false;
    const bool qa = type_is(t, ia->type, "misl"), qb = type_is(t, ib->type, "misl");
    if (blocks_other_hand(t, a, w)) return qb;
    if (blocks_other_hand(t, b, w)) return qa;
    const bool wa = type_is(t, ia->type, "weap"), wb = type_is(t, ib->type, "weap");
    if (wa && wb) return w.cls == 4 || (w.cls == 6 && type_is(t, ia->type, "h2h") && type_is(t, ib->type, "h2h"));
    return wa != wb;
}

// The item cursor. D2 moves one item at a time: picking up takes it out
// of the list into `held`; putting down places it, swapping with the one
// item it lands on (into `held`), and fails when it would overlap two or
// more, doesn't fit, or isn't allowed there. Each put returns whether it
// changed anything.

// Put `held` into a stored grid (location 0; panel 1 inventory, 4 cube,
// 5 stash) with its top-left cell at (col, row).
inline bool put_in_grid(const Tables& t, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                        int panel, int cols, int rows, int col, int row) {
    if (!held || (panel == 4 && held->code == "box")) return false;   // the cube can't go in itself
    const auto [w, h] = item_size(t, held->code);
    if (col < 0 || row < 0 || col + w > cols || row + h > rows) return false;
    int hit = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.location != 0 || it.panel != panel) continue;
        const auto [iw, ih] = item_size(t, it.code);
        if (it.column < col + w && col < it.column + iw && it.row < row + h && row < it.row + ih) {
            if (hit >= 0) return false;
            hit = int(i);
        }
    }
    auto put = std::move(*held);
    held.reset();
    if (hit >= 0) { held = std::move(items[std::size_t(hit)]); items.erase(items.begin() + hit); }
    put.location = 0; put.panel = panel; put.column = col; put.row = row; put.slot = 0;
    items.push_back(std::move(put));
    return true;
}

// Equip `held` in body slot `slot` (1..10), swapping out what's there.
// In the hands, the other hand's item comes off too when the two can't
// be held together (a two-hander and a shield, a second weapon for a
// non-Barbarian) — as long as only one item comes off in all.
// ponytail: that displacing is the D2 behaviour for two-handers; for the
// second-weapon case it isn't checked against game.exe yet.
inline bool equip(const Tables& t, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                  int slot, const Wearer& w) {
    if (!held || !can_wear(t, *held, slot, w)) return false;
    const bool hand = slot == 4 || slot == 5;
    const int other = hand ? 9 - slot : 0;
    int off = -1;                                      // the item coming off
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.location != 1) continue;
        const bool comes_off = it.slot == slot
            || (hand && it.slot == other && !hands_ok(t, *held, it, w));
        if (!comes_off) continue;
        if (off >= 0) return false;
        off = int(i);
    }
    auto put = std::move(*held);
    held.reset();
    if (off >= 0) { held = std::move(items[std::size_t(off)]); items.erase(items.begin() + off); }
    put.location = 1; put.slot = slot; put.panel = 0; put.column = 0; put.row = 0;
    items.push_back(std::move(put));
    return true;
}

// Put `held` in belt box `box` (0..boxes-1): potions, scrolls — ItemTypes
// Beltable along the Equiv chain.
// ponytail: swapping in a smaller belt doesn't check the rows it'd lose.
inline bool put_in_belt(const Tables& t, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                        int box, int boxes) {
    if (!held || box < 0 || box >= boxes) return false;
    const auto* info = info_of(t, *held);
    if (!info) return false;
    auto beltable = [&](auto&& self, const std::string& ty, int depth) -> bool {
        const auto e = t.types.find(ty);
        if (e == t.types.end() || depth > 8) return false;
        return e->second.beltable || self(self, e->second.equiv[0], depth + 1) || self(self, e->second.equiv[1], depth + 1);
    };
    if (!beltable(beltable, info->type, 0)) return false;
    auto put = std::move(*held);
    held.reset();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].location == 2 && items[i].column == box) { held = std::move(items[i]); items.erase(items.begin() + std::ptrdiff_t(i)); break; }
    put.location = 2; put.column = box; put.row = 0; put.panel = 0; put.slot = 0;
    items.push_back(std::move(put));
    return true;
}

// Per stat point, in quarter points (CharStats LifePerVitality,
// StaminaPerVitality, ManaPerMagic): Amazon 12 = 3 life per vitality.
// CharStats per class: gains per stat point and per level (quarter
// points), stat points per level, and ToHitFactor.
struct ClassGains {
    int life_per_vit = 0, stamina_per_vit = 0, mana_per_energy = 0;
    int life_per_level = 0, stamina_per_level = 0, mana_per_level = 0, stat_per_level = 5, to_hit = 0;
};

// Spends up to n unspent stat points (stat 4) on stat (0 strength, 1
// energy, 2 dexterity, 3 vitality), as the char panel's buttons ask with
// packet 0x3a (FUN_004a78c0). Vitality raises life and stamina, energy
// mana, current and max (8.8 fixed: a quarter point is 64). Returns the
// points spent.
// ponytail: the server's handler isn't traced; gains are CharStats'.
inline int spend_stat_points(d2d::d2s::Stats& st, int stat, int n, const ClassGains& g) {
    using namespace d2d::d2s;
    n = int(std::min<std::int64_t>(n, st.get(kStatPts)));
    if (n <= 0 || stat < 0 || stat > 3) return 0;
    st.v[std::size_t(stat)] += n;
    st.v[kStatPts] -= n;
    auto both = [&](int cur, int max, int quarters) {
        st.v[std::size_t(cur)] += std::int64_t(quarters) * 64 * n;
        st.v[std::size_t(max)] += std::int64_t(quarters) * 64 * n;
    };
    if (stat == kVit) { both(kLife, kMaxLife, g.life_per_vit); both(kStamina, kMaxStamina, g.stamina_per_vit); }
    if (stat == kEne) both(kMana, kMaxMana, g.mana_per_energy);
    return n;
}

// Can skill i (0..29 of class cls) take a point: a level to go, the
// character level, and every prerequisite learned? (FUN_004ac200 greys
// out the icons that can't.)
// ponytail: base levels; +skills from items don't count toward anything.
inline bool can_learn(const Tables& t, int cls, int i, const std::array<std::uint8_t, 30>& lv, int clvl) {
    if (cls < 0 || cls > 6 || i < 0 || std::size_t(i) >= t.class_skills[std::size_t(cls)].size()) return false;
    const auto& sk = t.class_skills[std::size_t(cls)][std::size_t(i)];
    if (lv[std::size_t(i)] >= sk.max_level || clvl < sk.req_level) return false;
    for (const int r : sk.req) if (r >= 0 && lv[std::size_t(r)] == 0) return false;
    return true;
}

// Spends a skill point (stat 5) on skill i. Returns whether it did.
inline bool learn_skill(const Tables& t, int cls, int i, std::array<std::uint8_t, 30>& lv, d2d::d2s::Stats& st) {
    if (st.get(d2d::d2s::kSkillPts) <= 0 || !can_learn(t, cls, i, lv, int(st.get(d2d::d2s::kLevel)))) return false;
    ++lv[std::size_t(i)];
    --st.v[d2d::d2s::kSkillPts];
    return true;
}

// Healers restore life and mana when you talk to them: Akara, Fara,
// Ormus, Jamella, Malah (MonStats hcIdx).
// ponytail: the list is D2's known healers, not located in game.exe;
// poison/curse removal waits for those states to exist. The save's max
// life/mana are base values without gear (current can be higher), so
// healing only raises to them; the true max needs item stat totals.
inline bool is_healer(int hc_idx) {
    return hc_idx == 148 || hc_idx == 178 || hc_idx == 255 || hc_idx == 405 || hc_idx == 513;
}
inline void heal(d2d::d2s::Stats& st) {
    using namespace d2d::d2s;
    st.v[kLife] = std::max(st.v[kLife], st.v[kMaxLife]);
    st.v[kMana] = std::max(st.v[kMana], st.v[kMaxMana]);
}

// Pick up item i into the (empty) cursor.
inline bool pick_up(std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held, std::size_t i) {
    if (held || i >= items.size()) return false;
    held = std::move(items[i]);
    items.erase(items.begin() + std::ptrdiff_t(i));
    return true;
}

// A mod's stats, per Properties.txt funcs, in the save's (stat, param,
// value) form: 1/2/8 value, 3 the previous value again, 5/6/7 min/max/%
// damage, 10 skill tab (txt param class*3+tab -> class<<3|tab), 11
// chance to cast (param level | skill<<6, value chance), 15/16/17 min /
// max / param, 19 charges (value charges | charges<<8), 20 indestructible,
// 21 class skills, 22 single skill.
// ponytail: 12 (random skill), 18 (by time), 23, 24 (monster type) and
// 36 are dropped; sockets (14) and ethereal (23) aren't set on the item.
inline void apply_mod(const Tables& t, const Mod& m, std::vector<d2d::d2s::ItemProp>& out, Rng& rng) {
    const auto pf = t.properties.find(m.code);
    if (pf == t.properties.end()) return;
    auto skill = [&](const std::string& s) {
        if (!s.empty() && s[0] >= '0' && s[0] <= '9') return std::atoi(s.c_str());
        const auto i = t.skill_id.find(s);
        return i != t.skill_id.end() ? i->second : 0;
    };
    const int par = m.param.empty() ? 0 : (m.param[0] >= '0' && m.param[0] <= '9') || m.param[0] == '-'
                                               ? std::atoi(m.param.c_str()) : skill(m.param);
    int v = rng.range(std::min(m.min, m.max), std::max(m.min, m.max)), last = v;
    for (const auto& f : pf->second) {
        switch (f.func) {
            case 1: case 2: case 8: if (f.stat >= 0) out.push_back({ f.stat, par, v }); last = v; break;
            case 3: if (f.stat >= 0) out.push_back({ f.stat, par, last }); break;
            case 5: out.push_back({ 21, 0, v }); break;
            case 6: out.push_back({ 22, 0, v }); break;
            case 7: out.push_back({ 17, 0, v }); out.push_back({ 18, 0, v }); break;
            case 10: if (f.stat >= 0) out.push_back({ f.stat, (par / 3) << 3 | (par % 3), v }); break;
            case 11: if (f.stat >= 0) out.push_back({ f.stat, (m.max & 63) | skill(m.param) << 6, m.min }); break;
            case 15: if (f.stat >= 0) out.push_back({ f.stat, 0, m.min }); break;
            case 16: if (f.stat >= 0) out.push_back({ f.stat, 0, m.max }); break;
            case 17: if (f.stat >= 0) out.push_back({ f.stat, 0, par }); break;
            case 19: if (f.stat >= 0) out.push_back({ f.stat, (m.max & 63) | skill(m.param) << 6, m.min | m.min << 8 }); break;
            case 20: if (f.stat >= 0) out.push_back({ f.stat, 0, 1 }); break;
            case 21: if (f.stat >= 0) out.push_back({ f.stat, f.val, v }); break;
            case 22: if (f.stat >= 0) out.push_back({ f.stat, skill(m.param), v }); break;
            default: break;
        }
    }
}

// Affix level for an item of level ilvl with base qlvl (the known D2
// formula): ilvl - qlvl/2 below 99 - qlvl/2, else 2*ilvl - 99.
// ponytail: no "magic lvl" (wands, staves, circlets).
inline int affix_level(int ilvl, int qlvl) {
    ilvl = std::clamp(std::max(ilvl, qlvl), 1, 99);
    return ilvl < 99 - qlvl / 2 ? ilvl - qlvl / 2 : 2 * ilvl - 99;
}

// A random affix row for an item of `type` at affix level alvl, weighted
// by frequency; skips groups already taken. 0 = none fits.
inline int pick_affix(const Tables& t, const std::vector<Affix>& list, const std::string& type, int alvl, bool rare,
                      const std::vector<int>& groups, Rng& rng) {
    std::vector<std::pair<int, int>> ok;                   // (row, frequency)
    int total = 0;
    for (std::size_t r = 1; r < list.size(); ++r) {
        const auto& a = list[r];
        if (!a.spawnable || a.frequency <= 0 || a.level > alvl || (a.max_level > 0 && a.max_level < alvl) || (rare && !a.rare))
            continue;
        if (std::ranges::find(groups, a.group) != groups.end()) continue;
        if (std::ranges::none_of(a.itypes, [&](const auto& it) { return type_is(t, type, it); })) continue;
        if (std::ranges::any_of(a.etypes, [&](const auto& et) { return type_is(t, type, et); })) continue;
        ok.push_back({ int(r), a.frequency });
        total += a.frequency;
    }
    if (ok.empty()) return 0;
    int roll = rng(total);
    for (auto [r, f] : ok) { if (roll < f) return r; roll -= f; }
    return ok.back().first;
}

// A new item of `code` at level ilvl and quality (4 magic, 5 set, 6
// rare, 7 unique; set/unique fall back to rare when none fits),
// identified, with its defence and durability rolled.
// ponytail: rare affix count 3..6 alternating prefix/suffix, magic 1/4
// prefix, 1/4 suffix, 1/2 both; unique/set picks weigh rarity but skip
// the "nolimit"/one-per-game rules; no set bonus lists.
inline d2d::d2s::Item generate_item(const Tables& t, const std::string& code, int ilvl, int quality, Rng& rng) {
    d2d::d2s::Item it;
    it.code = code;
    it.identified = true;
    it.ilvl = std::clamp(ilvl, 1, 99);
    const auto* info = info_of(t, it);
    const auto b = t.item_base.find(code);
    const int qlvl = b != t.item_base.end() ? b->second.level : 1;
    if (b != t.item_base.end()) {
        if (info && info->kind == 1) it.defense = rng.range(b->second.minac, b->second.maxac);
        if ((it.max_durability = b->second.durability) > 0) it.durability = it.max_durability;
    }
    const std::string type = info ? info->type : std::string{};
    // Low / normal / superior: no affixes; stacks (arrows, bolts, keys) roll
    // their quantity. ponytail: superior items' own bonuses aren't rolled.
    if (quality <= 3) {
        it.quality = std::max(quality, 1);
        if (b != t.item_base.end() && b->second.stackable) it.quantity = rng.range(b->second.min_stack, b->second.max_stack);
        return it;
    }
    auto special = [&](const std::vector<Special>& list) {
        int total = 0, pick = -1;
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].enabled && list[i].code == code && list[i].level <= it.ilvl) total += std::max(1, list[i].rarity);
        if (total == 0) return -1;
        int roll = rng(total);
        for (std::size_t i = 0; i < list.size() && pick < 0; ++i) {
            if (!list[i].enabled || list[i].code != code || list[i].level > it.ilvl) continue;
            if (roll < std::max(1, list[i].rarity)) pick = int(i);
            else roll -= std::max(1, list[i].rarity);
        }
        return pick;
    };
    if (quality == 7 || quality == 5) {
        const auto& list = quality == 7 ? t.uniques : t.sets;
        if (const int i = special(list); i >= 0) {
            it.quality = quality;
            (quality == 7 ? it.unique_id : it.set_id) = i;
            for (const auto& m : list[std::size_t(i)].mods) apply_mod(t, m, it.props, rng);
            return it;
        }
        quality = 6;
    }
    const int alvl = affix_level(it.ilvl, qlvl);
    std::vector<int> groups;
    auto add = [&](bool prefix) {
        const auto& list = prefix ? t.prefixes : t.suffixes;
        const int r = pick_affix(t, list, type, alvl, quality == 6, groups, rng);
        if (r <= 0) return 0;
        groups.push_back(list[std::size_t(r)].group);
        for (const auto& m : list[std::size_t(r)].mods) apply_mod(t, m, it.props, rng);
        return r;
    };
    if (quality == 6) {
        it.quality = 6;
        it.rare1 = t.rare_prefixes > 0 ? 156 + rng(t.rare_prefixes) : 0;
        it.rare2 = t.rare_suffixes > 0 ? 1 + rng(t.rare_suffixes) : 0;
        const int n = 3 + rng(4);
        for (int k = 0; k < n; ++k) {
            const bool prefix = k % 2 == 0;
            it.affixes[std::size_t(k / 2 * 2 + (prefix ? 0 : 1))] = add(prefix);
        }
        return it;
    }
    it.quality = 4;
    const int shape = rng(4);                              // 0 prefix, 1 suffix, 2-3 both
    if (shape != 1) it.prefix = add(true);
    if (shape != 0) it.suffix = add(false);
    return it;
}

// Exceptional / elite upgrade weights out of 10000 for a gamble at clvl
// (FUN_00629370): (clvl - qlvl) * 100 / 2 + 1 and * 100 / 4 + 1.
inline std::pair<int, int> gamble_upgrade(const Tables& t, const std::string& code, int clvl) {
    const auto b = t.item_base.find(code);
    if (b == t.item_base.end()) return { 0, 0 };
    auto w = [&](const std::string& c, int div) {
        const auto u = t.item_base.find(c);
        if (c.empty() || c == code || u == t.item_base.end()) return 0;
        return std::max(0, (clvl - u->second.level) * 100 / div + 1);
    };
    return { w(b->second.ubercode, 2), w(b->second.ultracode, 4) };
}

// Gheed's price for gambling on `code` (FUN_00629370): rings and amulets
// cost their "gamble cost"; the rest mix the base, exceptional and elite
// costs by upgrade odds and scale with character level.
inline int gamble_price(const Tables& t, const std::string& code, int clvl) {
    const auto b = t.item_base.find(code);
    if (b == t.item_base.end()) return 0;
    const auto& base = b->second;
    if (code == "rin" || code == "amu") return base.gamble_cost;
    const auto [pb, pu] = gamble_upgrade(t, code, clvl);
    auto cost_of = [&](const std::string& c) { const auto u = t.item_base.find(c); return u != t.item_base.end() ? u->second.cost : 0; };
    const long long stack = std::max(1, (base.min_stack + base.max_stack) / 2);
    const int c = std::max(clvl, 5);
    const long long mix = ((10000LL - pu - pb) * base.cost * stack + (long long)cost_of(base.ultracode) * pu
                           + (long long)cost_of(base.ubercode) * pb) / 10000;
    const long long lvl = ((std::max(base.level - 45, 0) - base.level / 2 + c) * 250) / 3;
    return int((lvl + mix) * ((c * 2 + 1) / 3 + 20) / 15);
}

// Gambles on `code`: the base may upgrade (exceptional / elite, by the
// same odds the price uses), the level is clvl-5..clvl+4, the quality
// unique / set / rare by DifficultyLevels odds per 100000, else magic.
// ponytail: the server's gamble roll isn't traced; those are the table
// odds and the price's upgrade weights.
inline d2d::d2s::Item gamble_item(const Tables& t, const std::string& code, int clvl, int diff, Rng& rng) {
    std::string c = code;
    if (const auto b = t.item_base.find(code); b != t.item_base.end()) {
        const auto [pb, pu] = gamble_upgrade(t, code, clvl);
        const int r = rng(10000);
        if (r < pu) c = b->second.ultracode;
        else if (r < pu + pb) c = b->second.ubercode;
    }
    const auto& g = t.gamble_rates[std::size_t(std::clamp(diff, 0, 2))];
    const int r = rng(100000);
    const int q = r < g.unique ? 7 : r < g.unique + g.set ? 5 : r < g.unique + g.set + g.rare ? 6 : 4;
    return generate_item(t, c, clvl - 5 + rng(10), q, rng);
}

// Gheed's gamble screen: every gamble.txt base up to the character's
// level, packed like a store (armour, weapons, misc).
// ponytail: the game shows a random subset and refreshes it; this shows
// them all and keeps them.
inline Store open_gamble(const Tables& t, std::string npc_id, int clvl) {
    Store st;
    st.npc_id = std::move(npc_id);
    st.gamble = true;
    for (const auto& code : t.gamble) {
        const auto b = t.item_base.find(code);
        if (b == t.item_base.end() || b->second.level > clvl) continue;
        d2d::d2s::Item it;
        it.code = code;
        it.identified = true;
        store_place(t, st, store_tab_for(t, code), std::move(it));
    }
    for (int i = 0; i < 4; ++i) if (!st.tabs[std::size_t(i)].empty()) { st.tab = i; break; }
    return st;
}

// Gambles on stock item i of the open tab: pays the gamble price (carried
// gold, then the stash) and puts the rolled item in the inventory. The
// stock stays. False if it can't be paid for or doesn't fit.
inline bool store_gamble(const Tables& t, Store& st, int i, std::vector<d2d::d2s::Item>& items,
                         d2d::d2s::Stats& stats, Rng& rng) {
    const auto& code = st.tabs[std::size_t(st.tab)][std::size_t(i)].code;
    const int clvl = int(stats.get(d2d::d2s::kLevel));
    const int price = gamble_price(t, code, clvl);
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < price) return false;
    auto it = gamble_item(t, code, clvl, st.header.active_difficulty(), rng);
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& x : items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
    const auto [w, h] = item_size(t, it.code);
    const auto [x, y] = free_spot(t, inv, 10, 4, w, h);
    if (x < 0) return false;
    it.location = 0; it.panel = 1; it.column = x; it.row = y;
    items.push_back(std::move(it));
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), price);
    stats.v[d2d::d2s::kGold] -= from_inv;
    stats.v[d2d::d2s::kGoldBank] -= price - from_inv;
    return true;
}

// A mercenary for hire, as FUN_006637f0 rolls it for the hire list.
struct MercOffer {
    int id = 0, level = 0, life = 0, str = 0, dex = 0, cost = 0, def = 0, dmg_min = 0, dmg_max = 0;
    std::uint32_t exp = 0, seed = 0;
    int name = 0;                                       // index from NameFirst
};

// One offer for act (0-based) and difficulty (0-2): FUN_00656580 lists
// the rows of that act, difficulty and version (100 = LoD) sharing the
// first one's Level; one is picked, the level is clvl - 5 + rand(5)
// (at least 2), and with d = level - the row's Level:
//   life = HP + HP/Lvl * d (>= 40), str/dex = base + (per-level * d >> 3)
//   (>= 10), cost = Gold * (15d + 100) / 100 (>= Gold), exp =
//   (level + 1) * Exp/Lvl * level^2, def = Def + Def/Lvl * d, damage =
//   Dmg-Min/Max + (Dmg/Lvl * d >> 3).
inline std::optional<MercOffer> merc_offer(const Tables& t, bool expansion, int act, int diff, int clvl, Rng& rng) {
    std::vector<const Hireling*> rows;
    for (const auto& h : t.hirelings)
        if (h.act == act + 1 && h.difficulty == diff + 1 && h.version == (expansion ? 100 : 0)
            && (rows.empty() || h.level == rows.front()->level))
            rows.push_back(&h);
    if (rows.empty()) return std::nullopt;
    const auto& h = *rows[std::size_t(rng(int(rows.size())))];
    MercOffer o;
    o.id = h.id;
    o.seed = std::uint32_t(rng(0x7fffffff)) | 1;
    o.level = std::max(2, clvl - 5 + rng(5));
    const int d = o.level - h.level;
    o.life = std::max(40, h.hp + h.hp_per_level * d);
    o.str = std::max(10, h.str + (h.str_per_level * d >> 3));
    o.dex = std::max(10, h.dex + (h.dex_per_level * d >> 3));
    o.cost = std::max(h.gold, h.gold * (d * 15 + 100) / 100);
    o.exp = std::uint32_t(std::max<long long>(0, (long long)(o.level + 1) * h.exp_per_level * o.level * o.level));
    o.def = std::max(0, h.def + h.def_per_level * d);
    o.dmg_min = std::max(0, h.dmg_min + (h.dmg_per_level * d >> 3));
    o.dmg_max = std::max(1, h.dmg_max + (h.dmg_per_level * d >> 3));
    o.name = rng(std::max(1, h.names));
    return o;
}

// The save's mercenary (hireling Id, experience) as it fights: its level
// is the highest whose experience ((level + 1) * Exp/Lvl * level^2, as
// hiring sets it) it has, its row the band of that Id at or below that
// level, and life / defence / damage / strength / dexterity grow from the
// row like a hire offer's; attack rating is AR + AR/Lvl per level.
// ponytail: items the merc wears aren't counted.
struct MercStats { int level = 1, life = 40, def = 0, dmg_min = 1, dmg_max = 2, ar = 0; };
inline MercStats merc_stats(const Tables& t, int id, std::uint32_t exp) {
    MercStats m;
    const Hireling* row = nullptr;
    for (const auto& h : t.hirelings)
        if (h.id == id && (!row || h.level < row->level)) row = &h;
    if (!row) return m;
    for (int l = 1; l < 99; ++l)
        if ((long long)(l + 1) * row->exp_per_level * l * l <= (long long)exp) m.level = l;
    for (const auto& h : t.hirelings)
        if (h.id == id && h.level <= m.level && h.level > row->level) row = &h;
    const auto& h = *row;
    const int d = m.level - h.level;
    m.life = std::max(40, h.hp + h.hp_per_level * d);
    m.def = std::max(0, h.def + h.def_per_level * d);
    m.dmg_min = std::max(0, h.dmg_min + (h.dmg_per_level * d >> 3));
    m.dmg_max = std::max(m.dmg_min + 1, h.dmg_max + (h.dmg_per_level * d >> 3));
    m.ar = std::max(1, h.ar + h.ar_per_level * d);
    return m;
}

// Hires `o`: pays its cost (carried gold, then the stash) and makes it
// the save's mercenary. False if it can't be paid for.
inline bool hire(const MercOffer& o, d2d::d2s::Header& h, d2d::d2s::Stats& st) {
    using namespace d2d::d2s;
    if (st.get(kGold) + st.get(kGoldBank) < o.cost) return false;
    const auto from_inv = std::min<std::int64_t>(st.get(kGold), o.cost);
    st.v[kGold] -= from_inv;
    st.v[kGoldBank] -= o.cost - from_inv;
    h.merc_dead = false;
    h.merc_seed = o.seed;
    h.merc_type = std::uint16_t(o.id);
    h.merc_name = std::uint16_t(o.name);
    h.merc_exp = o.exp;
    return true;
}

// Cain's "Identify Items": the carried and worn ones. Returns how many.
// ponytail: the server's scope isn't traced (stash and cube are left).
inline int unidentified(const std::vector<d2d::d2s::Item>& items) {
    return int(std::ranges::count_if(items, [](const auto& it) {
        return !it.identified && (it.location == 1 || it.location == 2 || (it.location == 0 && it.panel == 1));
    }));
}
inline int identify_all(std::vector<d2d::d2s::Item>& items) {
    int n = 0;
    for (auto& it : items)
        if (!it.identified && (it.location == 1 || it.location == 2 || (it.location == 0 && it.panel == 1))) {
            it.identified = true;
            ++n;
        }
    return n;
}

// Pathing over subtiles for a unit that can't stand where blocked(x, y):
// 8-way A* (no cutting corners past a blocked neighbour), at most
// max_nodes expanded. Returns the subtiles from start (exclusive) to the
// goal, or to the reached subtile nearest the goal when it can't be
// reached (clicking a wall walks up to it); empty when already there.
// ponytail: stands in for D2's pathing (Path.cpp), which isn't traced.
template <class Blocked>
std::vector<std::pair<int, int>> find_path(int sx, int sy, int gx, int gy, Blocked&& blocked, int max_nodes = 6000) {
    using P = std::pair<int, int>;
    auto key = [](int x, int y) { return std::uint64_t(std::uint32_t(x)) << 32 | std::uint32_t(y); };
    auto h = [&](int x, int y) {
        const int dx = std::abs(x - gx), dy = std::abs(y - gy);
        return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
    };
    std::unordered_map<std::uint64_t, std::pair<int, P>> seen;     // g, parent
    std::priority_queue<std::tuple<int, int, int, int>, std::vector<std::tuple<int, int, int, int>>, std::greater<>> open;
    seen[key(sx, sy)] = { 0, { sx, sy } };
    open.push({ h(sx, sy), 0, sx, sy });
    P best{ sx, sy };
    int best_h = h(sx, sy), expanded = 0;
    while (!open.empty() && expanded < max_nodes) {
        const auto [f, g, x, y] = open.top();
        open.pop();
        if (g > seen[key(x, y)].first) continue;
        ++expanded;
        if (h(x, y) < best_h) { best_h = h(x, y); best = { x, y }; }
        if (x == gx && y == gy) break;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = x + dx, ny = y + dy;
                if (blocked(nx, ny) || (dx && dy && (blocked(x + dx, y) || blocked(x, y + dy)))) continue;
                const int ng = g + (dx && dy ? 14 : 10);
                const auto k = key(nx, ny);
                if (const auto it = seen.find(k); it != seen.end() && it->second.first <= ng) continue;
                seen[k] = { ng, { x, y } };
                open.push({ ng + h(nx, ny), ng, nx, ny });
            }
    }
    std::vector<P> path;
    for (P p = best; p != P{ sx, sy }; p = seen[key(p.first, p.second)].second) path.push_back(p);
    std::ranges::reverse(path);
    return path;
}

}  // namespace d2d::rules
