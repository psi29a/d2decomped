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
    int version = 0;                            // version (row +0x22): 100 an expansion affix
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
    int version = 0;                                       // version: 100 an expansion row
};
// Properties.txt: per code the funcs that turn a mod into stats.
struct PropFunc { int func = 0, stat = -1, val = 0; };
// Sets.txt row: its partial bonuses PCode2a, 2b, 3a .. 5b (8, code empty
// when unused), its full bonuses FCode1..8, and how many SetItems rows
// belong to it (the record's +0xc).
struct SetBonus { std::vector<Mod> partial, full; int pieces = 0, version = 0; };   // version: 100 an expansion set
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
    // The Version 0 rows a classic item's roll takes (FUN_00637910(.., 0)), and which pairs have one.
    std::array<std::array<QualityRatio, 6>, 4> quality_ratio_classic{};
    std::array<bool, 4> has_classic_ratio{};
    // misc.txt potions: life / mana restored (hpregen / manarecovery: that
    // much over `ticks`; hitpoints / mana on a rejuvenation: percent, at once;
    // stamina: staminarecoverybonus % for `ticks`).
    // An antidote's / thawing's state (pSpell 6): its stats (ItemStatCost
    // id, value) for `ticks` frames.
    struct Potion { int life = 0, mana = 0, ticks = 0; bool percent = false, antidote = false, thawing = false; int stamina = 0;
                    std::vector<std::pair<int, int>> state_stats; };
    std::unordered_map<std::string, Potion> potions;
};

// A buy's NPC transaction result (S->C 0x2a +2, FUN_00577830 -> FUN_0053d740):
// done, or the client's box (FUN_004b53f0: 9 string 0xd21 "I cannot complete
// that request.", 10 string 0xd1c "You do not have enough free space to do
// that.", 12 string 0xd22 "You do not have enough gold for that.").
enum class Trade : std::uint8_t { done = 0, hireling = 5, refused = 9, no_room = 10, no_gold = 12 };   // S->C 0x2a's result (5: a merc back)

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

// FUN_0056bd10's pick (its callback 0x56bcd0, unsigned ids) among the
// units it found (`ids`, any order): the lowest id above `last`, else the
// lowest one up to it (round the ring; `last` itself when it's the lowest).
// -1 none.
int ring_pick(std::span<const int> ids, int last);

}  // namespace d2d::rules
