// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp — Blizzard BNUpdate delta patches (the compressed entries of
// LODPatch_114d.exe and friends).
//
// RE'd from BNUpdate.exe (bnupdate\ptc.cpp, FUN_004072d0 and helpers). A
// patch entry is the 24-byte installer header followed by two streams:
//   header: u16 24 | u8 4 | u8 stored (0 = delta) | u32 CRC-32 of the
//           SOURCE file | u32 source size | u32 output size | u64 time
//   u32 len1 | u32 len2 | stream1[len1] | stream2[len2]
// The source is the same file as shipped in the base MPQs (d2data /
// d2exp), so one patch upgrades any 1.07+ install.
//
// stream1: ops, each a u16 (kind = top 2 bits, n = low 14 bits); a
// "cursor" walks the source:
//   0x4000 copy   — signed varint added to cursor, copy n source bytes
//   0x8000 dcopy  — signed varint added to cursor, n/2 words rebuilt as
//                   filtered_src[cursor] + previous output word
//   0x0000 insert — n literal bytes follow
//   0xC000 zeros  — n zero bytes
// every op advances the cursor by n. filtered_src is the source with each
// 16-bit word (from the end, 2-byte steps) minus the one before it.
// stream2: 16-bit additive fixups on the output: a signed value and its
// positions (first absolute, then deltas, 0-terminated), then groups of
// {value delta, positions...} until a 0 value delta.
// Varints are 1-4 bytes: <0x80 → 7 bits; 10xxxxxx → 14; 110xxxxx → 21;
// 111xxxxx → 29 bits (the signed form sign-extends from that width).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace d2d::mpq::bnpatch {

inline std::uint32_t crc32(std::span<const std::byte> bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int k = 0; k < 8; ++k) crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
            entries[i] = crc;
        }
        return entries;
    }();
    std::uint32_t crc = 0xffffffffu;
    for (auto x : bytes) crc = table[(crc ^ std::uint8_t(x)) & 0xff] ^ (crc >> 8);
    return ~crc;
}

namespace detail {
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint8_t u8() {
        if (at >= bytes.size()) throw 0;
        return std::uint8_t(bytes[at++]);
    }
    std::uint32_t uvar() {
        const std::uint32_t x = u8();
        if (x < 0x80) return x;
        if (!(x & 0x40)) return (x & 0x3f) | std::uint32_t(u8()) << 6;
        std::uint32_t value = (x & 0x1f) | std::uint32_t(u8()) << 5;
        value |= std::uint32_t(u8()) << 13;
        if (!(x & 0x20)) return value;
        return value | std::uint32_t(u8()) << 21;
    }
    std::int32_t svar() {
        const std::size_t start = at;
        const std::uint32_t x = std::uint8_t(bytes[start]);
        const std::uint32_t value = uvar();
        const int bits = x < 0x80 ? 7 : !(x & 0x40) ? 14 : !(x & 0x20) ? 21 : 29;
        return std::int32_t(value << (32 - bits)) >> (32 - bits);
    }
};
inline std::uint16_t rd16(const std::vector<std::byte>& bytes, std::size_t offset) {
    return std::uint16_t(std::uint8_t(bytes[offset]) | std::uint8_t(bytes[offset + 1]) << 8);
}
inline void wr16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = std::byte(value & 0xff); bytes[offset + 1] = std::byte(value >> 8);
}
}  // namespace detail

// The 24-byte header of an installer entry.
struct Header { bool stored = false; std::uint32_t crc = 0, src_size = 0, out_size = 0; };
inline std::optional<Header> header(std::span<const std::byte> entry) {
    if (entry.size() < 24 || std::uint8_t(entry[0]) != 24 || std::uint8_t(entry[1]) != 0) return std::nullopt;
    auto u32 = [&](std::size_t offset) { std::uint32_t value; std::memcpy(&value, entry.data() + offset, 4); return value; };
    return Header{ std::uint8_t(entry[3]) == 1, u32(4), u32(8), u32(12) };
}

