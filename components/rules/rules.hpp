// SPDX-License-Identifier: GPL-3.0-or-later
// Game rules that don't need assets or a screen: item sizes and grid
// placement, vendor stock, prices, buying and selling. Everything reads
// the excel-derived Tables (filled by d2d's loader, or by hand in tests).
#pragma once

#include "monster_ids.hpp"

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
    std::string use_sound;             // usesound, played when the item is picked up, put down or worn
    int range_adder = 0;               // weapons.txt rangeadder (the item record's +0x104 byte, FUN_006288d0)
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
    bool autostack = false;            // AutoStack: a pickup joins the inventory's stacks of it (keys)
    bool throwable = false;            // Throwable (the item's own type, FUN_0062ba80)
    bool reload = false, reequip = false;   // Reload / ReEquip (+0x11 / +0x12, FUN_0062e7e0 / FUN_0062e830): a spent stack's slot (rules::spent_fate)
    bool repair = false;               // Repair (+0x08): a smith may repair it (FUN_0062e660)
    std::string shoots, quiver;        // Shoots / Quiver: a bow's ammunition, a quiver's bow (+0xc / +0xe, FUN_0062e6f0 / FUN_0062e740)
    std::array<int, 3> max_sock{};     // MaxSock1 / 25 / 40: by ilvl <= 25, <= 40, above
    int staff_mods = 7;                // StaffMods class (ama .. ass = 0 .. 6), 7 none
    int var_inv_gfx = 0;               // VarInvGfx: pictures to pick from
    int store_page = -1;               // StorePage: armo 0, weap 1 (spilling to 2), misc 3; -1 never stocked
};
// TreasureClassEx.txt row: picks, the NoDrop weight, quality modifiers
// (Unique, Set, Rare, Magic, in 1024ths off the odds) and the weighted
// entries (item codes, other classes, "gld" or "gld,mul=N"; Prob >= 1).
// group / level: a monster's level moves it on to the group's next rows
// (FUN_00654e00); `next` is that row's name ("" past the group's last).
// entry_flags / entry_index, parallel to items (empty: all 0): entry +0xc
// (1 unique item, 2 set item, 4 a class) and +0xa (the unique / set index).
// flag_mod: the two flag chances (bin +0x30 / +0x32). The txt leaves them 0.
struct TreasureClass {
    int picks = 1, nodrop = 0;
    std::array<int, 4> mod{};
    std::vector<std::pair<std::string, int>> items;
    int group = 0, level = 0;
    std::string next;
    std::vector<int> entry_flags, entry_index;
    std::array<int, 2> flag_mod{};
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
    bool autobelt = false;                             // misc.txt autobelt (Items +0x131): a pickup takes a free belt column
    bool multibuy = false;                             // misc.txt multibuy (Items +0x1a5): shift + right click fills (FUN_004b2650)
    int gem_sockets = 0, auto_prefix = 0, magic_lvl = 0;  // gemsockets (+0x138), auto prefix (+0xf8), magic lvl (+0x140)
    bool has_inv = false, no_durability = false;          // hasinv (+0x137), nodurability (+0x113)
    bool useable = false;                                 // useable (+0x11d, FUN_00628c20): a right click uses it
    bool quest_diff = false;                              // questdiffcheck (+0x12b): stat 356 = the difficulty made in
    int max_1h = 0, max_2h = 0;                           // maxdam (+0xff, armor.txt's too) / weapons.txt 2handmaxdam (+0x103)
    int min_1h = 0, min_2h = 0;                           // mindam (+0xfe, armor.txt's too) / weapons.txt 2handmindam (+0x102)
    std::string nightmare_upgrade, hell_upgrade;       // NightmareUpgrade / HellUpgrade ("xxx" none): a store's base in those difficulties
    bool spawnable = false;                            // spawnable (+0x133)
    bool nameable = false;                             // nameable (+0x145): Anya may personalize it
    int version = 0;                                   // version (+0xf6): 100 an expansion item
    std::string type2;                                 // type2 (+0x120)
    int hit_class = 0;                                 // weapons.txt "hit class" (sound_ids::hit_class): its swing and impact sounds
    std::string wclass;                                // weapons.txt wclass (the item record's +0xc0 class key): attack_start_frame's row
    int min_misdam = 0, max_misdam = 0;                // weapons.txt minmisdam / maxmisdam: a thrown one's (stats 159 / 160)
};
// Prices (FUN_0062efb0, docs/research/re/store.md): npc.txt by MonStats Id.
struct NpcPrice { int buy = 1024, sell = 1024, rep = 1024; std::array<int, 3> qflag{}, qbuy{}, qsell{}, qrep{}, max_buy{}; };
// level (Items +0xfd) and version (+0xf6: 100 an expansion item), magic:
// bitfield1 bit 0 (FUN_00629c80), what a vendor may sell magic.
struct VendorItem { std::string code; int min = 0, max = 0, magic_min = 0, magic_max = 0, magic_lvl = 0; bool perm = false;
                    int level = 0, version = 0; bool magic = false; };

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
    int class_specific = -1;                    // classspecific (ama .. ass = 0 .. 6), -1 any (row +0x66)
};
// UniqueItems / SetItems row (without separators, as the save's IDs).
// UniqueItems / SetItems row: ladder (flag 8) never drops outside a ladder
// game, nolimit (flag 2) isn't one per game; set: its Sets.txt row.
struct Special {
    std::string code; int level = 0, rarity = 1; bool enabled = true; std::vector<Mod> mods; bool ladder = false, nolimit = false; int set = -1;
    std::string index;                                     // its "index" name (cubemain's names, FUN_006bd3c0)
    std::vector<Mod> bonus;                                // a set's aprop1a..aprop5b, all 10 (unused: no code)
    bool bonus_apart = false;                              // add func: the bonuses go to their own lists (else the item's)
    int add_func = 0;                                      // add func: 1 a list per other member worn, 2 by the count
    int member = -1;                                       // its place in its set, in row order (+0x2e, FUN_00634dc0)
};
// Properties.txt: per code the funcs that turn a mod into stats.
struct PropFunc { int func = 0, stat = -1, val = 0; };
// Sets.txt row: its partial bonuses PCode2a, 2b, 3a .. 5b (8, code empty
// when unused), its full bonuses FCode1..8, and how many SetItems rows
// belong to it (the record's +0xc).
struct SetBonus { std::vector<Mod> partial, full; int pieces = 0; };
// DifficultyLevels gamble odds: rare / set / unique per 100000 (+0x44 ..
// +0x4c), the upgrades' weight per level over the upgrade's (of 10000).
struct GambleRates { int rare = 10000, set = 100, unique = 50, uber = 0, ultra = 0; };   // + GambleUber / GambleUltra (+0x50 / +0x54)

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
    int resist = 0, resist_per_level = 0, default_chance = 0;   // Resist, Resist/Lvl, DefaultChance (+0x5c, +0x60, +0x64)
    // Skill1..6 (+0x78), Mode (+0xc0), Chance (+0x90), ChancePerLvl (+0xa8),
    // Level (+0xc6), LvlPerLvl (+0xcc): the merc's skills and how it picks
    // them (rules::merc_skill_levels, merc_skill_pick).
    struct Skill { std::string name; int mode = 0, chance = 0, chance_per_level = 0, level = 0, level_per_level = 0; };
    std::array<Skill, 6> skills{};
};

