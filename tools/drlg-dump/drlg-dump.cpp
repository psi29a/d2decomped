// drlg-dump <mpq dir> <map seed> [level] — our generator's level in the
// same text form as tools/emu/drlg.py prints game.exe's, for diffing.
// drlg-dump <mpq dir> <first>-<last> <level> <out dir> writes <seed>.txt each.
// A trailing `tiles` adds every room's tiles (as drlg.py <seed> <level> tiles);
// `monsters` prints each room's population (as monsters.py <seed> <level>;
// $DIFFICULTY 0..2).
// drlg-dump <mpq dir> <seed> <level> [<out dir>] objgroups: each room's random
// object groups, as tools/emu/objgroups.py prints game.exe's (a range: to <out dir>).
// drlg-dump <mpq dir> <seed> <level> [<out dir>] collision: each room's grid
// with the rooms brought up in $ORDER (d2d::game::relevel), as tools/emu
// drlg.py collision_dump prints game.exe's.
// drlg-dump <mpq dir> <seed> 0 [<out dir>] game: one game, $LEVELS in turn,
// each brought up in $ORDER and populated, then every container its rooms
// made opened, on the game's one object seed: as tools/emu objgroups.py
// <seed> game prints game.exe's.
#include <drops.hpp>
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
#include <shrines.hpp>
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
#include <memory>
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

// drlg.py walk_order: a room order (list indices, the list newest first) as Level::rooms indices.
static std::vector<std::size_t> walk_order(std::size_t count, std::uint32_t seed, const std::string& kind) {
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) order[i] = i;
    if (kind == "reverse") std::ranges::reverse(order);
    if (kind == "shuffle") {
        std::uint32_t x = seed;
        for (std::size_t i = count; i-- > 1;) {
            x = (x * 1103515245u + 12345u) & 0x7fffffffu;
            std::swap(order[i], order[x % (i + 1)]);
        }
    }
    for (auto& room : order) room = count - 1 - room;
    return order;
}

