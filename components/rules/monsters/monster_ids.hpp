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
inline constexpr int kWarrivAct2 = 175;    // warriv2: Lut Gholein's
inline constexpr int kJerhyn = 201;
inline constexpr int kAtma = 176;
inline constexpr int kDrognan = 177;
inline constexpr int kCainAct2 = 244;      // cain2: Lut Gholein's
inline constexpr int kRadament = 229;
inline constexpr int kSummoner = 250;
inline constexpr int kDuriel = 211;
inline constexpr int kTyrael = 251;      // tyrael1: in Duriel's Lair
inline constexpr int kMeshif = 210;      // meshif1: Lut Gholein's
inline constexpr int kMeshifAct3 = 264;  // meshif2: Kurast Docks'
inline constexpr int kFlavie = 266;        // navi: the rogue at the Blood Moor's way to the Cold Plains
inline constexpr int kPalaceGuard = 331;   // act2guard2 (MonAI JarJar): at the palace gate
inline constexpr int kAndariel = 156;
inline constexpr int kMephisto = 242;
inline constexpr int kDarkWanderer = 368;                  // a3q7's, the Spider Forest's head (MonAI 91)
inline constexpr int kFara = 178;
inline constexpr int kCainAct3 = 245;     // cain3
inline constexpr int kAsheara = 252;
inline constexpr int kHratli = 253;
inline constexpr int kAlkor = 254;
inline constexpr int kNatalya = 297;
inline constexpr int kOrmus = 255;
inline constexpr int kHalbu = 257;
inline constexpr int kCampCain = 265;      // cain5: in the Rogue Encampment once rescued
inline constexpr int kBloodRaven = 267;
inline constexpr int kHellBovine = 391;
inline constexpr int kJamella = 405;
inline constexpr int kLarzuk = 511;
inline constexpr int kMalah = 513;

// The client's critters (critters.md: FUN_0046d780's jump table by row).
inline constexpr int kChicken = 149, kRat = 151, kBird1 = 157, kBird2 = 158, kBat = 159, kMaggot = 227, kBug = 268, kScorpion = 269;
inline constexpr int kLarva = 283, kSnake = 318, kParrot = 319, kFish = 320, kMiniSpider = 339, kBunny = 556;

}  // namespace d2d::rules::monster_ids
