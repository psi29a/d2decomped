// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: d2s mpq compcode
// Parse a synthetic 1.14d .d2s header; reject malformed ones.
#include <character_name.hpp>
#include <compcode.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <mpq.hpp>
#include <txt.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> make_save(std::uint32_t version, const char* name,
                                 std::uint8_t status, std::uint8_t cls,
                                 std::uint8_t level) {
    std::vector<std::byte> bytes(0x2FD);   // real saves are at least this long
    auto wr32 = [&](std::size_t off, std::uint32_t value) { std::memcpy(bytes.data() + off, &value, 4); };
    wr32(0x00, d2d::d2s::kMagic);
    wr32(0x04, version);
    wr32(0x08, std::uint32_t(bytes.size()));
    std::memcpy(bytes.data() + 0x14, name, std::strlen(name));
    bytes[0x24] = std::byte(status);
    bytes[0x28] = std::byte(cls);
    bytes[0x2B] = std::byte(level);
    bytes[0x25] = std::byte{15};
    wr32(0x30, 1625730359u);
    for (int i = 0; i < 16; ++i) { bytes[0x88 + i] = std::byte(i); bytes[0x98 + i] = std::byte{0xff}; }
    for (int i = 0; i < 16; ++i) wr32(0x38 + std::size_t(i) * 4, 0xffff);
    wr32(0x38, 0x8000 | 254);                          // F1: Tiger Strike, to the left button
    wr32(0x78, 251); wr32(0x7C, 261); wr32(0x80, 0); wr32(0x84, 264);
    return bytes;
}

bool throws(const std::vector<std::byte>& bytes) {
    try { d2d::d2s::parse_header(bytes); } catch (const std::runtime_error&) { return true; }
    return false;
}

}  // namespace

