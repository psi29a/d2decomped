// SPDX-License-Identifier: GPL-3.0-or-later
// A game joined on a game.exe host (--join): the connection, the join and
// the host's units as its packets place and move them
// (docs/design/net-join-plan.md, the weekend cut of M6). d2d's own World
// still builds Act 1 from the host's map seed (0x03) and walks the
// player; this adds the host's other players and monsters on top.
// ponytail: what's drawn is placed and walked, not fought: no hits,
// casts, items or own moves to the host yet (tier C).
#pragma once

#include <d2s_items.hpp>
#include <join.hpp>
#include <net_log.hpp>
#include <tcp.hpp>

#include <array>
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
        int type = 0;                 // 0 player, 1 monster (NPCs and mercs too), 2 object, 5 warp
        std::uint32_t id = 0;
        int cls = 0;                  // player: d2s class; monster: MonStats row
        std::string name;             // players
        float x = 0, y = 0;
        float goal_x = 0, goal_y = 0;
        bool moving = false;
        int life = 128;               // of 128
        int mode = -1;                // a monster's last unit command (0x69..0x6c: 8 dying, 0xa attack, ...), -1 none
        std::uint32_t mode_ms = 0;    // when it came (steady ms)
    };

    // An item on the ground (0x9c actions 0 / 2 / 3): act subtiles.
    struct GroundItem {
        std::uint32_t id = 0;
        d2d::d2s::Item item;
        int x = 0, y = 0, gold = 0;
    };

    // Connects, uploads the save and waits (up to timeout_ms) until the host
    // has put the player in the world (0x04). The log goes to `log_path`.
    static auto join(const std::string& host, const std::filesystem::path& game_exe, std::vector<std::uint8_t> save,
                     const std::filesystem::path& log_path, int timeout_ms, const d2d::d2s::ItemTables* item_tables)
        -> std::expected<std::unique_ptr<NetGame>, std::string>;

    // What arrived, the ping when due, units walked on by `elapsed_ms`.
    auto pump(std::uint32_t now_ms, std::uint32_t elapsed_ms) -> void;
    // Our moves, as game.exe's client sends them (0x01 / 0x03, 0x53 / 0x54).
    auto move_to(float subtile_x, float subtile_y, bool run) -> void;
    auto set_running(bool run) -> void;
    // Skills: the hand's skill first when it changed (0x3c), then 05 / 0c
    // at a point or 06 / 0d on a unit.
    auto skill_at(int skill, bool left, float subtile_x, float subtile_y) -> void;
    auto skill_on(int skill, bool left, int type, std::uint32_t id) -> void;
    auto interact(int type, std::uint32_t id) -> void;
    auto waypoint(std::uint32_t id, int level) -> void;
    auto pick_up(std::uint32_t id) -> void;
    // The host's id for one of our items as d2d has it: same code, same
    // place (grid cell and panel, body slot, belt box, cursor); 0: none.
    auto host_item(const d2d::d2s::Item& local) const -> std::uint32_t;
    auto send_items(const std::vector<d2d::net::Bytes>& packets) -> void { if (session.state() == d2d::net::JoinState::InGame) send(packets); }
    // The host's unit of `type` (2 object, 5 warp) nearest act subtile
    // (x, y), of `cls` when >= 0; nullptr when none within `within`.
    auto nearest(int type, int cls, float subtile_x, float subtile_y, float within) const -> const Unit*;
    // The host moved us (0x15: a warp, a waypoint, a correction): once.
    auto take_reassign() -> bool { return std::exchange(reassigned, false); }
    // 0x69, then up to a second for the host to close.
    auto leave() -> void;
    auto closed() const -> bool;
    static auto steady_now() -> std::uint32_t;   // the clock unit.mode_ms is on

    d2d::net::JoinSession session;
    d2d::net::TcpConnection connection;
    d2d::net::NetLog log;
    std::uint32_t map_seed = 0;       // 0x03
    int act = 0, area = 0;            // 0x03
    int difficulty = 0;               // 0x01
    std::uint32_t self_id = 0;        // our 0x59 / 0x15
    std::uint32_t merc_id = 0;        // our merc (0x81): d2d's World draws its own
    float self_x = 0, self_y = 0;     // where the host put us (0x15)
    float host_x = 0, host_y = 0;     // where the host has us now (0x95 / 0x96 / 0x18)
    std::uint32_t walked_ms = 0;      // the host's last word of our walk (0x96), steady ms: still walking
    std::unordered_map<std::uint64_t, Unit> units;
    std::unordered_map<std::uint32_t, GroundItem> ground;   // by the host's item id
    std::unordered_map<std::uint32_t, d2d::d2s::Item> own_items;   // ours as the host has them (grids, body, belt, cursor), by its ids
    std::unordered_map<std::uint32_t, d2d::d2s::Item> store_items;   // the open store's stock (0x9c 0xb), by its ids
    std::uint32_t trade_npc = 0;               // the NPC we trade with (its unit id), 0 none
    std::uint32_t npc_info = 0;                // the unit the host last sent NPC info for (0x27): talking
    std::uint32_t picking = 0;                 // the ground item we asked for (0x16), until it lands with us
    bool buying = false;                       // a buy went out: the next new item in our bags is it
    std::vector<d2d::d2s::Item> picked;        // what came to us since (0x9c into a grid / the belt): the client takes them
    // Our stats as the host sets them (0x19..0x1f): id, value, delta (true:
    // add). The client takes them each frame.
    struct StatChange { int id = 0; std::int64_t value = 0; bool add = false; };
    std::vector<StatChange> stat_changes;

private:
    bool socket_gone = false;
    const d2d::d2s::ItemTables* item_tables = nullptr;
    bool reassigned = false;
    d2d::net::Bytes last_sent;
    std::uint32_t last_sent_ms = 0;
    std::array<int, 2> hand_skill{ -1, -1 };   // what the host has on the right / left hand
    auto select(int skill, bool left) -> void;
    NetGame(d2d::net::JoinSession joined, d2d::net::TcpConnection socket, const std::filesystem::path& log_path);
    auto handle(const d2d::net::Bytes& packet) -> void;
    auto send(const std::vector<d2d::net::Bytes>& packets) -> void;
};
