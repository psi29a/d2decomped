// Parse a synthetic 1.14d .d2s header; reject malformed ones.
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <mpq.hpp>
#include <compcode.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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

int main() {
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
            int saves = 0;
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
                assert(items.size() > 0 && equipped > 0);
                // Written back (d2s_write.hpp), the same bytes.
                const auto again = d2d::d2s::write_save(bytes, hdr, stats, items, item_tables);
                if (again.size() != bytes.size() || !std::equal(again.begin(), again.end(), bytes.begin())) {
                    std::size_t offset = 0;
                    while (offset < std::min(again.size(), bytes.size()) && again[offset] == bytes[offset]) ++offset;
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
                    assert(without.size() == bytes.size() && std::equal(without.begin(), without.end(), bytes.begin()));
                    assert(d2d::d2s::parse_items(with, item_tables).size() == items.size());
                }
                // Changed (gold, a level, an item moved, a new one), written
                // and read back: what was changed, and the rest intact.
                {
                    auto header2 = hdr; auto st2 = stats; auto items2 = items;
                    st2.values[d2d::d2s::kGold] = 12345; st2.values[d2d::d2s::kLevel] = header2.level = 97;
                    d2d::d2s::Item potion; potion.code = "hp1"; potion.simple = potion.identified = true; potion.location = 2; potion.column = 3;
                    items2.push_back(potion);
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
                d2d::d2s::Header fresh_header; fresh_header.name = "Fresh"; fresh_header.cls = 4; fresh_header.level = 1; fresh_header.status = 0x20;
                fresh_header.appearance.fill(0xff); fresh_header.tints.fill(0xff); fresh_header.difficulty = { 0x80, 0, 0 };
                d2d::d2s::Stats fresh_stats; fresh_stats.values[d2d::d2s::kStr] = 30; fresh_stats.values[d2d::d2s::kLevel] = 1; fresh_stats.values[d2d::d2s::kLife] = 55 << 8;
                d2d::d2s::Item axe; axe.code = "hax"; axe.location = 1; axe.slot = 4; axe.quality = 2; axe.ilvl = 1;
                axe.identified = true; axe.max_durability = axe.durability = 28;
                const auto written = d2d::d2s::write_save({}, fresh_header, fresh_stats, { axe }, item_tables);
                const auto reparsed = d2d::d2s::parse_header(written);
                const auto items = d2d::d2s::parse_items(written, item_tables);
                assert(reparsed.name == "Fresh" && reparsed.cls == 4 && reparsed.expansion() && reparsed.active_difficulty() == 0);
                assert(d2d::d2s::parse_stats(written, item_tables).get(d2d::d2s::kStr) == 30);
                assert(items.size() == 1 && items[0].code == "hax" && items[0].durability == 28 && items[0].slot == 4);
            }
            std::printf("items: %d real saves parsed\n", saves);
            assert(saves > 0);
        }
    }

    std::printf("OK\n");
    return 0;
}
