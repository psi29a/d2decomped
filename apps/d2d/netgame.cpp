// SPDX-License-Identifier: GPL-3.0-or-later
#include "netgame.hpp"

#include "game_api.hpp"

#include <d2gs/c2s.hpp>
#include <d2gs/c2s_names.hpp>
#include <d2gs/exe_tables.hpp>
#include <d2gs/s2c_names.hpp>
#include <d2gs/wire.hpp>
#include <log.hpp>
#include <d2s_items.hpp>
#include <join.hpp>
#include <tcp.hpp>

#include <bitset>
#include <chrono>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using d2d::net::d2gs::read_name;
using d2d::net::d2gs::read_u16;
using d2d::net::d2gs::read_u32;

auto key(int type, std::uint32_t id) -> std::uint64_t { return std::uint64_t(type) << 32 | id; }

// The S->C ids NetGame acts on (the net log's "used").
auto used_ids() -> std::bitset<256> {
    std::bitset<256> used;
    for (const int id : { 0x01, 0x02, 0x03, 0x04, 0x06, 0x09, 0x27, 0x5d, 0x81, 0x9c, 0x9d, 0x42, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x0a, 0x0c, 0x0d, 0x0f, 0x15, 0x18, 0x51, 0x59, 0x5c, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x95, 0x96, 0xac, 0xab, 0xaf, 0xb3, 0xb4 })
        used.set(std::size_t(id));
    return used;
}

// d2d.log's view of the stream (the net log file has all of it):
// warning for an id nobody has looked at, debug every packet, trace bytes.
auto log_packet(std::string_view arrow, std::string_view name, const d2d::net::Bytes& packet) -> void {
    if (packet.empty()) return;
    auto bytes = [&] { std::string out; for (const auto byte : packet) out += std::format(" {:02x}", byte); return out; };
    if (name.empty()) d2d::log::warn("net: {} {:02x} unknown ({} bytes):{}", arrow, packet[0], packet.size(), bytes());
    else d2d::log::debug("net: {} {:02x} {} ({} bytes)", arrow, packet[0], name, packet.size());
    if (d2d::log::enabled(d2d::log::Level::trace)) d2d::log::trace("net: {} {:02x}{}", arrow, packet[0], bytes());
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
                   const std::filesystem::path& log_path, int timeout_ms, const d2d::d2s::ItemTables* item_tables)
    -> std::expected<std::unique_ptr<NetGame>, std::string> {
    const auto tables = d2d::net::d2gs::load_exe_tables(game_exe);
    if (!tables) return std::unexpected(tables.error());
    auto session = d2d::net::JoinSession::create(*tables, std::move(save));
    if (!session) return std::unexpected(session.error());
    auto connection = d2d::net::TcpConnection::connect(host, 4000, 5000);
    if (!connection) return std::unexpected(connection.error());
    std::unique_ptr<NetGame> game(new NetGame(std::move(*session), std::move(*connection), log_path));
    game->item_tables = item_tables;
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
        log_packet("C>S", d2d::net::d2gs::c2s_name(packet[0]), packet);
        log.to_host(packet);
        if (auto sent = connection.send(packet); !sent) log.note("send failed: " + sent.error());
    }
}

