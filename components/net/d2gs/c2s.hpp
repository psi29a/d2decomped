// SPDX-License-Identifier: GPL-3.0-or-later
// The C->S packets a join needs (docs/research/re/net-join.md "Join",
// "Ping", "Leaving"). C->S has no frames and no compression: each packet
// goes out as its own bytes.
#pragma once

#include <d2gs/wire.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::net::d2gs::c2s {

inline constexpr std::uint32_t kVersion = 0x0e;      // FUN_0051ca10; the host refuses others with B4 0x10
inline constexpr std::uint16_t kTcpGameToken = 1;    // config +0x1d for a TCP join
inline constexpr std::size_t kJoinSize = 37;
inline constexpr std::size_t kSaveChunk = 0xff;      // the bytes of the save in one 0x6c

// 0x68 (FUN_00477f70, layout at 0x477f76). The dword at +1 and the two at
// +0xc / +0x10 aren't checked by a TCP host, so they go out as 0.
inline auto join_request(int character_class, std::string_view name, std::uint8_t language = 0) -> Bytes {
    Bytes out;
    put_u8(out, 0x68);
    put_u32(out, 0);
    put_u16(out, kTcpGameToken);
    put_u8(out, static_cast<std::uint8_t>(character_class));
    put_u32(out, kVersion);
    put_u32(out, 0);
    put_u32(out, 0);
    put_u8(out, language);
    put_name(out, name, 16);
    return out;
}

// The save, 0x6c a chunk: [6c, n, u32 total, n bytes, 0] (n + 7 bytes),
// all sent at once after the 0x68.
inline auto save_chunks(std::span<const std::uint8_t> save) -> std::vector<Bytes> {
    std::vector<Bytes> chunks;
    for (std::size_t offset = 0; offset < save.size(); offset += kSaveChunk) {
        const auto part = save.subspan(offset, std::min(kSaveChunk, save.size() - offset));
        Bytes out;
        put_u8(out, 0x6c);
        put_u8(out, static_cast<std::uint8_t>(part.size()));
        put_u32(out, static_cast<std::uint32_t>(save.size()));
        put_bytes(out, part);
        put_u8(out, 0);
        chunks.push_back(std::move(out));
    }
    return chunks;
}

// 0x6b: the answer to S->C 0x02 (FUN_00477da0).
inline auto load_ack() -> Bytes { return { 0x6b }; }

// 0x6d (FUN_00477dd0), at most once every 5 s: the tick, a timer value
// (0 here) and the anti-cheat dword, which a TCP host doesn't check.
inline auto ping(std::uint32_t tick) -> Bytes {
    Bytes out;
    put_u8(out, 0x6d);
    put_u32(out, tick);
    put_u32(out, 0);
    put_u32(out, 0);
    return out;
}

// 0x01 / 0x03: walk / run to act subtile (x, y) (FUN_005497e0 / 0x5498d0
// on the host; one the bounds check refuses is answered after 25 frames).
inline auto move_to(std::uint16_t x, std::uint16_t y, bool run) -> Bytes {
    Bytes out;
    put_u8(out, run ? 0x03 : 0x01);
    put_u16(out, x);
    put_u16(out, y);
    return out;
}

// 0x53 / 0x54: run / walk from now on.
inline auto set_running(bool run) -> Bytes { return { static_cast<std::uint8_t>(run ? 0x53 : 0x54) }; }

// 0x69: leave the game; the host answers with the save (B3), B0, 05, 06.
inline auto leave() -> Bytes { return { 0x69 }; }

} // namespace d2d::net::d2gs::c2s
