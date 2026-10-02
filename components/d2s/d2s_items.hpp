// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp .d2s item-list parser (1.10+ saves, versions 92..96).
//
// The player's items follow the header/quest/waypoint/stat/skill sections
// as "JM" <u16 count>, then `count` items. Each item is an LSB-first
// bitstream starting with its own "JM", padded to a byte; items with
// filled sockets are followed by those socketed items (not counted).
// unverified (source: the community d2s spec): the layout; the data agrees
// (19 real 1.14d saves, every list ends exactly on the next "JM" — test_d2s):
//   16 "JM" | 32 flags (bit 4 identified, 11 socketed, 16 ear, 21 simple,
//   22 ethereal, 24 personalized, 26 runeword) | 10 version | 3 location
//   (0 stored, 1 equipped, 2 belt, 4 cursor, 6 socketed) | 4 body slot |
//   4 column | 4 row | 3 panel (1 inventory, 4 cube, 5 stash) |
//   32 code (4 chars) | 3 filled sockets
//   extended (not simple): 32 uid | 7 ilvl | 4 quality | 1(+3) picture |
//   1(+11) class affix | quality data | runeword 12+4 | name 7-bit chars
//   to 0 | tome 5 | 1 | armour: 11 defence-10 | armour/weapon: 8 maxdur
//   (+9 durability when non-zero) | stackable: 9 qty | socketed: 4 |
//   set: 5 list flags | property list(s), each ending in 0x1ff.
// Property widths come from ItemStatCost.txt (Save Param Bits, Save Bits,
// Save Add); some stats pull the next ones along (17/18 enhanced damage,
// elemental min/max/length runs).
#pragma once

#include <txt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace d2d::d2s {

struct ItemProp { int stat = 0, param = 0, value = 0; };

struct Item {
    std::string code;                  // "hax", "ear" for ears
    int  location = 0, slot = 0, column = 0, row = 0, panel = 0;
    bool identified = false, socketed = false, ethereal = false,
         personalized = false, runeword = false, simple = false;
    int  quality = 2;                  // 1 low .. 8 crafted; 2 = normal
    int  ilvl = 0, defense = -1, quantity = -1, sockets = 0;
    int  durability = 0, max_durability = 0;   // max 0: indestructible / none
    int  set_id = -1, unique_id = -1;
    int  qsub = 0;                     // low (0 crude..3) / superior subtype
    int  picture = -1;                 // ItemTypes InvGfx index (rings, charms, jewels ...)
    int  prefix = 0, suffix = 0;       // magic: MagicPrefix/MagicSuffix row (0 = none)
    int  rare1 = 0, rare2 = 0;         // rare/crafted name: RarePrefix/RareSuffix IDs
    std::array<int, 6> affixes{};      // rare/crafted: prefix, suffix, prefix, ... (0 = none)
    int  runeword_id = -1;
    std::string owner;                 // personalized
    std::uint32_t uid = 0;
    std::vector<ItemProp> props;       // main list, runeword list appended
    std::vector<ItemProp> set_props;   // set bonus lists (active by pieces worn)
    std::vector<Item>     socketed_items;
    // What the writer needs to put the item back bit for bit (d2s_write.hpp):
    // the flag word as read (the named flags above win over it), the item
    // version, the class-specific affix (-1 none), the runeword's 4 extra
    // bits, a tome's 5 bits (-1 none), the bit after them, durability's
    // extra bit, the set's list flags, where the runeword list starts in
    // `props` and how long each set list is; an ear's class, level and name.
    int id = -1;                       // the game's unit id while it's in play (not saved)
    std::uint32_t flags = 0;
    int version = 101, class_affix = -1, rw_extra = 0, tome = -1, bit_after = 0, dur_extra = 0, set_lists = 0;
    std::size_t main_props = 0;
    std::vector<std::size_t> set_list_sizes;
    int ear_class = 0, ear_level = 0;
};

// What the parser needs from the excel tables.
struct ItemTables {
    struct Stat { int save_bits = 0, save_add = 0, param_bits = 0, csv_bits = 0; };
    std::vector<Stat> stats;                        // by ItemStatCost ID
    std::unordered_set<std::string> armor, weapons, stackable;
    std::unordered_set<std::string> compact;        // misc.txt compactsave: written short (simple)

