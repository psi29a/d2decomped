// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace d2d::rules {

// What a potion will have brought by its end, as a percent of the max
// (FUN_005485b0 life / FUN_00548640 mana, the server's, as life_update_frame
// goes): the value now
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

// The server's life update, a player's each frame (FUN_0052d980 ->
// FUN_00548760): what it told the client last. Each of the player's frames
// adds one to `frames` (FUN_005380d0, client +0x1b0). The update is
// forced at 20 frames (10 with the client's +0x1b8, not traced); unforced
// it goes only once life is 10 % of max (whole points) off the last sent
// and not at 0 from above. Going, it sends S->C 0x18 when either end
// percent (life_end_percent / mana_end_percent) differs from the last
// (else 0x95 / 0x96, which leave stat 0x4a / 0x1a be), keeps life and
// resets `frames`. The join forces one (FUN_00539760: FUN_00548760(1)),
// hence `frames` starting at 19.
struct LifeSent { int life = 0, life_percent = 0, mana_percent = 0, frames = 19; };

constexpr void life_update_frame(LifeSent& sent, int life_points, int max_points, int life_percent, int mana_percent) {
    ++sent.frames;
    const bool forced = sent.frames >= 20;
    if (max_points < 1) return;
    const int off = sent.life > life_points ? sent.life - life_points : life_points - sent.life;
    if (!forced && (off * 100 / max_points < 10 || (life_points == 0 && sent.life != 0))) return;
    if (life_percent != sent.life_percent || mana_percent != sent.mana_percent) { sent.life_percent = life_percent; sent.mana_percent = mana_percent; }
    sent.life = life_points;
    sent.frames = 0;
}

// The rows of a globe's potion preview (FUN_00496f80 life / FUN_00497110
// mana): with a healthpot / manapot (state 100 / 0x6a) on, the client
// player's stat 0x4a / 0x1a in all (FUN_00625480: the percent above plus
// the gear's hpregen / manarecovery) x 80 / 100, at most 80. Drawn (draw
// mode 0) only above the fill's own rows.
constexpr int globe_preview_rows(int percent) { return std::min(percent * 80 / 100, 80); }

// A press on a globe (FUN_00499500, which takes nothing at or above
// H-0x30, nor with no player or a dead one, FUN_00463df0): x 0x1e..0x6e
// life, W-0x6f..W-0x1f mana, y H-0x2f..H-0xf. It toggles that globe's
// "Show HP Text" / "Show MP Text" (0x7befdc / 0x7befe0) and writes it to
// the registry (FUN_004150e0; FUN_004967f0 reads both at the game's
// start). 1 life, 2 mana, 0 neither.
constexpr int globe_clicked(int mouse_x, int mouse_y, int width, int height) {
    if (mouse_y <= height - 0x30 || mouse_y > height - 0xf) return 0;
    if (mouse_x >= 0x1e && mouse_x <= 0x6e) return 1;
    if (mouse_x >= width - 0x6f && mouse_x <= width - 0x1f) return 2;
    return 0;
}

}  // namespace d2d::rules
