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
#include <quests.hpp>
#include <tcp.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
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
        bool running = false;         // a player's move is a run (command 0x17 / 0x18)
        int party = 0xffff;           // a player's party (0x8d, 0x75; 0xffff none)
        int party_life = 100;         // a party member's life % (0x7f)
        int level = 0;                // a player's level (0x75 +7)
        int area = 0;                 // a party member's level id (0x7f +8)
        int relation = 0;             // the party button's state toward it (0x8b: 0 invite, 2 accept, 3 leave, 4 asked; roster +0x30)
        int flags = 0;                // our relation flags toward it (0x8c: 1 loot, 2 not heard, 4 squelched, 8 hostile)
        int life = 128;               // of 128
        int mode = -1;                // a monster's last unit command (0x69..0x6c: 8 dying, 0xa attack, ...), -1 none
        std::uint32_t mode_ms = 0;    // when it came (steady ms)
        std::unordered_map<std::uint32_t, d2d::d2s::Item> worn;   // another player's: by item id (0x9d)
        int skill = -1;               // a player's last skill (0x4c / 0x4d), Skills.txt id
        std::uint32_t skill_ms = 0;   // when (steady ms)
        float skill_x = 0, skill_y = 0;   // where it went
        int velocity_pct = 100;       // a monster's stat 0x43 velocitypercent, from its moves (0x67 / 0x68)
        std::vector<int> states;      // the States.txt rows the host has on it (0xa8 / 0xaa set, 0xa9 end)
        int boss_flags = 0;           // a monster's 0xac boss bits: 1 champion, 2 unique, 4 superunique, 8 minion, 0x10 ghostly
        int super = -1;               // its SuperUniques row, -1 none
        std::vector<int> mods;        // its MonUMod mods
        int name_seed = 0;            // a unique's name roll
    };

    // An item on the ground (0x9c actions 0 / 2 / 3): act subtiles.
    struct GroundItem {
        std::uint32_t id = 0;
        d2d::d2s::Item item;
        int x = 0, y = 0, gold = 0;
    };

    // Why a join failed: d2d's account of it, and what game.exe would show
    // its player for (docs/research/re/tcpip-menu.md, "Join errors").
    struct JoinError {
        std::string what;
        bool no_connect = false;         // the TCP connect itself failed
        std::int64_t refused = -1;       // the host's B4 reason, when it refused
    };

    // Connects, uploads the save and waits (up to timeout_ms) until the host
    // has put the player in the world (0x04). The log goes to `log_path`.
    static auto join(const std::string& host, const std::filesystem::path& game_exe, std::vector<std::uint8_t> save,
                     const std::filesystem::path& log_path, int timeout_ms, const d2d::d2s::ItemTables* item_tables, bool auto_party = true,
                     std::vector<std::pair<int, int>> send_bits = {}, std::vector<std::array<std::uint8_t, 16>> monster_parts = {})
        -> std::expected<std::unique_ptr<NetGame>, JoinError>;

    // What arrived, the ping when due, units walked on by `elapsed_ms`.
    auto pump(std::uint32_t now_ms, std::uint32_t elapsed_ms) -> void;
    // Our moves, as game.exe's client sends them (0x01 / 0x03, 0x53 / 0x54).
    auto move_to(float subtile_x, float subtile_y, bool run) -> void;
    auto set_running(bool run) -> void;
    auto swap_weapons() -> void;   // C->S 0x60: the weapon switch
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
    // An item's use (0x20) waits for the host's player to stop: a walking
    // player's is dropped (FUN_0054d750); the rest go now.
    auto send_items(const std::vector<d2d::net::Bytes>& packets) -> void;
    auto holding() const -> bool { return !when_still.empty(); }   // an item's use waits: don't walk the host's player on
    // The host's unit of `type` (2 object, 5 warp) nearest act subtile
    // (x, y), of `cls` when >= 0; nullptr when none within `within`.
    auto nearest(int type, int cls, float subtile_x, float subtile_y, float within) const -> const Unit*;
    // The host moved us (0x15: a warp, a waypoint, a correction): once.
    auto take_reassign() -> bool { return std::exchange(reassigned, false); }
    // The host killed us (0x0d, our unit's command 8: dying): once.
    auto take_death() -> bool { return std::exchange(died, false); }
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
    std::uint32_t waypoint_menu = 0;  // the waypoint whose menu the host opened for us (0x63); 0x49 only then
    std::uint32_t walked_ms = 0;      // the host's last word of our walk (0x96), steady ms: still walking
    std::unordered_map<std::uint64_t, Unit> units;
    std::vector<std::array<std::uint8_t, 16>> monster_parts;   // MonStats2 HDv..S8v entry counts by MonStats row (0xac's component widths)
    std::vector<int> monster_velocity;   // MonStats Velocity by row: a monster's walk (empty: a player's walk)
    std::unordered_map<std::uint32_t, GroundItem> ground;   // by the host's item id
    // The States.txt rows the host has on our player (0xa8 set, 0xa9 end):
    // a monster's curse, an aura, a shrine's; their overlays are drawn.
    // Every other unit's are its Unit::states (0xaa too, as it comes into view).
    std::vector<int> self_states;
    // ItemStatCost Send Bits / Send Param Bits by stat id: 0xaa's stat lists
    // are stepped over by them (their values unread).
    std::vector<std::pair<int, int>> stat_send_bits;
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
    struct ObjectMode { std::uint32_t id = 0; int mode = 0; };
    std::vector<ObjectMode> object_modes;      // objects' new modes (0x0e): the client sets its doors
    // A trade with another player (game.exe's DAT_007c0e7c, FUN_004b8cf0):
    // 0 none, 1 we asked, 2 they asked us, 3 open, 5 they accepted, 7 we
    // accepted. Their offer arrives as 0x9c action 4 into page 2 (kept in
    // store_items, the store's stock while one's open); ours is what we
    // put in. Done (0x77 0xd), what we got comes as 0x9c action 4 into our
    // bags (live 2026-10-05: an isc for an ibk).
    struct Trade {
        int state = 0;
        std::uint32_t with = 0;                // the other player (0x78)
        std::string with_name;
        std::uint32_t our_gold = 0, their_gold = 0;   // 0x79
        std::vector<d2d::d2s::Item> ours;      // by the host's ids
        std::uint32_t settle_until = 0;        // steady ms: items coming to our bags then are the trade's (back or new)
    };
    Trade trade;
    auto trade_request(std::uint32_t player) -> void { trade.with = player; interact(0, player); }
    auto trade_answer(bool accept) -> void;
    auto trade_accept() -> void;
    auto trade_cancel() -> void;
    auto say(std::string_view message) -> void;   // chat to all (0x15)
    auto phrase(std::uint16_t which) -> void;     // a Say (0x3f, 0x19..0x20)
    auto merc_item(std::uint16_t slot) -> void;   // the merc's items (0x61): 0 give the held one, 1..10 take that one
    // The Party screen's (docs/research/re/party.md): its big button
    // (FUN_00479eb0: 0x5e 6 invite / 7 cancel / 8 accept / 9 leave, the
    // state as game.exe sets it before the host answers) and a relation
    // flag toggled (0x5d: 1 loot, 2 hear, 4 squelch, 8 hostile).
    auto party_button(std::uint32_t player) -> void;
    auto relation(std::uint32_t player, int flag) -> void;
    auto leave_party() -> void;                   // LEAVE (FUN_00479f60: 0x5e 9)
    auto trade_gold(std::uint32_t gold) -> void;   // our offer (0x4f 8); an accept up is taken back first (7), as FUN_004b9110 does
    auto trade_settling() const -> bool;   // a trade just ended: the host sends our items again (a cancel: all of them, new ids)
    std::uint32_t portal_here = 0;             // our town portal's end in the host's area for us (0x82 +0x15; +0x19 the other)
    std::vector<std::uint32_t> no_auto_invite;   // players the Party screen cancelled an invite to or left: autoparty leaves them be
    bool auto_party = true;                    // invite the other players, accept their invites (deviations.md)
    std::vector<std::uint32_t> corpses;        // our corpses' player units (0x8e), oldest first
    std::optional<std::array<std::uint8_t, 96>> quest_words;   // our quest words in the host's game (0x28 type 6): the client takes them
    struct ChatLine { int type = 1, colour = 0; std::string name, message; };   // type 1 to all, 2 a whisper; colour +8
    std::vector<ChatLine> chat;                // players' chat (0x26 types 1, 2), for the client to take
    // An event message (0x5a): +1 event, +2 colour, +3 u32 argument, +7
    // u8, +8 name[16], +0x18 second name[16] (or a u16), for the client.
    struct Event { int event = 0, colour = 0, sub = 0; std::uint32_t argument = 0; std::string name, second; std::uint16_t second_id = 0; };
    std::vector<Event> events;
    struct Overhead { int type = 0; std::uint32_t id = 0; std::string text; };   // 0x26 type 5: over a unit
    std::vector<Overhead> overheads;
    struct Phrase { int cls = 0, phrase = 0; };   // another player's Say (0x2c)
    std::vector<Phrase> phrases;
    int movie = -1;                            // the cinematic the host asked for (0x61), -1 none
    std::vector<int> quest_news;               // quests (words) whose log state the host sent (0x5d, no flags): the Quest Log button
    // The quest log as the host has it: each quest's log state by its word
    // (0x52 whole, 0x5d one), the game's quest words (0x29), the Den's
    // count (0x5d flags 0x20). `quest_log_known` once 0x52 came.
    d2d::rules::QuestLogStates quest_log{};
    d2d::rules::QuestGameFlags game_quests{};
    int den_left = 0;
    bool quest_log_known = false;
    // An NPC's quest lines as the host has them for us (0x27 mode 1), for
    // the client to take: what its menu greets with and lists under Talk.
    struct NpcQuest { std::uint32_t id = 0; std::vector<d2d::rules::QuestMsg> lines; };
    std::vector<NpcQuest> npc_quests;
    std::vector<std::uint32_t> npc_alerts;
    struct Sky { int phase = 0, time = 0; bool eclipse = false; };
    std::optional<Sky> sky;
    bool cube_opened = false;                  // the host opened our Cube (0x77 0x15), for the client to take                    // the act's day the host set (0x53: the Tainted Sun's dark), for the client to take     // NPCs the host balloons for us (0x8a: the "!"), for the client to take

