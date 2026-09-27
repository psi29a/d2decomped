// D2Decomp .d2s writer: the parser (d2s.hpp, d2s_items.hpp) run backwards.
//
// A save is written over its original file (the template): the header's
// fields the game tracks go back at their offsets, everything else in the
// first 0x2FD bytes (hotkeys' neighbours, the NPC intro flags, ...) stays;
// then the stats ("gf"), the skills ("if") and the player's items ("JM")
// are encoded; the rest of the file (the corpse, the merc's and the golem's
// items) is copied as it was. Last, the file size (+0x08) and the checksum
// (+0x0C: every byte, with the checksum field zeroed, added to the running
// sum rotated left by one).
// A character with no template (made in d2d) gets a fresh header and
// empty corpse / merc / golem lists.
// test_d2s writes every real save it parses back and wants the same bytes.
#pragma once

#include "d2s.hpp"
#include "d2s_items.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2d::d2s {

namespace detail {
struct BitWriter {
    std::vector<std::byte> out;
    std::size_t pos = 0;   // in bits
    void write(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++pos) {
            if ((pos >> 3) >= out.size()) out.push_back(std::byte{ 0 });
            if (v >> i & 1) out[pos >> 3] |= std::byte(1 << (pos & 7));
        }
    }
    void align() {
        pos = (pos + 7) & ~std::size_t(7);
        out.resize(pos >> 3);
    }
};

// One property list: runs of stats that travel together (17/18, the
// elemental min / max / length) are written under their first id.
inline void write_props(BitWriter& w, const ItemTables& t, const ItemProp* p, std::size_t n) {
    for (std::size_t i = 0; i < n;) {
        const int id = p[i].stat;
        const std::size_t run = id == 17 || id == 48 || id == 50 || id == 52 ? 2 : id == 54 || id == 57 ? 3 : 1;
        w.write(std::uint32_t(id), 9);
        for (std::size_t k = 0; k < run && i < n; ++k, ++i) {
            if (p[i].stat < 0 || std::size_t(p[i].stat) >= t.stats.size() || !t.stats[std::size_t(p[i].stat)].save_bits)
                throw std::runtime_error("d2s write: stat " + std::to_string(p[i].stat) + " can't be saved");
            const auto& st = t.stats[std::size_t(p[i].stat)];
            if (st.param_bits) w.write(std::uint32_t(p[i].param), st.param_bits);
            w.write(std::uint32_t(p[i].value + st.save_add), st.save_bits);
        }
    }
    w.write(0x1ff, 9);
}

