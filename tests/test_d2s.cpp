// Parse a synthetic 1.14d .d2s header; reject malformed ones.
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <mpq.hpp>

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
            d2d::mpq::Stack st;
            st.push_installer(pi);                // ItemStatCost.txt is 1.14d-only
            if (fs::exists(mpq_dir / "d2exp.mpq")) st.push(mpq_dir / "d2exp.mpq");
            st.push(mpq_dir / "d2data.mpq");
            auto tab = [&](const char* n) {
                return d2d::txt::Table(st.read(std::string(R"(data\global\excel\)") + n + ".txt"));
            };
            const auto t = d2d::d2s::ItemTables::from(tab("ItemStatCost"), tab("armor"),
                                                      tab("weapons"), tab("misc"));
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
                assert(hdr.quest_flag(0, 1, 0) && hdr.quest_flag(0, 6, 0));
                assert(hdr.waypoint(0, 0) && hdr.waypoint(0, 1));    // town + Cold Plains
                assert(st.fixed(d2d::d2s::kMaxLife) > 0 && st.get(d2d::d2s::kVit) > 0);
                assert(bytes[st.items_at] == std::byte{'J'} && bytes[st.items_at + 1] == std::byte{'M'});
                const auto items = d2d::d2s::parse_items(bytes, t);
                int equipped = 0;
                for (const auto& it : items) {
                    assert(!it.code.empty());
                    equipped += it.location == 1;
                    if (it.quality == 4) assert(it.prefix || it.suffix);     // magic: named by affix
                    if (it.quality == 6 || it.quality == 8) assert(it.rare1 && it.rare2);
                    if (it.quality == 5) assert(it.set_id >= 0);
                }
                assert(items.size() > 0 && equipped > 0);
                ++saves;
            }
            std::printf("items: %d real saves parsed\n", saves);
            assert(saves > 0);
        }
    }

    std::printf("OK\n");
    return 0;
}
