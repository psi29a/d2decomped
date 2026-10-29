// S->C decoders that are more than a field read (net-packets.md).
#pragma once

#include "wire.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace d2d::net::d2gs::s2c {

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
// "0xac"): LSB-first bits from +0xd to the length at +0xc. Mode 4,
// components (1, then 16, each 1 bit under 3 choices, else bit_width(n - 1)
// with n MonStats2's HDv..S8v count), then the boss: 1, 5 flags (champion,
// unique, superunique, minion, ghostly), a superunique's SuperUniques row
// (16), mods 8 bits each till a 0 (at most 9), the name seed (16), then 1
// and an owning player's id (32); then the owner: 1, its id (31: a pet's
// or merc's player).
struct MonsterBoss { int flags = 0; int super = -1; std::vector<int> mods; int name_seed = 0; };   // flags: 1 champion, 2 unique, 4 superunique, 8 minion, 0x10 ghostly
struct MonsterTail { std::optional<MonsterBoss> boss; std::uint32_t owner = 0; };              // owner 0: none
inline auto monster_tail(std::span<const std::uint8_t> packet, const std::array<std::uint8_t, 16>& choices) -> std::optional<MonsterTail> {
    if (packet.size() < 0xd || packet[0] != 0xac) return std::nullopt;
    const std::size_t end = std::min<std::size_t>(packet[0xc], packet.size()) * 8;
    std::size_t bit_at = 0xd * 8;
    bool over = false;
    auto bits = [&](int count) {
        std::uint32_t value = 0;
        for (int bit = 0; bit < count; ++bit, ++bit_at) {
            if (bit_at >= end) { over = true; return 0u; }
            value |= std::uint32_t(packet[bit_at / 8] >> (bit_at % 8) & 1) << bit;
        }
        return value;
    };
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
        if (bit_at < end && bits(1)) bits(32);
        if (over) return std::nullopt;
        out.boss = std::move(boss);
    }
    if (bit_at < end && bits(1)) out.owner = bits(31);
    if (over) out.owner = 0;                       // cut short: no owner read
    return out;
}
inline auto monster_boss(std::span<const std::uint8_t> packet, const std::array<std::uint8_t, 16>& choices) -> std::optional<MonsterBoss> {
    const auto tail = monster_tail(packet, choices);
    return tail ? tail->boss : std::nullopt;
}

}  // namespace d2d::net::d2gs::s2c