struct Tables {
    std::unordered_map<std::string, ItemType> types;       // by type code
    // gems.txt socket bonuses by gem/rune code, per slot kind (weapon,
    // helm/armour, shield), already resolved through Properties.txt.
    std::unordered_map<std::string, std::array<std::vector<d2d::d2s::ItemProp>, 3>> gem_props;
    std::unordered_map<std::string, ItemInfo> item_info;   // by item code
    std::unordered_map<std::string, ItemBase> item_base;   // by item code
    std::unordered_map<std::string, NpcPrice> npc_prices;  // by MonStats Id
    // The (multiply, add) cost pairs of affixes (raw rows, like the
    // names), uniques and set items; AutoMagic's by row (class_affix - 1:
    // all 0 in 1.14d's AutoMagic.txt).
    std::vector<std::pair<int, int>> prefix_cost, suffix_cost, unique_cost, set_cost, automagic_cost;
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
    // Runes.txt rows (FUN_006394a0, 0x120 bytes each), in file order.
    struct Runeword {
        std::string name;                                  // "RunewordN"
        bool complete = false, ladder = false;             // +0x80, +0x81 (the "server" column)
        std::vector<std::string> itypes, etypes, runes;    // +0x86 itype1..6, +0x92 etype1..3, +0x98 Rune1..6 (item codes)
        std::vector<Mod> mods;                             // +0xb0 T1Code1..7
        int save_id = -1;                                  // the save's 12 bits (item_names.runeword's rank + 27; Delirium 2718)
    };
    std::vector<Runeword> runewords;
    // QualityItems rows (superior): which bases take it (armor, weapon,
    // shield, scepter, wand, staff, bow, boots, gloves, belt: the record's
    // flags, FUN_0065e7d0) and its mods.
    struct Superior { std::array<bool, 10> fits{}; std::vector<Mod> mods; };
    std::vector<Superior> superior;
    std::vector<std::string> skill_itype;                  // Skills.txt itypea1 by Id (staffmods)
    std::vector<std::pair<int, int>> skill_levels;         // Skills.txt reqlevel, maxlvl by Id (FUN_00644710 / FUN_004aa8b0)
    // books.txt CostPerCharge by BookSpellCode (tbk, ibk): what each scroll in a tome adds to its price (FUN_0062efb0).
    std::unordered_map<std::string, int> book_charge_cost;
    // books.txt ScrollSpellCode, BookSpellCode by row (record +0x18 / +0x1c, FUN_005c2540).
    std::vector<std::pair<std::string, std::string>> books;
    std::vector<std::pair<int, int>> skill_costs;          // Skills.txt cost mult, cost add by Id (+0x234 / +0x238: a charge's repair, FUN_00628d30)
    // ItemStatCost Multiply, Add, Encode by ID (+0x10, +0x14, +0x30): what a magic stat adds to a price (FUN_00628e70).
    struct StatCost { int mul = 0, add = 0, encode = 0; };
    std::vector<StatCost> stat_cost;
    std::array<int, 7> class_first_skill{};                // per class its first skill Id (FUN_006460f0)
    std::vector<Special> uniques, sets;
    std::vector<std::string> item_order;                  // item codes in the Items table's order (weapons, armor, misc)
    std::vector<SetBonus> set_bonuses;                    // by Sets.txt row
    std::unordered_map<std::string, std::vector<PropFunc>> properties;
    std::unordered_map<std::string, int> skill_id;         // Skills.txt skill name -> Id
    int rare_prefixes = 0, rare_suffixes = 0;              // RarePrefix / RareSuffix rows
    // RarePrefix / RareSuffix itype1..7 and etype1..4 by row (FUN_0065e710: a name fits an item of one itype, none of its etypes).
    struct RareName { std::vector<std::string> itypes, etypes; };
    std::vector<RareName> rare_prefix_names, rare_suffix_names;
    std::vector<std::string> gamble;                       // gamble.txt codes
    std::array<GambleRates, 3> gamble_rates{};
    std::vector<Hireling> hirelings;                       // hireling.txt rows
    // Drops: TreasureClassEx by name (plus the auto weapN / armoN classes)
    // and ItemRatio.
    std::unordered_map<std::string, TreasureClass> treasure;
    std::array<std::array<QualityRatio, 6>, 4> quality_ratio{};
    // misc.txt potions: life / mana restored (hpregen / manarecovery: that
    // much over `ticks`; hitpoints / mana on a rejuvenation: percent, at once;
    // stamina: staminarecoverybonus % for `ticks`).
    // An antidote's / thawing's state (pSpell 6): its stats (ItemStatCost
    // id, value) for `ticks` frames.
    struct Potion { int life = 0, mana = 0, ticks = 0; bool percent = false, antidote = false, thawing = false; int stamina = 0;
                    std::vector<std::pair<int, int>> state_stats; };
    std::unordered_map<std::string, Potion> potions;
};

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

// FUN_0062efb0's last step for a buy, a gamble or a repair: the player's
// reduced prices (stat 87, at most 99) off, at least 1.
long long price_reduced(long long price, int reduced);

std::pair<int, int> item_size(const Tables& tables, const std::string& code);

// Where an inventory grid takes an item it's given: a w x h spot in the cols x rows grid of placed items, wholly
// free.
std::pair<int, int> free_spot(const Tables& tables, const std::vector<const d2d::d2s::Item*>& placed,
                              int cols, int rows, int width, int height, bool players);

// Puts an item into the store grid from tab on (weapons spill 1 -> 2).
bool store_place(const Tables& tables, Store& store, int tab, d2d::d2s::Item item);

int store_tab_for(const Tables& tables, const std::string& code);

// MonStats hcIdx -> game.exe vendor index, -1 if not a vendor.
int vendor_index(int hc_idx);

bool is_repair_vendor(int hc_idx);


bool type_is(const Tables& tables, const std::string& type, std::string_view want, int depth = 0);
int max_durability(const d2d::d2s::Item& item);
bool indestructible(const Tables& tables, const d2d::d2s::Item& item);

// The price base of an armor: cost x its own defence (stat 31, not the stat list's) / maxac, for buy, sell and
// repair alike; any other item keeps its cost.
int armor_scaled(const Tables& tables, const d2d::d2s::Item& item, int cost);

