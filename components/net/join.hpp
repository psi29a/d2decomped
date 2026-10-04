// SPDX-License-Identifier: GPL-3.0-or-later
// Joining a game.exe TCP/IP host, sans-IO (docs/research/re/net-join.md,
// docs/design/net-join-plan.md M3): hand it what the socket received and
// the time, send what it returns. The order a live host keeps: raw AF 01
// -> 68 and every 6c at once -> 01 00 02 (answer 6b) -> 59 03 53 ... ->
// 04 in the world; 6d every 5 s; 69 to leave -> B3 chunks, B0, 05, 06.
// ponytail: the save the host sends back (B3) is kept, not written: a net
// game doesn't change the local save yet.
#pragma once

#include <d2gs/exe_tables.hpp>
#include <d2gs/huffman.hpp>
#include <d2gs/split.hpp>
#include <d2gs/wire.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace d2d::net {

using d2gs::Bytes;

enum class JoinState { Connecting, Uploading, Loading, InGame, Leaving, Closed, Refused, Desync };

auto join_state_name(JoinState state) -> const char*;

// What one receive() turned up: the S->C packets, in order, and the C->S
// packets to send now.
struct JoinStep {
    std::vector<Bytes> packets;
    std::vector<Bytes> to_send;
};

class JoinSession {
public:
    // The character comes from its save (name +0x14, class +0x28).
    // Refuses a save over 0x1fff bytes: a chunk past the host's buffer
    // kills the host process (net-join.md "Risks").
    static auto create(const d2gs::ExeTables& tables, std::vector<std::uint8_t> save) -> std::expected<JoinSession, std::string>;

    auto receive(std::span<const std::uint8_t> received, std::uint32_t now_ms) -> JoinStep;
    // The ping when one is due (InGame, 5 s apart); empty otherwise.
    auto tick(std::uint32_t now_ms) -> std::vector<Bytes>;
    // 0x69, once; the host then sends the save back and closes.
    auto leave() -> std::vector<Bytes>;

    auto state() const -> JoinState { return state_; }
    auto refused_reason() const -> std::uint32_t { return refused_reason_; }     // B4's code (net-join.md stage 1-3)
    auto desync_reason() const -> const std::string& { return desync_reason_; }
    auto save_back() const -> const Bytes& { return save_back_; }
    auto save_back_total() const -> std::uint32_t { return save_back_total_; }
    auto name() const -> const std::string& { return name_; }

private:
    JoinSession(const d2gs::Huffman& huffman, const d2gs::SizeTable& sizes) : receiver_(huffman, sizes) {}
    auto handle(const Bytes& packet, std::vector<Bytes>& to_send) -> void;

    d2gs::Receiver receiver_;
    Bytes save_;
    std::string name_;
    int character_class_ = 0;
    JoinState state_ = JoinState::Connecting;
    std::uint32_t refused_reason_ = 0;
    std::string desync_reason_;
    std::uint32_t last_ping_ms_ = 0;
    bool pinged_ = false;
    Bytes save_back_;
    std::uint32_t save_back_total_ = 0;
};

} // namespace d2d::net
