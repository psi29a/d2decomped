// drlg-dump <mpq dir> <map seed> [level] — our generator's level in the
// same text form as tools/emu/drlg.py prints game.exe's, for diffing.
// drlg-dump <mpq dir> <first>-<last> <level> <out dir> writes <seed>.txt each.
// A trailing `tiles` adds every room's tiles (as drlg.py <seed> <level> tiles).
#include <maze.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <mpq.hpp>
#include <rules.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <room_tiles.hpp>
#include <tile_pick.hpp>
#include <txt.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::drlg;

static bool g_units = false;                            // print rooms' units instead of their tiles
static bool g_seeds = false;                            // print rooms' room1 seeds instead of their tiles
static bool g_objgroups = false;                        // print per-room object-group picks (FUN_00552610)
static d2d::txt::Table g_objgroup_table;                // objgroup.txt, loaded once
static std::array<std::uint8_t, 8> g_obj_group{}, g_obj_prob{};   // Levels ObjGrp / ObjPrb of the target level

static std::vector<d2d::rules::ObjGroup> load_obj_groups(const d2d::txt::Table& table) {
    std::vector<d2d::rules::ObjGroup> groups;
    auto num = [](std::string_view text) { return std::atoi(std::string(text).c_str()); };
    int max_offset = 0;
    for (std::size_t row_index = 0; row_index < table.size(); ++row_index) max_offset = std::max(max_offset, num(table.get(row_index, "Offset")));
    groups.assign(std::size_t(max_offset) + 1, {});
    for (std::size_t row_index = 0; row_index < table.size(); ++row_index) {
        const int off = num(table.get(row_index, "Offset"));
        if (off < 0 || off > max_offset) continue;
        auto& group = groups[std::size_t(off)];
        for (int i = 0; i < 8; ++i) {
            group.id[std::size_t(i)]      = num(table.get(row_index, "ID" + std::to_string(i)));
            group.density[std::size_t(i)] = std::uint8_t(num(table.get(row_index, "DENSITY" + std::to_string(i))));
            group.weight[std::size_t(i)]  = std::uint8_t(num(table.get(row_index, "PROB" + std::to_string(i))));
        }
    }
    return groups;
}