// game.exe's `mul * price * value / 1024 + add` in 32 bits: above 0xffff (and a
// mul) the price * value is divided first. `limit` is the buy side's price *
// value, which picks the branch for the sale and the repair too; `shift` is 10,
// or 12 for a sale's skill terms.
int price_term(int mul, int add, int price, int value, int limit, int shift);

// What an item's magic stats add to a price (FUN_00628e70): each stat of the
// item's full list less its base one, summed by stat and param, costs the
// ItemStatCost Multiply / Add on `price` x value (a sale's too). A skill stat
// takes the skill's Skills.txt cost mult / add instead: Encode 1 (the param is
// the skill) by the value, Encode 2 / 3 (skill << 6 | level: on-hit, charges) by
// the level, a sale by / 4096. Encode 4 (by time) is mult 0 and no add in 1.14d.
// `buy` is the buy side's price, which picks the branch. The save's lists (the
// main one, a runeword's after it) stand for the magic lists. The list is the
// full one (FUN_00625c30: the stat list's +0x48; FUN_00625560: FUN_00625420
// full less FUN_00625350 base), what the sockets hang on it included
// (socket_props), as stat 91 is read (FUN_00625500 -> FUN_00625420).
std::vector<d2d::d2s::ItemProp> socket_props(const Tables& tables, const d2d::d2s::Item& parent, const d2d::d2s::Item& filled);
int stat_costs(const Tables& tables, const d2d::d2s::Item& item, int buy, int price, bool sell);

// What an item type's staff mods add.
// `buy` picks the branch.
int staff_mod_costs(const Tables& tables, const d2d::d2s::Item& item, int buy, int price, bool sell);

// A stack's most (FUN_006295b0): Items.txt maxstack plus item_extra_stack
// (stat 0xfe), at most 0x1ff.
inline constexpr int kExtraStack = 0xfe, kReplenishQuantity = 0xfd, kReplenishDurability = 0xfc;
int stack_most(const Tables& tables, const d2d::d2s::Item& item);
// A tome (ItemTypes book) or a quiver (bowq / xboq): FUN_0062efb0 prices
// them by their quantity up front, not as a stack.
bool priced_by_contents(const Tables& tables, const d2d::d2s::Item& item);
// A stack's share of its extras.
bool priced_as_stack(const Tables& tables, const d2d::d2s::Item& item);
int stack_divisor(const Tables& tables, const d2d::d2s::Item& item);
int unsigned_share(int total, int divisor);
// What a stack lacks of its most (0062fc97): none when it's full or it replenishes itself.
int stack_missing(const Tables& tables, const d2d::d2s::Item& item);
// The repair channel (FUN_0062efb0's local_c) after the NPC's rep mults,
// before a stack's missing count and the charges: `repair_cost`. A sale
// of a restocking stack takes it off (0062fcb6) without the worn share.
long long repair_each(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, bool worn_share);
bool repairable(const Tables& tables, const d2d::d2s::Item& item);

// Buy (sell = false) or sell price of an item at the NPC with MonStats Id
// npc_id, for the player's header (difficulty, quest flags): FUN_0062efb0
// (D2Common's transaction cost). A buy, gamble or repair then takes the
// player's reduced prices (stat 87) off (price_reduced).
int item_price(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, bool sell,
               const d2d::d2s::Header& header, int reduced = 0);

// An item's max durability with its modifiers: the save's base, plus
// flat maxdurability (stat 73), times item_maxdurability_percent (75).
int max_durability(const d2d::d2s::Item& item);
// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour). game.exe hangs the socketed item's stat list on the
// parent's (FUN_00626e10), so these count as the parent's own.
std::vector<d2d::d2s::ItemProp> socket_props(const Tables& tables, const d2d::d2s::Item& parent, const d2d::d2s::Item& filled);
// item_indesctructible (stat 152, Zod's too): never wears, never needs repair.
bool indestructible(const Tables& tables, const d2d::d2s::Item& item);

// A broken item (item flag 0x100, set by FUN_0055f850): its stats and
// defense are off until a repair (FUN_0055f900) clears it.
// Item flag 0x200, "repaired": FUN_0055f900 sets it as it clears 0x100. Nothing
// in game.exe's rules reads it back (the one reader, FUN_0053d440's caller in
// InvMode.cpp, tells a client); the item's save writer keeps it (FUN_006312b0).
inline constexpr std::uint32_t kItemRepaired = 0x200;
inline bool broken(const d2d::d2s::Item& item) { return (item.flags & 0x100) != 0; }
void wield_enters(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);
// A stack a repair refills: stackable and throwable — every stacking weapon.
bool restocks(const Tables& tables, const d2d::d2s::Item& item);

// What an item's missing charges add to its repair.
int charges_repair_cost(const Tables& tables, const d2d::d2s::Item& item);
// A repair's recharge: each stat 204 below its max back to max.
bool recharge(d2d::d2s::Item& item);

// A charged skill (stat 204) below its max: FUN_0062a840's second answer 0.
bool charge_missing(const d2d::d2s::Item& item);
// FUN_00629930: an item with durability (nodurability 0, a durability
// column, max != 0, not indestructible).
bool has_durability(const Tables& tables, const d2d::d2s::Item& item);
// The item's ItemTypes row has Repair.
bool type_mends(const Tables& tables, const d2d::d2s::Item& item);
// What a repair may price (FUN_0062e660): identified, not ethereal, and a
// charge missing, or a type with Repair that restocks or has durability.
// A whole item passes: worn or not isn't asked here.
bool repairable(const Tables& tables, const d2d::d2s::Item& item);
// What Repair All counts.
bool needs_repair(const Tables& tables, const d2d::d2s::Item& item);

// Repair cost at the NPC (FUN_0062efb0 mode 3): the buy base with its
// quality extras, times missing / max durability, times npc.txt rep mult
// and the quest rep mults / 1024, then the missing charges'
// (charges_repair_cost, after the mults). An item `repairable` refuses
// (FUN_0062e660 at 0062efee) costs 0. A stack (stackable, not a tome or
// quiver) pays a unit's repair, after the mults, times what it lacks of its
// most (`stack_missing`; its durability free as a throwable's). In
// vanilla data every base of a type without Repair also has nodurability
// 1, so the Repair test is redundant there and kept for fidelity (mods).
// With charges missing the gate passes and the base is scaled by the
// durability as FUN_0062efb0 does (0062f9e7): an item without durability
// pays its whole base, a durable one its missing share. A whole item that
// passes the gate costs 0 before the minimum, so 1 (0062fd5c): Repair All
// skips it by `needs_repair`, not by its price. What its sockets hold adds
// half of each one's cost (FUN_006292f0 at 0062f86c, the repair total too),
// after the staff mods and before the missing share and the mults.
long long repair_each(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, bool worn_share);
int repair_cost(const Tables& tables, const d2d::d2s::Item& item, const std::string& npc_id, const d2d::d2s::Header& header, int reduced = 0);

