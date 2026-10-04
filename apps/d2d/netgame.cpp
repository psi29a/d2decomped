// SPDX-License-Identifier: GPL-3.0-or-later
#include "netgame.hpp"

#include "game_api.hpp"

#include <d2gs/c2s.hpp>
#include <d2gs/exe_tables.hpp>
#include <d2gs/wire.hpp>
#include <log.hpp>

#include <bitset>
#include <chrono>
#include <cmath>
#include <utility>

namespace {

using d2d::net::d2gs::read_name;
using d2d::net::d2gs::read_u16;
using d2d::net::d2gs::read_u32;

auto key(int type, std::uint32_t id) -> std::uint64_t { return std::uint64_t(type) << 32 | id; }

// The S->C ids NetGame acts on (the net log's "used").
auto used_ids() -> std::bitset<256> {
    std::bitset<256> used;
    for (const int id : { 0x01, 0x02, 0x03, 0x04, 0x06, 0x0a, 0x0d, 0x0f, 0x15, 0x18, 0x59, 0x5c, 0x67, 0x68, 0x6d, 0x95, 0x96, 0xac, 0xab, 0xaf, 0xb3, 0xb4 })
        used.set(std::size_t(id));
    return used;
}

auto steady_ms() -> std::uint32_t {
    static const auto start = std::chrono::steady_clock::now();
    return std::uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

constexpr float kWalkSubtilesPerSec = d2d::game::cells_per_sec(6) * 5;   // ponytail: everyone at a player's walk (6); MonStats Velocity later

} // namespace

NetGame::NetGame(d2d::net::JoinSession joined, d2d::net::TcpConnection socket, const std::filesystem::path& log_path)
    : session(std::move(joined)), connection(std::move(socket)), log(log_path, used_ids()) {}

auto NetGame::join(const std::string& host, const std::filesystem::path& game_exe, std::vector<std::uint8_t> save,
                   const std::filesystem::path& log_path, int timeout_ms) -> std::expected<std::unique_ptr<NetGame>, std::string> {
    const auto tables = d2d::net::d2gs::load_exe_tables(game_exe);
    if (!tables) return std::unexpected(tables.error());
    auto session = d2d::net::JoinSession::create(*tables, std::move(save));
    if (!session) return std::unexpected(session.error());
    auto connection = d2d::net::TcpConnection::connect(host, 4000, 5000);
    if (!connection) return std::unexpected(connection.error());
    std::unique_ptr<NetGame> game(new NetGame(std::move(*session), std::move(*connection), log_path));
    game->log.note("joining " + host + " as " + game->session.name());
    const auto deadline = steady_ms() + std::uint32_t(timeout_ms);
    while (game->session.state() != d2d::net::JoinState::InGame) {
        if (steady_ms() > deadline) return std::unexpected("the host didn't put us in the game within " + std::to_string(timeout_ms / 1000) + " s");
        game->pump(steady_ms(), 0);
        if (game->closed()) {
            if (game->session.state() == d2d::net::JoinState::Refused) return std::unexpected("the host refused the join (reason " + std::to_string(game->session.refused_reason()) + ")");
            return std::unexpected(std::string("the join ended: ") + d2d::net::join_state_name(game->session.state()) + " " + game->session.desync_reason());
        }
    }
    game->log.note("in the game: map seed " + std::to_string(game->map_seed) + ", difficulty " + std::to_string(game->difficulty));
    return game;
}

auto NetGame::send(const std::vector<d2d::net::Bytes>& packets) -> void {
    for (const auto& packet : packets) {
        // FUN_00478350: a packet the same as the last one sent within 200 ms
        // is dropped (a held button doesn't flood).
        const auto now = steady_ms();
        if (packet == last_sent && now - last_sent_ms < 200) continue;
        last_sent = packet;
        last_sent_ms = now;
        log.to_host(packet);
        if (auto sent = connection.send(packet); !sent) log.note("send failed: " + sent.error());
    }
}

auto NetGame::closed() const -> bool {
    const auto state = session.state();
    return socket_gone || state == d2d::net::JoinState::Closed || state == d2d::net::JoinState::Refused || state == d2d::net::JoinState::Desync;
}

auto NetGame::pump(std::uint32_t now_ms, std::uint32_t elapsed_ms) -> void {
    if (!closed()) {
        if (auto received = connection.receive(0); !received) {
            log.note(received.error());
            d2d::log::warn("net: {}", received.error());
            socket_gone = true;
        } else if (!received->empty()) {
            auto step = session.receive(*received, now_ms);
            for (const auto& packet : step.packets) {
                log.from_host(packet);
                handle(packet);
            }
            send(step.to_send);
            if (session.state() == d2d::net::JoinState::Desync) d2d::log::warn("net: the stream broke: {}", session.desync_reason());
        }
        send(session.tick(now_ms));
    }
    const float step = kWalkSubtilesPerSec * float(elapsed_ms) / 1000.f;
    for (auto& [unit_key, unit] : units) {
        if (!unit.moving) continue;
        const float dx = unit.goal_x - unit.x, dy = unit.goal_y - unit.y, distance = std::hypot(dx, dy);
        if (distance <= step) { unit.x = unit.goal_x; unit.y = unit.goal_y; unit.moving = false; continue; }
        unit.x += dx / distance * step;
        unit.y += dy / distance * step;
    }
}

auto NetGame::handle(const d2d::net::Bytes& packet) -> void {
    auto unit_at = [&](int type, std::uint32_t id) -> Unit& {
        auto& unit = units[key(type, id)];
        unit.type = type;
        unit.id = id;
        return unit;
    };
    auto place = [](Unit& unit, float x, float y) { unit.x = unit.goal_x = x; unit.y = unit.goal_y = y; unit.moving = false; };
    auto walk = [](Unit& unit, float x, float y) { unit.goal_x = x; unit.goal_y = y; unit.moving = true; };
    const std::size_t size = packet.size();
    switch (packet[0]) {
    case 0x01: if (size >= 2) difficulty = packet[1]; break;
    case 0x03:
        if (size >= 8) { act = packet[1]; map_seed = read_u32(packet, 2); area = read_u16(packet, 6); }
        break;
    case 0x59:   // assign player: id, class, name, x, y (ours comes at 0, 0: its place is the 0x15)
        if (size >= 26) {
            auto& unit = unit_at(0, read_u32(packet, 1));
            unit.cls = packet[5];
            unit.name = std::string(read_name(packet, 6, 16));
            if (unit.name == session.name()) self_id = unit.id;
            place(unit, read_u16(packet, 0x16), read_u16(packet, 0x18));
        }
        break;
    case 0x15:   // reassign: type, id, x, y
        if (size >= 10) {
            auto& unit = unit_at(packet[1], read_u32(packet, 2));
            place(unit, read_u16(packet, 6), read_u16(packet, 8));
            if (unit.type == 0 && unit.id == self_id) { self_x = host_x = unit.x; self_y = host_y = unit.y; reassigned = true; }
        }
        break;
    case 0x0f:   // a unit to x, y: +1 type, +2 id, +7 target, +0xc where it is
        if (size >= 16) {
            auto& unit = unit_at(packet[1], read_u32(packet, 2));
            unit.x = read_u16(packet, 0xc); unit.y = read_u16(packet, 0xe);
            walk(unit, read_u16(packet, 7), read_u16(packet, 9));
        }
        break;
    case 0x95:   // our life, mana, stamina and place: bits 8 id, 15, 15, 15, 16 x, 16 y
    case 0x96:   // ... walk verify: 8 id, 15 stamina, 16 x, 16 y
    case 0x18: { // ... 8 id, 15, 15, 15, 7, 7, 16 x, 16 y
        const std::size_t skip = packet[0] == 0x96 ? 23 : packet[0] == 0x95 ? 53 : 67;
        if (size * 8 < skip + 32) break;
        auto bits_at = [&](std::size_t first, int count) {
            std::uint32_t value = 0;
            for (int bit = 0; bit < count; ++bit) value |= std::uint32_t(packet[(first + std::size_t(bit)) / 8] >> ((first + std::size_t(bit)) % 8) & 1) << bit;
            return value;
        };
        host_x = float(bits_at(skip, 16));
        host_y = float(bits_at(skip + 16, 16));
        break;
    }
    case 0x0d:   // a unit stops at x, y
        if (size >= 11) place(unit_at(packet[1], read_u32(packet, 2)), read_u16(packet, 7), read_u16(packet, 9));
        break;
    case 0xac:   // assign monster: id, MonStats row, x, y, life
        if (size >= 12) {
            auto& unit = unit_at(1, read_u32(packet, 1));
            unit.cls = read_u16(packet, 5);
            place(unit, read_u16(packet, 7), read_u16(packet, 9));
            unit.life = packet[0xb];
        }
        break;
    case 0x67:   // monster to x, y
        if (size >= 10) walk(unit_at(1, read_u32(packet, 1)), read_u16(packet, 6), read_u16(packet, 8));
        break;
    case 0x68:   // monster to a unit, from where it is
        if (size >= 10) place(unit_at(1, read_u32(packet, 1)), read_u16(packet, 6), read_u16(packet, 8));
        break;
    case 0x6d:   // monster stops at x, y
        if (size >= 10) { auto& unit = unit_at(1, read_u32(packet, 1)); place(unit, read_u16(packet, 5), read_u16(packet, 7)); unit.life = packet[9]; }
        break;
    case 0xab:   // heal: type, id, life
        if (size >= 7) unit_at(packet[1], read_u32(packet, 2)).life = packet[6];
        break;
    case 0x0a:   // remove: type, id
        if (size >= 6) units.erase(key(packet[1], read_u32(packet, 2)));
        break;
    case 0x5c:   // a player leaves
        if (size >= 5) units.erase(key(0, read_u32(packet, 1)));
        break;
    default:
        break;
    }
}

auto NetGame::move_to(float subtile_x, float subtile_y, bool run) -> void {
    if (session.state() != d2d::net::JoinState::InGame || subtile_x < 0 || subtile_y < 0) return;
    send({ d2d::net::d2gs::c2s::move_to(std::uint16_t(subtile_x), std::uint16_t(subtile_y), run) });
}

auto NetGame::set_running(bool run) -> void {
    if (session.state() == d2d::net::JoinState::InGame) send({ d2d::net::d2gs::c2s::set_running(run) });
}

auto NetGame::leave() -> void {
    send(session.leave());
    const auto until = steady_ms() + 1000;
    while (!closed() && steady_ms() < until) pump(steady_ms(), 0);
    d2d::log::info("net: left; {} unknown packets this game", log.unknown_count());
    log.summary();
}
