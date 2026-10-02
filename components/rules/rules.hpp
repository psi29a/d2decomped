// Game rules that don't need assets or a screen: item sizes and grid
// placement, vendor stock, prices, buying and selling. Everything reads
// the excel-derived Tables (filled by d2d's loader, or by hand in tests).
#pragma once

#include <d2s.hpp>
#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

struct ItemInfo {
    std::string invfile;
    int width = 1, height = 1;
    std::string namestr, type;
    int kind = 0;                      // 0 misc, 1 armor, 2 weapon
    int belt = -1;                     // armor.txt belt: belts.txt index
    bool two_handed = false;           // weapons.txt 2handed
    bool one_or_two = false;           // 1or2handed: a Barbarian wields it in one hand
    int req_str = 0, req_dex = 0, req_lvl = 0;
    std::string flippy;                // flippyfile: the on-the-ground animation
    std::string drop_sound;            // dropsound, played at flippy frame dropsfxframe
    int drop_frame = 0;
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
    bool treasure_class = false;       // TreasureClass: game.exe makes <code>3 .. <code>96 classes of it
    int rarity = 1;                    // Rarity: its bases' weight in those classes
    bool throwable = false;            // Throwable (the item's own type, FUN_0062ba80)
    std::array<int, 3> max_sock{};     // MaxSock1 / 25 / 40: by ilvl <= 25, <= 40, above
    int staff_mods = 7;                // StaffMods class (ama .. ass = 0 .. 6), 7 none
    int var_inv_gfx = 0;               // VarInvGfx: pictures to pick from
};
// TreasureClassEx.txt row: picks, the NoDrop weight, quality modifiers
// (Unique, Set, Rare, Magic, in 1024ths off the odds) and the weighted
// entries (item codes, other classes, "gld" or "gld,mul=N"; Prob >= 1).
// group / level: a monster's level moves it on to the group's next rows
// (FUN_00654e00); `next` is that row's name ("" past the group's last).
struct TreasureClass {
    int picks = 1, nodrop = 0;
    std::array<int, 4> mod{};
    std::vector<std::pair<std::string, int>> items;
    int group = 0, level = 0;
    std::string next;
};
// ItemRatio.txt per [class specific * 2 + uber][unique, set, rare, magic,
// superior, normal]: odds base, level divisor, minimum (FUN_00637910: the
// highest Version <= 100, i.e. the LoD rows).
struct QualityRatio { int base = 0, divisor = 1, min = 0; };
struct ItemBase {
    int minac = 0, maxac = 0, cost = 0;
    int mindam = 0, maxdam = 0, str_bonus = 0, dex_bonus = 0;   // weapons.txt (2handmindam for two-handers)
    int speed = 0, block = 0;                                   // weapons.txt speed (WSM) / armor.txt speed, armor.txt block
    bool stackable = false;
    int level = 0, durability = 0, gamble_cost = 0, min_stack = 0, max_stack = 0;
    std::string normcode, ubercode, ultracode;         // normal / exceptional / elite versions
    std::string better_gem;                            // misc.txt BetterGem ("" or "non": none)
    int bitfield1 = 0;                                 // Items +0xdc (bit 0: an imbue can take it, FUN_00629c80)
    bool quest = false;                                // Items +0x12a: a quest item
    bool only_unique = false;                          // Items +0x129 (unique): drops unique
    int spawn_stack = 0;                               // misc.txt spawnstack (Items +0xec)
    int rarity = 0;                                    // weapons / armor.txt rarity (Items +0xfc): a stand's pick
    int gem_sockets = 0, auto_prefix = 0, magic_lvl = 0;  // gemsockets (+0x138), auto prefix (+0xf8), magic lvl (+0x140)
    bool has_inv = false, no_durability = false;          // hasinv (+0x137), nodurability (+0x113)
    bool quest_diff = false;                              // questdiffcheck (+0x12b): stat 356 = the difficulty made in
    int max_1h = 0, max_2h = 0;                           // weapons.txt maxdam (+0xff) / 2handmaxdam (+0x103)
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
// UniqueItems / SetItems row: ladder (flag 8) never drops outside a ladder
// game, nolimit (flag 2) isn't one per game; set: its Sets.txt row.
struct Special {
    std::string code; int level = 0, rarity = 1; bool enabled = true; std::vector<Mod> mods; bool ladder = false, nolimit = false; int set = -1;
    std::vector<Mod> bonus;                                // a set's aprop1a..aprop5b, all 10 (unused: no code)
    bool bonus_apart = false;                              // add func: the bonuses go to their own lists (else the item's)
};
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
        const std::uint64_t product = std::uint64_t(low) * 0x6AC690C5u + high;
        low = std::uint32_t(product);
        high = std::uint32_t(product >> 32);
        return low;
    }
    int operator()(int bound) {
        if (bound < 1) return 0;
        next();
        return (bound & (bound - 1)) == 0 ? int(low & std::uint32_t(bound - 1)) : int(low % std::uint32_t(bound));
    }
    int range(int minimum, int maximum) { return maximum > minimum ? minimum + (*this)(maximum - minimum + 1) : minimum; }
};

