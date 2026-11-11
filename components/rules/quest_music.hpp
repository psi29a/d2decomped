// SPDX-License-Identifier: GPL-3.0-or-later
// A quest's music sequence, from game.exe 1.14d's client: FUN_004dcd40
// (ECX the jingle, EDX its delay, then back, from_block, the line, its
// delay, speaks), run by the sound tick FUN_004dcaa0 (0x4dcb6a..0x4dcc16).
// The level's music fades out at once (FUN_004ba8f0); the jingle plays
// `jingle_at` frames on; the class's line `line_at` frames on when `speaks`
// (on the player, FUN_004b9a00 with EDX the unit); at `back` the jingle fades
// (FUN_004ba890) and the level's song comes back, from its Block 1 cue when
// `from_block`, else from its start (0x4dcd76: the song record's +0x50 +
// 4 x from_block). Frames are DAT_007a0498's (FUN_0044db00), 25 a second.
// docs/research/re/unit-sounds.md "Quest music".
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace d2d::rules {

struct QuestMusic {
    int event = 0;                  // the unit sound event (FUN_004cb9c0), 0 for an object's
    std::string_view jingle;        // its Sounds.txt row, "" none
    int jingle_at = 0, back = 0, line_at = 0;
    bool speaks = false, from_block = false;
};

// By unit event (FUN_004cb9c0, 0x4cba52..0x4cbbfd), the classic ones, then
// by object (FUN_004ad1a0's mode handlers: 0x46b146 the Horadric Staff,
// 0x46b33e the Hellforge, 0x46b883 the Compelling Orb, 0x46bc28 Izual's).
inline constexpr std::array<QuestMusic, 12> kQuestMusic{ {
    { 0x21, "music_quest_andariel", 0, 0x1db, 0x1a9, true, false },
    { 0x22, "music_quest_bloodraven", 0x1db, 0x320, 0x20d, true, true },
    { 0x23, "music_quest_den", 0, 0x12c, 0xfa, true, true },
    { 0x25, "", 0, 0x5dc, 0, false, false },                         // the Tower: the line waits for event 0x5c
    { 0x32, "music_quest_radament", 0x1db, 0x44c, 0x20d, true, true },
    { 0x34, "music_quest_tainted", 0, 0x1a9, 0x177, true, true },
    { 0x42, "music_quest_mephisto", 0, 0x1a9, 0x177, true, false },
    { 0x4b, "music_quest_diablo", 0, 0x1db, 0x145, true, false },
    { 0, "music_quest_horadric", 0, 0x2d5, 0, false, true },
    { 0, "music_quest_forge", 0, 0x15e, 0, false, false },
    { 0, "music_quest_compelling", 0, 0x15e, 0, false, false },
    { 0, "music_quest_izual", 0, 0xfa, 0, false, false },
} };

inline std::optional<QuestMusic> quest_music_event(int event) {
    for (const auto& row : kQuestMusic)
        if (row.event != 0 && row.event == event) return row;
    return std::nullopt;
}
// Any row by its jingle.
inline std::optional<QuestMusic> quest_music_jingle(std::string_view jingle) {
    for (const auto& row : kQuestMusic)
        if (!jingle.empty() && row.jingle == jingle) return row;
    return std::nullopt;
}
// When unit event `event`'s class line plays, from `now_ms` (40 ms frames).
inline std::uint32_t quest_line_due(int event, std::uint32_t now_ms) {
    const auto row = quest_music_event(event);
    return row && row->speaks ? now_ms + std::uint32_t(row->line_at) * 40 : now_ms;
}

}  // namespace d2d::rules
