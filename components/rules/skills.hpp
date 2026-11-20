// SPDX-License-Identifier: GPL-3.0-or-later
// Skills: Skills.txt rows, the calc language they're written in (compiled
// here to a small stack program, run like game.exe's FUN_006c0bc0), the
// per-level values game.exe derives from a row (level brackets, elemental
// damage and length, to-hit, mana cost) and a character's level in each
// skill (points + item bonuses). docs/research/re/skills.md.
#pragma once

#include "combat.hpp"
#include "rules.hpp"

#include <d2s_items.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::rules {

// ---- the calc language

// A compiled calc: postfix instructions for game.exe's stack machine
// (FUN_006c0bc0). The ops carry the VM's own byte values. Operand codes are
// skillcalc.txt rows (FUN_00646460's switch: 0 ln12 .. 72 skpt); a Call's
// operand1 is the function table's index (0x745774, CalcFunction).
struct Calc {
    enum Op : std::uint8_t { Call = 0x01, Operand = 0x04, Const = 0x07,
                             Lt = 0x0a, Gt, Le, Ge, Eq, Ne, Add = 0x10, Sub, Mul, Div, Pow, Neg, Cond };
    struct Ins { Op opcode; int operand1 = 0; };
    std::vector<Ins> code;
    [[nodiscard]] bool empty() const { return code.empty(); }
};
enum CalcFunction : std::uint8_t { kCalcMin, kCalcMax, kCalcRand, kCalcSkill, kCalcMissile, kCalcStat, kCalcSkillLevel };

// What names mean when compiling: skillcalc.txt's operand names in order,
// skill names (Skills.txt `skill`) and stat names (ItemStatCost `Stat`);
// inside miss(), Missiles.txt rows (by row) and misscalc.txt's operands.
struct CalcNames {
    std::vector<std::string> operands, miss_operands;
    std::unordered_map<std::string, int> skills, stats, missiles;
};

// Compiles one calc cell the way game.exe's FUN_006c1ae0 does (skills.md
// "The calc-text compiler"): a shunting-yard over FUN_006c11c0's tokens.
// - Whitespace and '"' are skipped; a character it doesn't know (or `=`,
Calc compile_calc(std::string_view src, const CalcNames& names, std::string* error = nullptr);

// ---- skill functions

// Skills.txt srvstfunc: the index into game.exe's server start function
// table (0x732140, index 0 empty), run when the skill starts (FUN_0056f640).
// Named for what the function does; the ones d2d reads (the column holds
// others, 1..65).
enum class ServerStartFunction : int {
    kNone = 0,
    kCheckAmmo = 4,                // FUN_005da8b0: the bow and javelin skills, only the ammo checked
    kJab = 5,
    kElementalStrike = 6,          // FUN_005da940: Power Strike, Charged Strike; no to-hit bonus, the element
    kImpale = 7,
    kStrafe = 8,                   // FUN_005dacd0
    kFend = 9,                     // FUN_005dae30
    kLightningStrike = 10,
    kCheckMana = 11,               // FUN_005c8fa0: Inferno, Arctic Blast
    kThunderStorm = 13,
    kRaiseSkeleton = 15,           // FUN_005c3070: a corpse rules::raise_takes passes
    kPoisonDagger = 16,            // FUN_005c30a0
    kCorpseBlast = 17,             // FUN_005c31c0: Corpse / Poison Explosion, a corpse rules::blast_takes passes
    kRevive = 21,                  // FUN_005c3350: a corpse rules::revive_takes passes
    kChargeUp = 23,                // the Assassin's charge-ups (FUN_005d3490 hits)
    kDragonTalon = 24,             // FUN_005d5970: calc1 kicks
    kDragonClaw = 25,
    kBladeFury = 26,
    kDragonTail = 27,
    kBladeShield = 28,
    kSacrifice = 29,               // FUN_005ce790
    kCharge = 31,                  // FUN_005cf6b0
    kBuildHit = 32,                // FUN_005d7ea0: the Bash family's hit record (calc1 ED, calc2 added, calc4 conversion)
    kTargetCorpse = 33,            // Find Potion, Grim Ward
    kVengeance = 35,               // FUN_005cfe10
    kHolyShield = 36,
    kRepeatedHit = 37,             // FUN_005daf40: Zeal, Fury; calc1 hits stored
    kWhirlwind = 38,               // FUN_005d8f50
    kBerserk = 39,                 // FUN_005d97f0
    kLeap = 40,                    // FUN_005d9d50: a monster hits its target, then leaps past it (twice its spot less its own)
    kLeapAttack = 41,              // FUN_005da540
    kFireHit = 42,                 // FUN_005cae40: a monster's hit with its S1 damage (FUN_005a4f50 mode 8)
    kMaggotUp = 44,                // FUN_005cb170: up at the nearest free spot (FUN_00554ea0), flags 0xe back
    kMaggotDown = 45,              // FUN_005cb270: flags 0xe off, its 0x1000 lifted
    kSubmerge = 51,                // FUN_005cc1d0: flags 0xe off (a tentacle, a swamp dweller under)
    kEmerge = 52,                  // FUN_005cc220: flags 0xe back
    kStackingStrike = 56,          // Feral Rage, Maul
    kRabies = 57,
    kFireClaws = 58,               // FUN_005c7e00
    kImpInferno = 59,              // FUN_005d1280: the flames' end calc1 frames on (at least 1), state 0xc on
};

