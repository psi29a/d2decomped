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
    std::vector<std::byte> b(0x2FD);   // real saves are at least this long
    auto wr32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(b.data() + off, &v, 4); };
    wr32(0x00, d2d::d2s::kMagic);
    wr32(0x04, version);
    wr32(0x08, std::uint32_t(b.size()));
    std::memcpy(b.data() + 0x14, name, std::strlen(name));
    b[0x24] = std::byte(status);
    b[0x28] = std::byte(cls);
    b[0x2B] = std::byte(level);
    b[0x25] = std::byte{15};
    wr32(0x30, 1625730359u);
    for (int i = 0; i < 16; ++i) { b[0x88 + i] = std::byte(i); b[0x98 + i] = std::byte{0xff}; }
    for (int i = 0; i < 16; ++i) wr32(0x38 + std::size_t(i) * 4, 0xffff);
    wr32(0x38, 0x8000 | 254);                          // F1: Tiger Strike, to the left button
    wr32(0x78, 251); wr32(0x7C, 261); wr32(0x80, 0); wr32(0x84, 264);
    return b;
}

bool throws(const std::vector<std::byte>& b) {
    try { d2d::d2s::parse_header(b); } catch (const std::runtime_error&) { return true; }
    return false;
}

}  // namespace

int main() {
    // Hardcore expansion Assassin, level 42, 15-char name (max + NUL pad).
    const auto h = d2d::d2s::parse_header(make_save(96, "Shadowdancerxyz", 0x24, 6, 42));
    assert(h.version == 96);
    assert(h.name == "Shadowdancerxyz");
    assert(h.cls == 6 && h.level == 42);
    assert(h.hardcore() && h.expansion() && !h.died());
    assert(h.progression == 15 && h.last_played == 1625730359u);
    assert(h.appearance[0] == 0 && h.appearance[5] == 5 && h.tints[3] == 0xff);
    assert(!h.quest_flag(0, 1, 0));                  // no "Woo!" block: no quests done
    assert(!h.waypoint(0, 0));                       // no "WS" block
    assert(h.hotkeys[0] == (0x8000u | 254) && h.hotkeys[1] == 0xffff);
    assert(h.left_skill == 251 && h.right_skill == 261 && h.left_swap == 0 && h.right_swap == 264);

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
        std::vector<std::byte> b(0x2FD + 4 + 14);
        std::size_t bit = (0x2FD + 4) * 8;
        auto put = [&](std::uint32_t v, int n) {
            for (int i = 0; i < n; ++i, ++bit)
                if (v >> i & 1) b[bit >> 3] |= std::byte(1 << (bit & 7));
        };
        std::memcpy(b.data() + 0x2FD, "JM\x01\x00", 4);
        put(0x4d4a, 16);
        put(1u << 21 | 1u << 4, 32);         // simple, identified
        put(101, 10);                        // item version
        put(2, 3); put(0, 4); put(3, 4); put(0, 4); put(0, 3);   // belt, col 3
        for (char c : std::string("hp1 ")) put(std::uint8_t(c), 8);
        put(0, 3);                           // no socketed items
        const auto items = d2d::d2s::parse_items(b, d2d::d2s::ItemTables{});
        assert(items.size() == 1);
        assert(items[0].code == "hp1" && items[0].simple && items[0].identified);
        assert(items[0].location == 2 && items[0].column == 3);
    }
    {
        const char* sd = std::getenv("D2_SAVES_DIR");
        const char* md = std::getenv("D2_MPQ_DIR");
        const char* pi = std::getenv("D2_PATCH_INSTALLER");
        namespace fs = std::filesystem;
        const fs::path mpq_dir = md ? fs::path(md)
            : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / "Workspace/private/diablo2";
        if (!sd || !pi || !fs::exists(mpq_dir / "d2data.mpq")) {
            std::printf("SKIP real items: set D2_SAVES_DIR and D2_PATCH_INSTALLER\n");
        } else {
            d2d::mpq::Stack stack;
            stack.push_installer(pi);                // ItemStatCost.txt is 1.14d-only
            if (fs::exists(mpq_dir / "d2exp.mpq")) stack.push(mpq_dir / "d2exp.mpq");
            stack.push(mpq_dir / "d2data.mpq");
            auto tab = [&](const char* n) {
                return d2d::txt::Table(stack.read(std::string(R"(data\global\excel\)") + n + ".txt"));
            };
            const auto t = d2d::d2s::ItemTables::from(tab("ItemStatCost"), tab("armor"),
                                                      tab("weapons"), tab("misc"));
            const auto comp = d2d::compcode::build(tab("ItemTypes"), tab("weapons"), tab("armor"), tab("misc"));
            const auto pcs = d2d::compcode::pieces(tab("weapons"), tab("armor"), tab("misc"));
            auto col = [&](const char* n, const char* c, bool all) {
                std::vector<std::string> v;
                const d2d::txt::Table tb(stack.read(std::string(R"(data\global\excel\)") + n + ".txt"), all);
                for (std::size_t r = 0; r < tb.size(); ++r) v.emplace_back(tb.get(r, c));
                return v;
            };
            const d2d::compcode::Colours colours{ col("Colors", "Code", false), col("UniqueItems", "chrtransform", false),
                col("SetItems", "chrtransform", false), col("MagicPrefix", "transformcolor", true),
                col("MagicSuffix", "transformcolor", true), col("AutoMagic", "transformcolor", true),
                d2d::compcode::gem_colours(tab("ItemTypes"), tab("misc"), tab("gems")) };
            int saves = 0;
            for (const auto& e : fs::directory_iterator(sd)) {
                if (e.path().extension() != ".d2s") continue;
                std::ifstream in(e.path(), std::ios::binary);
                std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
                const auto bytes = std::as_bytes(std::span(raw));
                const auto st = d2d::d2s::parse_stats(bytes, t);
                const auto hdr = d2d::d2s::parse_header(bytes);
                assert(st.get(d2d::d2s::kLevel) == hdr.level);      // two copies agree
                // Level 80+ characters: Den of Evil (1) and Andariel (6)
                // done in Normal; the flags past the last quest are clear.
                if (hdr.level >= 80) {
                    assert(hdr.quest_flag(0, 1, 0) && hdr.quest_flag(0, 6, 0));
                    assert(hdr.waypoint(0, 0) && hdr.waypoint(0, 1));    // town + Cold Plains
                }
                assert(st.fixed(d2d::d2s::kMaxLife) > 0 && st.get(d2d::d2s::kVit) > 0);
                assert(bytes[st.items_at] == std::byte{'J'} && bytes[st.items_at + 1] == std::byte{'M'});
                const auto items = d2d::d2s::parse_items(bytes, t);
                // The look, from what's worn, is the header's (compcode::look).
                {
                    std::vector<d2d::compcode::Worn> worn;
                    for (const auto& it : items)
                        if (it.location == 1)
                            worn.push_back({ it.slot, it.code, colours.of(it.quality, it.unique_id, it.set_id, it.prefix, it.suffix, it.affixes, it.class_affix,
                                                                       it.socketed && !it.socketed_items.empty() ? it.socketed_items[0].code : std::string{}) });
                    assert(d2d::compcode::look(comp, pcs, worn) == hdr.appearance);
                    // The tints too (compcode::tints).
                    const auto tn = d2d::compcode::tints(pcs, worn);
                    for (std::size_t l = 0; l < 16; ++l)
                        if (tn[l] != hdr.tints[l]) std::printf("tint %s layer %zu: %02x, save %02x\n", e.path().filename().string().c_str(), l, tn[l], hdr.tints[l]);
                    assert(tn == hdr.tints);
                }
                int equipped = 0;
                for (const auto& it : items) {
                    assert(!it.code.empty());
                    equipped += it.location == 1;
                    if (it.quality == 4) assert(it.prefix || it.suffix);     // magic: named by affix
                    if (it.quality == 6 || it.quality == 8) assert(it.rare1 && it.rare2);
                    if (it.quality == 5) assert(it.set_id >= 0);
                }
                assert(items.size() > 0 && equipped > 0);
                // Written back (d2s_write.hpp), the same bytes.
                const auto again = d2d::d2s::write_save(bytes, hdr, st, items, t);
                if (again.size() != bytes.size() || !std::equal(again.begin(), again.end(), bytes.begin())) {
                    std::size_t at = 0;
                    while (at < std::min(again.size(), bytes.size()) && again[at] == bytes[at]) ++at;
                    std::printf("%s: written back differs at 0x%zx (sizes %zu / %zu)\n", e.path().filename().string().c_str(),
                                at, again.size(), bytes.size());
                    assert(false);
                }
                // A corpse holding the worn items, written and read back; and
                // taken away again: the save as it was.
                {
                    const auto had = d2d::d2s::parse_corpse(bytes, t);
                    std::vector<d2d::d2s::Item> worn;
                    for (const auto& it : items) if (it.location == 1) worn.push_back(it);
                    const auto with = d2d::d2s::write_save(bytes, hdr, st, items, t, &worn);
                    const auto cl = d2d::d2s::parse_corpse(with, t);
                    assert(cl.has == !worn.empty() && cl.items.size() == worn.size());
                    for (std::size_t k = 0; k < worn.size(); ++k) assert(cl.items[k].code == worn[k].code);
                    const std::vector<d2d::d2s::Item> none;
                    const auto without = d2d::d2s::write_save(with, hdr, st, items, t, had.has ? &had.items : &none);
                    assert(without.size() == bytes.size() && std::equal(without.begin(), without.end(), bytes.begin()));
                    assert(d2d::d2s::parse_items(with, t).size() == items.size());
                }
                // Changed (gold, a level, an item moved, a new one), written
                // and read back: what was changed, and the rest intact.
                {
                    auto h2 = hdr; auto st2 = st; auto items2 = items;
                    st2.v[d2d::d2s::kGold] = 12345; st2.v[d2d::d2s::kLevel] = h2.level = 97;
                    d2d::d2s::Item hp; hp.code = "hp1"; hp.simple = hp.identified = true; hp.location = 2; hp.column = 3;
                    items2.push_back(hp);
                    const auto w = d2d::d2s::write_save(bytes, h2, st2, items2, t);
                    const auto back = d2d::d2s::parse_items(w, t);
                    const auto st3 = d2d::d2s::parse_stats(w, t);
                    assert(back.size() == items.size() + 1 && back.back().code == "hp1" && back.back().column == 3);
                    assert(st3.get(d2d::d2s::kGold) == 12345 && d2d::d2s::parse_header(w).level == 97);
                    assert(d2d::d2s::save_checksum(w) == [&] { std::uint32_t c; std::memcpy(&c, w.data() + 0x0C, 4); return c; }());
                }
                ++saves;
            }
            // A character made in d2d (no file yet): a fresh header.
            {
                d2d::d2s::Header fh; fh.name = "Fresh"; fh.cls = 4; fh.level = 1; fh.status = 0x20;
                fh.appearance.fill(0xff); fh.tints.fill(0xff); fh.difficulty = { 0x80, 0, 0 };
                d2d::d2s::Stats st; st.v[d2d::d2s::kStr] = 30; st.v[d2d::d2s::kLevel] = 1; st.v[d2d::d2s::kLife] = 55 << 8;
                d2d::d2s::Item axe; axe.code = "hax"; axe.location = 1; axe.slot = 4; axe.quality = 2; axe.ilvl = 1;
                axe.identified = true; axe.max_durability = axe.durability = 28;
                const auto w = d2d::d2s::write_save({}, fh, st, { axe }, t);
                const auto h2 = d2d::d2s::parse_header(w);
                const auto items = d2d::d2s::parse_items(w, t);
                assert(h2.name == "Fresh" && h2.cls == 4 && h2.expansion() && h2.active_difficulty() == 0);
                assert(d2d::d2s::parse_stats(w, t).get(d2d::d2s::kStr) == 30);
                assert(items.size() == 1 && items[0].code == "hax" && items[0].durability == 28 && items[0].slot == 4);
            }
            std::printf("items: %d real saves parsed\n", saves);
            assert(saves > 0);
        }
    }

    std::printf("OK\n");
    return 0;
}
