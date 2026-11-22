// game.exe's bit buffer: bits LSB-first in a fixed run of bytes, an
// overflow flag in place of a bounds error (docs/research/re/bit-buffer.md).
// BitReader reads a packet, BitWriter fills one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace d2d::net::d2gs {

// Where a buffer stands: the length in bits, the byte, the bit in it, and
// whether a read or write went past the end.
struct BitCursor {
    std::int32_t bits = 0;
    std::int32_t byte = 0;
    std::int32_t bit = 0;
    bool overflow = false;
    // The bytes started, a partly used one counted whole.
    [[nodiscard]] auto bytes_used() const -> std::int32_t { return byte + (bit != 0 ? 1 : 0); }
};

class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> bytes);
    // `count` bits as an unsigned number. Past the end: the bits left, the
    // overflow flag set. 0 for a count of 0 or less.
    auto read(std::int32_t count) -> std::uint32_t;
    // The same, sign-extended from bit count - 1 when count is under 32.
    auto read_signed(std::int32_t count) -> std::int32_t;
    [[nodiscard]] auto cursor() const -> const BitCursor& { return cursor_; }

private:
    std::span<const std::uint8_t> bytes_;
    BitCursor cursor_;
};

class BitWriter {
public:
    explicit BitWriter(std::span<std::uint8_t> bytes);
    // The low `count` bits of value. A write that doesn't fit writes
    // nothing, sets the overflow flag and moves to the last whole byte.
    auto write(std::int32_t value, std::int32_t count) -> void;
    // To the next whole byte, zeroing it while it is in range.
    auto align() -> void;
    [[nodiscard]] auto cursor() const -> const BitCursor& { return cursor_; }
    // The byte stores go to: cursor().byte, until a write overflows.
    [[nodiscard]] auto pointer() const -> std::size_t { return pointer_; }

private:
    std::span<std::uint8_t> bytes_;
    BitCursor cursor_;
    std::size_t pointer_ = 0;
    auto zero_current() -> void;
};

}  // namespace d2d::net::d2gs