auto NetGame::steady_now() -> std::uint32_t { return steady_ms(); }

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
                log_packet("S>C", d2d::net::d2gs::s2c_name(packet[0]), packet);
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
        if (packet[0] == 0x96) walked_ms = steady_ms();
        // Life, mana, stamina: whole points, stats 6 / 8 / 10 take them << 8
        // (FUN_0045d4b0); 0x96 has stamina alone.
        if (packet[0] == 0x96) {
            stat_changes.push_back({ 10, std::int64_t(bits_at(8, 15)) << 8, false });
        } else {
            stat_changes.push_back({ 6, std::int64_t(bits_at(8, 15)) << 8, false });
            stat_changes.push_back({ 8, std::int64_t(bits_at(23, 15)) << 8, false });
            stat_changes.push_back({ 10, std::int64_t(bits_at(38, 15)) << 8, false });
        }
        break;
    }
    case 0x51:   // assign object: +1 type (2), +2 id, +6 Objects.txt row, +8 x, +0xa y
        if (size >= 12) {
            auto& unit = unit_at(packet[1], read_u32(packet, 2));
            unit.cls = read_u16(packet, 6);
            place(unit, read_u16(packet, 8), read_u16(packet, 0xa));
        }
        break;
    case 0x09:   // assign warp: +1 type (5), +2 id, +6 LvlWarp row, +7 x, +9 y
        if (size >= 11) {
            auto& unit = unit_at(packet[1], read_u32(packet, 2));
            unit.cls = packet[6];
            place(unit, read_u16(packet, 7), read_u16(packet, 9));
        }
        break;
    case 0x9c: {  // an item in the world: +1 action, +4 id, the item from +8
        if (size < 9 || !item_tables) break;
        const int action = packet[1];
        const std::uint32_t id = read_u32(packet, 4);
        auto parsed = d2d::d2s::parse_net_item(std::as_bytes(std::span(packet)).subspan(8), *item_tables);
        if (!parsed) { log.note("item " + std::to_string(id) + " (0x9c action " + std::to_string(action) + ") didn't parse"); break; }
        if (action == 0 || action == 2 || action == 3) {
            own_items.erase(id);
            ground[id] = GroundItem{ id, std::move(parsed->item), parsed->x, parsed->y, parsed->gold };
        } else if (action == 0xb) {
            store_items[id] = std::move(parsed->item);                 // into the open store's stock
        } else if (action == 0xc) {
            store_items.erase(id);
        } else {                                                       // ours, somewhere
            ground.erase(id);
            if ((action == 4 || action == 0xe) && (id == picking || (buying && !own_items.contains(id)))) {
                picked.push_back(parsed->item);
                picking = 0;
                buying = false;
            }
            own_items[id] = std::move(parsed->item);
        }
        break;
    }
    case 0x9d: {  // an item owned by a unit: +8 owner type, +9 owner id, the item from +13
        if (size < 14 || !item_tables || packet[8] != 0 || read_u32(packet, 9) != self_id) break;
        const std::uint32_t id = read_u32(packet, 4);
        if (auto parsed = d2d::d2s::parse_net_item(std::as_bytes(std::span(packet)).subspan(13), *item_tables)) own_items[id] = std::move(parsed->item);
        else log.note("item " + std::to_string(id) + " (0x9d action " + std::to_string(packet[1]) + ") didn't parse");
        break;
    }
    case 0x42:   // our cursor's item is gone
        std::erase_if(own_items, [](const auto& entry) { return entry.second.location == d2d::d2s::item_location::kCursor; });
        break;
    case 0x19: if (size >= 2) stat_changes.push_back({ 14, packet[1], true }); break;   // gold +=
    case 0x1a: if (size >= 2) stat_changes.push_back({ 13, packet[1], true }); break;   // experience +=
    case 0x1b: if (size >= 3) stat_changes.push_back({ 13, read_u16(packet, 1), true }); break;
    case 0x1c: if (size >= 5) stat_changes.push_back({ 13, read_u32(packet, 1), false }); break;
    case 0x1d: if (size >= 3) stat_changes.push_back({ packet[1], packet[2], false }); break;   // stat =
    case 0x1e: if (size >= 4) stat_changes.push_back({ packet[1], read_u16(packet, 2), false }); break;
    case 0x1f: if (size >= 6) stat_changes.push_back({ packet[1], read_u32(packet, 2), false }); break;
    case 0x5d:   // quest log news: +1 quest, +2 flags (2: a sound), +3 log state, +4 s16 (quests.md)
        if (size >= 3 && packet[2] == 0) quest_news.push_back(packet[1]);
        break;
    case 0x27:   // NPC info: +1 mode, +2 unit id (the answer to an 0x13 on an NPC)
        if (size >= 6) npc_info = read_u32(packet, 2);
        break;
    case 0x81:   // assign merc: +2 u16 class, +4 u32 owner, +8 u32 merc id
        if (size >= 12 && read_u32(packet, 4) == self_id) merc_id = read_u32(packet, 8);
        break;
    case 0x0c:   // hit: +1 type, +2 id, +7 hit class, +8 life / 128
        if (size >= 9) {
            const int life = packet[8];
            unit_at(packet[1], read_u32(packet, 2)).life = life > 128 ? life & 0x7f : life;
        }
        break;
    case 0x69: case 0x6b:   // a monster's unit command at x, y (+5; net-packets.md "The unit command")
    case 0x6a: case 0x6c:   // ... on a unit
        if (size >= 6) {
            auto& unit = unit_at(1, read_u32(packet, 1));
            unit.mode = packet[5];
            unit.mode_ms = steady_ms();
            if (unit.mode == 8 || unit.mode == 9) { unit.life = 0; unit.moving = false; }   // dying / dead: no hit says so
        }
        break;
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
    case 0x0a:   // remove: type, id (4: an item)
        if (size >= 6) {
            units.erase(key(packet[1], read_u32(packet, 2)));
            if (packet[1] == 4) { ground.erase(read_u32(packet, 2)); own_items.erase(read_u32(packet, 2)); }
        }
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

