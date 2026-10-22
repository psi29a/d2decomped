// SPDX-License-Identifier: GPL-3.0-or-later
// The C->S packets a join needs (docs/research/re/net-join.md "Join",
// "Ping", "Leaving"). C->S has no frames and no compression: each packet
// goes out as its own bytes.
#pragma once

#include <d2gs/wire.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::net::d2gs::c2s {

inline constexpr std::uint32_t kVersion = 0x0e;      // FUN_0051ca10; the host refuses others with B4 0x10
inline constexpr std::uint16_t kTcpGameToken = 1;    // config +0x1d for a TCP join
inline constexpr std::size_t kJoinSize = 37;
inline constexpr std::size_t kSaveChunk = 0xff;      // the bytes of the save in one 0x6c

// 0x68 (FUN_00477f70, layout at 0x477f76). The dword at +1 and the two at
// +0xc / +0x10 aren't checked by a TCP host, so they go out as 0.
inline auto join_request(int character_class, std::string_view name, std::uint8_t language = 0) -> Bytes {
    Bytes out;
    put_u8(out, 0x68);
    put_u32(out, 0);
    put_u16(out, kTcpGameToken);
    put_u8(out, static_cast<std::uint8_t>(character_class));
    put_u32(out, kVersion);
    put_u32(out, 0);
    put_u32(out, 0);
    put_u8(out, language);
    put_name(out, name, 16);
    return out;
}

// The save, 0x6c a chunk: [6c, n, u32 total, n bytes, 0] (n + 7 bytes),
// all sent at once after the 0x68.
inline auto save_chunks(std::span<const std::uint8_t> save) -> std::vector<Bytes> {
    std::vector<Bytes> chunks;
    for (std::size_t offset = 0; offset < save.size(); offset += kSaveChunk) {
        const auto part = save.subspan(offset, std::min(kSaveChunk, save.size() - offset));
        Bytes out;
        put_u8(out, 0x6c);
        put_u8(out, static_cast<std::uint8_t>(part.size()));
        put_u32(out, static_cast<std::uint32_t>(save.size()));
        put_bytes(out, part);
        put_u8(out, 0);
        chunks.push_back(std::move(out));
    }
    return chunks;
}

// 0x6b: the answer to S->C 0x02 (FUN_00477da0).
inline auto load_ack() -> Bytes { return { 0x6b }; }

// 0x6d (FUN_00477dd0), at most once every 5 s: the tick, a timer value
// (0 here) and the anti-cheat dword, which a TCP host doesn't check.
inline auto ping(std::uint32_t tick) -> Bytes {
    Bytes out;
    put_u8(out, 0x6d);
    put_u32(out, tick);
    put_u32(out, 0);
    put_u32(out, 0);
    return out;
}

// 0x01 / 0x03: walk / run to act subtile (x, y) (FUN_005497e0 / 0x5498d0
// on the host; one the bounds check refuses is answered after 25 frames).
inline auto move_to(std::uint16_t x, std::uint16_t y, bool run) -> Bytes {
    Bytes out;
    put_u8(out, run ? 0x03 : 0x01);
    put_u16(out, x);
    put_u16(out, y);
    return out;
}

// 0x53 / 0x54: run / walk from now on.
inline auto set_running(bool run) -> Bytes { return { static_cast<std::uint8_t>(run ? 0x53 : 0x54) }; }
// 0x60: the weapon switch (FUN_0054ce70).
inline auto swap_weapons() -> Bytes { return { 0x60 }; }

// 0x40: the player's quest words again (the host's FUN_00546040 answers
// 0x28 type 6, then 0x29).
inline auto update_quests() -> Bytes { return { 0x40 }; }

// 0x15: chat (54a5d0): +1 type (1: to all), +2 language, the message,
// NUL, a whisper's target, NUL, then a count byte and that many more
// (FUN_0052bc20 sizes it so; without the count the host takes the next
// packet's first byte for it, seen live 2026-10-17). The host sends it on
// as 0x26 type 1 (2 when a target is named), the sender's name and level
// filled in.
inline auto chat(std::string_view message) -> Bytes {
    Bytes out{ 0x15, 0x01, 0x00 };
    for (const char letter : message.substr(0, 255)) put_u8(out, std::uint8_t(letter));
    put_u8(out, 0);   // the message's end
    put_u8(out, 0);   // no target
    put_u8(out, 0);   // no more bytes
    return out;
}

// 0x41: a dead player back to town (54c0e0 takes it only from the dead).
inline auto resurrect() -> Bytes { return { 0x41 }; }

// 0x3c: the skill on a hand (bit 31: the left), and the item it comes
// from (-1: none). Sent before a skill packet when the hand's skill changed.
inline auto select_skill(int skill, bool left, std::uint32_t item_id = 0xffffffff) -> Bytes {
    Bytes out;
    put_u8(out, 0x3c);
    put_u32(out, static_cast<std::uint32_t>(skill) | (left ? 0x80000000u : 0u));
    put_u32(out, item_id);
    return out;
}

