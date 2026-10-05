// SPDX-License-Identifier: GPL-3.0-or-later
#include "join.hpp"

#include <d2gs/c2s.hpp>
#include <d2gs/exe_tables.hpp>
#include <d2gs/huffman.hpp>
#include <d2gs/wire.hpp>

#include <algorithm>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::net {

namespace {
constexpr std::size_t kMaxSave = 0x1fff;
constexpr std::uint32_t kPingEveryMs = 5000;   // FUN_0044efa0
} // namespace

auto join_state_name(JoinState state) -> const char* {
    switch (state) {
    case JoinState::Connecting: return "connecting";
    case JoinState::Uploading: return "uploading";
    case JoinState::Loading: return "loading";
    case JoinState::InGame: return "in game";
    case JoinState::Leaving: return "leaving";
    case JoinState::Closed: return "closed";
    case JoinState::Refused: return "refused";
    case JoinState::Desync: return "desync";
    }
    return "?";
}

auto valid_join_name(std::string_view name) -> bool {
    if (name.size() < 2 || name.size() > 15) return false;
    int marks = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char letter = name[i];
        if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')) continue;
        if ((letter != '\'' && letter != '-' && letter != '_') || i == 0 || i + 1 == name.size() || ++marks > 1) return false;
    }
    return true;
}

auto JoinSession::create(const d2gs::ExeTables& tables, std::vector<std::uint8_t> save) -> std::expected<JoinSession, std::string> {
    if (save.size() < 0x2c || d2gs::read_u32(save, 0) != 0xaa55aa55) return std::unexpected("not a .d2s save");
    if (save.size() > kMaxSave) return std::unexpected("save over 0x1fff bytes: the host can't take it");
    auto huffman = d2gs::Huffman::build(tables.lengths);
    if (!huffman) return std::unexpected(huffman.error());
    const auto name = d2gs::read_name(save, 0x14, 16);
    if (!valid_join_name(name)) return std::unexpected("the name \"" + std::string(name) + "\" can't join a game.exe host: letters and at most one ' - _ (not first or last)");
    JoinSession session(*huffman, tables.s2c_sizes);
    session.name_ = std::string(name);
    session.character_class_ = save[0x28];
    session.save_ = std::move(save);
    return session;
}

auto JoinSession::receive(std::span<const std::uint8_t> received, std::uint32_t now_ms) -> JoinStep {
    JoinStep step;
    if (state_ == JoinState::Desync || state_ == JoinState::Closed) return step;
    auto packets = receiver_.receive(received);
    if (!packets) {
        state_ = JoinState::Desync;
        desync_reason_ = packets.error().reason;
        return step;
    }
    for (const auto& packet : *packets) handle(packet, step.to_send);
    if (state_ == JoinState::InGame && !pinged_) last_ping_ms_ = now_ms, pinged_ = true;   // the first ping 5 s in
    step.packets = std::move(*packets);
    return step;
}

auto JoinSession::handle(const Bytes& packet, std::vector<Bytes>& to_send) -> void {
    switch (packet[0]) {
    case 0xaf:   // the host's hello, raw: the join and the whole save at once
        if (state_ == JoinState::Connecting) {
            to_send.push_back(d2gs::c2s::join_request(character_class_, name_));
            for (auto& chunk : d2gs::c2s::save_chunks(save_)) to_send.push_back(std::move(chunk));
            state_ = JoinState::Uploading;
        }
        break;
    case 0x02:   // load successful; a refused join still gets 01 00 02 after its B4: ignored
        if (state_ == JoinState::Uploading) {
            to_send.push_back(d2gs::c2s::load_ack());
            state_ = JoinState::Loading;
        }
        break;
    case 0x04:
        if (state_ == JoinState::Loading) state_ = JoinState::InGame;
        break;
    case 0xb4:
        state_ = JoinState::Refused;
        if (packet.size() >= 5) refused_reason_ = d2gs::read_u32(packet, 1);
        break;
    case 0xb3:   // [b3, n, flag, u32 total, n bytes]; a first chunk restarts it
        if (packet.size() >= 7) {
            if (packet[2] != 0) save_back_.clear();
            save_back_total_ = d2gs::read_u32(packet, 3);
            save_back_.insert(save_back_.end(), packet.begin() + 7, packet.begin() + 7 + std::min<std::size_t>(packet[1], packet.size() - 7));
        }
        break;
    case 0x06:
        if (state_ != JoinState::Refused) state_ = JoinState::Closed;
        break;
    default:
        break;
    }
}

auto JoinSession::tick(std::uint32_t now_ms) -> std::vector<Bytes> {
    if (state_ != JoinState::InGame || now_ms - last_ping_ms_ < kPingEveryMs) return {};
    last_ping_ms_ = now_ms;
    return { d2gs::c2s::ping(now_ms) };
}

auto JoinSession::leave() -> std::vector<Bytes> {
    if (state_ != JoinState::InGame && state_ != JoinState::Loading) return {};
    state_ = JoinState::Leaving;
    return { d2gs::c2s::leave() };
}

} // namespace d2d::net
