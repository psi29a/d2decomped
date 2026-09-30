// Splitting the decompressed S->C stream into packets (FUN_0052a8d0,
// sizes from FUN_0052b920: docs/research/re/net-join.md "Splitter"), and
// the client's whole receive path: the raw phase, frames, the AF 81
// table reload. Nothing resynchronizes a stream: a size that can't be
// right ends it as a Desync.
#pragma once

#include "exe_tables.hpp"
#include "frame.hpp"
#include "huffman.hpp"
#include "wire.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2d::net::d2gs {

// No S->C packet is longer (FUN_0052a8d0 fails past it).
inline constexpr std::size_t kMaxPacketSize = 0x204;

struct Desync {
    std::string reason;
};

// Ids whose size FUN_0052b920 works out from the packet: 0x16, 0x26,
// 0x3e, 0x5b, 0x94, 0x9c, 0x9d, 0xa6, 0xa8, 0xaa, 0xac, 0xae, 0xaf, 0xb3.
auto has_variable_rule(std::uint8_t id) -> bool;

// The size of the packet starting `stream`, as FUN_0052b920 has it:
// nullopt when more bytes are needed first (each variable rule waits for
// a set number of bytes, e.g. 13 for 0x16, before it reads its length).
// An id past 0xb4, a size of 0 (never sent), a variable id with no rule,
// or a size past 0x204 is a Desync: game.exe stalls or dies there.
auto packet_size(std::span<const std::uint8_t> stream, const SizeTable& sizes) -> std::expected<std::optional<std::size_t>, Desync>;

// A persistent packet buffer, like game.exe's (+0x7b8 holds its fill): a
// packet may straddle frames.
class Splitter {
public:
    explicit Splitter(const SizeTable& sizes) : sizes_(sizes) {}
    auto append(std::span<const std::uint8_t> stream) -> void;
    // The next whole packet; nullopt while it's still partial.
    auto next() -> std::expected<std::optional<Bytes>, Desync>;
    auto buffered() const -> std::size_t { return pending_.size() - start_; }

private:
    SizeTable sizes_;
    Bytes pending_;
    std::size_t start_ = 0;
};

// The client's receive side, sans-IO: hand it what recv() returned and
// take the packets. Raw at first (the host's AF 01 comes unframed); the
// first AF with a nonzero byte 1 switches to frames for the next read.
// AF 81 installs its table for the frames after the one carrying it.
// ponytail: game.exe also stamps GetTickCount() into a received 0x8f at
// +0xd; the caller times pongs itself.
class Receiver {
public:
    Receiver(const Huffman& huffman, const SizeTable& sizes) : huffman_(huffman), splitter_(sizes) {}
    // Once it has returned a Desync it returns it again.
    auto receive(std::span<const std::uint8_t> received) -> std::expected<std::vector<Bytes>, Desync>;
    auto framed() const -> bool { return framed_; }
    auto huffman() const -> const Huffman& { return huffman_; }

private:
    auto drain(std::vector<Bytes>& packets) -> std::expected<void, Desync>;
    Huffman huffman_;
    FrameReader frames_;
    Splitter splitter_;
    bool framed_ = false;
    bool switch_to_frames_ = false;
    std::optional<Desync> failed_;
};

} // namespace d2d::net::d2gs