inline void write_item(BitWriter& w, const Item& it, const ItemTables& t) {
    w.write(0x4d4a, 16);
    const bool ear = it.code == "ear";
    std::uint32_t f = it.flags;
    auto flag = [&](int bit, bool on) { f = on ? f | 1u << bit : f & ~(1u << bit); };
    flag(4, it.identified); flag(11, it.socketed); flag(16, ear); flag(21, it.simple);
    flag(22, it.ethereal); flag(24, it.personalized); flag(26, it.runeword);
    f |= 1u << 23;                                        // set on every item in the real saves
    w.write(f, 32);
    w.write(std::uint32_t(it.version), 10);
    w.write(std::uint32_t(it.location), 3); w.write(std::uint32_t(it.slot), 4);
    w.write(std::uint32_t(it.column), 4); w.write(std::uint32_t(it.row), 4); w.write(std::uint32_t(it.panel), 3);
    if (ear) {
        w.write(std::uint32_t(it.ear_class), 3); w.write(std::uint32_t(it.ear_level), 7);
        for (const char c : it.owner) w.write(std::uint8_t(c), 7);
        w.write(0, 7);
    } else {
        for (std::size_t i = 0; i < 4; ++i) w.write(i < it.code.size() ? std::uint8_t(it.code[i]) : std::uint8_t(' '), 8);
        w.write(std::uint32_t(it.socketed_items.size()), 3);
        if (!it.simple) {
            w.write(it.uid, 32); w.write(std::uint32_t(it.ilvl), 7); w.write(std::uint32_t(it.quality), 4);
            w.write(it.picture >= 0, 1); if (it.picture >= 0) w.write(std::uint32_t(it.picture), 3);
            w.write(it.class_affix >= 0, 1); if (it.class_affix >= 0) w.write(std::uint32_t(it.class_affix), 11);
            switch (it.quality) {
                case 1: case 3: w.write(std::uint32_t(it.qsub), 3); break;
                case 4: w.write(std::uint32_t(it.prefix), 11); w.write(std::uint32_t(it.suffix), 11); break;
                case 5: w.write(std::uint32_t(it.set_id), 12); break;
                case 7: w.write(std::uint32_t(it.unique_id), 12); break;
                case 6: case 8:
                    w.write(std::uint32_t(it.rare1), 8); w.write(std::uint32_t(it.rare2), 8);
                    for (const int a : it.affixes) { w.write(a != 0, 1); if (a) w.write(std::uint32_t(a), 11); }
                    break;
                default: break;
            }
            if (it.runeword) { w.write(std::uint32_t(it.runeword_id), 12); w.write(std::uint32_t(it.rw_extra), 4); }
            if (it.personalized) { for (const char c : it.owner) w.write(std::uint8_t(c), 7); w.write(0, 7); }
            if (it.code == "tbk" || it.code == "ibk") w.write(std::uint32_t(std::max(it.tome, 0)), 5);
            w.write(std::uint32_t(it.bit_after), 1);
            if (t.armor.contains(it.code)) w.write(std::uint32_t(it.defense + 10), 11);
            if (t.armor.contains(it.code) || t.weapons.contains(it.code)) {
                w.write(std::uint32_t(it.max_durability), 8);
                if (it.max_durability) { w.write(std::uint32_t(it.durability), 8); w.write(std::uint32_t(it.dur_extra), 1); }
            }
            if (t.stackable.contains(it.code)) w.write(std::uint32_t(std::max(it.quantity, 0)), 9);
            if (it.socketed) w.write(std::uint32_t(it.sockets), 4);
            if (it.quality == 5) w.write(std::uint32_t(it.set_lists), 5);
            // Items made in d2d have no runeword list: all of props is the main one.
            const std::size_t main = it.runeword ? std::min(it.main_props, it.props.size()) : it.props.size();
            write_props(w, t, it.props.data(), main);
            std::size_t at = 0;
            for (const auto n : it.set_list_sizes) { write_props(w, t, it.set_props.data() + at, n); at += n; }
            if (it.runeword) write_props(w, t, it.props.data() + main, it.props.size() - main);
        }
    }
    w.align();
    for (const auto& s : it.socketed_items) write_item(w, s, t);
}
}  // namespace detail

// The checksum at +0x0C: over the whole file with that field zeroed, each
// byte added to the sum rotated left by one bit.
inline std::uint32_t save_checksum(std::span<const std::byte> b) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < b.size(); ++i) {
        const std::uint32_t v = i >= 0x0C && i < 0x10 ? 0 : std::uint32_t(std::uint8_t(b[i]));
        sum = (sum << 1 | sum >> 31) + v;
    }
    return sum;
}

