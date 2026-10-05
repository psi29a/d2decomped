// Definitions for split.hpp.
#include "split.hpp"

#include "exe_tables.hpp"
#include "huffman.hpp"
#include "wire.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace d2d::net::d2gs {

auto has_variable_rule(std::uint8_t id) -> bool {
    switch (id) {
    case 0x16: case 0x26: case 0x3e: case 0x5b: case 0x94: case 0x9c: case 0x9d:
    case 0xa6: case 0xa8: case 0xaa: case 0xac: case 0xae: case 0xaf: case 0xb3:
        return true;
    default:
        return false;
    }
}

namespace {

// The length of the NUL-terminated string at `offset`, if its NUL is there.
auto string_length(std::span<const std::uint8_t> stream, std::size_t offset) -> std::optional<std::size_t> {
    for (std::size_t i = offset; i < stream.size(); ++i)
        if (stream[i] == 0) return i - offset;
    return std::nullopt;
}

// A variable id's size, or nullopt to wait. `need` is how many bytes
// FUN_0052b920 wants before it looks (its `avail > need - 1` tests).
auto variable_size(std::span<const std::uint8_t> stream) -> std::optional<std::size_t> {
    const std::size_t avail = stream.size();
    auto when = [&](std::size_t need, auto size) -> std::optional<std::size_t> {
        if (avail < need) return std::nullopt;
        return size();
    };
    switch (stream[0]) {
    case 0x16: return when(13, [&] { return std::size_t{read_u16(stream, 1)}; });
    case 0x5b: return when(34, [&] { return std::size_t{read_u16(stream, 1)}; });
    case 0x3e: return when(2, [&] { return std::size_t{stream[1]}; });
    case 0x94: return when(9, [&] { return (std::size_t{stream[1]} + 2) * 3; });
    case 0x9c: case 0x9d: return when(3, [&] { return std::size_t{stream[2]}; });
    case 0xa6: return when(4, [&] { return std::size_t{read_u16(stream, 2)}; });
    case 0xa8: case 0xaa: return when(7, [&] { return std::size_t{stream[6]}; });
    case 0xac: return when(13, [&] { return std::size_t{stream[0xc]}; });
    // 0xae: u16 at +1 plus 3, the u16 read as 0 past 0x1fd.
    case 0xae: return when(3, [&] { const std::size_t body = read_u16(stream, 1); return (body > 0x1fd ? 0 : body) + 3; });
    case 0xaf: return when(2, [&] { return stream[1] != 0 ? std::size_t{stream[1]} + 1 : std::size_t{2}; });
    case 0xb3: return when(8, [&] { return std::size_t{stream[1]} + 7; });
    case 0x26: {
        // Two NUL-terminated strings from +10: 12 + both lengths.
        if (avail < 10) return std::nullopt;
        const auto first = string_length(stream, 10);
        if (!first || *first + 11 > avail) return std::nullopt;
        const auto second = string_length(stream, 10 + *first + 1);
        if (!second) return std::nullopt;
        return *first + 12 + *second;
    }
    default:
        return std::nullopt; // not reached: callers ask only for has_variable_rule ids
    }
}

} // namespace

auto packet_size(std::span<const std::uint8_t> stream, const SizeTable& sizes) -> std::expected<std::optional<std::size_t>, Desync> {
    if (stream.empty()) return std::nullopt;
    const std::uint8_t id = stream[0];
    if (id >= sizes.size()) return std::unexpected(Desync{std::format("S->C id {:#04x} is past 0xb4", id)});
    const std::int32_t fixed = sizes[id];
    if (fixed == 0) return std::unexpected(Desync{std::format("S->C id {:#04x} is never sent", id)});
    if (fixed > 0) {
        if (std::cmp_greater(fixed, kMaxPacketSize)) return std::unexpected(Desync{std::format("S->C id {:#04x} is {} bytes", id, fixed)});
        return std::size_t(fixed);
    }
    if (!has_variable_rule(id)) return std::unexpected(Desync{std::format("S->C id {:#04x} is variable with no rule", id)});
    const auto size = variable_size(stream);
    if (!size) {
        // Waiting past the longest packet means the length never comes.
        if (stream.size() >= kMaxPacketSize) return std::unexpected(Desync{std::format("S->C id {:#04x}: no length in {} bytes", id, stream.size())});
        return std::nullopt;
    }
    if (*size == 0 || *size > kMaxPacketSize) return std::unexpected(Desync{std::format("S->C id {:#04x} says {} bytes", id, *size)});
    return size;
}

auto Splitter::append(std::span<const std::uint8_t> stream) -> void {
    if (start_ > 0) {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(start_));
        start_ = 0;
    }
    put_bytes(pending_, stream);
}

