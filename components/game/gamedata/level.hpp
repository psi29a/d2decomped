// SPDX-License-Identifier: GPL-3.0-or-later
// One level (Levels.txt row) as built: its DS1, DT1s, walk grid and units; the
// level builder and what builds, lays and seeds a level. The drlg, ds1 and dt1
// headers come with it: include it only where levels are built or walked.
#pragma once

#include "gamedata.hpp"
#include "npc.hpp"
#include "spawning.hpp"

#include <drlg.hpp>
#include <ds1.hpp>
#include <dt1.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <mpq.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <rules.hpp>
#include <tile_pick.hpp>
#include <txt.hpp>
#include <units.hpp>

#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::game {

// A warp tile's spot, its destination and the client's click box (Level::warps).
struct LevelWarp { float x, y; int destination; float exit_x, exit_y, unit_x, unit_y; int slot = 0, pair = 0; std::array<int, 4> select{}; };

// A generated level's DT1s: the headers its rooms' picks read (LvlTypes
// files of its type by mask bit, then Blank, InvisWal, Warp) and the
// archives drawn from, loaded into the level.
struct LevelDt1s {
    d2d::drlg::RoomDt1s heads;
    std::unordered_map<const d2d::drlg::Dt1File*, const d2d::dt1::Archive*> archive;
};

// One level (Levels.txt row): its DS1, the DT1s it references and a
// (style, seq, type) -> tile lookup for the compositor, the walk grid, the
// units the DS1 places, where players arrive, and its sound environment.
// Empty when the assets aren't found (headless / bad data dir).
// Level::spawn_at before any spawn tile is found.
constexpr std::array<std::pair<float, float>, 12> no_spawns() {
    std::array<std::pair<float, float>, 12> none{};
    for (auto& spot : none) spot = { -1.f, -1.f };
    return none;
}