private:
    bool socket_gone = false;
    std::uint32_t heard_ms = 0;                // when the host last sent anything
    static constexpr std::uint32_t kSilentMs = 20000;
    const d2d::d2s::ItemTables* item_tables = nullptr;
    bool reassigned = false;
    std::vector<d2d::net::Bytes> when_still;   // held till the host's player stops (or 3 s)
    std::uint32_t still_since_ms = 0;          // when the first of them was held
    float walk_to_x = 0, walk_to_y = 0;        // our last walk sent (0x01 / 0x03), and when
    std::uint32_t walk_sent_ms = 0;
    // A walk past the host's reach goes in legs (kWalkReach): where it ends,
    // and whether it runs; the next leg leaves when the host nears this one's end.
    float walk_goal_x = -1, walk_goal_y = -1;
    bool walk_goal_run = false;
    bool died = false;
    d2d::net::Bytes last_sent;
    std::uint32_t last_sent_ms = 0;
    std::array<int, 2> hand_skill{ -1, -1 };   // what the host has on the right / left hand
    auto select(int skill, bool left) -> void;
    NetGame(d2d::net::JoinSession joined, d2d::net::TcpConnection socket, const std::filesystem::path& log_path);
    auto handle(const d2d::net::Bytes& packet) -> void;
    auto send(const std::vector<d2d::net::Bytes>& packets) -> void;
};