// Rebuild the patched file from an installer entry and its source file.
// nullopt if the source isn't the one the patch was made against (size or
// CRC mismatch) or the streams are malformed.
inline std::optional<std::vector<std::byte>> apply(std::span<const std::byte> entry,
                                                   std::span<const std::byte> src) {
    using namespace detail;
    const auto patch_header = header(entry);
    if (!patch_header || patch_header->stored || entry.size() < 32) return std::nullopt;
    if (src.size() != patch_header->src_size || crc32(src) != patch_header->crc) return std::nullopt;
    std::uint32_t length1, length2;
    std::memcpy(&length1, entry.data() + 24, 4);
    std::memcpy(&length2, entry.data() + 28, 4);
    if (32 + std::size_t(length1) + length2 > entry.size()) return std::nullopt;
    try {
        std::vector<std::byte> filtered(src.begin(), src.end());           // filtered source
        for (std::size_t i = filtered.size() >= 2 ? filtered.size() - 2 : 0; i > 1; i -= 2)
            wr16(filtered, i, std::uint16_t(rd16(filtered, i) - rd16(filtered, i - 2)));
        std::vector<std::byte> out(std::size_t(patch_header->out_size) + 2);
        std::size_t written = 0;
        std::int64_t cur = 0;
        Reader stream1{ entry.subspan(32, length1) };
        auto room = [&](std::size_t count) { if (written + count > patch_header->out_size) throw 0; };
        auto in_src = [&](std::int64_t source_at, std::size_t count) {
            if (source_at < 0 || std::size_t(source_at) + count > src.size()) throw 0;
        };
        while (stream1.at < stream1.bytes.size()) {
            const std::uint16_t op_code = std::uint16_t(stream1.u8() | stream1.u8() << 8);
            const std::size_t count = op_code & 0x3fff;
            const std::uint16_t kind = op_code & 0xc000;
            if (kind == 0x4000 || kind == 0x8000) cur += stream1.svar();
            room(count);
            if (kind == 0x4000) {
                in_src(cur, count);
                std::memcpy(out.data() + written, src.data() + cur, count);
            } else if (kind == 0x8000) {
                in_src(cur, count);
                for (std::size_t k = 0; k < count; k += 2) {
                    const std::uint16_t prev = written + k >= 2 ? rd16(out, written + k - 2) : 0;
                    wr16(out, written + k, std::uint16_t(rd16(filtered, std::size_t(cur) + k) + prev));
                }
            } else if (kind == 0) {
                if (stream1.at + count > stream1.bytes.size()) throw 0;
                std::memcpy(out.data() + written, stream1.bytes.data() + stream1.at, count);
                stream1.at += count;
            }                                                          // 0xC000: zeros
            written += count;
            cur += std::int64_t(count);
        }
        Reader stream2{ entry.subspan(32 + length1, length2) };
        auto add = [&](std::uint32_t pos, std::int32_t value) {
            if (std::size_t(pos) + 2 > out.size()) throw 0;
            wr16(out, pos, std::uint16_t(rd16(out, pos) + value));
        };
        std::int32_t acc = 0;
        if (stream2.bytes.size()) {
            const std::int32_t value = stream2.svar();
            if (value != 0) {
                std::uint32_t pos = stream2.uvar();
                add(pos, value);
                for (std::uint32_t delta = stream2.uvar(); delta; delta = stream2.uvar()) add(pos += delta, value);
                acc = value;
            }
            for (std::uint32_t delta_value = stream2.uvar(); delta_value; delta_value = stream2.uvar()) {
                acc += std::int32_t(delta_value);
                std::uint32_t pos = stream2.uvar();
                add(pos, acc);
                for (std::uint32_t delta = stream2.uvar(); delta; delta = stream2.uvar()) add(pos += delta, acc);
            }
        }
        out.resize(patch_header->out_size);
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace d2d::mpq::bnpatch