// Skills.txt srvdofunc (and srvprgfunc1..3, which index the same table):
// the index into game.exe's server do function table (0x7322b0), run on
// the skill's action frame (FUN_0056f7f0). Named for what the function
// does; the ones d2d reads (the column holds others, up to 152).
enum class ServerDoFunction : int {
    kNone = 0,
    kAttack = 1,                   // FUN_0056f070: the plain attack
    kResolveHit = 2,               // FUN_0056f1f0: the skill's states, then the record its start built
    kThrow = 3,                    // FUN_0056f460: the throwable in hand flies as its missile
    kStateAroundCaster = 6,        // FUN_005db1c0: Inner Sight, Slow Missiles
    kJab = 7,                      // FUN_005db2d0
    kMissileFan = 8,               // FUN_005db410: Multiple Shot, Teeth, Shock Wave
    kFrenzy = 9,                   // FUN_005d8e00
    kGuidedMissile = 10,           // Guided Arrow, Bone Spirit
    kChargedStrike = 11,           // FUN_005db850
    kStrafe = 12,                  // FUN_005dba40
    kRepeatedHit = 13,             // FUN_005dbc60: Zeal, Fend, Fury; a hit a frame, then another target
    kLightningStrike = 14,
    kDecoy = 15,                   // FUN_005dc000
    kValkyrie = 16,
    kScatteredBolts = 17,          // FUN_005c9300: Charged Bolt and the monsters' bolts
    kSelfState = 18,               // FUN_005c9480: the armors, Holy Shield, Burst of Speed, Fade, Venom
    kBreath = 19,                  // FUN_005c8ca0: Inferno, Arctic Blast
    kStaticField = 20,
    kTelekinesis = 21,
    kNova = 22,                    // FUN_005c9b50: the novas, Howl
    kSelfStateWithMissile = 23,    // FUN_005c9c10: Blaze, Energy Shield, SpiderLay
    kMissileWall = 24,             // FUN_005c9ea0: the fire walls
    kEnchant = 25,
    kChainLightning = 26,
    kTeleport = 27,
    kMissileAtTarget = 28,         // Meteor, Blizzard, Eruption, the catapults
    kThunderStorm = 29,
    kCurse = 30,                   // FUN_005c37c0: the Necromancer's curses
    kRaiseSkeleton = 31,           // Raise Skeleton, Raise Skeletal Mage
    kPoisonDagger = 32,            // FUN_005c4cd0
    kPsychicHammer = 33,           // FUN_005d3140
    kChargeUp = 34,                // FUN_005d3490: Tiger Strike, Cobra Strike, Royal Strike
    kElementalChargeUp = 35,       // FUN_005d35d0: Fists of Fire, Claws of Thunder, Blades of Ice
    kReleaseNova = 36,             // FUN_005d4db0: a nova of the count's missile
    kReleaseMissileAtUnits = 37,   // FUN_005d4e70 -> FUN_005d4150: the count's missile on the 64-point ring
    kReleaseAreaHit = 38,          // FUN_005d3e80: the skill's damage on everything in reach
    kReleaseScatter = 39,          // FUN_005d3f90: missiles at random points in reach
    kReleaseAtTarget = 40,         // FUN_005d5010: the count's missile standing at the target
    kReleaseSpread = 41,           // FUN_005d5080: missiles toward random points round the target
    kDragonTalon = 42,             // FUN_005d5880 kicks
    kShockField = 43,              // FUN_005d5d70
    kBladeSentinel = 44,           // FUN_005d6020
    kTrap = 45,                    // FUN_005d6170: the Assassin's sentries
    kDragonClaw = 46,              // FUN_005d6340
    kCloakOfShadows = 47,
    kBladeFury = 48,
    kMirrorImage = 49,             // FUN_005d6e70: Shadow Warrior, Shadow Master
    kDragonTail = 50,
    kMindBlast = 51,
    kDragonFlight = 52,
    kBladeShield = 54,             // FUN_005d7e10
    kCorpseExplosion = 55,
    kGolem = 56,                   // FUN_005c5100: Clay, Blood, Fire Golem
    kIronGolem = 57,
    kRevive = 58,                  // FUN_005c56c0
    kAttract = 59,                 // FUN_005c3b90
    kBoneWall = 60,                // FUN_005c58b0
    kConfuse = 61,                 // FUN_005c3f20
    kBonePrison = 62,              // FUN_005c5d00
    kPoisonExplosion = 63,
    kSacrifice = 64,               // FUN_005ce8e0
    kFriendlyAura = 65,            // FUN_005cf010: the Paladin's and the Druid spirits' friendly auras
    kEnemyAura = 66,               // FUN_005cf3a0: Holy Fire, Holy Shock, Sanctuary, Conviction
    kCharge = 67,                  // FUN_005cf900
    kWarCry = 68,                  // FUN_005d83e0: the Barbarian's shouts and cries
    kFindPotion = 69,              // FUN_005d81c0
    kDoubleSwing = 70,             // FUN_005d8470
    kTaunt = 71,                   // FUN_005d8570
    kFindItem = 72,                // FUN_005d8780
    kBlessedHammer = 73,           // FUN_005d0040
    kDoubleThrow = 74,             // FUN_005d88b0
    kGrimWard = 75,
    kWhirlwind = 76,               // FUN_005d9580
    kLeap = 77,
    kLeapAttack = 78,              // FUN_005da7e0
    kConversion = 79,
    kFistOfTheHeavens = 80,        // FUN_005d0670
    kHolyFreeze = 81,              // FUN_005d0920
    kRedemption = 82,
    kHatch = 84,                   // FUN_005cafa0: calc1 of its MonStats spawn about it, then it dies (MaggotEgg)
    kTransMissile = 85,            // FUN_005cb0c0: srvmissilea's row + the monster's TransLvl, once (ShamanFire, UnHolyBolt)
    kMaggotDown = 86,              // FUN_005cb300: burrowed, it heals calc1 % of its life
    kMaggotLay = 87,               // FUN_005cb3c0: its MonStats spawn two subtiles toward the target
    kMonsterBreath = 95,           // FUN_005cc4e0: FetishInferno, the inferno sentries
    kNest = 91,                    // FUN_005cbe00: its young at the spot its start found (the vile mothers')
    kBestow = 96,                  // FUN_005cc840: heals the one it's at calc1..calc2 % of its life
    kMonTeleport = 98,             // FUN_005ccc80: to the free spot nearest its skill's point
    kMonPoisonNova = 99,              // FUN_005ccd10: srvmissilea on 8 ways, 16 with calc2 2
    kDiabCold = 100,               // FUN_005cce80: one hit of the skill's damage at the unit
    kFingerMageSpider = 101,       // FUN_005ccfa0: one srvmissilea at the target
    kDiabWall = 102,               // FUN_005cd1c0: calc1 diabwallmakers round the target's side
    kDiabRun = 103,                // FUN_005cd380: the run at the target, a hit on contact
    kDiabPrison = 104,             // FUN_005cd5d0: a bone prison round the unit
    kRegurgitatorEat = 108,        // FUN_005cdc10: a corpse eaten, life back
    kDesertTurret = 105,           // FUN_005cd6a0: calc1 srvmissilea fanned at the skill's point
    kArcaneTower = 106,            // FUN_005cd870: the nova of its srvmissilea (FUN_0056d400)
    kMonFrenzy = 109,              // FUN_005cdf10: a strike (FUN_005cddd0), the frenzy state on
    kMonCurseCast = 112,           // FUN_005ce2b0: one of five curses (0x6e3228) on rand(5)
    kBookOrScroll = 113,           // Identify, Town Portal
    kRaven = 114,
    kVine = 115,                   // FUN_005c6a80: Plague Poppy, Cycle of Life, Vines
    kShapeShift = 116,             // FUN_005c6ec0: Werewolf, Werebear
    kFirestorm = 117,              // FUN_005c7160
    kWindMissile = 118,            // FUN_005c72f0: Twister, Tornado
    kDruidSummon = 119,            // the spirits, wolves and Grizzly
    kStackingStrike = 120,         // Feral Rage, Maul
    kRabies = 121,
    kHunger = 122,                 // FUN_005c7f10
    kVolcano = 123,                // FUN_005c8080
    kStormAroundCaster = 124,      // FUN_005c8190: Armageddon, Hurricane
    kWakeOfDestruction = 125,
    kImpInferno = 126,             // FUN_005d1350: a flame at the target every 3 frames till the start's end
    kCryHelp = 128,                // FUN_005d1800: the caster's group at the target for calc1 frames
    kImpTeleport = 129,            // FUN_005d1ab0: onto its mount, off it, else as MonTeleport
    kOverseerWhip = 131,           // FUN_005d1f70: bloodlust on the minion, or it turns suicide minion
    kImpFireMissile = 132,         // FUN_005d2090: srvmissilea + TransLvl at the target, once
    kImpregnate = 133,             // FUN_005d2250: state 0x6e on a monster; dead, its summon comes out
    kSiegeBeastStomp = 134,        // FUN_005d2320: needs a srvmissilea, row 301 has none: nothing
    kMinionSpawner = 135,          // FUN_005d2490: what its start picked (FUN_0056e620), made through FUN_005b2f20
    kDeathMaul = 136,              // FUN_005d25b0: srvmissilea at the target's spot, its range by the distance
    kFenrisRage = 137,             // FUN_005d26f0: a corpse used up, the aurastate on the caster
    kBaalColdMissiles = 139,       // FUN_005d2940: srvmissilea (a maker) at the target's spot
    kBaalTentacle = 140,           // FUN_005d2c20: 2 + rand(3) + difficulty tentacles about the target
    kBaalCorpseExplode = 141,      // FUN_005d2e80: the corpses about the caster spent (baal_corpse_spent)
    kReleaseMissileAtUnitsSecondScan = 143,   // FUN_005d4f40 -> FUN_005d4870: the same ring, hops only on missile 0x238
    kHydra = 144,                  // FUN_005ca910
    kDoomKnightMissile = 148,      // FUN_005cdfb0: srvmissilea (+ the knight's variant) at the target
    kVariantMissile = 149,         // FUN_005ce0b0: srvmissilea with the caster's variant 1..4 (necromage1..4)
    kSmite = 150,                  // FUN_005ce9f0
    kDiabLight = 152,              // FUN_005cc690: diablight at the target every calc3 frames while state 0xc's on
};

