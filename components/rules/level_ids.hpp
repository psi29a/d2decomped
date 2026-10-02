// SPDX-License-Identifier: GPL-3.0-or-later
// Levels.txt rows (Id) the code names, by their LevelName: the five towns
// and the levels the generators, quests and rules single out. Only the
// ones in use; the rest come from the table at run time.
#pragma once

namespace d2d::rules::level_ids {

// The towns, each its act's first level.
inline constexpr int kRogueEncampment = 1;
inline constexpr int kLutGholein = 40;
inline constexpr int kKurastDocks = 75;
inline constexpr int kPandemoniumFortress = 103;
inline constexpr int kHarrogath = 109;

constexpr bool is_town(int level_id) {
    return level_id == kRogueEncampment || level_id == kLutGholein || level_id == kKurastDocks || level_id == kPandemoniumFortress || level_id == kHarrogath;
}

// Act 1.
inline constexpr int kBloodMoor = 2;
inline constexpr int kColdPlains = 3;
inline constexpr int kStonyField = 4;
inline constexpr int kDarkWood = 5;
inline constexpr int kBlackMarsh = 6;
inline constexpr int kTamoeHighland = 7;
inline constexpr int kDenOfEvil = 8;
inline constexpr int kCaveLevel1 = 9;
inline constexpr int kUndergroundPassageLevel1 = 10;
inline constexpr int kBurialGrounds = 17;
inline constexpr int kCrypt = 18;
inline constexpr int kMausoleum = 19;
inline constexpr int kForgottenTower = 20;
inline constexpr int kTowerCellarLevel1 = 21;
inline constexpr int kTowerCellarLevel5 = 25;
inline constexpr int kMonasteryGate = 26;
inline constexpr int kOuterCloister = 27;
inline constexpr int kBarracks = 28;
inline constexpr int kJailLevel1 = 29;
inline constexpr int kJailLevel2 = 30;
inline constexpr int kJailLevel3 = 31;
inline constexpr int kCatacombsLevel1 = 34;
inline constexpr int kCatacombsLevel2 = 35;
inline constexpr int kCatacombsLevel4 = 37;
inline constexpr int kTristram = 38;
inline constexpr int kMooMooFarm = 39;

// Acts 2 .. 5.
inline constexpr int kArcaneSanctuary = 74;
inline constexpr int kTravincal = 83;
inline constexpr int kBloodyFoothills = 110;
inline constexpr int kFrigidHighlands = 111;
inline constexpr int kArreatPlateau = 112;
inline constexpr int kFrozenTundra = 117;
inline constexpr int kMatronsDen = 133;

}  // namespace d2d::rules::level_ids
