// SPDX-License-Identifier: GPL-3.0-or-later
// Maze levels (DrlgType 1, .\DRLG\Maze.cpp): act 1's caves, the Den of
// Evil first — rooms grown beside random rooms (FUN_00671210), special
// rooms placed from tables (FUN_00672550), each room a cave preset picked
// by which sides it links through (FUN_006709b0), then split into 8x8
// rooms (FUN_00673a60 -> FUN_00667ed0). docs/research/re/drlg.md "Maze levels".
#pragma once

#include "outdoor.hpp"

#include <level_ids.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::drlg {

// LvlMaze.txt: rooms to grow by difficulty, room size, merge chance /1000;
// Levels.txt's LevelType.
// boss_tomb / staff_tomb: act 2's two Tal Rasha's Tombs (roll_act2_tombs).
struct MazeDef { std::array<int, 3> rooms{}; int width = 0, height = 0, merge = 0, type = 3, boss_tomb = 0, staff_tomb = 0; };

namespace maze_detail {

// {def to replace, def to set, file (-1 = roll), side} (0x6ef8a8 + 0x40 k):
// the entrance, then the level's own (0x6ef8e8 Den of Evil, 0x6ef928 the
// rest), then Cave 2's and Cave 3's (levels 9, 10); then the crypts'
// (0x6ef9e8 + 0x40 k): the entrance, level 18's, 0x6efa68 (unused), levels
// 19 / 133's, levels 21-24's; the Jail's (0x6efb28 + 0x40 k): the
// entrance, 31's second, 29 / 30's last, 29's, 30's; the Catacombs'
// (0x6efc68): the entrance, 35's; the Barracks' (0x6f03e8, 0x6f0428): Next, Forge.
struct Special { int from, to_room, file, dir; };
inline constexpr std::array<std::array<Special, 4>, 19> kSpecials = { {
    { { { 60, 86, -1, 3 }, { 54, 84, -1, 0 }, { 56, 85, -1, 1 }, { 53, 83, -1, 2 } } },
    { { { 60, 98, -1, 3 }, { 54, 96, -1, 0 }, { 56, 97, -1, 1 }, { 53, 95, -1, 2 } } },
    { { { 60, 94, -1, 3 }, { 54, 92, -1, 0 }, { 56, 93, -1, 1 }, { 53, 91, -1, 2 } } },
    { { { 60, 102, -1, 3 }, { 54, 100, -1, 0 }, { 56, 101, -1, 1 }, { 53, 99, -1, 2 } } },
    { { { 60, 90, -1, 3 }, { 54, 88, -1, 0 }, { 56, 89, -1, 1 }, { 53, 87, -1, 2 } } },
    { { { 116, 142, -1, 3 }, { 110, 140, -1, 0 }, { 112, 141, -1, 1 }, { 109, 139, -1, 2 } } },
    { { { 116, 150, -1, 3 }, { 110, 148, -1, 0 }, { 112, 149, -1, 1 }, { 109, 147, -1, 2 } } },
    { { { 116, 158, -1, 3 }, { 110, 156, -1, 0 }, { 112, 157, -1, 1 }, { 109, 155, -1, 2 } } },
    { { { 116, 154, -1, 3 }, { 110, 152, -1, 0 }, { 112, 153, -1, 1 }, { 109, 151, -1, 2 } } },
    { { { 116, 146, -1, 3 }, { 110, 144, -1, 0 }, { 112, 145, -1, 1 }, { 109, 143, -1, 2 } } },
    { { { 213, 239, -1, 3 }, { 207, 237, -1, 0 }, { 209, 238, -1, 1 }, { 206, 236, -1, 2 } } },
    { { { 213, 247, -1, 3 }, { 207, 245, -1, 0 }, { 209, 246, -1, 1 }, { 206, 244, -1, 2 } } },
    { { { 213, 243, -1, 3 }, { 207, 241, -1, 0 }, { 209, 242, -1, 1 }, { 206, 240, -1, 2 } } },
    { { { 213, 251, -1, 3 }, { 207, 249, -1, 0 }, { 209, 250, -1, 1 }, { 206, 248, -1, 2 } } },
    { { { 213, 255, -1, 3 }, { 207, 253, -1, 0 }, { 209, 254, -1, 1 }, { 206, 252, -1, 2 } } },
    { { { 265, 294, -1, 3 }, { 259, 292, -1, 0 }, { 261, 293, -1, 1 }, { 258, 291, -1, 2 } } },
    { { { 265, 298, -1, 3 }, { 259, 296, -1, 0 }, { 261, 297, -1, 1 }, { 258, 295, -1, 2 } } },
    { { { 175, 201, -1, 3 }, { 169, 199, -1, 0 }, { 171, 200, -1, 1 }, { 168, 198, -1, 2 } } },
    { { { 175, 205, -1, 3 }, { 169, 203, -1, 0 }, { 171, 204, -1, 1 }, { 168, 202, -1, 2 } } } } };

// Act 2's special tables (16-byte records, + 0x10 k as kSpecials): the
// sewers' (0x6efce8 the entrance, 0x6efd28 level 47's last / 48's third,
// 0x6efd68 48's, 0x6efda8 49's, 0x6efde8 65's); the tombs' (0x6efe28
// levels 55-58, 0x6efe68 57's, 0x6efea8 59 / 61 / the tombs but the boss's,
// 0x6efee8 59's, 0x6eff28 60's, 0x6eff68 59 / 61's, 0x6effa8 the boss
// tomb's, 0x6effe8 the staff tomb's); the lair's (0x6f0028, 0x6f0068, and
// level 64's single records 0x6f00e8, 0x6f00d8).
// The Arcane Sanctuary's one special room by the turn (0x6f00f8, FUN_00672e50).
inline constexpr std::array<Special, 4> kArcaneSpecials{ { { 517, 528, -1, 3 }, { 511, 526, -1, 0 }, { 513, 527, -1, 1 }, { 510, 525, -1, 2 } } };
inline constexpr std::array<std::array<Special, 4>, 15> kAct2Specials = { {
    { { { 309, 335, -1, 3 }, { 303, 333, -1, 0 }, { 305, 334, -1, 1 }, { 302, 332, -1, 2 } } },
    { { { 309, 340, -1, 3 }, { 303, 338, -1, 0 }, { 305, 339, -1, 1 }, { 302, 337, -1, 2 } } },
    { { { 309, 348, -1, 3 }, { 303, 346, -1, 0 }, { 305, 347, -1, 1 }, { 302, 345, -1, 2 } } },
    { { { 309, 344, -1, 3 }, { 303, 342, -1, 0 }, { 305, 343, -1, 1 }, { 302, 341, -1, 2 } } },
    { { { 309, 352, -1, 3 }, { 303, 350, -1, 0 }, { 305, 351, -1, 1 }, { 302, 349, -1, 2 } } },
    { { { 421, 451, -1, 3 }, { 415, 449, -1, 0 }, { 417, 450, -1, 1 }, { 414, 448, -1, 2 } } },
    { { { 421, 479, -1, 3 }, { 415, 477, -1, 0 }, { 417, 478, -1, 1 }, { 414, 476, -1, 2 } } },
    { { { 421, 475, -1, 3 }, { 415, 473, -1, 0 }, { 417, 474, -1, 1 }, { 414, 472, -1, 2 } } },
    { { { 421, 467, -1, 3 }, { 415, 465, -1, 0 }, { 417, 466, -1, 1 }, { 414, 464, -1, 2 } } },
    { { { 421, 459, -1, 3 }, { 415, 457, -1, 0 }, { 417, 458, -1, 1 }, { 414, 456, -1, 2 } } },
    { { { 421, 455, -1, 3 }, { 415, 453, -1, 0 }, { 417, 454, -1, 1 }, { 414, 452, -1, 2 } } },
    { { { 421, 463, -1, 3 }, { 415, 461, -1, 0 }, { 417, 462, -1, 1 }, { 414, 460, -1, 2 } } },
    { { { 421, 471, -1, 3 }, { 415, 469, -1, 0 }, { 417, 470, -1, 1 }, { 414, 468, -1, 2 } } },
    { { { 489, 500, -1, 3 }, { 483, 498, -1, 0 }, { 485, 499, -1, 1 }, { 482, 497, -1, 2 } } },
    { { { 489, 504, -1, 3 }, { 483, 502, -1, 0 }, { 485, 503, -1, 1 }, { 482, 501, -1, 2 } } } } };
inline constexpr Special kLair64First = { 485, 509, -1, 1 }, kLair64Second = { 482, 505, -1, 2 };
// Act 3's special tables (16-byte records, + 0x10 k): the Flayer
// Dungeon and Swampy Pits' (LevelType 24, FUN_00672ea0: 0x6f0138 then
// 0x6f0178), the sewers' (25, FUN_00672f00: 0x6f01b8, 0x6f01f8), the
// Durance's (22, FUN_00672f60: 0x6f0238; level 101 0x6f0278 and 0x6f02b8,
// level 100 0x6f02b8).
inline constexpr std::array<std::array<Special, 4>, 7> kAct3Specials = { {
    { { { 672, 698, -1, 3 }, { 666, 696, -1, 0 }, { 668, 697, -1, 1 }, { 665, 695, -1, 2 } } },
    { { { 672, 702, -1, 3 }, { 666, 700, -1, 0 }, { 668, 701, -1, 1 }, { 665, 699, -1, 2 } } },
    { { { 712, 742, -1, 3 }, { 706, 740, -1, 0 }, { 708, 741, -1, 1 }, { 705, 739, -1, 2 } } },
    { { { 712, 746, -1, 3 }, { 706, 744, -1, 0 }, { 708, 745, -1, 1 }, { 705, 743, -1, 2 } } },
    { { { 761, 787, -1, 3 }, { 755, 785, -1, 0 }, { 757, 786, -1, 1 }, { 754, 784, -1, 2 } } },
    { { { 761, 795, -1, 3 }, { 755, 793, -1, 0 }, { 757, 794, -1, 1 }, { 754, 792, -1, 2 } } },
    { { { 761, 791, -1, 3 }, { 755, 789, -1, 0 }, { 757, 790, -1, 1 }, { 754, 788, -1, 2 } } } } };
// The spider caves' (23) room by link mask (0x6ef758 + 8, 12-byte rows; 0
// none, the room keeps its def); the Spider Cave's 662 is 664, the Spider
// Cavern's 661 is 663 (FUN_006709b0).
inline constexpr std::array<int, 16> kSpiderDefs = { 0, 0, 0, 0, 0, 659, 660, 0, 0, 661, 662, 0, 0, 0, 0, 0 };
// The Kurast Sewers' first level (92): its ring's rooms made special
// (FUN_00673b30), each the first non-special listed room with the def.
inline constexpr std::array<std::pair<int, int>, 4> kSewer92Specials = { { { 0x2c5, 0x2df }, { 0x2c6, 0x2e0 }, { 0x2c9, 0x2e1 }, { 0x2ca, 0x2e2 } } };
// The tombs' entrance room by the side left free (0x6ef888, 8-byte rows),
// and the turn its def starts the specials on (FUN_00672be0); the
// Worldstone Keep's (0x6ef88c, FUN_006718c0).
inline constexpr std::array<int, 4> kTombEntrance = { 447, 444, 446, 445 };
inline constexpr std::array<int, 4> kWorldstoneEntrance = { 1075, 1077, 1076, 1074 };
// Act 5's rooms by link mask (FUN_006709b0, 8-byte rows; 0 none): the
// temple's (32, 0x6ef6d8) and the hell levels' (35, 0x6ef6dc).
inline constexpr std::array<int, 16> kTempleDefs = { 0, 0, 0, 0, 0, 1045, 1044, 0, 0, 1043, 1042, 0, 0, 0, 0, 0 };
inline constexpr std::array<int, 16> kHellDefs = { 0, 1056, 1055, 1057, 1054, 0, 0, 0, 1053, 0, 0, 0, 1058, 0, 0, 0 };
// The hell levels' two rooms off the first (0x6ef828, FUN_00671430): by a
// roll on the first room's seed, {def, side, file} twice.
struct HellArm { int def, side, file; };
inline constexpr std::array<std::array<HellArm, 2>, 4> kHellArms = { { { { { 1054, 1, 0 }, { 1053, 3, 1 } } }, { { { 1054, 1, 1 }, { 1053, 3, 0 } } },
                                                                      { { { 1055, 0, 1 }, { 1056, 2, 0 } } }, { { { 1055, 0, 0 }, { 1056, 2, 1 } } } } };
// Act 5's special tables (16-byte records): the temple's (FUN_00673000:
// 0x6f02f8, three; 0x6f0328), the Worldstone Keep's (FUN_006730b0: 0x6f0368,
// 0x6f03a8), the ice caves' (FUN_00673530: 0x6f0498, 0x6f04d8, 0x6f0518,
// 0x6f0558, 0x6f0598).
inline constexpr std::array<Special, 4> kTempleFirst = { { { 1042, 1046, -1, 1 }, { 1043, 1047, -1, 0 }, { 1045, 1048, -1, 3 }, { 1042, 1049, -1, 1 } } };
inline constexpr std::array<Special, 4> kTempleSecond = { { { 1042, 1049, -1, 1 }, { 1043, 1050, -1, 0 }, { 1045, 1052, -1, 3 }, { 1044, 1051, -1, 2 } } };
inline constexpr std::array<std::array<Special, 4>, 7> kAct5Specials = { {
    { { { 1066, 1078, -1, 3 }, { 1060, 1080, -1, 0 }, { 1062, 1079, -1, 1 }, { 1059, 1081, -1, 2 } } },
    { { { 1066, 1082, -1, 3 }, { 1060, 1084, -1, 0 }, { 1062, 1083, -1, 1 }, { 1059, 1085, -1, 2 } } },
    { { { 1010, 1021, -1, 3 }, { 1004, 1019, -1, 0 }, { 1006, 1020, -1, 1 }, { 1003, 1018, -1, 2 } } },
    { { { 1010, 1025, -1, 3 }, { 1004, 1023, -1, 0 }, { 1006, 1024, -1, 1 }, { 1003, 1022, -1, 2 } } },
    { { { 1010, 1029, -1, 3 }, { 1004, 1027, -1, 0 }, { 1006, 1028, -1, 1 }, { 1003, 1026, -1, 2 } } },
    { { { 1010, 1033, -1, 3 }, { 1004, 1031, -1, 0 }, { 1006, 1032, -1, 1 }, { 1003, 1030, -1, 2 } } },
    { { { 1010, 1034, -1, 3 }, { 1004, 1036, -1, 0 }, { 1006, 1035, -1, 1 }, { 1003, 1037, -1, 2 } } } } };
// The harem's (14) and cellars' (15) room by link mask (0x6ef758, 12-byte
// rows; 0 = none, the room keeps its def).
inline constexpr std::array<std::array<int, 2>, 16> kHaremDefs = { { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 356, 360 }, { 355, 359 }, { 0, 0 },
                                                                    { 0, 0 }, { 357, 361 }, { 354, 358 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } };

// A LevelType's first maze def (FUN_006709b0, FUN_006735f0, FUN_006738c0):
// act 1's caves (3), crypts (4), Barracks (7), Jail (8), Catacombs (10);
// act 2's sewers (13), tombs (17), lair (18), Arcane Sanctuary (19);
// act 3's Durance (22), Flayer Dungeon (24), sewers (25); act 4's lava (28);
// act 5's ice caves (33), Worldstone Keep (34).
inline int maze_base(int type) {
    switch (type) {
    case 33: return 0x3ea;
    case 34: return 0x422;
    case 22: return 0x2f1;
    case 28: return 836;                                    // act 4's River of Flame: LavaX + mask
    case 24: return 0x298;
    case 25: return 0x2c0;
    case 4: return 0x6c;
    case 7: return 0xa7;
    case 8: return 0xcd;
    case 10: return 0x101;
    case 13: return 0x12d;
    case 17: return 0x19d;
    case 18: return 0x1e1;
    case 19: return 0x1fd;
    default: return 0x34;
    }
}
// The types with theme rooms (FUN_006735f0) and with a file rotation
// (FUN_006738c0), of acts 1 to 4 (the River of Flame rotates, no themes).
inline bool maze_themes(int type) { return type == 3 || type == 4 || type == 7 || type == 8 || type == 10 || type == 13 || type == 17 || type == 22 || type == 24 || type == 25; }
inline bool maze_rotates(int type) { return maze_themes(type) || type == 18 || type == 28 || type == 33 || type == 34 || type == 35; }
// The defs a rotation covers start past this (FUN_006738c0): the hell levels' at 0x41c.
inline int rotation_base(int type) { return type == 35 ? 0x41c : maze_base(type); }

struct Room {
    int x = 0, y = 0, width = 0, height = 0, def = 0, file = -1;
    bool special = false;                               // preset flags & 2
    d2d::rules::Rng seed;
    std::vector<std::pair<int, int>> links;             // {room, side}, newest first (FUN_0066b560)
};

}  // namespace maze_detail

