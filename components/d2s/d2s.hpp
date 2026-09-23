// D2Decomp .d2s character-save header parser.
//
// Reads only the fixed header the char-select screen needs: name, class,
// level, status flags. Layout per the Phrozen Keep d2s spec, stable from
// 1.09 (version 92) through 1.14d (version 96):
//   +0x00 u32  magic 0xAA55AA55
//   +0x04 u32  version
//   +0x08 u32  file size
//   +0x0C u32  checksum
//   +0x14 char name[16], NUL-padded
//   +0x24 u8   status  (0x04 hardcore, 0x08 died, 0x20 expansion)
//   +0x25 u8   progression (acts/difficulties beaten; drives the title)
//   +0x28 u8   class   (0 AM, 1 SO, 2 NE, 3 PA, 4 BA, 5 DZ, 6 AS)
//   +0x2B u8   level
//   +0x30 u32  last-played time (unix seconds) — char-select sort key
//   +0x88 u8   appearance[16]: per composite layer (HD TR LG RA LA RH LH
//              SH S1..S8), an index into D2's component table (see
//              components/compcode), 0xff = empty
//   +0x98 u8   tints[16]: per-layer item colormap, 0xff = none
// ponytail: header only. Stats/skills/items are bit-packed sections after
// 0x2FD; parse them when gameplay needs more than what char-select shows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

namespace d2d::d2s {

inline constexpr std::uint32_t kMagic = 0xAA55AA55;
// D2R (97+) moved the name field; pre-1.09 used a different header.
inline constexpr std::uint32_t kMinVersion = 92;
inline constexpr std::uint32_t kMaxVersion = 96;

struct Header {
    std::uint32_t version = 0;
    std::string   name;
    std::uint8_t  status = 0;
    std::uint8_t  progression = 0;
    std::uint8_t  cls = 0;      // .d2s class id, see table above
    std::uint8_t  level = 0;
    std::uint32_t last_played = 0;
    std::array<std::uint8_t, 16> appearance{};
    std::array<std::uint8_t, 16> tints{};

    [[nodiscard]] bool hardcore()  const noexcept { return status & 0x04; }
    [[nodiscard]] bool died()      const noexcept { return status & 0x08; }
    [[nodiscard]] bool expansion() const noexcept { return status & 0x20; }
};

// Throws std::runtime_error on anything that isn't a 1.09–1.14d save.
// Saves are user-supplied files: validate before trusting any field.
inline Header parse_header(std::span<const std::byte> b) {
    constexpr std::size_t kHeaderEnd = 0xA8;   // through tints[]
    if (b.size() < kHeaderEnd) throw std::runtime_error("d2s: truncated header");
    auto rd32 = [&](std::size_t off) {
        std::uint32_t v; std::memcpy(&v, b.data() + off, 4); return v;
    };
    if (rd32(0x00) != kMagic) throw std::runtime_error("d2s: bad magic");

    Header h;
    h.version = rd32(0x04);
    if (h.version < kMinVersion || h.version > kMaxVersion)
        throw std::runtime_error("d2s: unsupported version "
                                 + std::to_string(h.version));

    const auto* name = reinterpret_cast<const char*>(b.data() + 0x14);
    h.name.assign(name, strnlen(name, 16));
    if (h.name.empty()) throw std::runtime_error("d2s: empty name");

    h.status      = std::uint8_t(b[0x24]);
    h.progression = std::uint8_t(b[0x25]);
    h.cls         = std::uint8_t(b[0x28]);
    h.level       = std::uint8_t(b[0x2B]);
    h.last_played = rd32(0x30);
    std::memcpy(h.appearance.data(), b.data() + 0x88, 16);
    std::memcpy(h.tints.data(),      b.data() + 0x98, 16);
    if (h.cls > 6) throw std::runtime_error("d2s: bad class " + std::to_string(h.cls));
    return h;
}

}  // namespace d2d::d2s