struct Level {
    int id = 0;                                        // Levels.txt Id (1: the Rogue Encampment)
    std::string name;                                  // Levels.txt LevelName
    int pal = -1;                                      // Levels.txt Pal: the act whose palette it draws with (build_level); -1 its own act's
    int type = 1;                                      // its LevelType (AutoMap.txt rules)
    int world_x = 0, world_y = 0;                      // act tiles of its (0, 0): where it sits in the act
    int layer = 0;                                     // Levels.txt Layer: levels on one share an automap
    int song = 0, ambience = 0;                        // SoundEnviron Song / Day Ambience (Sounds.txt)
    int material = 1;                                  // SoundEnviron Material 1: its floor's where a tile says none (footsteps)
    int night_ambience = 0, day_event = 0, night_event = 0, event_delay = 0;   // its Night Ambience, Day / Night Event, Event Delay (sound ticks)
    int light = -1;                                    // Levels.txt Intensity when it has its own light (caves: 0), -1 the day's
    bool rain = false;                                 // Levels.txt Rain: the weather reaches it
    bool mud = false;                                  // Levels.txt Mud: its water floors bubble
    // Levels.txt cmon1..4 / cpct1..4 / camt1..4: the client's critters (MonStats row, % a room, x count; critters.md).
    struct Critter { int row = -1, percent = 0, amount = 0; };
    std::array<Critter, 4> critters{};
    int teleport = 1;                                  // Levels.txt Teleport: 0 no, 1 yes, 2 only in a clear line (FUN_0061db70 +4)
    std::uint32_t lays = 0;                            // the times relevel laid its rooms again (World::reopen_doors)
    d2d::ds1::Map ds1;
    std::vector<d2d::dt1::Archive> dt1s;
    // Keyed by (style, seq, type) — one map covers floors, walls, trees,
    // shadows, roofs; the DT1's `type` field disambiguates orientations
    // that share (style, seq). First matching tile wins across DT1s.
    std::unordered_map<std::uint64_t, const d2d::dt1::Tile*> tile_lookup;
    // The DT1 tile game.exe's room pass gives each cell (drlg
    // level_room_tiles, proven against game.exe): layer 0 wall, 1 floor,
    // 2 shadow, in the order the rooms add them. Filled for generated
    // levels; when present it replaces tile_lookup for drawing and walking.
    // `hidden`: a floor or wall whose word has bit 31 (DS1 hidden,
    // FillBlanks, style-30 floors): tile flag 8, which the client's draws
    // skip (FUN_004de410, FUN_004dea70: & 0x408) and collision doesn't
    // (FUN_0064c790).
    // `cell`: what its word ORs over the whole cell (FUN_0066dde0's tile
    // flags 2 / 0x40 / 0x80 as FUN_0064c790 stamps them: 0x10, 0x01, 0x04).
    // `stamp`: what collision took of it (set_level_tiles): the tile as it
    // was when the room it lies in came up, none if that was before its own.
    // no_fade: the tile's flag 4 (never_fades): a wall in front stays opaque.
    // frame / frames: an Animate preset's frame (drlg::PlacedTile), 0 frames when not animated.
    struct Pick { std::uint8_t layer, orient; const d2d::dt1::Tile* tile; bool hidden = false; std::uint8_t cell = 0; const d2d::dt1::Tile* stamp = nullptr; bool no_fade = false; std::uint8_t frame = 0, frames = 0; };
    // A tile's flag 4 as the DRLG makes it (FUN_0066dc50): the DS1 word's bit 3
    // or bit 26, or the DT1 tile's material flags bit 0; FUN_004dd180 then
    // never fades it (walls.md).
    [[nodiscard]] static constexpr bool never_fades(std::uint32_t word, std::uint16_t material_flags) {
        return (word & 0x8u) != 0 || (word & 0x4000000u) != 0 || (material_flags & 1u) != 0;
    }
    std::vector<std::vector<Pick>> picks;               // ds1 width x height, or empty
    // What the screen shows: game.exe's client brings each batch of rooms up
    // in the order they were made (the near list and the ring past it, oldest
    // first), the server the arrived room first; edge sharing makes their
    // picks differ (Lut Gholein's sand: 1280/1280 cells live this way, 950
    // as `picks`). Empty: the same as `picks`. Collision stays the server's.
    std::vector<std::vector<Pick>> drawn;
    std::vector<std::size_t> drawn_laid;                // the order drawn was laid in (list indices), empty: none
    [[nodiscard]] const std::vector<std::vector<Pick>>& shown() const noexcept { return drawn.empty() ? picks : drawn; }
    // Each room's tile lists as the automap walks them (FUN_00458f40: room1
    // +8's floors, then its walls), by Level::rooms index: {cell, its
    // index in shown()'s cell} in list order, a shared edge tile in each
    // room's that lists it. From the last lay (lay_tiles), shown()'s.
    struct RoomTile { std::uint32_t cell; std::uint16_t pick; };
    std::vector<std::array<std::vector<RoomTile>, 2>> room_tiles;   // [0] floors, [1] walls
    // Shared tiles re-picked after the room they lie in came up: that grid's
    // cell loses the old tile's flags and takes the new one's (FUN_0064c860).
    struct Patch { int x, y; const d2d::dt1::Tile* old_tile; const d2d::dt1::Tile* tile; };
    std::vector<Patch> patches;
    // Collision, as game.exe's room grids hold it (u16 a subtile): every
    // floor/wall tile's 5x5 subtile flags OR'd onto its cell, (width*5) x
    // (height*5), row-major — 0x01 blocks walking, 0x08 a player's, 0x04
    // missiles (DT1 subtile flag bits) — and the objects' stamps (Npc::stamp:
    // 0x400 objects, 0x800 doors, 0x8000 the unblocking). Units aren't in
    // it: they stamp the Crowd (ai.hpp).
    // Mutable: a door's footprint comes and goes as it's used (set_footprint).
    mutable std::vector<std::uint16_t> walk;
    std::vector<Npc> npcs;                             // what its DS1 places (and Cain)
    // The levels next to it in the act, (dx, dy) = their origin minus
    // ours, in cells. Past this map's edge, collision and
    // the renderer use theirs — the way D2 walks and draws across rooms
    // of neighbouring levels.
    struct Near { const Level* level; int dx, dy; };
    mutable std::vector<Near> nearby;                  // linked as neighbours get built (install_level)
    // The act palette (0 = ACT1) it draws with: its Pal, else its act's.
    [[nodiscard]] int palette_act() const { return pal >= 0 ? pal : d2d::rules::level_ids::act_of(id); }
    [[nodiscard]] bool inside(float x, float y) const {
        return x >= 0 && y >= 0 && x < float(ds1.width()) && y < float(ds1.height());
    }
    // `mask` 0x09 walls for walkers; 0x04 the missile barrier (the DT1
    // subtile bit missiles stop on; walk-only 0x01 cells, like the Fallen
    // camp's, let them by).
    [[nodiscard]] bool blocked(float x, float y, std::uint16_t mask = 0x09) const {
        if (!inside(x, y))
            for (const auto& neighbour : nearby)
                if (neighbour.level->inside(x - float(neighbour.dx), y - float(neighbour.dy))) return neighbour.level->blocked_here(x - float(neighbour.dx), y - float(neighbour.dy), mask);
        return blocked_here(x, y, mask);
    }
    [[nodiscard]] bool blocked_here(float x, float y, std::uint16_t mask = 0x09) const {
        const int width = ds1.width() * 5, height = ds1.height() * 5;
        const int screen_x = int(std::floor(x * 5)), screen_y = int(std::floor(y * 5));
        if (screen_x < 0 || screen_y < 0 || screen_x >= width || screen_y >= height || walk.empty()) return true;
        return walk[std::size_t(screen_y) * std::size_t(width) + std::size_t(screen_x)] & mask;
    }
    // Can a small unit (the player, the merc, NPCs) stand at (x, y)? Its
    // collision pattern is a plus: the subtile and its four neighbours,
    // any of them with a bit of `mask` blocks — FUN_0064d4e0, collision
    // pattern 1 (FUN_0064d910). The default is a player's grid part
    // (0x1c09 less the units' 0x1000): walls, player walls, doors and
    // objects. Off the map counts as blocked, like the game's 0x27 outside
    // a room. Units block through the Crowd (ai.hpp unit_blocked).
    [[nodiscard]] bool unit_blocked(float x, float y, std::uint16_t mask = 0x0c09) const {
        return blocked(x, y, mask & 0x0fff) || blocked(x - 0.2f, y, mask & 0x0fff) || blocked(x + 0.2f, y, mask & 0x0fff)
            || blocked(x, y - 0.2f, mask & 0x0fff) || blocked(x, y + 0.2f, mask & 0x0fff);
    }
    // A unit's grid test by its shape: the plus, or for 2 / 4 the 3x3 box
    // x - 1 .. x + 1, y - 1 .. y + 1 whatever its size (FUN_0064d7c0,
    // 0x64d7c6..0x64d7de: an inclusive rect, FUN_0064cf50).
    [[nodiscard]] bool unit_blocked(float x, float y, const UnitShape& shape) const {
        if (shape.pattern == 0) return blocked(x, y, shape.mask & 0x0fff);
        if (!shape.box()) return unit_blocked(x, y, shape.mask);
        const float centre_x = std::floor(x * 5), centre_y = std::floor(y * 5);
        for (int row = -1; row <= 1; ++row)
            for (int column = -1; column <= 1; ++column)
                if (blocked((centre_x + float(column) + 0.5f) / 5, (centre_y + float(row) + 0.5f) / 5, shape.mask & 0x0fff)) return true;
        return false;
    }
    // The nearest spot a unit can stand, searching outward a subtile per
    // ring (FUN_0064dea0 does it per room for spawns).
    [[nodiscard]] std::pair<float, float> nearest_free(float x, float y) const {
        if (!unit_blocked(x, y)) return { x, y };
        for (int ring = 1; ring < 200; ++ring)
            for (int i = -ring; i <= ring; ++i)
                for (auto [offset_x, offset_y] : { std::pair{ i, -ring }, { i, ring }, { -ring, i }, { ring, i } })
                    if (!unit_blocked(x + float(offset_x) * 0.2f, y + float(offset_y) * 0.2f))
                        return { x + float(offset_x) * 0.2f, y + float(offset_y) * 0.2f };
        return { x, y };
    }
    // Where a warp puts a player on subtile (x, y) (FUN_00554ea0 -> FUN_0064e7b0
    // -> FUN_0064dea0, the plus footprint, mask 0x1c09 less the units' bit): the
    // subtile itself when it is free, else the first square ring of subtiles
    // round it (1 to 49: FUN_0064dea0 goes on only while ring + 1 < 50) holding
    // a free one, in it the one of least |dx| + |dy|; ties are FUN_0064dea0's
    // scan order: the left and right columns (row by row, left first), then the
    // top and bottom rows (column by column, top first), then the corners (row
    // by row). None: nullopt.
    // VERIFIED: ring limit and scan order read in FUN_0064dea0; the ring and the
    // distance also against game.exe's own run (sk_tele_spot.py, dense obstacles).
    [[nodiscard]] std::optional<std::pair<int, int>> warp_spot(int x, int y) const {
        const auto is_free = [&](int sub_x, int sub_y) { return !unit_blocked((float(sub_x) + 0.5f) / 5, (float(sub_y) + 0.5f) / 5); };
        if (is_free(x, y)) return std::pair{ x, y };
        for (int ring = 1; ring < 50; ++ring) {
            std::optional<std::pair<int, int>> best;
            int best_distance = 0;
            const auto consider = [&](int column, int row) {
                const int distance = std::abs(column) + std::abs(row);
                if ((best && distance >= best_distance) || !is_free(x + column, y + row)) return;
                best = std::pair{ x + column, y + row };
                best_distance = distance;
            };
            for (int row = 1 - ring; row < ring; ++row) { consider(-ring, row); consider(ring, row); }
            for (int column = 1 - ring; column < ring; ++column) { consider(column, -ring); consider(column, ring); }
            for (const int row : { -ring, ring }) { consider(-ring, row); consider(ring, row); }
            if (best) return best;
        }
        return std::nullopt;
    }
    // FUN_00545340: a spot near subtile (x, y) for something `size` wide
    // whose (size x 2 + 1) square, centred a subtile up-left of it, has no
    // bit of `mask` (FUN_0064d800), searched ring by ring (c = 1 .. rings - 1:
    // rows c, c - 2 .. > -c; columns -c, -c + 2 .. < c). None: nullopt.
    // The mask meets the grid as game.exe's does: its unit bits (0x1000,
    // 0x2000, stamped by FUN_0064ea90) are the Crowd's here, so `crowd`
    // gives them on a subtile; none given: no unit stands in the way.
    [[nodiscard]] std::optional<std::pair<int, int>> free_area(int x, int y, int size, std::uint16_t mask, int rings,
                                                                const std::function<int(int, int)>& crowd = {}) const {
        const int half = size >> 1, side = size * 2 + 1;
        const auto clear = [&](int centre_x, int centre_y) {
            for (int row = 0; row < side; ++row)
                for (int column = 0; column < side; ++column) {
                    const int sub_x = centre_x - side / 2 + column, sub_y = centre_y - side / 2 + row;
                    if (blocked((float(sub_x) + 0.5f) / 5, (float(sub_y) + 0.5f) / 5, mask & 0x0fff)) return false;
                    if (crowd && (crowd(sub_x, sub_y) & mask & 0xf000)) return false;
                }
            return true;
        };
        for (int ring = 1; ring < rings; ++ring)
            for (int row = ring; row > -ring; row -= 2)
                for (int column = -ring; column < ring; column += 2)
                    if (clear(x - half + column, y - half + row)) return std::pair{ x + column, y + row };
        return std::nullopt;
    }
    std::pair<float, float> start{ -1.f, -1.f };            // cells: where a player joining arrives; see load_world
    std::pair<float, float> portal_spot{ -1.f, -1.f };      // cells: a town's portal arrival (DS1 special 33, spawn index 11)
    // Every spawn index's spot (the first tile of each; -1 none): 5 is where
    // an act change of kind 5 lands (Warriv's way back west, FUN_0054b830).
    std::array<std::pair<float, float>, 12> spawn_at = no_spawns();
    // Its warps (the units its hidden warp tiles make): cell, the level
    // it leads to (Levels.txt Vis), its LvlWarp ExitWalk (cells), and where
    // its tile unit stands (cells: the cell x 5 + LvlWarp Offset subtiles).
    // Someone arriving through it lands at the free spot nearest the unit,
    // then walks ExitWalk on from there (FUN_005550b0).
    // pair: its Levels.txt slot's rank among the row's slots to the same
    // level (FUN_0066c220 links the k-th one to the other side's k-th back).
    // select: LvlWarp's SelectX / SelectY / SelectDX / SelectDY, the client's
    // click box in world pixels from the tile unit (World::warp_clicked).
    using Warp = LevelWarp;
    std::vector<Warp> warps;
    // Its rooms' preset units (drlg level_room_tiles, proven against
    // game.exe): level-relative subtiles; load_npcs / load_monsters make
    // them objects, NPCs and monsters.
    std::vector<d2d::drlg::Unit> units;
    // Monsters: its Levels.txt columns, the rooms the generator made (in
    // that order) and the seed each room's room1 gets (drlg BuiltRoom::
    // room1_seed, proven against game.exe), what populating it rolls.
    d2d::rules::LevelMon mon;
    std::vector<d2d::drlg::Outdoor::RoomSeed> rooms;
    std::vector<std::uint32_t> room1_seeds;            // by `rooms` index
    // The client's room seeds (room1 +0x6c / +0x70), from room1_seeds (else the room's own seed):
    // seed_client_units steps one for each unit it seeds there.
    mutable std::vector<d2d::rules::Rng> client_rooms;
    mutable std::vector<std::uint32_t> client_room_starts;   // the room1 seed each began at: relevel can lay a room anew
    // Rooms flagged 0x800000 as built: a hidden warp tile's (FUN_0066e360)
    // or a preset's with LvlPrest Populate 0 (FUN_00666680). FUN_0054ebc0
    // (FUN_0066bb20) populates none there. By `rooms` index.
    std::vector<bool> nopop_rooms;
    // Each room's areas (FUN_0061ad50's list, newest first): a Logicals
    // preset's by walls (drlg BuiltRoom::areas), else the whole room
    // (FUN_0066ccb0). FUN_0054ec90 rolls each. By `rooms` index.
    std::vector<std::vector<d2d::drlg::Area>> room_areas;
    // Each room's floor tiles in its list's order (room1 +8: +8 / +0xc):
    // level tile x, y and DT1 material flags (2 water: FUN_005b2700's spots).
    struct Floor { int x, y; std::uint16_t material; };
    std::vector<std::vector<Floor>> room_floors;
    // Plain outdoor rooms on a road cell (g2c 0x80; room data +0x54 & 0x80):
    // FUN_00552560 (FUN_0061abb0) places no object groups there.
    std::vector<bool> road_rooms;
    // The room2 flags FUN_00552560 reads (0x800000, 0x30000; 0x80 a
    // plain room's path; 0x10 << n its vis slot n, FUN_00667970), and each
    // unit's room, by `rooms` index.
    std::vector<std::uint32_t> room_flags;
    // Its level record's Vis0..7 / Warp0..7 (drlg::level_vis): the level a
    // room flag 0x10 << n reaches, and slot n's LvlWarp (-1 a walk across).
    std::array<int, 8> vis{};
    std::array<int, 8> vis_warp{ -1, -1, -1, -1, -1, -1, -1, -1 };
    std::vector<std::pair<int, int>> starts;           // drlg BuiltRoom::starts, the rooms' in build order
    std::vector<int> unit_rooms;                       // by `units` index
    // What populating a room made before its monsters (room_objects,
    // tools/emu objgroups.py's lines): the room seed going into 552610 and
    // after, the object seed after it, each group object (objects.txt id,
    // level subtile). Spawning::LevelState::group_rooms.
    using GroupRoom = d2d::game::GroupRoom;
    // What brings its rooms up again in another order (relevel): an outdoor
    // level's plain rooms, its DT1s, the assets they're from. Null assets: not generated.
    std::vector<d2d::drlg::PlainRoom> plain;
    LevelDt1s tile_dt1s;
    const d2d::drlg::OutdoorAssets* assets = nullptr;
    std::vector<std::size_t> laid;                     // the order (list indices) relevel last laid its rooms in, empty: list order
    std::size_t laid_up = 0;                           // and how many of them were up (their spawn areas merged)
    // The walk grid as the tiles alone make it (finish_level, before any
    // footprint): what room_objects' collision starts from.
    std::vector<std::uint16_t> tile_walk;
    // How many of `npcs` the level has before any room populates (off-room
    // units, Tristram Cain): a new game cuts `npcs` back to these.
    std::size_t npc_base = 0;
    // Its monster region's MonStats rows by difficulty (trap 8), set when a
    // game first populates it.
    mutable std::array<std::vector<int>, 3> region;
    // Its arrow traps' kind (FUN_005fb650: the game's level record +0x2d4):
    // -1 till one first fires, then 1 the poison ball, else the spike.
    mutable int trap_kind = -1;
};

