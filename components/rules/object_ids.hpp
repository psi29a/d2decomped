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
inline constexpr int kSunAltar = 24;
inline constexpr int kTeleportPad = 27;     // FUN_00581bf0: the Arcane Sanctuary's pads
inline constexpr int kSanctuaryPortal = 34; // FUN_005846b0: Palace Cellar 3 <-> the Arcane Sanctuary
inline constexpr int kJournal = 42;         // FUN_0059b970: Horazon's Journal
inline constexpr int kOrifice = 25;         // FUN_0059dc70: where the Horadric Staff goes        // FUN_0059a7e0: the Tainted Sun's altar
inline constexpr int kStash = 32;
inline constexpr int kSewerStairs = 44;     // FUN_005b84e0: the sewers' way down, open (mode 2) by the lever (a3q2, object 366)
inline constexpr int kSewerLever = 45;      // FUN_005b8530 (object 367)
inline constexpr int kCompellingOrb = 53;   // FUN_005bb980: hit with Khalim's Will (a3q5, object 404)
inline constexpr int kKhalimHeart = 57, kKhalimEye = 58, kKhalimBrain = 59;   // the Khalim chests (a3q2: FUN_005b8860 / 005b8940 / 005b8a20)
inline constexpr int kGidbinnAltar = 31;    // FUN_005b9b40: the Gidbinn's altar (a3q3, object 252)
inline constexpr int kLamTome = 28;         // FUN_005b7a60: Lam Esen's tome on its stand (a3q1)
inline constexpr int kHellgate = 46;        // the red portal to the Pandemonium Fortress (a3q6)
// Act 4's (quests-act4.md): the burning souls' (FUN_005869f0), the Hellforge
// (FUN_005b5c10), the seals (52 FUN_005b5630; 54 / 55 / 56 each with its
// boss, FUN_005b6b70..), Tyrael's last portal to Harrogath (FUN_005b5880).
inline constexpr int kBurningSoul = 48, kHellforge = 49, kSeal = 52, kSealInfector = 54, kSealDeSeis = 55, kSealVizier = 56, kLastPortal = 73;
inline constexpr int kCubeChest = 39, kScrollChest = 40, kShaftChest = 41;   // the Horadric Staff's chests (FUN_00599df0 / FUN_00599c10 / FUN_00599cf0)
}  // namespace operate_fn

// objects.txt rows (Id).
namespace object_ids {
inline constexpr int kExplodingBarrel = 11;   // Barrel (BarrelExploding)
inline constexpr int kStoneAlpha = 17;        // the Cairn stone the portal opens by
inline constexpr int kGibbet = 26;
inline constexpr int kMephistoBridge = 341;   // InitFn 45: rises over the lava once he's dead (a3q6)
inline constexpr int kHellgate = 342;         // InitFn 44: the red portal to Act 4 (a3q6)
inline constexpr int kDiabloStart = 255;      // InitFn 55: where Diablo comes (a4q2)
inline constexpr int kLastPortal = 566;       // OperateFn 73: Tyrael's portal to Harrogath (a4q2, LoD)
inline constexpr int kLargeChestR = 371;      // the Countess' treasure chests
inline constexpr int kSparklyChest = 397;     // rolls its own table (open_container)
inline constexpr int kTownPortal = 59;        // a cyan automap mark (FUN_00459bc0)
inline constexpr int kPermanentPortal = 60;   // cyan too, but on levels 111, 112, 117, 125..127
inline constexpr int kBank = 267;             // the stash: an automap icon only in Acts 3 and 4 (FUN_00458dc0)
inline constexpr int kSewerStairs = 366;      // its automap icon only open (mode 2)
inline constexpr int kSanctuaryWaypoint = 402;   // its automap icon only in the Arcane Sanctuary
inline constexpr int kSubclassWaypoint = 0x40;   // objects.txt SubClass bit: a waypoint
}  // namespace object_ids

}  // namespace d2d::rules