// Paying (FUN_00576d90): the purse first, then the stash; nothing taken
// and false when both together fall short. A payment only lowers a
// purse within its cap, so only a growing one can pass it.
bool pay(d2d::d2s::Stats& stats, std::int64_t cost);

// Mends an item: only one its gate passes.
bool mend(const Tables& tables, d2d::d2s::Item& item);

// Repairs item i (gold first from the inventory, then the stash, as a buy).
bool store_repair(const Tables& tables, const Store& store, d2d::d2s::Item& item, d2d::d2s::Stats& stats);

// An item Repair All takes: a worn one, the 13 body slots, never the inventory grid, that `needs_repair`.
bool repair_all_takes(const Tables& tables, const d2d::d2s::Item& item);

// What repair all would cost: each item it takes, at its repair price.
int repair_all_cost(const Tables& tables, const Store& store, const std::vector<d2d::d2s::Item>& items);
// Whether a death makes a corpse (FUN_0057f700): the live corpse count
// (FUN_0063d5f0) against 0xf, unsigned (jbe at 0x57f730). Over 15,
// FUN_0057f540 drops the gear instead and there is no corpse.
// tools/emu/corpse_cap.py.
constexpr bool death_makes_corpse(int corpses) { return unsigned(corpses) <= 15; }
// Which corpse a save keeps.
int corpse_to_save(const Tables& tables, const std::vector<const std::vector<d2d::d2s::Item>*>& corpses, const std::string& npc_id,
                   const d2d::d2s::Header& header, int reduced);
// Repair all: the sum is paid at once.
std::optional<int> store_repair_all(const Tables& tables, const Store& store, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats);

// A scroll's tome (Books.txt pairs tsc / tbk, isc / ibk) for `scroll`: true when `book` is one with room.
bool tome_takes(const Tables& tables, const d2d::d2s::Item& scroll, const d2d::d2s::Item& book);
// A buy's NPC transaction result (S->C 0x2a +2, FUN_00577830 -> FUN_0053d740):
// done, or the client's box (FUN_004b53f0: 9 string 0xd21 "I cannot complete
// that request.", 10 string 0xd1c "You do not have enough free space to do
// that.", 12 string 0xd22 "You do not have enough gold for that.").
enum class Trade : std::uint8_t { done = 0, hireling = 5, refused = 9, no_room = 10, no_gold = 12 };   // S->C 0x2a's result (5: a merc back)
int belt_box(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, int boxes);
bool stacks_with(const d2d::d2s::Item& one, const d2d::d2s::Item& other);
// The vendor's perm items: its PermStoreItems, and in an expansion game greater and super potions.
bool store_perm(const Store& store, const std::string& code);
// Buys stock item i of the open tab (C->S 0x32, FUN_00577830): gold down
// by the price (more than carried + stash gold: no_gold), carried gold
// first, then the stash (FUN_00576d90); the item leaves the stock unless
// it's a perm one (FUN_00576f50 asks FUN_00576ed0: store_perm). A scroll goes into a carried tome with room
// (FUN_0055f640). Anything else is a copy (FUN_0055a2a0), stack and all,
// offered to the belt first (FUN_0055e9b0 -> FUN_0063c790: belt_box,
// `boxes` the worn belt's, 4 with none), else the inventory's first free
// spot (FUN_00560200; nowhere for it: no_room, nothing paid).
// `fill` (the packet's 0x80000000: shift + right click on a multibuy item,
// FUN_004b2650) counts for a perm item only (FUN_00576ed0):
// - a scroll fills the tome: as many as the gold buys (local_14 / local_8),
//   at most its room;
// - an AutoStack type (keys; FUN_0062e790, its own ItemTypes row +0x13)
//   priced one at a time (stat 0x46 set to 1) tops up the first carried
//   stack of it with room, worn then the backpack (FUN_00577700:
//   FUN_0063c2f0, FUN_0063c200), with as many as the gold buys (none:
//   no_gold, 0x89a); with none carried, it buys one stack of that many,
//   at most its most;
// - anything else is bought again while it goes to the belt (every row,
//   FUN_0055e9b0): the first copy that can't is dropped unpaid and ends it
//   (0x577d9c -> LAB_00577e68: the gold put back, nothing said) unless a
//   body slot takes it; only the first copy goes to the inventory. Gold
//   running out after one is bought says no_gold (0x8d4).
// Given a wearer, an item bound for the inventory is first offered to a body
// slot (0x577d10 .. FUN_00562e00 -> FUN_0055d710 mode 0: auto_equip_slot,
// `required_level` the item's), unless a weapon is in hand (FUN_0063bef0,
// either arm, ItemTypes 0x2d) and the item is a weapon too. Bolts (cqv,
// 0x883e9c) go on only by a crossbow in hand (its wclass xbw, FUN_00623c60
// 7), arrows (aqv, 0x883ea0) only by a bow (bow, 1), neither with nothing in
// hand: in vanilla data every bow's wclass is bow and every crossbow's xbw,
// so auto_equip_slot's own test (the hand's weapon shoots the quiver's type)
// is the same one.
struct Wearer;
int auto_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                           int required_level);
Trade store_buy(const Tables& tables, Store& store, int index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats, int boxes = 4,
                bool fill = false, const Wearer* wearer = nullptr, int required_level = -1);

// Sells item i: gold up by the sell value, what passes the carried cap (clvl x 10000) returned to drop at the
// player's feet.
std::int64_t store_sell(const Tables& tables, Store& store, std::size_t index, std::vector<d2d::d2s::Item>& items, d2d::d2s::Stats& stats);

// Is type t (or one of its Equiv ancestors) the type want?
bool type_is(const Tables& tables, const std::string& type, std::string_view want, int depth);

// BodyLocs.txt codes -> body slot (d2s slot numbering: 1 head .. 10 gloves).
int body_slot(std::string_view code);

// The class code (ama, sor, nec, pal, bar, dru, ass) an item type is
// restricted to, "" for anyone: ItemTypes Class along the Equiv chain.
std::string type_class(const Tables& tables, const std::string& type, int depth = 0);

inline constexpr std::string_view kClassCode[7] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };   // by d2s class

// What the player brings to an equip check: d2s class and the current
// strength, dexterity and level. `holy_shield` is the effective Holy
// Shield level while state 0x65 is on (FUN_00485be0 / FUN_00485d40), else 0.
struct Wearer { int cls = 0, str = 0, dex = 0, lvl = 1, holy_shield = 0; };

const ItemInfo* info_of(const Tables& tables, const d2d::d2s::Item& item);

// game.exe's value x times / over, truncated.
int scale(int value, int times, int over);

