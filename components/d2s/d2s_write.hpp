// SPDX-License-Identifier: GPL-3.0-or-later
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2d::d2s {

namespace detail {
struct BitWriter {
    std::vector<std::byte> out;
    std::size_t pos = 0;   // in bits
    void write(std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i, ++pos) {
            if ((pos >> 3) >= out.size()) out.push_back(std::byte{ 0 });
            if (value >> i & 1) out[pos >> 3] |= std::byte(1 << (pos & 7));
        }
    }
    void align() {
        pos = (pos + 7) & ~std::size_t(7);
        out.resize(pos >> 3);
    }
};

// One property list: runs of stats that travel together (17/18, the
// elemental min / max / length) are written under their first id.
inline void write_props(BitWriter& writer, const ItemTables& item_tables, const ItemProp* props, std::size_t count) {
    for (std::size_t i = 0; i < count;) {
        const int id = props[i].stat;
        const std::size_t run = id == kMaxDamagePercent || id == kFireMinDamage || id == kLightningMinDamage || id == kMagicMinDamage ? 2
                                : id == kColdMinDamage || id == kPoisonMinDamage                                                   ? 3
                                                                                                                                   : 1;
        writer.write(std::uint32_t(id), 9);
        for (std::size_t k = 0; k < run && i < count; ++k, ++i) {
            if (props[i].stat < 0 || std::size_t(props[i].stat) >= item_tables.stats.size() || !item_tables.stats[std::size_t(props[i].stat)].save_bits)
                throw std::runtime_error("d2s write: stat " + std::to_string(props[i].stat) + " can't be saved");
            const auto& stat_info = item_tables.stats[std::size_t(props[i].stat)];
            if (stat_info.param_bits) writer.write(std::uint32_t(props[i].param), stat_info.param_bits);
            writer.write(std::uint32_t(props[i].value + stat_info.save_add), stat_info.save_bits);
        }
    }
    writer.write(0x1ff, 9);
}

inline void write_item(BitWriter& writer, const Item& original, const ItemTables& item_tables) {
    writer.write(0x4d4a, 16);
    Item item = original;
    if (item_tables.compact.contains(item.code)) item.simple = true;   // potions, scrolls, gems, runes: always short
    const bool ear = item.code == "ear";
    std::uint32_t flags = item.flags;
    auto flag = [&](int bit, bool set) { flags = set ? flags | 1u << bit : flags & ~(1u << bit); };
    flag(4, item.identified); flag(11, item.socketed); flag(16, ear); flag(21, item.simple);
    flag(22, item.ethereal); flag(24, item.personalized); flag(26, item.runeword);
    flags |= 1u << 23;                                        // set on every item in the real saves
    writer.write(flags, 32);
    writer.write(std::uint32_t(item.version), 10);
    writer.write(std::uint32_t(item.location), 3); writer.write(std::uint32_t(item.slot), 4);
    writer.write(std::uint32_t(item.column), 4); writer.write(std::uint32_t(item.row), 4); writer.write(std::uint32_t(item.panel), 3);
    if (ear) {
        writer.write(std::uint32_t(item.ear_class), 3); writer.write(std::uint32_t(item.ear_level), 7);
        for (const char letter : item.owner) writer.write(std::uint8_t(letter), 7);
        writer.write(0, 7);
    } else {
        for (std::size_t i = 0; i < 4; ++i) writer.write(i < item.code.size() ? std::uint8_t(item.code[i]) : std::uint8_t(' '), 8);
        writer.write(std::uint32_t(item.socketed_items.size()), 3);
        if (!item.simple) {
            writer.write(item.uid, 32); writer.write(std::uint32_t(item.ilvl), 7); writer.write(std::uint32_t(item.quality), 4);
            writer.write(item.picture >= 0, 1); if (item.picture >= 0) writer.write(std::uint32_t(item.picture), 3);
            writer.write(item.class_affix >= 0, 1); if (item.class_affix >= 0) writer.write(std::uint32_t(item.class_affix), 11);
            switch (item.quality) {
                case 1: case 3: writer.write(std::uint32_t(item.qsub), 3); break;
                case 4: writer.write(std::uint32_t(item.prefix), 11); writer.write(std::uint32_t(item.suffix), 11); break;
                case 5: writer.write(std::uint32_t(item.set_id), 12); break;
                case 7: writer.write(std::uint32_t(item.unique_id), 12); break;
                case 6: case 8:
                    writer.write(std::uint32_t(item.rare1), 8); writer.write(std::uint32_t(item.rare2), 8);
                    for (const int affix : item.affixes) { writer.write(affix != 0, 1); if (affix) writer.write(std::uint32_t(affix), 11); }
                    break;
                default: break;
            }
            if (item.runeword) { writer.write(std::uint32_t(item.runeword_id), 12); writer.write(std::uint32_t(item.rw_extra), 4); }
            if (item.personalized) { for (const char letter : item.owner) writer.write(std::uint8_t(letter), 7); writer.write(0, 7); }
            if (item.code == "tbk" || item.code == "ibk") writer.write(std::uint32_t(std::max(item.tome, 0)), 5);
            writer.write(std::uint32_t(item.bit_after), 1);
            if (item_tables.armor.contains(item.code)) writer.write(std::uint32_t(item.defense + 10), 11);
            if (item_tables.armor.contains(item.code) || item_tables.weapons.contains(item.code)) {
                writer.write(std::uint32_t(item.max_durability), 8);
                if (item.max_durability) { writer.write(std::uint32_t(item.durability), 8); writer.write(std::uint32_t(item.dur_extra), 1); }
            }
            if (item_tables.stackable.contains(item.code)) writer.write(std::uint32_t(std::max(item.quantity, 0)), 9);
            if (item.socketed) writer.write(std::uint32_t(item.sockets), 4);
            if (item.quality == 5) writer.write(std::uint32_t(item.set_lists), 5);
            // Items made in d2d have no runeword list: all of props is the main one.
            const std::size_t main = item.runeword ? std::min(item.main_props, item.props.size()) : item.props.size();
            write_props(writer, item_tables, item.props.data(), main);
            std::size_t offset = 0;
            for (const auto count : item.set_list_sizes) { write_props(writer, item_tables, item.set_props.data() + offset, count); offset += count; }
            if (item.runeword) write_props(writer, item_tables, item.props.data() + main, item.props.size() - main);
        }
    }
    writer.align();
    for (const auto& socketed : item.socketed_items) write_item(writer, socketed, item_tables);
}
}  // namespace detail