    static ItemTables from(const txt::Table& isc, const txt::Table& armor_t,
                           const txt::Table& weapons_t, const txt::Table& misc_t) {
        ItemTables item_tables;
        const auto c_id = isc.col("ID"), c_b = isc.col("Save Bits"),
                   c_a = isc.col("Save Add"), c_p = isc.col("Save Param Bits"),
                   c_c = isc.col("CSvBits");
        auto num = [](std::string_view text) { return text.empty() ? 0 : std::stoi(std::string(text)); };
        for (std::size_t row = 0; row < isc.size(); ++row) {
            const auto id = isc.get(row, c_id);
            if (id.empty()) continue;
            const auto stat_id = std::size_t(num(id));
            if (stat_id >= item_tables.stats.size()) item_tables.stats.resize(stat_id + 1);
            item_tables.stats[stat_id] = { num(isc.get(row, c_b)), num(isc.get(row, c_a)), num(isc.get(row, c_p)),
                           num(isc.get(row, c_c)) };
        }
        auto codes = [&](const txt::Table& tab, auto& into) {
            for (std::size_t row = 0; row < tab.size(); ++row) {
                const std::string code(tab.get(row, "code"));
                into.insert(code);
                if (tab.get(row, "stackable") == "1") item_tables.stackable.insert(code);
                if (tab.get(row, "compactsave") == "1") item_tables.compact.insert(code);
            }
        };
        std::unordered_set<std::string> misc;
        codes(armor_t, item_tables.armor); codes(weapons_t, item_tables.weapons); codes(misc_t, misc);
        return item_tables;
    }
};

namespace detail {
struct Bits {
    std::span<const std::byte> bytes;
    std::size_t pos = 0;   // in bits
    std::uint32_t read(int count) {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i, ++pos) {
            if ((pos >> 3) >= bytes.size()) throw std::runtime_error("d2s items: read past end");
            value |= std::uint32_t((std::uint8_t(bytes[pos >> 3]) >> (pos & 7)) & 1) << i;
        }
        return value;
    }
};

inline void props(Bits& bits, const ItemTables& item_tables, std::vector<ItemProp>& out) {
    for (;;) {
        const int id = int(bits.read(9));
        if (id == 0x1ff) return;
        // Stats that carry the next ones with them.
        int run = 1;
        if (id == 17 || id == 48 || id == 50 || id == 52) run = 2;
        if (id == 54 || id == 57) run = 3;
        for (int stat = id; stat < id + run; ++stat) {
            if (std::size_t(stat) >= item_tables.stats.size() || item_tables.stats[std::size_t(stat)].save_bits == 0)
                throw std::runtime_error("d2s items: unknown stat " + std::to_string(stat));
            const auto& stat_info = item_tables.stats[std::size_t(stat)];
            const int param = stat_info.param_bits ? int(bits.read(stat_info.param_bits)) : 0;
            out.push_back({ stat, param, int(bits.read(stat_info.save_bits)) - stat_info.save_add });
        }
    }
}

