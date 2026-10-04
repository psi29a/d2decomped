// SPDX-License-Identifier: GPL-3.0-or-later
// Room tiles (RoomTile.cpp): which DT1 tile each cell of a room gets when
// game.exe brings the room up (FUN_0061b730) — the walk over its DS1 words
// (plain rooms FUN_0067d710), the per-word rules (FUN_0066e9b0), the rarity
// pick on the room seed (tile_pick.hpp) and edge tiles shared with the
// rooms next to it (FUN_0066e940). docs/research/re/drlg.md "Room tiles".
//
// Edge sharing makes the result depend on which rooms are already up, so
// this brings them all up in game.exe's room-list order (the reverse of
// the order they were made in); in the game it's whatever order the player
// walks them in.
#pragma once

#include "outdoor.hpp"
#include "tile_pick.hpp"
#include "units.hpp"

#include <ds1.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace d2d::drlg {

// A level's warp slot (Levels.txt Warp0..7): its LvlWarp row — Id, lit
// (LitVersion), the warp unit's offset from its tile (OffsetX / Y), and
// the lit wall's sequence bits (Tiles).
struct WarpSlot { int id = -1; bool lit = false; int off_x = 0, off_y = 0, tiles = 0; };

// A tile a room holds: level-relative x, y; the word it came from.
struct PlacedTile {
    int layer, x, y, orient;                            // layer 0 wall, 1 floor, 2 shadow
    const Dt1File* file;
    int index;
    std::uint32_t word;
    int next = -1;                                      // tile +0x20: the next in its chain (room.tiles index)
    bool door = false;                                  // flags & 0x20: its door unit made (FUN_0066d9e0)
    // FUN_0066db20's flags as rooms share it: +0x14 bits 14..16, each word's
    // layer + 1 OR'd in (not a shadow's); bit 0, any of the words had 0x80.
    int layers = 0;
    bool keep = false;
};
// A room's area (FUN_0066ca50's entry): level-relative tiles, right /
// bottom exclusive; id its flood label, skip (+0x20) where it began on a blank floor.
struct Area { int left, top, right, bottom; std::uint32_t id; bool skip; };

struct BuiltRoom {
    int x, y, width = 8, height = 8, kind = 1;                   // level-relative tiles
    std::vector<PlacedTile> tiles;                      // in the order game.exe adds them
    // Edge tiles other rooms can find (FUN_0066e4c0): linked through
    // PlacedTile::next from the newest, chains newest first; floor chains vs the rest.
    struct Chain { bool floor; int head = -1; };
    std::vector<Chain> chains;
    // Hidden warp tiles (orientation 10/11, style = the level's warp slot):
    // level-relative cell and slot — where the warp unit stands (FUN_0066e1c0).
    struct Warp { int x, y, slot; };
    std::vector<Warp> warps;
    // Units for the server (room +0x5c): room-relative subtiles, newest first.
    std::vector<Unit> units;
    bool upper = false;
    std::size_t step = 0;                               // its place in the order the rooms came up
    const PlainRoom* plain = nullptr;
    const Outdoor::RoomSeed* seed = nullptr;
    // The seed its room1 gets as it comes into play (FUN_006422a0: the
    // tiles' seed stepped once more; room1 +0x6c, {this, 666}): what
    // populating the room rolls.
    std::uint32_t room1_seed = 0;
    std::vector<Area> areas;                            // FUN_0066d110's, newest first; empty: the whole room (FUN_0066ccb0)
    // The neighbours' tiles it shared (FUN_0066e740), in order: owner (rooms
    // index), its tile, this room's word (OR'd into the tile's flags,
    // FUN_0066db20), the tile before and after any re-pick. game.exe patches
    // a re-picked tile into the collision of the room it lies in, if that's
    // up (FUN_0064c860).
    struct Share { int owner, tile; std::uint32_t word; const Dt1File* old_file; int old_index; const Dt1File* file; int index; };
    std::vector<Share> shares;
    std::uint32_t vis = 0;                              // FUN_00667970: 0x10 << slot, a preset's slot wall (orientation 10/11, style < 8, sequence 0 / 4 or hidden)
    // FUN_00667970's tile infos of type 0xb (orientation 10/11, style 30..33 ->
    // sequence, sequence + 5, 10, 11; level +0x2c): level-relative tiles, the
    // start spot of a Levels Position level.
    // ponytail: later acts: type 0xb only, not 0x6eed88's kin types (unseen in Act 1).
    std::vector<std::pair<int, int>> starts;
};