// FUN_0062eaf0's strength, dexterity and level: the base's ReqStr / ReqDex
// each + itself x the item's stat 91 ("requirements -x%") / 100
// (FUN_00483360), 10 less on an ethereal item; a wearer with no strength
// or no dexterity meets neither.
bool meets_stats(const ItemInfo& info, const d2d::d2s::Item& item, int str, int dex, int lvl, int required_level = -1,
                 const Tables* tables = nullptr);

// The class, stat and level requirements alone.
bool meets_requirements(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer, int required_level = -1);

// Can `it` be worn in body slot `slot` by `w` at all, ignoring what's already equipped?
bool can_wear(const Tables& tables, const d2d::d2s::Item& item, int slot, const Wearer& wearer, int required_level = -1);

// A two-handed weapon fills both hands; only a bow's own quiver joins it (hands_ok).
bool blocks_other_hand(const Tables& tables, const d2d::d2s::Item& item, const Wearer& wearer);

// Can a and b be held in the two hands together?
bool hands_ok(const Tables& tables, const d2d::d2s::Item& first, const d2d::d2s::Item& second, const Wearer& wearer);

// A body slot's other side: the arms 4 <-> 5, the rings 6 <-> 7, the switch arms 11 <-> 12; 0 for the rest.
int other_side(int slot);
// Where a corpse's item from `slot` goes: `taken` the player's slot, `other_taken` its other side's.
int corpse_slot(int slot, bool taken, bool other_taken);

// The item cursor.

// Put `held` into a stored grid (location 0; panel 1 inventory, 4 cube,
// 5 stash) with its top-left cell at (col, row).
bool put_in_grid(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                 int panel, int cols, int rows, int col, int row);

// A hand's stack `held` joins: its index in `items`, -1 none.
int hand_stack(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& held, int slot);
// The cursor's stack onto `target`.
void stack_onto(const Tables& tables, d2d::d2s::Item& target, std::optional<d2d::d2s::Item>& held);

// The wielded weapon (weapon-switch.md "The wielded weapon"): the
// inventory keeps one item's id at +0x1c, here that item's `wielded`.
// Usable (FUN_0062a4e0): not broken (flag 0x100) nor flagged 0x4000.
inline bool item_usable(const d2d::d2s::Item& item) { return (item.flags & 0x4100u) == 0; }
bool in_a_hand(const d2d::d2s::Item& item);
bool weapon_item(const Tables& tables, const d2d::d2s::Item& item);
// The item in hand `slot`, if any.
int hand_index(const std::vector<d2d::d2s::Item>& items, int slot);
// FUN_0063bef0: the left hand's item (cell 5), then the right's (cell 4),
// if it's a weapon (ItemTypes weap) and the wielded one; usable or not.
const d2d::d2s::Item* wielded_weapon(const Tables& tables, const std::vector<d2d::d2s::Item>& items);
void wield(std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);
// FUN_0063d1d0: weapon `item` is now in a hand (one of `items`). Usable, it
// becomes the wielded one when no weapon is (FUN_0063bef0 says none) or
// the wielded one is a throwing potion (ItemTypes tpot); unusable, it isn't.
// Every hand exit clears the leaving weapon's mark (wield_leaves), so the
// wielded one found here is never `item` itself.
void wield_enters(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item);
// FUN_0063d2b0: weapon `leaving`, still in its hand, comes off. It stops
// being the wielded one, and the other hand's item, if usable (any item,
// not only a weapon), becomes it.
void wield_leaves(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& leaving);
// A loaded character's hands.
int wield_settle(const Tables& tables, std::vector<d2d::d2s::Item>& items);
// FUN_0055db00: worn `item` (one of `items`) meets its requirements:
// FUN_0062eaf0's (identified, class, strength, dexterity, level), and a
// quiver (its ItemTypes Quiver, +0xe: FUN_0062e740) only with an item of
// its bow's type (FUN_00629bb0) in the right hand, or the left when the
// quiver is the right hand's.
bool requirements_hold(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                       int required_level = -1);
// An inventory charm, as FUN_0062ff70 has it before its requirements: an
// item of ItemType 0xd (char) on page 0 (item data +0x45), not broken
// (0x100) nor flagged 0x4000.
bool inventory_charm(const Tables& tables, const d2d::d2s::Item& item);
// FUN_0055dbc0 (ItemMode.cpp). `wearer()` is the strength, dexterity and
// level with the gear's stats on, which a flagged item's no longer are;
// `level(item)` its required level.
// - Pass 1, the inventory's charms (0x55dc07..0x55dc7b): one that passes
//   FUN_0062ff70 (an inventory charm meeting FUN_0062eaf0's requirements)
//   has its stats put on (FUN_0055d970 -> FUN_0063d1d0). One short of them
//   gets nothing; game.exe flags no charm, so `charm_short` (not saved)
//   says it. No caller of FUN_0055dbc0 is a level up, so a charm reached
//   by levelling counts from the next item move.
// - The worn passes (body slots 1..10; the weapon switch's 11 / 12 aren't
//   walked). First, in slot order, an unbroken unflagged item that fails
//   gets flag 0x4000 (its stats off, FUN_0055c730) and FUN_0063d2b0.
//   Then, repeated while one changed (one's stats can let another pass), an
//   unbroken flagged item that meets them loses 0x4000 and gets
//   FUN_0063d1d0 (its stats on, FUN_0055c2c0). A flagged item's own stats
//   don't count toward its own requirements; an unflagged one's do.
template <class WearerOf, class LevelOf>
inline void settle_requirements(const Tables& tables, std::vector<d2d::d2s::Item>& items, WearerOf&& wearer, LevelOf&& level) {
    const auto worn = [](const d2d::d2s::Item& item) {
        return item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst
            && item.slot <= d2d::d2s::body_location::kLast;
    };
    for (auto& item : items) item.charm_short = inventory_charm(tables, item) && !requirements_hold(tables, items, item, wearer(), level(item));
    for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot)
        for (auto& item : items)
            if (worn(item) && item.slot == slot && item_usable(item) && !requirements_hold(tables, items, item, wearer(), level(item))) {
                item.flags |= 0x4000u;
                wield_leaves(tables, items, item);
            }
    for (bool changed = true; changed;) {
        changed = false;
        for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot)
            for (auto& item : items)
                if (worn(item) && item.slot == slot && !broken(item) && (item.flags & 0x4000u) && requirements_hold(tables, items, item, wearer(), level(item))) {
                    item.flags &= ~0x4000u;
                    wield_enters(tables, items, item);
                    changed = true;
                }
    }
}
// The item Throw takes (FUN_0056f460; the gate FUN_00647640 for skill 2):
// the first usable weapon in hand (FUN_0063c9b0: the right hand's, then
// the left's); when that isn't the wielded one (FUN_0063bef0), the other
// hand's item (FUN_00643d60), whatever it is. -1 none. `wielded` says
// whether the first was the wielded one.
int throw_hand(const Tables& tables, const std::vector<d2d::d2s::Item>& items, bool* wielded = nullptr);

