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
//   `!` alone) ends the expression there.
// - A name is looked up by its first four characters among the operands
//   (FUN_006119f0), so Bone Wall's calc2 "par34" reads par3; an unknown
//   name is 0. Directly inside skill() / sklvl() a name (quoted or not)
//   is first a skill, inside stat() a stat (then `base` 1, `mod` 2, else
//   0), as constants; `.name` is the same lookup but always a constant,
//   and an unknown one ends the expression.
// - Precedence is the in-stack table at 0x6fc874 against the incoming
//   op's byte: comparisons all one level, then + -, * /, ^ (power), unary
//   minus, then `?`, which only an operator after its third operand pops:
//   `lvl < 4 ? 1 : 2` is lvl < (4 ? 1 : 2); `:` does nothing; `,` only
//   flushes. `-` is unary unless an operand or a function's "(" came
//   last, where a "," or "?" doesn't count.
// - At the end, a "(" still open stops the program there (the VM ends on
//   its mark byte), so "1+(2" is 2; a function left open drops the rest.
// - Too few operands for an op, a ")" with no "(", or more than 64
//   pending ops: game.exe stores no calc (0xffffffff); here an empty
//   calc, which evaluates to 0, and `error` says why.
// Constant folding (literal-only text runs through the VM once) isn't
// done: it doesn't change a value. Checked against game.exe's own
// compiler in tools/emu on every Skills.txt / SkillDesc.txt calc and 4000
// random strings (skills.md).
// - Inside miss() (FUN_006119f0 function 4) a name is first a Missiles.txt
//   row, as a constant, else a misscalc.txt operand by its first four.
inline Calc compile_calc(std::string_view src, const CalcNames& names, std::string* error = nullptr) {
    Calc out;
    struct Pending { std::uint8_t kind; int value; };      // kind: 1 a function's "(" (value its tag), 2 a "(", else a Calc::Op
    std::vector<Pending> pending;
    int depth = 0;                                         // values on the VM stack so far
    bool operand_last = false, failed = false;
    auto fail = [&](std::string why) { if (!failed && error) *error = std::move(why); failed = true; };
    auto lower = [](std::string_view text) { std::string folded(text); for (auto& letter : folded) letter = char(std::tolower((unsigned char)letter)); return folded; };
    auto find_name = [&](const std::unordered_map<std::string, int>& table, std::string_view name) {
        if (const auto found = table.find(std::string(name)); found != table.end()) return found->second;
        for (const auto& [key, id] : table) if (lower(key) == lower(name)) return id;
        return -1;
    };
    auto arity = [](const Pending& entry) { return entry.kind == 1 ? (entry.value == kCalcSkillLevel ? 3 : 2) : entry.kind == Calc::Neg ? 1 : entry.kind == Calc::Cond ? 3 : 2; };
    auto emit_pending = [&](const Pending& entry, bool write = true) {
        if (depth < arity(entry)) { fail("an operator is missing an operand"); return; }
        depth += 1 - arity(entry);
        if (write) out.code.push_back(entry.kind == 1 ? Calc::Ins{ Calc::Call, entry.value } : Calc::Ins{ Calc::Op(entry.kind) });
    };
    auto value = [&](Calc::Op opcode, int operand) { out.code.push_back({ opcode, operand }); ++depth; operand_last = true; };
    // FUN_006c18a0: pop while the top's in-stack precedence (0x6fc874) is
    // at least the incoming op's byte, then push it (not "," or ":").
    auto push_op = [&](std::uint8_t incoming) {
        static constexpr std::array<std::uint8_t, 13> kStackPrecedence{ 15, 15, 15, 15, 15, 15, 17, 17, 19, 19, 20, 21, 22 };
        while (!failed && !pending.empty() && pending.back().kind >= Calc::Lt && kStackPrecedence[pending.back().kind - Calc::Lt] >= incoming) {
            emit_pending(pending.back());
            pending.pop_back();
        }
        if (incoming >= Calc::Lt && incoming <= Calc::Cond) {
            if (pending.size() >= 64) fail("too many pending operators");
            pending.push_back({ incoming, 0 });
        }
    };
    // FUN_006119f0: {id, is a constant} or {-1, _} when unknown, by the
    // function whose "(" is on top of the pending stack.
    auto lookup = [&](std::string_view name) -> std::pair<int, bool> {
        const int function = !pending.empty() && pending.back().kind == 1 ? pending.back().value : -1;
        if (function == kCalcSkill || function == kCalcSkillLevel) {
            if (const int skill = find_name(names.skills, name); skill >= 0) return { skill, true };
        } else if (function == kCalcMissile) {
            if (const int missile = find_name(names.missiles, name); missile >= 0) return { missile, true };
            const auto key = name.substr(0, 4);
            for (std::size_t code = 0; code < names.miss_operands.size(); ++code) if (!key.empty() && names.miss_operands[code] == key) return { int(code), false };
            return { -1, false };
        } else if (function == kCalcStat) {
            if (const int stat = find_name(names.stats, name); stat >= 0) return { stat, true };
            return { lower(name) == "base" ? 1 : lower(name) == "mod" ? 2 : 0, false };
        }
        const auto key = name.substr(0, 4);
        for (std::size_t code = 0; code < names.operands.size(); ++code) if (!key.empty() && names.operands[code] == key) return { int(code), false };
        return { -1, false };
    };
    auto is_name_char = [](char letter) { return std::isalnum((unsigned char)letter) != 0; };
    std::size_t offset = 0;
    while (!failed) {
        while (offset < src.size() && (std::isspace((unsigned char)src[offset]) || src[offset] == '"')) ++offset;
        if (offset >= src.size()) break;
        const char letter = src[offset];
        const char next = offset + 1 < src.size() ? src[offset + 1] : '\0';
        if (std::isdigit((unsigned char)letter)) {
            std::uint32_t number = 0;
            while (offset < src.size() && std::isdigit((unsigned char)src[offset])) number = number * 10 + std::uint32_t(src[offset++] - '0');
            value(Calc::Const, int(number));
        } else if (letter == '\'') {
            const auto end_quote = std::min(src.find('\'', offset + 1), src.size());
            const auto [id, constant] = lookup(src.substr(offset + 1, end_quote - offset - 1));
            offset = std::min(end_quote + 1, src.size());
            value(id < 0 || constant ? Calc::Const : Calc::Operand, std::max(id, 0));
        } else if (letter == '.') {
            std::size_t end = offset + 2;
            while (end < src.size() && is_name_char(src[end])) ++end;
            const int id = next == '\0' ? -1 : lookup(src.substr(offset + 1, std::min(end, src.size()) - offset - 1)).first;
            if (id < 0) break;
            offset = end;
            value(Calc::Const, id);
        } else if (std::isalpha((unsigned char)letter)) {
            std::size_t end = offset;
            while (end < src.size() && is_name_char(src[end])) ++end;
            const auto name = src.substr(offset, end - offset);
            offset = end;
            std::size_t paren = end;
            while (paren < src.size() && std::isspace((unsigned char)src[paren])) ++paren;
            static constexpr std::array<std::string_view, 7> kFunctions{ "min", "max", "rand", "skill", "miss", "stat", "sklvl" };   // LAB_00611930
            const auto function = std::ranges::find(kFunctions, lower(name));
            if (paren < src.size() && src[paren] == '(' && function != kFunctions.end()) {
                if (pending.size() >= 64) fail("too many pending operators");
                pending.push_back({ 1, int(function - kFunctions.begin()) });
                offset = paren + 1;
                operand_last = true;
            } else {
                const auto [id, constant] = lookup(name);
                value(id < 0 || constant ? Calc::Const : Calc::Operand, std::max(id, 0));
            }
        } else {
            ++offset;
            const bool two = next == '=';
            switch (letter) {
                case '(': if (pending.size() >= 64) fail("too many pending operators"); pending.push_back({ 2, 0 }); break;
                case ')':
                    while (!failed && !pending.empty() && pending.back().kind != 2 && pending.back().kind != 1) { emit_pending(pending.back()); pending.pop_back(); }
                    if (pending.empty()) { fail("a ) with no ("); break; }
                    if (pending.back().kind == 1) emit_pending(pending.back());
                    pending.pop_back();
                    break;
                case ',': push_op(3); break;
                case '?': push_op(Calc::Cond); break;
                case ':': break;
                case '+': push_op(Calc::Add); operand_last = false; break;
                case '-': push_op(operand_last ? Calc::Sub : Calc::Neg); operand_last = false; break;
                case '*': push_op(Calc::Mul); operand_last = false; break;
                case '/': push_op(Calc::Div); operand_last = false; break;
                case '^': push_op(Calc::Pow); operand_last = false; break;
                case '<': push_op(two ? Calc::Le : Calc::Lt); offset += two; operand_last = false; break;
                case '>': push_op(two ? Calc::Ge : Calc::Gt); offset += two; operand_last = false; break;
                case '=': case '!':
                    if (!two) { offset = src.size(); break; }
                    push_op(letter == '=' ? Calc::Eq : Calc::Ne); ++offset; operand_last = false; break;
                default: offset = src.size(); break;
            }
        }
    }
    // The end: pop down to a function left open. A "(" still open is
    // written as its mark byte, where the VM stops (so "1+(2" is 2), but
    // counts as a value for the ops under it, which are still checked.
    bool open_paren = false;
    while (!failed && !pending.empty() && pending.back().kind != 1) {
        if (pending.back().kind == 2) { open_paren = true; ++depth; }
        else emit_pending(pending.back(), !open_paren);
        pending.pop_back();
    }
    if (!failed && out.code.empty()) {
        if (open_paren) out.code.push_back({ Calc::Const, 0 });   // the VM's empty stack
        else fail("nothing to compute");
    }
    if (failed) out.code.clear();
    return out;
}

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
    kPoisonDagger = 16,            // FUN_005c30a0
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
    kReleaseMissileAtUnits = 37,   // FUN_005d4e70 -> FUN_005d4150: a missile at each unit in reach
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
    kBaalColdMissiles = 139,       // FUN_005d2940: srvmissilea (a maker) at the target's spot
    kBaalTentacle = 140,           // FUN_005d2c20: 2 + rand(3) + difficulty tentacles about the target
    kReleaseMissileAtUnitsSecondScan = 143,   // FUN_005d4f40 -> FUN_005d4870: 37 through the other scan
    kHydra = 144,                  // FUN_005ca910
    kDoomKnightMissile = 148,      // FUN_005cdfb0: srvmissilea (+ the knight's variant) at the target
    kVariantMissile = 149,         // FUN_005ce0b0: srvmissilea with the caster's variant 1..4 (necromage1..4)
    kSmite = 150,                  // FUN_005ce9f0
    kDiabLight = 152,              // FUN_005cc690: diablight at the target every calc3 frames while state 0xc's on
};