// The 64 subtile offsets embedded in FUN_005d4150 and FUN_005d4870 (the same
// list in both). A release walks it by `step` entries, byte offset under 0x100.
inline constexpr std::array<std::pair<int, int>, 64> kReleaseRing{{
    {30, 0}, {29, 2}, {29, 5}, {28, 8}, {27, 11}, {26, 14}, {24, 16}, {23, 19},
    {21, 21}, {19, 23}, {16, 24}, {14, 26}, {11, 27}, {8, 28}, {5, 29}, {2, 29},
    {0, 30}, {-2, 29}, {-5, 29}, {-8, 28}, {-11, 27}, {-14, 26}, {-16, 24}, {-19, 23},
    {-21, 21}, {-23, 19}, {-24, 16}, {-26, 14}, {-27, 11}, {-28, 8}, {-29, 5}, {-29, 2},
    {-30, 0}, {-29, -2}, {-29, -5}, {-28, -8}, {-27, -11}, {-26, -14}, {-24, -16}, {-23, -19},
    {-21, -21}, {-19, -23}, {-16, -24}, {-14, -26}, {-11, -27}, {-8, -28}, {-5, -29}, {-2, -29},
    {0, -30}, {2, -29}, {5, -29}, {8, -28}, {11, -27}, {14, -26}, {16, -24}, {19, -23},
    {21, -21}, {23, -19}, {24, -16}, {26, -14}, {27, -11}, {28, -8}, {29, -5}, {29, -2},
}};
// FUN_004efc20(0x118) is that skill's Param2 (+0x14c). FUN_005d4150 stores it
// plus 1 on every missile. FUN_005d4870 stores it only when the missile row is 0x238.
inline constexpr int kReleaseHopSkill = 0x118;
inline constexpr int kReleaseHopMissile = 0x238;
int release_ring_hops(int param2, int missile_id, bool second_scan);
// `step` is prgcalc, or aurarange when that is 0. A step under 1 would not advance.
template <class Visit>
void visit_release_ring(int step, Visit&& visit) {
    if (step < 1) step = 1;
    for (int byte = 0; byte < 0x100; byte += step * 4) visit(kReleaseRing[std::size_t(byte / 4)]);
}

