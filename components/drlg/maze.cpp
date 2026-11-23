// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for maze.hpp.
#include "maze.hpp"

#include "outdoor.hpp"

#include <level_ids.hpp>
#include <rules.hpp>
#include <units.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::drlg {

void preset_rooms(const OutdoorData& data, const Preset& preset, int def, int file, int x, int y, int fallback_w, int fallback_h,
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

int preset_file(const OutdoorData& data, int level, d2d::rules::Rng& seed, int file_override) {
    const auto found = std::ranges::find_if(data.presets, [&](const auto& entry) { return entry.second.level_id == level; });
    const int file = found == data.presets.end() ? 0 : seed(found->second.files);   // FUN_00666ed0
    return file_override != -1 ? file_override : file;
}

std::vector<Outdoor::RoomSeed> generate_preset(const OutdoorData& data, int level, int level_w, int level_h, d2d::rules::Rng seed, std::vector<std::string>& notes,
                                                     int file_override) {
    std::vector<Outdoor::RoomSeed> out;
    const auto found = std::ranges::find_if(data.presets, [&](const auto& entry) { return entry.second.level_id == level; });
    if (found == data.presets.end()) { notes.push_back("drlg: no preset for level " + std::to_string(level)); return out; }
    const int file = preset_file(data, level, seed, file_override);
    preset_rooms(data, found->second, found->first, file, 0, 0, level_w, level_h, seed, out);   // a sizeless preset fills the level
    return out;
}

std::vector<Outdoor::RoomSeed> generate_maze(const OutdoorData& data, const MazeDef& maze, int level, int level_w,
                                                   int level_h, d2d::rules::Rng seed, int difficulty,
                                                   std::vector<std::string>& notes, int court_file, std::array<int, 4>* court) {
    // court: the Barracks' (28) level 27 {x, y, w, h} and its file in, the Barracks' own rect out (FUN_00673120);
    // the River of Flame's (107) the Chaos Sanctuary's rect in, its own out (FUN_00673320).
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
        rooms.push_back(std::move(room));
        return int(rooms.size()) - 1;
    };
    auto add = [&](int index) { list.insert(list.begin(), index); };                   // FUN_0066b970
    auto gap = [&](const Room& first, const Room& second) {
        return std::pair{ first.x < second.x ? second.x - first.width - first.x : first.x - second.width - second.x, first.y < second.y ? second.y - first.height - first.y : first.y - second.height - second.y };
    };
    // FUN_00670880: beside room `at` on `side` (0 left, 1 above, 2 right, 3
    // below; 4..7 the corners: up-left, up-right, down-right, down-left), clear of every room but `at`.
    auto place = [&](int index, int next_to, int side) {
        auto& room = rooms[std::size_t(index)];
        const auto& neighbour = rooms[std::size_t(next_to)];
        const bool left = side == 0 || side == 4 || side == 7, right = side == 2 || side == 5 || side == 6;
        const bool above = side == 1 || side == 4 || side == 5, down = side == 3 || side == 6 || side == 7;
        room.x = left ? neighbour.x - neighbour.width : right ? neighbour.x + neighbour.width : neighbour.x;
        room.y = above ? neighbour.y - neighbour.height : down ? neighbour.y + neighbour.height : neighbour.y;
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
        int def = maze_base(maze.type) + mask, file = -1;
        if (maze.type == 14 || maze.type == 15) {                  // the harem / cellars: a table, 0 none
            def = kHaremDefs[std::size_t(mask)][maze.type == 14 ? 0 : 1];
            if (level == level_ids::kPalaceCellarLevel1 && def == 0x169) file = 2;
            if (level == level_ids::kPalaceCellarLevel3 && (def == 0x169 || def == 0x167)) file = 3;
        } else if (maze.type == 32 || maze.type == 35) {           // act 5's temple and hell levels: a table, 0 none
            def = (maze.type == 32 ? kTempleDefs : kHellDefs)[std::size_t(mask)];
        } else if (maze.type == 23) {                              // the spider caves: a table, 0 none
            def = kSpiderDefs[std::size_t(mask)];
            if (level == level_ids::kSpiderCave && def == 0x296) def = 0x298;
            if (level == level_ids::kSpiderCavern && def == 0x295) def = 0x297;
        }
        if (def == 0) return;
        room.def = def;
        room.file = file;
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
    // The Frozen River, Drifter Cavern and Icy Cellar: the first room alone,
    // the Frozen River's def on a roll of the level seed (rand(2)).
    const bool one_room = maze.type == 33 && (level == level_ids::kFrozenRiver || level == level_ids::kDrifterCavern || level == level_ids::kIcyCellar);
    if (one_room) {
        auto& room = rooms[std::size_t(first)];
        room.def = level == level_ids::kFrozenRiver ? (seed(2) != 0 ? 0x40e : 0x40f) : level == level_ids::kDrifterCavern ? 0x410 : 0x411;
        room.special = true;
        room.file = -1;
    }

    // Jail and Barracks: a ring of 2x2 rooms round the first (FUN_00670f60(2):
    // above, left, below, closed back on the first). Catacombs: arms off the
    // first, which becomes special (FUN_006714d0): level 34 all four sides,
    // else a roll picks above / below or left / right.
    // The ring (FUN_00670f60(n)): n - 1 rooms up, n - 1 left, n - 1 down,
    // n - 2 right, each from the last one placed (a room that won't fit is
    // freed), then the last linked right to the first. 2 but the Kurast
    // Sewers' first level, 5, whose ring then has four rooms made special.
    bool grows = true;
    if (maze.type == 7 || maze.type == 8 || maze.type == 13 || maze.type == 14 || maze.type == 15 || maze.type == 18
        || maze.type == 22 || maze.type == 23 || maze.type == 24 || maze.type == 25 || maze.type == 32
        || (maze.type == 33 && !one_room)) {
        grows = maze.type != 14 && maze.type != 15 && maze.type != 23 && maze.type != 32;   // the harem, cellars, spider caves and temple: the ring alone
        const int size = level == level_ids::kKurastSewersLevel1 ? 5 : 2;
        int last = first;
        for (const auto& [side, count] : { std::pair{ 1, size - 1 }, std::pair{ 0, size - 1 }, std::pair{ 3, size - 1 }, std::pair{ 2, size - 2 } })
            for (int step = 0; step < count; ++step)
                if (const int placed = arm(last, side); placed >= 0) last = placed;
        link(last, first, 2);
        set_def(last);
        set_def(first);
        if (level == level_ids::kKurastSewersLevel1)
            for (const auto& [from, to_room] : kSewer92Specials) {
                const auto found = std::ranges::find_if(list, [&](int index) { return !rooms[std::size_t(index)].special && rooms[std::size_t(index)].def == from; });
                if (found == list.end()) continue;                  // game.exe asserts here; not seen in a 500k-maze sweep (bugs.md #39)
                auto& room = rooms[std::size_t(*found)];
                room.special = true; room.def = to_room; room.file = -1;
            }
    } else if (maze.type == 10) {
        const auto& sides = level == level_ids::kCatacombsLevel1 ? std::vector{ 1, 2, 3, 0 } : seed.next() & 1 ? std::vector{ 0, 2 } : std::vector{ 1, 3 };
        for (const int side : sides) arm(first, side);
        auto& room = rooms[std::size_t(first)];
        room.def = level == level_ids::kCatacombsLevel1 ? 0x122 : sides.front() == 0 ? 0x120 : 0x121;
        room.special = true;
        room.file = -1;
    } else if (maze.type == 19) {
        // The Arcane Sanctuary (FUN_006719f0): a rotation roll, then four
        // arms off the first room, one by side u (0..3): two rooms on u, one
        // on u + 3, four on u, one on u + 1 (A), a dead end on u from A, one
        // on u + 1 from A (B), two on u + 2 (C), a dead end on u + 3 from C,
        // two more on u + 2. The 13 kept rooms of each arm take file
        // (arm + rotation) & 3, the first room 4. The dead ends keep no file
        // (-1): the preset step rolls theirs, rand(1) = 0 (their one-link
        // defs, 510 / 511 / 513 / 517, have Files 1), a seed step as game.exe's.
        grows = false;
        const int rotation = int(seed.next() & 3);
        std::array<int, 60> kept{};
        kept.fill(-1);
        auto step = [&](int from, int side) { return from < 0 ? -1 : arm(from, side); };   // game.exe goes on from a null room; not seen in a 500k-maze sweep (bugs.md #39)
        for (int arm_index = 0; arm_index < 4; ++arm_index) {
            const int side = arm_index & 3, next = (arm_index + 1) & 3, back = (arm_index + 2) & 3, before = (arm_index + 3) & 3;
            auto* slot = &kept[std::size_t(arm_index * 15)];
            slot[0] = step(first, side);
            slot[1] = step(slot[0], side);
            slot[2] = step(slot[1], before);
            for (int index = 3; index <= 6; ++index) slot[index] = step(slot[index - 1], side);
            slot[7] = step(slot[6], next);
            step(slot[7], side);
            slot[9] = step(slot[7], next);
            slot[10] = step(slot[9], back);
            slot[11] = step(slot[10], back);
            step(slot[11], before);
            slot[13] = step(slot[11], back);
            slot[14] = step(slot[13], back);
        }
        for (std::size_t index = 0; index < kept.size(); ++index)
            if (kept[index] >= 0) rooms[std::size_t(kept[index])].file = (int(index) / 15 + rotation) & 3;
        rooms[std::size_t(first)].file = 4;
    } else if (maze.type == 17 || maze.type == 34) {
        int def = 0x1e0;                                            // the Claw Viper Temple's second level: its one room
        if (level == level_ids::kClawViperTempleLevel2) grows = false;
        else {                                                      // FUN_006718c0: three arms off the first, the entrance faces the side left
            int side = int(seed.next() & 3);
            for (int arm_index = 0; arm_index < 3; ++arm_index, side = (side + 1) & 3) arm(first, side);
            def = (maze.type == 17 ? kTombEntrance : kWorldstoneEntrance)[std::size_t(side)];
        }
        auto& room = rooms[std::size_t(first)];
        room.def = def;
        room.special = true;
        room.file = -1;
    } else if (maze.type == 35) {
        // The hell levels (FUN_00671430): two rooms off the first by a roll
        // of its seed (FUN_00670de0), then 836 on every side of every room
        // (FUN_00671320, as the River of Flame's fill).
        grows = false;
        const auto& arms = kHellArms[std::size_t(rooms[std::size_t(first)].seed.next() & 3)];
        for (const auto& hell_arm : arms) {
            const int index = alloc(maze.width, maze.height);
            if (!place(index, first, hell_arm.side)) { rooms.pop_back(); continue; }
            link(first, index, hell_arm.side);
            add(index);
            set_def(first);
            auto& room = rooms[std::size_t(index)];
            room.special = true; room.def = hell_arm.def; room.file = hell_arm.file;
        }
        const auto listed = list;   // NOLINT(performance-unnecessary-copy-initialization): a copy, add() grows list
        for (const int other_index : listed)
            for (int side = 0; side < 8; ++side) {
                const int index = alloc(maze.width, maze.height);
                if (!place(index, other_index, side)) continue;
                link(other_index, index, side);
                add(index);
                auto& room = rooms[std::size_t(index)];
                room.special = true; room.def = 836; room.file = -1;
            }
    }
    if (one_room) grows = false;

    // Grow (FUN_00671210).
    // FUN_00671210: the boss tomb grows three times the rooms, the staff tomb twice.
    const int want = !grows ? 0 : maze.rooms[std::size_t(std::clamp(difficulty, 0, 2))] * (level == maze.boss_tomb ? 3 : 1) * (level == maze.staff_tomb ? 2 : 1);
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
    std::optional<std::pair<int, int>> river_shift;     // FUN_00673320's move, absolute
    // Special rooms (FUN_00672550 caves, FUN_00672610 crypts -> FUN_006724e0 / FUN_00670eb0).
    {
        // The turn: the Barracks' court file; the tombs' from the entrance
        // (FUN_00672be0); the sewers' after a roll for level 47's last side
        // (FUN_00672810); none for the harem, cellars and Claw Viper 2.
        int sewer_side = 1, turn = 0;
        const bool specials = maze.type != 14 && maze.type != 15 && maze.type != 23 && maze.type != 28 && maze.type != 35 && !one_room
                              && !(maze.type == 17 && level == level_ids::kClawViperTempleLevel2);
        if (maze.type == 7) turn = std::max(court_file, 0);
        else if (maze.type == 17) {
            for (const int other_index : list)
                if (const int def = rooms[std::size_t(other_index)].def; def >= 0x1ad) { turn = def == 0x1bc ? 3 : def == 0x1bd ? 1 : def == 0x1be ? 0 : 2; break; }
        } else if (maze.type == 13) {
            sewer_side = int(seed.next() & 1) * 2 | 1;
            turn = int(seed.next() & 3);
        } else if (maze.type == 32) turn = int(seed.next() % 3);    // FUN_00673000
        else if (specials) turn = int(seed.next() & 3);
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
        // The room `pick` prefers (FUN_006723e0 topmost, FUN_00672460
        // rightmost) that a room fits beside on `side` (FUN_00672340's
        // probe, freed): a candidate's only probed when it beats the best.
        auto best_fit = [&](int side, auto&& better) {
            int best = -1;
            for (const int other_index : list) {
                if (best != -1 && !better(rooms[std::size_t(other_index)], rooms[std::size_t(best)])) continue;
                const auto& room = rooms[std::size_t(other_index)];
                if (room.special || std::ranges::find(room.links, side, &std::pair<int, int>::second) != room.links.end()) continue;
                const int probe = alloc(maze.width, maze.height);
                if (place(probe, other_index, side)) best = other_index;
                rooms.pop_back();
            }
            return best;
        };
        // FUN_00670de0: a special room of its own beside `anchor`, no merge.
        auto connect = [&](int anchor, int side, int def, int file = 0) {
            const int index = alloc(maze.width, maze.height);
            if (!place(index, anchor, side)) return -1;
            link(anchor, index, side);
            add(index);
            set_def(anchor);
            auto& room = rooms[std::size_t(index)];
            room.special = true;
            room.def = def;
            room.file = file;
            return index;
        };
        if (maze.type == 28) {                                                      // FUN_00673320, the River of Flame
            // Warp Mesa below the lowest room, BridgeLava above the topmost,
            // two Bridge rooms on up from it (no set_def), the forge swapped
            // in on a roll, then a LavaX room on all eight sides of every
            // room listed so far but the last bridge.
            // A room that doesn't fit leaves game.exe going on from a null room; not seen in a 500k-maze sweep (bugs.md #39), d2d stops.
            const int lowest = best_fit(3, [](const Room& room, const Room& best) { return room.y > best.y; });   // FUN_006724a0
            if (lowest >= 0) connect(lowest, 3, 852, -1);
            const int topmost = best_fit(1, [](const Room& room, const Room& best) { return room.y < best.y; });  // FUN_006723e0
            int bridge = topmost >= 0 ? connect(topmost, 1, 855, -1) : -1;
            for (int step = 0; step < 2 && bridge >= 0; ++step) {
                const int index = alloc(maze.width, maze.height);
                if (!place(index, bridge, 1)) { bridge = -1; break; }
                link(bridge, index, 1);
                add(index);
                auto& room = rooms[std::size_t(index)];
                room.special = true; room.def = 856; room.file = -1;
                bridge = index;
            }
            special(seed.next() & 1 ? Special{ 838, 854, -1, 0 } : Special{ 837, 853, -1, 2 });   // 0x6f0474: Forge W / E
            const auto listed = list;   // NOLINT(performance-unnecessary-copy-initialization): FUN_00671320; a copy, add() grows list
            for (const int other_index : listed) {
                if (other_index == bridge) continue;
                for (int side = 0; side < 8; ++side) {
                    const int index = alloc(maze.width, maze.height);
                    if (!place(index, other_index, side)) continue;
                    link(other_index, index, side);
                    add(index);
                    auto& room = rooms[std::size_t(index)];
                    room.special = true; room.def = 836; room.file = -1;
                }
            }
            if (bridge >= 0 && court) {                    // the last bridge under the sanctuary's entry: x + 2w, y + h
                const auto [chaos_x, chaos_y, chaos_w, chaos_h] = *court;
                const auto& last = rooms[std::size_t(bridge)];
                river_shift = { 2 * last.width - last.x + chaos_x, chaos_h - last.y + chaos_y };
            }
        } else if (maze.type == 13) {                                                      // FUN_00672810
            if (level == level_ids::kSewersLevel1) {
                // Up from the topmost room two rooms and a special (0x14d) to
                // their left; right from the rightmost two rooms, a special
                // (0x150) above or below, and a room past it.
                // A step that doesn't fit leaves game.exe's room pointer null; not seen in a 500k-maze sweep (bugs.md #39), d2d stops the arm.
                int room_at = best_fit(1, [](const Room& room, const Room& best) { return room.y < best.y; });
                for (int step = 0; step < 2 && room_at >= 0; ++step) room_at = arm(room_at, 1);
                if (room_at >= 0) connect(room_at, 0, 0x14d);
                room_at = best_fit(2, [](const Room& room, const Room& best) { return best.x < room.x; });
                for (int step = 0; step < 2 && room_at >= 0; ++step) room_at = arm(room_at, 2);
                if (room_at >= 0) room_at = connect(room_at, sewer_side, 0x150);
                if (room_at >= 0) {
                    const int index = alloc(maze.width, maze.height);
                    if (place(index, room_at, sewer_side)) { link(room_at, index, sewer_side); add(index); set_def(index); }
                }
                special(kAct2Specials[1][std::size_t(turn)]);
            } else {
                special(kAct2Specials[0][std::size_t(turn)]);
                if (level == level_ids::kSewersLevel2) { special(kAct2Specials[2][std::size_t(turn)]); special(kAct2Specials[1][std::size_t(turn)]); }
                if (level == level_ids::kSewersLevel3) special(kAct2Specials[3][std::size_t(turn)]);
                if (level == level_ids::kAncientTunnels) special(kAct2Specials[4][std::size_t(turn)]);
            }
        } else if (maze.type == 17 && specials) {                                   // FUN_00672be0
            const bool tomb = level >= level_ids::kFirstTalRashasTomb && level <= level_ids::kLastTalRashasTomb;
            if (level >= level_ids::kStonyTombLevel1 && level <= level_ids::kClawViperTempleLevel1) special(kAct2Specials[5][std::size_t(turn)]);
            if (level == level_ids::kHallsOfTheDeadLevel2) special(kAct2Specials[6][std::size_t(turn)]);
            if (level == level_ids::kStonyTombLevel2 || level == level_ids::kClawViperTempleLevel2) special(kAct2Specials[7][std::size_t(turn)]);
            if (tomb && level != maze.boss_tomb) special(kAct2Specials[7][std::size_t(turn)]);
            if (level == level_ids::kStonyTombLevel2) special(kAct2Specials[8][std::size_t(turn)]);
            if (level == level_ids::kHallsOfTheDeadLevel3) special(kAct2Specials[9][std::size_t(turn)]);
            if (level == level_ids::kStonyTombLevel2 || level == level_ids::kClawViperTempleLevel2) special(kAct2Specials[10][std::size_t(turn)]);
            if (level == maze.boss_tomb) special(kAct2Specials[11][std::size_t(turn)]);
            if (level == maze.staff_tomb) special(kAct2Specials[12][std::size_t(turn)]);
        } else if (maze.type == 32) {                                               // FUN_00673000
            if (level != level_ids::kHallsOfVaught) special(kTempleFirst[std::size_t(turn)]);
            if (level == level_ids::kHallsOfPain) special(kTempleSecond[std::size_t(turn)]);
        } else if (maze.type == 33 && specials) {                                   // FUN_00673530
            for (int table = 2; table <= 4; ++table) special(kAct5Specials[std::size_t(table)][std::size_t(turn)]);
            if (level == level_ids::kGlacialTrail) special(kAct5Specials[5][std::size_t(turn)]);
            if (level == level_ids::kCrystallinePassage || level == level_ids::kGlacialTrail || level == level_ids::kAncientsWay)
                special(kAct5Specials[6][std::size_t(turn)]);
        } else if (maze.type == 34) {                                               // FUN_006730b0
            special(kAct5Specials[0][std::size_t(turn)]);
            if (level == level_ids::kWorldstoneKeepLevel2) special(kAct5Specials[1][std::size_t(turn)]);
        } else if (maze.type == 24 || maze.type == 25) {                          // FUN_00672ea0 / FUN_00672f00
            special(kAct3Specials[maze.type == 24 ? 0 : 2][std::size_t(turn)]);
            special(kAct3Specials[maze.type == 24 ? 1 : 3][std::size_t(turn)]);
        } else if (maze.type == 22) {                                               // FUN_00672f60
            special(kAct3Specials[4][std::size_t(turn)]);
            if (level == level_ids::kDuranceOfHateLevel2) { special(kAct3Specials[5][std::size_t(turn)]); special(kAct3Specials[6][std::size_t(turn)]); }
            if (level == level_ids::kDuranceOfHateLevel1) special(kAct3Specials[6][std::size_t(turn)]);
        } else if (maze.type == 19) {                                               // FUN_00672e50
            special(kArcaneSpecials[std::size_t(turn)]);
        } else if (maze.type == 18) {                                               // FUN_00672dc0
            if (level == level_ids::kMaggotLairLevel3) { special(kLair64First); turn = 3; special(kLair64Second); }
            else special(kAct2Specials[14][std::size_t(turn)]);
            special(kAct2Specials[13][std::size_t(turn)]);
        } else if (maze.type == 8) {                                                       // FUN_006726d0
            special(kSpecials[10][std::size_t(turn)]);
            special(kSpecials[level == level_ids::kJailLevel1 ? 13 : level == level_ids::kJailLevel2 ? 14 : 11][std::size_t(turn)]);
            if (level != level_ids::kJailLevel3) special(kSpecials[12][std::size_t(turn)]);
        } else if (maze.type == 10) {                                               // FUN_006727a0
            special(kSpecials[15][std::size_t(turn)]);
            if (level == level_ids::kCatacombsLevel2) special(kSpecials[16][std::size_t(turn)]);
        } else if (maze.type == 7 && court_file >= 0 && court_file < 3) {          // FUN_00673120
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
            // No room fits the court: game.exe goes on from a null room; not seen in a 500k-maze sweep (bugs.md #39), d2d stops.
            if (anchor < 0) { notes.push_back("drlg: no room fits the Barracks' court"); return {}; }
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
        } else if (maze.type == 3) {
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
    if (river_shift && court) {                         // the River of Flame: its rect is its rooms' box, moved (FUN_00642520)
        auto& [river_x, river_y, width, height] = *court;
        river_x = min_x + river_shift->first;
        river_y = min_y + river_shift->second;
        width = height = 0;
        for (const int other_index : list) {
            width = std::max(width, rooms[std::size_t(other_index)].x + rooms[std::size_t(other_index)].width);
            height = std::max(height, rooms[std::size_t(other_index)].y + rooms[std::size_t(other_index)].height);
        }
    }
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
    if (level != level_ids::kDenOfEvil && maze_themes(maze.type)) {
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
        else if (maze_rotates(maze.type) && room.def > rotation_base(maze.type) && room.def < rotation_base(maze.type) + 0x10) {
            auto rot = rotation.find(room.def);
            if (rot == rotation.end()) rot = rotation.emplace(room.def, seed(preset.files)).first;
            file = rot->second = preset.files ? (rot->second + 1) % preset.files : 0;
        }
        preset_rooms(data, preset, room.def, file, room.x, room.y, room.width, room.height, seed, out);
    }
    return out;
}

}  // namespace d2d::drlg