// ---- skill rows

// A Skills.txt row: the columns game.exe's skill records hold (0x23c-byte
// records at [0x744304]+0xb98; offsets in docs/research/re/skills.md).
struct Skill {
    int id = -1;
    std::string name, cls, desc;           // skill, charclass ("ama".. or ""), skilldesc
    ServerStartFunction srvstfunc = ServerStartFunction::kNone;   // srvstfunc: the server start function
    ServerDoFunction srvdofunc = ServerDoFunction::kNone;         // srvdofunc: the server do function
    std::string anim, range;               // anim (A1, SC, KK, ...), range (h2h, rng, both, none)
    std::string monanim;                   // +0x11: a monster's mode for it (FUN_00647110; xx: SQ)
    int aitype = 0, aibonus = 0;           // +0x230 / +0x232: how the Shadows' AIs score it (pet-ai.md)
    int attack_rank = 0;                   // attackrank (+0x18e): Diablo's target score weighs it (FUN_005e8530)
    bool progressive = false;              // flags +4 bit 2: a charge-up
    Calc delay;                            // +0x190: frames before it may be used again
    bool leftskill = false, passive = false, aura = false, use_attack_rate = false, in_town = false;
    bool decquant = false;                 // decquant: a shot spends one ammo (FUN_0056f7f0 -> FUN_0056c3f0)
    bool attack_no_mana = false;
    int reqlevel = 1, maxlvl = 20;
    int mana = 0, lvlmana = 0, manashift = 8, minmana = 0;
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
    bool st_sound_delay = false, weapon_sound = false;
    int srcdam_raw = 0;                    // SrcDam as written: a missile's weapon share (FUN_0064b860), 0 none
    std::string srvmissile;                // +0x46: the Missiles.txt row the skill fires
    std::string srvmissilea;               // +0x48: the row its srvdofunc fires (FUN_005d3cf0)
    int perdelay = 0;                      // an aura's pulse, ticks
    Calc perdelay_calc;                    // perdelay as a calc (Blade Shield's par3: a periodic skill's pace)
    std::string summon, pettype;           // +0xbc the MonStats row it raises, +0xbe its pet group
    std::string summode;                   // +0xbf the mode its summon comes in
    Calc petmax;                           // +0xc0: how many of the group at once
    bool target_corpse = false;            // TargetCorpse: raised from a corpse
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
    std::string aurastate, auratarget;     // aurastate (+0x80), auratargetstate (+0x82): States.txt names
    int prgdam = 0;
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
inline int level_bonus(const std::array<int, 5>& lev, int lvl) {
    if (lvl < 2) return 0;
    if (lvl > 28) return (lvl - 28) * lev[4] + (lev[3] + lev[2]) * 6 + lev[1] * 8 + lev[0] * 7;
    if (lvl > 22) return (lvl - 22) * lev[3] + lev[2] * 6 + lev[1] * 8 + lev[0] * 7;
    if (lvl > 16) return (lvl - 16) * lev[2] + lev[1] * 8 + lev[0] * 7;
    if (lvl > 8) return (lvl - 8) * lev[1] + lev[0] * 7;
    return (lvl - 1) * lev[0];
}
// ln (FUN_004e6ca0): a + b x (lvl - 1). dm (FUN_00645b20): a + (b - a) x
// (110 x lvl / (lvl + 6)) / 100, at most b.
inline int calc_ln(int base, int per_level, int lvl) { return lvl > 0 ? base + per_level * (lvl - 1) : 0; }
inline int calc_dm(int base, int per_level, int lvl) {
    if (lvl < 1) return 0;
    return std::min((110 * lvl / (lvl + 6)) * (per_level - base) / 100 + base, per_level);
}
// Mana cost in 256ths (FUN_0056c160): (mana + lvlmana x (lvl - 1)) << manashift,
// at least minmana points.
inline int mana_cost(const Skill& skill, int lvl) {
    if (lvl < 1) return 0;
    return std::max((skill.mana + skill.lvlmana * (lvl - 1)) << (skill.manashift & 31), skill.minmana * 256);
}

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

inline int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth = 0);

