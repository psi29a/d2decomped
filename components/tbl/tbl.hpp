// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp TBL string-table parser.
//
// Format (Phrozen Keep spec, confirmed by our RE of D2 1.14d's
// .\StrTable\strtable.cpp): a 21-byte packed header, an indices array
// (u16 * NodesNumber, unused for lookup — hash-mapping metadata), a hash
// node array (17 packed bytes each), then the string data blob. Keys are
// NUL-terminated ASCII, values NUL-terminated UTF-8 (FUN_00526100).
//
// We ignore the on-disk hash structure and rebuild a std::unordered_map
// at load time — TBLs are small (largest is ~400 KB), map lookup is O(1),
// and the on-disk hash is designed for zero-alloc in-place scans we don't
// need. Load once at startup, keep forever.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace d2d::tbl {

// game.exe's UTF-8 decoder (FUN_00526100, its table at 0x7309b8): a
// sequence of 1..6 bytes by the first byte's lead bits, continuation bytes
// 10xxxxxx, each sequence one 16-bit unit (truncated), under its minimum an
// error. FUN_00526320 ends the string at the first error.
inline std::u16string utf8_units(std::span<const std::byte> text) {
    struct Lead { unsigned mask, value, value_mask, minimum; };
    static constexpr Lead kLeads[] = { { 0x80, 0x00, 0x7f, 0 }, { 0xe0, 0xc0, 0x7ff, 0x80 }, { 0xf0, 0xe0, 0xffff, 0x800 },
                                       { 0xf8, 0xf0, 0x1fffff, 0x10000 }, { 0xfc, 0xf8, 0x3ffffff, 0x200000 }, { 0xfe, 0xfc, 0x7fffffff, 0x4000000 } };
    std::u16string units;
    for (std::size_t at = 0; at < text.size();) {
        const unsigned first = unsigned(text[at]);
        if (!first) break;                                  // a NUL ends it (0x526385)
        std::uint32_t code = first;
        std::size_t length = 0;
        for (std::size_t step = 0; step < std::size(kLeads); ++step) {
            if ((first & kLeads[step].mask) == kLeads[step].value) {
                if ((code & kLeads[step].value_mask) >= kLeads[step].minimum) length = step + 1;
                break;
            }
            if (at + step + 1 >= text.size() || ((unsigned(text[at + step + 1]) ^ 0x80u) & 0xc0u) != 0) break;
            code = code << 6 | (unsigned(text[at + step + 1]) ^ 0x80u);
        }
        if (!length) break;
        units.push_back(char16_t(code & kLeads[length - 1].value_mask));
        at += length;
    }
    return units;
}

class Table {
public:
    Table() = default;

    explicit Table(std::span<const std::byte> bytes) {
        parse(bytes);
    }

    [[nodiscard]] std::optional<std::u16string_view>
    get(std::string_view key) const {
        // heterogeneous lookup would remove the string(), but not worth the
        // template ceremony until profiling asks for it.
        if (auto found = entries_.find(std::string(key)); found != entries_.end()) {
            return std::u16string_view(found->second);
        }
        return std::nullopt;
    }

    // Lookup by the 16-bit `Index` field of the hash node. D2's game.exe
    // stores that ID directly in menu/UI record fields (e.g. the front-end
    // menu table at 0x00708ec0+ carries button labels as IDs 0x13f2..).
    // See docs/research/re/frontend-menu-table.md for the anchor use.
    [[nodiscard]] std::optional<std::u16string_view>
    get(std::uint16_t id) const {
        if (auto found = by_id_.find(id); found != by_id_.end()) {
            return std::u16string_view(found->second);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    // Direct map access for diagnostics / bulk scans. Not intended for hot
    // paths — use get(key) or get(id).
    [[nodiscard]] const auto& entries() const noexcept { return entries_; }

private:
    static std::uint32_t rd32(const std::byte* source) {
        std::uint32_t value; std::memcpy(&value, source, 4); return value;
    }
    static std::uint16_t rd16(const std::byte* source) {
        std::uint16_t value; std::memcpy(&value, source, 2); return value;
    }

    // Reads the header, the hash nodes and each key and value (FUN_005259c0's
    // layout), keeping the first entry per key.
    void parse(std::span<const std::byte> bytes) {
        constexpr std::size_t kHeader = 21;
        constexpr std::size_t kNode   = 17;
        if (bytes.size() < kHeader) throw std::runtime_error("TBL: truncated header");

        const auto nodesNumber   = rd16(bytes.data() + 0x02);
        const auto hashTableSize = rd32(bytes.data() + 0x04);
        // +0x08 Version — 0 or 1, don't care.
        // +0x09 DataStartOffset, +0x0D HashMaxTries, +0x11 FileSize — unused.

        const std::size_t nodesOff = kHeader + std::size_t(nodesNumber) * 2;
        const std::size_t stringsOff = nodesOff
                                     + std::size_t(hashTableSize) * kNode;
        if (bytes.size() < stringsOff) throw std::runtime_error("TBL: truncated body");

        entries_.reserve(hashTableSize);
        for (std::uint32_t i = 0; i < hashTableSize; ++i) {
            const std::byte* node = bytes.data() + nodesOff + i * kNode;
            if (std::uint8_t(node[0]) == 0) continue;   // deleted entry
            const auto keyOff = rd32(node + 0x07);
            const auto valOff = rd32(node + 0x0b);
            const auto valLen = rd16(node + 0x0f);        // includes NUL, in bytes

            if (keyOff >= bytes.size() || valOff >= bytes.size()) continue;

            const auto* keyp = reinterpret_cast<const char*>(bytes.data() + keyOff);
            const std::size_t keyMax = bytes.size() - keyOff;
            std::size_t keyLen = 0;
            while (keyLen < keyMax && keyp[keyLen] != '\0') ++keyLen;

            // UTF-8 to 16-bit units (FUN_00526320 -> FUN_00526100). The font
            // map's lead bytes (FUN_00527d50) only size game.exe's buffer.
            if (valLen == 0 || valOff + std::size_t(valLen) > bytes.size()) continue;
            std::u16string value = utf8_units({ bytes.data() + valOff, std::size_t(valLen - 1) });   // less the trailing NUL

            // Nodes carry a 16-bit `Index` at offset +0x01 — that's D2's ID.
            // Stash the value under it too so callers can look up by either.
            const auto id = rd16(node + 0x01);
            entries_.emplace(std::string(keyp, keyLen), value);
            by_id_.emplace(id, std::move(value));
        }
    }

    std::unordered_map<std::string, std::u16string>   entries_;
    std::unordered_map<std::uint16_t, std::u16string> by_id_;
};

}  // namespace d2d::tbl
