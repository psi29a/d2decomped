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
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::drlg {

// LvlMaze.txt: rooms to grow by difficulty, room size, merge chance /1000;
// Levels.txt's LevelType.
struct MazeDef { std::array<int, 3> rooms{}; int width = 0, height = 0, merge = 0, type = 3; };

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

// A LevelType's first maze def (FUN_006709b0, FUN_006735f0, FUN_006738c0).
// ponytail: act 1's caves (3), crypts (4), Barracks (7), Jail (8), Catacombs (10) only.
inline int maze_base(int type) { return type == 4 ? 0x6c : type == 7 ? 0xa7 : type == 8 ? 0xcd : type == 10 ? 0x101 : 0x34; }

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
inline void preset_rooms(const OutdoorData& data, const Preset& preset, int def, int file, int x, int y, int fallback_w, int fallback_h,
                         d2d::rules::Rng& seed, std::vector<Outdoor::RoomSeed>& out) {
    const int width = preset.width && preset.height ? preset.width : fallback_w, height = preset.width && preset.height ? preset.height : fallback_h;
    const auto* map = file >= 0 && file < 6 ? preset.maps[std::size_t(file)] : nullptr;
    const bool rolled = map && (preset.scan || preset.pops);
    std::vector<Unit> units;
    if (rolled) {
        units = ds1_units(*map, data.ids);
        std::erase_if(units, [&](const Unit& unit) { return !stays(unit, data.ids, seed); });
    }
    const bool one = width < 13 && height < 13;
    for (int tile_y = 0; tile_y < height; tile_y += 8)                          // FUN_00666680
        for (int tile_x = 0; tile_x < width; tile_x += 8) {
            seed.next();
            d2d::rules::Rng room_seed{ seed.low };
            room_seed.next();
            out.push_back({ x + tile_x, y + tile_y, room_seed.low, one ? width : std::min(8, width - tile_x), one ? height : std::min(8, height - tile_y), 2, def, file, x, y, rolled, units, room_seed.high });
            if (one) return;
        }
}

// A preset level (DrlgType 2, FUN_00668100): the first LvlPrest row for
// the level, its file rolled on the level seed (the act may override it),
// then the finish.
// Its file (level +0x14 [1]): rolled on the level seed unless the act layout set it. The Barracks reads level 27's.
inline int preset_file(const OutdoorData& data, int level, d2d::rules::Rng& seed, int file_override = -1) {
    const auto found = std::ranges::find_if(data.presets, [&](const auto& entry) { return entry.second.level_id == level; });
    const int file = found == data.presets.end() ? 0 : seed(found->second.files);   // FUN_00666ed0
    return file_override != -1 ? file_override : file;
}

inline std::vector<Outdoor::RoomSeed> generate_preset(const OutdoorData& data, int level, int level_w, int level_h, d2d::rules::Rng seed, std::vector<std::string>& notes,
                                                     int file_override = -1) {
    std::vector<Outdoor::RoomSeed> out;
    const auto found = std::ranges::find_if(data.presets, [&](const auto& entry) { return entry.second.level_id == level; });
    if (found == data.presets.end()) { notes.push_back("drlg: no preset for level " + std::to_string(level)); return out; }
    const int file = preset_file(data, level, seed, file_override);
    preset_rooms(data, found->second, found->first, file, 0, 0, level_w, level_h, seed, out);   // a sizeless preset fills the level
    return out;
}

// An act 1 cave (LevelType 3) from its seed: every 8x8 (or smaller preset)
// room in the order game.exe makes them, level-relative, kind 2 with its
// preset def, file and the preset's origin.
inline std::vector<Outdoor::RoomSeed> generate_maze(const OutdoorData& data, const MazeDef& maze, int level, int level_w,
                                                   int level_h, d2d::rules::Rng seed, int difficulty,
                                                   std::vector<std::string>& notes, int court_file = -1, std::array<int, 4>* court = nullptr) {
    // court: the Barracks' (28) level 27 {x, y, w, h} and its file in, the Barracks' own rect out (FUN_00673120).
    using namespace maze_detail;
    std::vector<Room> rooms;
    std::vector<int> list;                              // the level's room list, newest first
    auto alloc = [&](int width, int height) {                    // FUN_0066b3e0
        seed.next();
        Room room;
        room.seed = d2d::rules::Rng{ seed.low };
        room.seed.next();
        room.width = width;
        room.height = height;
        rooms.push_back(room);
        return int(rooms.size()) - 1;
    };
    auto add = [&](int index) { list.insert(list.begin(), index); };                   // FUN_0066b970
    auto gap = [&](const Room& first, const Room& second) {
        return std::pair{ first.x < second.x ? second.x - first.width - first.x : first.x - second.width - second.x, first.y < second.y ? second.y - first.height - first.y : first.y - second.height - second.y };
    };
    // FUN_00670880: beside room `at` on `side` (0 left, 1 above, 2 right, 3
    // below), clear of every room but `at`.
    auto place = [&](int index, int next_to, int side) {
        auto& room = rooms[std::size_t(index)];
        const auto& neighbour = rooms[std::size_t(next_to)];
        room.x = side == 0 ? neighbour.x - neighbour.width : side == 2 ? neighbour.x + neighbour.width : neighbour.x;
        room.y = side == 1 ? neighbour.y - neighbour.height : side == 3 ? neighbour.y + neighbour.height : neighbour.y;
        for (const int other_index : list) {
            if (other_index == next_to) continue;
            const auto [gap_x, gap_y] = gap(room, rooms[std::size_t(other_index)]);
            if (gap_x < 0 && gap_y < 0) return false;
        }
        return true;
    };
    auto link1 = [&](int from, int to_room, int side) {                                 // FUN_0066b560
        auto& links = rooms[std::size_t(from)].links;
        if (std::ranges::find(links, to_room, &std::pair<int, int>::first) == links.end()) links.insert(links.begin(), { to_room, side });
    };
    auto link = [&](int from, int to_room, int side) { link1(from, to_room, side); link1(to_room, from, (side - 2) & 3); };   // FUN_0066b5e0
    auto set_def = [&](int index) {                                                // FUN_006709b0(1)
        auto& room = rooms[std::size_t(index)];
        int mask = 0;
        for (const auto& [other_index, side] : room.links) mask |= side == 0 ? 1 : side == 1 ? 8 : side == 2 ? 2 : 4;
        room.def = maze_base(maze.type) + mask;
        room.file = -1;
        room.special = false;
    };
    auto side_of = [&](const Room& first, const Room& second) {                         // FUN_00642240
        if (second.x < first.x) { if (first.x == second.x + second.width) return 0; }
        else if (second.x == first.x + first.width) return 2;
        if (second.y < first.y) { if (first.y == second.y + second.height) return 1; }
        else if (second.y == first.y + first.height) return 3;
        return -1;
    };

    auto merge = [&](int index) {                                                   // FUN_00670c70
        for (const int other_index : list) {
            auto& other = rooms[std::size_t(other_index)];
            if (other.special) continue;
            const auto [gap_x, gap_y] = gap(rooms[std::size_t(index)], other);
            if (!(gap_x < 1 && gap_y < 1) || gap_x == gap_y) continue;
            const auto& links = rooms[std::size_t(index)].links;
            if (std::ranges::find(links, other_index, &std::pair<int, int>::first) != links.end()) continue;
            if (int(other.seed.next() % 1000) >= maze.merge) continue;
            const int other_side = side_of(rooms[std::size_t(index)], other);
            if (other_side == -1) continue;
            link(index, other_index, other_side);
            set_def(other_index);
        }
    };
    // One room beside `next_to` (FUN_00670f60 / FUN_006714d0's step), -1 when it won't fit.
    auto arm = [&](int next_to, int side) {
        const int index = alloc(maze.width, maze.height);
        if (!place(index, next_to, side)) return -1;
        link(next_to, index, side);
        merge(index);
        add(index);
        set_def(next_to);
        set_def(index);
        return index;
    };

    // The first room, centred (FUN_00673b30).
    const int first = alloc(maze.width, maze.height);
    rooms[std::size_t(first)].x = (level_w - maze.width) / 2;
    rooms[std::size_t(first)].y = (level_h - maze.height) / 2;
    add(first);

    // Jail and Barracks: a ring of 2x2 rooms round the first (FUN_00670f60(2):
    // above, left, below, closed back on the first). Catacombs: arms off the
    // first, which becomes special (FUN_006714d0): level 34 all four sides,
    // else a roll picks above / below or left / right.
    if (maze.type == 7 || maze.type == 8) {
        int last = first;
        for (const int side : { 1, 0, 3 }) last = arm(last, side);                   // ponytail: a failed step (never, from one room) would crash game.exe
        link(last, first, 2);
        set_def(last);
        set_def(first);
    } else if (maze.type == 10) {
        const auto& sides = level == level_ids::kCatacombsLevel1 ? std::vector{ 1, 2, 3, 0 } : seed.next() & 1 ? std::vector{ 0, 2 } : std::vector{ 1, 3 };
        for (const int side : sides) arm(first, side);
        auto& room = rooms[std::size_t(first)];
        room.def = level == level_ids::kCatacombsLevel1 ? 0x122 : sides.front() == 0 ? 0x120 : 0x121;
        room.special = true;
        room.file = -1;
    }

    // Grow (FUN_00671210).
    const int want = maze.rooms[std::size_t(std::clamp(difficulty, 0, 2))];
    while (int(list.size()) < want) {
        const int next_to = list[std::size_t(seed(int(list.size())))];            // FUN_006711a0
        const int side = int(rooms[std::size_t(next_to)].seed.next() & 3);
        if (rooms[std::size_t(next_to)].special) continue;
        const int index = alloc(maze.width, maze.height);
        if (!place(index, next_to, side)) continue;                                     // freed (FUN_0066c100)
        link(next_to, index, side);
        merge(index);
        add(index);
        set_def(index);
        set_def(next_to);
    }

    int court_room = -1;
    // Special rooms (FUN_00672550 caves, FUN_00672610 crypts -> FUN_006724e0 / FUN_00670eb0).
    {
        int turn = maze.type == 7 ? court_file : int(seed.next() & 3);
        auto special = [&](const Special& special_room) {
            bool done = false;
            for (const int other_index : list) {
                auto& room = rooms[std::size_t(other_index)];
                if (!room.special && room.def == special_room.from) { room.def = special_room.to_room; room.file = special_room.file; room.special = true; done = true; break; }
            }
            for (std::size_t i = 0; !done && i < list.size(); ++i) {
                const int other_index = list[i];
                if (rooms[std::size_t(other_index)].special) continue;
                const int index = alloc(maze.width, maze.height);
                if (!place(index, other_index, special_room.dir)) continue;
                link(other_index, index, special_room.dir);
                add(index);
                set_def(other_index);
                auto& room = rooms[std::size_t(index)];
                room.special = true;
                room.def = special_room.to_room;
                room.file = special_room.file;
                done = true;
            }
            turn = (turn + 1) & 3;
        };
        if (maze.type == 8) {                                                       // FUN_006726d0
            special(kSpecials[10][std::size_t(turn)]);
            special(kSpecials[level == level_ids::kJailLevel1 ? 13 : level == level_ids::kJailLevel2 ? 14 : 11][std::size_t(turn)]);
            if (level != level_ids::kJailLevel3) special(kSpecials[12][std::size_t(turn)]);
        } else if (maze.type == 10) {                                               // FUN_006727a0
            special(kSpecials[15][std::size_t(turn)]);
            if (level == level_ids::kCatacombsLevel2) special(kSpecials[16][std::size_t(turn)]);
        } else if (maze.type == 7) {                                                // FUN_00673120
            // The anchor: the rightmost / lowest / leftmost room (by court file) a room fits beside (FUN_00672340's probe, then freed).
            const int side = std::array{ 2, 3, 0 }[std::size_t(court_file)];
            auto key = [&](int index) { const auto& room = rooms[std::size_t(index)]; return court_file == 0 ? room.x : court_file == 1 ? room.y : -room.x; };
            int anchor = -1;
            for (const int other_index : list) {
                if (anchor != -1 && key(other_index) <= key(anchor)) continue;
                const auto& room = rooms[std::size_t(other_index)];
                if (room.special || std::ranges::find(room.links, side, &std::pair<int, int>::second) != room.links.end()) continue;
                const int probe = alloc(maze.width, maze.height);
                if (place(probe, other_index, side)) anchor = other_index;
                rooms.pop_back();
            }
            // Court Connect (FUN_00670de0): no merge, no set_def of its own.
            court_room = alloc(maze.width, maze.height);
            place(court_room, anchor, side);
            link(anchor, court_room, side);
            add(court_room);
            set_def(anchor);
            auto& room = rooms[std::size_t(court_room)];
            room.special = true;
            room.def = 0xa7;
            room.file = court_file;
            if (seed.next() & 1) { special(kSpecials[17][std::size_t(turn)]); special(kSpecials[18][std::size_t(turn)]); }
            else { special(kSpecials[18][std::size_t(turn)]); special(kSpecials[17][std::size_t(turn)]); }
        } else if (maze.type == 4) {
            special(kSpecials[5][std::size_t(turn)]);
            if (level == level_ids::kCrypt) special(kSpecials[6][std::size_t(turn)]);
            if (level == level_ids::kMausoleum || level == level_ids::kMatronsDen) special(kSpecials[8][std::size_t(turn)]);
            if (level >= level_ids::kTowerCellarLevel1 && level < level_ids::kTowerCellarLevel5) special(kSpecials[9][std::size_t(turn)]);
        } else {
            special(kSpecials[0][std::size_t(turn)]);
            special(kSpecials[level == level_ids::kDenOfEvil ? 1 : 2][std::size_t(turn)]);
            if (level == level_ids::kCaveLevel1) special(kSpecials[3][std::size_t(turn)]);
            if (level == level_ids::kUndergroundPassageLevel1) special(kSpecials[4][std::size_t(turn)]);
        }
    }

    // Level-relative from the rooms' top-left (FUN_00642590).
    int min_x = rooms[std::size_t(list.front())].x, min_y = rooms[std::size_t(list.front())].y;
    for (const int other_index : list) { min_x = std::min(min_x, rooms[std::size_t(other_index)].x); min_y = std::min(min_y, rooms[std::size_t(other_index)].y); }
    for (const int other_index : list) { rooms[std::size_t(other_index)].x -= min_x; rooms[std::size_t(other_index)].y -= min_y; }
    if (court_room != -1 && court) {                   // the Barracks beside level 27: its rect is its rooms' box (FUN_00642520)
        auto& [court_x, court_y, width, height] = *court;
        const auto& room = rooms[std::size_t(court_room)];
        const int target_x = court_file == 0 ? court_x - maze.width : court_file == 1 ? court_x + width / 2 - 6 : court_x + width;
        const int target_y = court_file == 0 ? court_y + height / 2 : court_file == 1 ? court_y - maze.height : court_y + height / 2 + 1;
        court_x = target_x - room.x;
        court_y = target_y - room.y;
        width = height = 0;
        for (const int other_index : list) {
            width = std::max(width, rooms[std::size_t(other_index)].x + rooms[std::size_t(other_index)].width);
            height = std::max(height, rooms[std::size_t(other_index)].y + rooms[std::size_t(other_index)].height);
        }
    }

    // Theme rooms (FUN_006735f0): up to rooms / 5 + 1 (at least 2) plain
    // rooms of def base + perm[i] become def + 15, file rolled later.
    if (level != level_ids::kDenOfEvil) {
        const int base = maze_base(maze.type);
        int slot = int(seed.next() % 15);
        std::array<int, 15> perm{};
        for (int i = 0; i < 15; ++i) perm[std::size_t(i)] = i;
        for (int i = 0; i < 15; ++i) {
            const auto swap_a = seed.next() % 15, swap_b = seed.next() % 15;
            std::swap(perm[swap_a], perm[swap_b]);
        }
        int left = std::max(2, int(list.size()) / 5 + 1);
        for (int tries = int(list.size()) * 2; left && tries; --tries, slot = (slot + 1) % 15)
            for (const int other_index : list) {
                auto& room = rooms[std::size_t(other_index)];
                if (room.special || room.def != perm[std::size_t(slot)] + base) continue;
                room.special = true;
                room.def += 15;
                room.file = -1;
                --left;
                break;
            }
    }

    // Each maze room becomes its preset's rooms (FUN_00673a60), list order.
    std::unordered_map<int, int> rotation;              // level +0x1cc
    std::vector<Outdoor::RoomSeed> out;
    for (const int other_index : std::vector<int>(list)) {
        const auto& room = rooms[std::size_t(other_index)];
        const auto found = data.presets.find(room.def);
        if (found == data.presets.end()) { notes.push_back("drlg: maze preset " + std::to_string(room.def) + " missing"); continue; }
        const auto& preset = found->second;
        int file = seed(preset.files);                        // FUN_00666ed0
        if (room.file != -1) file = room.file;                 // FUN_006738c0
        else if (room.def > maze_base(maze.type) && room.def < maze_base(maze.type) + 0x10) {
            auto rot = rotation.find(room.def);
            if (rot == rotation.end()) rot = rotation.emplace(room.def, seed(preset.files)).first;
            file = rot->second = preset.files ? (rot->second + 1) % preset.files : 0;
        }
        preset_rooms(data, preset, room.def, file, room.x, room.y, room.width, room.height, seed, out);
    }
    return out;
}

}  // namespace d2d::drlg
