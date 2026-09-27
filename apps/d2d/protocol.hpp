// What a client asks the game server for: its intents, the shapes of
// game.exe's client -> server packets (docs/research/re/network.md). The
// server validates and applies them in its tick; nothing else from the
// client touches the world. docs/design/multiplayer.md.
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

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
// 0x53 / 0x54: run or walk.
struct Run { bool on = false; };
// 0x2f: the NPC the player's talking with (its menu, speech or store open;
// it stands meanwhile), -1 none.
struct Chat { int npc = -1; };
// 0x31: the player heard quest message `string` from NPC `npc` (Akara's
// "Den of Evil" starts the quest, her "successful" hands out the reward).
struct QuestMessage { int npc = -1, string = 0; };
}  // namespace cmd

using Command = std::variant<cmd::Move, cmd::UseSkill, cmd::Interact, cmd::Pickup, cmd::Resurrect,
                             cmd::StatPoint, cmd::SkillPoint, cmd::SelectSkill, cmd::UseBelt,
                             cmd::ToCursor, cmd::ToGrid, cmd::ToBody, cmd::ToBelt,
                             cmd::OpenTrade, cmd::OpenHire, cmd::Buy, cmd::Sell, cmd::Repair, cmd::Identify, cmd::Hire, cmd::CloseTrade,
                             cmd::Run, cmd::Chat, cmd::QuestMessage>;

// The wire form of a command (what a transport carries): its id byte —
// game.exe's packet id where there's one to match — then its fields,
// little-endian. Positions go as float32 cells.
// ponytail: little-endian hosts; the byte-exact D2GS layouts are a later
// codec's (docs/research/re/network.md).
namespace wire {
struct Out {
    std::vector<std::uint8_t> b;
    template <class T> Out& put(T v) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto at = b.size();
        b.resize(at + sizeof v);
        std::memcpy(b.data() + at, &v, sizeof v);
        return *this;
    }
    Out& u8(int v) { return put(std::uint8_t(v)); }
    Out& u16(int v) { return put(std::uint16_t(v)); }
    Out& i32(int v) { return put(std::int32_t(v)); }
    Out& u32(std::uint32_t v) { return put(v); }
    Out& f32(float v) { return put(v); }
    Out& str(std::string_view v) {
        u16(int(v.size()));
        b.insert(b.end(), v.begin(), v.end());
        return *this;
    }
};
struct In {
    std::span<const std::uint8_t> b;
    std::size_t at = 1;
    bool ok = true;
    std::string str() {
        const std::size_t n = get<std::uint16_t>();
        if (!ok || at + n > b.size()) { ok = false; return {}; }
        std::string v(reinterpret_cast<const char*>(b.data() + at), n);
        at += n;
        return v;
    }
    template <class T> T get() {
        T v{};
        if (at + sizeof v > b.size()) { ok = false; return v; }
        std::memcpy(&v, b.data() + at, sizeof v);
        at += sizeof v;
        return v;
    }
};
}  // namespace wire

inline std::vector<std::uint8_t> encode(const Command& c) {
    wire::Out o;
    std::visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, cmd::Move>) o.u8(0x01).f32(m.x).f32(m.y).u8(m.fresh);
        else if constexpr (std::is_same_v<T, cmd::UseSkill>) o.u8(m.left ? 0x05 : 0x0c).i32(m.skill).f32(m.x).f32(m.y).i32(m.unit);
        else if constexpr (std::is_same_v<T, cmd::Interact>) o.u8(0x13).i32(m.npc);
        else if constexpr (std::is_same_v<T, cmd::Pickup>) o.u8(0x16).i32(m.item);
        else if constexpr (std::is_same_v<T, cmd::Resurrect>) o.u8(0x41);
        else if constexpr (std::is_same_v<T, cmd::StatPoint>) o.u8(0x3a).i32(m.stat).i32(m.count);
        else if constexpr (std::is_same_v<T, cmd::SkillPoint>) o.u8(0x3b).i32(m.skill);
        else if constexpr (std::is_same_v<T, cmd::SelectSkill>) o.u8(0x3c).i32(m.skill).u8(m.left);
        else if constexpr (std::is_same_v<T, cmd::UseBelt>) o.u8(0x26).i32(m.slot);
        else if constexpr (std::is_same_v<T, cmd::ToCursor>) o.u8(0x19).i32(m.item);
        else if constexpr (std::is_same_v<T, cmd::ToGrid>) o.u8(0x18).i32(m.panel).i32(m.col).i32(m.row);
        else if constexpr (std::is_same_v<T, cmd::ToBody>) o.u8(0x1a).i32(m.slot);
        else if constexpr (std::is_same_v<T, cmd::ToBelt>) o.u8(0x23).i32(m.box);
        else if constexpr (std::is_same_v<T, cmd::OpenTrade>) o.u8(0x38).u8(m.gamble ? 1 : 0).i32(m.npc);
        else if constexpr (std::is_same_v<T, cmd::OpenHire>) o.u8(0x38).u8(2).i32(m.npc);
        else if constexpr (std::is_same_v<T, cmd::Buy>) o.u8(0x32).i32(m.stock);
        else if constexpr (std::is_same_v<T, cmd::Sell>) o.u8(0x33).i32(m.item);
        else if constexpr (std::is_same_v<T, cmd::Repair>) o.u8(0x35).i32(m.item);
        else if constexpr (std::is_same_v<T, cmd::Identify>) o.u8(0x34);
        else if constexpr (std::is_same_v<T, cmd::Hire>) o.u8(0x36).i32(m.offer);
        else if constexpr (std::is_same_v<T, cmd::CloseTrade>) o.u8(0x30);
        else if constexpr (std::is_same_v<T, cmd::Run>) o.u8(m.on ? 0x53 : 0x54);
        else if constexpr (std::is_same_v<T, cmd::Chat>) o.u8(0x2f).i32(m.npc);
        else if constexpr (std::is_same_v<T, cmd::QuestMessage>) o.u8(0x31).i32(m.npc).i32(m.string);
        else static_assert(!sizeof(T), "a command without a wire form");
    }, c);
    return o.b;
}

