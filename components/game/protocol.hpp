// What a client asks the game server for: its intents, the shapes of
// game.exe's client -> server packets (docs/research/re/network.md). The
// server validates and applies them in its tick; nothing else from the
// client touches the world. docs/design/multiplayer.md.
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace d2d::game {

namespace cmd {
// 0x01 / 0x03: walk or run to (x, y) cells. `fresh`: a new click, not a
// held button re-aiming (it drops the attack or pick-up under way).
struct Move { float x = 0, y = 0; bool fresh = false; };
// 0x05..0x0a (`left`) / 0x0c..0x11: a skill at (x, y), or on `unit` (a
// monster id; -1 none). A plain attack is skill 0. The left one sent
// again while the button's held keeps an attack going.
struct UseSkill { int skill = 0; float x = 0, y = 0; int unit = -1; bool left = false; };
// 0x13: interact with an object or NPC (walk up, then operate / talk), by
// its Level::npcs index: objects and NPCs never come or go, and every
// machine makes the same list from the map seed.
struct Interact { int npc = -1; };
// 0x16: pick up a ground item (walk up, then take it), by its unit id.
struct Pickup { int item = -1; };
// 0x41: back in town after dying (let through only when dead).
struct Resurrect {};
// 0x3a: spend `count` stat points on a stat (0 str, 1 energy, 2 dex, 3 vit).
struct StatPoint { int stat = 0, count = 1; };
// 0x3b: spend a skill point on the class's skill `skill` (its index 0..29,
// the save's order; game.exe's packet names the Skills.txt id).
struct SkillPoint { int skill = 0; };
// 0x3c: put a skill on the left or right button (a right-button aura runs).
struct SelectSkill { int skill = 0; bool left = false; };
// 0x26: use (drink) the belt's column `slot`.
struct UseBelt { int slot = 0; };
// The cursor (the item in hand is the server's): 0x19 / 0x1c / 0x24, an
// item (by unit id) from the grid, the body or the belt into the hand;
// 0x18 the hand's item into a grid (panel 1 inventory, 4 cube, 5 stash)
// at (col, row), swapping with what's there; 0x1a / 0x1d onto a body slot
// (1..10); 0x23 into a belt box.
struct ToCursor { int item = -1; };
struct Drop { int item = -1; };                    // the item in hand onto the ground at the player's feet (C→S 0x17)
struct UseItem { int item = -1; };                 // right-click an item: a potion's drunk (C→S 0x20 / 0x26)
struct ToGrid { int panel = 1, col = 0, row = 0; };
struct ToBody { int slot = 0; };
struct ToBelt { int box = 0; };
// NPC deals: 0x38 an NPC's trade / gamble window or hire list (the server
// rolls the stock / offers); 0x32 buy stock item `stock` (a gamble too);
// 0x33 sell an item (by unit id; the one in hand too); 0x35 repair one
// (-1: everything); 0x34 Cain identifies everything; 0x36 hire offer
// `offer`; closing the window.
// ponytail: stock and offers by index, not unit id.
struct OpenTrade { int npc = -1; bool gamble = false; };
struct OpenHire { int npc = -1; };
struct Buy { int stock = -1; };
struct Sell { int item = -1; };
struct Repair { int item = -1; };
struct Identify {};
struct Hire { int offer = -1; };
struct CloseTrade {};
// Akara's Reset Stat/Skill Points, confirmed ("ok" sends game.exe's 0x38
// to her; d2d tells it apart with kind 3).
struct Respec { int npc = -1; };
// 0x38 (game.exe's kind 0, arg the level): Warriv's "Go East" to Lut Gholein.
struct GoEast { int npc = -1; };
// 0x38 (game.exe's kind 0 at Charsi): imbue the item in hand.
struct Imbue { int npc = -1; };
// 0x53 / 0x54: run or walk.
struct Run { bool running = false; };
// 0x2f: the NPC the player's talking with (its menu, speech or store open;
// it stands meanwhile), -1 none.
struct Chat { int npc = -1; };
// 0x31: the player heard quest message `string` from NPC `npc` (Akara's
// "Den of Evil" starts the quest, her "successful" hands out the reward).
struct QuestMessage { int npc = -1, string = 0; };
// 0x49: travel from waypoint object `npc` to the waypoint of `level`.
struct Waypoint { int npc = -1, level = 0; };
}  // namespace cmd

using Command = std::variant<cmd::Move, cmd::UseSkill, cmd::Interact, cmd::Pickup, cmd::Resurrect,
                             cmd::StatPoint, cmd::SkillPoint, cmd::SelectSkill, cmd::UseBelt, cmd::UseItem,
                             cmd::ToCursor, cmd::Drop, cmd::ToGrid, cmd::ToBody, cmd::ToBelt,
                             cmd::OpenTrade, cmd::OpenHire, cmd::Buy, cmd::Sell, cmd::Repair, cmd::Identify, cmd::Hire, cmd::CloseTrade, cmd::Respec,
                             cmd::Run, cmd::Chat, cmd::QuestMessage, cmd::Waypoint, cmd::GoEast, cmd::Imbue>;

// The wire form of a command (what a transport carries): its id byte —
// game.exe's packet id where there's one to match — then its fields,
// little-endian. Positions go as float32 cells.
// ponytail: little-endian hosts; the byte-exact D2GS layouts are a later
// codec's (docs/research/re/network.md).
namespace wire {
struct Out {
    std::vector<std::uint8_t> bytes;
    template <class T> Out& put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto offset = bytes.size();
        bytes.resize(offset + sizeof value);
        std::memcpy(bytes.data() + offset, &value, sizeof value);
        return *this;
    }
    Out& u8(int value) { return put(std::uint8_t(value)); }
    Out& u16(int value) { return put(std::uint16_t(value)); }
    Out& i32(int value) { return put(std::int32_t(value)); }
    Out& u32(std::uint32_t value) { return put(value); }
    Out& f32(float value) { return put(value); }
    Out& str(std::string_view text) {
        u16(int(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
        return *this;
    }
};
struct In {
    std::span<const std::uint8_t> bytes;
    std::size_t offset = 1;
    bool ok = true;
    std::string str() {
        const std::size_t length = get<std::uint16_t>();
        if (!ok || offset + length > bytes.size()) { ok = false; return {}; }
        std::string text(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
        return text;
    }
    template <class T> T get() {
        T value{};
        if (offset + sizeof value > bytes.size()) { ok = false; return value; }
        std::memcpy(&value, bytes.data() + offset, sizeof value);
        offset += sizeof value;
        return value;
    }
};
}  // namespace wire

inline std::vector<std::uint8_t> encode(const Command& command) {
    wire::Out out;
    std::visit([&](const auto& message) {
        using T = std::decay_t<decltype(message)>;
        if constexpr (std::is_same_v<T, cmd::Move>) out.u8(0x01).f32(message.x).f32(message.y).u8(message.fresh);
        else if constexpr (std::is_same_v<T, cmd::UseSkill>) out.u8(message.left ? 0x05 : 0x0c).i32(message.skill).f32(message.x).f32(message.y).i32(message.unit);
        else if constexpr (std::is_same_v<T, cmd::Interact>) out.u8(0x13).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Pickup>) out.u8(0x16).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Resurrect>) out.u8(0x41);
        else if constexpr (std::is_same_v<T, cmd::StatPoint>) out.u8(0x3a).i32(message.stat).i32(message.count);
        else if constexpr (std::is_same_v<T, cmd::SkillPoint>) out.u8(0x3b).i32(message.skill);
        else if constexpr (std::is_same_v<T, cmd::SelectSkill>) out.u8(0x3c).i32(message.skill).u8(message.left);
        else if constexpr (std::is_same_v<T, cmd::UseBelt>) out.u8(0x26).i32(message.slot);
        else if constexpr (std::is_same_v<T, cmd::UseItem>) out.u8(0x20).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::ToCursor>) out.u8(0x19).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Drop>) out.u8(0x17).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::ToGrid>) out.u8(0x18).i32(message.panel).i32(message.col).i32(message.row);
        else if constexpr (std::is_same_v<T, cmd::ToBody>) out.u8(0x1a).i32(message.slot);
        else if constexpr (std::is_same_v<T, cmd::ToBelt>) out.u8(0x23).i32(message.box);
        else if constexpr (std::is_same_v<T, cmd::OpenTrade>) out.u8(0x38).u8(message.gamble ? 1 : 0).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::OpenHire>) out.u8(0x38).u8(2).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Respec>) out.u8(0x38).u8(3).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Buy>) out.u8(0x32).i32(message.stock);
        else if constexpr (std::is_same_v<T, cmd::Sell>) out.u8(0x33).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Repair>) out.u8(0x35).i32(message.item);
        else if constexpr (std::is_same_v<T, cmd::Identify>) out.u8(0x34);
        else if constexpr (std::is_same_v<T, cmd::Hire>) out.u8(0x36).i32(message.offer);
        else if constexpr (std::is_same_v<T, cmd::CloseTrade>) out.u8(0x30);
        else if constexpr (std::is_same_v<T, cmd::Run>) out.u8(message.running ? 0x53 : 0x54);
        else if constexpr (std::is_same_v<T, cmd::Chat>) out.u8(0x2f).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::QuestMessage>) out.u8(0x31).i32(message.npc).i32(message.string);
        else if constexpr (std::is_same_v<T, cmd::GoEast>) out.u8(0x38).u8(4).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Imbue>) out.u8(0x38).u8(5).i32(message.npc);
        else if constexpr (std::is_same_v<T, cmd::Waypoint>) out.u8(0x49).i32(message.npc).i32(message.level);
        else static_assert(!sizeof(T), "a command without a wire form");
    }, command);
    return out.bytes;
}

