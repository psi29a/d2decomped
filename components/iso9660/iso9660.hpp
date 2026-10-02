// SPDX-License-Identifier: GPL-3.0-or-later
// iso9660.hpp — minimal ISO 9660 + Joliet reader, header-only.
//
// ponytail: scope is "read the 4 D2 ISOs". No Rock Ridge, no UDF, no raw
// BIN/CUE (2352-byte sectors), no writes. Upgrade path if we ever hit a
// non-D2 disc: replace with libarchive.
//
// Layout reminder (all offsets in 2048-byte sectors):
//   sector 16..N: volume descriptors
//     PVD (type 1)   — root directory record at offset 156
//     SVD (type 2)   — Joliet if escape sequence at offset 88 is %/@, %/C, %/E
//     terminator (type 255) — stop
//   directory extent: packed directory records; records never cross a
//     sector boundary, padding (len=0) skips to the next sector.

#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace iso9660 {

inline constexpr std::size_t kSector = 2048;

struct Entry {
    std::string path;     // "/data/global/...", forward slashes, no ";1" suffix
    std::uint32_t lba{};
    std::uint32_t size{}; // bytes
    bool directory{};
};

class Reader {
public:
    static std::optional<Reader> open(const std::filesystem::path& iso);

    const std::vector<Entry>& entries() const noexcept { return entries_; }

    // Read `size` bytes starting at LBA `lba` into `out` (>= size room).
    bool read(std::uint32_t lba, std::uint32_t size, std::byte* out);
    std::vector<std::byte> read(const Entry& entry);

private:
    Reader() = default;
    bool parse_volume_descriptors();
    void walk(std::uint32_t lba, std::uint32_t size,
              const std::string& prefix, bool joliet);

    std::ifstream file_;
    std::vector<Entry> entries_;
    std::uint32_t root_lba_{};
    std::uint32_t root_size_{};
    bool joliet_{};
};

namespace detail {

inline std::uint32_t u32_le(const std::byte* source) noexcept {
    return  static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[0]))       |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[1])) << 8) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[2])) << 16)|
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(source[3])) << 24);
}

// UCS-2 big-endian → UTF-8 (BMP only; D2 names are ASCII in practice).
inline std::string ucs2be_to_utf8(const std::byte* source, std::size_t bytes) {
    std::string out;
    out.reserve(bytes / 2);
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        const auto code_point = static_cast<std::uint16_t>(
            (std::to_integer<std::uint16_t>(source[i]) << 8) |
             std::to_integer<std::uint16_t>(source[i + 1]));
        if (code_point < 0x80) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }
    return out;
}

inline std::string strip_version(std::string name) {
    if (const auto semicolon = name.rfind(';'); semicolon != std::string::npos) name.resize(semicolon);
    while (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

} // namespace detail

inline bool Reader::read(std::uint32_t lba, std::uint32_t size, std::byte* out) {
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(lba) * kSector);
    file_.read(reinterpret_cast<char*>(out), size);
    return static_cast<std::uint32_t>(file_.gcount()) == size;
}

inline std::vector<std::byte> Reader::read(const Entry& entry) {
    std::vector<std::byte> buf(entry.size);
    if (entry.size) read(entry.lba, entry.size, buf.data());
    return buf;
}

inline bool Reader::parse_volume_descriptors() {
    std::byte sector[kSector];
    std::uint32_t primary_lba = 0, primary_size = 0;
    std::uint32_t joliet_lba  = 0, joliet_size  = 0;

    for (std::uint32_t sector_index = 16; sector_index < 100; ++sector_index) {   // safety bound
        if (!read(sector_index, kSector, sector)) return false;
        if (std::memcmp(reinterpret_cast<const char*>(sector) + 1, "CD001", 5) != 0)
            return false;
        const auto type = std::to_integer<std::uint8_t>(sector[0]);
        if (type == 255) break;

        const std::byte* root_rec = sector + 156;
        const auto lba  = detail::u32_le(root_rec + 2);
        const auto size = detail::u32_le(root_rec + 10);

        if (type == 1) {                 // PVD
            primary_lba = lba;
            primary_size = size;
        } else if (type == 2) {          // SVD — Joliet if escape says so
            const std::byte* esc = sector + 88;
            bool is_joliet = false;
            for (std::size_t i = 0; i + 2 < 32; ++i) {
                const auto first = std::to_integer<char>(esc[i]);
                const auto second = std::to_integer<char>(esc[i + 1]);
                const auto third = std::to_integer<char>(esc[i + 2]);
                if (first == '%' && second == '/' && (third == '@' || third == 'C' || third == 'E')) {
                    is_joliet = true;
                    break;
                }
            }
            if (is_joliet) { joliet_lba = lba; joliet_size = size; }
        }
    }

    if (joliet_lba) {
        root_lba_ = joliet_lba; root_size_ = joliet_size; joliet_ = true;
    } else if (primary_lba) {
        root_lba_ = primary_lba; root_size_ = primary_size; joliet_ = false;
    } else {
        return false;
    }
    return true;
}

inline void Reader::walk(std::uint32_t lba, std::uint32_t size,
                         const std::string& prefix, bool joliet) {
    std::vector<std::byte> buf(size);
    if (!read(lba, size, buf.data())) return;

    std::size_t pos = 0;
    while (pos < size) {
        const auto rec_len = std::to_integer<std::uint8_t>(buf[pos]);
        if (rec_len == 0) {
            // Padding to end of sector — jump to the next one.
            pos = ((pos / kSector) + 1) * kSector;
            continue;
        }
        if (pos + rec_len > size) break;

        const auto e_lba  = detail::u32_le(buf.data() + pos + 2);
        const auto e_size = detail::u32_le(buf.data() + pos + 10);
        const auto flags  = std::to_integer<std::uint8_t>(buf[pos + 25]);
        const auto n_len  = std::to_integer<std::uint8_t>(buf[pos + 32]);
        const std::byte* n_p = buf.data() + pos + 33;

        // "." (0x00) and ".." (0x01) — skip both.
        const bool dotty = n_len == 1 &&
            (std::to_integer<std::uint8_t>(n_p[0]) <= 1);

        if (!dotty) {
            std::string name = joliet
                ? detail::ucs2be_to_utf8(n_p, n_len)
                : std::string(reinterpret_cast<const char*>(n_p), n_len);
            name = detail::strip_version(std::move(name));

            const bool is_dir = (flags & 0x02) != 0;
            std::string path = prefix + "/" + name;
            entries_.push_back({path, e_lba, e_size, is_dir});
            if (is_dir) walk(e_lba, e_size, path, joliet);
        }
        pos += rec_len;
    }
}

inline std::optional<Reader> Reader::open(const std::filesystem::path& iso) {
    Reader reader;
    reader.file_.open(iso, std::ios::binary);
    if (!reader.file_) return std::nullopt;
    if (!reader.parse_volume_descriptors()) return std::nullopt;
    reader.walk(reader.root_lba_, reader.root_size_, "", reader.joliet_);
    return reader;
}

} // namespace iso9660