// Elemental damage in 256ths (FUN_00644d50 / FUN_00644e40): (EMin + brackets)
// << HitShift, plus EDmgSymPerCalc percent of that; with `mastery` (the
// flag), plus the element's mastery % of the sum (FUN_00644c90: fire 329,
// lightning 330, cold 331, poison 332; env.stat).
inline int elem_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth = 0,
                       bool mastery = false) {
    if (lvl < 1) return 0;
    int damage = ((max ? skill.emax : skill.emin) + level_bonus(max ? skill.emax_lev : skill.emin_lev, lvl)) << (skill.hitshift & 31);
    if (!skill.edmg_sym.empty()) damage += damage * eval_calc(skill_tables, skill.edmg_sym, env, skill.id, lvl, depth + 1) / 100;
    if (mastery && env.stat && skill.etype >= 0 && skill.etype <= 3) damage += int(std::int64_t(damage) * env.stat(d2d::d2s::kPassiveFireMastery + skill.etype) / 100);
    return damage;
}
// Elemental length in ticks (FUN_00644f20): ELen + ELevLen1..3 over levels
// 2..8, 9..16, 17 up, plus ELenSymPerCalc percent.
inline int length_bonus(const std::array<int, 3>& lengths, int lvl) {
    return lvl < 2 ? 0 : lvl < 9 ? (lvl - 1) * lengths[0] : lvl < 17 ? (lvl - 8) * lengths[1] + lengths[0] * 7
                      : (lvl - 16) * lengths[2] + lengths[1] * 8 + lengths[0] * 7;
}
inline int elem_length(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth = 0) {
    if (lvl < 1) return 0;
    int length = skill.elen + length_bonus(skill.elen_lev, lvl);
    if (!skill.elen_sym.empty()) length += length * eval_calc(skill_tables, skill.elen_sym, env, skill.id, lvl, depth + 1) / 100;
    return length;
}
// The skill's own physical damage in 256ths (FUN_00647bc0 without the
// weapon share): (MinDam + brackets) plus DmgSymPerCalc percent, << HitShift.
inline int skill_phys(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, bool max, int depth = 0) {
    if (lvl < 1) return 0;
    int damage = (max ? skill.maxdam : skill.mindam) + level_bonus(max ? skill.maxdam_lev : skill.mindam_lev, lvl);
    if (!skill.dmg_sym.empty()) damage += damage * eval_calc(skill_tables, skill.dmg_sym, env, skill.id, lvl, depth + 1) / 100;
    return damage << (skill.hitshift & 31);
}

