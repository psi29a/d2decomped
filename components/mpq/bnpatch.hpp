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

inline std::uint32_t crc32(std::span<const std::byte> b) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xffffffffu;
    for (auto x : b) c = table[(c ^ std::uint8_t(x)) & 0xff] ^ (c >> 8);
    return ~c;
}

namespace detail {
struct Reader {
    std::span<const std::byte> b;
    std::size_t i = 0;
    std::uint8_t u8() {
        if (i >= b.size()) throw 0;
        return std::uint8_t(b[i++]);
    }
    std::uint32_t uvar() {
        const std::uint32_t x = u8();
        if (x < 0x80) return x;
        if (!(x & 0x40)) return (x & 0x3f) | std::uint32_t(u8()) << 6;
        std::uint32_t v = (x & 0x1f) | std::uint32_t(u8()) << 5;
        v |= std::uint32_t(u8()) << 13;
        if (!(x & 0x20)) return v;
        return v | std::uint32_t(u8()) << 21;
    }
    std::int32_t svar() {
        const std::size_t start = i;
        const std::uint32_t x = std::uint8_t(b[start]);
        const std::uint32_t v = uvar();
        const int bits = x < 0x80 ? 7 : !(x & 0x40) ? 14 : !(x & 0x20) ? 21 : 29;
        return std::int32_t(v << (32 - bits)) >> (32 - bits);
    }
};
inline std::uint16_t rd16(const std::vector<std::byte>& v, std::size_t o) {
    return std::uint16_t(std::uint8_t(v[o]) | std::uint8_t(v[o + 1]) << 8);
}
inline void wr16(std::vector<std::byte>& v, std::size_t o, std::uint16_t w) {
    v[o] = std::byte(w & 0xff); v[o + 1] = std::byte(w >> 8);
}
}  // namespace detail

// The 24-byte header of an installer entry.
struct Header { bool stored = false; std::uint32_t crc = 0, src_size = 0, out_size = 0; };
inline std::optional<Header> header(std::span<const std::byte> e) {
    if (e.size() < 24 || std::uint8_t(e[0]) != 24 || std::uint8_t(e[1]) != 0) return std::nullopt;
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, e.data() + o, 4); return v; };
    return Header{ std::uint8_t(e[3]) == 1, u32(4), u32(8), u32(12) };
}

// Rebuild the patched file from an installer entry and its source file.
// nullopt if the source isn't the one the patch was made against (size or
// CRC mismatch) or the streams are malformed.
inline std::optional<std::vector<std::byte>> apply(std::span<const std::byte> entry,
                                                   std::span<const std::byte> src) {
    using namespace detail;
    const auto h = header(entry);
    if (!h || h->stored || entry.size() < 32) return std::nullopt;
    if (src.size() != h->src_size || crc32(src) != h->crc) return std::nullopt;
    std::uint32_t l1, l2;
    std::memcpy(&l1, entry.data() + 24, 4);
    std::memcpy(&l2, entry.data() + 28, 4);
    if (32 + std::size_t(l1) + l2 > entry.size()) return std::nullopt;
    try {
        std::vector<std::byte> f(src.begin(), src.end());           // filtered source
        for (std::size_t i = f.size() >= 2 ? f.size() - 2 : 0; i > 1; i -= 2)
            wr16(f, i, std::uint16_t(rd16(f, i) - rd16(f, i - 2)));
        std::vector<std::byte> out(std::size_t(h->out_size) + 2);
        std::size_t o = 0;
        std::int64_t cur = 0;
        Reader s1{ entry.subspan(32, l1) };
        auto room = [&](std::size_t n) { if (o + n > h->out_size) throw 0; };
        auto in_src = [&](std::int64_t at, std::size_t n) {
            if (at < 0 || std::size_t(at) + n > src.size()) throw 0;
        };
        while (s1.i < s1.b.size()) {
            const std::uint16_t op = std::uint16_t(s1.u8() | s1.u8() << 8);
            const std::size_t n = op & 0x3fff;
            const std::uint16_t kind = op & 0xc000;
            if (kind == 0x4000 || kind == 0x8000) cur += s1.svar();
            room(n);
            if (kind == 0x4000) {
                in_src(cur, n);
                std::memcpy(out.data() + o, src.data() + cur, n);
            } else if (kind == 0x8000) {
                in_src(cur, n);
                for (std::size_t k = 0; k < n; k += 2) {
                    const std::uint16_t prev = o + k >= 2 ? rd16(out, o + k - 2) : 0;
                    wr16(out, o + k, std::uint16_t(rd16(f, std::size_t(cur) + k) + prev));
                }
            } else if (kind == 0) {
                if (s1.i + n > s1.b.size()) throw 0;
                std::memcpy(out.data() + o, s1.b.data() + s1.i, n);
                s1.i += n;
            }                                                          // 0xC000: zeros
            o += n;
            cur += std::int64_t(n);
        }
        Reader s2{ entry.subspan(32 + l1, l2) };
        auto add = [&](std::uint32_t pos, std::int32_t v) {
            if (std::size_t(pos) + 2 > out.size()) throw 0;
            wr16(out, pos, std::uint16_t(rd16(out, pos) + v));
        };
        std::int32_t acc = 0;
        if (s2.b.size()) {
            const std::int32_t v = s2.svar();
            if (v != 0) {
                std::uint32_t pos = s2.uvar();
                add(pos, v);
                for (std::uint32_t d = s2.uvar(); d; d = s2.uvar()) add(pos += d, v);
                acc = v;
            }
            for (std::uint32_t dv = s2.uvar(); dv; dv = s2.uvar()) {
                acc += std::int32_t(dv);
                std::uint32_t pos = s2.uvar();
                add(pos, acc);
                for (std::uint32_t d = s2.uvar(); d; d = s2.uvar()) add(pos += d, acc);
            }
        }
        out.resize(h->out_size);
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace d2d::mpq::bnpatch