// 0x05 / 0x0c: the left / right skill at act subtile (x, y).
inline auto skill_at(bool left, std::uint16_t x, std::uint16_t y) -> Bytes {
    Bytes out;
    put_u8(out, left ? 0x05 : 0x0c);
    put_u16(out, x);
    put_u16(out, y);
    return out;
}

// 0x06 / 0x0d: the left / right skill on a unit (type, id), walking up to it.
inline auto skill_on(bool left, std::uint32_t type, std::uint32_t id) -> Bytes {
    Bytes out;
    put_u8(out, left ? 0x06 : 0x0d);
    put_u32(out, type);
    put_u32(out, id);
    return out;
}

// 0x4f: a UI button (FUN_00478600): +1 u16 button, +3 u16, +5 u16. The
// trade's (FUN_004b8730 .. FUN_004b9110): 2 decline / cancel, 3 accept
// the request, 4 accept the trade, 7 take the accept back, 8 our gold
// (high, low half).
// A character phrase (Say, FUN_004785b0): 0x19..0x20.
inline auto phrase(std::uint16_t which) -> Bytes {
    Bytes out;
    put_u8(out, 0x3f);
    put_u16(out, which);
    return out;
}

// The merc's items (FUN_0054d430): slot 0 gives it the cursor's item, 1..10 takes that one.
inline auto merc_item(std::uint16_t slot) -> Bytes {
    Bytes out;
    put_u8(out, 0x61);
    put_u16(out, slot);
    return out;
}

inline auto click_button(std::uint16_t button, std::uint16_t first = 0, std::uint16_t second = 0) -> Bytes {
    Bytes out;
    put_u8(out, 0x4f);
    put_u16(out, button);
    put_u16(out, first);
    put_u16(out, second);
    return out;
}

// 0x5e: party (FUN_00478780, from the party button FUN_00479eb0 by the
// relationship 0x8b set): 6 invite, 7 cancel the invite, 8 accept one,
// 9 leave.
inline auto party(std::uint8_t action, std::uint32_t player) -> Bytes {
    Bytes out;
    put_u8(out, 0x5e);
    put_u8(out, action);
    put_u32(out, player);
    return out;
}

// 0x5d: a relation flag toward a player (FUN_00478740): 1 loot, 2 hear,
// 4 squelch, 8 hostile; on or off.
inline auto relation(std::uint8_t flag, bool set, std::uint32_t player) -> Bytes {
    Bytes out;
    put_u8(out, 0x5d);
    put_u8(out, flag);
    put_u8(out, set ? 1 : 0);
    put_u32(out, player);
    return out;
}

// 0x13: interact with a unit (type, id): talk, operate, take a warp.
inline auto interact(std::uint32_t type, std::uint32_t id) -> Bytes {
    Bytes out;
    put_u8(out, 0x13);
    put_u32(out, type);
    put_u32(out, id);
    return out;
}

// 0x49: travel by the waypoint object `id` to Levels.txt `level`.
inline auto waypoint(std::uint32_t id, std::uint16_t level) -> Bytes {
    Bytes out;
    put_u8(out, 0x49);
    put_u32(out, id);
    put_u16(out, level);
    put_u16(out, 0);
    return out;
}

// 0x16: pick up the ground item `id` (type 4); the third dword: 0.
inline auto pick_up(std::uint32_t id) -> Bytes {
    Bytes out;
    put_u8(out, 0x16);
    put_u32(out, 4);
    put_u32(out, id);
    put_u32(out, 0);
    return out;
}

// Our items, by the host's ids (net-packets.md "Client -> server: Items").
// 0x20: use (drink, read) an item in a grid, from where we stand.
inline auto use_item(std::uint32_t id, std::uint16_t x, std::uint16_t y) -> Bytes {
    Bytes out;
    put_u8(out, 0x20); put_u32(out, id); put_u32(out, x); put_u32(out, y);
    return out;
}
// 0x26: use an item in the belt.
inline auto use_belt(std::uint32_t id) -> Bytes {
    Bytes out;
    put_u8(out, 0x26); put_u32(out, id); put_u32(out, 0); put_u32(out, 0);
    return out;
}
// One u32 id after the packet id: 0x17 drop the cursor's, 0x19 grid ->
// cursor, 0x24 belt -> cursor.
inline auto item_id_packet(std::uint8_t packet_id, std::uint32_t id) -> Bytes {
    Bytes out;
    put_u8(out, packet_id); put_u32(out, id);
    return out;
}
// 0x18: the cursor's item into a grid at (x, y); buffer 0 inventory, 3
// cube, 4 stash.
inline auto cursor_to_grid(std::uint32_t id, std::uint32_t x, std::uint32_t y, std::uint32_t buffer) -> Bytes {
    Bytes out;
    put_u8(out, 0x18); put_u32(out, id); put_u32(out, x); put_u32(out, y); put_u32(out, buffer);
    return out;
}
// 0x1f: swap the cursor's item with the grid item `target` at (x, y).
// 0x50: drop `amount` gold (FUN_0054c800: u32 the player's unit id, u32 amount).
inline auto drop_gold(std::uint32_t player, std::uint32_t amount) -> Bytes {
    Bytes out;
    put_u8(out, 0x50); put_u32(out, player); put_u32(out, amount);
    return out;
}
// 0x28: the held socket filler into item `target` (FUN_0054b650).
inline auto insert_socket(std::uint32_t id, std::uint32_t target) -> Bytes {
    Bytes out;
    put_u8(out, 0x28); put_u32(out, id); put_u32(out, target);
    return out;
}
// 0x29: the held scroll into tome `book` (FUN_0054b710).
inline auto scroll_to_book(std::uint32_t id, std::uint32_t book) -> Bytes {
    Bytes out;
    put_u8(out, 0x29); put_u32(out, id); put_u32(out, book);
    return out;
}
inline auto swap_grid(std::uint32_t id, std::uint32_t target, std::uint32_t x, std::uint32_t y) -> Bytes {
    Bytes out;
    put_u8(out, 0x1f); put_u32(out, id); put_u32(out, target); put_u32(out, x); put_u32(out, y);
    return out;
}
// 0x1a / 0x1d: equip the cursor's item in body slot 1..10 / swap it there.
inline auto equip(std::uint32_t id, std::uint32_t slot, bool swap) -> Bytes {
    Bytes out;
    put_u8(out, swap ? 0x1d : 0x1a); put_u32(out, id); put_u32(out, slot);
    return out;
}
// 0x1c: the item in body slot -> cursor (by slot, not id).
inline auto body_to_cursor(std::uint16_t slot) -> Bytes {
    Bytes out;
    put_u8(out, 0x1c); put_u16(out, slot);
    return out;
}
// 0x23: the cursor's item into belt box `box`.
inline auto cursor_to_belt(std::uint32_t id, std::uint32_t box) -> Bytes {
    Bytes out;
    put_u8(out, 0x23); put_u32(out, id); put_u32(out, box);
    return out;
}