// Levels.txt cmon1..4 / cpct1..4 / camt1..4 of one row (FUN_0046c460 reads them at record +0xcc).
template <class Text> std::array<Level::Critter, 4> level_critters(const d2d::rules::Monsters& monsters, Text&& text) {
    std::array<Level::Critter, 4> out{};
    for (std::size_t slot = 0; slot < out.size(); ++slot) {
        const std::string id(text("cmon" + std::to_string(slot + 1)));
        out[slot] = { id.empty() ? -1 : monsters.row(id), std::atoi(std::string(text("cpct" + std::to_string(slot + 1))).c_str()),
                      std::atoi(std::string(text("camt" + std::to_string(slot + 1))).c_str()) };
    }
    return out;
}

// The level builder (GameData::builder): one build at a time, on its own MPQ
// handles and its own DRLG tables (the generator caches DT1 heads in them).
struct GameData::LevelBuilder {
    std::mutex mutex;                                       // held for a whole build; `mpqs` and `act1` are its
    std::optional<d2d::mpq::Stack> mpqs;
    std::unique_ptr<d2d::drlg::OutdoorAssets> act1;     // the act's DRLG tables (load_scene's, handed over)
    // What load_npcs read that a build needs: objects.txt (and its rows by
    // Id), Levels.txt, SoundEnviron.txt.
    d2d::txt::Table objects, levels, sound_env;
    std::unordered_map<std::string, std::size_t> obj_row;
    std::map<int, std::future<std::unique_ptr<Level>>> jobs;   // the main thread's: builds under way
};