static std::string dump(const OutdoorAssets& assets, std::uint32_t seed, int id, const RoomDt1s* dt1s = nullptr) {
    std::ostringstream out;
    char buffer[256];
#define pf(...) (std::snprintf(buffer, sizeof buffer, __VA_ARGS__), out << buffer)   // a literal format each time (-Wformat-security)
    if (const auto row = level_row(assets.levels, id); row && to_int(assets.levels.get(*row, "DrlgType")) == 1) {
        const int width = to_int(assets.levels.get(*row, "SizeX")), height = to_int(assets.levels.get(*row, "SizeY"));
        pf("level %d at %d,%d size %dx%d\n", id, to_int(assets.levels.get(*row, "OffsetX")), to_int(assets.levels.get(*row, "OffsetY")), width, height);
        MazeDef maze;
        for (std::size_t row_index = 0; row_index < assets.lvl_maze.size(); ++row_index)
            if (to_int(assets.lvl_maze.get(row_index, "Level"), -1) == id) {
                // ponytail: LvlMaze.txt has one Rooms column; game.exe's .bin has one per difficulty
                maze.rooms.fill(to_int(assets.lvl_maze.get(row_index, "Rooms")));
                maze.width = to_int(assets.lvl_maze.get(row_index, "SizeX"));
                maze.height = to_int(assets.lvl_maze.get(row_index, "SizeY"));
                maze.merge = to_int(assets.lvl_maze.get(row_index, "Merge"));
            }
        std::vector<std::string> notes;
        auto rooms = generate_maze(assets.data, maze, id, width, height, level_seed(seed, id), 0, notes);
        auto sorted = rooms;
        std::ranges::sort(sorted, {}, [](const auto& room) { return std::tuple(room.y, room.x); });
        pf("rooms %zu\n", sorted.size());
        for (const auto& room : sorted)
            pf("%d,%d %dx%d kind %d seed %08x def %d file %d at %d,%d\n", room.x, room.y, room.width, room.height, room.kind, room.seed, room.def, room.file, room.preset_x, room.preset_y);
        if (dt1s) {
            auto built = level_room_tiles(rooms, {}, assets.data, *dt1s, id, warp_slots(assets, id), notes);
            std::ranges::sort(built, {}, [](const auto& room) { return std::tuple(room.y, room.x); });
            if (g_seeds) {
                for (const auto& room : built) pf("room1 %d,%d seed %08x\n", room.x, room.y, room.room1_seed);
                built.clear();
            }
            if (g_units) {
                for (const auto& room : built) {
                    if (room.units.empty()) continue;
                    pf("units %d,%d:", room.x, room.y);
                    for (const auto& unit : room.units) pf(" %d:%d m%d %d,%d f%x", unit.type, unit.id, unit.mode, unit.x, unit.y, unit.flags);
                    pf("\n");
                }
                built.clear();
            }
            for (const auto& room : built) {
                const auto found = assets.data.presets.find(room.seed->def);
                const auto mask = found != assets.data.presets.end() ? found->second.dt1_mask : 0u;
                pf("room %d,%d mask %x dt1s", room.x, room.y, mask);
                for (const auto* file : room_dt1_list(mask, *dt1s)) pf(" %s", file->name.c_str());
                pf("\n");
                for (int layer : { 0, 1, 2 })
                    for (const auto& tile : room.tiles)
                        if (tile.layer == layer)
                            pf(" %s %d,%d o%d %s:%d\n", layer == 0 ? "wall" : layer == 1 ? "floor" : "shadow", tile.x - room.x, tile.y - room.y,
                               tile.orient, tile.file ? tile.file->name.c_str() : "?", tile.index);
            }
        }
        for (const auto& note : notes) std::fprintf(stderr, "not implemented: %s\n", note.c_str());
        return out.str();
    }
    const auto layout = act1_from_map_seed(level_defs(assets.levels), seed);
    if (std::getenv("DRLG_LAYOUT"))
        for (const auto& placement : layout) std::fprintf(stderr, "layout %d at %d,%d %dx%d dir %d flip %d flags %x\n", placement.level, placement.x, placement.y, placement.width, placement.height, placement.dir, placement.flip, placement.flags);
    const auto level = outdoor_level(assets.levels, layout, id);
    const auto outdoor = generate_outdoor(assets.data, level, level_seed(seed, id));
    pf("level %d at %d,%d size %dx%d\n", id, level.rect.x, level.rect.y, level.rect.width, level.rect.height);
    pf("flags %x\n", outdoor.flags);
    for (auto [name, grid] : { std::pair{ "g04", &outdoor.g04 }, { "g18", &outdoor.g18 }, { "g2c", &outdoor.g2c } }) {
        pf("%s %dx%d\n", name, outdoor.cells_wide, outdoor.cells_high);
        for (int y = 0; y < outdoor.cells_high; ++y)
            for (int x = 0; x < outdoor.cells_wide; ++x) pf("%x%c", (*grid)[std::size_t(y * outdoor.cells_wide + x)], x + 1 < outdoor.cells_wide ? ' ' : '\n');
    }
    auto rooms = outdoor.rooms;
    std::ranges::sort(rooms, {}, [](const auto& room) { return std::tuple(room.y, room.x); });
    pf("rooms %zu\n", rooms.size());
    for (const auto& room : rooms) pf("%d,%d %dx%d kind %d seed %08x\n", room.x, room.y, room.width, room.height, room.kind, room.seed);
    auto notes = outdoor.notes;
    if (dt1s) {                                         // plain rooms' tiles, as tools/emu/drlg.py tiles
        auto built = level_room_tiles(outdoor.rooms, outdoor.plain, assets.data, *dt1s, id, warp_slots(assets, id), notes);
        std::ranges::sort(built, {}, [](const auto& room) { return std::tuple(room.y, room.x); });
        if (g_seeds) {
            for (const auto& room : built) pf("room1 %d,%d seed %08x\n", room.x, room.y, room.room1_seed);
            built.clear();
        }
        if (g_units) {
            for (const auto& room : built) {
                if (room.units.empty()) continue;
                pf("units %d,%d:", room.x, room.y);
                for (const auto& unit : room.units) pf(" %d:%d m%d %d,%d f%x", unit.type, unit.id, unit.mode, unit.x, unit.y, unit.flags);
                pf("\n");
            }
            built.clear();
        }
        if (g_objgroups) {
            const auto groups = load_obj_groups(g_objgroup_table);
            d2d::rules::LevelMon level_mon;
            level_mon.obj_group = g_obj_group;
            level_mon.obj_prob  = g_obj_prob;
            pf("objgrp");
            for (const auto value : g_obj_group) pf(" %d", value);
            pf(" objprb");
            for (const auto value : g_obj_prob) pf(" %d", value);
            pf("\nrooms %zu\n", built.size());
            for (std::size_t room_index = 0; room_index < built.size(); ++room_index) {
                const auto& room = built[room_index];
                d2d::rules::Rng room_seed{ room.room1_seed };
                const auto picks = d2d::rules::place_object_groups(level_mon, groups, room_seed, int(room_index), int(built.size()));
                pf("room %d,%d seed %08x post %08x picks", room.x, room.y, room.room1_seed, room_seed.low);
                if (picks.empty()) pf(" -");
                else for (const auto& pick : picks) pf(" obj%dd%d", pick.object_id, int(pick.density));
                pf("\n");
            }
            built.clear();
        }
        for (const auto& room : built) {
            const auto found = assets.data.presets.find(room.seed->def);
            const auto mask = room.plain ? room.plain->dt1_mask : found != assets.data.presets.end() ? found->second.dt1_mask : 0u;
            pf("room %d,%d mask %x dt1s", room.x, room.y, mask);
            for (const auto* file : room_dt1_list(mask, *dt1s)) pf(" %s", file->name.c_str());
            pf("\n");
            for (int layer : { 0, 1, 2 })
                for (const auto& tile : room.tiles)
                    if (tile.layer == layer)
                        pf(" %s %d,%d o%d %s:%d\n", layer == 0 ? "wall" : layer == 1 ? "floor" : "shadow", tile.x - room.x, tile.y - room.y,
                           tile.orient, tile.file ? tile.file->name.c_str() : "?", tile.index);
        }
    }
    for (const auto& note : notes) std::fprintf(stderr, "not implemented: %s\n", note.c_str());
    return out.str();
#undef pf
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: drlg-dump <mpq dir> <map seed> [level]\n"); return 2; }
    const fs::path dir = argv[1];
    const std::string range = argv[2];
    const int id = argc > 3 ? std::atoi(argv[3]) : 2;
    d2d::mpq::Stack mpqs;
    if (const char* patch = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(patch);
    for (const char* name : { "d2exp.mpq", "d2data.mpq" }) if (fs::exists(dir / name)) mpqs.push(dir / name);
    OutdoorAssets assets;                                    // not const: load_room_dt1s adds to it
    load_outdoor_assets(assets, [&](const std::string& path) { return mpqs.try_read(path); });
    g_units = argc > 4 && std::string(argv[argc - 1]) == "units";
    g_seeds = argc > 4 && std::string(argv[argc - 1]) == "seeds";
    g_objgroups = argc > 4 && std::string(argv[argc - 1]) == "objgroups";
    const bool tiles = argc > 4 && (std::string(argv[argc - 1]) == "tiles" || g_units || g_seeds || g_objgroups);
    if (g_objgroups) {
        if (auto bytes = mpqs.try_read(R"(data\global\excel\objgroup.txt)")) g_objgroup_table = d2d::txt::Table(*bytes);
        if (const auto row = level_row(assets.levels, id))
            for (int i = 0; i < 8; ++i) {
                g_obj_group[std::size_t(i)] = std::uint8_t(to_int(assets.levels.get(*row, "ObjGrp" + std::to_string(i))));
                g_obj_prob[std::size_t(i)]  = std::uint8_t(to_int(assets.levels.get(*row, "ObjPrb" + std::to_string(i))));
            }
    }
    RoomDt1s dt1s;
    if (tiles) {
        const auto row = level_row(assets.levels, id);
        dt1s = load_room_dt1s(assets, [&](const std::string& path) { return mpqs.try_read(path); }, row ? to_int(assets.levels.get(*row, "LevelType")) : 0);
        assets.data.dt1s = &dt1s;
    }
    if (const auto dash = range.find('-'); dash != std::string::npos && argc > 4) {
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << dump(assets, seed, id, tiles ? &dt1s : nullptr);
            if (seed == last) break;
        }
    } else {
        std::fputs(dump(assets, std::uint32_t(std::stoul(range, nullptr, 0)), id, tiles ? &dt1s : nullptr).c_str(), stdout);
    }
}
