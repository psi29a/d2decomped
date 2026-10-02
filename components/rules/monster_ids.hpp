// SPDX-License-Identifier: GPL-3.0-or-later
// MonStats.txt rows (hcIdx) the code names: the town NPCs it talks to,
// trades with or heals at, and the monsters the quests and drops watch.
// Only the ones in use; the rest come from the table at run time.
#pragma once

namespace d2d::rules::monster_ids {

inline constexpr int kCain = 146;          // cain1: caged in Tristram
inline constexpr int kGheed = 147;
inline constexpr int kAkara = 148;
inline constexpr int kKashya = 150;
inline constexpr int kCharsi = 154;
inline constexpr int kWarriv = 155;        // warriv1: Act 1's
inline constexpr int kAndariel = 156;
inline constexpr int kFara = 178;
inline constexpr int kHratli = 253;
inline constexpr int kOrmus = 255;
inline constexpr int kHalbu = 257;
inline constexpr int kCampCain = 265;      // cain5: in the Rogue Encampment once rescued
inline constexpr int kBloodRaven = 267;
inline constexpr int kHellBovine = 391;
inline constexpr int kJamella = 405;
inline constexpr int kLarzuk = 511;
inline constexpr int kMalah = 513;

}  // namespace d2d::rules::monster_ids
