// S->C frames (docs/research/re/net-join.md "Transport"): after the raw
// phase every server send is one compressed frame behind a 1- or 2-byte
// length that counts the header too. The client's receive thread
// (FUN_0052ab00) collects bytes, decompresses each whole frame and keeps a
// partial one for the next read. C->S has no frames (c2s.hpp).
#pragma once

#include "huffman.hpp"
#include "wire.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace d2d::net::d2gs {

// A frame's length, header included, is at most 0xfff.
inline constexpr std::size_t kMaxFrameSize = 0xfff;

// The server's side (FUN_0052b330): compress, then [c + 1] when that is
// under 0xf0, else [0xf0 | n >> 8, n & 0xff] with n = c + 2. Refuses a
// payload that doesn't fit 0xfff.
auto encode_frame(const Huffman& huffman, std::span<const std::uint8_t> payload) -> std::expected<Bytes, std::string>;

struct FrameHeader {
    std::size_t size = 0;        // the whole frame, header included
    std::size_t header_size = 0; // 1 or 2
};
// FUN_0052ab00's reading: size = b0 < 0xf0 ? b0 : (b0 & 0x0f) << 8 | b1,
// header 1 byte when size < 0xf0, else 2. It waits for 2 bytes even when
// the first is a whole 1-byte header. nullopt: not enough bytes yet.
auto read_frame_header(std::span<const std::uint8_t> bytes) -> std::optional<FrameHeader>;

// Collects received bytes and yields each whole frame's decompressed
// payload.
class FrameReader {
public:
    auto append(std::span<const std::uint8_t> received) -> void;
    // The next whole frame, decompressed with `huffman`; nullopt while a
    // frame is still partial. A size shorter than its own header (which
    // game.exe loops on) is an error.
    auto next(const Huffman& huffman) -> std::expected<std::optional<Bytes>, std::string>;
    auto buffered() const -> std::size_t { return pending_.size() - start_; }

private:
    Bytes pending_;
    std::size_t start_ = 0;
};

} // namespace d2d::net::d2gs
