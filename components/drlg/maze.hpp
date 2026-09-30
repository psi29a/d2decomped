// Maze levels (DrlgType 1, .\DRLG\Maze.cpp): act 1's caves, the Den of
// Evil first — rooms grown beside random rooms (FUN_00671210), special
// rooms placed from tables (FUN_00672550), each room a cave preset picked
// by which sides it links through (FUN_006709b0), then split into 8x8
// rooms (FUN_00673a60 -> FUN_00667ed0). docs/research/re/drlg.md "Maze levels".
#pragma once

#include "outdoor.hpp"

#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::drlg {

// LvlMaze.txt: rooms to grow by difficulty, room size, merge chance /1000.
struct MazeDef { std::array<int, 3> rooms{}; int width = 0, height = 0, merge = 0; };

namespace maze_detail {

// {def to replace, def to set, file (-1 = roll), side} (0x6ef8a8 + 0x40 k):
// the entrance, then the level's own (0x6ef8e8 Den of Evil, 0x6ef928 the
// rest), then Cave 2's and Cave 3's (levels 9, 10).
struct Special { int from, to_room, file, dir; };
inline constexpr std::array<std::array<Special, 4>, 5> kSpecials = { {
    { { { 60, 86, -1, 3 }, { 54, 84, -1, 0 }, { 56, 85, -1, 1 }, { 53, 83, -1, 2 } } },
    { { { 60, 98, -1, 3 }, { 54, 96, -1, 0 }, { 56, 97, -1, 1 }, { 53, 95, -1, 2 } } },
    { { { 60, 94, -1, 3 }, { 54, 92, -1, 0 }, { 56, 93, -1, 1 }, { 53, 91, -1, 2 } } },
    { { { 60, 102, -1, 3 }, { 54, 100, -1, 0 }, { 56, 101, -1, 1 }, { 53, 99, -1, 2 } } },
    { { { 60, 90, -1, 3 }, { 54, 88, -1, 0 }, { 56, 89, -1, 1 }, { 53, 87, -1, 2 } } } } };

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
            out.push_back({ x + tile_x, y + tile_y, room_seed.low, one ? width : std::min(8, width - tile_x), one ? height : std::min(8, height - tile_y), 2, def, file, x, y, rolled, units });
            if (one) return;
        }
}

// A preset level (DrlgType 2, FUN_00668100): the first LvlPrest row for
// the level, its file rolled on the level seed (the act may override it),
// then the finish.
inline std::vector<Outdoor::RoomSeed> generate_preset(const OutdoorData& data, int level, int level_w, int level_h, d2d::rules::Rng seed, std::vector<std::string>& notes,
                                                     int file_override = -1) {
    std::vector<Outdoor::RoomSeed> out;
    const auto found = std::ranges::find_if(data.presets, [&](const auto& entry) { return entry.second.level_id == level; });
    if (found == data.presets.end()) { notes.push_back("drlg: no preset for level " + std::to_string(level)); return out; }
    int file = seed(found->second.files);                                       // FUN_00666ed0
    if (file_override != -1) file = file_override;                              // level +0x14 [1], set by the act layout
    preset_rooms(data, found->second, found->first, file, 0, 0, level_w, level_h, seed, out);   // a sizeless preset fills the level
    return out;
}

// An act 1 cave (LevelType 3) from its seed: every 8x8 (or smaller preset)
// room in the order game.exe makes them, level-relative, kind 2 with its
// preset def, file and the preset's origin.
inline std::vector<Outdoor::RoomSeed> generate_maze(const OutdoorData& data, const MazeDef& maze, int level, int level_w,
                                                   int level_h, d2d::rules::Rng seed, int difficulty,
                                                   std::vector<std::string>& notes) {
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
        room.def = 0x34 + mask;
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

    // The first room, centred (FUN_00673b30).
    const int first = alloc(maze.width, maze.height);
    rooms[std::size_t(first)].x = (level_w - maze.width) / 2;
    rooms[std::size_t(first)].y = (level_h - maze.height) / 2;
    add(first);

    // Grow (FUN_00671210).
    const int want = maze.rooms[std::size_t(std::clamp(difficulty, 0, 2))];
    while (int(list.size()) < want) {
        const int next_to = list[std::size_t(seed(int(list.size())))];            // FUN_006711a0
        const int side = int(rooms[std::size_t(next_to)].seed.next() & 3);
        if (rooms[std::size_t(next_to)].special) continue;
        const int index = alloc(maze.width, maze.height);
        if (!place(index, next_to, side)) continue;                                     // freed (FUN_0066c100)
        link(next_to, index, side);
        for (const int other_index : list) {                                             // FUN_00670c70: merge
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
        add(index);
        set_def(index);
        set_def(next_to);
    }

    // Special rooms (FUN_00672550 -> FUN_006724e0 / FUN_00670eb0).
    {                                                   // LevelType 3 (act 1 caves)
        int turn = int(seed.next() & 3);
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
        special(kSpecials[0][std::size_t(turn)]);
        special(kSpecials[level == 8 ? 1 : 2][std::size_t(turn)]);
        if (level == 9) special(kSpecials[3][std::size_t(turn)]);
        if (level == 10) special(kSpecials[4][std::size_t(turn)]);
    }

    // Level-relative from the rooms' top-left (FUN_00642590).
    int min_x = rooms[std::size_t(list.front())].x, min_y = rooms[std::size_t(list.front())].y;
    for (const int other_index : list) { min_x = std::min(min_x, rooms[std::size_t(other_index)].x); min_y = std::min(min_y, rooms[std::size_t(other_index)].y); }
    for (const int other_index : list) { rooms[std::size_t(other_index)].x -= min_x; rooms[std::size_t(other_index)].y -= min_y; }

    // Theme rooms (FUN_006735f0): up to rooms / 5 + 1 (at least 2) plain
    // rooms of def base + perm[i] become def + 15, file rolled later.
    // ponytail: base 0x34 is LevelType 3's; crypts (4) use 0x6c.
    if (level != 8) {
        constexpr int base = 0x34;
        int at = int(seed.next() % 15);
        std::array<int, 15> perm{};
        for (int i = 0; i < 15; ++i) perm[std::size_t(i)] = i;
        for (int i = 0; i < 15; ++i) {
            const auto first = seed.next() % 15, second = seed.next() % 15;
            std::swap(perm[first], perm[second]);
        }
        int left = std::max(2, int(list.size()) / 5 + 1);
        for (int tries = int(list.size()) * 2; left && tries; --tries, at = (at + 1) % 15)
            for (const int other_index : list) {
                auto& room = rooms[std::size_t(other_index)];
                if (room.special || room.def != perm[std::size_t(at)] + base) continue;
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
        else if (room.def > 0x34 && room.def < 0x44) {
            auto rot = rotation.find(room.def);
            if (rot == rotation.end()) rot = rotation.emplace(room.def, seed(preset.files)).first;
            file = rot->second = preset.files ? (rot->second + 1) % preset.files : 0;
        }
        preset_rooms(data, preset, room.def, file, room.x, room.y, room.width, room.height, seed, out);
    }
    return out;
}

}  // namespace d2d::drlg