// What a skill's missile carries (FUN_0064b860, the missile's Skill set):
// combat.hpp's MissileDamage.
// A row with no Skill carries its own element at the missile's level
// (FUN_0064b100 / 0064b1d0 / 0064b2a0): (EMin + MinELev brackets) <<
// HitShift, the same for the max, ELen + ELevLen brackets.
inline MissileDamage row_damage(int etype, int emin, int emax, const std::array<int, 5>& emin_lev,
                                const std::array<int, 5>& emax_lev, int hitshift, int elen,
                                const std::array<int, 3>& elen_lev, int lvl) {
    MissileDamage damage;
    if (etype < 0 || lvl < 1) return damage;
    damage.etype = etype;
    damage.elo = (emin + level_bonus(emin_lev, lvl)) << (hitshift & 31);
    damage.ehi = std::max((emax + level_bonus(emax_lev, lvl)) << (hitshift & 31), damage.elo);
    damage.elen = elen + length_bonus(elen_lev, lvl);
    return damage;
}
inline MissileDamage missile_damage(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl) {
    MissileDamage damage;
    damage.phys_lo = skill_phys(skill_tables, skill, env, lvl, false);
    damage.phys_hi = std::max(skill_phys(skill_tables, skill, env, lvl, true), damage.phys_lo);
    damage.etype = skill.etype;
    if (skill.etype >= 0) {
        damage.elo = elem_damage(skill_tables, skill, env, lvl, false, 0, true);
        damage.ehi = std::max(elem_damage(skill_tables, skill, env, lvl, true, 0, true), damage.elo);
        damage.elen = elem_length(skill_tables, skill, env, lvl);
    }
    damage.srcdam = skill.srcdam_raw;
    return damage;
}
// Attack rating bonus % (FUN_006449f0): ToHitCalc, else ToHit + LevToHit x (lvl - 1).
inline int skill_tohit(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int depth = 0) {
    if (lvl < 1) return 0;
    if (!skill.tohit_calc.empty()) return eval_calc(skill_tables, skill.tohit_calc, env, skill.id, lvl, depth + 1);
    return skill.tohit + skill.levtohit * (lvl - 1);
}