// ---- skill rows

// A Skills.txt row: the columns game.exe's skill records hold (0x23c-byte
// records at [0x744304]+0xb98; offsets in docs/research/re/skills.md).
struct Skill {
    int id = -1;
    std::string name, cls, desc;           // skill, charclass ("ama".. or ""), skilldesc
    ServerStartFunction srvstfunc = ServerStartFunction::kNone;   // srvstfunc: the server start function
    ServerDoFunction srvdofunc = ServerDoFunction::kNone;         // srvdofunc: the server do function
    std::string anim, range;               // anim (A1, SC, KK, ...), range (h2h, rng, both, none)
    std::string itypea1;                   // +0x18: the first weapon type it takes ("" none)
    // The weapon-type gate's two sets (FUN_00643f80): itypea1..3 / etypea1..2 (taken / excluded), itypeb1..3 / etypeb1..2.
    std::array<std::string, 3> itype_a, itype_b;
    std::array<std::string, 2> etype_a, etype_b;
    std::string monanim;                   // +0x11: a monster's mode for it (FUN_00647110; xx: SQ)
    int aitype = 0, aibonus = 0;           // +0x230 / +0x232: how the Shadows' AIs score it (pet-ai.md)
    int attack_rank = 0;                   // attackrank (+0x18e): Diablo's target score weighs it (FUN_005e8530)
    bool progressive = false;              // flags +4 bit 2: a charge-up
    Calc delay;                            // +0x190: frames before it may be used again
    bool leftskill = false, passive = false, aura = false, use_attack_rate = false, in_town = false;
    bool decquant = false;                 // decquant: a shot spends one ammo (FUN_0056f7f0 -> FUN_0056c3f0)
    bool noammo = false;                   // noammo (flags bit 16, the field table built at 0x616969): a bow needs no quiver (FUN_00643d90)
    bool attack_no_mana = false;
    // restrict (+0x228) and State1..3 (+0x22a, FUN_00644060): 0 not in a restrict state (wolf, bear,
    // delerium), 2 in one and in one of the listed states, any other value anywhere.
    int restrict_form = 0;
    std::array<std::string, 3> restrict_states;
    int reqlevel = 1, maxlvl = 20;
    int mana = 0, lvlmana = 0, manashift = 8, minmana = 0;
    int startmana = 0;                     // +0x184: the mana a start needs, whole points (FUN_006440f0)
    int tohit = 0, levtohit = 0;
    Calc tohit_calc;
    std::array<Calc, 4> calc;              // calc1..4
    std::array<int, 8> par{};              // Param1..8
    int hitshift = 8, srcdam = 128;
    int hit_class = 0;                     // HitClass (+0x134): OR-ed into the hit's (its element's impact layer)
    // Its sounds (Sounds.txt rows, 0 none; docs/research/re/unit-sounds.md): stsound (+0xfc),
    // stsoundclass (+0xfe, the class's own), ItemCastSound (+0x122, cast from an item), dosound
    // (+0x100), tgtsound (+0x10a); flags stsounddelay (13: the swing's frames less 3) and weaponsnd (14).
    int st_sound = 0, st_sound_class = 0, item_cast_sound = 0, do_sound = 0, tgt_sound = 0;
    int prg_sound = 0, clt_st_func = 0;   // prgsound (+0x10e), cltstfunc (+0xf2: the client's start, table 0x727a90)
    // dosound a / b (+0x102 / +0x104) and cltdofunc (+0xf4: the client's do, table 0x727ba8).
    // Only cltdofunc 16 (Jab's, FUN_004f4590) and cltstfunc 25 (FUN_004c9b40) read a / b.
    int do_sound_a = 0, do_sound_b = 0, clt_do_func = 0;
    std::array<int, 3> clt_prg_func{};     // cltprgfunc1..3 (+0xf6: the client's charge 1..3, table 0x727ba8)
    bool st_success_only = false, st_sound_delay = false, weapon_sound = false;
    bool interrupt = false;                // Skills.txt `interrupt` (the row's flags byte +7 against the mask at 0x6ce284): 0, a hit never puts the user in a get-hit (FUN_0057eec0)
    int srcdam_raw = 0;                    // SrcDam as written: a missile's weapon share (FUN_0064b860), 0 none
    std::string srvmissile;                // +0x46: the Missiles.txt row the skill fires
    std::string srvmissilea;               // +0x48: the row its srvdofunc fires (FUN_005d3cf0)
    int perdelay = 0;                      // an aura's pulse, ticks
    Calc perdelay_calc;                    // perdelay as a calc (Blade Shield's par3: a periodic skill's pace)
    std::string summon, pettype;           // +0xbc the MonStats row it raises, +0xbe its pet group
    std::string summode;                   // +0xbf the mode its summon comes in
    Calc petmax;                           // +0xc0: how many of the group at once
    int sumumod = 0;                       // +0xe4: the MonUMod its summon gets (FUN_005c4470)
    bool target_ally = false;             // TargetAlly (flag bit 0x1a, record +7 & 4): FUN_00554d20
    bool target_corpse = false;            // TargetCorpse: raised from a corpse
    bool target_pet = false;               // TargetPet (flag bit 0x19, record +7 & 2): FUN_005542c0
    bool targetable_only = false;          // TargetableOnly (flag bit 0x14, record +6 & 0x10)
    bool search_enemy_xy = false;          // SearchEnemyXY (flag bit 0x15, record +6 & 0x20): a shift-click's pick, FUN_00467660
    bool search_enemy_near = false;        // SearchEnemyNear (flag bit 0x16, record +6 & 0x40): the unit nearest the click, FUN_00467490
    bool search_open_xy = false;           // SearchOpenXY (flag bit 0x17, record +6 & 0x80): off a wall to a free spot, FUN_004677b0
    std::array<std::string, 5> sumskill;   // +0xc4: the pet's skills
    std::array<Calc, 5> sumsk_calc;        // +0xd0: their levels
    int result_flags = 0;                  // ResultFlags (8: knockback)
    int etype = -1;                        // 0 fire, 1 lightning, 2 cold, 3 poison, 4 magic, 5 stun
    int emin = 0, emax = 0;
    std::array<int, 5> emin_lev{}, emax_lev{};
    int elen = 0;
    std::array<int, 3> elen_lev{};
    Calc edmg_sym, elen_sym;               // EDmgSymPerCalc, ELenSymPerCalc: percent bonuses
    int mindam = 0, maxdam = 0;
    std::array<int, 5> mindam_lev{}, maxdam_lev{};
    Calc dmg_sym;                          // DmgSymPerCalc
    std::array<int, 5> passive_stat{ -1, -1, -1, -1, -1 };   // +0x98
    std::array<Calc, 5> passive_calc;      // +0xa4
    std::string passive_itype;             // +0x96: the stats' layer, the weapon type they need ("" any)
    std::array<Calc, 6> aura_calc;         // aurastatcalc1..6
    std::array<int, 6> aurastat{ -1, -1, -1, -1, -1, -1 };   // aurastat1..6 (ItemStatCost ids)
    Calc auralen, aurarange;               // auralencalc (+0x60, ticks), aurarangecalc (+0x64, subtiles)
    std::uint32_t aura_filter = 0;         // aurafilter (+0x50): the units an aura takes (aura_filter.hpp)
    std::string aurastate, auratarget;     // aurastate (+0x80), auratargetstate (+0x82): States.txt names
    // auraevent1..3 (Events.txt names) and auraeventfunc1..3: the unit events FUN_0056e740 hooks onto the summon
    // or the state's wearer, each with its function, slot n of the table at 0x7325b0 (23: Blood Golem's life steal).
    std::array<std::string, 3> aura_event;
    std::array<int, 3> aura_event_func{};
    std::string srvoverlay;                // srvoverlay (+0x4e): an Overlay.txt name, played once on its unit (FUN_00621e40)
    // castoverlay (+0x106) and ItemCastOverlay (+0x124, cast from an item): Overlay.txt names the
    // client's skill start (FUN_004c6140) plays once on its caster (rules::cast_overlay_starts).
    std::string cast_overlay, item_cast_overlay;
    int prgdam = 0;
    std::string seqtrans;                  // +0x12: the mode a sequence counts as (SC: FUN_006216e0 times it as a cast)
    int seqnum = 0;                        // +0x13: anim SQ's sequence (sequences.hpp)                        // +0x44: what a charge-up's charges add to the releasing hit
    std::array<ServerDoFunction, 3> prgfunc{};   // srvprgfunc1..3 (+0x30): srvdofunc slots run on release
    std::array<Calc, 3> prgcalc;           // prgcalc1..3 (+0x38..): by the charges held (FUN_005d3da0)
    bool prgstack = false;                 // a release runs srvprgfunc 1..n, not just n (FUN_005d5220)
    std::string srvmissileb, srvmissilec;  // +0x4a / +0x4c: 2 / 3 charges' missile (FUN_005d3cf0)
    int page = 0;                          // SkillDesc SkillPage: 1..3 its class's tabs
    int list_row = 0;                      // SkillDesc ListRow: the picker row (0..4, general at 0)
    int list_pos = 0;                      // SkillDesc ListPool: dup-suppression group within a row
    int icon = 0;                          // SkillDesc IconCel
    std::string str_name;                  // SkillDesc "str name"
    std::string str_alt;                   // SkillDesc "str alt": the char panel's attack block name
    int descdam = 0, descatt = 0;          // SkillDesc: the char panel's damage / attack rating kind
    Calc ddam_calc1, ddam_calc2;           // SkillDesc "ddam calc1" (damage %) / "ddam calc2" (flat)
    std::string str_long, str_short, str_mana;   // SkillDesc "str long" / "str short" / "str mana": the tooltips' texts
    std::array<std::string, 3> desc_missile;     // SkillDesc descmissile1..3 (+0x3c): the rows its tooltip shows (calcs' m1en..)
    // SkillDesc's tooltip lines as game.exe holds them (the record from
    // +0x42): descline1..6 (0..5), dsc2line1..4 (6..9), dsc3line1..7
    // (10..16); func picks the format (FUN_004edea0), texts are tbl keys.
    struct DescLine { int func = 0; std::string text_a, text_b; Calc calc_a, calc_b; };
    std::array<DescLine, 17> desc_lines{};
};

