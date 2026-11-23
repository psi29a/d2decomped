// SPDX-License-Identifier: GPL-3.0-or-later
// A held spinner's step (FUN_004bbfb0, the gold box's arrows), `held_ms`
// after the press with `draws` frames drawn since (FUN_004bbe70's +0x40):
// 1 for the first second, then the draws >> 5, >> 4, >> 2 past 1, 2, 3 s,
// and (ms held >> 11) x the draws past 4096 ms.
#pragma once

#include <cstdint>

namespace d2d::client {

constexpr std::int64_t spinner_step(std::uint32_t held_ms, std::int64_t draws) {
    if (held_ms > 0x1000) return std::int64_t(held_ms >> 11) * draws;
    if (held_ms > 3000) return draws >> 2;
    if (held_ms > 2000) return draws >> 4;
    if (held_ms > 1000) return draws >> 5;
    return 1;
}

}  // namespace d2d::client
