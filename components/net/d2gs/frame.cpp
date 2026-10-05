// Definitions for frame.hpp.
#include "frame.hpp"

#include "huffman.hpp"
#include "wire.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace d2d::net::d2gs {

auto encode_frame(const Huffman& huffman, std::span<const std::uint8_t> payload) -> std::expected<Bytes, std::string> {
    Bytes compressed;
    huffman.compress(payload, compressed);
    Bytes frame;
    if (compressed.size() + 1 < 0xf0) {
        put_u8(frame, static_cast<std::uint8_t>(compressed.size() + 1));
    } else {
        const std::size_t size = compressed.size() + 2;
        if (size > kMaxFrameSize) return std::unexpected(std::format("a {}-byte frame is over {:#x}", size, kMaxFrameSize));
        put_u8(frame, static_cast<std::uint8_t>(0xf0 | size >> 8));
        put_u8(frame, static_cast<std::uint8_t>(size));
    }
    put_bytes(frame, compressed);
    return frame;
}

auto read_frame_header(std::span<const std::uint8_t> bytes) -> std::optional<FrameHeader> {
    if (bytes.size() < 2) return std::nullopt;
    const std::size_t size = bytes[0] < 0xf0 ? bytes[0] : std::size_t{bytes[0] & 0x0fU} << 8 | bytes[1];
    return FrameHeader{size, size < 0xf0 ? std::size_t{1} : std::size_t{2}};
}

auto FrameReader::append(std::span<const std::uint8_t> received) -> void {
    // Drop what's been read before growing (the memmove at 0x52acc7).
    if (start_ > 0) {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(start_));
        start_ = 0;
    }
    put_bytes(pending_, received);
}

auto FrameReader::next(const Huffman& huffman) -> std::expected<std::optional<Bytes>, std::string> {
    const std::span<const std::uint8_t> rest = std::span(pending_).subspan(start_);
    const auto header = read_frame_header(rest);
    if (!header) return std::nullopt;
    if (header->size < header->header_size)
        return std::unexpected(std::format("frame size {} is shorter than its {}-byte header", header->size, header->header_size));
    if (rest.size() < header->size) return std::nullopt;
    Bytes payload;
    huffman.decompress(rest.subspan(header->header_size, header->size - header->header_size), payload);
    start_ += header->size;
    return payload;
}

} // namespace d2d::net::d2gs