// The whole file. `original`: the save as read (empty for a character made
// in d2d); its unknown header bytes and trailing lists are kept.
inline std::vector<std::byte> write_save(std::span<const std::byte> original, const Header& h, const Stats& st,
                                         const std::vector<Item>& items, const ItemTables& t) {
    constexpr std::size_t kGf = 0x2FD;
    std::vector<std::byte> b(kGf, std::byte{ 0 });
    const bool has_template = original.size() >= kGf;
    if (has_template) std::memcpy(b.data(), original.data(), kGf);
    auto w32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
    auto w16 = [&](std::size_t off, std::uint16_t v) { std::memcpy(b.data() + off, &v, 2); };
    w32(0x00, kMagic);
    w32(0x04, h.version ? h.version : kMaxVersion);
    std::memset(b.data() + 0x14, 0, 16);
    std::memcpy(b.data() + 0x14, h.name.data(), std::min<std::size_t>(h.name.size(), 15));
    b[0x24] = std::byte(h.status); b[0x25] = std::byte(h.progression);
    b[0x28] = std::byte(h.cls);    b[0x2B] = std::byte(h.level);
    w32(0x30, h.last_played);
    for (std::size_t i = 0; i < 16; ++i) w32(0x38 + i * 4, h.hotkeys[i]);
    w32(0x78, h.left_skill); w32(0x7C, h.right_skill); w32(0x80, h.left_swap); w32(0x84, h.right_swap);
    std::memcpy(b.data() + 0x88, h.appearance.data(), 16);
    std::memcpy(b.data() + 0x98, h.tints.data(), 16);
    std::memcpy(b.data() + 0xA8, h.difficulty.data(), 3);
    w16(0xB1, h.merc_dead ? 1 : 0); w32(0xB3, h.merc_seed); w16(0xB7, h.merc_name); w16(0xB9, h.merc_type); w32(0xBB, h.merc_exp);
    // Quests and waypoints: their blocks' markers, then the flags.
    std::memcpy(b.data() + 0x14F, "Woo!", 4); w32(0x153, 6); w16(0x157, 298);
    for (std::size_t d = 0; d < 3; ++d) std::memcpy(b.data() + 0x159 + d * 96, h.quests[d].data(), 96);
    std::memcpy(b.data() + 0x279, "WS", 2);
    if (!has_template) { w32(0x27B, 1); w16(0x27F, 80); }
    for (std::size_t d = 0; d < 3; ++d) {
        if (!has_template) { b[0x281 + d * 24] = std::byte{ 2 }; b[0x282 + d * 24] = std::byte{ 1 }; }
        std::memcpy(b.data() + 0x283 + d * 24, h.waypoints[d].data(), 5);
    }
    if (!has_template) { b[0x2C9] = std::byte{ 0x01 }; std::memcpy(b.data() + 0x2CA, "w4", 2); b[0x2CC] = std::byte{ 0x34 }; }

    // Stats: those that aren't zero, by id, in their CSvBits widths; the
    // level always.
    detail::BitWriter w;
    w.write('g', 8); w.write('f', 8);
    for (int id = 0; id < 16; ++id) {
        if (st.v[std::size_t(id)] == 0 && id != kLevel) continue;
        if (std::size_t(id) >= t.stats.size() || !t.stats[std::size_t(id)].csv_bits) continue;
        w.write(std::uint32_t(id), 9);
        w.write(std::uint32_t(st.v[std::size_t(id)]), t.stats[std::size_t(id)].csv_bits);
    }
    w.write(0x1ff, 9);
    w.align();
    b.insert(b.end(), w.out.begin(), w.out.end());
    b.push_back(std::byte{ 'i' }); b.push_back(std::byte{ 'f' });
    for (const auto s : st.skills) b.push_back(std::byte(s));

    // The player's items.
    b.push_back(std::byte{ 'J' }); b.push_back(std::byte{ 'M' });
    b.push_back(std::byte(items.size() & 0xff)); b.push_back(std::byte(items.size() >> 8));
    detail::BitWriter iw;
    for (const auto& it : items) detail::write_item(iw, it, t);
    b.insert(b.end(), iw.out.begin(), iw.out.end());

    // The rest as it was: where the original's player list ended.
    std::size_t tail = original.size();
    if (has_template) {
        try {
            const auto ost = parse_stats(original, t);
            const int n = int(std::uint8_t(original[ost.items_at + 2])) | int(std::uint8_t(original[ost.items_at + 3])) << 8;
            detail::Bits bs{ original, (ost.items_at + 4) * 8 };
            for (int i = 0; i < n; ++i) detail::item(bs, t);
            tail = bs.pos / 8;
        } catch (const std::runtime_error&) { tail = original.size(); }   // a header-only template: nothing after it
    }
    if (tail < original.size()) b.insert(b.end(), original.begin() + std::ptrdiff_t(tail), original.end());
    else {                                                   // fresh: no corpse; no merc items; no golem
        for (const char c : { 'J', 'M', '\0', '\0' }) b.push_back(std::byte(c));
        if (h.expansion()) {
            b.push_back(std::byte{ 'j' }); b.push_back(std::byte{ 'f' });
            if (h.merc_seed) for (const char c : { 'J', 'M', '\0', '\0' }) b.push_back(std::byte(c));
            for (const char c : { 'k', 'f', '\0' }) b.push_back(std::byte(c));
        }
    }
    w32(0x08, std::uint32_t(b.size()));
    w32(0x0C, 0);
    w32(0x0C, save_checksum(b));
    return b;
}

}  // namespace d2d::d2s