// Equip `held` in body slot `slot` (1..10), swapping out what's there; a
// stack onto its kind in a hand joins it (hand_stack, stack_onto).
bool equip(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
           int slot, const Wearer& wearer, int required_level = -1, int cols = 0, int rows = 0, int* kept_weapon = nullptr);

// A bag item worn in one step. The hand must be empty. Any refusal leaves
// `items` and `held` as they were.
int bag_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                   int id, const Wearer& wearer, int required_level = -1);

// `kept_weapon` as equip's: the ToBody it stands for (deviations.md 18) is code 1 or 5.
bool equip_from_bag(const Tables& tables, std::vector<d2d::d2s::Item>& items, const std::optional<d2d::d2s::Item>& held,
                    int id, const Wearer& wearer, int required_level, int cols, int rows, int* kept_weapon = nullptr);

// An empty hand clicked with nothing held, beside a two-hander.
int two_hander_beside(const Tables& tables, const std::vector<d2d::d2s::Item>& items, int slot);

// Put `held` in belt box `box` (0..boxes-1): potions, scrolls — ItemTypes
// Beltable along the Equiv chain. A smaller belt's lost rows: spill_belt.
bool put_in_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held,
                 int box, int boxes);

// A belt put on or taken off (FUN_005608c0, from the swap FUN_00560f00 and
// body -> cursor FUN_00560cd0; none worn: Belts.txt row 2, one row): each
// item in a box the belt now worn lacks, `boxes` up to 15 in order, goes to
// the inventory (FUN_0063b950: free_spot, a player's best fit), else it's
// returned, for the caller to drop at the player's feet (FUN_00555da0,
// FUN_00558aa0).
std::vector<d2d::d2s::Item> spill_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int boxes, int cols, int rows);

// Per stat point, in quarter points (CharStats LifePerVitality,
// StaminaPerVitality, ManaPerMagic): Amazon 12 = 3 life per vitality.
// CharStats per class: gains per stat point and per level (quarter
// points), stat points per level, and ToHitFactor.
struct ClassGains {
    int life_per_vit = 0, stamina_per_vit = 0, mana_per_energy = 0;
    int life_per_level = 0, stamina_per_level = 0, mana_per_level = 0, stat_per_level = 5, to_hit = 0;
    int block = 0;                                     // BlockFactor
    int mana_regen = 120;                              // ManaRegen (record +0x3a): seconds to fill the mana globe
};

// game.exe's MulDiv: value x times / over, truncated, 0 over 0.
int mul_div(int value, int times, int over);

// The player's mana a frame, in its 32-bit math.
std::int64_t mana_per_frame(std::int64_t max_mana, int regen_seconds, int bonus_pct, int flat, bool no_regen = false);

// Spends up to n unspent stat points (stat 4) on stat (0 strength, 1
// energy, 2 dexterity, 3 vitality), as the char panel's buttons ask with
// packet 0x3a (FUN_004a78c0). Vitality raises life and stamina, energy
// mana, current and max (8.8 fixed: a quarter point is 64). Returns the
// points spent.
int spend_stat_points(d2d::d2s::Stats& stats, int stat, int count, const ClassGains& gains);

// Akara's Reset Stat/Skill Points.
void respec(d2d::d2s::Stats& stats, const std::array<int, 4>& base, const ClassGains& gains);

// Can skill i (0..29 of class cls) take a point: a level to go, the character level, and every prerequisite
// learned?
bool can_learn(const Tables& tables, int cls, int skill_index, const std::array<std::uint8_t, 30>& levels, int clvl);

// Spends a skill point (stat 5) on skill i. Returns whether it did.
bool learn_skill(const Tables& tables, int cls, int skill_index, std::array<std::uint8_t, 30>& levels, d2d::d2s::Stats& stats);

// Healers restore life and mana when you talk to them: Akara, Atma, Fara, Ormus, Jamella, Malah.
bool is_healer(int hc_idx);
void heal(d2d::d2s::Stats& stats);

// Pick up item i into the (empty) cursor.
bool pick_up(std::vector<d2d::d2s::Item>& items, std::optional<d2d::d2s::Item>& held, std::size_t index);

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
// (param min..max, value param), 18 by time (FUN_0065f870: the period,
// param 0..3, + ((max + 256) << 10 | (min + 256)) << 2, each 0..1023;
// returns the max part), 19 charges (FUN_0065f6a0: of c = min
// rand(c - c/8) + c/8 + 1 now, value now | c<<8), 20 indestructible
// (stat 152), 21 class skills, 36 the param drawn, value the func's val.
// With `on` (the item the mods land on, FUN_0065fec0's kind 1 superior)
// as game.exe: ac% (2 / 4), and on a superior ac / dur% (1 / 3 / 13),
// first raise an armor's defence to maxac + 1 (FUN_0065ccc0); min / max
// damage (5 / 6) go to the one-hand, two-hand and throw stats the base has
// (FUN_0065ecf0 / FUN_0065ee80; anything but a weapon all three); dmg%
// (7) too small to move the base's max is +1 max damage instead
// (FUN_0065f010); dur% (13) fills the durability to the new most
// (FUN_0065fc90); ethereal (23) makes it so. 5 / 6 pick the hands by
// the base's min / max bytes, and an amount that takes a nonzero base below
// 1 is held: min at 1 (1 - base), max at 0 (-base). Sockets (14,
// FUN_0065f590): the grid (at most 6) and the type's MaxSock cap the value.
// 11 / 19's level is max, or with none off ilvl and the skill's reqlevel
// ((ilvl - req) / 4 + 1 up to its maxlvl; negative: one per (99 - req) /
// -max levels past req). Codes match whatever their case.
// Without `on` (no item: set bonuses, a monster's gear) damage is one-hand,
// charges full and 11 / 19 off ilvl 1.
struct ModItem { d2d::d2s::Item* item; const ItemBase* base; int kind; bool throwable, raise; };   // raise: ac / dur% (1 / 3 / 13) raise the defence too (superior: FUN_0065fec0 kind 1)
std::string lower_case(std::string_view text);
void apply_mod(const Tables& tables, const Mod& mod, std::vector<d2d::d2s::ItemProp>& out, Rng& rng, const ModItem* target = nullptr);

// Affix level for an item of level ilvl with base qlvl.
int affix_level(int ilvl, int qlvl, int magic_lvl);

// A random affix row for an item of `type` at affix level alvl, weighted by frequency; skips groups already
// taken.
int pick_affix(const Tables& tables, const std::vector<Affix>& list, const std::string& type, int alvl, bool rare,
               const std::vector<int>& groups, Rng& rng, bool by_level = false);

// A scroll's or tome's Books.txt row.
int books_row(const Tables& tables, const std::string& code, bool scroll);

