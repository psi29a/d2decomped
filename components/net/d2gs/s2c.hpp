// S->C decoders that are more than a field read (net-packets.md).
#pragma once

#include "bit_buffer.hpp"
#include "wire.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace d2d::net::d2gs::s2c {

struct AnimationStats {
    int velocity = 0, attack_rate = 0, ias = 0, frw = 0;
    [[nodiscard]] bool empty() const { return velocity == 0 && attack_rate == 0 && ias == 0 && frw == 0; }
};

struct StateStats {
    AnimationStats animation;
    int skill = -1, level = 0;
};

// FUN_0045ee20 and FUN_0045efa0 read state lists using ItemStatCost's send widths.
// The animation stats selected below are signed in 1.14d and have no param bits.
inline auto read_state_stats(BitReader& bits, std::span<const std::pair<int, int>> send_bits)
    -> std::optional<StateStats> {
    StateStats out;
    for (;;) {
        const auto stat = bits.read(9);
        if (bits.cursor().overflow) return std::nullopt;
        if (stat == 0x1ff) return out;
        if (stat >= send_bits.size()) return std::nullopt;
        const auto [width, param] = send_bits[stat];
        if (width <= 0 || width > 32 || param < 0 || param > 32) return std::nullopt;
        bits.read(param);
        switch (stat) {
        case 67: out.animation.velocity = bits.read_signed(width); break;
        case 68: out.animation.attack_rate = bits.read_signed(width); break;
        case 93: out.animation.ias = bits.read_signed(width); break;
        case 96: out.animation.frw = bits.read_signed(width); break;
        case 0x15e: out.skill = int(bits.read(width)); break;
        case 0x15f: out.level = int(bits.read(width)); break;
        default: bits.read(width); break;
        }
        if (bits.cursor().overflow) return std::nullopt;
    }
}

struct MonsterCommand {
    std::uint32_t id;
    std::uint8_t command;
    std::optional<std::pair<std::uint16_t, std::uint16_t>> point, correction;
    std::optional<std::pair<std::uint8_t, std::uint32_t>> target;
};

// FUN_0045cea0 / FUN_0045cef0 dispatch the aim. FUN_0045cf40 /
// FUN_0045cfb0 first correct the monster to the subtiles at +0xc and +0xe.
inline auto monster_command(std::span<const std::uint8_t> packet) -> std::optional<MonsterCommand> {
    if (packet.empty() || packet[0] < 0x69 || packet[0] > 0x6c) return std::nullopt;
    const bool corrected = packet[0] >= 0x6b;
    if (packet.size() != (corrected ? 16u : 12u)) return std::nullopt;
    MonsterCommand out{read_u32(packet, 1), packet[5], {}, {}, {}};
    if (packet[0] == 0x69 || packet[0] == 0x6b) out.point = {read_u16(packet, 6), read_u16(packet, 8)};
    else out.target = {packet[6], read_u32(packet, 7)};
    if (corrected) out.correction = {read_u16(packet, 12), read_u16(packet, 14)};
    return out;
}

// 0x27 NPC info, mode 1: the NPC's quest lines for this player
// (FUN_004a1600 -> FUN_00661510): +2 u32 the NPC's id, +6 u8 count (< 8),
// from +8 four bytes a line, u8 kind (0 greets, 2 a topic) and u16 message
// at +10. A lone kind 3 is a number over the NPC: not quest lines.
struct NpcLine { std::uint16_t message = 0; bool greet = false; };
struct NpcLines { std::uint32_t id = 0; std::vector<NpcLine> lines; };
inline auto npc_lines(std::span<const std::uint8_t> packet) -> std::optional<NpcLines> {
    if (packet.size() < 40 || packet[0] != 0x27 || packet[1] != 1 || packet[6] >= 8 || (packet[6] == 1 && packet[8] == 3)) return std::nullopt;
    NpcLines out{ .id = read_u32(packet, 2) };
    for (std::size_t line = 0; line < packet[6]; ++line) out.lines.push_back({ read_u16(packet, 10 + 4 * line), packet[8 + 4 * line] == 0 });
    return out;
}

// 0x01 game flags (0x45c8b0): +2 the u32 FUN_0047a9a0 keeps at 0x712efc.
// Its bit 1 (FUN_0047a980) has the Party roster sort by 0x65's word
// (FUN_004799c0, party.md).
inline auto game_flags(std::span<const std::uint8_t> packet) -> std::optional<std::uint32_t> {
    if (packet.size() < 6 || packet[0] != 0x01) return std::nullopt;
    return read_u32(packet, 2);
}
inline auto roster_by_sort_key(std::uint32_t flags) -> bool { return (flags >> 1 & 1) != 0; }

