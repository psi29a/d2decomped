// Parse a synthetic 1.14d .d2s header; reject malformed ones.
#include <d2s.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>
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

    std::printf("OK\n");
    return 0;
}