// The char panel's attack block for the left / right skill (FUN_004ed570,
// by SkillDesc descdam / descatt). Colours are the panel's ids: 0 white,
// 1 red, 2 green, 3 blue, 9 yellow.
//   damage, descdam 1 / 7 (FUN_004ea010): the weapon's (FUN_004e86f0) with
//     ddam calc1 joining its % (at least -90) and ddam calc2 flat on both;
//     its elements (FUN_004e89a0), each colouring it (fire 1, lightning 9,
//     cold / magic 3, poison 2 last), then min >= 1, max >= min + 1; plus
//     the skill's own MinDam / EMin (with mastery) >> 8.
//   damage, descdam 5 (FUN_004ead60): the skill's own, coloured by its
//     EType, poison x its length; SrcDam / 128 of the weapon's on top.
//   attack rating, descatt 1 / 5 (FUN_004e9040): (dex - 7) x 5 + ToHitFactor
//     + 19, x (1 + (119 + the skill's ToHit) %); descatt 2 (FUN_004e8ec0)
//     adds the weapon mastery (342) to the %. No floor at 1.
// ponytail: descdam 2..4, 6, 8..24 and descatt 3 / 4 (kicks, throws,
// dual wield: the second value, FUN_004e93a0) show no line; the states'
// red / blue (FUN_0063a380..) and stat 325, the usability check
// (FUN_004d9fc0) and barehanded strength % (FUN_004e86f0) are left out.
struct AttackLine {
    int skill = -1;
    bool damage = false;
    int min = 0, max = 0, damage_colour = 0;
    int attack_rating = 0, ar_colour = 0;
};
inline AttackLine attack_line(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl,
                              const Fighter& fighter, int mastery_tohit) {
    AttackLine line;
    line.skill = skill.id;
    constexpr int kElemColour[5] = { 1, 9, 3, 2, 3 };    // Fighter::elem order: fire, lightning, cold, poison, magic
    const auto calc_of = [&](const Calc& calc) { return calc.empty() ? 0 : eval_calc(skill_tables, calc, env, skill.id, lvl); };
    const auto weapon = [&](int pct, int flat) {
        const int total = std::max(fighter.phys_pct + pct, -90);
        line.min = int((std::int64_t(fighter.phys_lo) * (100 + total) / 100) >> 8) + flat;
        line.max = int((std::int64_t(fighter.phys_hi) * (100 + total) / 100) >> 8) + flat;
        for (std::size_t elem : { 0u, 1u, 2u, 4u, 3u }) {
            const auto& [low, high] = fighter.elem[elem];
            line.min += std::min(low, high); line.max += high;
            if (low != 0 || high != 0) line.damage_colour = kElemColour[elem];
        }
        if (line.max > 0) { line.min = std::max(line.min, 1); line.max = std::max(line.max, line.min + 1); }
    };
    const int poison_len = skill.etype == 3 ? std::max(elem_length(skill_tables, skill, env, lvl), 1) : 1;
    if (skill.descdam == 1 || skill.descdam == 7) {
        line.damage = true;
        weapon(calc_of(skill.ddam_calc1), calc_of(skill.ddam_calc2));
    } else if (skill.descdam == 5) {
        line.damage = true;
        if (skill.srcdam_raw != 0) {
            weapon(0, 0);
            line.min = line.min * skill.srcdam_raw / 128; line.max = line.max * skill.srcdam_raw / 128;
        }
        line.damage_colour = skill.etype >= 0 && skill.etype < 5 ? kElemColour[skill.etype] : 0;
    }
    if (line.damage) {
        line.min += skill_phys(skill_tables, skill, env, lvl, false) >> 8;
        line.max += skill_phys(skill_tables, skill, env, lvl, true) >> 8;
        line.min += elem_damage(skill_tables, skill, env, lvl, false, 0, true) * poison_len >> 8;
        line.max += elem_damage(skill_tables, skill, env, lvl, true, 0, true) * poison_len >> 8;
    }
    if (skill.descatt == 1 || skill.descatt == 2 || skill.descatt == 5) {
        const int pct = fighter.ar_pct - (skill.descatt == 2 ? 0 : mastery_tohit) + skill_tohit(skill_tables, skill, env, lvl);
        line.attack_rating = int(std::int64_t(fighter.ar_base) * pct / 100 + fighter.ar_base);
    }
    return line;
}

