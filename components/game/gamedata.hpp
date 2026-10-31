// SPDX-License-Identifier: GPL-3.0-or-later
// GameData: what the rules read (tables, levels, timings, strings) and
// the World's view of them. No pixels, fonts or sound here: Scene
// (scene.hpp) adds those for the client.
#pragma once

#include "game.hpp"

#include <cof.hpp>
#include <cube.hpp>
#include <compcode.hpp>
#include <d2s_items.hpp>
#include <drlg.hpp>
#include <ds1.hpp>
#include <level_ids.hpp>
#include <dt1.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <mpq.hpp>
#include <outdoor.hpp>
#include <outdoor_data.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <tbl.hpp>
#include <tile_pick.hpp>
#include <txt.hpp>
#include <uniques.hpp>
#include <units.hpp>

#include <algorithm>
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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace d2d::game {

// A unit's collision (FUN_006484e0): its pattern from MonStats2 SizeX
// (DAT_006eb3dc 0, 1, 1, 2; over 3: 1), 1 → 3 and 2 → 4 for a MonStats
// npc or inTown that isn't interact (the merc, the cow), or a player's pet
// (unit +0xc4 bit 31, FUN_0056d8d0; FUN_0063e860). Stamped (FUN_0064ea90):
// 0 nothing, 1 / 3 its footprint bit (0x1000, 3 / 4 0x2000) on its
// subtile, 2 / 4 on the plus. Tested (FUN_0064d910): 0 its subtile, 1 / 3
// the plus, 2 / 4 the SizeX box. `mask` (path +0x50) is what blocks it: a player 0x1c09, a
// monster 0x3c01, opendoors 0x3401, flying 0x1804. Players, by default.
struct UnitShape {
    std::uint8_t pattern = 1, size = 2;
    std::uint16_t mask = 0x1c09;
    [[nodiscard]] std::uint16_t bit() const { return pattern >= 3 ? 0x2000 : 0x1000; }
    [[nodiscard]] bool box() const { return pattern == 2 || pattern == 4; }
    [[nodiscard]] bool stamps() const { return pattern >= 1 && pattern <= 4; }
    // A pet's: 1 → 3, 2 → 4 (FUN_00648580 with bit 31 set).
    [[nodiscard]] UnitShape owned() const {
        UnitShape out = *this;
        if (pattern == 1 || pattern == 2) out.pattern = std::uint8_t(pattern + 2);
        return out;
    }
    static UnitShape monster(int size, bool town_unit, bool flying, bool open_doors) {
        const int pattern = size == 0 ? 0 : size == 3 ? 2 : 1;
        const UnitShape shape{ std::uint8_t(pattern), std::uint8_t(size), std::uint16_t(flying ? 0x1804 : open_doors ? 0x3401 : 0x3c01) };
        return town_unit ? shape.owned() : shape;
    }
};

// The town AIs (MonAI 32 Npc FUN_005e7130, 41 Towner FUN_005e7540, 42
// Vendor FUN_005e9e00; town-npcs.md).
enum class TownAi { none, npc, towner, vendor, jarjar };

// Things the DS1 places (its object list): NPCs (type 1, from
// data\global\monsters) and objects (type 2 — torches, fires, the
// waypoint..., from data\global\objects). Both are composites.
struct Npc {
    std::string root;                    // "monsters" or "objects"
    std::string code;                    // <root>\<code>\ ...
    std::string mode;                    // animation mode token (NU, ON...)
    std::string base_w;                  // weapon class ("hth" for objects)
    std::array<std::string, 16> comp;    // per layer, "" = not present
    float x = 0, y = 0;
    int size_x = 0, size_y = 0;          // collision footprint, subtiles
    std::string name;                    // hover label; "" = not selectable
    std::vector<std::pair<float, float>> path;   // DS1 patrol points, cells
    std::vector<int> actions;            // each path point's DS1 action (town_npcs.hpp)
    float velocity = 3;                  // MonStats Velocity
    TownAi town_ai = TownAi::none;       // MonStats AI Npc / Towner / Vendor: thinks with rules::npc_think / vendor_think
    std::uint32_t modes = 0;             // MonStats2 mWL / mS1 / mS2 as bits 2 / 8 / 9 (FUN_0046c140)
    int operate_fn = 0;                  // objects.txt OperateFn (32: the town stash)
    std::optional<int> name_offset;      // objects.txt NameOffset: its hover name's height over its feet (FUN_00454f30)
    int automap = 0;                     // objects.txt AutoMap (+0x1bc): its automap icon's cel, 0 none (FUN_00458dc0)
    bool interact = false;               // MonStats interact (flags bit 9): a white automap mark (FUN_00459bc0)
    UnitShape shape;                     // an NPC's collision (MonStats / MonStats2)
    int object_id = 0;                   // an object's objects.txt Id
    bool door = false, monster_ok = false;   // objects.txt IsDoor (+0x13a), MonsterOK (+0x16d): monsters open it (FUN_005b0f50)
    // What an object stamps into the collision grid while its mode has
    // collision (FUN_006209d0): a door 0x806 with BlocksVis (+0x13b), else
    // 0x804 with BlockMissile (+0x1b6), else 0x400; another object 0x8000
    // with SubClass (+0x167) & 4, else 0x400 (| 4 with BlockMissile).
    std::uint16_t stamp = 0x400;
    std::uint8_t collision = 0;          // an object's HasCollision0..7, a bit a mode (NU OP ON S1..S5)
    std::uint8_t selectable = 0;         // its Selectable0..7, the same way
    std::uint8_t cycle = 0xff;           // its CycleAnim0..7: loops in that mode, else holds the last frame
    std::array<std::uint8_t, 8> frames{};      // its FrameCnt0..7: a mode's length (FUN_004bcbb0), not its COF's
    std::array<std::uint8_t, 8> loop_from{};   // its Start0..7: the frame a mode starts on and a loop goes back to
    // Its FrameDelta0..7: 256 a frame a tick (unit +0x4c, FUN_00624390).
    // ponytail: without Sync game.exe takes 1/16 off and adds up to 1/8 from
    // the client unit's seed (0x624558); d2d runs it as written.
    std::array<std::uint16_t, 8> frame_delta{};
    std::uint32_t walls = 0;             // footprint subtiles a tile blocks too, row-major (stamp_footprints)
    int hc_idx = -1;                     // MonStats hcIdx (NPC menu table key)
    std::string id;                      // MonStats Id (npc.txt key)
    int quest = 0;                       // shown once this Act 1 quest is done (Cain: 4), 0 = always
    // Lut Gholein's palace NPCs, shown by rules::palace_spots: 1 Jerhyn by
    // object 121, 2 / 3 Jerhyn in the palace (object 122's x - 10 / x + 15),
    // 4 the gate guard; 0 = none.
    int palace = 0;
    int op_frames = 0;                   // objects.txt FrameCnt1 when it has an OP mode (Mode1)
    int shrine = 0;                      // a shrine's Shrines.txt row (roll_shrine)
    int trap = 0;                        // a chest's trap type (roll_chest), 0 none
    bool locked = false;                 // a locked chest: takes a key
    bool sparkle = false;                // a sparkling chest (InitFn 57: unit +0x78 & 1)
    bool env_effect = false;             // objects.txt EnvEffect (+0x139): NU by day, OP at dusk, night and dawn (the client's FUN_004bc5e0)
    // An object's unit seed (+0x20, FUN_00555230; a chest's InitFn re-seeds
    // it, roll_chest): what its rounds (FUN_00585b90) roll on. Its room
    // (Level::rooms index, -1 none): a stand rolls on the room's seed.
    mutable d2d::rules::Rng seed;
    int room = -1;
    bool preoperated = false;            // a PreOperate object that starts opened (ON)
    int light = 0;                       // a monster's light radius, subtiles (MonStats2 Light)
    int trans_lvl = 0;                   // MonStats TransLvl: its palshift.dat colour (Fallen 0, Carver 1 ...)
    bool no_unique_shift = false;        // MonStats2 noUniqueShift: a unique keeps its type's colour
    std::array<int, 3> utrans{};         // MonStats2 Utrans by difficulty (0: none)
    int colour = 0;                      // the colour it's drawn in (FUN_00466360; the client's Scene::monster_map), 0/1 none
    int overlay_class = 0;               // which Overlay.txt Height it takes: MonStats2 OverlayHeight - 1 (FUN_006223a0)
    std::array<std::uint8_t, 8> lit{};   // an object's light radius in each mode NU OP ON S1..S5 (objects.txt Lit0..7)
};

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
    struct Pick { std::uint8_t layer, orient; const d2d::dt1::Tile* tile; bool hidden = false; std::uint8_t cell = 0; const d2d::dt1::Tile* stamp = nullptr; };
    std::vector<std::vector<Pick>> picks;               // ds1 width x height, or empty
    // What the screen shows: game.exe's client brings each batch of rooms up
    // in the order they were made (the near list and the ring past it, oldest
    // first), the server the arrived room first; edge sharing makes their
    // picks differ (Lut Gholein's sand: 1280/1280 cells live this way, 950
    // as `picks`). Empty: the same as `picks`. Collision stays the server's.
    std::vector<std::vector<Pick>> drawn;
    std::vector<std::size_t> drawn_laid;                // the order drawn was laid in (list indices), empty: none
    [[nodiscard]] const std::vector<std::vector<Pick>>& shown() const noexcept { return drawn.empty() ? picks : drawn; }
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
    // A unit's grid test by its shape: the plus, or its SizeX box
    // (FUN_0064d7c0: the subtile less half the size).
    [[nodiscard]] bool unit_blocked(float x, float y, const UnitShape& shape) const {
        if (shape.pattern == 0) return blocked(x, y, shape.mask & 0x0fff);
        if (!shape.box()) return unit_blocked(x, y, shape.mask);
        const float left = std::floor(x * 5) - float(shape.size / 2), top = std::floor(y * 5) - float(shape.size / 2);
        for (int row = 0; row < shape.size; ++row)
            for (int column = 0; column < shape.size; ++column)
                if (blocked((left + float(column) + 0.5f) / 5, (top + float(row) + 0.5f) / 5, shape.mask & 0x0fff)) return true;
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
    // FUN_00545340: a spot near subtile (x, y) for something `size` wide
    // whose (size x 2 + 1) square, centred a subtile up-left of it, has no
    // bit of `mask` (FUN_0064d800), searched ring by ring (c = 1 .. rings - 1:
    // rows c, c - 2 .. > -c; columns -c, -c + 2 .. < c). None: nullopt.
    // ponytail: the units' bits (0x1000+) are the Crowd's, not the grid's.
    [[nodiscard]] std::optional<std::pair<int, int>> free_area(int x, int y, int size, std::uint16_t mask, int rings) const {
        const int half = size >> 1, side = size * 2 + 1;
        const auto clear = [&](int centre_x, int centre_y) {
            for (int row = 0; row < side; ++row)
                for (int column = 0; column < side; ++column)
                    if (blocked((float(centre_x - side / 2 + column) + 0.5f) / 5, (float(centre_y - side / 2 + row) + 0.5f) / 5, mask & 0x0fff)) return false;
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
    struct Warp { float x, y; int destination; float exit_x, exit_y, unit_x, unit_y; int slot = 0, pair = 0; };
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
    // plain room's path), and each unit's room, by `rooms` index.
    std::vector<std::uint32_t> room_flags;
    std::vector<std::pair<int, int>> starts;           // drlg BuiltRoom::starts, the rooms' in build order
    std::vector<int> unit_rooms;                       // by `units` index
    // What populating a room made before its monsters (room_objects,
    // tools/emu objgroups.py's lines): the room seed going into 552610 and
    // after, the object seed after it, each group object (objects.txt id,
    // level subtile). Spawning::LevelState::group_rooms.
    struct GroupRoom { std::uint32_t pre = 0, post = 0, rgn = 0; std::vector<std::array<int, 3>> made; };
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

// What the game's rules read: the tables, the levels, animation timings,
// string tables, the MPQs. The World (world.hpp) sees only this, so a
// standalone server loads no graphics beyond the levels' tiles.
// A Level's DT1s carry pixels only when GameData::tile_pixels (the client).
struct GameData {
    // A mode's timing, all the World needs of an animation: its COF name
    // ("AITW1HS"), animdata.d2's rate (256 = a frame per tick), frames per
    // direction and event frame (the hit), the COF's own rate and frames
    // for modes animdata lacks, and its directions (0: no such mode).
    struct AnimTiming {
        std::string   name;
        std::uint32_t speed = 0, frames = 0;
        int           action = -1;
        std::uint32_t cof_speed = 0, cof_frames = 0, directions = 0;
        // One frame's length: 25 ticks/s, speed/256 frames a tick.
        [[nodiscard]] std::uint32_t ms_per_frame() const {
            return 40u * 256u / std::max<std::uint32_t>(speed ? speed : cof_speed, 1);
        }
        [[nodiscard]] std::uint32_t length_ms() const {
            return ms_per_frame() * std::max<std::uint32_t>(frames ? frames : cof_frames, 1);
        }
        // When the attack lands: its event frame, else halfway.
        [[nodiscard]] std::uint32_t action_ms() const {
            return action >= 0 ? ms_per_frame() * std::uint32_t(action) : length_ms() / 2;
        }
    };
    // An NPC's / monster's mode, a character's mode in its gear: read from
    // the COF alone (load.hpp), kept.
    mutable std::map<std::string, AnimTiming> npc_timings;
    mutable std::map<std::array<std::uint8_t, 18>, AnimTiming> composite_timings;
    const AnimTiming& npc_timing(const Npc& npc, std::string_view mode) const;
    const AnimTiming& composite_timing(int d2s_class, int mode, const std::array<std::uint8_t, 32>& gfx) const;   // tints ignored
    // data\global\animdata.d2 by COF name: the game's rate source (COFs of
    // walk/run modes store 0), frames per direction and the event frame.
    struct AnimInfo { std::uint32_t speed = 0, frames = 0; int action = -1; };
    std::unordered_map<std::string, AnimInfo> anim_data;
    using Appearance = std::array<std::uint8_t, 32>;   // the save's 16 layer graphics, then their 16 tints
    std::vector<d2d::compcode::Entry> comp;         // appearance byte -> component
    std::unordered_map<std::string, d2d::compcode::Piece> item_pieces;
    d2d::compcode::Colours item_colours;
    std::optional<d2d::compcode::Types> item_types;    // ItemTypes' hierarchy (a state's itemtype)
    // Overlay.txt (FUN_00470390; record 0x84): the DCC in
    // data\global\overlays, its frames, drawn before the unit (PreDraw), the
    // offset, the height by the unit's class (Height1..4; class -1: +75),
    // AnimRate (x 16 / 256 frames a tick), its draw mode (Trans, as a COF
    // layer's draw effect), its light (InitRadius / Radius, RGB).
    struct OverlayInfo {
        std::string file;
        int frames = 1, x = 0, y = 0, rate = 16, trans = 3, radius = 0, init_radius = 0;
        bool predraw = false;
        std::array<int, 4> height{};
        [[nodiscard]] int dy(int cls) const { return y + (cls >= 0 && cls < 4 ? height[std::size_t(cls)] : 75); }
    };
    std::unordered_map<std::string, OverlayInfo> overlays;
    // States.txt's look: the colour shift that wins by colorpri (PL2's
    // colour shift tables, +0x53500, colorshift - 1), overlays while it lasts
    // (overlay1..4) and once as it starts (castoverlay); the colour worn
    // items of itemtype take (itemtrans).
    struct StateInfo {
        int pri = 0, shift = -1, item_colour = -1;
        std::string item_type;
        std::array<const OverlayInfo*, 4> over{};
        const OverlayInfo* cast = nullptr;
        int gfx_type = 0, gfx_class = 0;   // gfxtype / gfxclass (+0x2d / +0x2e): 2 looks and moves as that player class (FUN_00645270)
        int on_sound = 0, off_sound = 0;   // onsound / offsound (+0x26 / +0x28): on the unit as it comes on (FUN_004d9b20) / goes (FUN_004d9c30)
    };
    std::unordered_map<std::string, StateInfo> states;
    std::vector<std::string> state_names;              // States.txt's names by row (a host's 0xa8 / 0xa9 state ids)
    std::vector<std::uint8_t> colour_shifts;           // ACT1 PL2 +0x53500: 111 x 256
    // Items\Palette\<transform>.dat by Transform 1..8: 21 colours x 256.
    std::array<std::vector<std::uint8_t>, 9> colormaps;   // item code -> its layer, graphic, armour tiers
    // The look of what's worn (compcode::look). A state on the player with
    // an itemtrans colours worn items of its itemtype (FUN_0062c100's first
    // loop: Enchant's red weapons, Venom Claws' green).
    [[nodiscard]] Appearance look_of(const std::vector<d2d::d2s::Item>& items, std::span<const std::string_view> active_states = {}) const {
        std::vector<d2d::compcode::Worn> worn;
        for (const auto& item : items)
            if (item.location == d2d::d2s::item_location::kEquipped) {
                int colour = item_colours.of(item.quality, item.unique_id, item.set_id, item.prefix, item.suffix, item.affixes, item.class_affix,
                                        item.socketed && !item.socketed_items.empty() ? item.socketed_items[0].code : std::string{});
                if (const auto piece = item_pieces.find(item.code); piece != item_pieces.end() && item_types)
                    for (const auto name : active_states)
                        if (const auto state = states.find(std::string(name)); state != states.end() && state->second.item_colour >= 0) {
                            const int want = item_types->index(state->second.item_type);
                            if (item_types->isa(item_types->index(piece->second.type), want) || item_types->isa(item_types->index(piece->second.type2), want)) {
                                colour = state->second.item_colour;
                                break;
                            }
                        }
                worn.push_back({ item.slot, item.code, colour });
            }
        const auto look = d2d::compcode::look(comp, item_pieces, worn), tints = d2d::compcode::tints(item_pieces, worn);
        Appearance appearance;
        std::copy(look.begin(), look.end(), appearance.begin());
        std::copy(tints.begin(), tints.end(), appearance.begin() + 16);
        return appearance;
    }
    std::array<Appearance, 7>         starting_gear{};  // per d2s class, CharStats.txt
    // Kept open for lazy loads after startup.
    d2d::mpq::Stack mpqs;
    // True when 1.14d patch data is layered in. Without it patchstring.tbl
    // is the CD's (826 entries), whose IDs don't match what 1.14d code asks
    // for (10832 is "CREATE NEW" in 1.14d, "Bonus to Attack Rating" on CD).
    bool patched = false;
    // Decode the levels' tile pixels (the client draws them); a headless
    // server keeps only the tiles' headers and walk flags.
    bool tile_pixels = false;
    std::vector<std::int64_t> exp_next;          // experience.txt: exp for level+1, by level
    std::vector<int> exp_ratio;                  // experience.txt ExpRatio, by level (rules::kill_award)
    int exp_ratio_shift = 10, max_level = 99;    // the MaxLvl row's ExpRatio and Amazon
    // PetType.txt rows (Skills.txt pettype names one; "hireable" the merc's):
    // the automap marks it (FUN_00478d90), its portrait (FUN_00494020: icon
    // type 1 one, 2 a count, 3 one of several; the icon by MonStats class,
    // else baseicon, under Hireables\), its name key and life bar.
    struct PetType {
        std::string type;                                // its "pet type"
        bool automap = false, draw_life = false;
        int icon_type = 0;
        std::string name, base_icon;
        std::array<std::pair<int, std::string>, 4> class_icons{};   // mclass1..4 -> micon1..4
    };
    std::vector<PetType> pet_types;                     // in the file's order (the portraits')
    [[nodiscard]] const PetType* pet_type(std::string_view type) const {
        const auto found = std::ranges::find(pet_types, type, &PetType::type);
        return found == pet_types.end() ? nullptr : &*found;
    }
    std::array<std::int64_t, 3> resist_penalty{ 0, -40, -100 };   // DifficultyLevels.txt
    std::array<int, 3> curse_divisor{};          // DifficultyLevels AiCurseDivisor (+0x1c): Confuse / Attract / Dim Vision / Terror's length / it (FUN_005c37a0)
    std::array<int, 3> cold_divisor{};           // DifficultyLevels MonsterColdDivisor (+0x18): a monster's chill length / it
    // Items: parse tables (needs 1.14d ItemStatCost.txt), per-code
    // inventory graphic + size, and the 800x600 inventory panel/layouts.
    std::optional<d2d::d2s::ItemTables> item_tables;
    // gems.txt socket bonuses by gem/rune code, per slot kind (weapon,
    // helm/armour, shield), already resolved through Properties.txt.
    std::unordered_map<std::string, std::array<std::vector<d2d::d2s::ItemProp>, 3>> gem_props;
    // String keys for item names, indexed the way the save's IDs are (see
    // item_lines): uniques/sets by row without separators, magic affixes by
    // raw row, rare names by raw row, runewords by RunewordN rank.
    struct ItemNames {
        std::vector<std::string> unique, set, prefix, suffix, rare_pre, rare_suf, runeword;
    } item_names;
    // What the hover text (item_lines, FUN_0048dd90) reads off the items
    // record by code: wclass (+0xc0), mindam/maxdam (+0xfe/+0xff),
    // 2handmindam/2handmaxdam (+0x102/+0x103), minmisdam/maxmisdam
    // (+0x100/+0x101), nodurability (+0x113), misc.txt spelldesc (+0xb4),
    // spelldescstr (+0xb6), spelldesccalc (+0xa4) and stat1's ID (+0x9e).
    // The level requirements by save ID, indexed like item_names.
    struct ItemDesc {
        std::string wclass, spell_str;
        std::array<int, 6> dam{};                // one-hand, two-hand, throw: min, max
        bool nodurability = false;
        int spell_desc = 0, spell_calc = 0, spell_stat = -1;
    };
    std::unordered_map<std::string, ItemDesc> item_desc;
    std::vector<int> prefix_req, suffix_req, unique_req, set_req;
    // UniqueItems / SetItems dropsound, usesound by row (item_sound).
    std::vector<std::array<std::string, 2>> unique_sounds, set_sounds;
    // ItemStatCost.txt description columns, by stat ID, and what the skill
    // descfuncs need: skill name keys by skill ID, CharStats strings by class.
    struct StatDesc {
        int prio = 0, func = 0, val = 0, operation = 0, op_param = 0, dgrp = 0, dgrp_func = 0, dgrp_val = 0;
        std::string pos, neg, str2, dgrp_pos, dgrp_neg, dgrp_str2;
        int send_bits = 0, send_param = 0;      // Send Bits / Send Param Bits (+8 / +9): a stat list's widths on the wire (0xa8, 0xaa)
    };
    std::vector<StatDesc> stat_desc;
    std::vector<std::string> skill_name;         // string key
    std::vector<int>         skill_class;        // CharStats row, -1 none
    struct ClassStrs { std::string all_skills, tab[3], only; };
    std::array<ClassStrs, 7> class_strs;
    d2d::rules::Tables rules;                    // item/vendor/price tables (components/rules)
    // LvlPrest Outdoors by Def: game.exe flags those presets' rooms 0x80000
    // (FUN_00667ed0), where monsters need no line of sight (FUN_0066ba70).
    std::vector<bool> prest_outdoors;
    // belts.txt 800x600 rows ("belt2" .. "uber belt", after the Expansion
    // separator) by armor.txt `belt` index: box count and boxes 1..16
    // {left, right, top, bottom}. Index 2 ("default") when no belt is worn.
    struct Belt { int boxes = 4; std::array<std::array<int, 4>, 16> box{}; };
    std::array<Belt, 7> belts{};
    struct InvLayout {
        int panel_x = 400, panel_y = 60;
        int grid_x = 0, grid_y = 0, box_w = 29, box_h = 29;
        int cols = 0, rows = 0;                     // gridX, gridY
        std::array<std::array<int, 4>, 11> slots{};   // by body slot 1..10: x, y, w, h
    };
    std::array<InvLayout, 7> inv_layout{};            // by d2s class
    // Stash: inventory.txt "Bank Page2" (classic, 6x4) / "Big Bank Page2"
    // (expansion, 6x8) grids; art PANEL\bank / PANEL\TradeStash (game.exe
    // loads the latter only for expansion games, FUN_00489e50), drawn in
    // the left-panel spot like the char panel (0x48f1a4).
    std::array<InvLayout, 2> stash_layout{};          // [expansion]
    // A trade with another player: inventory.txt "Trade Page 1-2" (their
    // offer, top) and "Trade Page 2-2" (ours), 10x4 each; art PANEL\trade
    // (FUN_00489360), the left-panel spot.
    std::array<InvLayout, 2> trade_layout{};          // [0] theirs, [1] ours
    // Horadric Cube: inventory.txt "Transmogrify Box2" (3x4), art
    // PANEL\supertransmogrifier (FUN_0048a4b0), same left-panel spot
    // (panel 0xe, 0x48eeca).
    InvLayout cube_layout{};
    InvLayout merc_layout{};                          // inventory.txt "Hireling2": the Hireling screen's body slots
    std::array<d2d::rules::ClassGains, 7> class_gains{};   // by d2s class (CharStats)
    // A new character's start (CharStats.txt): str, dex, int (energy), vit,
    // stamina, hpadd; item1..10 with their loc and count; StartSkill.
    struct ClassStart {
        int str = 0, dex = 0, ene = 0, vit = 0, stamina = 0, hpadd = 0;
        struct Item { std::string code, loc; int count = 0; };
        std::vector<Item> items;
        std::string start_skill;
    };
    std::array<ClassStart, 7> class_start{};
    // D2's three-tier string tables. Lookup order per D2's own convention:
    //   patchstring.tbl (826 entries) — patch-shipped overrides, wins
    //   expansionstring.tbl (2788 entries) — LoD additions (Druid/Assassin
    //     class names live here in some builds, but 1.14d put them in
    //     patchstring.tbl — see class-table.md)
    //   string.tbl (5099 entries) — base classic keys
    // Frontend button labels come out via ID lookup (see
    // docs/research/re/frontend-menu-table.md — records at 0x708ec0+ carry
    // TBL ids 0x13f2..0x13f7 in the +0x18 field). Class-name keys are bare
    // ("Barbarian", "Assassin", "Druid", …) — see class-table.md.
    d2d::tbl::Table       strings;         // string.tbl
    d2d::tbl::Table       patch_strings;   // patchstring.tbl (has Druid/Assassin)
    d2d::tbl::Table       exp_strings;     // expansionstring.tbl
    Level town;                                        // the Rogue Encampment, built at start
    // Every other level: built from the map seed the first time it's
    // wanted (the same on every machine), then kept. `level` builds it or
    // waits for its build; `want_level` starts one on the builder thread
    // (the levels next to the player's); `poll_levels` takes in finished
    // builds and links outdoor neighbours (Level::nearby). load.hpp.
    mutable std::map<int, std::unique_ptr<Level>> levels;
    const Level* level(int id) const;
    // The town of level `id`'s act: the camp, or a later act's town (built
    // on demand like any level; null while its act isn't built).
    [[nodiscard]] const Level* town_of(int id) const { return level(d2d::rules::level_ids::kActTowns[std::size_t(d2d::rules::level_ids::act_of(id))]); }
    void want_level(int id) const;
    void poll_levels() const;
    // SuperUniques.txt (without its Expansion row): name, MonStats row of
    // its Class, minions.
    struct SuperUnique { std::string name; int type = -1, min_grp = 0, max_grp = 0; std::vector<int> mods; std::array<std::string, 3> treasure_classes;
                         std::array<int, 3> utrans{}; bool autopos = false, stacks = false;   // Utrans by difficulty: its colour; AutoPos, Stacks
                         std::string sound; };                                                   // MonSound: its own MonSounds row (rules::boss_sound)
    std::vector<SuperUnique> superuniques;
    std::vector<d2d::rules::CubeRecipe> cube_recipes;    // cubemain.txt, compiled (rules::compile_cube_input / _output)
    d2d::rules::UMods umods;                           // MonUMod.txt: champion / unique mods and constants
    std::array<std::vector<std::string>, 3> unique_names;   // UniquePrefix / Suffix / Appellation, resolved
    std::array<std::string, 2> unique_formats;         // strings 0x6b9 ("%0 %1"), 0x6ba ("%0 %1 %2")
    std::vector<d2d::rules::ShrineRow> shrines;        // Shrines.txt
    std::vector<std::array<int, 4>> area_level;        // Levels.txt MonLvl1Ex..3Ex, then classic MonLvl1, by Id
    std::vector<d2d::rules::LevelMon> level_mon;       // Levels.txt monster columns, by Id
    std::vector<d2d::rules::ObjGroup> obj_groups;      // objgroup.txt rows by Offset (FUN_00552610)
    std::vector<std::uint8_t> obj_subclass;            // objects.txt SubClass by Id (+0x167: 552610's throttle, 0x40 a waypoint)
    std::vector<std::uint8_t> field;                   // expfield.d2: 256 x 256 directions (0-7, 8 the centre) toward (128, 128)
    std::uint32_t map_seed = 3;                        // act layout + levels (3: townE1)
    int difficulty = 0;                                // the levels' (a maze's rooms: LvlMaze Rooms by difficulty)
    std::vector<d2d::drlg::Placed> act1_layout;        // where act 1's levels sit (act tiles)
    std::vector<d2d::drlg::Placed> act2_layout;        // act 2's (its own act tiles: the acts overlap)
    d2d::drlg::Act3Layout act3_layout;                 // act 3's, and its jungle's cells
    std::vector<d2d::drlg::Placed> act4_layout;        // act 4's
    std::vector<d2d::drlg::Placed> act5_layout;        // act 5's
    // The layout level `id`'s act placed it in (empty for acts not laid out).
    [[nodiscard]] const std::vector<d2d::drlg::Placed>& layout_of(int id) const {
        static const std::vector<d2d::drlg::Placed> kNone;
        switch (d2d::rules::level_ids::act_of(id)) {
            case 0: return act1_layout;
            case 1: return act2_layout;
            case 2: return act3_layout.levels;
            case 3: return act4_layout;
            case 4: return act5_layout;
            default: return kNone;
        }
    }
    // Mercenary units by hireling.txt Id (the save's merc type): the
    // monster, and the first name key (merc01, merca201, MercX101, ...).
    struct Merc { Npc npc; std::string name_first; };
    // MonSeq.txt: a monster skill's sequence (its MonStats Sk*mode names
    // one), frame by frame: the mode played, its frame, the event (1 a hit).
    struct MonSeqFrame { std::string mode; int frame = 0, event = 0; };
    std::unordered_map<std::string, std::vector<MonSeqFrame>> mon_seqs;
    std::unordered_map<int, Merc> mercs;
    // Waypoints (docs/research/re/waypoint.md): Levels.txt rows with a
    // Waypoint index, per act in index order; art ui\menu\waygate*.
    struct WaypointLevel { int waypoint = 0, level = 0; std::string name; };
    std::array<std::vector<WaypointLevel>, 5> waypoint_levels;
    std::unordered_map<int, std::string> level_names;  // Levels.txt Id -> LevelName (a string key)
    // Sounds.txt by Index: file (under data\global\sfx or, for speech,
    // data\local\sfx) and volume 0..255.
    struct Sound { std::string file; int volume = 255; bool loop = false, music = false; int fade_in = 0, fade_out = 0;
                   int group = 0;                      // Group Size: variants at the following indices
                   std::array<int, 3> block{ -1, -1, -1 };      // a song's Block 1..3: cue points, in sample frames
                   int falloff = 0;                     // Falloff (record +0x47): its near / far range (audio.hpp)
                   bool tracking = false;               // Tracking: follows its unit, its volume and pan redone each tick
                   // How it starts against the playing ones (sound.md "Starting a sound"): Defer Inst /
                   // Stop Inst (record +0x41 / +0x42), Compound (+0x45, ticks, < 0 any), Solo (+0x52),
                   // Priority (+0x4e); group_base (+0x60) the first row of its group, itself when none.
                   bool defer = false, stop_inst = false, solo = false;
                   int compound = 0, priority = 0, group_base = 0; };
    std::unordered_map<std::string, int> sound_index;   // Sounds.txt Sound -> Index
    // MonSounds.txt by Id (MonStats MonSound): per attack mode (A1, A2) the
    // attack and weapon sounds, their delays in ticks and the attack sound's
    // chance; the get-hit and death sounds and delays.
    struct MonSound {
        std::array<int, 2> attack{}, weapon{}, att_del{}, wea_del{}, att_prb{ 100, 100 };
        int hit = 0, death = 0, hit_del = 0, death_del = 0;
        // Skill1..4 (+0x44..: on S1..S4, FUN_004cb890), Neutral and NeuTime (+0x68 / +0x6c, ticks:
        // FUN_004cb460), Init (+0x70: as it first shows, FUN_004cc380), Footstep, FootstepLayer and
        // FsCnt / FsOff / FsPrb (+0x54..+0x64: FUN_004caf60).
        std::array<int, 4> skill{};
        int neutral = 0, neutral_time = 0, init = 0, taunt = 0, flee = 0;
        int footstep = 0, footstep_layer = 0, fs_count = 0, fs_offset = 0, fs_chance = 100;
    };
    std::unordered_map<std::string, MonSound> mon_sounds;
    std::vector<Sound> sounds;
    fs::path data_dir;                                  // the MPQs' folder
    // CharStats WalkVelocity / RunVelocity by d2s class. Running adds
    // run*100/walk - 100 to velocitypercent (FUN_00620e80): +50%.
    std::array<int, 7> walk_velocity{ 6, 6, 6, 6, 6, 6, 6 }, run_velocity{ 9, 9, 9, 9, 9, 9, 9 };
    std::array<int, 7> run_drain{ 20, 20, 20, 20, 20, 20, 20 };   // CharStats RunDrain (CharStats +0x42)
    d2d::rules::Monsters monsters;                      // MonStats / MonStats2 / MonLvl
    d2d::rules::SkillTables skills;                     // Skills.txt, compiled calcs (skills.hpp)
    std::vector<Npc> mon_npc;                           // by MonStats row: its composite recipe
    // Missiles.txt rows monsters and skills fire: velocity (units like
    // MonStats Velocity), range in ticks (+ LevRange a skill level),
    // SrcDamage (128 = all the attack's damage), its own damage, animation
    // (AnimSpeed/16 frames a tick over AnimLen; its CelFile DCC, 32
    // directions, is in missile_cels); for a skill's: the Skill whose damage it carries
    // (FUN_0064b860), ToHit (rolls the attack rating), CollideKill (spent on
    // its first hit; else flies through), Pierce (may fly on, stat 328 %),
    // pSrvHitFunc (+0x0e, table 0x73c840) and its sHitPar1 (+0x4c).
    struct MissileInfo { std::string name, cel_file; int vel = 0, range = 0, src_damage = 0, min = 0, max = 0, anim_speed = 16, anim_len = 1;
                         int vel_lev = 0, max_vel = 0, accel = 0, activate = 0;   // its flight (rules::MissileFlight)
                         std::string skill; int lev_range = 0, hit_func = 0, hit_par1 = 0;
                         int hit_class = 0;                                     // HitClass, a number (+0x94, FUN_00661b20 type 4): its impact sound
                         int travel_sound = 0, hit_sound = 0, prog_sound = 0;   // TravelSound (+0x12), HitSound (+0x14), ProgSound (+0x34)
                         bool to_hit = false, collide_kill = true, pierce = false;
                         bool return_fire = false;   // ReturnFire (record flag bit 9, +5 & 2): Chilling Armor answers it (FUN_005cab40)
                         // Its own element (a row with no Skill: FUN_0064b100 / 0064b1d0 /
                         // 0064b2a0 by level): Skill::etype order, frze as cold.
                         int etype = -1, emin = 0, emax = 0, hitshift = 8, elen = 0;
                         std::array<int, 5> emin_lev{}, emax_lev{}; std::array<int, 3> elen_lev{};
                         std::string edmg_sym_text; d2d::rules::Calc edmg_sym;   // EDmgSymPerCalc (+0x118), compiled with the skills
                         std::string hit_calc_text; d2d::rules::Calc hit_calc;   // SHitCalc1 (+0x88): Immolation Arrow's fire length
                         // Its server functions' inputs: pSrvDoFunc (table 0x73c768), Param1..,
                         // sHitPar2, the rows it spawns (SubMissile1, HitSubMissile1).
                         int clt_do = 0;   // pCltDoFunc (the client's, table 0x72a398 + 4 x n)
                         int srv_do = 0, param1 = 0, param2 = 0, param3 = 0, hit_par2 = 0;
                         bool next_hit = false; int next_delay = 0;   // NextHit: it strikes a unit again NextDelay frames on
                         std::string sub, sub2, hit_sub;
                         std::array<const MissileInfo*, 2> subs{}; const MissileInfo* hit_sub_info = nullptr;   // those rows, loaded
                         // ExplosionMissile (+0x16): played where it hits a unit or a wall,
                         // and at its range's end too when AlwaysExplode (+0x18c) (FUN_004d2d70).
                         std::string explosion_name; const MissileInfo* explosion = nullptr; bool always_explode = false;
                         int light = 0;   // Light: its light radius, subtiles
                         int size = 0;    // Size: its unit collision shape (rules::missile_touches; 0 strikes none)
                         int trans = 0;   // Trans (record +0x18d): 1 additive, 2 multiply (FUN_004720xx: draw modes 3 / 4)
                       };
    std::unordered_map<std::string, MissileInfo> missiles;
    std::array<Npc, 2> trap_fires;                         // objects 162 / 160, ON (a chest's traps 5 and 7)
    Npc afterglow;                                         // object 131 (9b), OP: the "vile dog afterglow" the Dark Wanderer leaves
    Npc town_portal;                                       // object 59 (TP): a town portal's look and light
    std::unordered_map<std::string, std::string> thrown;   // a throwing weapon's code: its Missiles.txt row (weapons.txt missiletype)
    std::vector<std::size_t> mon_bin;                  // game.exe's MonStats unit ids -> rows (no Expansion row)
    std::vector<bool> mon_is_npc;                      // by MonStats row: MonStats npc
    // MonEquip.txt's oninit rows (FUN_005d6b60 as a monster's made), by
    // MonStats row: each row's item count (1..3). Only Blood Raven's and the
    // flying scimitar's in 1.14d, both from level 0.
    std::vector<std::vector<int>> mon_equip_oninit;
    // The level builder: its own MPQ handles, the DRLG tables and the
    // tables a build reads. Last, so it's torn down first: a build in
    // flight reads the rest of GameData.
    struct LevelBuilder;
    std::shared_ptr<LevelBuilder> builder;
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

// D2 TBL values are UTF-16; our font is Latin-1. Downcast char by char.
inline std::string u16_to_latin1(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char16_t code_unit : text) {
        // Keep printable Latin-1 (0x20..0xFF) and line breaks (two-line
        // labels like "Fire\nResistance"), drop the rest — D2 UI strings
        // are ASCII with occasional accented chars, all inside Latin-1.
        if ((code_unit >= 0x20 && code_unit <= 0xFF) || code_unit == '\n') out.push_back(char(code_unit));
    }
    return out;
}

// TBL lookup with D2's precedence: patch → expansion → base. First-hit wins,
// matching how the game resolves any string ID/key at runtime.
inline std::optional<std::u16string_view>
lookup_string(const GameData& game_data, std::string_view key) {
    if (auto found = game_data.patch_strings.get(key); found && !found->empty()) return found;
    if (auto found = game_data.exp_strings.get(key);   found && !found->empty()) return found;
    if (auto found = game_data.strings.get(key);       found && !found->empty()) return found;
    return std::nullopt;
}
inline std::optional<std::u16string_view>
lookup_string(const GameData& game_data, std::uint16_t id) {
    // Numeric IDs are banked, not layered (RE'd from char-select: 0x58cb =
    // 22731 resolves to expansionstring[2731] "EXPANSION CHARACTER"):
    //   0..9999 string.tbl, 10000..19999 patchstring, 20000+ expansionstring.
    // Trying every table with the raw ID hits the wrong one — string.tbl
    // 2731 is "Bile".
    if (id >= 10000 && id < 20000 && !game_data.patched) return std::nullopt;
    const auto& table = id >= 20000 ? game_data.exp_strings : id >= 10000 ? game_data.patch_strings : game_data.strings;
    const auto local = std::uint16_t(id >= 20000 ? id - 20000 : id >= 10000 ? id - 10000 : id);
    if (auto found = table.get(local); found && !found->empty()) return found;
    return std::nullopt;
}

inline std::string string_id(const GameData& game_data, std::uint16_t id) {
    const auto found = lookup_string(game_data, id);
    return found ? u16_to_latin1(*found) : std::string{};
}

// Composite tokens: d2s class id -> CHARS folder (Assassin is "AI", its
// dev codename), D2 mode ids we use, and layer names by COF type.
constexpr const char* kCharCode[7] = { "AM", "SO", "NE", "PA", "BA", "DZ", "AI" };
constexpr int kModeDT = 0, kModeNU = 1, kModeWL = 2, kModeRN = 3, kModeGH = 4, kModeTN = 5, kModeTW = 6,
              kModeA1 = 7, kModeA2 = 8, kModeBL = 9, kModeSC = 10, kModeKK = 12, kModeS1 = 13, kModeS3 = 15, kModeS4 = 16, kModeDD = 17;

// Movement speed from a unit's velocity (CharStats Walk/RunVelocity,
// MonStats Velocity): the path velocity is velocity << 8 (scaled by
// velocitypercent, FUN_00462a20), and a unit covers path velocity / 4096
// subtiles per 40 ms tick (arrival time (dist << 16) / ((v >> 8) << 12),
// 0x4c86a3) — velocity / 16 subtiles a tick. Walk 6: 1.875 cells/s.
constexpr float cells_per_sec(float velocity) { return velocity / 16.f * 25.f / 5.f; }

// Direction (0..15, D2's DCC order) for a world-space step (dx, dy) in
// cells. Directions are screen-space: project to screen, take the angle
// clockwise from straight down, and map the 16 sectors through D2's
// ordering — the 8 main directions first (0 SW, 1 NW, 2 NE, 3 SE, 4 S,
// 5 W, 6 N, 7 E), then the half-steps (8 between S and SW, ...).
inline constexpr int kDir16FromSector[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
inline constexpr int kDir32FromSector[32] = { 4, 16, 8, 17, 0, 18, 9, 19, 5, 20, 10, 21, 1, 22, 11, 23,
                                              6, 24, 12, 25, 2, 26, 13, 27, 7, 28, 14, 29, 3, 30, 15, 31 };
inline int direction16(float dx, float dy) {
    const auto& kFromSector = kDir16FromSector;
    const float screen_x = (dx - dy) * (kIsoW / 2), screen_y = (dx + dy) * (kIsoH / 2);
    const float angle = std::atan2(-screen_x, screen_y);                    // 0 = down, + = clockwise
    const int sector = int(std::lround(angle / (2 * 3.14159265f / 16)));
    return kFromSector[std::size_t((sector % 16 + 16) % 16)];
}
// A composite's direction for a 16-direction facing: 0..7 are the eight
// compass points, 8..15 the ones between; an 8-direction composite (town
// NPCs, mercs) takes the neighbouring point for those — clamping them
// made NPCs walk backwards. ponytail: the neighbour counter-clockwise;
// game.exe maps its 64 unit directions per direction count, not RE'd.
inline std::uint8_t cof_direction(int dir16, int dirs) {
    constexpr int k16to8[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 4, 0, 5, 1, 6, 2, 7, 3 };
    const int dir = dirs == 8 && dir16 >= 0 && dir16 < 16 ? k16to8[dir16] : dir16;
    return std::uint8_t(std::clamp(dir, 0, std::max(dirs - 1, 0)));
}
// A missile's frame in a sprite of `dirs` directions for its 64-step
// facing `dir64` (FUN_00600c90: the table at 0x6e45a0, [log2(dirs) + 1]
// [dir64]; fireball.dcc and firebolt.dcc have 16). The table rounds the
// 64 steps to the nearest of the sprite's own, clockwise from straight
// down, in D2's DCC order for that count (8: S SW W NW N NE E SE; 4 by
// angle; 2's row is all 0). Checked against it for every step and for 1,
// 2, 4, 8, 16 and 32 directions (tools/emu/missile_dirs.py table).
inline std::uint8_t missile_direction(int dir64, int dirs) {
    if (dirs <= 2) return 0;
    const int step = 64 / std::min(dirs, 64);
    const int sector = ((dir64 & 0x3f) + step / 2) / step % dirs;
    if (dirs == 32) return std::uint8_t(kDir32FromSector[std::size_t(sector)]);
    if (dirs == 16) return std::uint8_t(kDir16FromSector[std::size_t(sector)]);
    if (dirs == 8) return std::uint8_t(kDir16FromSector[std::size_t(sector * 2)]);
    return std::uint8_t(sector);
}
constexpr const char* kModeCode[18] = { "DT", "NU", "WL", "RN", "GH", "TN", "TW", "A1", "A2", "BL", "SC",
                                        "TH", "KK", "S1", "S2", "S3", "S4", "DD" };
constexpr const char* kLayerCode[16] = {
    "HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
};

// Loading and building (gamedata.cpp).
// Translate a DS1-embedded tileset path (e.g.
// "\d2\data\global\tiles\act1\town\floor.dt1") into the MPQ path we can hand to Stack::try_read. The
// DS1 files store paths as they were on Blizzard's build box, with a
// leading "\d2\" prefix and forward slashes never — normalize both.
[[nodiscard]] inline std::string ds1_path_to_mpq(std::string_view path) {
    if (path.size() > 4 && (path.starts_with("\\d2\\") || path.starts_with("/d2/")))
        path.remove_prefix(4);
    else if (!path.empty() && (path[0] == '\\' || path[0] == '/'))
        path.remove_prefix(1);
    std::string out(path);
    for (auto& letter : out) if (letter == '/') letter = '\\';
    return out;
}

// MonStats2 layer variants ("lit,med": quoted lists), HDv..S8v.
constexpr const char* kVariant[16] = {
    "HDv", "TRv", "LGv", "RAv", "LAv", "RHv", "LHv", "SHv",
    "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v",
};

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

// Encode (style, sequence, type) into a single lookup key. Style + sequence
// are DS1-record bytes; type is the DT1 orientation code (0..16 per D2's
// tile-type table). 24 bits × 24 bits × 16 bits comfortably fits u64.
[[nodiscard]] inline std::uint64_t tile_key(int style, int seq, int type) {
    return (std::uint64_t(std::uint32_t(style)) << 40)
         | (std::uint64_t(std::uint32_t(seq  )) << 16)
         |  std::uint64_t(std::uint16_t(type ));
}

// The Blood Moor from the map seed (components/drlg): act 1's layout
// places it against the town, the generator fills it, its tiles come from
// the Act 1 wilderness DT1s (LvlTypes).
// The levels d2d builds so far: Act 1's (the town is GameData::town),
// Act 2's, Kurast Docks (75: Meshif's boat lands there), the jungle
// (76-78), Kurast up to Travincal (79-83) and its dungeons (84-102),
// Act 4 (103-108) and Act 5 (109-136, the uber levels too).
constexpr std::array kBuiltLevels{ 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
                                    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83,
                                    84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108,
                                    109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130,
                                    131, 132, 133, 134, 135, 136 };

// A composite's COF and timing (the World's part of a composite).
struct CofAnim { d2d::cof::Cof cof; GameData::AnimTiming timing; std::string path; bool ok = false; };
CofAnim open_cof(const d2d::mpq::Stack& mpqs, const std::string& path);
CofAnim player_cof(const d2d::mpq::Stack& mpqs, const std::vector<d2d::compcode::Entry>& comp, int cls, int mode,
                   const std::array<std::uint8_t, 32>& gfx);
CofAnim npc_cof(const d2d::mpq::Stack& mpqs, const Npc& npc, std::string_view mode);

std::vector<std::string> split_variants(std::string_view text);
std::unordered_map<std::string, std::size_t> id_rows(const d2d::txt::Table& table);
int level_light(std::string_view intensity, std::string_view red, std::string_view green, std::string_view blue);
Npc monster_npc(const GameData& game_data, const d2d::txt::Table& monstats, const d2d::txt::Table& ms2,
                const std::unordered_map<std::string, std::size_t>& ms2_rows, std::size_t row);
// A game's monster spawning (docs/research/re/monsters.md "When a room
// populates"). game.exe populates a room once, the tick after it first
// comes into play (FUN_0052d160): the rooms one step from the player's in
// its near list, across a level's edge and a warp too. Every level's
// region is made at game start; the game seed then takes three more steps
// (the object seed, sunitproxy, quests) before the first room rolls on it.
struct ObjectRooms;                                     // objgroups.cpp
struct Spawning {
    struct LevelState {
        std::vector<d2d::rules::Spawn> spawns;             // every spawn so far, level-relative subtiles
        std::vector<bool> up;                              // by Level::rooms index: its room1 made (and populated)
        std::vector<std::size_t> order;                    // Level::rooms indices as they came up
        std::vector<std::size_t> client_order;             // the same, each batch in made order (Level::drawn)
        // The level's object placement as it stands (objgroups.cpp): objrgn's
        // record of it, the units' collision so far; each room's object
        // groups (by Level::rooms index) and its seed after its monsters,
        // what a stand draws on.
        std::shared_ptr<ObjectRooms> objects;
        std::vector<Level::GroupRoom> group_rooms;
        std::vector<d2d::rules::Rng> room_seeds;
        // A room's room1 seed stepped before it populated: a water monster's
        // party searching the room its leader stands in (FUN_005b2700).
        std::vector<std::optional<d2d::rules::Rng>> early_seeds;
    };
    int difficulty = 0;
    d2d::rules::Rng game;
    // The game's object seed (game +0x10f0, FUN_00546c60): every room's
    // objects, shrines, chests and every container opened step it, game-wide.
    d2d::rules::Rng objects;
    // The sunitproxy seed (FUN_00536070: {a game seed step, 666}, game
    // +0x1d24 +8, FUN_00536b40): the hire lists and the vendors' stock roll on it.
    d2d::rules::Rng proxy;
    d2d::rules::Rng quests;                 // the quests' seed (game +0x10f4 +0x18): its timers and drops (FUN_005438b0)
    std::vector<d2d::rules::Region> regions;               // by Levels.txt Id
    std::unordered_map<const Level*, LevelState> levels;
    std::bitset<128> superuniques;                         // spawned this game (game +0x1d30)
    // a5q2's cages up so far (its record's data +0x86 / +0x8c, game-wide):
    // level id, spot (level subtiles).
    std::vector<std::array<int, 3>> rescue_cages;
    int tomb_symbols = 0;                                  // the Summoner's symbols handed out (FUN_0059d720: record 13 +0x3a)
    // The game's quest words (World::game_bits, a d2d::rules::QuestBits), as
    // the town markers' inits read them (FUN_00544760).
    std::array<std::uint8_t, 96> game_quests{};
    // a3q5's record 0x13, its data +0xc: Khalim's Will done by a player at the
    // join (word 0x12 bit 0, FUN_005bb480): the compelling orb comes up broken.
    bool orb_smashed = false;
    const Level* room_level = nullptr;                     // the player's room
    int room = -1;
};
Spawning start_spawning(const GameData& game_data, int difficulty);
// The player at (x, y), cells, in `level`: rooms coming into play round
// the player's room populate, newest first. `arrived`: the player just
// came in, and its own room populates first (FUN_0056cf40). Returns each
// level whose spawns grew, with its first new spawn's index.
std::vector<std::pair<const Level*, std::size_t>> player_moved(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 float x, float y, bool arrived);
// Where a missile in the room holding (x, y) cells can be: that room's rect,
// then its near list's, act subtiles {x, y, w, h} (FUN_00463740 over room1
// +0 / +0x24). Empty when no room holds it.
// ponytail: every near room counts as up (game.exe skips one that isn't).
std::vector<std::array<int, 4>> missile_rooms(const GameData& game_data, const Level& level, float x, float y);
// Every room of the level populated at once, newest first: tools/emu
// monsters.py's order, for diffing against game.exe (drlg-dump monsters).
// The rooms come up `came_up` (Level::rooms indices) first, the rest in list
// order. Returns each room in the order populated with its first spawn's
// index; `done` hears of each room as it's populated.
std::vector<std::pair<std::size_t, std::size_t>> populate_level(const GameData& game_data, Spawning& spawning, const Level& level,
                                                                 const std::vector<std::size_t>& came_up = {},
                                                                 const std::function<void(std::size_t)>& done = {});
void stamp_footprints(Level& level);
// One unit's footprint into the walk grid (stamp_footprints).
void stamp_footprint(Level& level, Npc& npc);
// An object's footprint into (solid) or out of the walk grid, leaving the
// subtiles a tile blocks too (FUN_0064de30 / FUN_0064dc00).
void set_footprint(const Level& level, const Npc& npc, bool solid);

// A missile row's own elemental damage in 256ths (FUN_0064b100 /
// FUN_0064b1d0): (EMin + MinELev brackets) plus its EDmgSymPerCalc % of
// that (FUN_00483360), then << HitShift.
inline int missile_elem(const GameData& game_data, const GameData::MissileInfo& missile, const d2d::rules::CalcEnv& env, int skill_id, int lvl, bool max) {
    int damage = (max ? missile.emax : missile.emin) + d2d::rules::level_bonus(max ? missile.emax_lev : missile.emin_lev, lvl);
    if (const int bonus = missile.edmg_sym.empty() ? 0 : d2d::rules::eval_calc(game_data.skills, missile.edmg_sym, env, skill_id, lvl); bonus != 0)
        damage += int(std::int64_t(damage) * bonus / 100);
    return damage << (missile.hitshift & 31);
}
// An object's mode tokens, and the index of one (0 when unknown).
inline constexpr std::array<std::string_view, 8> kObjectModes{ "NU", "OP", "ON", "S1", "S2", "S3", "S4", "S5" };
int mode_index(std::string_view mode);
// The game's object seed: {the game seed's second step, 666}
// (FUN_00546c60, objrgn.cpp; the first step made the monster regions).
inline d2d::rules::Rng object_seed(std::uint32_t map_seed) {
    d2d::rules::Rng game{ map_seed };
    game.next();
    return d2d::rules::Rng{ game.next() };
}

// FUN_0064dea0's search, as FUN_0064e7b0 / FUN_0064e810 call it (50
// rings, a subtile a step): (x, y) itself if `clear`, else rings 1..49
// out, each ring's columns x - r and x + r top to bottom (left then right
// a row), then its rows y - r and y + r left to right (top then bottom a
// column); the first clear spot nearest (Manhattan) in the first ring with
// one wins. None: nullopt (the callers' "keep the room" flag then keeps
// (x, y) or fails).
template <class Clear>
std::optional<std::pair<int, int>> nearest_spot(int x, int y, Clear clear) {
    if (clear(x, y)) return std::pair{ x, y };
    int best = -1, best_x = x, best_y = y;
    for (int ring = 1; ring < 50 && best < 0; ++ring) {
        auto test = [&](int test_x, int test_y) {
            const int distance = std::abs(test_x - x) + std::abs(test_y - y);
            if (clear(test_x, test_y) && (best < 0 || distance < best)) { best = distance; best_x = test_x; best_y = test_y; }
        };
        for (int ty = y - ring; ty <= y + ring; ++ty) { test(x - ring, ty); test(x + ring, ty); }
        for (int tx = x - ring + 1; tx <= x + ring - 1; ++tx) { test(tx, y - ring); test(tx, y + ring); }
    }
    if (best < 0) return std::nullopt;
    return std::pair{ best_x, best_y };
}

// FUN_0064e810 -> nearest_spot with expfield.d2 (`field`): the free
// subtile nearest (x, y) where something `size` across (FUN_0064d9b0: 0 / 1
// the subtile, 2 the plus, 3 the 3 x 3 box) meets no bit of `mask` in
// `flags(x, y)` (FUN_0064ca50: 0x27 off the rooms), and the field's walk
// from it back to (origin_x, origin_y) meets no bit of `walk_mask`
// (FUN_0066a670).
template <class Flags>
std::optional<std::pair<int, int>> field_spot(const std::vector<std::uint8_t>& field, int origin_x, int origin_y, int x, int y, int size, int mask, int walk_mask, Flags flags) {
    auto hit = [&](int at_x, int at_y, int bits) { const int cell_flags = flags(at_x, at_y); return cell_flags == 0x27 || (cell_flags & bits); };
    auto walk = [&](int at_x, int at_y) {
        static constexpr int kStepX[9] = { 0, 1, 1, 1, 0, -1, -1, -1, 0 }, kStepY[9] = { -1, -1, 0, 1, 1, 1, 0, -1, 0 };
        auto dir = [&] { return field[std::size_t((at_y - origin_y + 128) * 256 + at_x - origin_x + 128)]; };
        if (field.empty()) return true;
        if (hit(at_x, at_y, walk_mask)) return false;
        for (;;) {
            const auto step = dir();
            at_x += kStepX[step]; at_y += kStepY[step];
            if (dir() == 8) return true;
            if (hit(at_x, at_y, walk_mask)) return false;
        }
    };
    auto fits = [&](int at_x, int at_y) {
        if (size <= 1) return !hit(at_x, at_y, mask);
        if (size == 2) return !hit(at_x, at_y, mask) && !hit(at_x - 1, at_y, mask) && !hit(at_x + 1, at_y, mask) && !hit(at_x, at_y - 1, mask) && !hit(at_x, at_y + 1, mask);
        if (size > 3) return false;
        for (int row = -1; row <= 1; ++row)
            for (int column = -1; column <= 1; ++column)
                if (hit(at_x + column, at_y + row, mask)) return false;
        return true;
    };
    return nearest_spot(x, y, [&](int test_x, int test_y) { return fits(test_x, test_y) && walk(test_x, test_y); });
}

// Where a drop at subtile (x, y) lands (FUN_00555da0): from (x + 2, y + 3)
// if that's in a room, else (x, y), field_spot's free subtile (size 1, mask
// 0x3e01, walk 0x801). None: the start.
template <class Flags>
std::pair<int, int> drop_spot(const std::vector<std::uint8_t>& field, int x, int y, Flags flags) {
    int at_x = x + 2, at_y = y + 3;
    if (flags(at_x, at_y) == 0x27) { at_x = x; at_y = y; }
    return field_spot(field, x, y, at_x, at_y, 1, 0x3e01, 0x801, flags).value_or(std::pair{ at_x, at_y });
}

void add_object(const GameData& game_data, const d2d::txt::Table& objects, const std::unordered_map<std::string, std::size_t>& obj_row,
                Level& into, int oid, int spot_x, int spot_y, d2d::rules::Rng& rgn);
void finish_level(Level& level);
// A town of any act: no monsters, no running drain, no town portal.
[[nodiscard]] inline bool in_town(const Level* level) { return level && d2d::rules::level_ids::is_town(level->id); }
// A town's spawn spots from its DS1 specials: Level::start, portal_spot.
void find_spawn_spots(Level& level);
LevelDt1s load_level_dt1s(Level& level, d2d::mpq::Stack& mpqs, d2d::drlg::OutdoorAssets& assets, int type, d2d::dt1::Pixels pixels);
std::size_t set_level_tiles(Level& level, const d2d::drlg::OutdoorAssets& assets, const LevelDt1s& dt1s,
                            const std::vector<d2d::drlg::Outdoor::RoomSeed>& made, const std::vector<d2d::drlg::PlainRoom>& plain,
                            std::vector<std::string>& notes);
void relevel(Level& level, const std::vector<std::size_t>& came_up, const GameData* game_data = nullptr, const Spawning* spawning = nullptr);

// The near list (room2 +8, FUN_0066c370) of the room holding (x, y) cells:
// each (level, room index) in game.exe's order; empty outside every room.
std::vector<std::pair<const Level*, int>> near_rooms(const GameData& game_data, const Level& level, float x, float y);
// The room holding (x, y) cells (its Level::rooms index), -1 none.
int room_of(const Level& level, float x, float y);
// Room `index`'s rect, level tiles: its Level::rooms entry, or the camp's
// 8x8 room (room_of's); none past the last.
std::optional<d2d::drlg::Outdoor::RoomSeed> level_room(const Level& level, std::size_t index);
// The camp's room seeds kept in its LevelState (by level_room index), once;
// false while the builder's busy. Any other level keeps them at population.
bool keep_camp_seeds(const GameData& game_data, const Level& level, Spawning::LevelState& state);
// The level's own monster of MonStats `row`'s family (FUN_0063ec70): the
// first of the level's list (Levels.bin +0x33 / +0x36: Mon1..10) of that
// BaseId; else up the family's chain (NextInClass, at most its count,
// MonStats +0x4a) while the next's Level is at most MonLvl1Ex + 1. A level
// with no list keeps `row`.
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
void place_objects(const GameData& game_data, GameData::LevelBuilder& builder, Level& level);
// Room `index` of `level` coming into play (FUN_0052d0f0, objgroups.cpp):
// its seed step (FUN_0054f060), its preset objects (FUN_005559a0) on the
// game's object seed, its preset monsters, object groups (FUN_00552610)
// and population (FUN_0054ec90) into the level's spawns. Returns the room
// seed as it stands.
d2d::rules::Rng room_objects(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t index);
// The level's collision as room_objects sees it (FUN_0064ca50's words, 0x27
// off the rooms up), level subtiles row-major: drlg-dump drops.
std::vector<std::uint16_t> object_collision(const Spawning& spawning, const Level& level);
void install_level(const GameData& game_data, int id, std::unique_ptr<Level> level);
std::unique_ptr<Level> finish_job(std::future<std::unique_ptr<Level>>& job, int id);
void want_nearby(const GameData& game_data, const Level& level);

}  // namespace d2d::game
