// SPDX-License-Identifier: GPL-3.0-or-later
// d2d's get-hit LENGTH before the patch (the oracle's `before`): the ticks a
// get-hit holds in d2d upstream main 32768ddb, line for line, on the cases
// tools/emu/hit_len.py writes (`id kind speed frames fhr anim ...`). Prints
// `id rate ticks` (rate -1: d2d keeps none), one line a case.
//
//   g++ -O2 -std=c++20 -o hit_len_harness hit_len_harness.cpp
//   ./hit_len_harness < cases.tsv > before.tsv
//
// - player (components/game/fight.cpp:457..460, set_pmode): shown =
//   rules::speed_frames(frames, fhr) (combat.hpp:197: E = 120 v / (120 + v),
//   ceil(frames x 256 / (256 + floor(256 E / 100)))); len = shown x
//   ms_per_frame (gamedata.hpp:393: 40 x 256 / speed, integer); pmode_until =
//   now + len; the player's frame ends the mode at the first tick with now >=
//   pmode_until (fight.cpp:2395), ticks 40 ms (apps/d2d/town.cpp:2945).
// - monster (components/game/ai.cpp:172, set_mode): mode_until = now +
//   length_ms x 100 / (100 + anim_rate_pct) with length_ms = ms_per_frame x
//   frames (gamedata.hpp:396) and anim_rate_pct = anim_pct for GH (ai.hpp:379);
//   ai.cpp:1723 ends it at the first tick with now >= mode_until. The merc
//   (fight.cpp:2127, 4804) is the monster's rule with anim_pct 0.
#include <cstdio>

static int effective_speed(int speed, int cap = 120) { return speed > 0 ? cap * speed / (cap + speed) : speed; }

static int speed_frames(int base_frames, int speed_stat) {
    if (base_frames <= 0 || speed_stat <= 0) return base_frames > 0 ? base_frames : 0;
    const int scaled = 256 * effective_speed(speed_stat) / 100;
    return (base_frames * 256 + 256 + scaled - 1) / (256 + scaled);
}

static unsigned ms_per_frame(unsigned speed) { return 40u * 256u / (speed ? speed : 1u); }

static unsigned ticks_of(unsigned ms) { return (ms + 39u) / 40u; }

int main() {
    long id, kind, speed, frames, fhr, anim;
    char line[512];
    while (std::fgets(line, sizeof line, stdin)) {
        if (line[0] == '#' || std::sscanf(line, "%ld %ld %ld %ld %ld %ld", &id, &kind, &speed, &frames, &fhr, &anim) != 6) continue;
        unsigned ticks;
        if (kind == 0) {
            const int shown = speed_frames(int(frames), int(fhr));
            ticks = ticks_of(unsigned(shown) * ms_per_frame(unsigned(speed)));
        } else {
            const unsigned length_ms = ms_per_frame(unsigned(speed)) * unsigned(frames > 0 ? frames : 1);
            ticks = ticks_of(length_ms * 100u / unsigned(100 + anim));
        }
        std::printf("%ld -1 %u\n", id, ticks);
    }
    return 0;
}