// One game (tools/emu objgroups.py game_dump): each level of `levels` made,
// its rooms brought up in `kind` order and populated newest first, then
// every container they made opened (World::operate's open_container).
static std::string dump_game(d2d::game::GameData& game, std::uint32_t seed, const std::vector<int>& levels, const std::string& kind) {
    d2d::game::set_map_seed(game, seed);
    auto spawning = d2d::game::start_spawning(game, 0);
    std::vector<std::unique_ptr<d2d::game::Level>> built;
    std::ostringstream out;
    char line[160];
    for (const int lid : levels) {
        auto level = d2d::game::build_level(game, *game.builder, lid);
        if (!level) continue;
        const auto& made = *level;
        const auto base = made.npcs.size();
        out << "level " << lid << '\n';
        auto& state = spawning.levels[&made];
        std::size_t from = base;
        d2d::game::populate_level(game, spawning, made, walk_order(made.rooms.size(), seed, kind), [&](std::size_t room) {
            const auto& group = state.group_rooms[room];
            std::snprintf(line, sizeof line, "room %d,%d r1 %08x seed %08x post %08x rgn %08x game %08x", made.rooms[room].x, made.rooms[room].y,
                          made.room1_seeds[room], group.pre, group.post, spawning.objects.low, spawning.game.low);
            out << line;
            for (; from < made.npcs.size(); ++from)
                if (made.npcs[from].root == "objects") out << ' ' << made.npcs[from].object_id << '@' << int(made.npcs[from].x * 5) << ',' << int(made.npcs[from].y * 5);
            out << '\n';
        });
        const auto& area = game.area_level;
        auto alvl = [&](int id) { return area[std::size_t(id)][0]; };
        const auto [low, high] = d2d::rules::kChestLevels[0];
        const auto tc = d2d::rules::chest_tc(0, 0, alvl(lid), alvl(low), alvl(high));
        for (auto i = base; i < made.npcs.size(); ++i) {
            auto& npc = level->npcs[i];
            if (npc.root != "objects" || !std::ranges::contains(std::array{ 1, 3, 4, 5, 14, 26 }, npc.operate_fn)) continue;
            std::vector<d2d::rules::Drop> drops;
            // ON already (PreOperate, a gold placeholder's InitFn 28): its OperateFn does nothing.
            const auto row = game.builder->obj_row.find(std::to_string(npc.object_id));
            const bool on = npc.preoperated || (row != game.builder->obj_row.end() && game.builder->objects.get(row->second, "InitFn") == "28");
            if (on) {
                std::snprintf(line, sizeof line, "open %d@%d,%d: |%s -> %08x %08x\n", npc.object_id, int(npc.x * 5), int(npc.y * 5), npc.operate_fn == 1 ? " shut" : "", spawning.objects.low, npc.seed.low);
                out << line;
                continue;
            }
            const auto opened = d2d::rules::open_container(npc.operate_fn, npc.object_id, npc.locked, npc.sparkle, spawning.objects,
                                                           [&](int forced) { return d2d::rules::chest_round(game.rules, tc, npc.seed, drops, forced); });
            for (std::size_t k = 0; k < 2 * (drops.size() + opened.extra.size()); ++k) spawning.game.next();   // FUN_00555230 → FUN_00552df0: two a unit
            out << "open " << npc.object_id << '@' << int(npc.x * 5) << ',' << int(npc.y * 5) << ':';
            for (const auto& drop : drops) out << ' ' << drop.code << ':' << drop.quality << (drop.mul ? "*" + std::to_string(drop.mul) : "");
            out << " |";
            for (const auto& code : opened.extra) out << ' ' << code;
            std::snprintf(line, sizeof line, "%s -> %08x %08x\n", opened.opened ? "" : " shut", spawning.objects.low, npc.seed.low);
            out << line;
        }
        std::snprintf(line, sizeof line, "rgn %08x:%08x game %08x:%08x\n", spawning.objects.low, spawning.objects.high, spawning.game.low, spawning.game.high);
        out << line;
        built.push_back(std::move(level));
    }
    return out.str();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: drlg-dump <mpq dir> <map seed> [level]\n"); return 2; }
    const fs::path dir = argv[1];
    const std::string range = argv[2];
    const int id = argc > 3 ? std::atoi(argv[3]) : 2;
    if (argc > 4 && std::string(argv[argc - 1]) == "collision") {
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 1);
        if (!game) return 1;
        const char* kind = std::getenv("ORDER");
        const std::string order_kind = kind ? kind : "shuffle";
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            d2d::game::set_map_seed(*game, seed);
            std::ostringstream out;
            if (auto level = d2d::game::build_level(*game, *game->builder, id)) {
                const std::size_t count = level->rooms.size();
                const auto order = walk_order(count, seed, order_kind);
                level->npcs.clear();                             // tiles only: no room is populated
                level->laid = { count };                         // laid again, even in list order
                d2d::game::relevel(*level, order);
                std::vector<std::size_t> sorted(count);
                for (std::size_t i = 0; i < count; ++i) sorted[i] = i;
                std::ranges::sort(sorted, {}, [&](std::size_t i) { return std::tuple(level->rooms[i].y, level->rooms[i].x); });
                const int walk_width = level->ds1.width() * 5;
                char cell[8];
                for (const auto i : sorted) {
                    const auto& room = level->rooms[i];
                    out << "col " << room.x << ',' << room.y << '\n';
                    for (int y = room.y * 5; y < (room.y + room.height) * 5; ++y) {
                        out << ' ';
                        for (int x = room.x * 5; x < (room.x + room.width) * 5; ++x) {
                            std::snprintf(cell, sizeof cell, "%02x", level->walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)]);
                            out << cell;
                        }
                        out << '\n';
                    }
                }
            }
            if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << out.str();
            else std::fputs(out.str().c_str(), stdout);
            if (seed == last) break;
        }
        return 0;
    }
    if (argc > 4 && std::string(argv[argc - 1]) == "game") {
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 1);
        if (!game) return 1;
        const char* kind = std::getenv("ORDER");
        const char* wanted = std::getenv("LEVELS");
        std::vector<int> levels;
        std::istringstream list(wanted ? wanted : "2,8,4,9");
        for (std::string lid; std::getline(list, lid, ',');) levels.push_back(std::stoi(lid));
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            const auto text = dump_game(*game, seed, levels, kind ? kind : "shuffle");
            if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << text;
            else std::fputs(text.c_str(), stdout);
            if (seed == last) break;
        }
        return 0;
    }
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
                auto spawning = d2d::game::start_spawning(*game, 0);
                d2d::game::populate_level(*game, spawning, *level);
                const auto& group_rooms = spawning.levels[level.get()].group_rooms;
                char line[80];
                for (std::size_t i = 0; i < level->rooms.size(); ++i) {
                    const auto& group = group_rooms[i];
                    std::snprintf(line, sizeof line, "room %d,%d seed %08x post %08x rgn %08x", level->rooms[i].x, level->rooms[i].y, group.pre, group.post, group.rgn);
                    out << line;
                    for (const auto& [object, x, y] : group.made) out << ' ' << object << '@' << x << ',' << y;
                    out << '\n';
                }
                std::snprintf(line, sizeof line, "rgn %08x\n", spawning.objects.low);
                out << line;
                // drops: three items at each group object, each landing an item (0x200) the next sees.
                auto grid = d2d::game::object_collision(spawning, *level);
                const int width = level->ds1.width() * 5, height = level->ds1.height() * 5;
                auto flags = [&](int x, int y) -> int {
                    return x < 0 || y < 0 || x >= width || y >= height ? 0x27 : grid[std::size_t(y) * std::size_t(width) + std::size_t(x)];
                };
                for (const auto& group : group_rooms)
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
