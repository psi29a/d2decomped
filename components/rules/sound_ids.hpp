// SPDX-License-Identifier: GPL-3.0-or-later
// Sounds.txt rows (Index) the code plays by number, by their Sound name;
// the rest are looked up by name (GameData::sound_index).
#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace d2d::rules::sound_ids {

inline constexpr int kCursorPass = 1;          // a menu choice or slider moves
inline constexpr int kCursorSelect = 2;        // a menu row taken
inline constexpr int kCursorButtonClick = 4;   // a HUD / mini-panel button pressed
inline constexpr int kCursorPointDrop = 5;
inline constexpr int kCursorDurabilityBreak = 9;
inline constexpr int kCursorConvertReady = 10;
inline constexpr int kCursorConvertItem = 11;
inline constexpr int kCursorIdentifyReady = 12;
inline constexpr int kCursorIdentifyItem = 13;
inline constexpr int kCursorLevelUp = 7;       // S→C 0x2C event 2
inline constexpr int kCursorQuestDone = 14;
inline constexpr int kCursorRepairItem = 15;
inline constexpr int kSceneRain = 64;
inline constexpr int kEventThunder = 202;              // event_thunder_1: the lightning's thunder (FUN_00473910)
// Diablo's death (FUN_004cc410 at 0x4cc520): after its MonSounds DeathSound (diablo_death), the
// next two rows, diablo_death_schwing 0x6a ticks on and diablo_death_hit 0xc1 ticks on.
inline constexpr int kDiabloDeath = 745;
inline constexpr int kDiabloDeathSchwing = 746;
inline constexpr int kDiabloDeathHit = 747;
inline constexpr int kDiabloSchwingTicks = 0x6a;
inline constexpr int kDiabloHitTicks = 0xc1;
// Cain in the Gibbet calls to a player near (FUN_004cb460; rules::object_call).
inline constexpr int kCainHelp = 3671;
inline constexpr std::array<int, 7> kClassSelect = { 17, 25, 21, 23, 19, 0, 0 };   // d2s class order; expansions have no classic intro cue
inline constexpr std::array<int, 7> kClassDeselect = { 18, 26, 22, 24, 20, 0, 0 };

// A hit class (hitclass.txt's rows, as Weapons.txt "hit class" names them)
// in its low nibble; a skill's HitClass adds an element's layer in the high
// one (Skills.txt and Missiles.txt write the number).
inline constexpr std::array<std::string_view, 14> kHitClassCodes = { "", "hth", "1hss", "1hsl", "2hss", "2hsl", "1ht", "2ht",
                                                                     "club", "staf", "bow", "xbow", "claw", "over" };
inline int hit_class(std::string_view code) {
    for (std::size_t i = 1; i < kHitClassCodes.size(); ++i) if (code == kHitClassCodes[i]) return int(i);
    return 0;
}