auto Splitter::next() -> std::expected<std::optional<Bytes>, Desync> {
    const std::span<const std::uint8_t> rest = std::span(pending_).subspan(start_);
    const auto size = packet_size(rest, sizes_);
    if (!size) return std::unexpected(size.error());
    if (!*size || rest.size() < **size) return std::nullopt;
    Bytes packet(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(**size));
    start_ += **size;
    return packet;
}

auto Receiver::drain(std::vector<Bytes>& packets) -> std::expected<void, Desync> {
    while (true) {
        auto packet = splitter_.next();
        if (!packet) return std::unexpected(packet.error());
        if (!*packet) return {};
        const Bytes& bytes = **packet;
        if (bytes[0] == 0xaf && bytes[1] != 0) {
            // The raw phase ends after the read that brought this one.
            switch_to_frames_ = true;
            if (bytes[1] == 0x81) {
                auto lengths = reload_lengths(bytes);
                if (!lengths) return std::unexpected(Desync{lengths.error()});
                auto huffman = Huffman::build(*lengths);
                if (!huffman) return std::unexpected(Desync{"AF 81 table: " + huffman.error()});
                huffman_ = *huffman;
            }
        }
        packets.push_back(std::move(**packet));
    }
}

auto Receiver::receive(std::span<const std::uint8_t> received) -> std::expected<std::vector<Bytes>, Desync> {
    if (failed_) return std::unexpected(*failed_);
    std::vector<Bytes> packets;
    auto fail = [&](Desync desync) -> std::expected<std::vector<Bytes>, Desync> {
        failed_ = desync;
        return std::unexpected(std::move(desync));
    };
    if (!framed_) {
        // Raw: the bytes go straight to the splitter (FUN_0052ab00, phase 1).
        splitter_.append(received);
        if (auto drained = drain(packets); !drained) return fail(drained.error());
        framed_ = switch_to_frames_;
        return packets;
    }
    frames_.append(received);
    while (true) {
        auto payload = frames_.next(huffman_);
        if (!payload) return fail(Desync{payload.error()});
        if (!*payload) return packets;
        splitter_.append(**payload);
        if (auto drained = drain(packets); !drained) return fail(drained.error());
    }
}

auto c2s_packet_size(std::span<const std::uint8_t> stream, const C2sSizeTable& sizes) -> std::expected<std::optional<std::size_t>, Desync> {
    if (stream.empty()) return std::optional<std::size_t>{};
    const std::uint8_t id = stream[0];
    std::size_t size = 0;
    if (id == 0xff) {
        size = 16;
    } else if (id >= sizes.size()) {
        return std::unexpected(Desync{std::format("C->S id {:#04x} past 0x70", id)});
    } else if (id == 0x6c) {
        if (stream.size() < 2) return std::optional<std::size_t>{};
        size = std::size_t(stream[1]) + 7;
    } else if (id == 0x66) {
        if (stream.size() < 3) return std::optional<std::size_t>{};
        size = std::size_t(read_u16(stream, 1)) + 3;
    } else if (id == 0x14 || id == 0x15) {   // u8, u8 type, message\0, name\0
        int nuls = 0;
        for (std::size_t at = 3; at < stream.size() && at < kMaxPacketSize; ++at)
            if (stream[at] == 0 && ++nuls == 2) { size = at + 1; break; }
        if (!size) {
            if (stream.size() >= kMaxPacketSize) return std::unexpected(Desync{"C->S chat past 0x204 bytes"});
            return std::optional<std::size_t>{};
        }
    } else if (sizes[id] > 0) {
        size = std::size_t(sizes[id]);
    } else {
        return std::unexpected(Desync{std::format("C->S id {:#04x} isn't valid (size {})", id, sizes[id])});
    }
    if (size > kMaxPacketSize) return std::unexpected(Desync{std::format("C->S {:#04x} of {} bytes", id, size)});
    if (stream.size() < size) return std::optional<std::size_t>{};
    return std::optional<std::size_t>{size};
}

auto C2sSplitter::append(std::span<const std::uint8_t> stream) -> void {
    if (start_ > 0 && start_ == pending_.size()) { pending_.clear(); start_ = 0; }
    pending_.insert(pending_.end(), stream.begin(), stream.end());
}

auto C2sSplitter::next() -> std::expected<std::optional<Bytes>, Desync> {
    const auto rest = std::span<const std::uint8_t>(pending_).subspan(start_);
    auto size = c2s_packet_size(rest, sizes_);
    if (!size) return std::unexpected(size.error());
    if (!*size) return std::optional<Bytes>{};
    Bytes packet(rest.begin(), rest.begin() + std::ptrdiff_t(**size));
    start_ += **size;
    return std::optional<Bytes>{std::move(packet)};
}

} // namespace d2d::net::d2gs
