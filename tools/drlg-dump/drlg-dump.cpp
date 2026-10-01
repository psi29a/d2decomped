// drlg-dump <mpq dir> <map seed> [level] — our generator's level in the
// same text form as tools/emu/drlg.py prints game.exe's, for diffing.
// drlg-dump <mpq dir> <first>-<last> <level> <out dir> writes <seed>.txt each.
// A trailing `tiles` adds every room's tiles (as drlg.py <seed> <level> tiles);
// `monsters` prints each room's population (as monsters.py <seed> <level>;
// $DIFFICULTY 0..2).
// drlg-dump <mpq dir> <seed> <level> [<out dir>] objgroups: each room's random
// object groups, as tools/emu/objgroups.py prints game.exe's (a range: to <out dir>).
#include <gamedata.hpp>
#include <gamedata_load.hpp>
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
static std::string dump(const OutdoorAssets& assets, std::uint32_t seed, int id, const RoomDt1s* dt1s = nullptr) {
    std::ostringstream out;
    char buffer[256];
#define pf(...) (std::snprintf(buffer, sizeof buffer, __VA_ARGS__), out << buffer)   // a literal format each time (-Wformat-security)
    if (const auto row = level_row(assets.levels, id); row && (to_int(assets.levels.get(*row, "DrlgType")) == 1 || to_int(assets.levels.get(*row, "DrlgType")) == 2)) {
        const bool preset_level = to_int(assets.levels.get(*row, "DrlgType")) == 2;
        const int width = to_int(assets.levels.get(*row, "SizeX")), height = to_int(assets.levels.get(*row, "SizeY"));
        const auto [origin_x, origin_y] = level_origin(assets.levels, *row);
        std::array<int, 4> rect{ origin_x, origin_y, width, height };
        MazeDef maze;
        for (std::size_t row_index = 0; row_index < assets.lvl_maze.size(); ++row_index)
            if (to_int(assets.lvl_maze.get(row_index, "Level"), -1) == id) {
                // ponytail: LvlMaze.txt has one Rooms column; game.exe's .bin has one per difficulty
                maze.rooms.fill(to_int(assets.lvl_maze.get(row_index, "Rooms")));
                maze.width = to_int(assets.lvl_maze.get(row_index, "SizeX"));
                maze.height = to_int(assets.lvl_maze.get(row_index, "SizeY"));
                maze.merge = to_int(assets.lvl_maze.get(row_index, "Merge"));
            }
        maze.type = to_int(assets.levels.get(*row, "LevelType"));
        std::vector<std::string> notes;
        int court_file = id == 27 || id == 28 ? courtyard_file(act1_from_map_seed(level_defs(assets.levels), seed), act_seed(seed)) : -1;
        if (const auto court = level_row(assets.levels, 27); id == 28 && court) {
            auto court_seed = level_seed(seed, 27);
            court_file = preset_file(assets.data, 27, court_seed, court_file);
            const auto [court_x, court_y] = level_origin(assets.levels, *court);
            rect = { court_x, court_y, to_int(assets.levels.get(*court, "SizeX")), to_int(assets.levels.get(*court, "SizeY")) };
        }
        auto rooms = preset_level ? generate_preset(assets.data, id, width, height, level_seed(seed, id), notes, court_file)
                                  : generate_maze(assets.data, maze, id, width, height, level_seed(seed, id), 0, notes, court_file, &rect);
        pf("level %d at %d,%d size %dx%d\n", id, rect[0], rect[1], rect[2], rect[3]);
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

// Every room of the level populated in (y, x) order on a fresh game
// (d2d::game::populate_level), one line per room that spawned.
static std::string dump_monsters(d2d::game::GameData& game_data, std::uint32_t seed, int id) {
    d2d::game::set_map_seed(game_data, seed);
    const auto* level = game_data.level(id);
    if (!level) return "";
    const char* difficulty = std::getenv("DIFFICULTY");
    auto spawning = d2d::game::start_spawning(game_data, difficulty ? std::atoi(difficulty) : 0);
    const auto order = d2d::game::populate_level(game_data, spawning, *level);
    const auto& spawns = spawning.levels[level].spawns;
    std::ostringstream out;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const auto [room, first] = order[k];
        const std::size_t end = k + 1 < order.size() ? order[k + 1].second : spawns.size();
        if (first == end) continue;
        out << "mon " << level->rooms[room].x << ',' << level->rooms[room].y << ':';
        for (std::size_t i = first; i < end; ++i)
            out << ' ' << spawns[i].type << '@' << spawns[i].x << ',' << spawns[i].y << (spawns[i].dead ? "m12" : "") << '/' << (spawns[i].leader >= 0 ? std::size_t(spawns[i].leader) - first : i - first);
        out << '\n';
    }
    return out.str();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: drlg-dump <mpq dir> <map seed> [level]\n"); return 2; }
    const fs::path dir = argv[1];
    const std::string range = argv[2];
    const int id = argc > 3 ? std::atoi(argv[3]) : 2;
    const bool drops = argc > 4 && std::string(argv[argc - 1]) == "drops";
    if (argc > 4 && (std::string(argv[argc - 1]) == "objgroups" || drops)) {
        // game.exe's rooms populated up to their monsters (place_objects), as
        // tools/emu objgroups.py prints them; a range writes <out dir>/<seed>.txt each.
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 1);
        if (!game) return 1;
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            d2d::game::set_map_seed(*game, seed);
            std::ostringstream out;
            if (const auto level = d2d::game::build_level(*game, *game->builder, id)) {
                char line[80];
                for (std::size_t i = 0; i < level->rooms.size(); ++i) {
                    const auto& group = level->group_rooms[i];
                    std::snprintf(line, sizeof line, "room %d,%d seed %08x post %08x rgn %08x", level->rooms[i].x, level->rooms[i].y, group.pre, group.post, group.rgn);
                    out << line;
                    for (const auto& [object, x, y] : group.made) out << ' ' << object << '@' << x << ',' << y;
                    out << '\n';
                }
                std::snprintf(line, sizeof line, "rgn %08x\n", level->group_rgn);
                out << line;
                // drops: three items at each group object, each landing an item (0x200) the next sees.
                auto grid = level->collision;
                const int width = level->ds1.width() * 5, height = level->ds1.height() * 5;
                auto flags = [&](int x, int y) -> int {
                    return x < 0 || y < 0 || x >= width || y >= height ? 0x27 : grid[std::size_t(y) * std::size_t(width) + std::size_t(x)];
                };
                for (const auto& group : level->group_rooms)
                    for (const auto& [object, x, y] : group.made)
                        for (int k = 0; drops && k < 3; ++k) {
                            const auto [sx, sy] = d2d::game::drop_spot(game->field, x, y, flags);
                            if (flags(sx, sy) != 0x27) grid[std::size_t(sy) * std::size_t(width) + std::size_t(sx)] |= 0x200;
                            out << "drop " << sx << ',' << sy << '\n';
                        }
            }
            if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << out.str();
            else std::fputs(out.str().c_str(), stdout);
            if (seed == last) break;
        }
        return 0;
    }
    if (std::string(argv[argc - 1]) == "monsters") {
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game_data = d2d::game::load_game_data(dir, patch ? patch : "", 0);
        if (!game_data) return 1;
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            const auto text = dump_monsters(*game_data, seed, id);
            if (dash == std::string::npos) std::fputs(text.c_str(), stdout);
            else std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << text;
            if (seed == last) break;
        }
        return 0;
    }
    d2d::mpq::Stack mpqs;
    if (const char* patch = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(patch);
    for (const char* name : { "d2exp.mpq", "d2data.mpq" }) if (fs::exists(dir / name)) mpqs.push(dir / name);
    OutdoorAssets assets;                                    // not const: load_room_dt1s adds to it
    load_outdoor_assets(assets, [&](const std::string& path) { return mpqs.try_read(path); });
    g_units = argc > 4 && std::string(argv[argc - 1]) == "units";
    g_seeds = argc > 4 && std::string(argv[argc - 1]) == "seeds";
    const bool tiles = argc > 4 && (std::string(argv[argc - 1]) == "tiles" || g_units || g_seeds);
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