namespace room_tiles_detail {

inline constexpr std::array<int, 20> kOrientClass = { -1, 0, 1, 2, -1, 3, 4, 5, -2, -2, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1 };   // 0x6ef620
inline constexpr std::array<int, 42> kOrientMerge = { 0, 1, 3, 3, 4, 1, 3, 1, 1, 2, 3, 4, 3, 2, 2, 3, 3, 3, 4, 3, 3,              // 0x6ef574
                                                      3, 1, 3, 3, 4, 5, 6, 1, 3, 2, 3, 4, 3, 6, 2, 1, 2, 3, 4, 1, 2 };

// Door objects of orientation 8 / 9 walls (FUN_0066d960): level -> first,
// last row (0x6eefc8), each row {style, seq, orientation 9, object, dx, dy}
// (0x6ef188; all act 1's are type 2, objects).
struct DoorLevel { int level, first, last; };
inline constexpr std::array<DoorLevel, 12> kDoorLevels = { { { 28, 0, 3 }, { 29, 0, 3 }, { 30, 0, 3 }, { 31, 0, 3 }, { 26, 4, 6 }, { 27, 4, 6 },
                                                           { 32, 5, 9 }, { 33, 5, 9 }, { 34, 10, 11 }, { 35, 10, 11 }, { 36, 10, 11 }, { 37, 10, 12 } } };
struct DoorRow { int style, seq, is9, id, dx, dy; };
inline constexpr std::array<DoorRow, 13> kDoorRows = { { { 7, 0, 1, 14, 5, 0 }, { 7, 0, 0, 13, 0, 5 }, { 5, 0, 1, 16, 0, 0 }, { 5, 0, 0, 15, 0, 0 },
                                                       { 6, 0, 1, 27, 5, -2 }, { 4, 0, 1, 24, 1, 2 }, { 4, 0, 0, 23, 0, 0 }, { 4, 3, 1, 25, 1, 0 },
                                                       { 1, 2, 0, 62, 0, 3 }, { 1, 2, 1, 63, 3, 0 }, { 0, 0, 1, 16, 0, 0 }, { 0, 0, 0, 64, 0, 0 },
                                                       { 2, 0, 1, 47, 5, 0 } } };

// FUN_0066bc20 + FUN_0066bbc0: the level's rooms (list order, the first
// `own`) within a 6-tile gap on both axes, itself included, then
// bubble-sorted so a room wholly left of or above the one before it moves
// ahead; then each close room of the levels next door (those past `own`),
// appended and the list sorted again (FUN_0066be80, FUN_0066bda0).
inline std::vector<std::size_t> near_rooms(const std::vector<BuiltRoom>& rooms, std::size_t self, std::size_t own) {
    const auto& room = rooms[self];
    std::vector<std::size_t> close_rooms;
    auto close = [&](const BuiltRoom& other) {
        const int gap_x = room.x < other.x ? other.x - room.width - room.x : room.x - other.width - other.x;
        const int gap_y = room.y < other.y ? other.y - room.height - room.y : room.y - other.height - other.y;
        return gap_x < 6 && gap_y < 6;
    };
    auto sort = [&] {
        for (std::size_t k = close_rooms.size() ? close_rooms.size() - 1 : 0; k > 0; --k)
            for (std::size_t i = 0; i + 1 < close_rooms.size(); ++i) {
                const auto &first = rooms[close_rooms[i]], &second = rooms[close_rooms[i + 1]];
                if (second.x + second.width <= first.x || second.y + second.height <= first.y) std::swap(close_rooms[i], close_rooms[i + 1]);
            }
    };
    for (std::size_t i = 0; i < own; ++i) if (close(rooms[i])) close_rooms.push_back(i);
    sort();
    for (std::size_t i = own; i < rooms.size(); ++i) if (close(rooms[i])) { close_rooms.push_back(i); sort(); }
    return close_rooms;
}

inline constexpr int kDx[4] = { 1, 0, -1, 0 }, kDy[4] = { 0, 1, 0, -1 };                        // 0x6eee14
inline constexpr int kRow[20] = { -1, 0, 1, 2, 2, 0, 1, 3, 0, 1, 0, 1, 4, -1, 4, 0, 0, 0, 0, 0 };  // 0x6eeea0
inline constexpr int kMask[6][5] = { { -1, 0, 0, -1, 0 }, { 23, 0, 5, 21, 17 }, { 15, 3, 0, 9, 7 },  // 0x6eee24
                                     { 39, 0, 0, 5, 3 }, { 31, 31, 31, 31, 31 }, { 31, 31, 31, 31, 31 } };
// FUN_0066d110 (a LvlPrest Logicals preset): the room cut into areas by
// its walls. blocks: FUN_0066c870's wall cells (own layer-0 walls, near
// rooms' non-floor chain tiles inside it); FUN_0066c580 labels them by
// flood (FUN_0066c3d0) over the wall orientations; FUN_0066ca50 splits
// the labels into rects. orients / floors: the (w+1)x(h+1) wall layer 0
// orientation and floor layer 0 word slices.
inline void logic_areas(std::vector<BuiltRoom>& rooms, std::size_t self, const std::vector<std::size_t>& nearby,
                        const std::vector<std::uint32_t>& orients, const std::vector<std::uint32_t>& floors) {
    auto& room = rooms[self];
    const int width = room.width + 1, height = room.height + 1;
    std::vector<std::uint32_t> labels(std::size_t(width * height));
    std::vector<char> walls(std::size_t(width * height));
    auto blocks = [](const PlacedTile& tile) {                                                         // FUN_0066db20 flags
        const bool hidden = (tile.word & 0x20000000u) || (tile.file && tile.index >= 0 && (tile.file->tiles[std::size_t(tile.index)].material & 4));
        return tile.layer == 0 && tile.orient != 13 && tile.orient != 15 && ((tile.word >> 18) & 3) == 0 && !hidden;
    };
    auto mark = [&](const PlacedTile& tile) {
        if (tile.x >= room.x && tile.y >= room.y && tile.x <= room.x + room.width && tile.y <= room.y + room.height && blocks(tile))
            walls[std::size_t((tile.y - room.y) * width + tile.x - room.x)] = 1;
    };
    for (const auto& tile : room.tiles) mark(tile);
    for (const auto other : nearby) {
        if (other == self || !rooms[other].upper) continue;
        for (const auto& chain : rooms[other].chains)
            if (!chain.floor)
                for (int i = chain.head; i != -1; i = rooms[other].tiles[std::size_t(i)].next) mark(rooms[other].tiles[std::size_t(i)]);
    }
    std::uint32_t label = 0;
    auto flood = [&](auto&& flood, int x, int y, int dir) -> void {                                 // FUN_0066c3d0
        for (;;) {
            if (x < 0 || y < 0 || x >= width || y >= height) return;
            const auto cell = std::size_t(y * width + x);
            if (labels[cell] & 0x10000000u) return;
            if (!walls[cell]) {
                labels[cell] = label;
                for (int step = 0; step < 4; ++step) flood(flood, x + kDx[step], y + kDy[step], step);
                return;
            }
            const auto orient = orients.empty() ? 0u : orients[cell] & 0xff;
            const int mask = kMask[(orient < 20 ? kRow[orient] : -1) + 1][dir + 1];
            if (mask & 1) labels[cell] = label;
            if ((mask & 2) && dir != 2) flood(flood, x + 1, y, 0);
            if ((mask & 4) && dir != 3) flood(flood, x, y + 1, 1);
            if ((mask & 8) && dir != 0) flood(flood, x - 1, y, 2);
            if ((mask & 0x10) && dir != 1) flood(flood, x, y - 1, 3);
            if (!(mask & 0x20)) return;
            ++x; ++y; dir = -1;
        }
    };
    std::uint32_t count = 0;
    for (int y = 0; y < height; ++y)                                                                     // FUN_0066c580
        for (int x = 0; x < width; ++x) {
            const auto cell = std::size_t(y * width + x);
            if (labels[cell] & 0x10000000u) continue;
            label = (++count & 0xfffffffu) | 0x10000000u;
            if ((floors[cell] & 0x1e0ff00u) == 0x1e00000u || (floors[cell] & 0x80000000u)) label |= 0x20000000u;
            flood(flood, x, y, -1);
        }
    std::vector<char> claimed(std::size_t(width * height));                                                  // FUN_0066ca50
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            if (claimed[std::size_t(y * width + x)]) continue;
            const auto value = labels[std::size_t(y * width + x)], id = value & 0xfffffffu;
            auto same = [&](int cell_x, int cell_y) { return !claimed[std::size_t(cell_y * width + cell_x)] && (labels[std::size_t(cell_y * width + cell_x)] & 0xfffffffu) == id; };
            int right = x + 1;
            while (right < width && same(right, y)) ++right;
            int bottom = y + 1;
            for (; bottom < height; ++bottom) {
                bool row = true;
                for (int cx = x; cx < right && row; ++cx) row = same(cx, bottom);
                if (!row) break;
            }
            for (int cy = y; cy < bottom; ++cy)
                for (int cx = x; cx < right; ++cx) claimed[std::size_t(cy * width + cx)] = 1;
            Area area{ room.x + x, room.y + y, std::min(room.x + right, room.x + room.width), std::min(room.y + bottom, room.y + room.height), id, (value & 0x20000000u) != 0 };
            if (area.left >= room.x + room.width || area.top >= room.y + room.height) area.left = area.top = area.right = area.bottom = 0;
            room.areas.insert(room.areas.begin(), area);
        }
}

}  // namespace room_tiles_detail