// The two stsuccessonly client starts used by stock Skills.txt. Hydra's
// FUN_004e39a0 needs both the caster's room and a room at the selected point;
// unless InTown is set, that point must not be in town. Charge's
// FUN_004c9b40 needs the unit's selected skill record and dynamic path, and
// rejects the local player's already-selected flagged record. Its later
// target, sound, path and stat-list work cannot change the successful status.
struct ClientStartContext {
    bool caster_has_room = true;
    bool target_has_room = true;
    bool target_in_town = false;
    bool skill_in_town = false;
    bool has_skill_record = true;
    bool has_dynamic_path = true;
    bool same_local_flagged_skill = false;
};

inline constexpr bool client_start_succeeds(int clt_st_func, const ClientStartContext& context) {
    if (clt_st_func == 17)
        return context.has_skill_record && context.caster_has_room && context.target_has_room && (context.skill_in_town || !context.target_in_town);
    if (clt_st_func == 25)
        return context.has_skill_record && context.has_dynamic_path && !context.same_local_flagged_skill;
    return true;
}

// Charge's FUN_005cf6b0 reads stat 0x43 after RN's FUN_00620e80 has added
// RunVelocity * 100 / WalkVelocity - 100 and chill has added -50. The
// result is the path velocity in 8.8 units.
int charge_velocity(int walk_velocity, int run_velocity, int velocity_percent, bool chilled, int param1);