// 0x65 (0x45e6b0 -> 0x479b10): +1 u32 a player's id, +5 u16 its roster
// record's +0x18, sign-extended (00479b1e..00479b40).
struct RosterKey { std::uint32_t id = 0; int sort_key = 0; };
inline auto roster_key(std::span<const std::uint8_t> packet) -> std::optional<RosterKey> {
    if (packet.size() < 7 || packet[0] != 0x65) return std::nullopt;
    return RosterKey{ .id = read_u32(packet, 1), .sort_key = int(std::int16_t(read_u16(packet, 5))) };
}

// The quest log's row (= quest word) of a quest record id, as 0x5d's +1
// names it (FUN_004a1910: the table at 0x723f30, +8 the record, 41 rows;
// 37 is "none"). -1 when no row has it.
inline auto quest_word_of_record(int record) -> int {
    static constexpr std::array<int, 41> kRecordOfWord = { 37, 1, 2, 3, 4, 5, 6, 37, 37, 8, 9, 10, 11, 12, 13, 37, 37, 15, 16, 17, 18,
                                                           19, 20, 37, 37, 22, 23, 24, 37, 37, 37, 37, 37, 37, 30, 31, 32, 33, 34, 35, 36 };
    const auto found = std::ranges::find(kRecordOfWord, record);
    return found == kRecordOfWord.end() ? -1 : int(found - kRecordOfWord.begin());
}

// 0xac assign monster's boss, from its tail (FUN_0045f190, net-packets.md
// "0xac"): LSB-first bits from +0xd to the length at +0xc, read as
// game.exe does (FUN_00410e40 over +0xd, length +0xc less 0xd; then
// FUN_00411020). Mode 4, components (1, then 16, each 1 bit under 3
// choices, else bit_width(n - 1) with n MonStats2's HDv..S8v count), then
// the boss: 1, 5 flags (champion, unique, superunique, minion, ghostly), a
// superunique's SuperUniques row (16), mods 8 bits each till a 0 (at most
// 9), the name seed (16), then 1 and an owning player's id (32); then the
// owner: 1, its id (31: a pet's or merc's player).
struct MonsterBoss { int flags = 0; int super = -1; std::vector<int> mods; int name_seed = 0; };   // flags: 1 champion, 2 unique, 4 superunique, 8 minion, 0x10 ghostly
struct MonsterTail { std::optional<MonsterBoss> boss; std::uint32_t owner = 0; };              // owner 0: none
inline auto monster_tail(std::span<const std::uint8_t> packet, const std::array<std::uint8_t, 16>& choices) -> std::optional<MonsterTail> {
    if (packet.size() < 0xd || packet[0] != 0xac) return std::nullopt;
    const std::size_t end = std::min<std::size_t>(packet[0xc], packet.size());
    BitReader reader(packet.subspan(0xd, end > 0xd ? end - 0xd : 0));
    auto bits = [&](int count) { return reader.read(count); };
    auto more = [&] { return reader.cursor().byte * 8 + reader.cursor().bit < reader.cursor().bits; };   // a bit left
    bits(4);
    if (bits(1))
        for (const int count : choices) bits(count < 3 ? 1 : std::bit_width(unsigned(count - 1)));
    MonsterTail out;
    if (bits(1)) {
        MonsterBoss boss;
        for (int flag = 0; flag < 5; ++flag) boss.flags |= int(bits(1)) << flag;
        if (boss.flags & 4) boss.super = int(bits(16));
        for (std::uint32_t mod = bits(8); mod != 0 && boss.mods.size() < 9; mod = bits(8)) boss.mods.push_back(int(mod));
        boss.name_seed = int(bits(16));
        if (more() && bits(1)) bits(32);
        if (reader.cursor().overflow) return std::nullopt;
        out.boss = std::move(boss);
    }
    if (more() && bits(1)) out.owner = bits(31);
    if (reader.cursor().overflow) out.owner = 0;                       // cut short: no owner read
    return out;
}
inline auto monster_boss(std::span<const std::uint8_t> packet, const std::array<std::uint8_t, 16>& choices) -> std::optional<MonsterBoss> {
    const auto tail = monster_tail(packet, choices);
    return tail ? tail->boss : std::nullopt;
}

}  // namespace d2d::net::d2gs::s2c
