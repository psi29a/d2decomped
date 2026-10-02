// SPDX-License-Identifier: GPL-3.0-or-later
// The Blood Moor from real game data over many map seeds: a closed
// border, one Den of Evil, roads, grass everywhere else, and the same
// level from the same seed. Prints one level's cells.
#include <drlg.hpp>
#include <maze.hpp>
#include <mpq.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <room_tiles.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::drlg;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env) : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / "Workspace/private/diablo2";
    if (!fs::exists(dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", dir.string().c_str()); return 0; }
    d2d::mpq::Stack mpqs;
    if (const char* patch = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(patch);
    for (const char* name : { "d2exp.mpq", "d2data.mpq" }) if (fs::exists(dir / name)) mpqs.push(dir / name);

    OutdoorAssets assets;
    load_outdoor_assets(assets, [&](const std::string& path) { return mpqs.try_read(path); });
    assert(assets.levels.size() > 100 && assets.data.presets.count(52) && assets.data.subs.size() > 10);
    const auto defs = level_defs(assets.levels);

    std::vector<std::string> notes;
    std::array<std::uint32_t, 4> first{};                       // first map seed per town file
    for (std::uint32_t seed = 1; seed < 200; ++seed)
        if (const int town_index = town_file(act1_from_map_seed(defs, seed)); town_index >= 0 && !first[std::size_t(town_index)]) first[std::size_t(town_index)] = seed;
    std::printf("first seeds for townN1/E1/S1/W1: %u %u %u %u\n", first[0], first[1], first[2], first[3]);
    for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        const auto layout = act1_from_map_seed(defs, seed);
        const auto level = outdoor_level(assets.levels, layout, 2);
        assert(level.rect.level == 2 && level.rect.width + level.rect.height == 152);
        assert(level.neighbours.size() == 2);                       // Cold Plains and the town
        const auto outdoor = generate_outdoor(assets.data, level, level_seed(seed, 2));
        for (const auto& note : outdoor.notes) if (std::ranges::find(notes, note) == notes.end()) notes.push_back(note);
        auto cell = [&](const std::vector<std::uint32_t>& cells, int x, int y) { return cells[std::size_t(y * outdoor.cells_wide + x)]; };
        auto print = [&] {
            std::printf("seed %u: Blood Moor %dx%d at (%d,%d), flags 0x%x, town at (%d,%d)\n", seed, level.rect.width, level.rect.height,
                        level.rect.x, level.rect.y, level.rect.flags, level.town.x, level.town.y);
            for (int y = 0; y < outdoor.cells_high; ++y) {
                for (int x = 0; x < outdoor.cells_wide; ++x) {
                    const auto def = cell(outdoor.g04, x, y), flags = cell(outdoor.g2c, x, y);
                    if (def) std::printf("%3u", def);
                    else std::printf("  %c", flags & 0x200 ? '+' : flags & 0x100 ? ' ' : flags & 0x80 ? '=' : '.');
                }
                std::puts("");
            }
        };
        if (seed == 1 || seed == 3) {
            print();
            int road = 0;
            for (const auto& tile : outdoor.tiles.floors()[0].cells) road += (tile.prop1 & 0x80) && tile.style == 0 && tile.sequence;
            std::printf("road tiles %d; roads:", road);
            for (const auto& road_path : outdoor.roads) { std::printf(" ["); for (auto [x, y] : road_path) std::printf(" %d,%d", x - level.rect.x, y - level.rect.y); std::printf(" ]"); }
            std::puts("");
        }
        int dens = 0;
        for (int y = 0; y < outdoor.cells_high; ++y)
            for (int x = 0; x < outdoor.cells_wide; ++x) {
                const auto flags = cell(outdoor.g2c, x, y);
                if (x == 0 || y == 0 || x == outdoor.cells_wide - 1 || y == outdoor.cells_high - 1) { if (!(flags & 0x301)) { std::printf("seed %u open at (%d,%d)\n", seed, x, y); print(); } assert(flags & 0x301); }   // closed but for the town side
                dens += cell(outdoor.g04, x, y) == 52;
            }
        assert(dens == 1);
        assert(!outdoor.roads.empty());
        // Every tile has a floor or a preset's.
        int bare = 0;
        for (const auto& tile : outdoor.tiles.floors()[0].cells) bare += (tile.prop1 == 0);
        assert(bare < outdoor.tiles.width() * outdoor.tiles.height() / 4);
        const auto again = generate_outdoor(assets.data, level, level_seed(seed, 2));
        assert(again.g04 == outdoor.g04 && again.g2c == outdoor.g2c && again.roads == outdoor.roads);
    }
    for (const auto& note : notes) std::printf("not implemented: %s\n", note.c_str());

    // Values game.exe itself produces for map seed 3, read out under the
    // emulator (tools/emu/drlg.py 3 2 tiles, drlg.py 3 8); diff_drlg.py
    // checks thousands of seeds this way.
    {
        const auto read = [&](const std::string& path) { return mpqs.try_read(path); };
        const auto dt1s = load_room_dt1s(assets, read, 2);
        assets.data.dt1s = &dt1s;
        const auto level = outdoor_level(assets.levels, act1_from_map_seed(defs, 3), 2);
        const auto outdoor = generate_outdoor(assets.data, level, level_seed(3, 2));
        assert(outdoor.rooms.size() == 83 && outdoor.rooms.front().x == 0 && outdoor.rooms.front().seed == 0x32de6615);
        std::vector<std::string> dnotes;
        const auto built = level_room_tiles(outdoor.rooms, outdoor.plain, assets.data, dt1s, 2, warp_slots(assets, 2), dnotes);
        auto has = [&](int room_x, int room_y, int layer, int x, int y, const std::string& want) {    // any tile of the layer there
            for (const auto& room : built)
                if (room.x == room_x && room.y == room_y)
                    for (const auto& tile : room.tiles)
                        if (tile.layer == layer && tile.x == room_x + x && tile.y == room_y + y && tile.file
                            && tile.file->name + ":" + std::to_string(tile.index) == want) return true;
            return false;
        };
        assert(has(24, 8, 1, 0, 0, "floor.dt1:39"));                             // a plain room's grass
        assert(has(32, 8, 0, 6, 2, "trees.dt1:63"));                             // a stamped tree
        assert(has(32, 8, 2, 5, 1, "trees.dt1:67"));                             // its shadow, picked at stamp time
        assert(has(64, 24, 1, 2, 3, "cavedr.dt1:27"));                           // the Den entrance's lit floor
        assets.data.dt1s = nullptr;

        MazeDef maze;
        maze.rooms.fill(1); maze.width = maze.height = 24; maze.merge = 500;                         // LvlMaze "Act 1 - Cave 1"
        auto den = generate_maze(assets.data, maze, 8, 200, 200, level_seed(3, 8), 0, dnotes);
        assert(den.size() == 27);
        auto room_at = [&](int x, int y) { for (const auto& room : den) if (room.x == x && room.y == y) return room; return Outdoor::RoomSeed{}; };
        assert(room_at(24, 0).def == 97 && room_at(24, 0).file == 0 && room_at(24, 0).seed == 0x8d8dcf44);    // the Den's own room
        assert(room_at(0, 24).def == 84 && room_at(0, 24).file == 1);                                    // the entrance
        assert(room_at(24, 24).def == 61 && room_at(24, 24).seed == 0x775aabb3);                         // Cave NW
        // Units (drlg.py 3 8 units): Corpsefire (superunique 40 = MonStats rows + 40) in room (40, 8).
        const auto den_dt1s = load_room_dt1s(assets, read, 3);
        const auto den_rooms = level_room_tiles(den, {}, assets.data, den_dt1s, 8, warp_slots(assets, 8), dnotes);
        bool corpsefire = false;
        for (const auto& room : den_rooms)
            for (const auto& unit : room.units)
                corpsefire |= room.x == 40 && room.y == 8 && unit.type == 1 && unit.id == assets.data.ids.monstats + 40 && unit.x == 20 && unit.y == 5;
        assert(assets.data.ids.monstats == 734 && assets.data.ids.superuniques == 66 && corpsefire);
        bool flavie = false;                                                    // the Blood Moor's way in
        for (const auto& room : built) for (const auto& unit : room.units) flavie |= unit.type == 1 && unit.id == 266;
        assert(flavie);
    }
    std::puts("test_outdoor: ok");
}