// The checksum at +0x0C: over the whole file with that field zeroed, each
// byte added to the sum rotated left by one bit.
inline std::uint32_t save_checksum(std::span<const std::byte> bytes) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::uint32_t value = i >= 0x0C && i < 0x10 ? 0 : std::uint32_t(std::uint8_t(bytes[i]));
        sum = (sum << 1 | sum >> 31) + value;
    }
    return sum;
}

// The whole file. `original`: the save as read (empty for a character made
// in d2d); its unknown header bytes and trailing lists are kept.
// `corpse`: the corpse's items to write in the corpse list (an empty vector:
// none), or nullptr to keep the original's.
inline std::vector<std::byte> write_save(std::span<const std::byte> original, const Header& header, const Stats& stats,
                                         const std::vector<Item>& items, const ItemTables& item_tables,
                                         const std::vector<Item>* corpse = nullptr) {
    constexpr std::size_t kGf = 0x2FD;
    std::vector<std::byte> bytes(kGf, std::byte{ 0 });
    const bool has_template = original.size() >= kGf;
    if (has_template) std::memcpy(bytes.data(), original.data(), kGf);
    auto w32 = [&](std::size_t off, std::uint32_t value) { std::memcpy(bytes.data() + off, &value, 4); };
    auto w16 = [&](std::size_t off, std::uint16_t value) { std::memcpy(bytes.data() + off, &value, 2); };
    w32(0x00, kMagic);
    w32(0x04, header.version ? header.version : kMaxVersion);
    std::memset(bytes.data() + 0x14, 0, 16);
    std::memcpy(bytes.data() + 0x14, header.name.data(), std::min<std::size_t>(header.name.size(), 15));
    bytes[0x24] = std::byte(header.status); bytes[0x25] = std::byte(header.progression);
    bytes[0x28] = std::byte(header.cls);    bytes[0x2B] = std::byte(header.level);
    w32(0x30, header.last_played);
    for (std::size_t i = 0; i < 16; ++i) w32(0x38 + i * 4, header.hotkeys[i]);
    w32(0x78, header.left_skill); w32(0x7C, header.right_skill); w32(0x80, header.left_swap); w32(0x84, header.right_swap);
    std::memcpy(bytes.data() + 0x88, header.appearance.data(), 16);
    std::memcpy(bytes.data() + 0x98, header.tints.data(), 16);
    std::memcpy(bytes.data() + 0xA8, header.difficulty.data(), 3);
    w32(0xAB, header.map_id);
    w16(0xB1, header.merc_dead ? 1 : 0); w32(0xB3, header.merc_seed); w16(0xB7, header.merc_name); w16(0xB9, header.merc_type); w32(0xBB, header.merc_exp);
    // Quests and waypoints: their blocks' markers, then the flags.
    std::memcpy(bytes.data() + 0x14F, "Woo!", 4); w32(0x153, 6); w16(0x157, 298);
    for (std::size_t difficulty = 0; difficulty < 3; ++difficulty) std::memcpy(bytes.data() + 0x159 + difficulty * 96, header.quests[difficulty].data(), 96);
    std::memcpy(bytes.data() + 0x279, "WS", 2);
    if (!has_template) { w32(0x27B, 1); w16(0x27F, 80); }
    for (std::size_t difficulty = 0; difficulty < 3; ++difficulty) {
        if (!has_template) { bytes[0x281 + difficulty * 24] = std::byte{ 2 }; bytes[0x282 + difficulty * 24] = std::byte{ 1 }; }
        std::memcpy(bytes.data() + 0x283 + difficulty * 24, header.waypoints[difficulty].data(), 5);
    }
    if (!has_template) { bytes[0x2C9] = std::byte{ 0x01 }; std::memcpy(bytes.data() + 0x2CA, "w4", 2); bytes[0x2CC] = std::byte{ 0x34 }; }

    // Stats: those that aren't zero, by id, in their CSvBits widths; the
    // level always.
    detail::BitWriter writer;
    writer.write('g', 8); writer.write('f', 8);
    for (int id = 0; id < 16; ++id) {
        if (stats.values[std::size_t(id)] == 0 && id != kLevel) continue;
        if (std::size_t(id) >= item_tables.stats.size() || !item_tables.stats[std::size_t(id)].csv_bits) continue;
        writer.write(std::uint32_t(id), 9);
        writer.write(std::uint32_t(stats.values[std::size_t(id)]), item_tables.stats[std::size_t(id)].csv_bits);
    }
    writer.write(0x1ff, 9);
    writer.align();
    bytes.insert(bytes.end(), writer.out.begin(), writer.out.end());
    bytes.push_back(std::byte{ 'i' }); bytes.push_back(std::byte{ 'f' });
    for (const auto skill_level : stats.skills) bytes.push_back(std::byte(skill_level));

    // The player's items.
    bytes.push_back(std::byte{ 'J' }); bytes.push_back(std::byte{ 'M' });
    bytes.push_back(std::byte(items.size() & 0xff)); bytes.push_back(std::byte(items.size() >> 8));
    detail::BitWriter item_writer;
    for (const auto& item : items) detail::write_item(item_writer, item, item_tables);
    bytes.insert(bytes.end(), item_writer.out.begin(), item_writer.out.end());

    // The rest as it was: where the original's player list ended.
    std::size_t tail = original.size();
    if (has_template) {
        try {
            const auto ost = parse_stats(original, item_tables);
            const int count = int(std::uint8_t(original[ost.items_at + 2])) | int(std::uint8_t(original[ost.items_at + 3])) << 8;
            detail::Bits bits{ original, (ost.items_at + 4) * 8 };
            for (int i = 0; i < count; ++i) detail::item(bits, item_tables);
            tail = bits.pos / 8;
        } catch (const std::runtime_error&) { tail = original.size(); }   // a header-only template: nothing after it
    }
    auto corpse_list = [&] {                                 // "JM" <count>, then 12 bytes and its items
        bytes.push_back(std::byte{ 'J' }); bytes.push_back(std::byte{ 'M' });
        bytes.push_back(std::byte(corpse->empty() ? 0 : 1)); bytes.push_back(std::byte{ 0 });
        if (corpse->empty()) return;
        for (int i = 0; i < 12; ++i) bytes.push_back(std::byte{ 0 });
        bytes.push_back(std::byte{ 'J' }); bytes.push_back(std::byte{ 'M' });
        bytes.push_back(std::byte(corpse->size() & 0xff)); bytes.push_back(std::byte(corpse->size() >> 8));
        detail::BitWriter corpse_writer;
        for (const auto& item : *corpse) detail::write_item(corpse_writer, item, item_tables);
        bytes.insert(bytes.end(), corpse_writer.out.begin(), corpse_writer.out.end());
    };
    if (tail < original.size() && corpse) {                 // the corpse list anew, the rest as it was
        std::size_t after = tail;
        try {
            const auto parsed_corpse = parse_corpse(original, item_tables);
            after = parsed_corpse.end;
        } catch (const std::runtime_error&) {}
        corpse_list();
        bytes.insert(bytes.end(), original.begin() + std::ptrdiff_t(after), original.end());
    } else if (tail < original.size()) bytes.insert(bytes.end(), original.begin() + std::ptrdiff_t(tail), original.end());
    else {                                                   // fresh: no merc items; no golem
        if (corpse) corpse_list();
        else for (const char letter : { 'J', 'M', '\0', '\0' }) bytes.push_back(std::byte(letter));
        if (header.expansion()) {
            bytes.push_back(std::byte{ 'j' }); bytes.push_back(std::byte{ 'f' });
            if (header.merc_seed) for (const char letter : { 'J', 'M', '\0', '\0' }) bytes.push_back(std::byte(letter));
            for (const char letter : { 'k', 'f', '\0' }) bytes.push_back(std::byte(letter));
        }
    }
    w32(0x08, std::uint32_t(bytes.size()));
    w32(0x0C, 0);
    w32(0x0C, save_checksum(bytes));
    return bytes;
}

}  // namespace d2d::d2s