// A new item of `code` at level ilvl and quality (4 magic, 5 set, 6 rare, 7 unique; a set / unique none fits is
// remade, below), identified, with its defence and durability rolled.
d2d::d2s::Item roll_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                         Rng* unit_seed = nullptr, std::vector<bool>* found_uniques = nullptr, bool bovine = false,
                         int difficulty = 0, int special_wanted = 0);
// The item as roll_item makes it, with its unit's seed.
d2d::d2s::Item generate_item(const Tables& tables, const std::string& code, int ilvl, int quality, Rng& rng,
                             Rng* unit_seed = nullptr, std::vector<bool>* found_uniques = nullptr, bool bovine = false,
                             int difficulty = 0, int special_wanted = 0);
// The item's unit seed: where the making left it while it's in play, else fresh from its fingerprint, as a
// loaded item's.
[[nodiscard]] Rng item_seed(const d2d::d2s::Item& item);

// Item flag 0x1000 (data +0x18, the save's flags too): no NPC service
// takes the item (FUN_0062c590 / FUN_0062c770 / FUN_0062c6a0).
inline constexpr std::uint32_t kItemNoService = 0x1000;
// Whether a vendor buys `item` (FUN_00579510 -> FUN_0062a130, else result 9):
// no flag 0x1000, not an Items.txt quest item (+0x12a) nor of type 0x27
// (ques, the type itself: FUN_00629d40 reads +0x11e).
bool sellable(const Tables& tables, const d2d::d2s::Item& item);

// Whether Charsi's imbue takes `item`.
bool imbuable(const Tables& tables, const d2d::d2s::Item& item, bool expansion = true);
// A player's item level (FUN_00558200): the base level, at least 1.
// No +4 here. Charsi's imbue (FUN_00579d60) adds 4 when this is past 5.
inline int player_ilvl(int clvl) { return clvl < 2 ? 1 : clvl; }
// The imbued item.
d2d::d2s::Item imbue_item(const Tables& tables, const d2d::d2s::Item& item, int clvl, Rng& rng);

// Exceptional / elite upgrade weights out of 10000 for a gamble at clvl: (clvl - qlvl) * 100 / 2 + 1 and * 100 /
// 4 + 1.
std::pair<int, int> gamble_upgrade(const Tables& tables, const std::string& code, int clvl);

// The most sockets `item` may have: its gemsockets, capped by its type's MaxSock1 / 25 / 40 for item level <=
// 25, <= 40, above.
int most_sockets(const Tables& tables, const d2d::d2s::Item& item);
// Whether Larzuk's socketing takes `item`.
bool socketable(const Tables& tables, const d2d::d2s::Item& item);
// Larzuk's sockets: magic 1 + rand(the most, at most 2) on the item's seed; set, rare, unique, crafted (5..9) 1;
// the rest the most.
d2d::d2s::Item socket_item(const Tables& tables, const d2d::d2s::Item& item);
// Whether Anya's personalizing takes `item`.
bool nameable(const Tables& tables, const d2d::d2s::Item& item);

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

// A mercenary for hire, as FUN_006637f0 rolls it for the hire list.
struct MercOffer {
    int id = 0, level = 0, life = 0, str = 0, dex = 0, cost = 0, def = 0, dmg_min = 0, dmg_max = 0;
    std::uint32_t exp = 0, seed = 0;
    int name = 0;                                       // index from NameFirst
};

// A hire list's entry (FUN_00576070, SUNITNPC.CPP): one per name of the
// seller's (hireling.txt NameFirst..NameLast), its seed a step of the game
// seed; `offered` (+0xc) the ten picked for the list, `hired` (+8) taken.
struct HireEntry { int name = 0; std::uint32_t seed = 0; bool offered = false, hired = false; };
// The list: the entries in name order, each a game seed step, then ten picks of rand(names) on the game seed,
// each moving on past a taken entry (wrapping; back at its start the picking ends).
std::vector<HireEntry> hire_entries(int names, Rng& game);
// The seller's names for act (0-based), difficulty and version.
[[nodiscard]] int hire_names(const Tables& tables, bool expansion, int act, int diff);
// An entry still for hire (FUN_00577010 counts them: none left, the list's rolled anew).
[[nodiscard]] inline bool for_hire(const HireEntry& entry) { return entry.offered && !entry.hired; }

// An entry's offer for act (0-based) and difficulty (0-2), on its own seed {seed, 666}.
std::optional<MercOffer> merc_offer(const Tables& tables, bool expansion, int act, int diff, int clvl, const HireEntry& entry);

// The save's mercenary (hireling Id, experience) as it fights: its level
// is the highest whose experience ((level + 1) * Exp/Lvl * level^2, as
// hiring sets it) it has, its row the LoD band (Version 100) of that Id the
// last at or below that level (FUN_006562f0), its stats as the level-up
// sets them (FUN_00572840, d = level - the row's Level): life HP + HP/Lvl
// x d (40 at least), defence and attack rating + per level x d (0 at
// least), damage + Dmg/Lvl x d / 8 (min 0, max 1 at least: stats 0x17 /
// 0x18), strength and dexterity + per level x d / 8 (10 at least), each of its
// four resistances (stats 0x27 / 0x29 / 0x2b / 0x2d) Resist + Resist/Lvl x
// d / 4 (0 at least).
// Its worn items and the difficulty's resistance penalty go on in the fight
// (Fight::merc_fighter, merc_gear).
struct MercStats { int level = 1, life = 40, def = 0, dmg_min = 1, dmg_max = 2, attack_rating = 0, resist = 0, str = 10, dex = 10; };
MercStats merc_stats(const Tables& tables, int id, std::uint32_t exp);

// The merc's share of its owner's kill.
// `award` is the kill's experience at the merc's level;
std::uint32_t merc_kill_exp(std::uint32_t merc_exp, std::int64_t award, bool merc_blow, int merc_level, int owner_level);

// Hires `o`: pays its cost (carried gold, then the stash) and makes it
// the save's mercenary. False if it can't be paid for.
bool hire(const MercOffer& offer, d2d::d2s::Header& header, d2d::d2s::Stats& stats);

// A dead merc's resurrection: level^2 / 2 * 15, at most 50,000.
int merc_resurrect_cost(int merc_level);

// The NPCs that resurrect a merc (FUN_00579c00; the client's FUN_004b6440
// adds "resurrect", string 0x1507, to their menu while it's dead).
inline constexpr std::array kMercNpcs = { 150, 198, 252, 367, 515 };   // hcIdx: Kashya, Greiz, Asheara, Tyrael (act 4), Qual-Kehk

// Resurrects the save's dead merc at a hire NPC (FUN_00579c00): pays as
// FUN_00576d90 (carried gold, then the stash).
Trade resurrect_merc(int merc_level, d2d::d2s::Header& header, d2d::d2s::Stats& stats);

