// game.exe's S->C compression: canonical Huffman over bytes, only the 256
// code lengths stored (docs/research/re/net-join.md "Compression").
// FUN_0040adb0 builds the codes, FUN_0040b1b0 compresses, FUN_0040b260
// decompresses; the client reloads the table on AF 81 (FUN_0052a8d0).
#pragma once

#include "exe_tables.hpp"
#include "wire.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace d2d::net::d2gs {

class Huffman {
public:
    // Refuses lengths outside 1..15 or an incomplete / overfull code
    // (check_lengths). ponytail: game.exe's build only refuses 0 and >= 16;
    // a Kraft sum other than 1 would give a code that can't round trip.
    static auto build(std::span<const std::uint8_t> lengths) -> std::expected<Huffman, std::string>;

    // Codes MSB first, the last byte zero-padded; appended to `out`.
    auto compress(std::span<const std::uint8_t> input, Bytes& out) const -> void;
    // Decodes whole codes until the bits run out; the < 8 padding bits are
    // dropped (with game.exe's tables they are a prefix of the all-zero
    // longest code, so they never decode). Appended to `out`.
    auto decompress(std::span<const std::uint8_t> input, Bytes& out) const -> void;

    auto lengths() const -> const CodeLengths& { return lengths_; }

private:
    Huffman() = default;
    CodeLengths lengths_{};
    std::array<std::uint16_t, kSymbolCount> codes_{};
    // Canonical decode: the codes of one length are consecutive, from
    // first_code_[length]; sorted_ lists the symbols in code order.
    std::array<std::uint32_t, 16> first_code_{};
    std::array<std::uint16_t, 16> count_{};
    std::array<std::uint16_t, 16> offset_{};
    std::array<std::uint8_t, kSymbolCount> sorted_{};
};

// The lengths an `AF 81` packet (130 bytes) installs, as FUN_0052a8d0 reads
// them: 128 bytes, low nibble then high, each nibble + 1. It starts at +1,
// the 0x81 itself (so bytes 0 and 1 get lengths 2 and 9) and never reads
// the packet's last byte. Unmodified hosts never send it.
auto reload_lengths(std::span<const std::uint8_t> packet) -> std::expected<CodeLengths, std::string>;

} // namespace d2d::net::d2gs