inline Item item(Bits& bits, const ItemTables& item_tables) {
    if (bits.pos % 8 || bits.read(16) != 0x4d4a) throw std::runtime_error("d2s items: missing JM");
    Item parsed;
    const std::uint32_t flags = bits.read(32);
    parsed.flags = flags;
    parsed.identified   = flags >> 4 & 1;  parsed.socketed = flags >> 11 & 1;
    const bool ear  = flags >> 16 & 1; parsed.simple   = flags >> 21 & 1;
    parsed.ethereal     = flags >> 22 & 1; parsed.personalized = flags >> 24 & 1;
    parsed.runeword     = flags >> 26 & 1;
    parsed.version = int(bits.read(10));
    parsed.location = int(bits.read(3)); parsed.slot = int(bits.read(4));
    parsed.column   = int(bits.read(4)); parsed.row  = int(bits.read(4)); parsed.panel = int(bits.read(3));
    int filled = 0;
    if (ear) {
        parsed.code = "ear";
        parsed.ear_class = int(bits.read(3)); parsed.ear_level = int(bits.read(7));
        while (const auto letter = bits.read(7)) parsed.owner.push_back(char(letter));
    } else {
        for (int i = 0; i < 4; ++i) {
            const char letter = char(bits.read(8));
            if (letter != ' ' && letter != '\0') parsed.code.push_back(letter);
        }
        filled = int(bits.read(3));
        if (!parsed.simple) {
            parsed.uid = bits.read(32); parsed.ilvl = int(bits.read(7)); parsed.quality = int(bits.read(4));
            if (bits.read(1)) parsed.picture = int(bits.read(3));
            if (bits.read(1)) parsed.class_affix = int(bits.read(11));   // class-specific auto affix
            switch (parsed.quality) {
                case 1: case 3: parsed.qsub = int(bits.read(3)); break;
                case 4: parsed.prefix = int(bits.read(11)); parsed.suffix = int(bits.read(11)); break;
                case 5: parsed.set_id = int(bits.read(12)); break;
                case 7: parsed.unique_id = int(bits.read(12)); break;
                case 6: case 8:
                    parsed.rare1 = int(bits.read(8)); parsed.rare2 = int(bits.read(8));
                    for (auto& affix : parsed.affixes) affix = bits.read(1) ? int(bits.read(11)) : 0;
                    break;
                default: break;
            }
            if (parsed.runeword) { parsed.runeword_id = int(bits.read(12)); parsed.rw_extra = int(bits.read(4)); }
            if (parsed.personalized) while (const auto letter = bits.read(7)) parsed.owner.push_back(char(letter));
            if (parsed.code == "tbk" || parsed.code == "ibk") parsed.tome = int(bits.read(5));
            parsed.bit_after = int(bits.read(1));
            if (item_tables.armor.contains(parsed.code)) parsed.defense = int(bits.read(11)) - 10;
            if (item_tables.armor.contains(parsed.code) || item_tables.weapons.contains(parsed.code))
                if ((parsed.max_durability = int(bits.read(8)))) {   // max, then current (8 bits + 1 unused)
                    parsed.durability = int(bits.read(8));
                    parsed.dur_extra = int(bits.read(1));
                }
            if (item_tables.stackable.contains(parsed.code)) parsed.quantity = int(bits.read(9));
            if (parsed.socketed) parsed.sockets = int(bits.read(4));
            int lists = 0;
            if (parsed.quality == 5) { parsed.set_lists = int(bits.read(5)); for (auto set_list = parsed.set_lists; set_list; set_list &= set_list - 1) ++lists; }
            props(bits, item_tables, parsed.props);
            parsed.main_props = parsed.props.size();
            for (int i = 0; i < lists; ++i) {
                const auto props_before = parsed.set_props.size();
                props(bits, item_tables, parsed.set_props);
                parsed.set_list_sizes.push_back(parsed.set_props.size() - props_before);
            }
            if (parsed.runeword) props(bits, item_tables, parsed.props);
        }
    }
    bits.pos = (bits.pos + 7) & ~std::size_t(7);
    for (int i = 0; i < filled; ++i) parsed.socketed_items.push_back(item(bits, item_tables));
    return parsed;
}
}  // namespace detail

// Character attributes — the "gf" section at 0x2FD: 9-bit stat id, value
// of ItemStatCost CSvBits width, until 0x1ff; then "if" + 30 skill bytes,
// then the items. Life/mana/stamina (6..11) are 8.8 fixed point.
struct Stats {
    std::array<std::int64_t, 16> values{};   // by stat id 0..15 (strength .. goldbank)
    std::size_t items_at = 0;           // byte offset of the item list's "JM"
    std::array<std::uint8_t, 30> skills{};   // "if": the class's 30 skills in Skills.txt order, base levels
    [[nodiscard]] std::int64_t get(int id) const { return id >= 0 && id < 16 ? values[std::size_t(id)] : 0; }
    [[nodiscard]] std::int64_t fixed(int id) const { return get(id) >> 8; }   // life/mana/stamina
};
enum StatId { kStr = 0, kEne = 1, kDex = 2, kVit = 3, kStatPts = 4, kSkillPts = 5,
              kLife = 6, kMaxLife = 7, kMana = 8, kMaxMana = 9, kStamina = 10,
              kMaxStamina = 11, kLevel = 12, kExp = 13, kGold = 14, kGoldBank = 15 };