// One operand (FUN_00646460, codes in skillcalc.txt order).
// The missile operands (m1en.., 26..37, 43..48) go to env.desc_missile.
// ponytail: skpt (+0x170, the skill points calc) reads 0; no calc uses it.
inline int calc_operand(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int code, int depth) {
    const auto& params = skill.par;
    switch (code) {
        case 0: return calc_ln(params[0], params[1], lvl);  case 1: return calc_dm(params[0], params[1], lvl);
        case 2: return calc_ln(params[2], params[3], lvl);  case 3: return calc_dm(params[2], params[3], lvl);
        case 4: return calc_ln(params[4], params[5], lvl);  case 5: return calc_dm(params[4], params[5], lvl);
        case 6: return calc_ln(params[6], params[7], lvl);  case 7: return calc_dm(params[6], params[7], lvl);
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: return params[std::size_t(code - 8)];
        case 16: return lvl;
        case 17: return elem_damage(skill_tables, skill, env, lvl, false, depth) >> 8;          // edmn
        case 18: return elem_damage(skill_tables, skill, env, lvl, true, depth) >> 8;           // edmx
        case 19: return elem_length(skill_tables, skill, env, lvl, depth);                      // edln
        case 20: return skill_tohit(skill_tables, skill, env, lvl, depth);                      // toht
        case 21: return mana_cost(skill, lvl) >> 8;                                  // mana
        case 22: return lvl < 1 ? 0 : (((skill.mana + skill.lvlmana * (lvl - 1)) * 25 / 2) << (skill.manashift & 31)) >> 8;   // mps
        case 23: case 24: case 25: {                                             // math / madm / macr
            for (std::size_t k = 0; k < 5; ++k) {
                const int stat = skill.passive_stat[k], want = 0x156 + (code - 23);
                if (stat == want || stat == want + 3) return eval_calc(skill_tables, skill.passive_calc[k], env, skill.id, lvl, depth + 1);
            }
            return 0;
        }
        case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36: case 37:
        case 43: case 44: case 45: case 46: case 47: case 48:
            return env.desc_missile ? env.desc_missile(skill, code, lvl) : 0;
        case 49: return elem_damage(skill_tables, skill, env, lvl, false, depth, true) >> 8;    // enma (FUN_00644d50 flag 1)
        case 50: return elem_damage(skill_tables, skill, env, lvl, true, depth, true) >> 8;     // exma
        case 51: return elem_length(skill_tables, skill, env, lvl, depth);                      // edma (FUN_00644f20 flag 1)
        case 52: return elem_damage(skill_tables, skill, env, lvl, false, depth, true);         // enms
        case 53: return elem_damage(skill_tables, skill, env, lvl, true, depth, true);          // exms
        case 38: return elem_damage(skill_tables, skill, env, lvl, false, depth);               // edns
        case 39: return elem_damage(skill_tables, skill, env, lvl, true, depth);                // edxs
        case 40: return env.clvl;                                                // ulvl
        case 41: return env.base_level ? env.base_level(skill.id) : lvl;             // blvl
        case 42: return lvl < 1 ? 0 : (skill.mana + skill.lvlmana * (lvl - 1)) << (skill.manashift & 31);   // usmc
        case 54: return eval_calc(skill_tables, skill.auralen, env, skill.id, lvl, depth + 1);      // len
        case 59: return eval_calc(skill_tables, skill.aurarange, env, skill.id, lvl, depth + 1);    // rng
        case 71: return eval_calc(skill_tables, skill.petmax, env, skill.id, lvl, depth + 1);       // pets
        case 55: case 56: case 57: case 58: return eval_calc(skill_tables, skill.calc[std::size_t(code - 55)], env, skill.id, lvl, depth + 1);
        case 60: case 61: case 62: case 63: case 64: case 65:
            return eval_calc(skill_tables, skill.aura_calc[std::size_t(code - 60)], env, skill.id, lvl, depth + 1);
        case 66: case 67: case 68: case 69: case 70:
            return eval_calc(skill_tables, skill.passive_calc[std::size_t(code - 66)], env, skill.id, lvl, depth + 1);
        default: return 0;
    }
}

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
inline std::vector<PassiveStat> passive_stats(const SkillTables& skill_tables, const CalcEnv& env) {
    std::vector<PassiveStat> out;
    for (const auto& skill : skill_tables.rows) {
        if (skill.id < 0 || skill.passive_stat[0] < 0 || !env.level) continue;
        // A summoner's passive stats are its pet's (FUN_005c4470 puts
        // them on the pet: Raise Skeleton's maxhp from Skeleton Mastery,
        // the golems' speed, the spirits' resists), not the player's.
        // ponytail: FUN_00646f20's own skip isn't traced; none of these
        // has a passivestate, the field it walks by.
        if (!skill.summon.empty() || skill.srvdofunc == ServerDoFunction::kRevive) continue;
        const int lvl = env.level(skill.id);
        if (lvl < 1) continue;
        for (std::size_t k = 0; k < 5 && skill.passive_stat[k] >= 0; ++k)
            out.push_back({ skill.passive_stat[k], eval_calc(skill_tables, skill.passive_calc[k], env, skill.id, lvl, 0), skill.passive_itype });
    }
    return out;
}