// FUN_00667ed0: a Scan / Pops preset rolls its DS1 units on the level
// seed (FUN_00667970), then one room under 13x13, else 8x8 rooms, each
// allocation stepping the seed.
void preset_rooms(const OutdoorData& data, const Preset& preset, int def, int file, int x, int y, int fallback_w, int fallback_h,
                         d2d::rules::Rng& seed, std::vector<Outdoor::RoomSeed>& out);

// A preset level (DrlgType 2, FUN_00668100): the first LvlPrest row for
// the level, its file rolled on the level seed (the act may override it),
// then the finish.
// Its file (level +0x14 [1]): rolled on the level seed unless the act layout set it. The Barracks reads level 27's.
int preset_file(const OutdoorData& data, int level, d2d::rules::Rng& seed, int file_override = -1);

std::vector<Outdoor::RoomSeed> generate_preset(const OutdoorData& data, int level, int level_w, int level_h, d2d::rules::Rng seed, std::vector<std::string>& notes,
                                                     int file_override = -1);

// An act 1 cave (LevelType 3) from its seed: every 8x8 (or smaller preset)
// room in the order game.exe makes them, level-relative, kind 2 with its
// preset def, file and the preset's origin.
std::vector<Outdoor::RoomSeed> generate_maze(const OutdoorData& data, const MazeDef& maze, int level, int level_w,
                                                   int level_h, d2d::rules::Rng seed, int difficulty,
                                                   std::vector<std::string>& notes, int court_file = -1, std::array<int, 4>* court = nullptr);

}  // namespace d2d::drlg