inline Stats parse_stats(std::span<const std::byte> save, const ItemTables& item_tables) {
    constexpr std::size_t kGf = 0x2FD;
    if (save.size() < kGf + 2 || save[kGf] != std::byte{'g'} || save[kGf + 1] != std::byte{'f'})
        throw std::runtime_error("d2s: no stats section");
    Stats stats;
    detail::Bits bits{ save, (kGf + 2) * 8 };
    for (;;) {
        const int id = int(bits.read(9));
        if (id == 0x1ff) break;
        if (std::size_t(id) >= item_tables.stats.size() || item_tables.stats[std::size_t(id)].csv_bits == 0)
            throw std::runtime_error("d2s: unknown character stat " + std::to_string(id));
        const auto val = bits.read(item_tables.stats[std::size_t(id)].csv_bits);
        if (id < 16) stats.values[std::size_t(id)] = val;
    }
    const std::size_t items_at = (bits.pos + 7) / 8;              // "if" + 30 bytes follow
    if (items_at + 32 > save.size() || save[items_at] != std::byte{'i'} || save[items_at + 1] != std::byte{'f'})
        throw std::runtime_error("d2s: skills section not after stats");
    for (std::size_t i = 0; i < 30; ++i) stats.skills[i] = std::uint8_t(save[items_at + 2 + i]);
    stats.items_at = items_at + 32;
    return stats;
}

// The player's item list. Throws on anything that doesn't parse cleanly.
inline std::vector<Item> parse_items(std::span<const std::byte> save, const ItemTables& item_tables) {
    // Exactly after the stats + skills sections; the fixed-width scan is
    // only a fallback for tables without CSvBits (unit tests).
    std::size_t offset = 0x2FD;
    try { offset = parse_stats(save, item_tables).items_at; } catch (const std::runtime_error&) {
        for (; offset + 4 <= save.size(); ++offset)
            if (save[offset] == std::byte{'J'} && save[offset + 1] == std::byte{'M'}) break;
    }
    if (offset + 4 > save.size()) throw std::runtime_error("d2s items: no item list");
    const int count = int(std::uint8_t(save[offset + 2])) | int(std::uint8_t(save[offset + 3])) << 8;
    detail::Bits bits{ save, (offset + 4) * 8 };
    std::vector<Item> out;
    for (int i = 0; i < count; ++i) out.push_back(detail::item(bits, item_tables));
    return out;
}

// The corpse list after the player's items (PlrSave2.cpp; read back by
// FUN_00533850): "JM" <u16 corpses>, then per corpse 12 bytes (u32 ?,
// u32 x, u32 y) and its own "JM" item list. `end`: the byte after it.
// A save has at most one corpse.
struct CorpseList { std::size_t begin = 0, end = 0; std::vector<Item> items; bool has = false; };
inline CorpseList parse_corpse(std::span<const std::byte> save, const ItemTables& item_tables) {
    CorpseList corpse;
    const auto items_at = parse_stats(save, item_tables).items_at;
    const int count = int(std::uint8_t(save[items_at + 2])) | int(std::uint8_t(save[items_at + 3])) << 8;
    detail::Bits bits{ save, (items_at + 4) * 8 };
    for (int i = 0; i < count; ++i) detail::item(bits, item_tables);
    corpse.begin = corpse.end = bits.pos / 8;
    if (corpse.begin + 4 > save.size() || save[corpse.begin] != std::byte{'J'} || save[corpse.begin + 1] != std::byte{'M'}) return corpse;
    const int corpses = int(std::uint8_t(save[corpse.begin + 2])) | int(std::uint8_t(save[corpse.begin + 3])) << 8;
    std::size_t corpse_offset = corpse.begin + 4;
    for (int k = 0; k < corpses; ++k) {
        corpse_offset += 12;
        if (corpse_offset + 4 > save.size() || save[corpse_offset] != std::byte{'J'}) throw std::runtime_error("d2s: corpse without items");
        const int item_count = int(std::uint8_t(save[corpse_offset + 2])) | int(std::uint8_t(save[corpse_offset + 3])) << 8;
        detail::Bits corpse_bits{ save, (corpse_offset + 4) * 8 };
        for (int i = 0; i < item_count; ++i) corpse.items.push_back(detail::item(corpse_bits, item_tables));
        corpse_offset = corpse_bits.pos / 8;
        corpse.has = true;
    }
    corpse.end = corpse_offset;
    return corpse;
}

}  // namespace d2d::d2s
