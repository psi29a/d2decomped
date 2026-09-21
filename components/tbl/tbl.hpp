// D2Decomp TBL string-table parser.
//
// Format (Phrozen Keep spec, confirmed by our RE of D2 1.14d's
// .\StrTable\strtable.cpp): a 21-byte packed header, an indices array
// (u16 * NodesNumber, unused for lookup — hash-mapping metadata), a hash
// node array (17 packed bytes each), then the string data blob. Keys are
// NUL-terminated ASCII, values NUL-terminated UTF-16LE.
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

namespace d2d::tbl {

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
        if (auto it = entries_.find(std::string(key)); it != entries_.end()) {
            return std::u16string_view(it->second);
        }
        return std::nullopt;
    }

    // Lookup by the 16-bit `Index` field of the hash node. D2's game.exe
    // stores that ID directly in menu/UI record fields (e.g. the front-end
    // menu table at 0x00708ec0+ carries button labels as IDs 0x13f2..).
    // See docs/research/re/frontend-menu-table.md for the anchor use.
    [[nodiscard]] std::optional<std::u16string_view>
    get(std::uint16_t id) const {
        if (auto it = by_id_.find(id); it != by_id_.end()) {
            return std::u16string_view(it->second);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

private:
    static std::uint32_t rd32(const std::byte* p) {
        std::uint32_t v; std::memcpy(&v, p, 4); return v;
    }
    static std::uint16_t rd16(const std::byte* p) {
        std::uint16_t v; std::memcpy(&v, p, 2); return v;
    }

    void parse(std::span<const std::byte> b) {
        constexpr std::size_t kHeader = 21;
        constexpr std::size_t kNode   = 17;
        if (b.size() < kHeader) throw std::runtime_error("TBL: truncated header");

        const auto nodesNumber   = rd16(b.data() + 0x02);
        const auto hashTableSize = rd32(b.data() + 0x04);
        // +0x08 Version — 0 or 1, don't care.
        // +0x09 DataStartOffset, +0x0D HashMaxTries, +0x11 FileSize — unused.

        const std::size_t nodesOff = kHeader + std::size_t(nodesNumber) * 2;
        const std::size_t stringsOff = nodesOff
                                     + std::size_t(hashTableSize) * kNode;
        if (b.size() < stringsOff) throw std::runtime_error("TBL: truncated body");

        entries_.reserve(hashTableSize);
        for (std::uint32_t i = 0; i < hashTableSize; ++i) {
            const std::byte* node = b.data() + nodesOff + i * kNode;
            if (std::uint8_t(node[0]) == 0) continue;   // deleted entry
            const auto keyOff = rd32(node + 0x07);
            const auto valOff = rd32(node + 0x0b);
            const auto valLen = rd16(node + 0x0f);        // includes NUL, in u16 chars

            if (keyOff >= b.size() || valOff >= b.size()) continue;

            const auto* keyp = reinterpret_cast<const char*>(b.data() + keyOff);
            const std::size_t keyMax = b.size() - keyOff;
            std::size_t keyLen = 0;
            while (keyLen < keyMax && keyp[keyLen] != '\0') ++keyLen;

            // D2 stores values as single-byte (Latin-1 / Windows-1252 for
            // LATIN builds; a locale-specific MBCS for CYR/JPN/KOR). Non-LATIN
            // builds aren't handled here yet — treat every byte as Latin-1 for
            // now, matching OpenD2's Latin path. Fix when JPN/KOR TBLs surface.
            // ponytail: Latin-1 only; add MBCS decode when a non-Latin TBL fails.
            if (valLen == 0 || valOff + std::size_t(valLen) > b.size()) continue;
            const auto* valp = reinterpret_cast<const unsigned char*>(b.data() + valOff);
            std::u16string value;
            value.reserve(valLen - 1);
            for (std::uint16_t i = 0; i + 1 < valLen; ++i) {  // skip trailing NUL
                value.push_back(char16_t(valp[i]));
            }

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
