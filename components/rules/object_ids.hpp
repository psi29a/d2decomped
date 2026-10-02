// SPDX-License-Identifier: GPL-3.0-or-later
// Units and objects by the numbers game.exe and the data files use: the
// unit types (a DS1's and a room's unit lists), objects.txt OperateFn (what
// operating an object does) and the objects.txt rows (Id) the code names.
// Only the ones in use; the rest come from the tables at run time.
#pragma once

namespace d2d::rules {

// A unit's type (unit +0x00; a DS1 object's type): players, monsters,
// objects, missiles, items and warp tiles (LvlWarp rows).
namespace unit_type {
inline constexpr int kPlayer = 0;
inline constexpr int kMonster = 1;
inline constexpr int kObject = 2;
inline constexpr int kMissile = 3;
inline constexpr int kItem = 4;
inline constexpr int kWarp = 5;
}  // namespace unit_type

// objects.txt OperateFn: what operating the object does (docs/research/re/objects.md).
namespace operate_fn {
inline constexpr int kCasket = 1;           // FUN_00586410: opens only when its round drops
inline constexpr int kShrine = 2;           // FUN_00583c70
inline constexpr int kUrn = 3;              // FUN_005866c0
inline constexpr int kChest = 4;            // FUN_00585f60
inline constexpr int kBarrel = 5;           // FUN_005868a0
inline constexpr int kMoldyTome = 6;        // FUN_00594e70: the Forgotten Tower's
inline constexpr int kExplodingBarrel = 7;
inline constexpr int kDoor = 8;             // FUN_00581d40
inline constexpr int kCairnStone = 9;
inline constexpr int kGibbet = 10;          // Cain's cage in Tristram
inline constexpr int kInifussTree = 12;
inline constexpr int kCorpse = 14;          // FUN_005867a0
inline constexpr int kTrapDoor = 16;        // FUN_00581eb0
inline constexpr int kSecretDoor = 18;      // FUN_00583ff0
inline constexpr int kArmorStand = 19;
inline constexpr int kWeaponRack = 20;
inline constexpr int kMalusStand = 21;      // the Horadric Malus' stand
inline constexpr int kWell = 22;            // FUN_005858a0
inline constexpr int kWaypoint = 23;        // FUN_00584e30
inline constexpr int kBookshelf = 26;       // FUN_00584060
inline constexpr int kTrapObject = 30;      // FUN_00581cd0: the exploding chest
inline constexpr int kStash = 32;
}  // namespace operate_fn

// objects.txt rows (Id).
namespace object_ids {
inline constexpr int kExplodingBarrel = 11;   // Barrel (BarrelExploding)
inline constexpr int kStoneAlpha = 17;        // the Cairn stone the portal opens by
inline constexpr int kGibbet = 26;
inline constexpr int kLargeChestR = 371;      // the Countess' treasure chests
inline constexpr int kSparklyChest = 397;     // rolls its own table (open_container)
inline constexpr int kSubclassWaypoint = 0x40;   // objects.txt SubClass bit: a waypoint
}  // namespace object_ids

}  // namespace d2d::rules