// A command back from its wire form; nullopt for anything malformed (the
// server drops it: what comes over a transport is untrusted).
inline std::optional<Command> decode(std::span<const std::uint8_t> b) {
    if (b.empty()) return std::nullopt;
    wire::In in{ b };
    auto i32 = [&] { return int(in.get<std::int32_t>()); };
    auto f32 = [&] { return in.get<float>(); };
    auto u8 = [&] { return int(in.get<std::uint8_t>()); };
    std::optional<Command> c;
    switch (b[0]) {
        case 0x01: { const float x = f32(), y = f32(); c = cmd::Move{ x, y, u8() != 0 }; break; }
        case 0x05: case 0x0c: { const int k = i32(); const float x = f32(), y = f32(); c = cmd::UseSkill{ k, x, y, i32(), b[0] == 0x05 }; break; }
        case 0x13: c = cmd::Interact{ i32() }; break;
        case 0x16: c = cmd::Pickup{ i32() }; break;
        case 0x41: c = cmd::Resurrect{}; break;
        case 0x3a: { const int st = i32(); c = cmd::StatPoint{ st, i32() }; break; }
        case 0x3b: c = cmd::SkillPoint{ i32() }; break;
        case 0x3c: { const int k = i32(); c = cmd::SelectSkill{ k, u8() != 0 }; break; }
        case 0x26: c = cmd::UseBelt{ i32() }; break;
        case 0x19: c = cmd::ToCursor{ i32() }; break;
        case 0x18: { const int p = i32(), col = i32(); c = cmd::ToGrid{ p, col, i32() }; break; }
        case 0x1a: c = cmd::ToBody{ i32() }; break;
        case 0x23: c = cmd::ToBelt{ i32() }; break;
        case 0x38: { const int kind = u8(), npc = i32(); c = kind == 2 ? Command{ cmd::OpenHire{ npc } } : Command{ cmd::OpenTrade{ npc, kind == 1 } }; break; }
        case 0x32: c = cmd::Buy{ i32() }; break;
        case 0x33: c = cmd::Sell{ i32() }; break;
        case 0x35: c = cmd::Repair{ i32() }; break;
        case 0x34: c = cmd::Identify{}; break;
        case 0x36: c = cmd::Hire{ i32() }; break;
        case 0x30: c = cmd::CloseTrade{}; break;
        case 0x53: case 0x54: c = cmd::Run{ b[0] == 0x53 }; break;
        case 0x2f: c = cmd::Chat{ i32() }; break;
        case 0x31: { const int n = i32(); c = cmd::QuestMessage{ n, i32() }; break; }
        default: return std::nullopt;
    }
    if (!in.ok || in.at != b.size()) return std::nullopt;
    return c;
}

// The in-process transport (docs/design/multiplayer.md rule 3): the
// client's commands go over as their wire form, in order, and come out
// decoded on the server's side — single player, and the first thing a
// TCP transport replaces.
struct LocalTransport {
    std::deque<std::vector<std::uint8_t>> to_server;
    std::vector<std::uint8_t> to_client;   // the World's latest View, as its wire form (replication.hpp)
    void send(const Command& c) { to_server.push_back(encode(c)); }
    // What the server has been sent since it last looked.
    std::vector<Command> receive() {
        std::vector<Command> out;
        for (const auto& m : to_server) if (auto c = decode(m)) out.push_back(*c);
        to_server.clear();
        return out;
    }
};

}  // namespace