// A preset level's (no generated rooms.
// `assets`: any generated level's (the presets' rows and DS1s).
std::vector<d2d::drlg::BuiltRoom> preset_rooms(const GameData& game_data, const Level& level, const d2d::drlg::OutdoorAssets& assets);
// The camp's tiles as game.exe's rooms pick them (rarity, on the DRLG room seeds), into Level::picks;
// the rooms in `came_up` (room_rects indices) brought up first in that order, the rest after in list order.
void lay_camp_picks(GameData& game_data, const d2d::drlg::OutdoorAssets& assets, const std::vector<std::size_t>& came_up = {});
// Its rooms' room1 seeds by room_rects index, the rooms in `came_up` (room_rects indices) brought up in that
// order; `generated`: drlg::generate_preset's rooms, which hold the DRLG room seeds.
std::vector<std::uint32_t> preset_room1_seeds(const GameData& game_data, const Level& level, const d2d::drlg::OutdoorAssets& assets,
                                              const std::vector<d2d::drlg::Outdoor::RoomSeed>& generated, const std::vector<std::size_t>& came_up);

void stamp_footprints(Level& level);
// One unit's footprint into the walk grid (stamp_footprints).
void stamp_footprint(Level& level, Npc& npc);
// An object's footprint into (solid) or out of the walk grid, leaving the subtiles a tile blocks too.
void set_footprint(const Level& level, const Npc& npc, bool solid);

