// SPDX-License-Identifier: GPL-3.0-or-later
// The Horadric Staff's light show over the orifice — game.exe 1.14d, the
// client's quest event 3 (0x46b100) and missile 338's client function 31
// (FUN_004d6820) (docs/research/re/quests-act2.md "The orifice").
#pragma once

#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace d2d::rules {

namespace staff_show {

// Missiles.txt rows: the staff, its beams, the lightning at the way down, the dust there.
inline constexpr int kStaff = 338, kBeam = 340, kLightning = 339, kDust = 150;
inline constexpr int kStaffRange = 440;            // the staff's Range: its frames
inline constexpr int kLightJet = 0xb5;              // Overlay.txt light_jet, once on the orifice (FUN_00470390 kind 2)
// The way down's spot from the orifice's, subtiles (FUN_0059d870 and the lightning's target).
inline constexpr int kWayDownX = -13, kWayDownY = 3;

// The beams (the table at 0x6db7f0, 12 bytes a row): each one's offset
// from the staff in subtiles and its direction (of 64), the first made at
// 325 frames left, then one every 25.
struct Beam { int dx, dy, dir; };
inline constexpr std::array<Beam, 7> kBeams{ { { 3, -2, 4 }, { 1, -3, 58 }, { 3, 1, 13 }, { -2, -3, 49 }, { 1, 3, 22 }, { -3, 0, 40 }, { -2, 2, 31 } } };
inline constexpr int kFirstBeam = 0x145, kBeamStep = 0x19;
inline constexpr int kLightningBelow = 0xa5, kDustBelow = 0x96, kShakeAt = 0x96;
// The shake (FUN_00476a80 at r 0x96): strength r - 0x7d, no ramp, 4000 ms held, no fade.
inline constexpr int kShakeStrength = kShakeAt - 0x7d;
inline constexpr std::uint32_t kShakeHoldMs = 4000;

// What client function 31 makes on a frame with `remaining` frames left
// (FUN_0064a380), in its order: a beam (its row in kBeams, living
// `remaining` frames: all end with the staff), the shake, the lightning
// (every even frame under 165), the dust (every frame under 150).
struct Step {
    int beam = -1;
    bool shake = false, lightning = false, dust = false;
};
[[nodiscard]] constexpr Step step(int remaining) {
    Step out;
    if (remaining == kShakeAt) out.shake = true;
    for (int row = 0, at = kFirstBeam; row < int(kBeams.size()); ++row, at -= kBeamStep) {
        if (at < remaining) break;
        if (remaining == at) out.beam = row;
    }
    out.lightning = remaining < kLightningBelow && remaining % 2 == 0;
    out.dust = remaining < kDustBelow;
    return out;
}

// The dust's target from the way down's spot (FUN_004d6820): 1 + rand(5)
// across, rand(10) - 5 down, two draws on the staff's seed (unit +0x20).
struct Spot { int dx, dy; };
[[nodiscard]] inline Spot dust_target(Rng& seed) {
    const int across = 1 + seed(5);
    return { across, seed(10) - 5 };
}

// Heights in whole units (the motion record, 21.11, FUN_004da350), `frames`
// steps after it's made. The staff: from 0x28 up by 0x200 raw a step,
// stopping at 0x46 (flag 0x20: at or above). The lightning: from 0x46
// down one a step (-1 << 11), stopping at 0 (at or below its zero limit).
[[nodiscard]] constexpr float staff_height(int frames) {
    const std::int64_t raw = (std::int64_t(0x28) << 11) + std::int64_t(std::max(frames, 0)) * 0x200;
    return raw >= (std::int64_t(0x46) << 11) ? float(0x46) : float(raw) / 2048.f;
}
[[nodiscard]] constexpr float lightning_height(int frames) { return float(std::max(0x46 - std::max(frames, 0), 0)); }

}  // namespace staff_show

}  // namespace d2d::rules
