// game.exe's bit buffer (docs/research/re/bit-buffer.md). The state is
// FUN_00410e40's five words: the byte pointer, the length in bits, the
// byte, the bit, the overflow flag; FUN_00410e90 is BitCursor::bytes_used.
// Sums are game.exe's 32-bit ones.
// Verified: tools/emu/bit_buffer.py runs game.exe's functions on the same
// cases as tests/test_bit_buffer.cpp.
#include "bit_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace d2d::net::d2gs {

namespace {

// 32-bit wrapping sums, as the x86 adds.
auto wrap(std::int64_t value) -> std::int32_t { return std::int32_t(std::uint32_t(std::uint64_t(value) & 0xffffffffu)); }

// The low n bits, n 0..8 (the dword table at 0x6ce368).
auto low_mask(std::int32_t count) -> std::uint32_t { return (1u << count) - 1u; }

}  // namespace

// FUN_00410e40: the bytes, their length in bits, at bit 0. Its null-reader
// assert (FUN_00408090(0x13c)) has no counterpart: a span is never null.
BitReader::BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) { cursor_.bits = wrap(std::int64_t(bytes.size()) * 8); }

BitWriter::BitWriter(std::span<std::uint8_t> bytes) : bytes_(bytes) { cursor_.bits = wrap(std::int64_t(bytes.size()) * 8); }

// FUN_00410f60 (FUN_00411020 is a jmp to it): the bits past the end,
// floored at 0, are dropped and set the flag; the rest come a byte's worth
// at a time, each added in at the count read so far (shl by that & 31, so
// a count over 32 folds its high bits back onto the low ones).
auto BitReader::read(std::int32_t count) -> std::uint32_t {
    BitCursor& state = cursor_;
    std::int32_t past = wrap(std::int64_t(wrap(std::int64_t(state.byte) * 8)) - state.bits + count + state.bit);
    if (past < 0) past = 0;
    std::int32_t left = wrap(std::int64_t(count) - past);
    if (past != 0) state.overflow = true;
    if (left <= 0) return 0;
    std::uint32_t value = 0;
    std::int32_t done = 0;
    do {
        const std::int32_t chunk = left < 8 - state.bit ? left : 8 - state.bit;
        const std::uint32_t piece = (std::uint32_t(bytes_[std::size_t(state.byte)]) >> state.bit) & low_mask(chunk);
        value += piece << (std::uint32_t(done) & 31u);
        done += chunk;
        state.bit += chunk;
        left -= chunk;
        if (state.bit == 8) {
            ++state.byte;
            state.bit = 0;
        }
    } while (left > 0);
    return value;
}

// FUN_00411030: count < 32 (signed) with bit count - 1 set ORs in the
// bits above it.
auto BitReader::read_signed(std::int32_t count) -> std::int32_t {
    std::uint32_t value = read(count);
    if (count < 32 && (value & (1u << (std::uint32_t(count - 1) & 31u))) != 0) value |= ~((1u << (std::uint32_t(count) & 31u)) - 1u);
    return std::int32_t(value);
}

// game.exe's stores go through its byte pointer (word +0), which moves
// with the byte but not on an overflow (below): a write of 0 bits after
// one zeroes the byte the pointer was left state. At the very end (a count of
// 0 or less with every bit used) FUN_00410eb0 stores its zero one byte past
// the buffer; d2d skips it.
auto BitWriter::zero_current() -> void {
    if (pointer_ < bytes_.size()) bytes_[pointer_] = 0;
}

// FUN_00410eb0: a write past the end writes nothing and moves to byte
// bits / 8, bit 0, the pointer left where it was. Otherwise each chunk ORs (low byte of value & mask) <<
// bit in; a byte filled moves on and zeroes the next while in range, and
// the value shifts right (sar: a count over 32 writes its sign).
auto BitWriter::write(std::int32_t value, std::int32_t count) -> void {
    BitCursor& state = cursor_;
    if (wrap(std::int64_t(state.bit) + wrap(std::int64_t(state.byte) * 8) + count) > state.bits) {
        state.overflow = true;
        state.byte = state.bits / 8;
        state.bit = 0;
        return;
    }
    if (state.bit == 0) zero_current();
    while (count > 0) {
        const std::int32_t chunk = count < 8 - state.bit ? count : 8 - state.bit;
        const auto piece = std::uint8_t((low_mask(chunk) & std::uint32_t(value) & 0xffu) << state.bit);
        bytes_[pointer_] = std::uint8_t(bytes_[pointer_] | piece);
        state.bit += chunk;
        if (state.bit != 8) return;
        ++state.byte;
        ++pointer_;
        state.bit = 0;
        if (wrap(std::int64_t(state.byte) * 8) < state.bits) zero_current();
        value >>= chunk;
        count -= chunk;
    }
}

// FUN_00411070: mid-byte moves to the next and zeroes it while in range.
auto BitWriter::align() -> void {
    BitCursor& state = cursor_;
    if (state.bit == 0) return;
    ++state.byte;
    ++pointer_;
    state.bit = 0;
    if (wrap(std::int64_t(state.byte) * 8) < state.bits) zero_current();
}

}  // namespace d2d::net::d2gs
