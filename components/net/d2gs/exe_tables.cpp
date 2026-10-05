// Definitions for exe_tables.hpp: the PE walk and the table checks.
#include "exe_tables.hpp"

#include "split.hpp"
#include "wire.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2d::net::d2gs {

auto pe_bytes_at(std::span<const std::uint8_t> image, std::uint32_t va, std::size_t size) -> std::optional<std::span<const std::uint8_t>> {
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') return std::nullopt;
    const std::size_t pe = read_u32(image, 0x3c);
    // Signature, COFF header, and the optional header up to ImageBase.
    if (pe > image.size() || image.size() - pe < 24 + 32) return std::nullopt;
    if (image[pe] != 'P' || image[pe + 1] != 'E' || image[pe + 2] != 0 || image[pe + 3] != 0) return std::nullopt;
    const std::size_t section_count = read_u16(image, pe + 6);
    const std::size_t optional_size = read_u16(image, pe + 20);
    if (read_u16(image, pe + 24) != 0x10b) return std::nullopt; // PE32: game.exe is 32-bit
    const std::uint32_t image_base = read_u32(image, pe + 24 + 28);
    const std::size_t table = pe + 24 + optional_size;
    for (std::size_t i = 0; i < section_count; ++i) {
        const std::size_t header = table + 40 * i;
        if (header > image.size() || image.size() - header < 40) return std::nullopt;
        const std::uint64_t start = std::uint64_t{image_base} + read_u32(image, header + 12);
        const std::uint64_t raw_size = read_u32(image, header + 16);
        const std::uint64_t raw_offset = read_u32(image, header + 20);
        if (va < start || va + std::uint64_t{size} > start + raw_size) continue;
        const std::uint64_t offset = raw_offset + (va - start);
        if (offset + size > image.size()) return std::nullopt;
        return image.subspan(static_cast<std::size_t>(offset), size);
    }
    return std::nullopt;
}

auto check_lengths(std::span<const std::uint8_t> lengths) -> std::expected<void, std::string> {
    if (lengths.size() != kSymbolCount) return std::unexpected(std::format("{} code lengths, not 256", lengths.size()));
    // Kraft sum in units of 2^-15: exactly 1 is 1 << 15.
    std::uint32_t kraft = 0;
    for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
        const int length = lengths[symbol];
        if (length < 1 || length > 15) return std::unexpected(std::format("code length {} for byte {:#04x}, not 1..15", length, symbol));
        kraft += 1U << (15 - length);
    }
    if (kraft != 1U << 15) return std::unexpected(std::format("code lengths' Kraft sum is {}/32768, not 1", kraft));
    return {};
}

auto check_sizes(const SizeTable& sizes) -> std::expected<void, std::string> {
    struct Known {
        std::uint8_t id;
        std::int32_t size;
    };
    for (const Known known : {Known{0x01, 8}, Known{0x03, 12}, Known{0x59, 26}, Known{0x8f, 33}}) {
        if (sizes[known.id] != known.size)
            return std::unexpected(std::format("S->C packet {:#04x} is {} bytes, not {}", known.id, sizes[known.id], known.size));
    }
    for (std::size_t id = 0; id < sizes.size(); ++id) {
        if (sizes[id] < 0 && !has_variable_rule(static_cast<std::uint8_t>(id)))
            return std::unexpected(std::format("S->C packet {:#04x} is variable but has no split rule", id));
        if (sizes[id] > static_cast<std::int32_t>(kMaxPacketSize))
            return std::unexpected(std::format("S->C packet {:#04x} is {} bytes, over {:#x}", id, sizes[id], kMaxPacketSize));
    }
    return {};
}

auto read_exe_tables(std::span<const std::uint8_t> image) -> std::expected<ExeTables, std::string> {
    const auto lengths = pe_bytes_at(image, kHuffmanLengthsVa, kSymbolCount);
    const auto sizes = pe_bytes_at(image, kS2cSizesVa, kS2cIdCount * 4);
    const auto c2s_sizes = pe_bytes_at(image, kC2sSizesVa, kC2sIdCount * 4);
    if (!lengths || !sizes || !c2s_sizes) return std::unexpected(std::string("not a 1.14d game.exe (its tables aren't at the expected addresses)"));
    ExeTables tables;
    for (std::size_t i = 0; i < kSymbolCount; ++i) tables.lengths[i] = (*lengths)[i];
    for (std::size_t i = 0; i < kS2cIdCount; ++i) tables.s2c_sizes[i] = static_cast<std::int32_t>(read_u32(*sizes, i * 4));
    for (std::size_t i = 0; i < kC2sIdCount; ++i) tables.c2s_sizes[i] = static_cast<std::int32_t>(read_u32(*c2s_sizes, i * 4));
    // The C->S sizes d2d's builders fix (c2s.hpp): 0x01 5, 0x13 9, 0x68 37, 0x6d 13.
    for (const auto [id, size] : { std::pair{ 0x01, 5 }, std::pair{ 0x13, 9 }, std::pair{ 0x68, 37 }, std::pair{ 0x6d, 13 } })
        if (tables.c2s_sizes[std::size_t(id)] != size)
            return std::unexpected(std::format("game.exe's C->S size table: {:#04x} is {} bytes, not {}", id, tables.c2s_sizes[std::size_t(id)], size));
    if (auto checked = check_lengths(tables.lengths); !checked) return std::unexpected("game.exe's Huffman table: " + checked.error());
    if (auto checked = check_sizes(tables.s2c_sizes); !checked) return std::unexpected("game.exe's size table: " + checked.error());
    return tables;
}

auto load_exe_tables(const std::filesystem::path& game_exe) -> std::expected<ExeTables, std::string> {
    std::ifstream file(game_exe, std::ios::binary);
    if (!file) return std::unexpected(std::format("can't open {}", game_exe.string()));
    const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto tables = read_exe_tables(image);
    if (!tables) return std::unexpected(std::format("{}: {}", game_exe.string(), tables.error()));
    return tables;
}

} // namespace d2d::net::d2gs