struct SkillTables {
    std::vector<Skill> rows;               // by Id
    std::unordered_map<std::string, int> by_name;
    CalcNames names;
    // Each class's skill ids in Skills.txt order: how a save's 30 skill
    // bytes (Stats::skills) are indexed.
    std::array<std::vector<int>, 7> class_ids;
    [[nodiscard]] const Skill* get(int id) const {
        return id >= 0 && std::size_t(id) < rows.size() && rows[std::size_t(id)].id == id ? &rows[std::size_t(id)] : nullptr;
    }
};

// ---- per-level values

// The *Lev1..5 brackets (FUN_00644b70): Lev1 for each level 2..8, Lev2
// 9..16, Lev3 17..22, Lev4 23..28, Lev5 from 29.
int level_bonus(const std::array<int, 5>& lev, int lvl);
// ln (FUN_004e6ca0): a + b x (lvl - 1). dm (FUN_00645b20): a + (b - a) x
// (110 x lvl / (lvl + 6)) / 100, at most b.
inline int calc_ln(int base, int per_level, int lvl) { return lvl > 0 ? base + per_level * (lvl - 1) : 0; }
int calc_dm(int base, int per_level, int lvl);
// What a cast must have in mana, in 256ths (FUN_0056c160, the check):
// (mana + lvlmana x (lvl - 1)) << manashift. No minmana floor here.
int mana_formula(const Skill& skill, int lvl);
// The cost shown for a skill, and the most a payment takes: the formula, at
// least minmana points (FUN_0056bfe0).
int mana_cost(const Skill& skill, int lvl);
// What the payment takes off `mana` (FUN_0056bfe0): nothing for a row with
// no mana and no lvlmana, nothing when the mana is short of mana_cost (the
// cast has happened by then), else mana_cost.
int mana_paid(const Skill& skill, int lvl, std::int64_t mana);

// What calcs may ask of the unit using the skill.
struct CalcEnv {
    std::function<int(int skill)> base_level;   // blvl: points in the skill
    std::function<int(int skill)> level;        // lvl: with item bonuses
    std::function<int(int stat)> stat;          // stat('x'.accr)
    int clvl = 1;                               // ulvl
    Rng* rng = nullptr;                         // rand()
    // The SkillDesc missiles' operands (m1en..m3rn, m1eo..me3y; codes 26..37,
    // 43..48): game::skill_text fills it, Missiles.txt being GameData's.
    std::function<int(const Skill& skill, int code, int lvl)> desc_missile;
    // miss('row'.operand): a misscalc.txt operand of a Missiles.txt row at
    // the calc's level (FUN_00643740 -> FUN_0064b340), GameData's too.
    std::function<int(int skill, int missile, int code, int lvl)> missile;
};

int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth = 0);

// Elemental damage in 256ths (FUN_00644d50 / FUN_00644e40): (EMin + brackets)
// << HitShift, plus EDmgSymPerCalc percent of that (MulDiv); with `mastery`
// (the flag), plus the element's mastery % of the sum (FUN_00644c90: fire
// 329, lightning 330, cold 331, poison 332; env.stat).
int elem_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth = 0,
                bool mastery = false);
// Elemental length in ticks (FUN_00644f20): ELen + ELevLen1..3 over levels
// 2..8, 9..16, 17 up, plus ELenSymPerCalc percent.
int length_bonus(const std::array<int, 3>& lengths, int lvl);
int elem_length(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth = 0);
// The skill's own physical damage in 256ths (FUN_00647bc0 without the
// weapon share): (MinDam + brackets) plus DmgSymPerCalc percent, << HitShift.
int skill_phys(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth = 0);

// What a skill's missile carries (FUN_0064b860, the missile's Skill set):
// combat.hpp's MissileDamage.
// A row with no Skill carries its own element at the missile's level
// (FUN_0064b100 / 0064b1d0 / 0064b2a0): (EMin + MinELev brackets) <<
// HitShift, the same for the max, ELen + ELevLen brackets.
MissileDamage row_damage(int etype, int emin, int emax, const std::array<int, 5>& emin_lev,
                         const std::array<int, 5>& emax_lev, int hitshift, int elen,
                         const std::array<int, 3>& elen_lev, int lvl);
MissileDamage missile_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl);
// Attack rating bonus % (FUN_006449f0): ToHitCalc, else ToHit + LevToHit x (lvl - 1).
int skill_tohit(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth = 0);