// A command back from its wire form; nullopt for anything malformed (the
// server drops it: what comes over a transport is untrusted).
inline std::optional<Command> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    wire::In input{ bytes };
    auto i32 = [&] { return int(input.get<std::int32_t>()); };
    auto f32 = [&] { return input.get<float>(); };
    auto byte = [&] { return int(input.get<std::uint8_t>()); };
    std::optional<Command> command;
    switch (bytes[0]) {
        case 0x01: { const float x = f32(), y = f32(); command = cmd::Move{ x, y, byte() != 0 }; break; }
        case 0x05: case 0x0c: { const int skill = i32(); const float x = f32(), y = f32(); command = cmd::UseSkill{ skill, x, y, i32(), bytes[0] == 0x05 }; break; }
        case 0x13: command = cmd::Interact{ i32() }; break;
        case 0x16: command = cmd::Pickup{ i32() }; break;
        case 0x41: command = cmd::Resurrect{}; break;
        case 0x3a: { const int stat = i32(); command = cmd::StatPoint{ stat, i32() }; break; }
        case 0x3b: command = cmd::SkillPoint{ i32() }; break;
        case 0x3c: { const int skill = i32(); command = cmd::SelectSkill{ skill, byte() != 0 }; break; }
        case 0x26: command = cmd::UseBelt{ i32() }; break;
        case 0x20: command = cmd::UseItem{ i32() }; break;
        case 0x19: command = cmd::ToCursor{ i32() }; break;
        case 0x17: command = cmd::Drop{ i32() }; break;
        case 0x18: { const int panel = i32(), col = i32(); command = cmd::ToGrid{ panel, col, i32() }; break; }
        case 0x1a: command = cmd::ToBody{ i32() }; break;
        case 0x23: command = cmd::ToBelt{ i32() }; break;
        case 0x38: {
            const int kind = byte(), npc = i32();
            command = kind == 5 ? Command{ cmd::Imbue{ npc } } : kind == 4 ? Command{ cmd::GoEast{ npc } } : kind == 3 ? Command{ cmd::Respec{ npc } } : kind == 2 ? Command{ cmd::OpenHire{ npc } } : Command{ cmd::OpenTrade{ npc, kind == 1 } };
            break;
        }
        case 0x32: command = cmd::Buy{ i32() }; break;
        case 0x33: command = cmd::Sell{ i32() }; break;
        case 0x35: command = cmd::Repair{ i32() }; break;
        case 0x34: command = cmd::Identify{}; break;
        case 0x36: command = cmd::Hire{ i32() }; break;
        case 0x30: command = cmd::CloseTrade{}; break;
        case 0x53: case 0x54: command = cmd::Run{ bytes[0] == 0x53 }; break;
        case 0x2f: command = cmd::Chat{ i32() }; break;
        case 0x31: { const int quest = i32(); command = cmd::QuestMessage{ quest, i32() }; break; }
        case 0x49: { const int npc = i32(); command = cmd::Waypoint{ npc, i32() }; break; }
        default: return std::nullopt;
    }
    if (!input.ok || input.offset != bytes.size()) return std::nullopt;
    return command;
}

// The in-process transport (docs/design/multiplayer.md rule 3): the
// client's commands go over as their wire form, in order, and come out
// decoded on the server's side — single player, and the first thing a
// TCP transport replaces.
struct LocalTransport {
    std::deque<std::vector<std::uint8_t>> to_server;
    std::vector<std::uint8_t> to_client;   // the World's latest View, as its wire form (replication.hpp)
    void send(const Command& command) { to_server.push_back(encode(command)); }
    // What the server has been sent since it last looked.
    std::vector<Command> receive() {
        std::vector<Command> out;
        for (const auto& message : to_server) if (auto command = decode(message)) out.push_back(*command);
        to_server.clear();
        return out;
    }
};

}  // namespace d2d::game
