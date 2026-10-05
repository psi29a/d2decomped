// The two tables the codec needs, read at runtime from the user's own
// 1.14d game.exe (docs/research/re/net-join.md "Compression", "Splitter"):
// none of their bytes ship with d2d. A PE section walk maps a VA to file
// bytes; both tables are checked before anything uses them, and a failed
// check disables joining with the message.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace d2d::net::d2gs {

// VAs in 1.14d game.exe (image base 0x400000).
inline constexpr std::uint32_t kHuffmanLengthsVa = 0x7076c0; // 256 u8 code lengths
inline constexpr std::uint32_t kS2cSizesVa = 0x730ae8;       // S->C sizes, one i32 an id
inline constexpr std::size_t kSymbolCount = 256;
inline constexpr std::size_t kS2cIdCount = 181; // ids 0x00..0xb4
inline constexpr std::uint32_t kC2sSizesVa = 0x730dc0;       // C->S sizes, one i32 an id (FUN_0052bc20)
inline constexpr std::size_t kC2sIdCount = 0x71;             // ids 0x00..0x70

// Huffman code lengths, one per byte value.
using CodeLengths = std::array<std::uint8_t, kSymbolCount>;
// S->C packet sizes by id: > 0 fixed, 0 never sent, < 0 variable (split.hpp).
using SizeTable = std::array<std::int32_t, kS2cIdCount>;
// C->S packet sizes by id: > 0 fixed, 0 not valid, -1 variable (split.hpp).
using C2sSizeTable = std::array<std::int32_t, kC2sIdCount>;

struct ExeTables {
    CodeLengths lengths{};
    SizeTable s2c_sizes{};
    C2sSizeTable c2s_sizes{};
};

// The file bytes behind [virtual_address, virtual_address + size) in a PE32 image, if one section's
// raw data holds all of them.
auto pe_bytes_at(std::span<const std::uint8_t> image, std::uint32_t virtual_address, std::size_t size) -> std::optional<std::span<const std::uint8_t>>;

// Every length in 1..15 and a complete code (Kraft sum exactly 1).
auto check_lengths(std::span<const std::uint8_t> lengths) -> std::expected<void, std::string>;
// The sizes game.exe's layouts fix (0x01 8, 0x03 12, 0x59 26, 0x8f 33) and
// a split rule for every variable id.
auto check_sizes(const SizeTable& sizes) -> std::expected<void, std::string>;

// Both tables from a game.exe image, checked.
auto read_exe_tables(std::span<const std::uint8_t> image) -> std::expected<ExeTables, std::string>;
auto load_exe_tables(const std::filesystem::path& game_exe) -> std::expected<ExeTables, std::string>;

} // namespace d2d::net::d2gs