auto NetGame::select(int skill, bool left) -> void {
    auto& held = hand_skill[left ? 1 : 0];
    if (held == skill) return;
    held = skill;
    send({ d2d::net::d2gs::c2s::select_skill(skill, left) });
}

auto NetGame::skill_at(int skill, bool left, float subtile_x, float subtile_y) -> void {
    if (session.state() != d2d::net::JoinState::InGame || subtile_x < 0 || subtile_y < 0) return;
    select(skill, left);
    send({ d2d::net::d2gs::c2s::skill_at(left, std::uint16_t(subtile_x), std::uint16_t(subtile_y)) });
}

auto NetGame::skill_on(int skill, bool left, int type, std::uint32_t id) -> void {
    if (session.state() != d2d::net::JoinState::InGame) return;
    select(skill, left);
    send({ d2d::net::d2gs::c2s::skill_on(left, std::uint32_t(type), id) });
}

auto NetGame::interact(int type, std::uint32_t id) -> void {
    if (session.state() == d2d::net::JoinState::InGame) send({ d2d::net::d2gs::c2s::interact(std::uint32_t(type), id) });
}

auto NetGame::waypoint(std::uint32_t id, int level) -> void {
    if (session.state() == d2d::net::JoinState::InGame) send({ d2d::net::d2gs::c2s::waypoint(id, std::uint16_t(level)) });
}

auto NetGame::pick_up(std::uint32_t id) -> void {
    if (session.state() != d2d::net::JoinState::InGame) return;
    picking = id;
    send({ d2d::net::d2gs::c2s::pick_up(id) });
}

auto NetGame::host_item(const d2d::d2s::Item& local) const -> std::uint32_t {
    using namespace d2d::d2s::item_location;
    for (const auto& [id, item] : own_items) {
        if (item.code != local.code || item.location != local.location) continue;
        const bool same = local.location == kStored ? item.panel == local.panel && item.column == local.column && item.row == local.row
                        : local.location == kEquipped ? item.slot == local.slot
                        : local.location == kBelt ? item.column == local.column
                        : true;                                    // the cursor holds one
        if (same) return id;
    }
    return 0;
}

auto NetGame::nearest(int type, int cls, float subtile_x, float subtile_y, float within) const -> const Unit* {
    const Unit* best = nullptr;
    float best_distance = within;
    for (const auto& [unit_key, unit] : units) {
        if (unit.type != type || (cls >= 0 && unit.cls != cls)) continue;
        const float distance = std::hypot(unit.x - subtile_x, unit.y - subtile_y);
        if (distance <= best_distance) { best = &unit; best_distance = distance; }
    }
    return best;
}

auto NetGame::leave() -> void {
    send(session.leave());
    const auto until = steady_ms() + 1000;
    while (!closed() && steady_ms() < until) pump(steady_ms(), 0);
    d2d::log::info("net: left; {} unknown packets this game", log.unknown_count());
    log.summary();
}