// Runs a calc for `skill` at `lvl` (the stack machine; divide by zero
// gives 0, as FUN_006c0bc0's op 0x13).
inline int eval_calc(const SkillTables& skill_tables, const Calc& calc, const CalcEnv& env, int skill, int lvl, int depth) {
    const Skill* skill_row = skill_tables.get(skill);
    if (calc.empty() || !skill_row || depth > 8) return 0;
    std::vector<int> stack;
    stack.reserve(calc.code.size());
    auto pop = [&] { if (stack.empty()) return 0; const int value = stack.back(); stack.pop_back(); return value; };
    for (const auto& instruction : calc.code) {
        switch (instruction.opcode) {
            case Calc::Const: stack.push_back(instruction.operand1); break;
            case Calc::Operand: stack.push_back(calc_operand(skill_tables, *skill_row, env, lvl, instruction.operand1, depth)); break;
            case Calc::Call: {                         // the function table at 0x745774
                const int third = instruction.operand1 == kCalcSkillLevel ? pop() : 0;
                const int right = pop(), left = pop();
                switch (instruction.operand1) {
                    case kCalcMin: stack.push_back(std::min(left, right)); break;
                    case kCalcMax: stack.push_back(std::max(left, right)); break;
                    case kCalcRand: stack.push_back(env.rng ? env.rng->range(left, right) : left); break;
                    case kCalcSkill: {                 // skill(id, operand): that skill's operand at the unit's level in it
                        const Skill* other = skill_tables.get(left);
                        const int level = right == 41 ? (env.base_level ? env.base_level(left) : 0) : env.level ? env.level(left) : 0;
                        stack.push_back(other && right != 41 ? calc_operand(skill_tables, *other, env, level, right, depth + 1) : level);
                        break;
                    }
                    case kCalcStat: stack.push_back(env.stat ? env.stat(left) : 0); break;
                    case kCalcMissile: stack.push_back(env.missile ? env.missile(skill, left, right, lvl) : 0); break;
                    case kCalcSkillLevel: {            // sklvl(id, at, operand), FUN_00646c60: that skill's operand at this one's `at`
                        const Skill* other = skill_tables.get(left);
                        const int level = calc_operand(skill_tables, *skill_row, env, lvl, right, depth + 1);
                        stack.push_back(other ? calc_operand(skill_tables, *other, env, level, third, depth + 1) : 0);
                        break;
                    }
                    default: stack.push_back(0); break;
                }
                break;
            }
            case Calc::Neg: stack.push_back(int(0u - std::uint32_t(pop()))); break;
            case Calc::Cond: { const int if_false = pop(), if_true = pop(), condition = pop(); stack.push_back(condition ? if_true : if_false); break; }
            default: {
                const int right = pop(), left = pop();
                const auto left_bits = std::uint32_t(left), right_bits = std::uint32_t(right);
                switch (instruction.opcode) {
                    case Calc::Add: stack.push_back(int(left_bits + right_bits)); break;
                    case Calc::Sub: stack.push_back(int(left_bits - right_bits)); break;
                    case Calc::Mul: stack.push_back(int(left_bits * right_bits)); break;
                    case Calc::Div: stack.push_back(right ? left / right : 0); break;
                    case Calc::Pow: {                  // 1 for a power below 1, else repeated multiplies
                        std::uint32_t power = 1;
                        for (int step = 0; step < right; ++step) power *= left_bits;
                        stack.push_back(int(power));
                        break;
                    }
                    case Calc::Lt: stack.push_back(left < right); break;
                    case Calc::Gt: stack.push_back(left > right); break;
                    case Calc::Le: stack.push_back(left <= right); break;
                    case Calc::Ge: stack.push_back(left >= right); break;
                    case Calc::Eq: stack.push_back(left == right); break;
                    case Calc::Ne: stack.push_back(left != right); break;
                    default: stack.push_back(0); break;
                }
            }
        }
    }
    return stack.empty() ? 0 : stack.back();
}

// ---- Whirlwind

// Frames between Whirlwind's hits (FUN_005d9320) by the weapon's attack
// length in frames (FUN_0062a710): under 12 → 4, 15 → 6, 18 → 8, 20 → 10,
// 23 → 12, then 14, and 16 past 25; 10 bare-handed.
inline int whirlwind_gap(int attack_frames) {
    const int frames = attack_frames;
    return frames < 12 ? 4 : frames < 15 ? 6 : frames < 18 ? 8 : frames < 20 ? 10 : frames < 23 ? 12 : frames > 25 ? 16 : 14;
}

