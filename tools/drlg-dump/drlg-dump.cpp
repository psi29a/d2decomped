// SPDX-License-Identifier: GPL-3.0-or-later
// drlg-dump <mpq dir> <map seed> [level] — our generator's level in the
// same text form as tools/emu/drlg.py prints game.exe's, for diffing.
// drlg-dump <mpq dir> <first>-<last> <level> <out dir> writes <seed>.txt each.
// A trailing `tiles` adds every room's tiles (as drlg.py <seed> <level> tiles);
// `monsters` prints each room's population (as monsters.py <seed> <level>;
// $DIFFICULTY 0..2); an act boss adds `!<mods>` and `h` for half freeze
// (rules::act_boss).
// drlg-dump <mpq dir> <seed> <level> [<out dir>] objgroups: each room's random
// object groups, as tools/emu/objgroups.py prints game.exe's (a range: to <out dir>).
// drlg-dump <mpq dir> <seed> <level> [<out dir>] collision: each room's grid
// with the rooms brought up in $ORDER (d2d::game::relevel), as tools/emu
// drlg.py collision_dump prints game.exe's.
// drlg-dump <mpq dir> <seed> 0 [<out dir>] game: one game, $LEVELS in turn,
// each brought up in $ORDER and populated, then every container its rooms
// made opened, on the game's one object seed: as tools/emu objgroups.py
// <seed> game prints game.exe's.
#include <drlg.hpp>
#include <drops.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <install.hpp>
#include <level_ids.hpp>
#include <maze.hpp>
#include <mpq.hpp>
#include <rules.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <room_tiles.hpp>
#include <shrines.hpp>
#include <tile_pick.hpp>
#include <town_npcs.hpp>
#include <txt.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::drlg;

static int dump_difficulty() { const char* value = std::getenv("DIFFICULTY"); return value ? std::atoi(value) : 0; }   // $DIFFICULTY 0..2

