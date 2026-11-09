// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace d2d::rules {

// What a potion will have brought by its end, as a percent of the max
// (FUN_005485b0 life / FUN_00548640 mana, the server's): the value now
// plus `per_frame` x `frames_left` (the healthpot / manapot state's frames
// to go, its end FUN_00626090 less the game's frame), all in 256ths. Life
// takes the state's own hpregen (FUN_00625d00) and divides whole points;
// mana the unit's whole regen (manarecovery 0x1a in all, plus the steady
// rate: rules::mana_per_frame) over the 256ths max. Then the byte quirk:
// the low byte of the sum, 100 when that's past 100. 0 with no max. Sent
// in S->C 0x18's 7-bit fields (FUN_0053c230 from FUN_00548760), which the
// client sets as its player's stat 0x4a / 0x1a (FUN_0045d9b0).
constexpr int end_percent_byte(std::int64_t percent) {
    const int low = int(percent & 0xff);
    return low > 100 ? 100 : low;
}
constexpr int life_end_percent(std::int64_t life, std::int64_t max_life, std::int64_t per_frame, std::int64_t frames_left) {
    if (max_life >> 8 == 0) return 0;
    return end_percent_byte(((per_frame * frames_left + life) >> 8) * 100 / (max_life >> 8));
}
constexpr int mana_end_percent(std::int64_t mana, std::int64_t max_mana, std::int64_t per_frame, std::int64_t frames_left) {
    if (max_mana == 0) return 0;
    return end_percent_byte((frames_left * per_frame + mana) * 100 / max_mana);
}

// The rows of a globe's potion preview (FUN_00496f80 life / FUN_00497110
// mana): with a healthpot / manapot (state 100 / 0x6a) on, the client
// player's stat 0x4a / 0x1a in all (FUN_00625480: the percent above plus
// the gear's hpregen / manarecovery) x 80 / 100, at most 80. Drawn (draw
// mode 0) only above the fill's own rows.
constexpr int globe_preview_rows(int percent) { return std::min(percent * 80 / 100, 80); }

}  // namespace d2d::rules
