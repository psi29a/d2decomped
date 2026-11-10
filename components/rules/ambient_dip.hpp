// SPDX-License-Identifier: GPL-3.0-or-later
// The ambient's dip to black — game.exe 1.14d, the client's 0x7a742c..
// 0x7a7457 (docs/research/re/lighting.md "What fills it").
#pragma once

#include "level_ids.hpp"
#include "weather.hpp"

namespace d2d::rules {

// A dip (FUN_0046ae50 starts it, FUN_0046ad10 steps it each client frame
// from FUN_0046beb0): the fill falls on the cosine over `fade_in` frames,
// is 0 for the hold, and comes back over `fade_back`. While it's lit,
// FUN_0046bdd0 answers its intensity ahead of the level's light, in any
// level but 8, 107 and 108 (dip_light).
struct AmbientDip {
    bool running = false;                // 0x7a742c
    bool lit = false;                    // its colour set (the first step's fill): FUN_00474550 takes it
    int intensity = 0;                   // 0x7a7430
    int fade_in = 0, fade_back = 0;      // 0x7a7438, 0x7a743c
    int total = 0, frame = 0, angle = 0; // 0x7a7444, 0x7a7448, 0x7a744c (of 512 a turn)
    int level = 0;                       // 0x7a7450: another level's frame ends it

    // FUN_0046ae50(in, hold, back, level, force): a fade back of 0 is 25.
    // (0x7a7454, which `force` would get past, is never set.) A fade in of
    // 0 is game.exe's fatal error; here, no dip.
    void start(int in_frames, int hold, int back, int level_id) {
        *this = {};
        if (in_frames == 0) return;
        running = true;
        if (back == 0) back = 0x19;
        fade_in = in_frames;
        fade_back = back;
        total = in_frames + hold + back;
        level = level_id;
    }

    // FUN_0046ad10, one client frame in `client_level`; `fill`: its light
    // with no event (FUN_00474550 flag 0: the level's own, else the day's).
    // Each fade's step takes the angle the frame before left (the hold
    // keeps the fade in's last), then works out its own.
    void step(int fill, int client_level) {
        if (!running) return;
        lit = true;
        if (frame < fade_in) {
            intensity = int(double(cos512(angle)) * fill);
            angle = (frame << 7) / fade_in;
        } else if (frame < total - fade_back) {
            intensity = 0;
        } else {
            intensity = int(double(cos512(angle)) * fill);
            const int into = fade_back - total + frame;
            if (fade_back == 0) fade_back = 0x19;
            angle = 0x80 - (into << 7) / fade_back;
        }
        ++frame;
        if (frame > total || client_level != level) *this = {};
    }

    // FUN_0046bdd0's answer in `level_id` with the dip lit, -1 none (the
    // level's light; the Den and Diablo's release answer in 8, 107, 108).
    [[nodiscard]] int light(int level_id) const {
        if (!lit || level_id == level_ids::kDenOfEvil || level_id == level_ids::kRiverOfFlame || level_id == level_ids::kChaosSanctuary) return -1;
        return intensity;
    }
};

// Who starts one (lighting.md):
// - States.txt setfunc 2 (FUN_004d87e0), cloak_of_shadows (153) alone: in 5,
//   the hold 3/5 of the skill's auralencalc (Skills.txt +0x60, at the
//   state's skill level, stats 0x15e / 0x15f), back 2/5, in the unit's level;
inline constexpr int kCloakFadeIn = 5;
struct DipFrames { int fade_in = 0, hold = 0, back = 0; };
constexpr DipFrames cloak_dip(int aura_length) { return { kCloakFadeIn, aura_length * 3 / 5, aura_length * 2 / 5 }; }
// - S->C 0x89 event 1 (0x46aee0, FUN_00593710 as the Cairn Stones open):
//   missile cairnstones' Param1 (+0x38) in and back, its Range (+0x96) less
//   twice that held, in level 4.
constexpr DipFrames cairn_dip(int param1, int range) { return { param1, range - 2 * param1, param1 }; }

}  // namespace d2d::rules