// The swing a player's A1 / A2 plays (FUN_004cb6a0, the table at 0x727ed8):
// weapon_* by the weapon's hit class, `frames` into the attack (FUN_004ca500:
// (frames x 256 + 128) / the animation's speed ticks on). None for bows.
struct Swing { int sound = 0, frames = 0; };
inline constexpr std::array<Swing, 14> kSwing = { { { 0, 0 }, { 251, 6 }, { 262, 6 }, { 268, 6 }, { 274, 8 }, { 280, 8 }, { 286, 6 },
                                                   { 292, 8 }, { 268, 6 }, { 298, 6 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } };

// A player's own sounds by class (Amazon .. Assassin; the table at 0x72a008,
// FUN_004ca3e0): struck (GH / KB) and dying (DT), FUN_004cc410.
// `step`: its footsteps' first row (+0x28: light / medium / heavy walk on dirt).
struct PlayerVoice { int hit = 0, death = 0, step = 0; };
inline constexpr std::array<PlayerVoice, 7> kPlayerVoice = { { { 2878, 2883, 2768 }, { 2910, 2915, 2720 }, { 2894, 2899, 2720 }, { 2886, 2891, 2768 },
                                                              { 2902, 2907, 2816 }, { 2926, 2931, 2720 }, { 2918, 2923, 2720 } } };

// A town NPC's greetings by MonStats class (FUN_004e0370's records, from
// 0x72ae28): its greeting, "inactive" one, the first of its three time-of-day
// ones and its "return" one (0 none).
struct Greeting { int monster_class = 0, greeting = 0, inactive = 0, time = 0, back = 0; };
inline constexpr std::array<Greeting, 35> kGreetings = { {
    { 0x92, 3644, 3647, 3649, 3644 }, { 0x93, 3910, 3913, 3917, 3916 }, { 0x94, 3480, 3482, 3485, 3484 }, { 0x96, 4077, 4079, 4082, 4081 },
    { 0x9a, 3737, 3739, 3743, 3742 }, { 0x9b, 0, 4320, 4324, 4323 }, { 0xaf, 0, 4320, 4324, 4323 }, { 0xb0, 3613, 3615, 3618, 3617 },
    { 0xb1, 3776, 3779, 3782, 3781 }, { 0xb2, 3844, 3847, 3850, 3849 }, { 0xc6, 3949, 3951, 3954, 3953 }, { 0xc7, 3811, 3814, 3816, 3811 },
    { 0xc8, 3877, 3880, 3883, 3882 }, { 0xc9, 4049, 4051, 4054, 4053 }, { 0xca, 4118, 4120, 4123, 4122 }, { 0xd2, 4150, 4152, 4155, 4154 },
    { 0xf4, 3644, 3647, 3649, 3644 }, { 0xf5, 3644, 3647, 3649, 3644 }, { 0xf6, 3644, 3647, 3649, 3644 }, { 0xfb, 4303, 4305, 4306, 4303 },
    { 0xfc, 3571, 3573, 3576, 3575 }, { 0xfd, 3992, 3993, 3995, 3994 }, { 0xfe, 3523, 3525, 3528, 3527 }, { 0xff, 4248, 4250, 4252, 4251 },
    { 0x101, 3989, 3991, 0, 3989 }, { 0x109, 3644, 3647, 3649, 3644 }, { 0x129, 4217, 4219, 4221, 4220 }, { 0x195, 4043, 4045, 0, 4043 },
    { 0x1ff, 0, 4449, 4453, 4452 }, { 0x200, 0, 4382, 4386, 4385 }, { 0x201, 0, 4489, 4493, 4492 }, { 0x202, 0, 4533, 4537, 4536 },
    { 0x203, 0, 4563, 4567, 4566 }, { 0x208, 3644, 3647, 3649, 3644 }, { 0x209, 4303, 4305, 4306, 4303 } } };
inline const Greeting* greeting_of(int monster_class) {
    const auto found = std::ranges::find(kGreetings, monster_class, &Greeting::monster_class);
    return found != kGreetings.end() ? &*found : nullptr;
}
// The row a greeting starts from (FUN_004e0590), before its group member is
// drawn: `kind` 2 its return one; else its greeting, as kind 0 its inactive
// one a time in 2; and its time of day's (Day::phase 1: the first, 2 or 3:
// the second, else the third) a time in 3, or when that left none. `roll(n)`
// is the client's rand(n) (FUN_004e40a0).
template <class Roll> int greeting_row(const Greeting& npc, int kind, int phase, Roll&& roll) {
    if (kind == 2) return npc.back;
    int row = npc.greeting;
    if (kind == 0 && npc.inactive != 0 && roll(2) == 0) row = npc.inactive;
    if (npc.time != 0 && (row == 0 || roll(3) == 0)) row = npc.time + (phase == 1 ? 0 : phase == 2 || phase == 3 ? 1 : 2);
    return row;
}

// A footstep's row (FUN_004caeb0): its first, + 4 a floor material past dirt
// (1 dirt, 2 indoor stone, 3 outdoor stone, 4 sand, 5 snow, 6 wood), + 24 running.
inline int footstep(int first, int material, bool running) {
    return first <= 0 ? 0 : first + 4 * (std::clamp(material, 1, 6) - 1) + (running ? 24 : 0);
}
// A floor tile's material by its DT1 material flags (FUN_004cad60), 0 none:
// the level's SoundEnviron Material 1 then (FUN_004cadb0).
inline int floor_material(unsigned flags) {
    return flags & 0x20 ? 1 : flags & 0x08 ? 2 : flags & 0x10 ? 3 : flags & 0x40 ? 4 : flags & 0x80 ? 6 : flags & 0x400 ? 5 : 0;
}
// The first floor tile at a cell (FUN_004cadb0): its flags, or the level
// default when they name none. A later tile on that cell is not read.
template <class Rooms>
int first_floor_material(const Rooms& rooms, int cell_x, int cell_y, int level_material) {
    for (const auto& floors : rooms)
        for (const auto& floor : floors)
            if (floor.x == cell_x && floor.y == cell_y) {
                const int material = floor_material(floor.material);
                return material > 0 ? material : level_material;
            }
    return level_material;
}
// A step plays on the tick whose animation position `pos` (256ths of a frame,
// `rate` a tick) is nearer a footfall than the ticks either side (FUN_004caf60):
// footfalls `offset` + k x `length` / `count` apart, distances round the
// period (FUN_004e4180).
inline int around(int from, int point, int period) {
    if ((period & (period - 1)) == 0) { from &= period - 1; point &= period - 1; }
    else { from %= period; point %= period; }
    const int apart = point - from;
    return std::min({ std::abs(apart), std::abs(apart - period), std::abs(-apart - period) });
}
inline bool footfall(int pos, int rate, int length, int count, int offset) {
    if (rate <= 0 || count <= 0 || length / count <= 0) return false;
    const int period = length / count, here = around(pos, offset, period);
    return here < around(pos + rate, offset, period) && here < around(pos - rate, offset, period);
}

// What a unit struck plays as it goes into GH, KB or DT (FUN_004cc410 ->
// FUN_004ca560): impact_* by the hit class, and the element's layer
// (`dying`: DT, whose cold and ice layers are left out).
struct Impact { int impact = 0, element = 0; };
inline Impact impact(int hit_class, bool dying) {
    Impact out;
    switch (hit_class & 0xf) {
    case 1: out.impact = 321; break;                   // impact_punch
    case 2: case 3: case 4: case 5: out.impact = 341; break;   // impact_blade_swing
    case 6: case 7: out.impact = 335; break;           // impact_blade_thrust
    case 8: case 9: out.impact = 329; break;           // impact_blunt
    case 10: case 11: out.impact = 347; break;         // impact_arrow
    case 12: out.impact = 325; break;                  // impact_claw
    default: break;
    }
    switch (hit_class & 0xf0) {
    case 0x10: if (out.impact) out.element = 0x17b; break;
    case 0x20: out.element = out.impact ? 0x164 : 0x161; break;   // impact_fire_layer / impact_fire
    case 0x30: if (!out.impact) out.element = 0x167; else if (!dying) out.element = 0x16a; break;
    case 0x40: out.element = out.impact ? 0x174 : 0x170; break;
    case 0x50: if (!out.impact || !dying) out.element = 0x178; break;
    case 0x60: out.element = 0x17e; break;
    case 0x70: out.element = 0x18b; break;
    case 0x80: out.element = 0x956; break;
    case 0xb0: out.element = 0x187; break;
    default: break;
    }
    return out;
}

// A player's block (FUN_004cb860 -> FUN_004ca810): block_weapon for hit
// classes 1..9, block_arrow for bow and crossbow, none for the rest (claw,
// overhead, none).
inline constexpr int kBlockWeapon = 398, kBlockArrow = 401;
inline int block(int hit_class) {
    const int low = hit_class & 0xf;
    return low >= 1 && low <= 9 ? kBlockWeapon : low == 10 || low == 11 ? kBlockArrow : 0;
}
// A player's kick (KK, FUN_004cc5b0): weapon_kick, an Assassin's 4 ticks on.
inline constexpr int kKick = 255;
inline constexpr std::uint32_t kKickDelayMs = 4 * 40;   // FUN_004b9a00's delay, 4 ticks

}  // namespace d2d::rules::sound_ids
