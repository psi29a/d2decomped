// SPDX-License-Identifier: GPL-3.0-or-later
// A game joined on a game.exe host (--join): the connection, the join and
// the host's units as its packets place and move them
// (docs/design/net-join-plan.md, the weekend cut of M6). d2d's own World
// still builds Act 1 from the host's map seed (0x03) and walks the
// player; this adds the host's other players and monsters on top.
// ponytail: what's drawn is placed and walked, not fought: no hits,
// casts, items or own moves to the host yet (tier C).
#pragma once

#include <join.hpp>
#include <net_log.hpp>
#include <tcp.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct NetGame {
    // A unit the host told us about: positions in act subtiles.
    struct Unit {
        int type = 0;                 // 0 player, 1 monster (NPCs and mercs too)
        std::uint32_t id = 0;
        int cls = 0;                  // player: d2s class; monster: MonStats row
        std::string name;             // players
        float x = 0, y = 0;
        float goal_x = 0, goal_y = 0;
        bool moving = false;
        int life = 128;               // of 128
    };

    // Connects, uploads the save and waits (up to timeout_ms) until the host
    // has put the player in the world (0x04). The log goes to `log_path`.
    static auto join(const std::string& host, const std::filesystem::path& game_exe, std::vector<std::uint8_t> save,
                     const std::filesystem::path& log_path, int timeout_ms) -> std::expected<std::unique_ptr<NetGame>, std::string>;

    // What arrived, the ping when due, units walked on by `elapsed_ms`.
    auto pump(std::uint32_t now_ms, std::uint32_t elapsed_ms) -> void;
    // Our moves, as game.exe's client sends them (0x01 / 0x03, 0x53 / 0x54).
    auto move_to(float subtile_x, float subtile_y, bool run) -> void;
    auto set_running(bool run) -> void;
    // The host moved us (0x15: a warp, a waypoint, a correction): once.
    auto take_reassign() -> bool { return std::exchange(reassigned, false); }
    // 0x69, then up to a second for the host to close.
    auto leave() -> void;
    auto closed() const -> bool;

    d2d::net::JoinSession session;
    d2d::net::TcpConnection connection;
    d2d::net::NetLog log;
    std::uint32_t map_seed = 0;       // 0x03
    int act = 0, area = 0;            // 0x03
    int difficulty = 0;               // 0x01
    std::uint32_t self_id = 0;        // our 0x59 / 0x15
    float self_x = 0, self_y = 0;     // where the host put us (0x15)
    float host_x = 0, host_y = 0;     // where the host has us now (0x95 / 0x96 / 0x18)
    std::unordered_map<std::uint64_t, Unit> units;

private:
    bool socket_gone = false;
    bool reassigned = false;
    d2d::net::Bytes last_sent;
    std::uint32_t last_sent_ms = 0;
    NetGame(d2d::net::JoinSession joined, d2d::net::TcpConnection socket, const std::filesystem::path& log_path);
    auto handle(const d2d::net::Bytes& packet) -> void;
    auto send(const std::vector<d2d::net::Bytes>& packets) -> void;
};
