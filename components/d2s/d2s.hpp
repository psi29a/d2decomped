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
//   +0xB1 u16  mercenary dead, +0xB3 u32 its seed (0 = no merc), +0xB7 u16
//              name index, +0xB9 u16 type (hireling.txt Id), +0xBB u32 exp
//   +0xA8 u8   difficulty[3]: normal/nightmare/hell; 0x80 = the one the
//              character was last played on, low 3 bits = act
//   +0x14F "Woo!" u32 version (6) u16 size (298), then per difficulty 96
//              bytes of quest flags: 16 bits per quest, bit 0 = done —
//              the game's own flag block (FUN_0065c310 tests bit
//              quest*16 + n, LSB first; 1 Den of Evil .. 6 Andariel)
//   +0x279 "WS" + 6 bytes, then per difficulty 24 bytes: 02 01 and a
//              bitfield of activated waypoints (Levels.txt Waypoint
//              index, LSB first; 39 used)
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
    std::array<std::uint8_t, 3>  difficulty{};
    std::array<std::array<std::uint8_t, 96>, 3> quests{};   // zero when the save has none
    // The mercenary: seed 0 = none hired.
    bool merc_dead = false;
    std::uint32_t merc_seed = 0, merc_exp = 0;
    std::uint16_t merc_name = 0, merc_type = 0;
    [[nodiscard]] bool quest_flag(int diff, int quest, int bit) const noexcept {
        const int n = quest * 16 + bit;
        if (diff < 0 || diff > 2 || n < 0 || n >= 96 * 8) return false;
        return quests[std::size_t(diff)][std::size_t(n >> 3)] >> (n & 7) & 1;
    }
    std::array<std::array<std::uint8_t, 5>, 3> waypoints{};  // zero when the save has none
    [[nodiscard]] bool waypoint(int diff, int wp) const noexcept {
        if (diff < 0 || diff > 2 || wp < 0 || wp >= 40) return false;
        return waypoints[std::size_t(diff)][std::size_t(wp >> 3)] >> (wp & 7) & 1;
    }
    // 0 normal, 1 nightmare, 2 hell — the last one played.
    [[nodiscard]] int active_difficulty() const noexcept {
        for (int i = 0; i < 3; ++i) if (difficulty[std::size_t(i)] & 0x80) return i;
        return 0;
    }

    [[nodiscard]] bool hardcore()  const noexcept { return status & 0x04; }
    [[nodiscard]] bool died()      const noexcept { return status & 0x08; }
    [[nodiscard]] bool expansion() const noexcept { return status & 0x20; }
};

// Throws std::runtime_error on anything that isn't a 1.09–1.14d save.
// Saves are user-supplied files: validate before trusting any field.
inline Header parse_header(std::span<const std::byte> b) {
    constexpr std::size_t kHeaderEnd = 0xBF;   // through the merc fields
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
    std::memcpy(h.difficulty.data(), b.data() + 0xA8, 3);
    auto rd16 = [&](std::size_t off) { std::uint16_t v; std::memcpy(&v, b.data() + off, 2); return v; };
    h.merc_dead = rd16(0xB1) != 0;
    h.merc_seed = rd32(0xB3);
    h.merc_name = rd16(0xB7);
    h.merc_type = rd16(0xB9);
    h.merc_exp  = rd32(0xBB);
    if (b.size() >= 0x159 + 3 * 96 && std::memcmp(b.data() + 0x14F, "Woo!", 4) == 0)
        for (std::size_t d = 0; d < 3; ++d) std::memcpy(h.quests[d].data(), b.data() + 0x159 + d * 96, 96);
    if (b.size() >= 0x281 + 3 * 24 && std::memcmp(b.data() + 0x279, "WS", 2) == 0)
        for (std::size_t d = 0; d < 3; ++d) std::memcpy(h.waypoints[d].data(), b.data() + 0x283 + d * 24, 5);
    if (h.cls > 6) throw std::runtime_error("d2s: bad class " + std::to_string(h.cls));
    return h;
}

}  // namespace d2d::d2s