int main() try {
    assert(d2d::d2s::character_name_allows("", 'A') && !d2d::d2s::character_name_allows("", '-'));
    assert(d2d::d2s::character_name_allows("A", '-') && d2d::d2s::character_name_allows("A", '_'));
    assert(!d2d::d2s::character_name_allows("A-B", '_') && !d2d::d2s::character_name_allows("A", '\''));
    assert(!d2d::d2s::character_name_allows("A", '1') && !d2d::d2s::character_name_allows("A", ' '));
    assert(d2d::d2s::valid_character_name("Ab") && d2d::d2s::valid_character_name("A-B")
           && d2d::d2s::valid_character_name("Mule_abcd") && d2d::d2s::valid_character_name("AAAAAAAAAAAAAAA"));
    assert(!d2d::d2s::valid_character_name("") && !d2d::d2s::valid_character_name("X")
           && !d2d::d2s::valid_character_name("AAAAAAAAAAAAAAAA") && !d2d::d2s::valid_character_name("_Mule")
           && !d2d::d2s::valid_character_name("Mule_") && !d2d::d2s::valid_character_name("a-b-c")
           && !d2d::d2s::valid_character_name("O'Neil") && !d2d::d2s::valid_character_name("Bob Bitchen")
           && !d2d::d2s::valid_character_name("A1"));

    // A game.exe host's ground item (S->C 0x9c, live 2026-10-04): gold, the
    // net layout (no JM, x / y 16 bits on the ground, the amount 1 + 12 bits
    // right after the code: FUN_0062a970 reads no socket count for it).
    {
        const std::uint8_t packet[] = { 0x9c, 0x00, 0x18, 0x10, 0xd3, 0x03, 0x00, 0x00, 0x10, 0x00, 0xa0, 0x00,
                                        0x65, 0x0c, 0x53, 0xc2, 0xc6, 0xe2, 0x8c, 0x8d, 0x0c, 0xc4, 0x0a, 0x00 };
        const auto net = d2d::d2s::parse_net_item(std::as_bytes(std::span(packet)).subspan(8), d2d::d2s::ItemTables{});
        assert(net && net->item.code == "gld" && net->x == 4760 && net->y == 5686 && net->gold == 43 && net->item.location == 3);
    }
    // Hardcore expansion Assassin, level 42, 15-char name (max + NUL pad).
    const auto header = d2d::d2s::parse_header(make_save(96, "Shadowdancerxyz", 0x24, 6, 42));
    assert(header.version == 96);
    assert(header.name == "Shadowdancerxyz");
    assert(header.cls == 6 && header.level == 42);
    assert(header.hardcore() && header.expansion() && !header.died());
    assert(header.progression == 15 && header.last_played == 1625730359u);
    assert(header.appearance[0] == 0 && header.appearance[5] == 5 && header.tints[3] == 0xff);
    assert(!header.quest_flag(0, 1, 0));                  // no "Woo!" block: no quests done
    assert(!header.waypoint(0, 0));                       // no "WS" block
    assert(header.hotkeys[0] == (0x8000u | 254) && header.hotkeys[1] == 0xffff);
    assert(header.left_skill == 251 && header.right_skill == 261 && header.left_swap == 0 && header.right_swap == 264);
    // The act: the low 7 bits of the played difficulty's byte; 5 or more is act 1 (FUN_0056a090).
    for (const auto& [byte, act] : { std::pair{ 0x83, 3 }, std::pair{ 0x84, 4 }, std::pair{ 0x85, 0 }, std::pair{ 0x8b, 0 } }) {
        d2d::d2s::Header played;
        played.difficulty = { std::uint8_t(byte), 0, 0 };
        assert(played.active_act() == act);
    }

    // Full 16 bytes with no NUL must not read past the field.
    auto full = make_save(96, "ABCDEFGHIJKLMNOP", 0, 0, 1);
    full[0x24] = std::byte{'Z'};
    assert(d2d::d2s::parse_header(full).name == "ABCDEFGHIJKLMNOP");

    auto bad_magic = make_save(96, "a", 0, 0, 1);
    bad_magic[0] = std::byte{0};
    assert(throws(bad_magic));
    assert(throws(make_save(97, "a", 0, 0, 1)));    // D2R layout
    assert(throws(make_save(71, "a", 0, 0, 1)));    // pre-1.09
    assert(throws(make_save(96, "a", 0, 7, 1)));    // no class 7
    assert(throws(make_save(96, "",  0, 0, 1)));
    assert(throws(std::vector<std::byte>(0x20)));   // truncated
    assert(throws(std::vector<std::byte>(0xA7)));   // cut inside tints[]

    // Item list: one hand-built simple item (a stack-less "hp1" potion in
    // the belt), bit for bit, then the real saves when they're around.
    {
        std::vector<std::byte> bytes(0x2FD + 4 + 14);
        bytes[4] = std::byte{ 0x60 };          // save version 96 (1.14d): the item reader's version checks
        std::size_t bit = (0x2FD + 4) * 8;
        auto put = [&](std::uint32_t value, int bits) {
            for (int i = 0; i < bits; ++i, ++bit)
                if (value >> i & 1) bytes[bit >> 3] |= std::byte(1 << (bit & 7));
        };
        std::memcpy(bytes.data() + 0x2FD, "JM\x01\x00", 4);
        put(0x4d4a, 16);
        put(1u << 21 | 1u << 4, 32);         // simple, identified
        put(101, 10);                        // item version
        put(2, 3); put(0, 4); put(3, 4); put(0, 4); put(0, 3);   // belt, col 3
        for (char letter : std::string("hp1 ")) put(std::uint8_t(letter), 8);
        put(0, 3);                           // no socketed items
        const auto items = d2d::d2s::parse_items(bytes, d2d::d2s::ItemTables{});
        assert(items.size() == 1);
        assert(items[0].code == "hp1" && items[0].simple && items[0].identified);
        assert(items[0].location == 2 && items[0].column == 3);
    }
    // A compact questdiffcheck item (FUN_0062a970): its difficulty (stat
    // 0x164, 2 bits) right after the code, then the realm bit; no socket
    // count. Read, then written back bit for bit.
    {
        std::vector<std::byte> bytes(0x2FD + 4 + 14);
        bytes[4] = std::byte{ 0x60 };          // save version 96 (1.14d): the item reader's version checks
        std::size_t bit = (0x2FD + 4) * 8;
        auto put = [&](std::uint32_t value, int bits) {
            for (int i = 0; i < bits; ++i, ++bit)
                if (value >> i & 1) bytes[bit >> 3] |= std::byte(1 << (bit & 7));
        };
        std::memcpy(bytes.data() + 0x2FD, "JM\x01\x00", 4);
        put(0x4d4a, 16);
        put(1u << 21 | 1u << 4, 32);
        put(101, 10);
        put(0, 3); put(0, 4); put(2, 4); put(1, 4); put(1, 3);   // inventory, 2, 1
        for (char letter : std::string("bks ")) put(std::uint8_t(letter), 8);
        put(2, 2);                           // Hell
        put(0, 1);                           // no realm data
        d2d::d2s::ItemTables item_tables;
        item_tables.quest_difficulty.insert("bks");
        item_tables.stats.resize(d2d::d2s::kQuestItemDifficulty + 1);
        item_tables.stats[d2d::d2s::kQuestItemDifficulty].save_bits = 2;
        const auto items = d2d::d2s::parse_items(bytes, item_tables);
        assert(items.size() == 1 && items[0].code == "bks" && items[0].quest_difficulty == 2 && items[0].socketed_items.empty());
        d2d::d2s::Item full_item;                                       // a full item keeps it in its props (stat 356)
        full_item.props.push_back({ .stat = 356, .value = 1 });
        assert(d2d::d2s::quest_difficulty_of(items[0]) == 2 && d2d::d2s::quest_difficulty_of(full_item) == 1 && d2d::d2s::quest_difficulty_of(d2d::d2s::Item{}) == -1);
        d2d::d2s::detail::BitWriter writer;
        d2d::d2s::detail::write_item(writer, items[0], item_tables);
        writer.align();
        auto expected = std::span(bytes).subspan(0x2FD + 4, writer.out.size());
        expected[4] |= std::byte{ 1 << 7 };  // write_item sets flag 23, as every real save has it
        assert(std::ranges::equal(writer.out, expected));
    }
    // An extended item's realm flag (FUN_0062cbe0, save version > 0x56): set,
    // three words follow past 0x5d, and the next item still starts on its
    // own "JM". Read, then written back bit for bit.
    {
        std::vector<std::byte> bytes(0x2FD + 4 + 64);
        bytes[4] = std::byte{ 0x60 };
        std::size_t bit = (0x2FD + 4) * 8;
        auto put = [&](std::uint32_t value, int bits) {
            for (int i = 0; i < bits; ++i, ++bit)
                if (value >> i & 1) bytes[bit >> 3] |= std::byte(1 << (bit & 7));
        };
        auto align = [&] { bit = (bit + 7) & ~std::size_t(7); };
        std::memcpy(bytes.data() + 0x2FD, "JM\x02\x00", 4);
        put(0x4d4a, 16);
        put(1u << 23 | 1u << 4, 32);         // extended, identified
        put(101, 10);
        put(0, 3); put(0, 4); put(2, 4); put(1, 4); put(1, 3);
        for (char letter : std::string("hax ")) put(std::uint8_t(letter), 8);
        put(0, 3);                           // no socketed items
        put(0x12345678, 32);                 // seed
        put(5, 7); put(2, 4);                // ilvl 5, normal
        put(0, 1); put(0, 1);                // no picture, no class affix
        put(1, 1);                           // realm data
        put(0xaaaaaaaa, 32); put(0xbbbbbbbb, 32); put(0xcccccccc, 32);
        put(0x1ff, 9);                       // no properties
        align();
        const std::size_t first_end = bit / 8;
        put(0x4d4a, 16);
        put(1u << 23 | 1u << 21 | 1u << 4, 32);
        put(101, 10);
        put(2, 3); put(0, 4); put(3, 4); put(0, 4); put(0, 3);
        for (char letter : std::string("hp1 ")) put(std::uint8_t(letter), 8);
        put(0, 1);                           // no realm data
        const auto items = d2d::d2s::parse_items(bytes, d2d::d2s::ItemTables{});
        assert(items.size() == 2 && items[0].code == "hax" && items[1].code == "hp1");
        assert((items[0].realm == std::vector<std::uint32_t>{ 0xaaaaaaaa, 0xbbbbbbbb, 0xcccccccc }));
        d2d::d2s::detail::BitWriter writer;
        d2d::d2s::detail::write_item(writer, items[0], d2d::d2s::ItemTables{}, 0x60);
        writer.align();
        assert(std::ranges::equal(writer.out, std::span(bytes).subspan(0x2FD + 4, first_end - (0x2FD + 4))));
    }
    // Durability is 8 bits up to save version 0x5f, 9 from 0x60 (FUN_0062cbe0):
    // a version 95 armour's next field starts right after the 8.
    {
        std::vector<std::byte> bytes(0x2FD + 4 + 32);
        bytes[4] = std::byte{ 95 };
        std::size_t bit = (0x2FD + 4) * 8;
        auto put = [&](std::uint32_t value, int bits) {
            for (int i = 0; i < bits; ++i, ++bit)
                if (value >> i & 1) bytes[bit >> 3] |= std::byte(1 << (bit & 7));
        };
        std::memcpy(bytes.data() + 0x2FD, "JM\x01\x00", 4);
        put(0x4d4a, 16);
        put(1u << 23 | 1u << 4, 32);
        put(101, 10);
        put(1, 3); put(1, 4); put(0, 4); put(0, 4); put(0, 3);   // worn on the head
        for (char letter : std::string("cap ")) put(std::uint8_t(letter), 8);
        put(0, 3); put(7, 32); put(1, 7); put(2, 4); put(0, 1); put(0, 1);
        put(0, 1);                           // no realm data
        put(13, 11);                         // defence 3
        put(12, 8); put(10, 8);              // max durability 12, durability 10: 8 bits
        put(0x1ff, 9);
        d2d::d2s::ItemTables item_tables;
        item_tables.armor.insert("cap");
        const auto items = d2d::d2s::parse_items(bytes, item_tables);
        assert(items.size() == 1 && items[0].defense == 3 && items[0].max_durability == 12 && items[0].durability == 10);
        d2d::d2s::detail::BitWriter writer;
        d2d::d2s::detail::write_item(writer, items[0], item_tables, 95);
        writer.align();
        assert(std::ranges::equal(writer.out, std::span(bytes).subspan(0x2FD + 4, writer.out.size())));
    }
    // A potion's quantity is not the save's 9-bit field unless the code is
    // stackable (d2s_write.hpp). hp1 is not, so a pile count adds no bits.
    // Marking it stackable inserts those 9 bits. No game files.
    {
        auto written = [](int quantity, bool stackable) {
            d2d::d2s::Item potion;
            potion.code = "hp1";
            potion.quality = 2;
            potion.quantity = quantity;
            d2d::d2s::ItemTables tables;
            if (stackable) tables.stackable.insert("hp1");
            d2d::d2s::detail::BitWriter writer;
            d2d::d2s::detail::write_item(writer, potion, tables);
            return writer;
        };
        // The property list ends in 0x1ff, so the last set bit is the end of
        // the item. Alignment zeros after that are not part of the field.
        auto payload_bits = [](const d2d::d2s::detail::BitWriter& writer) {
            std::size_t last = 0;
            for (std::size_t index = 0; index < writer.pos; ++index)
                if ((std::uint8_t(writer.out[index >> 3]) >> (index & 7)) & 1) last = index;
            return last + 1;
        };
        const auto plain = written(-1, false);
        const auto counted = written(50, false);
        assert(plain.out == counted.out);
        const auto stacked = written(50, true);
        assert(payload_bits(stacked) == payload_bits(plain) + 9);
    }
    {
        const char* saves_env = std::getenv("D2_SAVES_DIR");
        const char* mpq_env = std::getenv("D2_MPQ_DIR");
        const char* patch_env = std::getenv("D2_PATCH_INSTALLER");
        namespace fs = std::filesystem;
        const fs::path mpq_dir = mpq_env ? fs::path(mpq_env)
            : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / "Workspace/private/diablo2";
        if (!saves_env || !patch_env || !fs::exists(mpq_dir / "d2data.mpq")) {
            std::printf("SKIP real items: set D2_SAVES_DIR and D2_PATCH_INSTALLER\n");
        } else {
            d2d::mpq::Stack stack;
            stack.push_installer(patch_env);                // ItemStatCost.txt is 1.14d-only
            if (fs::exists(mpq_dir / "d2exp.mpq")) stack.push(mpq_dir / "d2exp.mpq");
            stack.push(mpq_dir / "d2data.mpq");
            auto tab = [&](const char* name) {
                return d2d::txt::Table(stack.read(std::string(R"(data\global\excel\)") + name + ".txt"));
            };
            const auto item_tables = d2d::d2s::ItemTables::from(tab("ItemStatCost"), tab("armor"),
                                                      tab("weapons"), tab("misc"));
            const auto comp = d2d::compcode::build(tab("ItemTypes"), tab("weapons"), tab("armor"), tab("misc"));
            const auto pcs = d2d::compcode::pieces(tab("weapons"), tab("armor"), tab("misc"));
            auto col = [&](const char* name, const char* column, bool all) {
                std::vector<std::string> values;
                const d2d::txt::Table table(stack.read(std::string(R"(data\global\excel\)") + name + ".txt"), all);
                for (std::size_t row = 0; row < table.size(); ++row) values.emplace_back(table.get(row, column));
                return values;
            };
            const d2d::compcode::Colours colours{ col("Colors", "Code", false), col("UniqueItems", "chrtransform", false),
                col("SetItems", "chrtransform", false), col("MagicPrefix", "transformcolor", true),
                col("MagicSuffix", "transformcolor", true), col("AutoMagic", "transformcolor", true),
                d2d::compcode::gem_colours(tab("ItemTypes"), tab("misc"), tab("gems")) };
            int saves = 0, merc_gear = 0;
            for (const auto& entry : fs::directory_iterator(saves_env)) {
                if (entry.path().extension() != ".d2s") continue;
                std::ifstream file(entry.path(), std::ios::binary);
                std::vector<char> raw{std::istreambuf_iterator<char>(file), {}};
                const auto bytes = std::as_bytes(std::span(raw));
                const auto stats = d2d::d2s::parse_stats(bytes, item_tables);
                const auto hdr = d2d::d2s::parse_header(bytes);
                assert(stats.get(d2d::d2s::kLevel) == hdr.level);      // two copies agree
                // Level 80+ characters: Den of Evil (1) and Andariel (6)
                // done in Normal; the flags past the last quest are clear.
                if (hdr.level >= 80) {
                    assert(hdr.quest_flag(0, 1, 0) && hdr.quest_flag(0, 6, 0));
                    assert(hdr.waypoint(0, 0) && hdr.waypoint(0, 1));    // town + Cold Plains
                }
                assert(stats.fixed(d2d::d2s::kMaxLife) > 0 && stats.get(d2d::d2s::kVit) > 0);
                assert(bytes[stats.items_at] == std::byte{'J'} && bytes[stats.items_at + 1] == std::byte{'M'});
                const auto items = d2d::d2s::parse_items(bytes, item_tables);
                // The merc's gear (after "jf"): worn on the head, torso or hands.
                if (hdr.merc_seed) {
                    const auto merc_items = d2d::d2s::parse_merc_items(bytes, item_tables);
                    for (const auto& item : merc_items) assert(item.location == 1 && (item.slot == 1 || item.slot == 3 || item.slot == 4 || item.slot == 5));
                    merc_gear += int(merc_items.size());
                }
                // The look, from what's worn, is the header's (compcode::look).
                {
                    std::vector<d2d::compcode::Worn> worn;
                    for (const auto& item : items)
                        if (item.location == 1)
                            worn.push_back({ item.slot, item.code, colours.of(item.quality, item.unique_id, item.set_id, item.prefix, item.suffix, item.affixes, item.class_affix,
                                                                       item.socketed && !item.socketed_items.empty() ? item.socketed_items[0].code : std::string{}) });
                    assert(d2d::compcode::look(comp, pcs, worn) == hdr.appearance);
                    // The tints too (compcode::tints).
                    const auto tints = d2d::compcode::tints(pcs, worn);
                    for (std::size_t layer = 0; layer < 16; ++layer)
                        if (tints[layer] != hdr.tints[layer]) std::printf("tint %s layer %zu: %02x, save %02x\n", entry.path().filename().string().c_str(), layer, tints[layer], hdr.tints[layer]);
                    assert(tints == hdr.tints);
                }
                int equipped = 0;
                for (const auto& item : items) {
                    assert(!item.code.empty());
                    equipped += item.location == 1;
                    if (item.quality == 4) assert(item.prefix || item.suffix);     // magic: named by affix
                    if (item.quality == 6 || item.quality == 8) assert(item.rare1 && item.rare2);
                    if (item.quality == 5) assert(item.set_id >= 0);
                }
                assert(items.size() > 0 && (equipped > 0 || d2d::d2s::parse_corpse(bytes, item_tables).has));   // a death leaves the worn on the corpse
                // Written back (d2s_write.hpp), the same bytes. d2d's own saves
                // from before d2d wrote game.exe's +0x34 (0xffffffff) mend.
                const auto again = d2d::d2s::write_save(bytes, hdr, stats, items, item_tables);
                std::vector<std::byte> expected(bytes.begin(), bytes.end());
                std::memset(expected.data() + 0x34, 0xff, 4);
                const std::uint32_t checksum = d2d::d2s::save_checksum(expected);
                std::memcpy(expected.data() + 0x0C, &checksum, 4);
                if (again.size() != expected.size() || !std::equal(again.begin(), again.end(), expected.begin())) {
                    std::size_t offset = 0;
                    while (offset < std::min(again.size(), expected.size()) && again[offset] == expected[offset]) ++offset;
                    std::printf("%s: written back differs at 0x%zx (sizes %zu / %zu)\n", entry.path().filename().string().c_str(),
                                offset, again.size(), bytes.size());
                    assert(false);
                }
                // A corpse holding the worn items, written and read back; and
                // taken away again: the save as it was.
                {
                    const auto had = d2d::d2s::parse_corpse(bytes, item_tables);
                    std::vector<d2d::d2s::Item> worn;
                    for (const auto& item : items) if (item.location == 1) worn.push_back(item);
                    const auto with = d2d::d2s::write_save(bytes, hdr, stats, items, item_tables, &worn);
                    const auto corpse = d2d::d2s::parse_corpse(with, item_tables);
                    assert(corpse.has == !worn.empty() && corpse.items.size() == worn.size());
                    for (std::size_t k = 0; k < worn.size(); ++k) assert(corpse.items[k].code == worn[k].code);
                    const std::vector<d2d::d2s::Item> none;
                    const auto without = d2d::d2s::write_save(with, hdr, stats, items, item_tables, had.has ? &had.items : &none);
                    assert(without.size() == expected.size() && std::equal(without.begin(), without.end(), expected.begin()));
                    assert(d2d::d2s::parse_items(with, item_tables).size() == items.size());
                }
                // Changed (gold, a level, an item moved, a new one), written
                // and read back: what was changed, and the rest intact.
                {
                    auto header2 = hdr; auto st2 = stats; auto items2 = items;
                    st2.values[d2d::d2s::kGold] = 12345; st2.values[d2d::d2s::kLevel] = header2.level = 97;
                    d2d::d2s::Item potion; potion.code = "hp1"; potion.simple = potion.identified = true; potion.location = 2; potion.column = 3;
                    items2.push_back(std::move(potion));
                    const auto written = d2d::d2s::write_save(bytes, header2, st2, items2, item_tables);
                    const auto back = d2d::d2s::parse_items(written, item_tables);
                    const auto st3 = d2d::d2s::parse_stats(written, item_tables);
                    assert(back.size() == items.size() + 1 && back.back().code == "hp1" && back.back().column == 3);
                    assert(st3.get(d2d::d2s::kGold) == 12345 && d2d::d2s::parse_header(written).level == 97);
                    assert(d2d::d2s::save_checksum(written) == [&] { std::uint32_t stored; std::memcpy(&stored, written.data() + 0x0C, 4); return stored; }());
                }
                ++saves;
            }
            // A character made in d2d (no file yet): a fresh header.
            {
                d2d::d2s::Header fresh_header; fresh_header.name = "Fresh"; fresh_header.cls = std::uint8_t(d2d::d2s::kBarbarian); fresh_header.level = 1; fresh_header.status = 0x20;
                fresh_header.appearance.fill(0xff); fresh_header.tints.fill(0xff); fresh_header.difficulty = { 0x80, 0, 0 };
                d2d::d2s::Stats fresh_stats; fresh_stats.values[d2d::d2s::kStr] = 30; fresh_stats.values[d2d::d2s::kLevel] = 1; fresh_stats.values[d2d::d2s::kLife] = 55 << 8;
                d2d::d2s::Item axe; axe.code = "hax"; axe.location = 1; axe.slot = 4; axe.quality = 2; axe.ilvl = 1;
                axe.identified = true; axe.max_durability = axe.durability = 28;
                const auto written = d2d::d2s::write_save({}, fresh_header, fresh_stats, { axe }, item_tables);
                const auto reparsed = d2d::d2s::parse_header(written);
                assert(std::uint8_t(written[0x34]) == 0xff && std::uint8_t(written[0x37]) == 0xff);   // FUN_00568f20's constant
                const auto items = d2d::d2s::parse_items(written, item_tables);
                assert(reparsed.name == "Fresh" && reparsed.cls == 4 && reparsed.expansion() && reparsed.active_difficulty() == 0);
                assert(d2d::d2s::parse_stats(written, item_tables).get(d2d::d2s::kStr) == 30);
                assert(items.size() == 1 && items[0].code == "hax" && items[0].durability == 28 && items[0].slot == 4);
                // game.exe steps over "if" by +0x2A (FUN_0056a710): 30 skill bytes, then "JM".
                assert(written[0x29] == std::byte{ 0x10 } && written[0x2A] == std::byte{ 0x1E });
                const auto if_at = std::ranges::search(written, std::array{ std::byte{ 'i' }, std::byte{ 'f' } }).begin() - written.begin();
                assert(written[std::size_t(if_at) + 2 + 0x1E] == std::byte{ 'J' } && written[std::size_t(if_at) + 3 + 0x1E] == std::byte{ 'M' });
                // Then a merc hired and dressed (issue #139): written over that
                // file, "jf" gets its "JM" list (FUN_005699a0), the golem's
                // "kf" stays after it; with no merc list given, it's kept.
                auto hired = std::move(fresh_header); hired.merc_seed = 0x5c096f0e; hired.merc_type = 8; hired.merc_name = 18;
                d2d::d2s::Item cap; cap.code = "cap"; cap.location = 1; cap.slot = 1; cap.quality = 2; cap.ilvl = 1;
                cap.identified = true; cap.defense = 3; cap.max_durability = cap.durability = 12;
                const std::vector<d2d::d2s::Item> merc_worn{ std::move(cap) };
                const auto with_merc = d2d::d2s::write_save(written, hired, fresh_stats, { axe }, item_tables, nullptr, &merc_worn);
                const auto merc_back = d2d::d2s::parse_merc(with_merc, item_tables);
                assert(merc_back.has && merc_back.items.size() == 1 && merc_back.items[0].code == "cap" && merc_back.items[0].slot == 1);
                assert(with_merc.size() == merc_back.end + 3 && with_merc[merc_back.end] == std::byte{ 'k' } && with_merc[merc_back.end + 1] == std::byte{ 'f' });
                const auto kept = d2d::d2s::write_save(with_merc, hired, fresh_stats, { axe }, item_tables);
                assert(d2d::d2s::parse_merc_items(kept, item_tables).size() == 1);
                const std::vector<d2d::d2s::Item> no_corpse;
                const auto undressed = d2d::d2s::write_save(with_merc, hired, fresh_stats, { axe }, item_tables, &no_corpse, &no_corpse);
                const auto bare = d2d::d2s::parse_merc(undressed, item_tables);
                assert(bare.has && bare.items.empty() && bare.end == bare.begin + 6 && undressed.size() == bare.end + 3);
            }
            std::printf("items: %d real saves parsed, %d merc items\n", saves, merc_gear);
            assert(saves > 0);
        }
    }

    std::printf("OK\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
