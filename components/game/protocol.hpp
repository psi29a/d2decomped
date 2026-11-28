// SPDX-License-Identifier: GPL-3.0-or-later
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

// d2d's co-op Resurrect (docs/design/local-coop.md): a skill only local
// co-op has, used on a fallen partner's body (UseSkill's unit: their player
// id). Past Skills.txt's ids; never saved on a button.
inline constexpr int kResurrectSkill = 0x7ffe;

namespace cmd {
// 0x01 / 0x03: walk or run to (x, y) cells. `fresh`: a new click, not a
// held button re-aiming (it drops the attack or pick-up under way).
struct Move { float x = 0, y = 0; bool fresh = false; };
// 0x05..0x0a (`left`) / 0x0c..0x11: a skill at (x, y), or on `unit` (a
// monster id; -1 none). A plain attack is skill 0. The left one sent
// again while the button's held keeps an attack going. `item`: the
// charged entry's item (its unit id; -1 a skill of the character's own),
// as the skill select carries the entry's owner (SelectSkillLeft's guid).
struct UseSkill { int skill = 0; float x = 0, y = 0; int unit = -1; bool left = false; int item = -1; bool stand = false; int unit_type = 1; bool direct = false; };   // direct: touch/controller location cast; stand: shift (stand still) held, the click flag 0x20; unit_type: 0 player, 1 monster
// 0x13: interact with an object or NPC (walk up, then operate / talk), by
// its Level::npcs index: objects and NPCs come with their rooms and never
// go, and every machine makes the same list from the map seed.
struct Interact { int npc = -1; };
// 0x16: pick up a ground item (walk up, then take it), by its unit id.
struct Pickup { int item = -1; };
// 0x41: back in town after dying (let through only when dead).
struct Resurrect {};
// 0x4f button 0x18: the Cube's Transmute (FUN_00566ae0 -> FUN_005665f0).
struct Transmute {};
// 0x4f button 0x13 / 0x14 (FUN_00564d50): `amount` gold from the stash to
// the inventory (none over clvl x 10000 carried) / into the stash (up to
// its 2,500,000).
struct StashGold { int amount = 0; bool deposit = false; };
// 0x50 (FUN_0054c800 -> FUN_00535510): `amount` carried gold onto the ground at the feet.
struct DropGold { int amount = 0; };
// 0x3a: spend `count` stat points on a stat (0 str, 1 energy, 2 dex, 3 vit).
struct StatPoint { int stat = 0, count = 1; };
// 0x3b: spend a skill point on the class's skill `skill` (its index 0..29,
// the save's order; game.exe's packet names the Skills.txt id).
struct SkillPoint { int skill = 0; };
// 0x3c: put a skill on the left or right button (a right-button aura runs).
struct SelectSkill { int skill = 0; bool left = false; };
// 0x26: use (drink) the belt's column `slot`.
struct UseBelt { int slot = 0; bool merc = false; };   // merc: shift held, the merc drinks it (FUN_00562390)
// The cursor (the item in hand is the server's): 0x19 / 0x1c / 0x24, an
// item (by unit id) from the grid, the body or the belt into the hand;
// 0x18 the hand's item into a grid (panel 1 inventory, 4 cube, 5 stash)
// at (col, row), swapping with what's there; 0x1a / 0x1d onto a body slot
// (1..10); 0x23 into a belt box.
struct ToCursor { int item = -1; };
struct Drop { int item = -1; };                    // the item in hand onto the ground at the player's feet (C→S 0x17)
struct UseItem { int item = -1; };                 // right-click an item: a potion's drunk (C→S 0x20 / 0x26)
// 0x3e (FUN_0049ff90 -> FUN_0054bf60): the deciphered Scroll of Inifuss
// read; the server answers with the stones' order (ev::StoneOrder).
struct ReadScroll { int item = -1; };
struct ToGrid { int panel = 1, col = 0, row = 0; };
struct ToBody { int slot = 0; };
struct ToBelt { int box = 0; };
// 0x63 (FUN_0054d520): a shift + left click on inventory item `item`, the
// hand empty: into the belt box a pickup would choose (rules::auto_belt).
struct AutoBelt { int item = -1; };
// 0x28 (FUN_0054b650 -> FUN_00562660): the held socket filler (a gem, rune
// or jewel) into item `item`'s next free socket, the item stored or worn.
struct Socket { int item = -1; };
// 0x29 (FUN_0054b710 -> FUN_0055ef20): the held scroll into tome `book`
// (stored; its quantity under the max).
struct ScrollToBook { int book = -1; };
// NPC deals: 0x38 an NPC's trade / gamble window or hire list (the server
// rolls the stock / offers); 0x32 buy the stock item of unit id `item` (a
// gamble too: +5, FUN_0054bac0 -> FUN_00577830, found by FUN_00552f60 on any
// tab); 0x33 sell an item (by unit id; the one in hand too); 0x35 repair one
// (-1: everything); 0x34 Cain identifies everything; 0x36 hire the entry of
// name `name` (u16 +5, FUN_0054bbd0 -> FUN_005770e0: HireEntry::name, from
// NameFirst; npc-menu.md); closing the window.
struct OpenTrade { int npc = -1; bool gamble = false; };
struct OpenHire { int npc = -1; };
struct Buy { int item = -1; bool fill = false; };   // fill: shift held on a multibuy item (C->S 0x32's 0x80000000)
struct Sell { int item = -1; };
struct Repair { int item = -1; };
struct Identify {};
struct IdentifyWith { int scroll = -1; int item = -1; };   // a Scroll / Tome of Identify on an item (C→S 0x27, FUN_0054b280)
struct Hire { int name = -1; };
// 0x62 (FUN_00579c00): resurrect the dead merc at hire NPC `npc`.
struct ResurrectMerc { int npc = -1; };
struct CloseTrade {};
// Akara's Reset Stat/Skill Points, confirmed ("ok" sends game.exe's 0x38
// to her; d2d tells it apart with kind 3).
struct Respec { int npc = -1; };
// 0x38 (game.exe's kind 0, arg the level): Warriv's caravan, "Go East" to
// Lut Gholein (warriv1) or "Go West" back to the camp (warriv2).
struct Caravan { int npc = -1; };
// 0x38 (game.exe's kind 0 at Charsi, Larzuk or Anya, FUN_004b2370): their
// service on the item in hand, `item` its id (the server checks it's the
// cursor item, FUN_00578610).
struct Imbue { int npc = -1; int item = -1; };
// 0x44 (FUN_00478700 from the item panel opened by S->C 0x58 kind 0): the
// item `item` into object `npc` (place, action 3: FUN_004bfaf0) or the
// panel cancelled (action 2: FUN_004c02f0 / FUN_004c0150, no item).
struct ObjectItem { int npc = -1; int item = -1; bool place = false; };
// 0x53 / 0x54: run or walk.
struct Run { bool running = false; };
// 0x2f: the NPC the player's talking with (its menu, speech or store open;
// it stands meanwhile), -1 none.
struct Chat { int npc = -1; };
// 0x31: the player heard quest message `string` from NPC `npc` (Akara's
// "Den of Evil" starts the quest, her "successful" hands out the reward).
struct QuestMessage { int npc = -1, string = 0; };
// 0x60: the weapon switch (an expansion character's I / II).
struct SwapWeapons {};
// 0x61: the merc's items (FUN_0054d430): slot 0 with an item in hand puts
// it on the merc (a healing, antidote or thawing potion it drinks); slot
// 1..10 with none takes that item into the hand.
struct MercItem { int slot = 0; };
// 0x49: travel from waypoint object `npc` to the waypoint of `level`.
struct Waypoint { int npc = -1, level = 0; };
// d2d only (opcode 0x64). Not a game.exe packet and not sent on a joined
// game: an older host's decode drops an unknown id, and this client never
// puts the bytes on a D2GS socket. One bag item into head, neck, torso,
// feet or gloves, the old piece into the bag when a cell is free.
struct EquipFromBag { int item = -1; };
// d2d only: the host checks the active player, policy, instance and expiry.
struct DismissEffect { int kind = 0; std::uint64_t instance = 0; };
// 0x15: a typed chat line, to everyone in the game (a LAN game's; game.exe's
// C→S 0x15 carries a whisper's target too).
struct Say { std::string text; };
// The Party screen's clicks (C->S 0x5d {action, on, player}: 1 loot, 2 hear,
// 3 squelch, 4 hostile, `on` the flag's new bits; 0x5e {action, player}: 6
// invite, 7 cancel, 8 accept, 9 leave). `player` is the row's unit id.
struct PartyScreen { int action = 0; int player = -1; std::uint32_t bits = 0; };
}  // namespace cmd