// A client unit's first seed.
d2d::rules::Rng client_unit_seed(const Level& level, int room);
// Each object of the level without one its client seed (its room, else the room at its spot), in the level's
// object order: as the level is built, then as each room's objects are made.
void seed_client_units(Level& level);

void add_object(const GameData& game_data, const d2d::txt::Table& objects, const std::unordered_map<std::string, std::size_t>& obj_row,
                Level& into, int oid, int spot_x, int spot_y, d2d::rules::Rng& rgn);
void finish_level(Level& level);
// The tile to draw for `picks[index]` at `elapsed_ms`: an animated one's frame showing now (FUN_0066d3b0), in
// frame 0's place; null for a later frame's own pick. A plain pick's own tile.
[[nodiscard]] const d2d::dt1::Tile* animated_tile(std::span<const Level::Pick> picks, std::size_t index, std::uint32_t elapsed_ms);
// A town of any act: no monsters, no running drain, no town portal.
[[nodiscard]] inline bool in_town(const Level* level) { return level && d2d::rules::level_ids::is_town(level->id); }
// A town's spawn spots from its DS1 specials: Level::start, portal_spot.
void find_spawn_spots(Level& level);
LevelDt1s load_level_dt1s(Level& level, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, int type, d2d::dt1::Pixels pixels);
std::size_t set_level_tiles(Level& level, const d2d::drlg::OutdoorAssets& assets, const LevelDt1s& dt1s,
                            const std::vector<d2d::drlg::Outdoor::RoomSeed>& made, const std::vector<d2d::drlg::PlainRoom>& plain,
                            std::vector<std::string>& notes);
