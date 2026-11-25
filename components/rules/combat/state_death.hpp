// SPDX-License-Identifier: GPL-3.0-or-later
// Which states a death keeps (FUN_00639fb0, run by FUN_0057f330 for a dying
// player, FUN_005a6520 for a dying monster): the States.txt columns
// plrstaydeath / monstaydeath / bossstaydeath (the loader FUN_00618100's
// strings) are masks at data tables +0x100 / +0x104 / +0x108.
#pragma once

#include <cstdint>
#include <span>

namespace d2d::rules {

// FUN_00639fb0's move: per dword, second |= ~keep & first, then first &= keep
// (the spans are one dword per 32 states; the shortest bounds the walk).
void states_leave_on_death(std::span<std::uint32_t> first, std::span<std::uint32_t> second, std::span<const std::uint32_t> keep);

}   // namespace d2d::rules