using Command = std::variant<cmd::Move, cmd::UseSkill, cmd::Interact, cmd::Pickup, cmd::Resurrect,
                             cmd::StatPoint, cmd::SkillPoint, cmd::SelectSkill, cmd::UseBelt, cmd::UseItem, cmd::ReadScroll,
                             cmd::ToCursor, cmd::Drop, cmd::ToGrid, cmd::ToBody, cmd::ToBelt, cmd::Socket, cmd::ScrollToBook,
                             cmd::OpenTrade, cmd::OpenHire, cmd::Buy, cmd::Sell, cmd::Repair, cmd::Identify, cmd::IdentifyWith, cmd::Hire, cmd::ResurrectMerc, cmd::CloseTrade, cmd::Respec,
                             cmd::Run, cmd::Chat, cmd::QuestMessage, cmd::Waypoint, cmd::Caravan, cmd::Imbue, cmd::ObjectItem, cmd::Transmute, cmd::StashGold, cmd::DropGold, cmd::SwapWeapons, cmd::MercItem, cmd::AutoBelt, cmd::EquipFromBag, cmd::DismissEffect, cmd::Say, cmd::PartyScreen>;

// The wire form of a command (what a transport carries): its id byte —
// game.exe's packet id where there's one to match — then its fields,
// little-endian. Positions go as float32 cells.
// For little-endian hosts; game.exe's byte-exact D2GS layouts are
// components/net/d2gs's, for a joined game (battlenet.md: a client codec).
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