void relevel(Level& level, const std::vector<std::size_t>& came_up, const GameData* game_data = nullptr, const Spawning* spawning = nullptr);

// The near list of the room holding (x, y) cells: each (level, room index) in game.exe's order; empty outside
// every room.
std::vector<std::pair<const Level*, int>> near_rooms(const GameData& game_data, const Level& level, float x, float y);
// The room holding (x, y) cells (its Level::rooms index), -1 none.
int room_of(const Level& level, float x, float y);
// Room `index`'s rect, level tiles: its Level::rooms entry, or the camp's
// 8x8 room (room_of's); none past the last.
std::optional<d2d::drlg::Outdoor::RoomSeed> level_room(const Level& level, std::size_t index);
// The camp's room seeds kept in its LevelState (by level_room index), once;
// false while the builder's busy. Any other level keeps them at population.
bool keep_camp_seeds(const GameData& game_data, const Level& level, Spawning::LevelState& state);
// The level's own monster of MonStats `row`'s family.
int level_monster(const GameData& game_data, const Level& level, int row);
// FUN_0061b130(room, x, y): the spawn area id (record +0x28) at (x, y)
// cells as seen from the room holding (from_x, from_y): that room if it
// holds the point, else the first of its near list that does, any level
// (FUN_00463740); its Logicals rect's id, a whole room's 1; 0 when none.
std::uint32_t area_id_at(const GameData& game_data, const Level& level, float from_x, float from_y, float x, float y);
bool build_outdoor(const GameData& game_data, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, Level& level);
bool build_maze(const GameData& game_data, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, Level& level, std::size_t row);
std::unique_ptr<Level> build_level(const GameData& game_data, GameData::LevelBuilder& builder, int id);
// The Act 1 camp's rooms with their seeds, as drlg-dump has them (GameData::town keeps none):
// empty while the builder's busy (try again), or with no Act 1 tables.
std::optional<std::vector<d2d::drlg::Outdoor::RoomSeed>> camp_room_seeds(const GameData& game_data);
// The camp's rooms' room1 seeds by room_rects index (preset_room1_seeds), `came_up` first; nullopt while the builder's busy.
std::optional<std::vector<std::uint32_t>> camp_room1_seeds(const GameData& game_data, const std::vector<std::size_t>& came_up);
void place_objects(const GameData& game_data, GameData::LevelBuilder& builder, Level& level);
// Room `index` of `level` coming into play.
d2d::rules::Rng room_objects(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t index);
// The level's collision as room_objects sees it, level subtiles row-major: drlg-dump drops.
std::vector<std::uint16_t> object_collision(const Spawning& spawning, const Level& level);
// Every room of the level that populates has (the objrgn record's +4 reaching it).
bool every_room_populated(const Spawning& spawning, const Level& level);
void install_level(const GameData& game_data, int id, std::unique_ptr<Level> level);
std::unique_ptr<Level> finish_job(std::future<std::unique_ptr<Level>>& job, int id);
void want_nearby(const GameData& game_data, const Level& level);

// The acts' layouts (GameData::layouts): where each act's levels sit, in act tiles, and the
// miniature towns' DT1 heads. Behind a pointer in GameData, so a file that doesn't build
// levels needn't parse drlg.
struct ActLayouts {
    std::vector<d2d::drlg::Placed> act1_layout;        // where act 1's levels sit (act tiles)
    std::vector<d2d::drlg::Placed> act2_layout;        // act 2's (its own act tiles: the acts overlap)
    d2d::drlg::Act3Layout act3_layout;                 // act 3's, and its jungle's cells
    std::vector<d2d::drlg::Placed> act4_layout;        // act 4's
    std::vector<d2d::drlg::Placed> act5_layout;        // act 5's
    // The prest_automap levels but the miniature towns (level_ids::kMiniatureTowns): their
    // LvlTypes row's DT1 heads (drlg load_room_dt1s, in the builder's act1
    // tables), so preset_rooms picks their tiles as game.exe (FUN_0066d820).
    std::map<int, d2d::drlg::RoomDt1s> prest_automap_dt1s;
    // The layout level `id`'s act placed it in (empty for acts not laid out).
    [[nodiscard]] const std::vector<d2d::drlg::Placed>& layout_of(int id) const;
};

}  // namespace d2d::game