// Every room of a level with its tiles; `made` in the order game.exe made
// them (Outdoor::rooms, generate_maze), `plain` an outdoor level's plain rooms.
// `slots`: the level's warp slots (warp_slots). `order`: the rooms (list
// indices) in the order they come up (FUN_0061b190), empty for list order;
// a room's picks and its share of an edge depend on which are up already.
// `outside`: the up rooms of the levels next door (the camp's) in this
// level's tiles, list order; up before the level's, after them in the result.
inline std::vector<BuiltRoom> level_room_tiles(const std::vector<Outdoor::RoomSeed>& made, const std::vector<PlainRoom>& plain,
                                               const OutdoorData& data, const RoomDt1s& dt1s, int level,
                                               const std::array<WarpSlot, 8>& slots, std::vector<std::string>& notes,
                                               const std::vector<std::size_t>& order = {}, std::vector<BuiltRoom> outside = {}) {
    using namespace room_tiles_detail;
    auto note = [&](std::string message) { if (std::ranges::find(notes, message) == notes.end()) notes.push_back(std::move(message)); };
    std::vector<BuiltRoom> rooms;                       // game.exe's list: newest room first
    for (auto made_room = made.rbegin(); made_room != made.rend(); ++made_room) {
        BuiltRoom room{ made_room->x, made_room->y, made_room->width, made_room->height, made_room->kind, {}, {}, {}, {}, false, 0, nullptr, &*made_room };
        if (made_room->kind == 1)
            for (const auto& plain_room : plain) if (plain_room.x == made_room->x && plain_room.y == made_room->y) room.plain = &plain_room;
        rooms.push_back(std::move(room));
    }
    const std::size_t own = rooms.size();
    for (auto& room : outside) rooms.push_back(std::move(room));
    // Each room's tile seed and DT1s outlive its walk: a later room re-picking a shared tile rolls the owner's.
    std::vector<d2d::rules::Rng> rngs(rooms.size());
    std::vector<decltype(room_dt1_list(0u, dt1s))> lists(rooms.size());
    std::map<std::pair<int, int>, std::vector<Unit>> preset_units;   // by the preset's origin: units no room has taken yet
    for (std::size_t step = 0; step < own; ++step) {
        const std::size_t room_index = order.empty() ? step : order[step];
        auto& room = rooms[room_index];
        room.upper = true;
        room.step = rooms.size() - own + step;
        const Preset* pre = nullptr;
        const d2d::ds1::Map* map = nullptr;
        if (!room.plain) {
            const auto found = data.presets.find(room.seed->def);
            if (found == data.presets.end()) { note("drlg: preset def " + std::to_string(room.seed->def) + " missing"); continue; }
            pre = &found->second;
            map = room.seed->file < 6 ? pre->maps[std::size_t(room.seed->file)] : nullptr;
            if (!map) { note("drlg: preset " + std::to_string(room.seed->def) + " file not loaded"); continue; }
        }
        if (room.plain) {
            room.units = room.plain->units;                   // the stamps' objects (made at the room's init)
        } else {
            // The preset's units, the first time one of its rooms comes up
            // (FUN_00667890 -> FUN_00667620; a maze made them at generation,
            // FUN_00667970), then this room takes the ones inside it (FUN_00666710).
            auto key = std::pair{ room.seed->preset_x, room.seed->preset_y };
            auto found = preset_units.find(key);
            if (found == preset_units.end()) {
                auto list = room.seed->rolled ? room.seed->units : ds1_units(*map, data.ids);
                // FUN_00667620: some roll to stay, in the loader's order, on
                // the seed of the room bringing them up (outdoors, presets
                // without Scan or Pops; those and a maze roll at generation on
                // the level's seed). The tiles' seed is reset after
                // (FUN_0066ee40), so they don't shift.
                if (room.seed->rolled) {
                    // already rolled
                } else {
                    d2d::rules::Rng roll{ room.seed->seed };
                    if (plain.empty()) roll.high = room.seed->seed_high;   // a maze / preset level: the room's whole seed (FUN_00667890)
                    std::erase_if(list, [&](const Unit& unit) { return !stays(unit, data.ids, roll); });
                }
                std::ranges::reverse(list);             // copied into the preset's list front-first again (FUN_00667510)
                for (auto& unit : list) {
                    unit.x += room.seed->preset_x * 5;
                    unit.y += room.seed->preset_y * 5;
                }
                if (level == level_ids::kBloodMoor && room.seed->def >= 4 && room.seed->def <= 7 && room.seed->file == 3 && data.ids.monstats > 0x10a)
                    list.insert(list.begin(), { 1, 0x10a, 1, (room.seed->preset_x + pre->width / 2) * 5, (room.seed->preset_y + pre->height / 2) * 5, 0 });   // FUN_006664a0: Flavie
                found = preset_units.emplace(key, std::move(list)).first;
            }
            auto& list = found->second;
            for (auto unit_it = list.begin(); unit_it != list.end();) {
                if (unit_it->x >= room.x * 5 && unit_it->y >= room.y * 5 && unit_it->x < (room.x + room.width) * 5 && unit_it->y < (room.y + room.height) * 5) {
                    room.units.insert(room.units.begin(), { unit_it->type, unit_it->id, unit_it->mode, unit_it->x - room.x * 5, unit_it->y - room.y * 5, unit_it->flags, unit_it->path });
                    unit_it = list.erase(unit_it);
                } else {
                    ++unit_it;
                }
            }
        }
        const auto& list = lists[room_index] = room_dt1_list(room.plain ? room.plain->dt1_mask : pre->dt1_mask, dt1s);
        const auto nearby = near_rooms(rooms, room_index, own);
        // Reset by FUN_0066ee40; a plain room's init (grass, roads, stamps) rolls it on.
        auto& rng = rngs[room_index] = room.plain ? room.plain->seed : d2d::rules::Rng{ room.seed->seed };
        if (room.plain)
            for (const auto& tile : room.plain->tiles) room.tiles.push_back({ tile.layer, room.x + tile.x, room.y + tile.y, tile.orient, tile.file, tile.index, 0 });
        auto pick = [&](int orient, std::uint32_t word) { return pick_tile(list, rng, orient, word); };
        auto add = [&](int layer, int x, int y, int orient, std::uint32_t word) {
            const auto [tile_file, tile_index] = pick(orient, word);
            room.tiles.push_back({ layer, x, y, orient, tile_file, tile_index, word });
            room.tiles.back().layers = orient == 13 ? 0 : int((word >> 18) & 3) + 1;   // FUN_0066db20, FUN_0066dde0
            room.tiles.back().keep = (word & 0x80) != 0;
            return room.tiles.size() - 1;
        };
        // FUN_0066db20 on a shared tile: the sharer's word into its flags.
        auto flag = [](PlacedTile& tile, std::uint32_t word) {
            if (tile.orient != 13) tile.layers |= int((word >> 18) & 3) + 1;
            if (word & 0x80) tile.keep = true;
        };
        auto chain = [&](bool floor) -> BuiltRoom::Chain& {                    // FUN_0066e620's node
            for (auto& existing : room.chains) if (existing.floor == floor) return existing;
            room.chains.insert(room.chains.begin(), BuiltRoom::Chain{ floor });
            return room.chains.front();
        };
        // FUN_0066e4c0: a chained tile of room N at (x, y) this word can share.
        auto find = [&](BuiltRoom& neighbour, bool floor, int x, int y, std::uint32_t word) -> PlacedTile* {
            if (!neighbour.upper || x < neighbour.x || y < neighbour.y || x > neighbour.x + neighbour.width || y > neighbour.y + neighbour.height) return nullptr;
            for (auto& existing : neighbour.chains) {
                if (existing.floor != floor) continue;
                for (int tile_index = existing.head; tile_index != -1; tile_index = neighbour.tiles[std::size_t(tile_index)].next) {
                    auto& tile = neighbour.tiles[std::size_t(tile_index)];
                    if (tile.x == x && tile.y == y && tile.orient != 4 && (tile.orient == 13 || !(word & 0x8000000u))
                        && (tile.layers == 0 || tile.layers - 1 == int((word >> 18) & 3)))
                        return &tile;
                }
            }
            return nullptr;
        };
        // FUN_0066d9e0: an orientation 8 / 9 wall's door, once per tile
        // (`tile` null for a hidden one), inside this room.
        // ponytail: later acts: act 1's rows only; other acts' type 1 and objects 0x5b / 0x5c (a 1-in-3 roll on the room seed) not ported.
        auto door = [&](PlacedTile* tile, std::uint32_t tile_word, int tile_orient, int x, int y) {
            if (tile && tile->door) return;
            const int is9 = (tile ? tile->orient : tile_orient) == 9;
            const int style = int((tile_word >> 20) & 0x3f), seq = int((tile_word >> 8) & 0xff);
            for (const auto& door_level : room_tiles_detail::kDoorLevels) {
                if (door_level.level != level) continue;
                for (int row_index = door_level.first; row_index <= door_level.last; ++row_index) {
                    const auto& row = room_tiles_detail::kDoorRows[std::size_t(row_index)];
                    if (row.style != style || row.seq != seq || row.is9 != is9) continue;
                    const int unit_x = (x - room.x) * 5 + row.dx, unit_y = (y - room.y) * 5 + row.dy;
                    if (unit_x < 0 || unit_y < 0 || unit_x >= room.width * 5 || unit_y >= room.height * 5) return;
                    room.units.insert(room.units.begin(), { 2, row.id, 0, unit_x, unit_y, 0 });
                    if (tile) tile->door = true;
                    return;
                }
            }
        };
        auto warp_unit = [&](const WarpSlot& warp_slot, int x, int y) {                           // FUN_0066e1c0
            if (warp_slot.id < 0 || x - room.x == room.width || y - room.y == room.height) return false;
            room.units.insert(room.units.begin(), { 5, warp_slot.id, 0, (x - room.x) * 5 + warp_slot.off_x, (y - room.y) * 5 + warp_slot.off_y, 0 });
            return true;
        };
        // The room's warp records (+0x4c, by LvlWarp id): their +0xc tile
        // lists run through tile +0x20, the share chains' link, so linking a
        // chained tile into one cuts or reroutes its chain.
        std::map<int, int> warp_lists;
        // FUN_0066e260: a visible warp wall's unit (seq 0 / 4); placed, the
        // wall joins its warp record's list and, when its LvlWarp row is
        // lit, gets its lit twin (seq | Tiles).
        auto warp_wall = [&](std::size_t tile, std::uint32_t tile_word, int tile_orient, int x, int y) {
            const int style = int((tile_word >> 20) & 0x3f), seq = int((tile_word >> 8) & 0xff);
            const auto& warp_slot = slots[std::size_t(style)];
            if ((seq == 0 || seq == 4) && !warp_unit(warp_slot, x, y)) return;
            auto& head = warp_lists.try_emplace(warp_slot.id, -1).first->second;
            room.tiles[tile].next = head;
            head = int(tile);
            if (warp_slot.lit) add(0, x, y, tile_orient, tile_word | std::uint32_t(warp_slot.tiles) << 8);
        };
        auto shared = [&](std::uint32_t word, int tile_orient, int x, int y) {               // FUN_0066e940
            PlacedTile* tile = nullptr;
            BuiltRoom* neighbour = nullptr;
            for (const auto near_index : nearby) {                                        // FUN_0066e580
                if (near_index == room_index) continue;
                if ((tile = find(rooms[near_index], tile_orient == 0, x, y, word))) { neighbour = &rooms[near_index]; break; }
            }
            if (!tile) {                                                            // FUN_0066e620
                if ((tile_orient == 10 || tile_orient == 11) && !(x >= room.x && y >= room.y && x < room.x + room.width && y < room.y + room.height)) return;
                chain(tile_orient == 0);
                const auto added = add(tile_orient == 0 ? 1 : tile_orient == 13 ? 2 : 0, x, y, tile_orient, word);
                auto& linked = chain(tile_orient == 0);
                room.tiles[added].next = linked.head;
                linked.head = int(added);
                if (tile_orient == 8 || tile_orient == 9) door(&room.tiles[added], word, tile_orient, x, y);
                if (tile_orient == 3) add(0, x, y, 4, word);
                if (tile_orient == 10 || tile_orient == 11) warp_wall(added, word, tile_orient, x, y);
                return;
            }
            // FUN_0066e740: the neighbour's tile stays unless the orientations merge differently.
            if (tile->keep) {                                                       // its flags & 1
                if (tile->orient != 8 && tile->orient != 9) return;
                room.shares.push_back({ int(neighbour - rooms.data()), int(tile - neighbour->tiles.data()), word, tile->file, tile->index, tile->file, tile->index });
                flag(*tile, word);
                door(tile, word, tile->orient, x, y);
                return;
            }
            int orient = tile_orient;
            if (!(word & 0x80)) {
                const int cls = tile_orient >= 0 && tile_orient < 20 ? kOrientClass[std::size_t(tile_orient)] : -1;
                bool skip_merge = false;
                if (tile_orient == 8 || tile_orient == 9) skip_merge = x == room.x || y == room.y;
                else if ((tile->orient == 8 || tile->orient == 9) && (x == neighbour->x || y == neighbour->y)) return;
                if (!skip_merge) {
                    if (cls < 0 || tile->orient > 7) { if (cls != -1) return; }
                    else orient = kOrientMerge[std::size_t(cls * 7 + tile->orient)];
                }
            }
            if (tile->orient != 3 && orient == 3) {
                tile->layers |= 3;                                                  // flags | 0xc008
                chain(false);
                add(0, x, y, 3, word);
            }
            const bool blank = tile->orient == 0 && tile->file && tile->index >= 0 && tile->file->tiles[std::size_t(tile->index)].style == 30
                               && tile->file->tiles[std::size_t(tile->index)].seq == 0;
            const auto* old_file = tile->file;
            const int old_index = tile->index;
            if (orient != tile->orient || blank) {
                const auto owner = std::size_t(neighbour - rooms.data());   // FUN_0066d820 on the tile's room
                const auto [tile_file, tile_index] = pick_tile(lists[owner], rngs[owner], orient, word);
                tile->orient = orient;
                tile->file = tile_file;
                tile->index = tile_index;
            }
            room.shares.push_back({ int(neighbour - rooms.data()), int(tile - neighbour->tiles.data()), word, old_file, old_index, tile->file, tile->index });   // FUN_0066db20 on its flags
            flag(*tile, word);
            if (tile->orient == 8 || tile->orient == 9) door(tile, word, tile->orient, x, y);
        };
        auto word = [&](std::uint32_t tile_word, int tile_orient, int x, int y, bool fill) {       // FUN_0066e9b0
            const int style = int((tile_word >> 20) & 0x3f), seq = int((tile_word >> 8) & 0xff);
            if ((tile_orient == 10 || tile_orient == 11) && style > 7) return;
            if (tile_orient == 0 && style == 30 && seq <= 1) tile_word |= 0x80000000u;
            if (tile_word & 0x80000000u) {
                if ((tile_orient == 8 || tile_orient == 9) && (level < level_ids::kFrigidHighlands || (level > level_ids::kArreatPlateau && level != level_ids::kFrozenTundra))) { door(nullptr, tile_word, tile_orient, x, y); return; }
                if (tile_orient == 10 || tile_orient == 11) {                // FUN_0066e1c0 (the warp unit), FUN_0066e360
                    room.warps.push_back({ x, y, style });
                    const auto& warp_slot = slots[std::size_t(style)];   // style <= 7 here: the warp slot
                    warp_unit(warp_slot, x, y);
                    if (!warp_slot.lit) return;
                    for (int k = 0; k < 4; ++k) {    // its lit floor, 2x2 up-left of it (0x6ef554)
                        const std::uint32_t warp_word = std::uint32_t(seq) << 20 | std::uint32_t(k | 4) << 8;
                        const auto [tile_file, tile_index] = pick(0, warp_word);
                        room.tiles.push_back({ 1, x - 1 + (k & 1), y - 1 + (k >> 1), 0, tile_file, tile_index, warp_word });
                    }
                    // FUN_0066e360 then lists the room's unlit warp floors
                    // (style = seq, sequence < 4) onto the warp record's list.
                    auto& warp_list = warp_lists.try_emplace(warp_slot.id, -1).first->second;
                    for (std::size_t i = 0; i < room.tiles.size(); ++i) {
                        auto& tile = room.tiles[i];
                        if (tile.layer != 1 || !tile.file || tile.index < 0) continue;
                        const auto& info = tile.file->tiles[std::size_t(tile.index)];
                        if (info.style != seq || info.seq >= 4) continue;
                        tile.next = warp_list;
                        warp_list = int(i);
                    }
                    return;
                }
            }
            if (tile_word & 4) {
                if (tile_word & 2) { if (style == 30 && seq <= 1) tile_word &= ~0x80u; shared(tile_word, 0, x, y); return; }
                if (tile_word & 1) { shared(tile_word, tile_orient, x, y); return; }
                if ((tile_word & 0x8000000u) && !(tile_word & 0x80000000u)) { shared(tile_word, 13, x, y); return; }
            }
            if (tile_word & 2) add(1, x, y, 0, tile_word);
            else if (fill && x < room.x + room.width && y < room.y + room.height) {                   // FillBlanks, first floor layer
                const std::uint32_t blank = level == level_ids::kArcaneSanctuary ? 0x1e00100u : 0x1e00000u;
                const auto [tile_file, tile_index] = pick(0, blank);
                room.tiles.push_back({ 1, x, y, 0, tile_file, tile_index, (tile_word & ~0x80u) | 0x80000000u });
            }
            if (tile_word & 1) {
                const auto added = add(0, x, y, tile_orient, tile_word);
                if (tile_orient == 8 || tile_orient == 9) door(&room.tiles[added], tile_word, tile_orient, x, y);
                if (tile_orient == 3) add(0, x, y, 4, tile_word);
                if ((tile_orient == 10 || tile_orient == 11) && level != level_ids::kMatronsDen) warp_wall(added, tile_word, tile_orient, x, y);
            }
            if (tile_word & 0x8000000u) add(2, x, y, 13, tile_word);
        };
        if (room.plain) {                                  // FUN_0067d710: wall grid, then floor grid, 9x9
            const auto& plain_room = *room.plain;
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x)
                    word(plain_room.wall[std::size_t(y * 9 + x)], int(plain_room.orient[std::size_t(y * 9 + x)] & 0xff), room.x + x, room.y + y, false);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) word(plain_room.floor[std::size_t(y * 9 + x)], 0, room.x + x, room.y + y, false);
            room.room1_seed = rng.next();
            continue;
        }
        // A preset room (FUN_006667d0 then FUN_00666ac0): its (w+1)x(h+1) slice
        // of each DS1 layer, edges | 0x84, layers past the first | index << 18; walked
        // floors (the first with FillBlanks), walls, shadow, one short where
        // KillEdge meets the preset's own right / bottom edge.
        const int origin_x = room.x - room.seed->preset_x, origin_y = room.y - room.seed->preset_y;
        const int slice_w = room.width + 1, slice_h = room.height + 1;
        using Words = std::vector<std::uint32_t>;
        auto slice = [&](const d2d::ds1::Layer& source, bool orient) {
            Words words(std::size_t(slice_w * slice_h));
            for (int y = 0; y < slice_h; ++y)
                for (int x = 0; x < slice_w; ++x) {
                    const int source_x = origin_x + x, source_y = origin_y + y;
                    if (source_x < 0 || source_y < 0 || source_x >= map->width() || source_y >= map->height()) continue;
                    const auto& tile = source.cells[std::size_t(source_y) * std::size_t(map->width()) + std::size_t(source_x)];
                    words[std::size_t(y * slice_w + x)] = orient ? std::uint32_t(tile.wall_type) | tile.wall_zero << 8 : d2d::ds1::Map::word(tile);
                }
            return words;
        };
        auto edges = [&](Words& words, std::uint32_t value) {
            for (int x = 0; x < slice_w; ++x) { words[std::size_t(x)] |= value; words[std::size_t((slice_h - 1) * slice_w + x)] |= value; }
            for (int y = 0; y < slice_h; ++y) { words[std::size_t(y * slice_w)] |= value; words[std::size_t(y * slice_w + slice_w - 1)] |= value; }
        };
        const int pre_w = pre->width ? pre->width : map->width() - 1, pre_h = pre->height ? pre->height : map->height() - 1;   // a sizeless preset fills the level
        const bool kill_x = pre->kill_edge && room.x + room.width == room.seed->preset_x + pre_w, kill_y = pre->kill_edge && room.y + room.height == room.seed->preset_y + pre_h;
        const int words_wide = kill_x ? slice_w - 1 : slice_w, words_high = kill_y ? slice_h - 1 : slice_h;
        auto walk = [&](const Words& words, const Words* orient, bool fill) {   // FUN_0066ec10
            for (int y = 0; y < words_high; ++y)
                for (int x = 0; x < words_wide; ++x)
                    word(words[std::size_t(y * slice_w + x)], orient ? int((*orient)[std::size_t(y * slice_w + x)] & 0xff) : 0, room.x + x, room.y + y, fill);
        };
        for (std::size_t layer_index = 0; layer_index < map->floors().size(); ++layer_index) {
            auto words = slice(map->floors()[layer_index], false);
            for (auto& tile_word : words) tile_word |= std::uint32_t(layer_index) << 18;   // FUN_0067c590
            edges(words, 0x84);
            walk(words, nullptr, layer_index == 0);
        }
        for (std::size_t layer_index = 0; layer_index < map->walls().size(); ++layer_index) {
            auto words = slice(map->walls()[layer_index], false);
            const auto orients = slice(map->walls()[layer_index], true);
            if (layer_index == 0) edges(words, 0x84);
            else for (auto& tile_word : words) tile_word |= std::uint32_t(layer_index) << 18;   // FUN_0067c590
            walk(words, &orients, false);
            // ponytail: Act 1: the room's own cells, not game.exe's 8x8 cells of the whole DS1.
            for (int y = 0; y < room.height; ++y)
                for (int x = 0; x < room.width; ++x) {
                    const auto tile_word = words[std::size_t(y * slice_w + x)];
                    const int orient = int(orients[std::size_t(y * slice_w + x)] & 0xff), style = int((tile_word >> 20) & 0x3f), seq = int((tile_word >> 8) & 0xff);
                    if ((orient == 10 || orient == 11) && style < 8 && (seq == 0 || seq == 4 || (tile_word & 0x80000000u))) room.vis |= 0x10u << style;
                    const int info = style == 30 ? seq : style == 31 ? seq + 5 : style == 32 ? 10 : style == 33 ? 11 : -1;   // the tile info's type
                    if ((orient == 10 || orient == 11) && info == 11) room.starts.push_back({ room.x + x, room.y + y });
                }
        }
        if (!map->shadows().empty()) {
            auto words = slice(map->shadows()[0], false);
            edges(words, 0x84);
            walk(words, nullptr, false);
        }
        if (pre->logicals)
            logic_areas(rooms, room_index, nearby, map->walls().empty() ? Words{} : slice(map->walls()[0], true),
                        [&] { auto words = map->floors().empty() ? Words(std::size_t(slice_w * slice_h)) : slice(map->floors()[0], false); edges(words, 0x84); return words; }());
        room.room1_seed = rng.next();
    }
    return rooms;
}

}  // namespace d2d::drlg
