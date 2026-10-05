// Definitions for huffman.hpp.
#include "huffman.hpp"

#include "exe_tables.hpp"
#include "wire.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>

namespace d2d::net::d2gs {

auto Huffman::build(std::span<const std::uint8_t> lengths) -> std::expected<Huffman, std::string> {
    if (auto checked = check_lengths(lengths); !checked) return std::unexpected(checked.error());
    Huffman huffman;
    std::ranges::copy(lengths, huffman.lengths_.begin());
    // FUN_0040adb0: symbols by length descending, ties by value; the first
    // gets code 0, each next (prev + 1) >> (prev_len - len).
    for (std::size_t i = 0; i < kSymbolCount; ++i) huffman.sorted_[i] = static_cast<std::uint8_t>(i);
    std::ranges::stable_sort(huffman.sorted_, [&](std::uint8_t lhs, std::uint8_t rhs) { return lengths[lhs] > lengths[rhs]; });
    std::uint32_t code = 0;
    int previous = 0;
    for (std::size_t i = 0; i < kSymbolCount; ++i) {
        const std::uint8_t symbol = huffman.sorted_[i];
        const int length = lengths[symbol];
        if (i > 0) code = (code + 1) >> (previous - length);
        if (length != previous) {
            huffman.first_code_[length] = code;
            huffman.offset_[length] = static_cast<std::uint16_t>(i);
        }
        ++huffman.count_[length];
        huffman.codes_[symbol] = static_cast<std::uint16_t>(code);
        previous = length;
    }
    return huffman;
}

auto Huffman::compress(std::span<const std::uint8_t> input, Bytes& out) const -> void {
    std::uint32_t window = 0;
    int bits = 0;
    for (const std::uint8_t symbol : input) {
        window = window << lengths_[symbol] | codes_[symbol];
        bits += lengths_[symbol];
        while (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(window >> bits));
        }
        window &= (1U << bits) - 1;
    }
    if (bits > 0) out.push_back(static_cast<std::uint8_t>(window << (8 - bits)));
}

auto Huffman::decompress(std::span<const std::uint8_t> input, Bytes& out) const -> void {
    std::uint32_t code = 0;
    int length = 0;
    for (const std::uint8_t byte : input) {
        for (int bit = 7; bit >= 0; --bit) {
            code = code << 1 | ((byte >> bit) & 1U);
            ++length;
            // A complete code has a 1..15-bit codeword for every path.
            if (code - first_code_[length] < count_[length]) {
                out.push_back(sorted_[offset_[length] + (code - first_code_[length])]);
                code = 0;
                length = 0;
            }
        }
    }
}

auto reload_lengths(std::span<const std::uint8_t> packet) -> std::expected<CodeLengths, std::string> {
    if (packet.size() < 129 || packet[0] != 0xaf || packet[1] != 0x81)
        return std::unexpected(std::format("not an AF 81 table packet ({} bytes)", packet.size()));
    CodeLengths lengths{};
    for (std::size_t i = 0; i < 128; ++i) {
        lengths[2 * i] = static_cast<std::uint8_t>((packet[1 + i] & 0x0f) + 1);
        lengths[2 * i + 1] = static_cast<std::uint8_t>((packet[1 + i] >> 4) + 1);
    }
    return lengths;
}

} // namespace d2d::net::d2gs