// An aura's next pulse (FUN_0056cda0 / FUN_0056ce70 arming unit event 8 /
// 9, FUN_0056cd50 for the states a pulse puts): perdelay's calc at the
// skill's level, under 6 taken as 5, and the first game frame (+0xa8) after
// `frame` that is 1 more than a multiple of it. Event 8's handler
// (FUN_0056fcb0, table 0x6e2490) casts the aura and arms the next, so every
// aura of one perdelay pulses on the same frames; its start (FUN_0056fc50)
// casts at once.
[[nodiscard]] constexpr int aura_next_frame(int frame, int perdelay) {
    const int period = perdelay < 6 ? 5 : perdelay;
    return (frame - 1 + period) / period * period + 1;
}

// The char panel's attack block for the left / right skill (FUN_004ed570,
// by SkillDesc descdam / descatt). Colours are the panel's ids: 0 white,
// 1 red, 2 green, 3 blue, 9 yellow.
//   damage, descdam 1 / 7 (FUN_004ea010): the weapon's (FUN_004e86f0) with
//     ddam calc1 joining its % (at least -90) and ddam calc2 flat on both;
//     its elements (FUN_004e89a0), each colouring it (fire 1, lightning 9,
//     cold / magic 3, poison 2 last), then min >= 1, max >= min + 1; plus
//     the skill's own MinDam / EMin (with mastery) >> 8.
//   damage, descdam 5 (FUN_004ead60) and 24 (FUN_004eae70, Blade Fury): the
//     skill's own, coloured by its EType, poison x its length; SrcDam / 128
//     of the weapon's on top (FUN_004e9660), whose elements and damblue /
//     damred then colour it.
//   damage, descdam 9 (FUN_004eb390, Fire Wall): MinDam / MaxDam << HitShift
//     (no level brackets) plus EMin / EMax with mastery, x 75 >> 8, then
//     the weapon's share as 5.
//   damage, descdam 8 (FUN_004eb290, Inferno): EMin / EMax with mastery x
//     ddam calc1 x 25 / ddam calc2 >> 8, a calc of 0 taken as 1; EType's
//     colour, no weapon.
//   attack rating, descatt 1 / 5 (FUN_004e9040): (dex - 7) x 5 + ToHitFactor
//     + 19, x (1 + (119 + the skill's ToHit) %); descatt 2 (FUN_004e8ec0)
//     adds the weapon mastery (342) to the %. No floor at 1.
//   A state on the unit turns the weapon damage blue / red (damblue /
//     damred: FUN_004e86f0, then again after the elements in FUN_004e89a0)
//     and the attack rating (attblue / attred, FUN_004e9040 / FUN_004e8ec0);
//     red wins.
//   The panel draws the damage and the attack rating only when the usable
//     status (FUN_004d9fc0) is 0, 1, 6 or 8. `usable` defaults to 0, so a
//     caller that omits it still fills the line. The server's status is
//     FUN_00647960 (Fight::skill_status). FUN_004d9fc0 may turn a 0 into 8
//     for the client player's own cast delay, and both still draw.
// ponytail: descdam 2..4, 6, 10..23 and descatt 3 / 4 (kicks, throws,
// dual wield: the second value, FUN_004e93a0) show no line; stat 325 and
// barehanded strength % (FUN_004e86f0) are left out.
// The char panel's state colour bits (States.txt's flags, FUN_00618100:
// bit n's states listed at data tables +0xcc + 4n, read by FUN_0063a130):
// armblue 0x13 / armred 0x19, rfblue 0x14 / rfred 0x1a, rcblue 0x15 /
// rcred 0x1b, rlblue 0x16 / rlred 0x1c, rpblue 0x17 / rpred 0x1d, damblue
// 7 / damred 8, attblue 9 / attred 0xa.
enum PanelColour : std::uint8_t {
    kPanelDefense = 1, kPanelFire = 2, kPanelCold = 4, kPanelLightning = 8, kPanelPoison = 16, kPanelAttack = 32, kPanelDamage = 64,
};
struct AttackLine {
    int skill = -1;
    bool damage = false;
    int min = 0, max = 0, damage_colour = 0;
    int attack_rating = 0, ar_colour = 0;
};
AttackLine attack_line(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl,
                       const Fighter& fighter, int mastery_tohit, std::uint8_t blue = 0, std::uint8_t red = 0,
                       int usable = 0);

// One operand (FUN_00646460, codes in skillcalc.txt order).
// The missile operands (m1en.., 26..37, 43..48) go to env.desc_missile.
int calc_operand(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int code, int depth);

// ---- passives

// A passive skill's stat on the player (FUN_00646d60): passivestat k =
// passivecalc k at the skill's level with item bonuses, in the skill's
// passivestate, on the layer passiveitype. Refreshed when the level
// changes (FUN_00646f20 walks every skill that has a passivestate).
struct PassiveStat { int stat = -1, value = 0; std::string itype; };

// Every skill with passive stats the player has a level in (env.level >
// 0) but a summoner, its stats up to the first empty passivestat: the passives, and the
// Paladin's auras — theirs sit in the passivestate while the aura is off
// (the state at +0x80 holds FUN_00646d60 off) and in the aura's own state
// while it's on (FUN_005cf3a0 fills it from +0x98), so they're always on.
std::vector<PassiveStat> passive_stats(const SkillTables& skill_tables, const CalcEnv& env);

// ---- a summon's stats (FUN_005c4470: every summoning skill runs it on its pet)

// Its level (FUN_005c49e0): a `forced` one above 0 as it is (the Raven's and
// the Druid's max(calc2, 1)), else the owner's x 3 / 4 + `bonus` (the skill
// level: Raise, the golems, the Decoy, the Valkyrie), at least 1 and at most
// the owner's.
inline int summon_level(int clvl, int forced, int bonus) { return forced > 0 ? forced : std::min(std::max(clvl * 3 / 4 + bonus, 1), clvl); }

