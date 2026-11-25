// SPDX-License-Identifier: GPL-3.0-or-later
// Server-side gates for quest-log sends (FUN_00544190).
#pragma once

namespace d2d::rules {

// The act byte (+8) of the server record behind quest log word `quest`, or -1 for
// a word with no record. The word's record is the client table 0x723f30's row +8
// (d2gs::s2c::quest_word_of_record); FUN_00545d80 copies each record's act into
// its +8 from the init table 0x731510 (24-byte rows, +4 the act, +0x10 the id).
// Read from both tables: words 1..6 Act 1, 9..14 Act 2, 17..22 Act 3, 25..27
// Act 4, 35..40 Act 5; word 34 is record 30, an Act 1 record (unshown).
[[nodiscard]] constexpr int quest_log_record_act(int quest) {
    if (quest >= 1 && quest <= 6) return 0;
    if (quest >= 9 && quest <= 14) return 1;
    if (quest >= 17 && quest <= 22) return 2;
    if (quest >= 25 && quest <= 27) return 3;
    if (quest == 34) return 0;
    if (quest >= 35 && quest <= 40) return 4;
    return -1;
}

// FUN_00544190's gate: the record's +8 act byte equals the player's level's act.
[[nodiscard]] constexpr bool quest_log_act_matches(int record_act, int player_act) {
    return record_act == player_act;
}

// Whether quest `quest`'s log record sends while the player is in `player_act`.
[[nodiscard]] constexpr bool quest_log_send_in_act(int quest, int player_act) {
    return quest_log_act_matches(quest_log_record_act(quest), player_act);
}

}  // namespace d2d::rules