// hireling.txt row (the stat and cost columns FUN_006637f0 reads).
struct Hireling {
    int version = 0, id = 0, cls = 0, act = 0, difficulty = 0, level = 0, gold = 0, exp_per_level = 0;
    int hit_points = 0, hp_per_level = 0, def = 0, def_per_level = 0, str = 0, str_per_level = 0, dex = 0, dex_per_level = 0;
    int dmg_min = 0, dmg_max = 0, dmg_per_level = 0;
    int names = 1;                                      // NameFirst..NameLast
    int attack_rating = 0, ar_per_level = 0;
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
    // What a stand can hold (FUN_00555e70 / FUN_00555fb0): armor.txt, then
    // weapons.txt, codes in row order, spawnable and not quest items.
    std::array<std::vector<std::string>, 2> stand_bases;
    std::array<std::vector<ClassSkill>, 7> class_skills;   // by d2s class, Skills.txt order
    // Item generation.
    std::vector<Affix> prefixes, suffixes;                 // raw rows (row 0 blank), = save IDs
    std::vector<Affix> automagic;                          // AutoMagic rows: class_affix - 1
    // QualityItems rows (superior): which bases take it (armor, weapon,
    // shield, scepter, wand, staff, bow, boots, gloves, belt: the record's
    // flags, FUN_0065e7d0) and its mods.
    struct Superior { std::array<bool, 10> fits{}; std::vector<Mod> mods; };
    std::vector<Superior> superior;
    std::vector<std::string> skill_itype;                  // Skills.txt itypea1 by Id (staffmods)
    std::vector<std::pair<int, int>> skill_levels;         // Skills.txt reqlevel, maxlvl by Id (FUN_00644710 / FUN_004aa8b0)
    std::array<int, 7> class_first_skill{};                // per class its first skill Id (FUN_006460f0)
    std::vector<Special> uniques, sets;
    std::unordered_map<std::string, std::vector<PropFunc>> properties;
    std::unordered_map<std::string, int> skill_id;         // Skills.txt skill name -> Id
    int rare_prefixes = 0, rare_suffixes = 0;              // RarePrefix / RareSuffix rows
    std::vector<std::string> gamble;                       // gamble.txt codes
    std::array<GambleRates, 3> gamble_rates{};
    std::vector<Hireling> hirelings;                       // hireling.txt rows
    // Drops: TreasureClassEx by name (plus the auto weapN / armoN classes)
    // and ItemRatio.
    std::unordered_map<std::string, TreasureClass> treasure;
    std::array<std::array<QualityRatio, 6>, 4> quality_ratio{};
    // misc.txt potions: life / mana restored (hpregen / manarecovery: that
    // much over `ticks`; hitpoints / mana on a rejuvenation: percent, at once).
    struct Potion { int life = 0, mana = 0, ticks = 0; bool percent = false; };
    std::unordered_map<std::string, Potion> potions;
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

inline std::pair<int, int> item_size(const Tables& tables, const std::string& code) {
    const auto info = tables.item_info.find(code);
    return info != tables.item_info.end() ? std::pair{ info->second.width, info->second.height } : std::pair{ 1, 1 };
}

// First free w x h spot in a cols x rows grid of placed items, scanning
// column by column (where D2 autoplaces pickups); {-1, -1} if full.
inline std::pair<int, int> free_spot(const Tables& tables, const std::vector<const d2d::d2s::Item*>& placed,
                                     int cols, int rows, int width, int height) {
    std::vector<bool> used(std::size_t(cols * rows));
    for (const auto* placed_item : placed) {
        const auto [item_width, item_height] = item_size(tables, placed_item->code);
        for (int y = placed_item->row; y < std::min(placed_item->row + item_height, rows); ++y)
            for (int x = placed_item->column; x < std::min(placed_item->column + item_width, cols); ++x) used[std::size_t(y * cols + x)] = true;
    }
    for (int x = 0; x + width <= cols; ++x)
        for (int y = 0; y + height <= rows; ++y) {
            bool free = true;
            for (int row = y; row < y + height && free; ++row)
                for (int column = x; column < x + width && free; ++column) free = !used[std::size_t(row * cols + column)];
            if (free) return { x, y };
        }
    return { -1, -1 };
}

// Puts an item into the store grid from tab on (weapons spill 1 -> 2).
inline bool store_place(const Tables& tables, Store& store, int tab, d2d::d2s::Item item) {
    const auto [width, height] = item_size(tables, item.code);
    for (int i = tab; i < 4; ++i) {
        std::vector<const d2d::d2s::Item*> placed;
        for (const auto& placed_item : store.tabs[std::size_t(i)]) placed.push_back(&placed_item);
        if (const auto [x, y] = free_spot(tables, placed, 10, 10, width, height); x >= 0) {
            item.column = x; item.row = y; item.location = 0; item.panel = 1;
            store.tabs[std::size_t(i)].push_back(std::move(item));
            return true;
        }
        if (i != 1) break;
    }
    return false;
}

inline int store_tab_for(const Tables& tables, const std::string& code) {
    const auto info = tables.item_info.find(code);
    const int kind = info != tables.item_info.end() ? info->second.kind : 0;
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

inline Store open_store(const Tables& tables, int hc_idx, std::string npc_id, Rng& rng) {
    Store store;
    store.npc_id = std::move(npc_id);
    store.hc_idx = hc_idx;
    store.vendor = vendor_index(hc_idx);
    if (store.vendor < 0) return store;
    for (const auto& vendor_item : tables.vendor_items[std::size_t(store.vendor)]) {
        if (vendor_item.perm) store.perm.push_back(vendor_item.code);
        int count = vendor_item.perm ? 1 : rng.range(vendor_item.min, vendor_item.max);
        while (count-- > 0) {
            d2d::d2s::Item item;
            item.code = vendor_item.code;
            if (const auto found = tables.item_base.find(vendor_item.code); found != tables.item_base.end() && store_tab_for(tables, vendor_item.code) == 0)
                item.defense = found->second.minac;
            store_place(tables, store, store_tab_for(tables, vendor_item.code), std::move(item));
        }
    }
    for (int i = 0; i < 4; ++i) if (!store.tabs[std::size_t(i)].empty()) { store.tab = i; break; }
    return store;
}

// Buy (sell = false) or sell price of an item at the NPC with MonStats Id
// npc_id, for the player's header (difficulty, quest flags).
inline int item_price(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, bool sell,
                      const d2d::d2s::Header& header) {
    const auto found = tables.item_base.find(item.code);
    const int base = found != tables.item_base.end() ? found->second.cost : 0;
    auto extra = [&](const std::vector<std::pair<int, int>>& costs, int index) {
        if (index < 0 || std::size_t(index) >= costs.size()) return 0;
        const auto [mul, add] = costs[std::size_t(index)];
        return (base < 0x10000 ? mul * base / 1024 : base / 1024 * mul) + add;
    };
    int x = 0;
    switch (item.quality) {
        case 1: x = -(base / 2); break;
        case 4: x = extra(tables.prefix_cost, item.prefix) + extra(tables.suffix_cost, item.suffix); break;
        case 5: x = extra(tables.set_cost, item.set_id); break;
        case 7: x = extra(tables.unique_cost, item.unique_id); break;
        case 6: case 8:
            for (int i = 0; i < 6; ++i) x += extra(i % 2 == 0 ? tables.prefix_cost : tables.suffix_cost, item.affixes[std::size_t(i)]);
            break;
        default: break;
    }
    long long price = base + x;
    for (const auto& socketed : item.socketed_items)
        if (const auto socketed_base = tables.item_base.find(socketed.code); socketed_base != tables.item_base.end()) price += socketed_base->second.cost / 2;
    if (sell && item.ethereal) price /= 4;
    const auto price_row = tables.npc_prices.find(npc_id);
    const int diff = header.active_difficulty();
    if (price_row != tables.npc_prices.end()) {
        const auto& prices = price_row->second;
        price = price * (sell ? prices.sell : prices.buy) / 1024;
        for (int quest = 0; quest < 3; ++quest)
            if (prices.qflag[std::size_t(quest)] && (header.quest_flag(diff, prices.qflag[std::size_t(quest)], 0) || header.quest_flag(diff, prices.qflag[std::size_t(quest)], 1)))
                price = price * (sell ? prices.qsell[std::size_t(quest)] : prices.qbuy[std::size_t(quest)]) / 1024;
    }
    if (item.quantity > 1 && !(found != tables.item_base.end() && found->second.stackable)) price *= item.quantity;
    if (sell && price_row != tables.npc_prices.end()) price = std::min<long long>(price, price_row->second.max_buy[std::size_t(diff)]);
    return int(std::max<long long>(price, 1));
}

// An item's max durability with its modifiers: the save's base, plus
// flat maxdurability (stat 73), times item_maxdurability_percent (75).
inline int max_durability(const d2d::d2s::Item& item) {
    int flat = 0, pct = 0;
    for (const auto& prop : item.props) {
        if (prop.stat == 73) flat += int(prop.value);
        if (prop.stat == 75) pct += int(prop.value);
    }
    return (item.max_durability + flat) * (100 + pct) / 100;
}
// item_indesctructible (stat 152): never wears, never needs repair.
inline bool indestructible(const d2d::d2s::Item& item) {
    return std::ranges::any_of(item.props, [](const auto& prop) { return prop.stat == 152 && prop.value; });
}

// Repair cost at the NPC (FUN_0062efb0 mode 3): the buy base with its
// quality extras, times missing / max durability, times npc.txt rep mult
// and the quest rep mults / 1024; 0 when there's nothing to repair.
// Ethereal items can't be repaired.
// ponytail: no charge recharging, no socket or "reduced prices" terms.
inline int repair_cost(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header) {
    const int max = max_durability(item);
    if (item.max_durability <= 0 || item.durability >= max || item.ethereal || indestructible(item)) return 0;
    const auto found = tables.item_base.find(item.code);
    const int base = found != tables.item_base.end() ? found->second.cost : 0;
    auto extra = [&](const std::vector<std::pair<int, int>>& costs, int index) {
        if (index < 0 || std::size_t(index) >= costs.size()) return 0;
        const auto [mul, add] = costs[std::size_t(index)];
        return (base < 0x10000 ? mul * base / 1024 : base / 1024 * mul) + add;
    };
    long long x = base;
    switch (item.quality) {
        case 1: x -= base / 2; break;
        case 4: x += extra(tables.prefix_cost, item.prefix) + extra(tables.suffix_cost, item.suffix); break;
        case 5: x += extra(tables.set_cost, item.set_id); break;
        case 7: x += extra(tables.unique_cost, item.unique_id); break;
        case 6: case 8:
            for (int i = 0; i < 6; ++i) x += extra(i % 2 == 0 ? tables.prefix_cost : tables.suffix_cost, item.affixes[std::size_t(i)]);
            break;
        default: break;
    }
    long long cost = (max - item.durability) * x / max;
    if (const auto price_row = tables.npc_prices.find(npc_id); price_row != tables.npc_prices.end()) {
        const auto& prices = price_row->second;
        cost = cost * prices.rep / 1024;
        const int diff = header.active_difficulty();
        for (int quest = 0; quest < 3; ++quest)
            if (prices.qflag[std::size_t(quest)] && (header.quest_flag(diff, prices.qflag[std::size_t(quest)], 0) || header.quest_flag(diff, prices.qflag[std::size_t(quest)], 1)))
                cost = cost * prices.qrep[std::size_t(quest)] / 1024;
    }
    return int(std::max<long long>(cost, 1));
}

// Repairs item i (gold first from the inventory, then the stash, as a
// buy). False if it's whole or you can't pay.
inline bool store_repair(const Tables& tables, const Store& store, d2d::d2s::Item& item, d2d::d2s::Stats& stats) {
    const int cost = repair_cost(tables, item, store.npc_id, store.header);
    if (cost <= 0 || stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < cost) return false;
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), cost);
    stats.values[d2d::d2s::kGold] -= from_inv;
    stats.values[d2d::d2s::kGoldBank] -= cost - from_inv;
    item.durability = max_durability(item);
    return true;
}