// NPCs (net-packets.md "NPC trade"). 0x2f / 0x30: start / end the chat
// with a unit (type, id).
inline auto npc_chat(bool start, std::uint32_t type, std::uint32_t id) -> Bytes {
    Bytes out;
    put_u8(out, start ? 0x2f : 0x30); put_u32(out, type); put_u32(out, id);
    return out;
}
// 0x31: quest message `message` heard from unit `id` (FUN_004786a0, the
// message zero-extended to a u32; sent as a quest line starts and again if
// one playing is cut, 0x4b4670 / 0x4b4812). The host (54ba90 ->
// FUN_005443b0) looks the NPC up and hands every record of the player's
// act its "message heard" (+0xcc); no open chat is needed.
inline auto quest_message(std::uint32_t id, std::uint32_t message) -> Bytes {
    Bytes out;
    put_u8(out, 0x31); put_u32(out, id); put_u32(out, message);
    return out;
}
// 0x4f: a button (FUN_00478600, 7 bytes: u16 which, u16, u16); 0x18 the
// Cube's Transmute, 0x17 the Cube shut (the host's FUN_00568060 ->
// FUN_00566ae0, only while it has the Cube open for us).
inline auto button(std::uint16_t which, std::uint16_t first = 0, std::uint16_t second = 0) -> Bytes {
    Bytes out;
    put_u8(out, 0x4f); put_u16(out, which); put_u16(out, first); put_u16(out, second);
    return out;
}
// 0x38: an NPC action with NPC `id`: 1 trade (a real client's, recorded
// 2026-10-05 through d2proxy), 2 gamble (unverified).
inline auto npc_action(std::uint32_t action, std::uint32_t id, std::uint32_t extra = 0) -> Bytes {
    Bytes out;
    put_u8(out, 0x38); put_u32(out, action); put_u32(out, id); put_u32(out, extra);
    return out;
}
// 0x59: where we see unit (type, id), sent before an 0x13 on an NPC (a
// real client's, recorded 2026-10-05).
inline auto unit_position(std::uint32_t type, std::uint32_t id, std::uint32_t x, std::uint32_t y) -> Bytes {
    Bytes out;
    put_u8(out, 0x59); put_u32(out, type); put_u32(out, id); put_u32(out, x); put_u32(out, y);
    return out;
}
// 0x32: buy the stock item `item` from NPC `npc` at `cost`; flags 0 (a real
// client's buy of a potion, page 4: no tab in them), 0x80000000 fill.
inline auto buy(std::uint32_t npc, std::uint32_t item, std::uint32_t flags, std::uint32_t cost) -> Bytes {
    Bytes out;
    put_u8(out, 0x32); put_u32(out, npc); put_u32(out, item); put_u32(out, flags); put_u32(out, cost);
    return out;
}
// 0x33: sell our item `item` to NPC `npc` at `cost` (+9 u16 mode, +0xd cost).
inline auto sell(std::uint32_t npc, std::uint32_t item, std::uint16_t mode, std::uint32_t cost) -> Bytes {
    Bytes out;
    put_u8(out, 0x33); put_u32(out, npc); put_u32(out, item); put_u16(out, mode); put_u16(out, 0); put_u32(out, cost);
    return out;
}

// 0x69: leave the game; the host answers with the save (B3), B0, 05, 06.
inline auto leave() -> Bytes { return { 0x69 }; }

} // namespace d2d::net::d2gs::c2s