// Its passive stats (Skills +0x98, added to the pet's own: FUN_006272b0) and
// aurastats (+0x54, on its state node: FUN_00627150), each at the skill's
// level through the owner's calcs (Skeleton Mastery, Golem Mastery and the
// golems' synergies come in that way), summed by stat id.
struct SummonStats { std::map<int, int> passive, aura; };
SummonStats summon_stats(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl);

// Its max life in 256ths (0x5c4752..0x5c4821): the max with its passives in
// (FUN_00625d10) and calc1 % (`pct`) of it, set as stats 7 and 6; the percent
// is MulDiv (FUN_00483360) inlined, so its 32-bit product wraps as rules::mul_div
// says (bugs.md 123), and so does the sum.
int summon_life(int max_fine, int pct);

// Its skills (FUN_0056deb0): sumskill k at sumsk calc k, the ones at level 1
// or more, in order, as (skill id, level). A name is looked up as written,
// then ignoring case.
std::vector<std::pair<int, int>> summon_skill_levels(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl);

// Summon Resist on a raised skeleton or mage or a golem (FUN_005c40d0, after
// its stats; Raise's do 0x5c4c97, the golems' FUN_005c50c0, not Revive's): the
// owner's passive_summon_resist (stat 0x15d) on a state node of the pet as
// fire, lightning and cold resist, each unless the pet's absorb percent of
// that element (stats 0x8e, 0x90, 0x94) is above 0, and as poison resist;
// nothing when it's 0. As (stat id, value), in the order game.exe adds them.
inline constexpr int kPassiveSummonResist = 0x15d;
std::vector<std::pair<int, int>> summon_resist(int value, int absorb_fire, int absorb_light, int absorb_cold);

// Runs a calc for `skill` at `lvl` (the stack machine; divide by zero
// gives 0, as FUN_006c0bc0's op 0x13).
int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth);

// ---- Whirlwind

// Frames between Whirlwind's hits (FUN_005d9320) by the weapon's attack
// length in frames (FUN_0062a710): under 12 → 4, 15 → 6, 18 → 8, 20 → 10,
// 23 → 12, then 14, and 16 past 25; 10 bare-handed.
int whirlwind_gap(int attack_frames);

// ---- charge-ups (Assassin martial arts)

// What `n` charges of charge-up `s` (at level `lvl`) add to the hit that
// releases them (FUN_005d3ba0, by prgdam):
// 1 (Tiger Strike, FUN_005d3680): calc1 x n enhanced damage;
// 2 (Cobra Strike, FUN_005d3790): ln12 % steal: life at 1 charge, life and
//   mana at 2, both doubled at 3 (FUN_004e6ca0(ECX skill, EDX level) =
//   Param1 + Param2 x (level - 1), at 0x5d3834);
// 4 (Fists of Fire, Claws of Thunder, Blades of Ice, FUN_005d3970): the
//   skill's elemental damage (FUN_0056e0c0), which sets the record's cold
//   length (+0x30, FUN_0056c8e0 case 4); cold at 3 charges adds that length
//   / Param5 (+0x158, FUN_004e6c70) to the freeze length (+0x34: Blades of
//   Ice, Param5 1); calc1 % (FUN_00646ca0 at +0x138) of the physical, at
//   most all of it, moves to the element (Fists of Fire's lvl*3).
// prgdam 3 (FUN_005d3880) is no 1.14d skill's.
// Each new charge also adds its aurastat calc to the state's stat
// (FUN_005d3320 -> FUN_00627030): progressive_tohit (325, Tiger Strike's
// and Fists of Fire's par4) is the finisher's to-hit % (FUN_0056f070
// hands stat 0x145 to FUN_0057ec10).
struct ChargeBonus { int ar_pct = 0, ed_pct = 0, life_steal = 0, mana_steal = 0, etype = -1, elem_lo = 0, elem_hi = 0, elem_len = 0, freeze_len = 0, conv_pct = 0; };
ChargeBonus charge_bonus(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int charges);

// ---- a character's skill levels

// Item bonuses to one skill (FUN_00644180, a player's), over the item
// properties the character wears (ItemStatCost ids): 127 +all skills (any
// skill it has, its class's or not); for its own class's 83 +class skills
// (param = class), 188 +skill tab (param = class x 8 + tab, tab = SkillDesc
// page - 1) and 97 oskill at most 3, another class's 97 in full; 126
// +elemental skills (param = game.exe's EType: 1 fire, 2 lightning, 3
// magic, 4 cold, 5 poison) and 107 +single skill (param = skill id).
int item_skill_bonus(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props);

// Whether worn items give the character a skill it has no points in
// (FUN_0055b800, ItemMode.cpp: stat 97 for any skill, 107 for its own
// class's adds the skill at 0 points).
bool item_grants_skill(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props);

// A monster's random curse (FUN_005ce2b0, do MonCurseCast): seed % 5 picks
// 0x6e3228's skill (Amplify Damage, Weaken, Life Tap, Decrepify, Lower
// Resist), whose state and length it takes, but the stats are the function's
// own by the pick (jump table 0x5ce4dc), one curse along from the names after
// the first two (bugs.md #43): 0 damageresist -100, 1 damagepercent -50, 2 and
// 3 only aura events (Iron Maiden's, Life Tap's), 4 Decrepify's list, its
// velocity and attack rate the picked row's Param7 (+0x160; Lower Resist's 0).
inline constexpr std::array<int, 5> kMonsterCurses{ 66, 72, 82, 87, 91 };
std::vector<std::pair<int, int>> monster_curse_stats(int pick, int param7);

}  // namespace d2d::rules