// ---- charge-ups (Assassin martial arts)

// What `n` charges of charge-up `s` (at level `lvl`) add to the hit that
// releases them (FUN_005d3ba0, by prgdam):
// 1 (Tiger Strike, FUN_005d3680): calc1 x n enhanced damage;
// 2 (Cobra Strike, FUN_005d3790): ln12 % steal: life at 1 charge, life and
//   mana at 2, both doubled at 3 (FUN_004e6ca0(ECX skill, EDX level) =
//   Param1 + Param2 x (level - 1), at 0x5d3834);
// 4 (Fists of Fire, Claws of Thunder, Blades of Ice, FUN_005d3970): the
//   skill's elemental damage (FUN_0056e0c0).
// ponytail: prgdam 4's third-charge freeze (cold length / an untraced
// divisor) and its calc1 physical-to-element share aren't applied.
struct ChargeBonus { int ed_pct = 0, life_steal = 0, mana_steal = 0, etype = -1, elem_lo = 0, elem_hi = 0, elem_len = 0; };
inline ChargeBonus charge_bonus(const SkillTables& skill_tables, const Skill& skill, const CalcEnv& env, int lvl, int charges) {
    ChargeBonus bonus;
    if (lvl < 1 || charges < 1) return bonus;
    charges = std::min(charges, 3);
    switch (skill.prgdam) {
        case 1: bonus.ed_pct = eval_calc(skill_tables, skill.calc[0], env, skill.id, lvl) * charges; break;
        case 2: {
            const int steal = calc_ln(skill.par[0], skill.par[1], lvl) * (charges == 3 ? 2 : 1);
            bonus.life_steal = steal;
            bonus.mana_steal = charges >= 2 ? steal : 0;
            break;
        }
        case 4:
            bonus.etype = skill.etype;
            bonus.elem_lo = elem_damage(skill_tables, skill, env, lvl, false) >> 8;
            bonus.elem_hi = elem_damage(skill_tables, skill, env, lvl, true) >> 8;
            bonus.elem_len = elem_length(skill_tables, skill, env, lvl);
            break;
        default: break;
    }
    return bonus;
}

// ---- a character's skill levels

// Item bonuses to one skill (FUN_00644180, a player's), over the item
// properties the character wears (ItemStatCost ids): 127 +all skills (any
// skill it has, its class's or not); for its own class's 83 +class skills
// (param = class), 188 +skill tab (param = class x 8 + tab, tab = SkillDesc
// page - 1) and 97 oskill at most 3, another class's 97 in full; 126
// +elemental skills (param = game.exe's EType: 1 fire, 2 lightning, 3
// magic, 4 cold, 5 poison) and 107 +single skill (param = skill id).
// ponytail: the skill's own +0x2c and FUN_00644150's share aren't kept.
inline int item_skill_bonus(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props) {
    static constexpr std::array<int, 5> kGameEType{ 1, 2, 4, 5, 3 };   // d2d's fire, lightning, cold, poison, magic
    const bool own = !skill.cls.empty() && skill.cls == kClassCode[std::size_t(cls)];
    const int etype = skill.etype >= 0 && skill.etype < 5 ? kGameEType[std::size_t(skill.etype)] : 0;
    int bonus = 0, oskill = 0;
    for (const auto& prop : props) {
        switch (prop.stat) {
            case 127: bonus += prop.value; break;
            case 83: if (own && prop.param == cls) bonus += prop.value; break;
            case 188: if (own && skill.page > 0 && prop.param == cls * 8 + skill.page - 1) bonus += prop.value; break;
            case 97: if (prop.param == skill.id) oskill += prop.value; break;
            case 126: if (etype != 0 && prop.param == etype) bonus += prop.value; break;
            case 107: if (prop.param == skill.id) bonus += prop.value; break;
            default: break;
        }
    }
    return bonus + (own ? std::min(oskill, 3) : oskill);
}

// Whether worn items give the character a skill it has no points in
// (FUN_0055b800, ItemMode.cpp: stat 97 for any skill, 107 for its own
// class's adds the skill at 0 points).
inline bool item_grants_skill(const Skill& skill, int cls, std::span<const d2d::d2s::ItemProp> props) {
    const bool own = !skill.cls.empty() && skill.cls == kClassCode[std::size_t(cls)];
    return std::ranges::any_of(props, [&](const d2d::d2s::ItemProp& prop) { return prop.param == skill.id && prop.value > 0 && (prop.stat == 97 || (prop.stat == 107 && own)); });
}

}  // namespace d2d::rules
