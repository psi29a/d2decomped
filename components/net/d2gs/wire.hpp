// Little-endian field access for the D2GS codec: every multi-byte field
// on game.exe's wire, and in its PE image, is little-endian.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace d2d::net::d2gs {

// A packet or a stream of them: plain bytes.
using Bytes = std::vector<std::uint8_t>;

// Reads assume the caller checked the bounds.
inline auto read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) -> std::uint16_t {
    return static_cast<std::uint16_t>(bytes[offset] | bytes[offset + 1] << 8);
}
inline auto read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) -> std::uint32_t {
    return static_cast<std::uint32_t>(bytes[offset]) | static_cast<std::uint32_t>(bytes[offset + 1]) << 8
         | static_cast<std::uint32_t>(bytes[offset + 2]) << 16 | static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

inline auto put_u8(Bytes& out, std::uint8_t value) -> void { out.push_back(value); }
inline auto put_u16(Bytes& out, std::uint16_t value) -> void {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
inline auto put_u32(Bytes& out, std::uint32_t value) -> void {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
inline auto put_bytes(Bytes& out, std::span<const std::uint8_t> bytes) -> void { out.insert(out.end(), bytes.begin(), bytes.end()); }

// A fixed char[width] field: the text, NUL-padded (and cut to width - 1
// so it stays terminated).
inline auto put_name(Bytes& out, std::string_view name, std::size_t width) -> void {
    const std::size_t kept = name.size() < width ? name.size() : width - 1;
    for (std::size_t i = 0; i < width; ++i) out.push_back(i < kept ? static_cast<std::uint8_t>(name[i]) : 0);
}
// The text of a char[width] field up to its first NUL (all of it when
// there is none).
inline auto read_name(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t width) -> std::string_view {
    const auto* first = reinterpret_cast<const char*>(bytes.data() + offset);
    std::size_t length = 0;
    while (length < width && first[length] != '\0') ++length;
    return {first, length};
}

} // namespace d2d::net::d2gs