// Repair all: every worn or carried item, in list order, while the gold lasts.
inline int store_repair_all(const Tables& tables, const Store& store, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    int repaired = 0;
    for (auto& item : items)
        if (item.location == 1 || (item.location == 0 && item.panel == 1)) repaired += store_repair(tables, store, item, stats);
    return repaired;
}

// Buys stock item i of the open tab into the inventory (10x4): gold
// down by the price, the item leaves the stock unless it's a perm one.
// False if it doesn't fit or you can't afford it.
// ponytail: no "not enough gold"/"no room" message, no stacks or quantity.
inline bool store_buy(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    auto& tab = store.tabs[std::size_t(store.tab)];
    const auto& item = tab[std::size_t(index)];
    const int price = item_price(tables, item, store.npc_id, false, store.header);
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < price) return false;
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& x : items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
    const auto [width, height] = item_size(tables, item.code);
    const auto [x, y] = free_spot(tables, inv, 10, 4, width, height);
    if (x < 0) return false;
    auto bought = item;
    bought.column = x; bought.row = y; bought.location = 0; bought.panel = 1;
    items.push_back(std::move(bought));
    // Carried gold first, then the stash (the store shows it for that).
    // ponytail: that order is a guess; the server's buy isn't RE'd.
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), price);
    stats.values[d2d::d2s::kGold] -= from_inv;
    stats.values[d2d::d2s::kGoldBank] -= price - from_inv;
    if (std::ranges::find(store.perm, item.code) == store.perm.end()) tab.erase(tab.begin() + index);
    return true;
}

// Sells inventory item i: gold up by the sell value (carried gold caps
// at clvl x 10000), the item joins the stock.
// ponytail: quest items aren't refused, no belt/equipped selling.
inline void store_sell(const Tables& tables, Store& store, std::size_t index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats) {
    const int price = item_price(tables, items[index], store.npc_id, true, store.header);
    stats.values[d2d::d2s::kGold] = std::min<std::int64_t>(stats.get(d2d::d2s::kGold) + price,
                                                       stats.get(d2d::d2s::kLevel) * 10000);
    store_place(tables, store, store_tab_for(tables, items[index].code), items[index]);
    items.erase(items.begin() + std::ptrdiff_t(index));
}

// Is type t (or one of its Equiv ancestors) the type want?
inline bool type_is(const Tables& tables, const std::string& type, std::string_view want, int depth = 0) {
    if (type.empty() || depth > 8) return false;
    if (type == want) return true;
    const auto found = tables.types.find(type);
    return found != tables.types.end()
        && (type_is(tables, found->second.equiv[0], want, depth + 1) || type_is(tables, found->second.equiv[1], want, depth + 1));
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
inline std::string type_class(const Tables& tables, const std::string& type, int depth = 0) {
    const auto found = tables.types.find(type);
    if (found == tables.types.end() || depth > 8) return {};
    if (!found->second.cls.empty()) return found->second.cls;
    for (const auto& equivalent : found->second.equiv)
        if (auto class_code = type_class(tables, equivalent, depth + 1); !class_code.empty()) return class_code;
    return {};
}

inline constexpr std::string_view kClassCode[7] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };   // by d2s class

// What the player brings to an equip check: d2s class and the current
// strength, dexterity and level.
struct Wearer { int cls = 0, str = 0, dex = 0, lvl = 1; };

inline const ItemInfo* info_of(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_info.find(item.code);
    return found != tables.item_info.end() ? &found->second : nullptr;
}

// Can `it` be worn in body slot `slot` by `w` at all (slot, class, stat
// and level requirements), ignoring what's already equipped?
// ponytail: base requirements only; ethereal/"requirements -x%" and the
// unique/set level requirements aren't applied.
inline bool can_wear(const Tables& tables, const d2d::d2s::Item& item, int slot, const Wearer& wearer) {
    const auto* info = info_of(tables, item);
    if (!info) return false;
    const auto found = tables.types.find(info->type);
    if (found == tables.types.end() || slot < 1 || (found->second.body[0] != slot && found->second.body[1] != slot)) return false;
    if (const auto class_code = type_class(tables, info->type); !class_code.empty() && (wearer.cls < 0 || wearer.cls > 6 || class_code != kClassCode[std::size_t(wearer.cls)]))
        return false;
    return wearer.str >= info->req_str && wearer.dex >= info->req_dex && wearer.lvl >= info->req_lvl;
}

// A two-handed weapon fills both hands (a Barbarian swings 1or2handed
// ones in one); only a quiver can join it.
inline bool blocks_other_hand(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer) {
    const auto* info = info_of(tables, item);
    return info && info->two_handed && !(wearer.cls == 4 && info->one_or_two);
}

// Can a and b be held in the two hands together? Weapon + shield or
// quiver; two weapons only for a Barbarian (or an Assassin's two claws);
// a two-hander only with a quiver.
inline bool hands_ok(const Tables& tables, const d2d::d2s::Item& first, const d2d::d2s::Item& second, const Wearer& wearer) {
    const auto* first_info = info_of(tables, first);
    const auto* second_info = info_of(tables, second);
    if (!first_info || !second_info) return false;
    const bool first_quiver = type_is(tables, first_info->type, "misl"), second_quiver = type_is(tables, second_info->type, "misl");
    if (blocks_other_hand(tables, first, wearer)) return second_quiver;
    if (blocks_other_hand(tables, second, wearer)) return first_quiver;
    const bool first_weapon = type_is(tables, first_info->type, "weap"), second_weapon = type_is(tables, second_info->type, "weap");
    if (first_weapon && second_weapon) return wearer.cls == 4 || (wearer.cls == 6 && type_is(tables, first_info->type, "h2h") && type_is(tables, second_info->type, "h2h"));
    return first_weapon != second_weapon;
}

// The item cursor. D2 moves one item at a time: picking up takes it out
// of the list into `held`; putting down places it, swapping with the one
// item it lands on (into `held`), and fails when it would overlap two or
// more, doesn't fit, or isn't allowed there. Each put returns whether it
// changed anything.