// Drinks the potion at the bottom of belt column `col` (box col, 0..3): the item goes, those stacked above it
// (boxes col + 4, + 8, + 12) drop a row.
std::string drink_at(const Tables& tables, std::vector<d2d::d2s::Item>& items, std::vector<d2d::d2s::Item>::iterator potion);
// The runeword `item`'s sockets spell, or null: not magic ..
const Tables::Runeword* runeword_row(const Tables& tables, const d2d::d2s::Item& item);
// The last filler in.
bool make_runeword(const Tables& tables, d2d::d2s::Item& item);
// FUN_00562660's checks: `filler` is a socket filler (item type "sock":
// gems, runes, jewels) and `target` identified, socketed, with a socket free.
bool takes_socket(const Tables& tables, const d2d::d2s::Item& filler, const d2d::d2s::Item& target);
std::string drink_item(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id);
std::string drink_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int col);

// Find Potion's potion.
std::string find_potion(int level_id, int difficulty, int roll, int mana_chance, int rejuv_chance);

// A healing / mana potion's amount in 256ths (FUN_005be3f0): calc1 << 8,
// the class's bonus (life FUN_0062a5d0: Amazon, Paladin, Assassin x1.5,
// Barbarian x2; mana FUN_0062a620: Amazon, Paladin, Assassin x1.5,
// Sorceress, Necromancer, Druid x2; `cls` -1, a non-player such as the
// hireling: life x2, mana x1), then doubled when rand(vitality / energy) /
// 2 beats rand(100), the drinker's seed (unit +0x20).
template <class T> T potion_bonus(T amount, int cls, bool life) {
    if (cls < 0) return life ? amount * 2 : amount;
    if (cls == d2d::d2s::kAmazon || cls == d2d::d2s::kPaladin || cls == d2d::d2s::kAssassin) return amount + (amount >> 1);
    return (life ? cls == d2d::d2s::kBarbarian : cls == d2d::d2s::kSorceress || cls == d2d::d2s::kNecromancer || cls == d2d::d2s::kDruid) ? amount * 2 : amount;
}
int potion_amount(int calc, int cls, bool life, int stat, Rng& rng);
// The potion's state (healthpot / manapot) gives it over `len` frames on top of what's left of the last one: per
// frame (old x left + amount) / (left + len), for left + len frames.
int potion_rate(int old_rate, int left, int amount, int len);

// Picking an item up (FUN_00563560, the server's auto-place; gold apart):
// - stacking first (FUN_00560020): a scroll into an inventory tome of its
//   kind with room, one scroll (FUN_0055ffa0 / FUN_0055ef20); a tome's
//   scrolls into such a tome, past its maxstack the rest staying on the
//   ground in the picked one (FUN_0055d370); an AutoStack type (keys) into
//   the inventory's stacks of it with room, the rest placed on (FUN_0055d0d0);
// - the belt (FUN_0063c790): a 1x1 item of a Beltable type (its own row,
//   FUN_0062bad0) to the first column whose bottom item matches (the same
//   code, or both of hp1-5 / mp1-5 / rvs, rvl: FUN_00628a40), its lowest
//   free box below `boxes`; else, autobelt, the first free bottom box
//   (FUN_0063c600). FUN_00628ba0 lets every item through: its bottom check
//   FUN_0063c560 (no isc / tsc) returns 0 or 1 and the caller tests -1,
//   so a scroll goes into a column whose bottom is the same scroll;
// - else the inventory's first free spot (FUN_005600a0); no room leaves it
//   on the ground (FUN_0055c9a0 message 0x17: the class's "can't carry").
//   Before the belt, given a wearer, the auto-equip (auto_equip_slot).
// Tomes by the Books.txt pairs tsc / tbk, isc / ibk.
enum class Pickup { kGone, kStays, kNoRoom };

// The body slot a pickup goes to (FUN_0055d710 mode 0), -1 none: an
// identified item the wearer can use (FUN_0062eaf0), of a type with a body
// slot (FUN_0062fdf0), not broken (0x100) nor flagged 0x4000 (FUN_0062a4e0),
// not a throwing potion (ItemTypes row 0x26, tpot); ammunition (its type's
// Quiver, FUN_00628480) only with a weapon in the hands that shoots it
// (FUN_0062e6f0). One slot (BodyLoc1 == BodyLoc2) when it's empty; two
// (rings, the hands) BodyLoc1 when both are empty, else the empty one when
// what fills the other goes with it (FUN_0055d670: two rings, a bow and its
// ammunition, a one-hander and a shield, two weapons a Barbarian or two
// claws an Assassin holds: rules::hands_ok).
int auto_equip_slot(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, const Wearer& wearer,
                    int required_level = -1);
// The belt groups of FUN_00628a40, 3-char codes in game.exe order (0x744684 hp, 0x74466c mp, 0x744660 rv).
inline constexpr std::array<std::string_view, 3> kGroups{ "hp1hp2hp3hp4hp5", "mp1mp2mp3mp4mp5", "rvlrvs" };
// The item's own ItemTypes row is Beltable.
bool beltable(const Tables& tables, const d2d::d2s::Item& item);
// The belt box `item` goes to (FUN_0063c600), -1 none: a 1x1 Beltable item
// to the first column whose bottom item matches (the same code, or both
// of hp1-5 / mp1-5 / rvs, rvl: FUN_00628a40), its lowest free box below
// `boxes`; else, autobelt, the first free bottom box.
int belt_box(const Tables& tables, const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Item& item, int boxes);
// Shift + left click on an inventory item, the hand empty: an item stored in the inventory moves to belt_box's
// box.
bool auto_belt(const Tables& tables, std::vector<d2d::d2s::Item>& items, int id, int boxes);
// Two items stack (FUN_0062c850): the same class, quality and file index
// (data +0x28: a low / superior row, qsub), both of a Stackable row
// (+0x132), both ethereal or neither (0x400000), of no magic or better
// quality (FUN_0062a2f0: 1..3 or past 9), with damage stats 21-24 and
// 159 / 160 equal (the class's, for such items) and no sockets (stat 0xc2).
bool stacks_with(const d2d::d2s::Item& one, const d2d::d2s::Item& other);
Pickup pick_up(const Tables& tables, std::vector<d2d::d2s::Item>& items, d2d::d2s::Item& item, int cols, int rows, int boxes,
               const Wearer* wearer = nullptr, int required_level = -1);

// Cain's "Identify Items".
bool cain_identifies(const d2d::d2s::Item& item);
inline int unidentified(const std::vector<d2d::d2s::Item>& items) { return int(std::ranges::count_if(items, cain_identifies)); }
int identify_all(std::vector<d2d::d2s::Item>& items);

// FUN_0056bd10's pick (its callback 0x56bcd0, unsigned ids) among the
// units it found (`ids`, any order): the lowest id above `last`, else the
// lowest one up to it (round the ring; `last` itself when it's the lowest).
// -1 none.
int ring_pick(std::span<const int> ids, int last);

}  // namespace d2d::rules