// The commands' first byte: game.exe's client -> server packet ids
// (docs/research/re/network.md).
namespace opcode {
inline constexpr std::uint8_t kMove = 0x01;
inline constexpr std::uint8_t kLeftSkill = 0x05;
inline constexpr std::uint8_t kRightSkill = 0x0c;
inline constexpr std::uint8_t kInteract = 0x13;
inline constexpr std::uint8_t kSay = 0x15;
inline constexpr std::uint8_t kPickup = 0x16;
inline constexpr std::uint8_t kDrop = 0x17;
inline constexpr std::uint8_t kToGrid = 0x18;
inline constexpr std::uint8_t kToCursor = 0x19;
inline constexpr std::uint8_t kToBody = 0x1a;
inline constexpr std::uint8_t kUseItem = 0x20;
inline constexpr std::uint8_t kToBelt = 0x23;
inline constexpr std::uint8_t kUseBelt = 0x26;
inline constexpr std::uint8_t kSocket = 0x28;
inline constexpr std::uint8_t kScrollToBook = 0x29;
inline constexpr std::uint8_t kChat = 0x2f;
inline constexpr std::uint8_t kCloseTrade = 0x30;
inline constexpr std::uint8_t kQuestMessage = 0x31;
inline constexpr std::uint8_t kBuy = 0x32;
inline constexpr std::uint8_t kSell = 0x33;
inline constexpr std::uint8_t kIdentify = 0x34;
inline constexpr std::uint8_t kIdentifyWith = 0x27;
inline constexpr std::uint8_t kRepair = 0x35;
inline constexpr std::uint8_t kHire = 0x36;
inline constexpr std::uint8_t kNpcDeal = 0x38;
inline constexpr std::uint8_t kStatPoint = 0x3a;
inline constexpr std::uint8_t kSkillPoint = 0x3b;
inline constexpr std::uint8_t kSelectSkill = 0x3c;
inline constexpr std::uint8_t kReadScroll = 0x3e;
inline constexpr std::uint8_t kResurrect = 0x41;
inline constexpr std::uint8_t kObjectItem = 0x44;
inline constexpr std::uint8_t kButton = 0x4f;      // game.exe's "click button": +1 u16 0x18 Transmute
inline constexpr std::uint8_t kWaypoint = 0x49;
inline constexpr std::uint8_t kDropGold = 0x50;
inline constexpr std::uint8_t kRunOn = 0x53;
inline constexpr std::uint8_t kRunOff = 0x54;
inline constexpr std::uint8_t kRelation = 0x5d;     // the Party screen's boxes
inline constexpr std::uint8_t kPartyButton = 0x5e;  // its party button and LEAVE
inline constexpr std::uint8_t kSwapWeapons = 0x60;
inline constexpr std::uint8_t kMercItem = 0x61;
inline constexpr std::uint8_t kAutoBelt = 0x63;
inline constexpr std::uint8_t kEquipFromBag = 0x64;   // d2d only; see cmd::EquipFromBag
inline constexpr std::uint8_t kDismissEffect = 0x65;   // d2d only
}  // namespace opcode

// 0x38's first byte: which deal (d2d's numbering; game.exe tells Respec,
// Caravan and Imbue apart by the NPC and an argument, see the cmd structs).
namespace npc_deal {
inline constexpr int kTrade = 0, kGamble = 1, kHire = 2, kRespec = 3, kCaravan = 4, kImbue = 5, kResurrectMerc = 6;
}  // namespace npc_deal

std::vector<std::uint8_t> encode(const Command& command);

// A command back from its wire form; nullopt for anything malformed (the
// server drops it: what comes over a transport is untrusted).
std::optional<Command> decode(std::span<const std::uint8_t> bytes);

// The in-process transport (docs/design/multiplayer.md rule 3): the
// client's commands go over as their wire form, in order, and come out
// decoded on the server's side — single player, and the first thing a
// TCP transport replaces.
struct LocalTransport {
    std::deque<std::vector<std::uint8_t>> to_server;
    std::vector<std::uint8_t> to_client;   // the World's latest View, as its wire form (replication.hpp)
    void send(const Command& command) { to_server.push_back(encode(command)); }
    // What the server has been sent since it last looked.
    std::vector<Command> receive();
};

}  // namespace d2d::game