// Put `held` into a stored grid (location 0; panel 1 inventory, 4 cube,
// 5 stash) with its top-left cell at (col, row).
inline bool put_in_grid(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                        int panel, int cols, int rows, int col, int row) {
    if (!held || (panel == 4 && held->code == "box")) return false;   // the cube can't go in itself
    const auto [width, height] = item_size(tables, held->code);
    if (col < 0 || row < 0 || col + width > cols || row + height > rows) return false;
    int hit = -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& placed_item = items[i];
        if (placed_item.location != 0 || placed_item.panel != panel) continue;
        const auto [item_width, item_height] = item_size(tables, placed_item.code);
        if (placed_item.column < col + width && col < placed_item.column + item_width && placed_item.row < row + height && row < placed_item.row + item_height) {
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
inline bool equip(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                  int slot, const Wearer& wearer) {
    if (!held || !can_wear(tables, *held, slot, wearer)) return false;
    const bool hand = slot == 4 || slot == 5;
    const int other = hand ? 9 - slot : 0;
    int off = -1;                                      // the item coming off
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& worn = items[i];
        if (worn.location != 1) continue;
        const bool comes_off = worn.slot == slot
            || (hand && worn.slot == other && !hands_ok(tables, *held, worn, wearer));
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
inline bool put_in_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
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
    int block = 0;                                     // BlockFactor
};

// Spends up to n unspent stat points (stat 4) on stat (0 strength, 1
// energy, 2 dexterity, 3 vitality), as the char panel's buttons ask with
// packet 0x3a (FUN_004a78c0). Vitality raises life and stamina, energy
// mana, current and max (8.8 fixed: a quarter point is 64). Returns the
// points spent.
// ponytail: the server's handler isn't traced; gains are CharStats'.
inline int spend_stat_points(d2d::d2s::Stats& stats, int stat, int count, const ClassGains& gains) {
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
inline void respec(d2d::d2s::Stats& stats, const std::array<int, 4>& base, const ClassGains& gains) {
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
// ponytail: base levels; +skills from items don't count toward anything.
inline bool can_learn(const Tables& tables, int cls, int skill_index, const std::array<std::uint8_t, 30>& levels, int clvl) {
    if (cls < 0 || cls > 6 || skill_index < 0 || std::size_t(skill_index) >= tables.class_skills[std::size_t(cls)].size()) return false;
    const auto& class_skill = tables.class_skills[std::size_t(cls)][std::size_t(skill_index)];
    if (levels[std::size_t(skill_index)] >= class_skill.max_level || clvl < class_skill.req_level) return false;
    for (const int required : class_skill.req) if (required >= 0 && levels[std::size_t(required)] == 0) return false;
    return true;
}

// Spends a skill point (stat 5) on skill i. Returns whether it did.
inline bool learn_skill(const Tables& tables, int cls, int skill_index, std::array<std::uint8_t, 30>& levels, d2d::d2s::Stats& stats) {
    if (stats.get(d2d::d2s::kSkillPts) <= 0 || !can_learn(tables, cls, skill_index, levels, int(stats.get(d2d::d2s::kLevel)))) return false;
    ++levels[std::size_t(skill_index)];
    --stats.values[d2d::d2s::kSkillPts];
    return true;
}

// Healers restore life and mana when you talk to them: Akara, Fara,
// Ormus, Jamella, Malah (MonStats hcIdx).
// ponytail: the list is D2's known healers, not located in game.exe;
// poison/curse removal isn't done. The save's max
// life/mana are base values without gear (current can be higher), so
// healing only raises to them; the true max needs item stat totals.
inline bool is_healer(int hc_idx) {
    return hc_idx == 148 || hc_idx == 178 || hc_idx == 255 || hc_idx == 405 || hc_idx == 513;
}
inline void heal(d2d::d2s::Stats& stats) {
    using namespace d2d::d2s;
    stats.values[kLife] = std::max(stats.values[kLife], stats.values[kMaxLife]);
    stats.values[kMana] = std::max(stats.values[kMana], stats.values[kMaxMana]);
}

// Pick up item i into the (empty) cursor.
inline bool pick_up(std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held, std::size_t index) {
    if (held || index >= items.size()) return false;
    held = std::move(items[index]);
    items.erase(items.begin() + std::ptrdiff_t(index));
    return true;
}

// A mod's stats, per Properties.txt funcs (FUN_0065fd70 over the func
// table at 0x7462f8), in the save's (stat, param, value) form. A func
// that takes a value draws min..max (FUN_0065e9e0: none when min == max)
// when it needs one: 1 / 2 / 13 / 21 / 22 always, 12 / 36 as the param;
// 3 / 4 / 5 / 6 / 7 / 8 / 9 / 10 / 14 / 24 reuse what the first func
// gave (0: a draw), 17 the param (0: a draw); 15 / 16 are min / max and
// 11 / 18 / 20 / 23 don't draw. Nothing is stored for no stat or a 0
// (FUN_0065ea50, which counts a poison with poison max damage).
// 1 / 2 / 3 / 4 / 8 value, 5 / 6 / 7 min / max / % damage, 9 / 22 / 24
// param, 10 skill tab (class*3+tab -> class<<3|tab), 11 chance to cast
// (param skill<<6 | level, value chance, 5 if none), 12 random skill
// (param min..max, value param), 19 charges (FUN_0065f6a0: of c = min
// rand(c - c/8) + c/8 + 1 now, value now | c<<8), 20 indestructible
// (stat 152), 21 class skills, 36 the param drawn, value the func's val.
// With `on` (the item the mods land on, FUN_0065fec0's kind 1 superior)
// as game.exe: ac% (2 / 4), and on a superior ac / dur% (1 / 3 / 13),
// first raise an armor's defence to maxac + 1 (FUN_0065ccc0); min / max
// damage (5 / 6) go to the one-hand, two-hand and throw stats the base has
// (FUN_0065ecf0 / FUN_0065ee80; anything but a weapon all three); dmg%
// (7) too small to move the base's max is +1 max damage instead
// (FUN_0065f010); dur% (13) fills the durability to the new most
// (FUN_0065fc90); ethereal (23) makes it so.
// 11 / 19's level is max, or with none off ilvl and the skill's reqlevel
// ((ilvl - req) / 4 + 1 up to its maxlvl; negative: one per (99 - req) /
// -max levels past req). Codes match whatever their case.
// ponytail: 18 (by time) is dropped; sockets (14) only draws; 5 / 6 don't
// clamp a negative to the base (kept by max damage, not min); without
// `on` (affixes) damage is one-hand, charges full and 11 / 19 off ilvl 1.
struct ModItem { d2d::d2s::Item* item; const ItemBase* base; int kind; bool throwable, raise; };   // raise: ac / dur% (1 / 3 / 13) raise the defence too (superior: FUN_0065fec0 kind 1)
inline std::string lower_case(std::string_view text) {
    std::string out(text);
    for (auto& letter : out) letter = char(std::tolower(static_cast<unsigned char>(letter)));
    return out;
}
inline void apply_mod(const Tables& tables, const Mod& mod, std::vector<d2d::d2s::ItemProp>& out, Rng& rng, const ModItem* on = nullptr) {
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
        if (stat == 58) out.push_back({ 326, 0, 1 });      // poison max damage counts a poison (poison_count)
        return amount;
    };
    auto damage = [&](int one_hand, int two_hand, int thrown, int amount) {
        const bool weapon = !on || on->kind == 2;
        if (!weapon || !on || on->base->max_1h != 0 || on->base->max_2h == 0) put(one_hand, 0, amount);
        if (on && (!weapon || on->base->max_2h != 0 || on->base->max_1h == 0)) put(two_hand, 0, amount);
        if (on && (!weapon || on->throwable)) put(thrown, 0, amount);
        return amount;
    };
    auto skill_level = [&] {                               // FUN_0065f470 / FUN_0065f6a0
        const bool known = par >= 0 && std::size_t(par) < tables.skill_levels.size();
        const int req = known ? tables.skill_levels[std::size_t(par)].first : 0x7fffffff;
        const int ilvl = on ? on->item->ilvl : 1;
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
        if (on && on->kind == 1 && on->base->maxac != 0 && (stat == 16 || stat == 31)
            && (func == 2 || func == 4 || (on->raise && (func == 1 || func == 3 || func == 13))))
            on->item->defense = std::max(on->item->defense + 1, on->base->maxac + 1);
        int result = 0;
        switch (func) {
            case 1: case 2: result = put(stat, 0, draw()); break;
            case 3: case 4: case 8: result = put(stat, 0, value()); break;
            case 5: result = damage(21, 23, 159, value()); break;
            case 6: result = damage(22, 24, 160, value()); break;
            case 7:
                result = value();
                if (on && on->kind == 2 && std::max(on->base->max_1h, on->base->max_2h) * result / 100 == 0) damage(22, 24, 160, 1);
                else { put(18, 0, result); put(17, 0, result); }
                break;
            case 9: if (const int amount = value(); amount != 0) result = put(stat, par, amount); break;
            case 10: if (const int amount = value(); amount != 0) result = put(stat, par % 3 + par / 3 * 8, amount); break;
            case 11: result = put(stat, par * 64 + (skill_level() & 63), mod.min < 1 ? 5 : mod.min); break;
            case 12: result = put(stat, draw(), par); break;
            case 13:
                result = put(stat, 0, draw());
                if (on && result != 0) {
                    int percent = 0, more = on->item->max_durability;
                    for (const auto& prop : out) {
                        if (prop.stat == 75) percent += prop.value;
                        if (prop.stat == 73) more += prop.value;
                    }
                    if (const int most = more + more * percent / 100; most > 0) on->item->durability = most;
                }
                break;
            case 14: if (on && first < 1) draw(); break;
            case 15: result = mod.min; if (stat == 21) damage(21, 23, 159, result); else put(stat, 0, result); break;
            case 16: result = mod.max; if (stat == 22) damage(22, 24, 160, result); else put(stat, 0, result); break;
            case 17:
                result = par != 0 ? par : draw();
                if (result != 0 && stat == 22) damage(22, 24, 160, result);
                else put(stat, 0, result);
                break;
            case 19: {
                if (stat < 0) break;
                const int level = skill_level();
                const int charges = mod.min == 0 ? 5 : std::clamp(mod.min < 0 ? -mod.min + -mod.min * level / 8 : mod.min, 1, 255);
                const int now = on ? (rng(charges - charges / 8) + charges / 8 + 1) & 0xff : charges;
                out.push_back({ stat, par << 6 | (level & 63), now + charges * 256 });
                result = charges;
                break;
            }
            case 20: if (on) result = put(152, 0, 1); break;
            case 21: result = put(stat, property_func.val, draw()); break;
            case 22: result = put(stat, par, draw()); break;
            case 23:
                if (on && !on->item->ethereal && on->item->max_durability > 0) {   // FUN_0065e4d0
                    on->item->ethereal = true;
                    if (on->kind == 1) on->item->defense = on->item->defense * 3 / 2;   // durability as it was (FUN_00556ca0 halves it, not this)
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

// Affix level for an item of level ilvl with base qlvl (the known D2
// formula): ilvl - qlvl/2 below 99 - qlvl/2, else 2*ilvl - 99.
// ponytail: no "magic lvl" (wands, staves, circlets).
inline int affix_level(int ilvl, int qlvl) {
    ilvl = std::clamp(std::max(ilvl, qlvl), 1, 99);
    return ilvl < 99 - qlvl / 2 ? ilvl - qlvl / 2 : 2 * ilvl - 99;
}

// A random affix row for an item of `type` at affix level alvl, weighted
// by frequency; skips groups already taken. 0 = none fits.
inline int pick_affix(const Tables& tables, const std::vector<Affix>& list, const std::string& type, int alvl, bool rare,
                      const std::vector<int>& groups, Rng& rng) {
    std::vector<std::pair<int, int>> ok;                   // (row, frequency)
    int total = 0;
    for (std::size_t row = 1; row < list.size(); ++row) {
        const auto& affix = list[row];
        if (!affix.spawnable || affix.frequency <= 0 || affix.level > alvl || (affix.max_level > 0 && affix.max_level < alvl) || (rare && !affix.rare))
            continue;
        if (std::ranges::find(groups, affix.group) != groups.end()) continue;
        if (std::ranges::none_of(affix.itypes, [&](const auto& item_type) { return type_is(tables, type, item_type); })) continue;
        if (std::ranges::any_of(affix.etypes, [&](const auto& excluded_type) { return type_is(tables, type, excluded_type); })) continue;
        ok.push_back({ int(row), affix.frequency });
        total += affix.frequency;
    }
    if (ok.empty()) return 0;
    int roll = rng(total);
    for (auto [affix_row, frequency] : ok) { if (roll < frequency) return affix_row; roll -= frequency; }
    return ok.back().first;
}

// A new item of `code` at level ilvl and quality (4 magic, 5 set, 6
// rare, 7 unique; set/unique fall back to rare when none fits),
// identified, with its defence and durability rolled.
// A made drop (FUN_00558d90) passes its unit seed (+0x20): the base rolls
// (FUN_00557ab0) come off it: arrows / bolts (ItemTypes Quiver) min +
// rand(max - min); armor durability rand(dur / 2) + dur / 2 (at most 255),
// then defence minac + rand(maxac - minac + 1); a weapon's stack then its
// durability; misc stacks min + rand(spawnstack - min). `rng` is then its
// own seed (item data +4): the unique (FUN_005566b0) / set (FUN_005c25c0)
// pick is its first draw. `found_uniques` is the game's one-per-game list
// (+0x1b24): a unique found already fails (but a quest item's); one found
// is added unless nolimit. A set 29 (the Cow King's) drops only from a Hell Bovine (`bovine`).
// A failed unique turns rare with 3x durability, a set 2x (FUN_00557450).
// With a unit seed (a made drop) the rest of FUN_00557ab0 / FUN_00557450
// too, in its order: the picture (ItemTypes VarInvGfx) off the unit seed;
// low (FUN_005c2d40): sub rand(4), durability dur*33/100, defence 75%;
// superior (FUN_005c2970): a QualityItems row rand(8) (4 if throwable or
// nodurability) redrawn until FUN_0065e7d0 fits, its mods (ac% sets the
// defence to maxac + 1 first, dur% raises durability); staffmods
// (FUN_005c0f90) on low / normal / superior; then ethereal (FUN_00556ca0:
// rand(100) < 5, not low / set / quest: defence 3/2, max durability / 2 +
// 1), sockets (FUN_00556b60, normal / superior: rand(100) < 33, the own
// seed's first low % the most + 1, the most = gemsockets / MaxSock by
// ilvl / 3, 4, 6 by difficulty) and the AutoMagic class affix (FUN_005c1560
// off the base's auto prefix group).
// ponytail: rare affix count 3..6 alternating prefix/suffix, magic 1/4
// prefix, 1/4 suffix, 1/2 both; the classic (version < 100) rules and
// forced picks (struct +0x40) aren't here; without a unit seed everything
// comes off `rng` (stores, gambling, imbue) and none of the above is
// rolled; base damage isn't kept on the item, so low's 75%, ethereal's 3/2
// and dmg%'s +1 max damage on a weak weapon only reach its props.
inline d2d::d2s::Item generate_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                                    Rng* unit_seed = nullptr, std::vector<bool>* found_uniques = nullptr, bool bovine = false,
                                    int difficulty = 0) {
    d2d::d2s::Item item;
    item.code = code;
    item.identified = true;
    item.ilvl = std::clamp(ilvl, 1, 99);
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
    auto max_durability = [&] { const int most = item.max_durability + stat_total(73); return most + most * stat_total(75) / 100; };   // FUN_00625e00
    auto has_durability = [&] { return base && !base->no_durability && base->durability > 0 && item.max_durability > 0 && stat_total(152) < 1; };
    // Staffmods (FUN_005c0f90): 0-3 class skills of the type's class off
    // tiers by ilvl, at level 1-3 (low: 1). A drop's percent add is 0.
    auto staff_mods = [&] {
        if (!item_type || item_type->staff_mods >= 7) return;
        auto percent = [&] { return int(rng.next() % 100); };
        const int roll = percent(), count = roll < 31 ? 0 : roll < 71 ? 1 : roll < 91 ? 2 : 3;
        const int tier = item.ilvl >= 37 ? 5 : item.ilvl >= 25 ? 4 : item.ilvl >= 19 ? 3 : item.ilvl >= 12 ? 2 : 1;
        const int first = tables.class_first_skill[std::size_t(item_type->staff_mods)];
        std::array<int, 3> picks{ -1, -1, -1 };
        for (int k = 0; k < count; ++k) {
            const int tier_roll = percent();
            int at = std::max(tier_roll < 11 ? tier - 2 : tier_roll < 31 ? tier - 1 : tier_roll < 81 ? tier : tier + 1, 1);
            if (item.quality == 1) at = std::min(at, 4);
            int skill = 0;
            for (int tries = 0; tries < 6; ++tries) {
                skill = int(rng.next() % 5) + (at - 1) * 5 + first;
                const bool usable = skill < 0 || std::size_t(skill) >= tables.skill_itype.size() || tables.skill_itype[std::size_t(skill)].empty()
                                 || type_is(tables, type, tables.skill_itype[std::size_t(skill)]);
                if (usable && std::ranges::find(picks, skill) == picks.end()) { picks[std::size_t(k)] = skill; break; }
            }
            int level = 1;
            if (item.quality != 1) { const int level_roll = percent(); level = level_roll < 60 ? 1 : level_roll < 90 ? 2 : 3; }
            const auto old = std::ranges::find_if(item.props, [&](const auto& prop) { return prop.stat == 107 && prop.param == skill; });
            if (old != item.props.end()) old->value = level;
            else item.props.push_back({ 107, skill, level });
        }
    };
    // FUN_00557450's tail on every quality: ethereal, sockets, the class affix.
    auto finish = [&] {
        if (!unit_seed || !base) return item;
        if ((kind == 1 || kind == 2) && item.quality != 1 && item.quality != 5 && !base->quest && has_durability() && rng(100) < 5) {
            item.ethereal = true;                          // FUN_0065e4d0
            if (kind == 1) item.defense = item.defense * 3 / 2;
            if (has_durability()) { item.max_durability = item.max_durability / 2 + 1; item.durability = max_durability(); }
        }
        if (item.quality >= 2 && item.quality <= 3 && base->has_inv && !base->stackable && item_type && info) {
            const int by_ilvl = item_type->max_sock[std::size_t(item.ilvl <= 25 ? 0 : item.ilvl <= 40 ? 1 : 2)];
            const int most = std::min(base->gem_sockets, by_ilvl);                         // FUN_0062bc20
            const int capped = std::min(most, std::array{ 3, 4, 6 }[std::size_t(std::clamp(difficulty, 0, 2))]);
            if (capped > 0 && rng(100) < 33) {
                item.socketed = true;                      // FUN_0062bcb0: at most the grid, 6
                item.sockets = std::min({ int(start % std::uint32_t(capped)) + 1, info->width * info->height, 6 });
            }
        }
        static constexpr std::array kAuto{ 1, 2, 3, 4, 6, 8, 9 };
        if (base->auto_prefix != 0 && std::ranges::find(kAuto, item.quality) != kAuto.end()) {
            rng.next();                                    // FUN_005c1560's coin (forced on)
            const int level = std::max(item.ilvl, qlvl);
            const int alvl = std::clamp(base->magic_lvl ? level + base->magic_lvl : level < 99 - qlvl / 2 ? level - qlvl / 2 : 2 * level - 99, 1, 99);
            std::vector<std::pair<int, int>> fits;         // (row, weight)
            int total = 0;
            for (std::size_t row = 0; row < tables.automagic.size(); ++row) {
                const auto& affix = tables.automagic[row];
                if (!affix.spawnable || affix.frequency <= 0 || affix.group != base->auto_prefix || affix.level > alvl || (affix.max_level > 0 && affix.max_level < alvl))
                    continue;
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
                const ModItem on{ &item, base, kind, item_type && item_type->throwable, false };
                for (const auto& mod : tables.automagic[std::size_t(chosen)].mods) apply_mod(tables, mod, item.props, rng, &on);
            }
        }
        if (base->quest && base->quest_diff) item.props.push_back({ 356, 0, difficulty });   // FUN_00557ab0 after FUN_00557450
        return item;
    };
    // Low / normal / superior: no affixes; stacks (arrows, bolts, keys) roll
    // their quantity.
    if (quality <= 3) {
        item.quality = std::max(quality, 1);
        if (!unit_seed && base && base->stackable) item.quantity = rng.range(base->min_stack, base->max_stack);
        if (!unit_seed || !base) return item;
        bool made = true;                                  // low / superior that doesn't take: FUN_005572a0, normal off a fresh own seed
        if (item.quality == 1 && kind != 1 && kind != 2) {
            made = false;
        } else if (item.quality == 1) {
            item.qsub = rng(4);
            if (has_durability()) {
                const int most = std::max(base->durability * 33 / 100, 1);
                item.durability = std::max((*unit_seed)(most >> 1) + (most >> 1), 1);
                item.max_durability = most;
            }
            if (kind == 1) item.defense = std::max(item.defense * 75 / 100, 1);
            staff_mods();
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
                const ModItem on{ &item, base, kind, item_type && item_type->throwable, true };
                for (const auto& mod : row.mods) apply_mod(tables, mod, item.props, rng, &on);   // FUN_0065fec0 kind 1
                staff_mods();
                break;
            }
        }
        if (!made) { rng = Rng(start); item.quality = 2; item.qsub = -1; }
        if (item.quality == 2) staff_mods();
        return finish();
    }
    auto special = [&](const std::vector<Special>& list) {
        auto fits = [&](const Special& row) { return row.enabled && !row.ladder && row.code == code && row.level <= item.ilvl && (row.set != 29 || bovine); };
        int total = 0, pick = -1;
        for (const auto& row : list) if (fits(row)) total += std::max(1, row.rarity);
        if (total == 0) return -1;
        int roll = rng(total);
        for (std::size_t i = 0; i < list.size() && pick < 0; ++i) {
            if (!fits(list[i])) continue;
            if (roll < std::max(1, list[i].rarity)) pick = int(i);
            else roll -= std::max(1, list[i].rarity);
        }
        return pick;
    };
    if (quality == 7 || quality == 5) {
        const auto& list = quality == 7 ? tables.uniques : tables.sets;
        const int picked = special(list);
        int special_row = picked;
        if (quality == 7 && special_row >= 0 && found_uniques && found != tables.item_base.end() && !found->second.quest) {
            if (found_uniques->size() <= std::size_t(special_row)) found_uniques->resize(std::size_t(special_row) + 1);
            if ((*found_uniques)[std::size_t(special_row)]) special_row = -1;
            else if (!list[std::size_t(special_row)].nolimit) (*found_uniques)[std::size_t(special_row)] = true;
        }
        if (special_row >= 0) {
            item.quality = quality;
            (quality == 7 ? item.unique_id : item.set_id) = special_row;
            const ModItem on{ &item, base, kind, item_type && item_type->throwable, false };
            const auto& row = list[std::size_t(special_row)];
            for (const auto& mod : row.mods) apply_mod(tables, mod, item.props, rng, base ? &on : nullptr);
            // A set's bonuses: aprop(i)a / b to list i - 1 (the d2s set lists,
            // on with i + 1 pieces worn), or the item's own with no add func.
            std::array<std::vector<d2d::d2s::ItemProp>, 5> lists;
            for (std::size_t k = 0; k < row.bonus.size(); ++k)
                apply_mod(tables, row.bonus[k], row.bonus_apart ? lists[k / 2 % 5] : item.props, rng, base ? &on : nullptr);
            for (std::size_t i = 0; i < lists.size(); ++i)
                if (!lists[i].empty()) {
                    item.set_lists |= 1 << i;
                    item.set_list_sizes.push_back(lists[i].size());
                    item.set_props.insert(item.set_props.end(), lists[i].begin(), lists[i].end());
                }
            return finish();
        }
        if (quality == 7 && picked < 0 && base && base->only_unique) {    // no row (the Malus, Gidbinn): FUN_005566b0 keeps it unique, id unset
            item.quality = 7;
            return finish();
        }
        if (item.max_durability > 0) {
            const int times = quality == 7 ? 3 : 2;
            item.durability = std::min(item.durability * times, 255);
            item.max_durability = std::min(item.max_durability * times, 255);
        }
        quality = 6;
    }
    const int alvl = affix_level(item.ilvl, qlvl);
    std::vector<int> groups;
    auto add = [&](bool prefix) {
        const auto& list = prefix ? tables.prefixes : tables.suffixes;
        const int affix_row = pick_affix(tables, list, type, alvl, quality == 6, groups, rng);
        if (affix_row <= 0) return 0;
        groups.push_back(list[std::size_t(affix_row)].group);
        for (const auto& mod : list[std::size_t(affix_row)].mods) apply_mod(tables, mod, item.props, rng);
        return affix_row;
    };
    if (quality == 6) {
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
    item.quality = 4;
    const int shape = rng(4);                              // 0 prefix, 1 suffix, 2-3 both
    if (shape != 1) item.prefix = add(true);
    if (shape != 0) item.suffix = add(false);
    return item;                                           // ponytail: magic / rare affixes aren't game.exe's, so no tail (ethereal, class affix)
}

// Whether Charsi's imbue takes `item` (FUN_0062c590): Items bitfield1
// bit 0, not gold, not socketed, not a quest item but Wirt's leg,
// quality low / normal / superior.
// ponytail: throwables (FUN_0062ba80) and the unit flag 0x1000 aren't
// checked.
inline bool imbuable(const Tables& tables, const d2d::d2s::Item& item) {
    const auto found = tables.item_base.find(item.code);
    if (item.code == "gld" || found == tables.item_base.end() || !(found->second.bitfield1 & 1)) return false;
    if (found->second.quest && item.code != "leg") return false;
    return !item.socketed && item.socketed_items.empty() && item.quality >= 1 && item.quality <= 3;
}
// The imbued item (FUN_00579d60, kind 0 at Charsi): the same base made
// anew — rare (+0x30 = 6), item level max(clvl, 1) (+4 past 5,
// FUN_00558200), a fresh seed; ethereal stays so (flag 4 / 2) and a
// personalized name is kept.
inline d2d::d2s::Item imbue_item(const Tables& tables, const d2d::d2s::Item& item, int clvl, Rng& rng) {
    int ilvl = clvl < 2 ? 1 : clvl;
    if (ilvl > 5) ilvl += 4;
    auto made = generate_item(tables, item.code, ilvl, 6, rng);
    made.ethereal = item.ethereal;
    made.personalized = item.personalized;
    made.owner = item.owner;
    return made;
}

// Exceptional / elite upgrade weights out of 10000 for a gamble at clvl
// (FUN_00629370): (clvl - qlvl) * 100 / 2 + 1 and * 100 / 4 + 1.
inline std::pair<int, int> gamble_upgrade(const Tables& tables, const std::string& code, int clvl) {
    const auto found = tables.item_base.find(code);
    if (found == tables.item_base.end()) return { 0, 0 };
    auto weight = [&](const std::string& upgrade_code, int div) {
        const auto upgrade_base = tables.item_base.find(upgrade_code);
        if (upgrade_code.empty() || upgrade_code == code || upgrade_base == tables.item_base.end()) return 0;
        return std::max(0, (clvl - upgrade_base->second.level) * 100 / div + 1);
    };
    return { weight(found->second.ubercode, 2), weight(found->second.ultracode, 4) };
}

// Gheed's price for gambling on `code` (FUN_00629370): rings and amulets
// cost their "gamble cost"; the rest mix the base, exceptional and elite
// costs by upgrade odds and scale with character level.
inline int gamble_price(const Tables& tables, const std::string& code, int clvl) {
    const auto found = tables.item_base.find(code);
    if (found == tables.item_base.end()) return 0;
    const auto& base = found->second;
    if (code == "rin" || code == "amu") return base.gamble_cost;
    const auto [exceptional_chance, elite_chance] = gamble_upgrade(tables, code, clvl);
    auto cost_of = [&](const std::string& upgrade_code) { const auto upgrade_base = tables.item_base.find(upgrade_code); return upgrade_base != tables.item_base.end() ? upgrade_base->second.cost : 0; };
    const long long stack = std::max(1, (base.min_stack + base.max_stack) / 2);
    const int level = std::max(clvl, 5);
    const long long mix = ((10000LL - elite_chance - exceptional_chance) * base.cost * stack + (long long)cost_of(base.ultracode) * elite_chance
                           + (long long)cost_of(base.ubercode) * exceptional_chance) / 10000;
    const long long lvl = ((std::max(base.level - 45, 0) - base.level / 2 + level) * 250) / 3;
    return int((lvl + mix) * ((level * 2 + 1) / 3 + 20) / 15);
}

// Gambles on `code`: the base may upgrade (exceptional / elite, by the
// same odds the price uses), the level is clvl-5..clvl+4, the quality
// unique / set / rare by DifficultyLevels odds per 100000, else magic.
// ponytail: the server's gamble roll isn't traced; those are the table
// odds and the price's upgrade weights.
inline d2d::d2s::Item gamble_item(const Tables& tables, const std::string& code, int clvl, int diff, Rng& rng) {
    std::string chosen_code = code;
    if (const auto found = tables.item_base.find(code); found != tables.item_base.end()) {
        const auto [exceptional_chance, elite_chance] = gamble_upgrade(tables, code, clvl);
        const int roll = rng(10000);
        if (roll < elite_chance) chosen_code = found->second.ultracode;
        else if (roll < elite_chance + exceptional_chance) chosen_code = found->second.ubercode;
    }
    const auto& rates = tables.gamble_rates[std::size_t(std::clamp(diff, 0, 2))];
    const int roll = rng(100000);
    const int quality = roll < rates.unique ? 7 : roll < rates.unique + rates.set ? 5 : roll < rates.unique + rates.set + rates.rare ? 6 : 4;
    return generate_item(tables, chosen_code, clvl - 5 + rng(10), quality, rng);
}

// Gheed's gamble screen: every gamble.txt base up to the character's
// level, packed like a store (armour, weapons, misc).
// ponytail: the game shows a random subset and refreshes it; this shows
// them all and keeps them.
inline Store open_gamble(const Tables& tables, std::string npc_id, int clvl) {
    Store store;
    store.npc_id = std::move(npc_id);
    store.gamble = true;
    for (const auto& code : tables.gamble) {
        const auto found = tables.item_base.find(code);
        if (found == tables.item_base.end() || found->second.level > clvl) continue;
        d2d::d2s::Item item;
        item.code = code;
        item.identified = true;
        store_place(tables, store, store_tab_for(tables, code), std::move(item));
    }
    for (int i = 0; i < 4; ++i) if (!store.tabs[std::size_t(i)].empty()) { store.tab = i; break; }
    return store;
}

// Gambles on stock item i of the open tab: pays the gamble price (carried
// gold, then the stash) and puts the rolled item in the inventory. The
// stock stays. False if it can't be paid for or doesn't fit.
inline bool store_gamble(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items,
                         d2d::d2s::Stats& stats, Rng& rng) {
    const auto& code = store.tabs[std::size_t(store.tab)][std::size_t(index)].code;
    const int clvl = int(stats.get(d2d::d2s::kLevel));
    const int price = gamble_price(tables, code, clvl);
    if (stats.get(d2d::d2s::kGold) + stats.get(d2d::d2s::kGoldBank) < price) return false;
    auto item = gamble_item(tables, code, clvl, store.header.active_difficulty(), rng);
    std::vector<const d2d::d2s::Item*> inv;
    for (const auto& x : items) if (x.location == 0 && x.panel == 1) inv.push_back(&x);
    const auto [width, height] = item_size(tables, item.code);
    const auto [x, y] = free_spot(tables, inv, 10, 4, width, height);
    if (x < 0) return false;
    item.location = 0; item.panel = 1; item.column = x; item.row = y;
    items.push_back(std::move(item));
    const auto from_inv = std::min<std::int64_t>(stats.get(d2d::d2s::kGold), price);
    stats.values[d2d::d2s::kGold] -= from_inv;
    stats.values[d2d::d2s::kGoldBank] -= price - from_inv;
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
inline std::optional<MercOffer> merc_offer(const Tables& tables, bool expansion, int act, int diff, int clvl, Rng& rng) {
    std::vector<const Hireling*> rows;
    for (const auto& hireling : tables.hirelings)
        if (hireling.act == act + 1 && hireling.difficulty == diff + 1 && hireling.version == (expansion ? 100 : 0)
            && (rows.empty() || hireling.level == rows.front()->level))
            rows.push_back(&hireling);
    if (rows.empty()) return std::nullopt;
    const auto& hireling = *rows[std::size_t(rng(int(rows.size())))];
    MercOffer offer;
    offer.id = hireling.id;
    offer.seed = std::uint32_t(rng(0x7fffffff)) | 1;
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
    offer.name = rng(std::max(1, hireling.names));
    return offer;
}

// The save's mercenary (hireling Id, experience) as it fights: its level
// is the highest whose experience ((level + 1) * Exp/Lvl * level^2, as
// hiring sets it) it has, its row the band of that Id at or below that
// level, and life / defence / damage / strength / dexterity grow from the
// row like a hire offer's; attack rating is AR + AR/Lvl per level.
// ponytail: items the merc wears aren't counted.
struct MercStats { int level = 1, life = 40, def = 0, dmg_min = 1, dmg_max = 2, attack_rating = 0; };
inline MercStats merc_stats(const Tables& tables, int id, std::uint32_t exp) {
    MercStats merc;
    const Hireling* row = nullptr;
    for (const auto& hireling : tables.hirelings)
        if (hireling.id == id && (!row || hireling.level < row->level)) row = &hireling;
    if (!row) return merc;
    for (int level = 1; level < 99; ++level)
        if ((long long)(level + 1) * row->exp_per_level * level * level <= (long long)exp) merc.level = level;
    for (const auto& hireling : tables.hirelings)
        if (hireling.id == id && hireling.level <= merc.level && hireling.level > row->level) row = &hireling;
    const auto& hireling = *row;
    const int level_delta = merc.level - hireling.level;
    merc.life = std::max(40, hireling.hit_points + hireling.hp_per_level * level_delta);
    merc.def = std::max(0, hireling.def + hireling.def_per_level * level_delta);
    merc.dmg_min = std::max(0, hireling.dmg_min + (hireling.dmg_per_level * level_delta >> 3));
    merc.dmg_max = std::max(merc.dmg_min + 1, hireling.dmg_max + (hireling.dmg_per_level * level_delta >> 3));
    merc.attack_rating = std::max(1, hireling.attack_rating + hireling.ar_per_level * level_delta);
    return merc;
}

// Hires `o`: pays its cost (carried gold, then the stash) and makes it
// the save's mercenary. False if it can't be paid for.
inline bool hire(const MercOffer& offer, d2d::d2s::Header& header, d2d::d2s::Stats& stats) {
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

// Drinks the potion at the bottom of belt column `col` (box col, 0..3):
// the item goes, those stacked above it (boxes col + 4, + 8, + 12) drop
// a row. Returns its code, "" when there's no potion there.
// A potion drunk from where it's carried: gone, and a belt one's column
// moves down. Returns its code, "" if it isn't a potion.
inline std::string drink_at(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::vector<d2d::d2s::Item>::iterator potion) {
    if (potion == items.end() || !tables.potions.contains(potion->code) || potion->location == 1) return {};
    std::string code = potion->code;
    const int col = potion->location == 2 ? potion->column : -1;
    items.erase(potion);
    if (col >= 0)
        for (int box = col + 4; box < 16; box += 4)
            for (auto& item : items) if (item.location == 2 && item.column == box) item.column = box - 4;
    return code;
}
inline std::string drink_item(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id) {
    return drink_at(tables, items, std::ranges::find(items, id, &d2d::d2s::Item::id));
}
inline std::string drink_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int col) {
    return drink_at(tables, items, std::ranges::find_if(items, [&](const d2d::d2s::Item& item) { return item.location == 2 && item.column == col; }));
}

// Cain's "Identify Items": the carried and worn ones. Returns how many.
// ponytail: the server's scope isn't traced (stash and cube are left).
inline int unidentified(const std::vector<d2d::d2s::Item>& items) {
    return int(std::ranges::count_if(items, [](const auto& item) {
        return !item.identified && (item.location == 1 || item.location == 2 || (item.location == 0 && item.panel == 1));
    }));
}
inline int identify_all(std::vector<d2d::d2s::Item>& items) {
    int count = 0;
    for (auto& item : items)
        if (!item.identified && (item.location == 1 || item.location == 2 || (item.location == 0 && item.panel == 1))) {
            item.identified = true;
            ++count;
        }
    return count;
}

// Pathing over subtiles for a unit that can't stand where blocked(x, y):
// 8-way A* (no cutting corners past a blocked neighbour), at most
// max_nodes expanded. Returns the subtiles from start (exclusive) to the
// goal, or to the reached subtile nearest the goal when it can't be
// reached (clicking a wall walks up to it); empty when already there.
// ponytail: stands in for D2's pathing (Path.cpp), which isn't traced.
template <class Blocked>
std::vector<std::pair<int, int>> find_path(int start_x, int start_y, int goal_x, int goal_y, Blocked&& blocked, int max_nodes = 6000) {
    using P = std::pair<int, int>;
    auto key = [](int x, int y) { return std::uint64_t(std::uint32_t(x)) << 32 | std::uint32_t(y); };
    auto heuristic = [&](int x, int y) {
        const int dx = std::abs(x - goal_x), dy = std::abs(y - goal_y);
        return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
    };
    std::unordered_map<std::uint64_t, std::pair<int, P>> seen;     // g, parent
    std::priority_queue<std::tuple<int, int, int, int>, std::vector<std::tuple<int, int, int, int>>, std::greater<>> open;
    seen[key(start_x, start_y)] = { 0, { start_x, start_y } };
    open.push({ heuristic(start_x, start_y), 0, start_x, start_y });
    P best{ start_x, start_y };
    int best_h = heuristic(start_x, start_y), expanded = 0;
    while (!open.empty() && expanded < max_nodes) {
        const auto [estimate, cost, x, y] = open.top();
        open.pop();
        if (cost > seen[key(x, y)].first) continue;
        ++expanded;
        if (heuristic(x, y) < best_h) { best_h = heuristic(x, y); best = { x, y }; }
        if (x == goal_x && y == goal_y) break;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int next_x = x + dx, next_y = y + dy;
                if (blocked(next_x, next_y) || (dx && dy && (blocked(x + dx, y) || blocked(x, y + dy)))) continue;
                const int next_cost = cost + (dx && dy ? 14 : 10);
                const auto next_key = key(next_x, next_y);
                if (const auto found = seen.find(next_key); found != seen.end() && found->second.first <= next_cost) continue;
                seen[next_key] = { next_cost, { x, y } };
                open.push({ next_cost + heuristic(next_x, next_y), next_cost, next_x, next_y });
            }
    }
    std::vector<P> path;
    for (P step = best; step != P{ start_x, start_y }; step = seen[key(step.first, step.second)].second) path.push_back(step);
    std::ranges::reverse(path);
    return path;
}

}  // namespace d2d::rules