// The MPQs as open_game_data opens them: the 1.14d patch layer first
// ($D2_PATCH_INSTALLER, else the folder's patch_d2.mpq or LODPatch_114d.exe,
// found case-insensitively), then d2exp and d2data. Without the patch there
// is no monpreset.bin, so a DS1's monsters keep their file ids: none is one
// that rolls to stay (FUN_00667620), the level seed misses those steps, and
// every room after them is seeded two rooms late in Lut Gholein.
static d2d::mpq::Stack open_mpqs(const fs::path& dir) {
    d2d::mpq::Stack mpqs;
    auto push_patch = [&](const fs::path& path) {
        std::string extension = path.extension().string();
        for (auto& letter : extension) letter = char(std::tolower(static_cast<unsigned char>(letter)));
        if (extension == ".mpq") mpqs.push(path);
        else mpqs.push_installer(path);
    };
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool given = patch && *patch && fs::exists(patch);
    if (patch && *patch && !given) std::fprintf(stderr, "drlg-dump: patch %s not found\n", patch);
    if (given) push_patch(patch);
    else if (const auto found = d2d::install::find_file(dir, "patch_d2.mpq")) push_patch(*found);
    else if (const auto found = d2d::install::find_file(dir, "LODPatch_114d.exe")) push_patch(*found);
    for (const char* name : { "d2exp.mpq", "d2data.mpq" }) if (fs::exists(dir / name)) mpqs.push(dir / name);
    return mpqs;
}
static bool g_units = false;                            // print rooms' units instead of their tiles
static bool g_seeds = false;                            // print rooms' room1 seeds instead of their tiles
// The units kind brings rooms up in $ORDER, as drlg.py bring_up: list
// positions (the list newest first), shuffled as walk_order shuffles; the
// other kinds bring them up in list order.
static std::vector<std::size_t> units_order(std::size_t count, std::uint32_t seed) {
    const char* kind = std::getenv("ORDER");
    if (!g_units || !kind || std::string(kind) == "list") return {};
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) order[i] = i;
    if (std::string(kind) == "reverse") std::ranges::reverse(order);
    if (std::string(kind) == "shuffle") {
        std::uint32_t x = seed;
        for (std::size_t i = count; i-- > 1;) {
            x = (x * 1103515245u + 12345u) & 0x7fffffffu;
            std::swap(order[i], order[x % (i + 1)]);
        }
    }
    return order;
}

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
                for (std::size_t k = 0; k < 3; ++k) maze.rooms[k] = to_int(assets.lvl_maze.get(row_index, std::array{ "Rooms", "Rooms(N)", "Rooms(H)" }[k]));
                maze.width = to_int(assets.lvl_maze.get(row_index, "SizeX"));
                maze.height = to_int(assets.lvl_maze.get(row_index, "SizeY"));
                maze.merge = to_int(assets.lvl_maze.get(row_index, "Merge"));
            }
        maze.type = to_int(assets.levels.get(*row, "LevelType"));
        { auto act = act_seed(seed); const auto tombs = roll_act2_tombs(act); maze.boss_tomb = tombs.boss; maze.staff_tomb = tombs.staff; }
        std::vector<std::string> notes;
        int court_file = id == 27 || id == 28 ? courtyard_file(act1_from_map_seed(level_defs(assets.levels), seed), act_seed(seed)) : -1;
        if (id == level_ids::kLutGholein) court_file = lut_gholein_file(act2_layout(level_defs(assets.levels), act_seed(seed)));
        if (id == level_ids::kRogueEncampment)               // the camp's file and spot: the side the Blood Moor went
            for (const auto& placement : act1_from_map_seed(level_defs(assets.levels), seed))
                if (placement.level == id) court_file = placement.dir, rect = { placement.x, placement.y, width, height };
        if (const auto court = level_row(assets.levels, 27); id == 28 && court) {
            auto court_seed = level_seed(seed, 27);
            court_file = preset_file(assets.data, 27, court_seed, court_file);
            const auto [court_x, court_y] = level_origin(assets.levels, *court);
            rect = { court_x, court_y, to_int(assets.levels.get(*court, "SizeX")), to_int(assets.levels.get(*court, "SizeY")) };
        }
        if (id == level_ids::kRiverOfFlame)                  // FUN_00673320 hangs it under the Chaos Sanctuary
            for (const auto& placement : act4_layout(level_defs(assets.levels), act_seed(seed)))
                if (placement.level == level_ids::kChaosSanctuary) rect = { placement.x, placement.y, placement.width, placement.height };
        auto rooms = preset_level ? generate_preset(assets.data, id, width, height, level_seed(seed, id), notes, court_file)
                                  : generate_maze(assets.data, maze, id, width, height, level_seed(seed, id), dump_difficulty(), notes, court_file, &rect);
        pf("level %d at %d,%d size %dx%d\n", id, rect[0], rect[1], rect[2], rect[3]);
        auto sorted = rooms;
        std::ranges::sort(sorted, {}, [](const auto& room) { return std::tuple(room.y, room.x); });
        pf("rooms %zu\n", sorted.size());
        for (const auto& room : sorted)
            pf("%d,%d %dx%d kind %d seed %08x def %d file %d at %d,%d\n", room.x, room.y, room.width, room.height, room.kind, room.seed, room.def, room.file, room.preset_x, room.preset_y);
        if (dt1s) {
            auto built = level_room_tiles(rooms, {}, assets.data, *dt1s, id, warp_slots(assets, id), notes, units_order(rooms.size(), seed));
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
    const auto act3 = act3_layout(level_defs(assets.levels), act_seed(seed));
    const auto layout = id >= level_ids::kHarrogath ? act5_layout(level_defs(assets.levels), act_seed(seed))
                      : id >= level_ids::kPandemoniumFortress ? act4_layout(level_defs(assets.levels), act_seed(seed))
                      : id >= level_ids::kKurastDocks ? act3.levels
                      : id >= level_ids::kLutGholein ? act2_layout(level_defs(assets.levels), act_seed(seed)) : act1_from_map_seed(level_defs(assets.levels), seed);
    if (std::getenv("DRLG_LAYOUT"))
        for (const auto& placement : layout) std::fprintf(stderr, "layout %d at %d,%d %dx%d dir %d flip %d flags %x\n", placement.level, placement.x, placement.y, placement.width, placement.height, placement.dir, placement.flip, placement.flags);
    const auto level = level_ids::act_of(id) == 2 ? act3_outdoor_level(assets.levels, act3, id) : outdoor_level(assets.levels, layout, id);
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
        auto built = level_room_tiles(outdoor.rooms, outdoor.plain, assets.data, *dt1s, id, warp_slots(assets, id), notes, units_order(outdoor.rooms.size(), seed));
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
    d2d::game::set_map_seed(game_data, seed, dump_difficulty());
    const auto* level = game_data.level(id);
    if (!level) return "";
    auto spawning = d2d::game::start_spawning(game_data, dump_difficulty());
    const auto order = d2d::game::populate_level(game_data, spawning, *level);
    const auto& spawns = spawning.levels[level].spawns;
    std::ostringstream out;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const auto [room, first] = order[k];
        const std::size_t end = k + 1 < order.size() ? order[k + 1].second : spawns.size();
        if (first == end) continue;
        out << "mon " << level->rooms[room].x << ',' << level->rooms[room].y << ':';
        for (std::size_t i = first; i < end; ++i) {
            out << ' ' << spawns[i].type;
            const auto& types = game_data.monsters.types;
            if (const auto act_boss = spawns[i].type >= 0 && std::size_t(spawns[i].type) < types.size()
                                          ? d2d::rules::act_boss(spawns[i].type, types[std::size_t(spawns[i].type)].base) : std::nullopt) {
                out << '!';
                for (std::size_t mod = 0; mod < act_boss->mods.size(); ++mod) out << (mod ? "." : "") << act_boss->mods[mod];
                out << (act_boss->half_freeze ? "h" : "");
            }
            out << '@' << spawns[i].x << ',' << spawns[i].y << (spawns[i].dead ? "m12" : "") << '/' << (spawns[i].leader >= 0 ? std::size_t(spawns[i].leader) - first : i - first);
        }
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
    d2d::game::set_map_seed(game, seed, dump_difficulty());
    auto spawning = d2d::game::start_spawning(game, dump_difficulty());
    std::vector<std::unique_ptr<d2d::game::Level>> built;
    std::ostringstream out;
    char line[160];
    for (const int lid : levels) {
        const bool camp = lid == game.town.id;       // the camp: built with the map seed, its units there already
        auto level = camp ? nullptr : d2d::game::build_level(game, *game.builder, lid);
        if (!level && !camp) continue;
        if (level && spawning.levels.contains(&game.town))   // next door to the camp made already (install_level's link)
            level->nearby.push_back({ &game.town, game.town.world_x - level->world_x, game.town.world_y - level->world_y });
        for (const auto& other : built)                       // and to the act's levels made already, both ways
            if (level && d2d::rules::level_act(other->id) == d2d::rules::level_act(lid)) {
                level->nearby.push_back({ other.get(), other->world_x - level->world_x, other->world_y - level->world_y });
                other->nearby.push_back({ level.get(), level->world_x - other->world_x, level->world_y - other->world_y });
            }
        auto& made = camp ? game.town : *level;
        const auto base = camp ? std::size_t(0) : made.npcs.size();
        out << "level " << lid << '\n';
        auto& state = spawning.levels[&made];
        std::size_t from = base;
        std::vector<std::size_t> objects;             // the objects in the order made: the order they're opened
        const int camp_wide = (made.ds1.width() + 6) / 8;   // 8x8 rooms, row by row, less the DS1's edge
        const auto count = camp ? std::size_t(camp_wide) * std::size_t((made.ds1.height() + 6) / 8) : made.rooms.size();
        d2d::game::populate_level(game, spawning, made, walk_order(count, seed, kind), [&](std::size_t room) {
            if (camp) {                               // no room seeds: the units made, objects listed
                std::snprintf(line, sizeof line, "room %d,%d rgn %08x game %08x", int(room) % camp_wide * 8, int(room) / camp_wide * 8, spawning.objects.low, spawning.game.low);
                out << line;
                for (std::size_t i = made.npcs.size(); i-- > 0;)
                    if (made.npcs[i].root == "objects" && made.npcs[i].room == int(room)) {
                        objects.push_back(i);
                        out << ' ' << made.npcs[i].object_id << '@' << int(made.npcs[i].x * 5) << ',' << int(made.npcs[i].y * 5);
                    }
                out << '\n';
                return;
            }
            const auto& group = state.group_rooms[room];
            if (level_ids::is_town(lid))              // a town's rooms print without their seeds (objgroups.py)
                std::snprintf(line, sizeof line, "room %d,%d rgn %08x game %08x", made.rooms[room].x, made.rooms[room].y, spawning.objects.low, spawning.game.low);
            else
                std::snprintf(line, sizeof line, "room %d,%d r1 %08x seed %08x post %08x rgn %08x game %08x", made.rooms[room].x, made.rooms[room].y,
                              made.room1_seeds[room], group.pre, group.post, spawning.objects.low, spawning.game.low);
            out << line;
            // $AREA_IDS=<level>: each room's spawn areas, clipped rect and id (tools/emu area_ids.py).
            if (const char* ids_level = std::getenv("AREA_IDS"); ids_level && std::atoi(ids_level) == lid) {
                std::fprintf(stderr, "areas %d,%d:", made.rooms[room].x, made.rooms[room].y);
                for (const auto& area : made.room_areas[room]) std::fprintf(stderr, " %d,%d,%d,%d=%u", area.left, area.top, area.right, area.bottom, area.id);
                std::fprintf(stderr, "\n");
            }
            for (; from < made.npcs.size(); ++from)
                if (made.npcs[from].root == "objects") out << ' ' << made.npcs[from].object_id << '@' << int(made.npcs[from].x * 5) << ',' << int(made.npcs[from].y * 5);
            out << '\n';
        });
        const auto& area = game.area_level;
        auto alvl = [&](int id) { return area[std::size_t(id)][0]; };
        const int act = d2d::rules::level_act(lid);
        const auto [low, high] = d2d::rules::kChestLevels[std::size_t(act)];
        const auto treasure_class = d2d::rules::chest_tc(act, 0, alvl(lid), alvl(low), alvl(high));
        if (!camp) for (auto i = base; i < made.npcs.size(); ++i) objects.push_back(i);
        for (const auto npc_index : objects) {
            auto& npc = made.npcs[npc_index];
            if (npc.root != "objects" || !std::ranges::contains(std::array{ 1, 3, 4, 5, 14, 26 }, npc.operate_fn)) continue;
            std::vector<d2d::rules::Drop> drops;
            // ON already (PreOperate, a gold placeholder's InitFn 28): its OperateFn does nothing.
            const auto row = game.builder->obj_row.find(std::to_string(npc.object_id));
            const bool already_on = npc.preoperated || (row != game.builder->obj_row.end() && game.builder->objects.get(row->second, "InitFn") == "28");
            if (already_on) {
                std::snprintf(line, sizeof line, "open %d@%d,%d: |%s -> %08x %08x\n", npc.object_id, int(npc.x * 5), int(npc.y * 5), npc.operate_fn == 1 ? " shut" : "", spawning.objects.low, npc.seed.low);
                out << line;
                continue;
            }
            const auto opened = d2d::rules::open_container(npc.operate_fn, npc.object_id, npc.locked, npc.sparkle, spawning.objects,
                                                           [&](int forced) { return d2d::rules::chest_round(game.rules, treasure_class, npc.seed, drops, forced); });
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
        if (level) built.push_back(std::move(level));
    }
    return out.str();
}

// The camp's NPCs thinking (tools/emu npcs.py): its rooms populated in list
// order, then each NPC with the Npc AI and a path thinks `thinks` times
// (rules::npc_think); a walk puts it at the spot, every other one 4
// subtiles short on x; its mode is NU at each think. Lines sorted.
// The town's NPCs thinking (tools/emu npcs.py): level 1 the camp, else any act's town.
static std::string dump_npcs(d2d::game::GameData& game, std::uint32_t seed, int thinks, int lid) {
    d2d::game::set_map_seed(game, seed, dump_difficulty());
    auto spawning = d2d::game::start_spawning(game, dump_difficulty());
    std::unique_ptr<d2d::game::Level> built;
    if (lid != level_ids::kRogueEncampment) built = d2d::game::build_level(game, *game.builder, lid);
    auto& town = built ? *built : game.town;
    const std::size_t count = built ? town.rooms.size() : std::size_t((town.ds1.width() + 6) / 8) * std::size_t((town.ds1.height() + 6) / 8);
    d2d::game::populate_level(game, spawning, town, walk_order(count, seed, "list"));
    std::vector<std::string> lines;
    char text[64];
    using d2d::game::TownAi;
    for (const auto& npc : town.npcs) {
        if (npc.town_ai == TownAi::none || (npc.town_ai != TownAi::vendor && npc.path.empty()) || npc.quest || npc.palace) continue;
        std::vector<d2d::rules::NpcPoint> path;
        for (std::size_t i = 0; i < npc.path.size(); ++i)
            path.push_back({ i < npc.actions.size() ? npc.actions[i] : 0, int(npc.path[i].first * 5), int(npc.path[i].second * 5) });
        int x = int(npc.x * 5), y = int(npc.y * 5), walks = 0;
        auto rng = npc.seed;
        std::snprintf(text, sizeof text, "npc %d @%d,%d seed %08x path", npc.hc_idx, x, y, rng.low);
        std::string line = text;
        for (const auto& point : path) line += ' ' + std::to_string(point.action) + ':' + std::to_string(point.x) + ',' + std::to_string(point.y);
        line += ':';
        d2d::rules::NpcBrain brain;
        for (int think = 0; think < thinks; ++think) {
            const auto act = npc.town_ai == TownAi::vendor ? d2d::rules::vendor_think(rng)
                                                           : d2d::rules::npc_think(brain, rng, path, x, y, npc.hc_idx, npc.modes, 1, {}, npc.town_ai == TownAi::towner);
            if (act.face >= 0) line += " f" + std::to_string(act.face);
            switch (act.kind) {
            case d2d::rules::NpcAct::Kind::stand: line += " s" + std::to_string(act.frames); break;
            case d2d::rules::NpcAct::Kind::mode: line += " m" + std::to_string(act.mode); break;
            case d2d::rules::NpcAct::Kind::walk:
                line += " w" + std::to_string(act.x) + ',' + std::to_string(act.y);
                x = act.x + (++walks % 2 == 0 ? 4 : 0); y = act.y;
                break;
            }
        }
        lines.push_back(std::move(line));
    }
    std::ranges::sort(lines);
    std::string out;
    for (const auto& line : lines) out += line + '\n';
    return out;
}

int main(int argc, char** argv) try {
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
            d2d::game::set_map_seed(*game, seed, dump_difficulty());
            std::ostringstream out;
            if (auto level = d2d::game::build_level(*game, *game->builder, id)) {
                const std::size_t count = level->rooms.size();
                const auto order = walk_order(count, seed, order_kind);
                level->npcs.clear();                             // tiles only: no room is populated
                level->laid = { count };                         // laid again, even in list order
                d2d::game::relevel(*level, order);
                std::vector<std::size_t> sorted(count);
                for (std::size_t i = 0; i < count; ++i) sorted[i] = i;
                std::ranges::sort(sorted, {}, [&](std::size_t room_index) { return std::tuple(level->rooms[room_index].y, level->rooms[room_index].x); });
                const int walk_width = level->ds1.width() * 5;
                char cell[8];
                for (const auto room_index : sorted) {
                    const auto& room = level->rooms[room_index];
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
    if (argc > 4 && std::string(argv[argc - 1]) == "crc") {
        // Every level's collision as one checksum a line, in jaenster/libd2's
        // golden form (coll_crc_verify.zig): per room FNV-1a over px, py, w, h
        // and each u16 cell (all as u32 LE), summed over the level's rooms.
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 1);
        if (!game) return 1;
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        if (last < first) { std::fprintf(stderr, "crc: seeds %s run backwards\n", range.c_str()); return 2; }
        const auto hash = [](std::uint32_t& sum, std::uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8) sum = (sum ^ ((value >> shift) & 0xff)) * 0x01000193u;
        };
        for (auto seed = first;; ++seed) {
            d2d::game::set_map_seed(*game, seed, dump_difficulty());
            for (int level_id = 1; level_id <= 136; ++level_id) {
                auto level = d2d::game::build_level(*game, *game->builder, level_id);
                if (!level || level->rooms.empty()) continue;
                const std::size_t count = level->rooms.size();
                level->npcs.clear();                             // tiles only, as the golden: no room is populated
                level->laid = { count };
                d2d::game::relevel(*level, walk_order(count, seed, "list"));
                const int walk_width = level->ds1.width() * 5;
                std::uint32_t crc = 0;
                std::size_t cells = 0;
                for (const auto& room : level->rooms) {
                    std::uint32_t room_hash = 0x811c9dc5u;
                    hash(room_hash, std::uint32_t((level->world_x + room.x) * 5));
                    hash(room_hash, std::uint32_t((level->world_y + room.y) * 5));
                    hash(room_hash, std::uint32_t(room.width * 5));
                    hash(room_hash, std::uint32_t(room.height * 5));
                    for (int y = room.y * 5; y < (room.y + room.height) * 5; ++y)
                        for (int x = room.x * 5; x < (room.x + room.width) * 5; ++x)
                            hash(room_hash, level->walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)]);
                    crc += room_hash;
                    cells += std::size_t(room.width * room.height * 25);
                }
                std::printf("{\"seed\":%u,\"diff\":%d,\"levelId\":%d,\"crc\":%u,\"cells\":%zu,\"px\":%d,\"py\":%d}\n", seed, dump_difficulty(), level_id, crc, cells, level->world_x * 5, level->world_y * 5);
            }
            std::fflush(stdout);
            if (seed == last) break;
        }
        return 0;
    }
    if (argc > 4 && std::string(argv[argc - 1]) == "layout") {
        // Act 2's outdoor rectangles (act2_layout), as tools/emu drlg.layout_dump prints them.
        d2d::mpq::Stack mpqs = open_mpqs(dir);
        OutdoorAssets assets;
        load_outdoor_assets(assets, [&](const std::string& path) { return mpqs.try_read(path); });
        const auto defs = level_defs(assets.levels);
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            std::ostringstream out;
            if (id >= level_ids::kPandemoniumFortress) {     // acts 4 and 5, as drlg.act4_layout_dump
                auto layout = id >= level_ids::kHarrogath ? act5_layout(defs, act_seed(seed)) : act4_layout(defs, act_seed(seed));
                std::ranges::sort(layout, {}, &Placed::level);
                for (const auto& placement : layout)
                    out << "level " << placement.level << " at " << placement.x << ',' << placement.y << " size " << placement.width << 'x' << placement.height << '\n';
                if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << out.str();
                else std::fputs(out.str().c_str(), stdout);
                if (seed == last) break;
                continue;
            }
            if (id >= level_ids::kKurastDocks) {     // act 3, as drlg.act3_layout_dump
                const auto act3 = act3_layout(defs, act_seed(seed));
                for (const auto& placement : act3.levels) {
                    out << "level " << placement.level << " at " << placement.x << ',' << placement.y << " size " << placement.width << 'x' << placement.height << '\n';
                    if (placement.level < level_ids::kSpiderForest || placement.level > level_ids::kFlayerJungle) continue;
                    const auto& cells = act3.jungle[std::size_t(placement.level - level_ids::kSpiderForest)];
                    out << "  cells";
                    for (const int def_id : cells.presets) out << ' ' << def_id;
                    out << " specials " << cells.specials << '\n';
                }
                out << "roll " << act3.roll << '\n';
                if (std::getenv("LAYOUT_STATS")) std::fprintf(stderr, "stats %u tries %d failed %d\n", seed, act3.tries, int(act3.failed));   // grids laid, game.exe's endless retry cut off
                if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << out.str();
                else std::fputs(out.str().c_str(), stdout);
                if (seed == last) break;
                continue;
            }
            auto layout = act2_layout(defs, act_seed(seed));
            std::ranges::sort(layout, {}, &Placed::level);
            for (const auto& placement : layout)
                out << "level " << placement.level << " at " << placement.x << ',' << placement.y << " size " << placement.width << 'x' << placement.height << '\n';
            out << "town file " << lut_gholein_file(layout) << '\n';
            auto tomb_seed = act_seed(seed);
            const auto tombs = roll_act2_tombs(tomb_seed);
            out << "tombs " << tombs.boss << ' ' << tombs.staff << '\n';
            if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << out.str();
            else std::fputs(out.str().c_str(), stdout);
            if (seed == last) break;
        }
        return 0;
    }
    if (argc > 3 && std::string(argv[argc - 1]) == "store") {
        // rules::store_fill's asks as tools/emu store.py prints FUN_00576980's (every item made).
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 0);
        if (!game) return 1;
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        static constexpr std::array<std::array<int, 3>, 6> kJobs = { { { 0, 0, 1 }, { 1, 0, 18 }, { 2, 0, 40 }, { 1, 1, 30 }, { 3, 2, 70 }, { 4, 2, 90 } } };
        for (auto index = first;; ++index) {
            const std::uint32_t seed = index * 0x9e3779b1u;
            for (int vendor = 0; vendor < 17; ++vendor)
                for (const auto& [act, diff, clvl] : kJobs) {
                    d2d::rules::Rng proxy{ seed };
                    std::string line = std::format("{:08x} {} {} {} {}:", seed, vendor, act, diff, clvl);
                    d2d::rules::store_fill(game->rules, vendor, d2d::rules::store_level(clvl, act, diff), true, proxy, [&](const d2d::rules::StoreAsk& ask) {
                        line += std::format(" {}:{}@{}", ask.code, ask.quality, ask.ilvl);
                        if (ask.fill) line += " =7";
                        return true;
                    });
                    std::printf("%s | %08x\n", line.c_str(), proxy.low);
                }
            if (index == last) break;
        }
        return 0;
    }
    if (argc > 3 && std::string(argv[argc - 1]) == "hirelist") {
        // rules::hire_entries as tools/emu hire.py entries_dump prints FUN_00576070's (41 and 21 names).
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto index = first;; ++index) {
            for (int names : { 41, 21 }) {
                d2d::rules::Rng game{ index * 0x9e3779b1u };
                for (const auto& entry : d2d::rules::hire_entries(names, game)) std::printf("%d %08x %d\n", entry.name, entry.seed, entry.offered ? 1 : 0);
                std::printf("game %08x\n", game.low);
            }
            if (index == last) break;
        }
        return 0;
    }
    if (argc > 3 && std::string(argv[argc - 1]) == "hire") {
        // rules::merc_offer as tools/emu hire.py prints game.exe's (seed s * 0x9e3779b1).
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 0);
        if (!game) return 1;
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto index = first;; ++index) {
            const std::uint32_t seed = index * 0x9e3779b1u;
            for (int act : { 0, 1, 2 })
                for (int diff : { 0, 1, 2 })
                    for (int clvl : { 1, 9, 30, 75 }) {
                        std::printf("%08x %d %d %d: ", seed, act, diff, clvl);
                        const auto offer = d2d::rules::merc_offer(game->rules, true, act, diff, clvl, d2d::rules::HireEntry{ 0, seed });
                        if (!offer) { std::puts("none"); continue; }
                        std::printf("%d %d %d %d %d %d %u %d %d %d\n", offer->id, offer->level, offer->life, offer->str, offer->dex, offer->cost, offer->exp,
                                    offer->def, offer->dmg_min, offer->dmg_max);
                    }
            if (index == last) break;
        }
        return 0;
    }
    if (argc > 4 && std::string(argv[argc - 1]) == "npcs") {
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto game = d2d::game::load_game_data(dir, patch ? fs::path(patch) : fs::path{}, 1);
        if (!game) return 1;
        const char* thinks = std::getenv("THINKS");
        const auto dash = range.find('-');
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = dash == std::string::npos ? first : std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            const auto text = dump_npcs(*game, seed, thinks ? std::atoi(thinks) : 40, level_ids::is_town(id) ? id : 1);
            if (argc > 5) std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << text;
            else std::fputs(text.c_str(), stdout);
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
            d2d::game::set_map_seed(*game, seed, dump_difficulty());
            std::ostringstream out;
            if (const auto level = d2d::game::build_level(*game, *game->builder, id)) {
                auto spawning = d2d::game::start_spawning(*game, dump_difficulty());
                d2d::game::populate_level(*game, spawning, *level);
                const auto& group_rooms = spawning.levels[level.get()].group_rooms;
                char line[80];
                for (std::size_t i = 0; i < level->rooms.size(); ++i) {
                    const auto& group = group_rooms[i];
                    std::snprintf(line, sizeof line, "room %d,%d seed %08x post %08x rgn %08x", level->rooms[i].x, level->rooms[i].y, group.pre, group.post, group.rgn);
                    out << line;
                    for (const auto& [object, object_x, object_y] : group.made) out << ' ' << object << '@' << object_x << ',' << object_y;
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
                    for (const auto& [object, object_x, object_y] : group.made)
                        for (int k = 0; drops && k < 3; ++k) {
                            const auto [drop_x, drop_y] = d2d::game::drop_spot(game->field, object_x, object_y, flags);
                            if (flags(drop_x, drop_y) != 0x27) grid[std::size_t(drop_y) * std::size_t(width) + std::size_t(drop_x)] |= 0x200;
                            out << "drop " << drop_x << ',' << drop_y << '\n';
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
    d2d::mpq::Stack mpqs = open_mpqs(dir);
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
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
