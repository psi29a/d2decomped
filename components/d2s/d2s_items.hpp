// D2Decomp .d2s item-list parser (1.10+ saves, versions 92..96).
//
// The player's items follow the header/quest/waypoint/stat/skill sections
// as "JM" <u16 count>, then `count` items. Each item is an LSB-first
// bitstream starting with its own "JM", padded to a byte; items with
// filled sockets are followed by those socketed items (not counted).
// Layout per the community d2s spec, confirmed against 19 real 1.14d
// saves (every list ends exactly on the next "JM" — test_d2s):
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
    int  set_id = -1, unique_id = -1;
    std::uint32_t uid = 0;
    std::vector<ItemProp> props;       // main list (set/runeword lists appended)
    std::vector<Item>     socketed_items;
};

// What the parser needs from the excel tables.
struct ItemTables {
    struct Stat { int save_bits = 0, save_add = 0, param_bits = 0; };
    std::vector<Stat> stats;                        // by ItemStatCost ID
    std::unordered_set<std::string> armor, weapons, stackable;

    static ItemTables from(const txt::Table& isc, const txt::Table& armor_t,
                           const txt::Table& weapons_t, const txt::Table& misc_t) {
        ItemTables t;
        const auto c_id = isc.col("ID"), c_b = isc.col("Save Bits"),
                   c_a = isc.col("Save Add"), c_p = isc.col("Save Param Bits");
        auto num = [](std::string_view s) { return s.empty() ? 0 : std::stoi(std::string(s)); };
        for (std::size_t r = 0; r < isc.size(); ++r) {
            const auto id = isc.get(r, c_id);
            if (id.empty()) continue;
            const auto i = std::size_t(num(id));
            if (i >= t.stats.size()) t.stats.resize(i + 1);
            t.stats[i] = { num(isc.get(r, c_b)), num(isc.get(r, c_a)), num(isc.get(r, c_p)) };
        }
        auto codes = [&](const txt::Table& tab, auto& into) {
            for (std::size_t r = 0; r < tab.size(); ++r) {
                const std::string code(tab.get(r, "code"));
                into.insert(code);
                if (tab.get(r, "stackable") == "1") t.stackable.insert(code);
            }
        };
        std::unordered_set<std::string> misc;
        codes(armor_t, t.armor); codes(weapons_t, t.weapons); codes(misc_t, misc);
        return t;
    }
};

namespace detail {
struct Bits {
    std::span<const std::byte> b;
    std::size_t pos = 0;   // in bits
    std::uint32_t read(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i, ++pos) {
            if ((pos >> 3) >= b.size()) throw std::runtime_error("d2s items: read past end");
            v |= std::uint32_t((std::uint8_t(b[pos >> 3]) >> (pos & 7)) & 1) << i;
        }
        return v;
    }
};

inline void props(Bits& bs, const ItemTables& t, std::vector<ItemProp>& out) {
    for (;;) {
        const int id = int(bs.read(9));
        if (id == 0x1ff) return;
        // Stats that carry the next ones with them.
        int run = 1;
        if (id == 17 || id == 48 || id == 50 || id == 52) run = 2;
        if (id == 54 || id == 57) run = 3;
        for (int s = id; s < id + run; ++s) {
            if (std::size_t(s) >= t.stats.size() || t.stats[std::size_t(s)].save_bits == 0)
                throw std::runtime_error("d2s items: unknown stat " + std::to_string(s));
            const auto& st = t.stats[std::size_t(s)];
            const int param = st.param_bits ? int(bs.read(st.param_bits)) : 0;
            out.push_back({ s, param, int(bs.read(st.save_bits)) - st.save_add });
        }
    }
}

inline Item item(Bits& bs, const ItemTables& t) {
    if (bs.pos % 8 || bs.read(16) != 0x4d4a) throw std::runtime_error("d2s items: missing JM");
    Item it;
    const std::uint32_t f = bs.read(32);
    it.identified   = f >> 4 & 1;  it.socketed = f >> 11 & 1;
    const bool ear  = f >> 16 & 1; it.simple   = f >> 21 & 1;
    it.ethereal     = f >> 22 & 1; it.personalized = f >> 24 & 1;
    it.runeword     = f >> 26 & 1;
    bs.read(10);                                   // item version
    it.location = int(bs.read(3)); it.slot = int(bs.read(4));
    it.column   = int(bs.read(4)); it.row  = int(bs.read(4)); it.panel = int(bs.read(3));
    int filled = 0;
    if (ear) {
        it.code = "ear";
        bs.read(3); bs.read(7);                    // class, level
        while (bs.read(7)) {}                      // owner name
    } else {
        for (int i = 0; i < 4; ++i) {
            const char c = char(bs.read(8));
            if (c != ' ' && c != '\0') it.code.push_back(c);
        }
        filled = int(bs.read(3));
        if (!it.simple) {
            it.uid = bs.read(32); it.ilvl = int(bs.read(7)); it.quality = int(bs.read(4));
            if (bs.read(1)) bs.read(3);            // picture
            if (bs.read(1)) bs.read(11);           // class-specific auto affix
            switch (it.quality) {
                case 1: case 3: bs.read(3); break;
                case 4: bs.read(11); bs.read(11); break;
                case 5: it.set_id = int(bs.read(12)); break;
                case 7: it.unique_id = int(bs.read(12)); break;
                case 6: case 8:
                    bs.read(8); bs.read(8);
                    for (int i = 0; i < 6; ++i) if (bs.read(1)) bs.read(11);
                    break;
                default: break;
            }
            if (it.runeword) { bs.read(12); bs.read(4); }
            if (it.personalized) while (bs.read(7)) {}
            if (it.code == "tbk" || it.code == "ibk") bs.read(5);
            bs.read(1);
            if (t.armor.contains(it.code)) it.defense = int(bs.read(11)) - 10;
            if (t.armor.contains(it.code) || t.weapons.contains(it.code))
                if (bs.read(8)) bs.read(9);        // max / current durability
            if (t.stackable.contains(it.code)) it.quantity = int(bs.read(9));
            if (it.socketed) it.sockets = int(bs.read(4));
            int lists = 0;
            if (it.quality == 5) for (auto sf = bs.read(5); sf; sf &= sf - 1) ++lists;
            props(bs, t, it.props);
            for (int i = 0; i < lists; ++i) props(bs, t, it.props);
            if (it.runeword) props(bs, t, it.props);
        }
    }
    bs.pos = (bs.pos + 7) & ~std::size_t(7);
    for (int i = 0; i < filled; ++i) it.socketed_items.push_back(item(bs, t));
    return it;
}
}  // namespace detail

// The player's item list. Throws on anything that doesn't parse cleanly.
inline std::vector<Item> parse_items(std::span<const std::byte> save, const ItemTables& t) {
    std::size_t at = 0x2FD;   // items live after the fixed-size sections
    for (; at + 4 <= save.size(); ++at)
        if (save[at] == std::byte{'J'} && save[at + 1] == std::byte{'M'}) break;
    if (at + 4 > save.size()) throw std::runtime_error("d2s items: no item list");
    const int count = int(std::uint8_t(save[at + 2])) | int(std::uint8_t(save[at + 3])) << 8;
    detail::Bits bs{ save, (at + 4) * 8 };
    std::vector<Item> out;
    for (int i = 0; i < count; ++i) out.push_back(detail::item(bs, t));
    return out;
}

}  // namespace d2d::d2s
