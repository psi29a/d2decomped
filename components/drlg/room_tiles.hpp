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
// off_x / off_y: its LvlWarp row's (Direction l or b); the r row's for an
// orientation 11 wall (FUN_0066af50: act 5's warps have both).
struct WarpSlot { int id = -1; bool lit = false; int off_x = 0, off_y = 0, tiles = 0, right_x = 0, right_y = 0; };

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
    // An Animate preset's tile (FUN_0066d440): its frame (rarity) and its key's frame count; 0 frames, not animated.
    std::uint8_t frame = 0, frames = 0;
};
// A room's area (FUN_0066ca50's entry): level-relative tiles, right /
// bottom exclusive; id its flood label, skip (+0x20) where it began on a blank floor.
// A spawn area (FUN_0066ca50's record): its rect clipped to the room (+0x10;
// all 0 when it starts on the room's far edge), its id (+0x28), skip
// (+0x20), and the rect unclipped (+0x00), over the room's (w+1)x(h+1)
// cells: the room's grid of records, FUN_0066ceb0's lookup.
struct Area { int left, top, right, bottom; std::uint32_t id; bool skip; int full_left = 0, full_top = 0, full_right = 0, full_bottom = 0; };

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
    // The tile picker state after this room came up. A room from a level
    // next door can own a shared tile which a later room re-picks
    // (FUN_0066e740 -> FUN_0066d820), on this seed and DT1 list.
    d2d::rules::Rng tile_seed;
    std::vector<const Dt1File*> tile_dt1s;
    std::uint32_t vis = 0;                              // FUN_00667970: 0x10 << slot, a preset's slot wall (orientation 10/11, style < 8, sequence 0 / 4 or hidden)
    // FUN_00667970's tile infos of type 0xb (orientation 10/11, style 30..33 ->
    // sequence, sequence + 5, 10, 11; level +0x2c): level-relative tiles, the
    // start spot of a Levels Position level.
    // 0x6eed88 gives kin only to indices 0 and 5 (any of 0..4, 5..9); 0xb matches itself.
    std::vector<std::pair<int, int>> starts;
    // Every such tile with its spawn index (0..4 a join's start, 11 a town
    // portal's arrival): a town's Level::start / portal_spot.
    struct Spawn { int x, y, index; };
    std::vector<Spawn> spawns;
    // Its visible warp walls that made a unit (FUN_0066e260): the slot is the
    // wall's style, which two slots sharing a LvlWarp id (the Kurast Sewers'
    // ways up) can't be told from the unit alone. No 0x800000 for these.
    std::vector<Warp> wall_warps;
};

namespace room_tiles_detail {

inline constexpr std::array<int, 20> kOrientClass = { -1, 0, 1, 2, -1, 3, 4, 5, -2, -2, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1 };   // 0x6ef620
inline constexpr std::array<int, 42> kOrientMerge = { 0, 1, 3, 3, 4, 1, 3, 1, 1, 2, 3, 4, 3, 2, 2, 3, 3, 3, 4, 3, 3,              // 0x6ef574
                                                      3, 1, 3, 3, 4, 5, 6, 1, 3, 2, 3, 4, 3, 6, 2, 1, 2, 3, 4, 1, 2 };

// Door units of orientation 8 / 9 walls (FUN_0066d960): level -> first,
// last row (0x6eefc8, inclusive), each row {style, seq, orientation 9, id,
// type, dx, dy} (0x6ef188; type 2 an object, 1 a monster by MonStats row).
// Act 5's: Harrogath's main gate (23), the barricades' doors and towers
// (24..33, some monsters).
struct DoorLevel { int level, first, last; };
inline constexpr std::array<DoorLevel, 37> kDoorLevels = { { { 28, 0, 3 }, { 29, 0, 3 }, { 30, 0, 3 }, { 31, 0, 3 }, { 26, 4, 6 }, { 27, 4, 6 },
                                                           { 32, 5, 9 }, { 33, 5, 9 }, { 34, 10, 11 }, { 35, 10, 11 }, { 36, 10, 11 }, { 37, 10, 12 },
                                                           { 51, 13, 14 }, { 52, 15, 18 }, { 53, 15, 18 }, { 54, 15, 18 }, { 55, 19, 20 }, { 56, 19, 20 },
                                                           { 57, 19, 20 }, { 58, 19, 20 }, { 59, 19, 20 }, { 60, 19, 20 }, { 61, 19, 20 }, { 66, 19, 20 },
                                                           { 67, 19, 20 }, { 68, 19, 20 }, { 69, 19, 20 }, { 70, 19, 20 }, { 71, 19, 20 }, { 72, 19, 20 },
                                                           { 62, 21, 22 }, { 63, 21, 22 }, { 64, 21, 22 },
                                                           { 109, 23, 24 }, { 111, 24, 33 }, { 112, 24, 33 }, { 117, 24, 33 } } };
struct DoorRow { int style, seq, is9, id, type, dx, dy; };
inline constexpr std::array<DoorRow, 34> kDoorRows = { { { 7, 0, 1, 14, 2, 5, 0 }, { 7, 0, 0, 13, 2, 0, 5 }, { 5, 0, 1, 16, 2, 0, 0 }, { 5, 0, 0, 15, 2, 0, 0 },
                                                       { 6, 0, 1, 27, 2, 5, -2 }, { 4, 0, 1, 24, 2, 1, 2 }, { 4, 0, 0, 23, 2, 0, 0 }, { 4, 3, 1, 25, 2, 1, 0 },
                                                       { 1, 2, 0, 62, 2, 0, 3 }, { 1, 2, 1, 63, 2, 3, 0 }, { 0, 0, 1, 16, 2, 0, 0 }, { 0, 0, 0, 64, 2, 0, 0 },
                                                       { 2, 0, 1, 47, 2, 5, 0 }, { 0, 1, 1, 291, 2, 2, 0 }, { 0, 1, 0, 290, 2, 0, 2 }, { 5, 0, 1, 293, 2, 2, 0 },
                                                       { 4, 0, 0, 292, 2, 0, 2 }, { 0, 0, 1, 295, 2, 2, 0 }, { 0, 0, 0, 294, 2, 0, 2 }, { 2, 4, 1, 92, 2, 1, 0 },
                                                       { 2, 1, 0, 91, 2, 0, 2 }, { 0, 1, 1, 229, 2, 0, 0 }, { 0, 1, 0, 230, 2, 0, 0 },
                                                       { 3, 3, 0, 449, 2, -2, 4 }, { 2, 1, 0, 435, 1, 1, 2 }, { 2, 1, 1, 435, 1, 2, 1 }, { 2, 6, 0, 435, 1, 1, 1 },
                                                       { 2, 2, 0, 433, 1, 0, 1 }, { 2, 3, 1, 432, 1, 1, 0 }, { 26, 0, 0, 434, 1, 0, 1 }, { 2, 4, 1, 524, 1, 0, 0 },
                                                       { 2, 4, 0, 525, 1, 0, 0 }, { 29, 0, 1, 60, 2, 2, 0 }, { 29, 0, 0, 60, 2, 0, 2 } } };

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
// Labels count on from the level's counter (level +0x1dc, `next_label`:
// 0 taken as 1, the first label one past it, then it moves one past the
// last); a room without Logicals (FUN_0066ccb0) sets it back to 1 and is
// one area of id 1. Then FUN_0066d040 joins the room's areas to its near
// rooms' up already (index < own: the level's own), merge_areas below.
inline void merge_areas(std::vector<BuiltRoom>& rooms, std::size_t self, std::size_t own);
inline void logic_areas(std::vector<BuiltRoom>& rooms, std::size_t self, std::size_t own, const std::vector<std::size_t>& nearby,
                        const std::vector<std::uint32_t>& orients, const std::vector<std::uint32_t>& floors, std::uint32_t& next_label, bool merge = true) {
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
    auto flood = [&](auto&& again, int x, int y, int dir) -> void {                                 // FUN_0066c3d0
        for (;;) {
            if (x < 0 || y < 0 || x >= width || y >= height) return;
            const auto cell = std::size_t(y * width + x);
            if (labels[cell] & 0x10000000u) return;
            if (!walls[cell]) {
                labels[cell] = label;
                for (int step = 0; step < 4; ++step) again(again, x + kDx[step], y + kDy[step], step);
                return;
            }
            const auto orient = orients.empty() ? 0u : orients[cell] & 0xff;
            const int mask = kMask[(orient < 20 ? kRow[orient] : -1) + 1][dir + 1];
            if (mask & 1) labels[cell] = label;
            if ((mask & 2) && dir != 2) again(again, x + 1, y, 0);
            if ((mask & 4) && dir != 3) again(again, x, y + 1, 1);
            if ((mask & 8) && dir != 0) again(again, x - 1, y, 2);
            if ((mask & 0x10) && dir != 1) again(again, x, y - 1, 3);
            if (!(mask & 0x20)) return;
            ++x; ++y; dir = -1;
        }
    };
    const std::uint32_t first_label = next_label ? next_label : 1;
    std::uint32_t count = first_label;
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
            Area area{ room.x + x, room.y + y, std::min(room.x + right, room.x + room.width), std::min(room.y + bottom, room.y + room.height), id, (value & 0x20000000u) != 0,
                       room.x + x, room.y + y, room.x + right, room.y + bottom };
            if (area.left >= room.x + room.width || area.top >= room.y + room.height) area.left = area.top = area.right = area.bottom = 0;
            room.areas.insert(room.areas.begin(), area);
        }
    next_label = first_label + (count - first_label) + 1;                                                 // +0x1dc += labels made + 1
    if (merge) merge_areas(rooms, self, own);
}

// FUN_0066ceb0's record at cell (x, y) of an up room (on its rect, edges
// included): a Logicals room's from its grid, else the whole room's (id 1).
inline const Area* area_cell(const BuiltRoom& room, int x, int y) {
    static constexpr Area kWhole{ 0, 0, 0, 0, 1, false };
    if (room.areas.empty()) return &kWhole;
    for (const auto& area : room.areas)
        if (x >= area.full_left && y >= area.full_top && x < area.full_right && y < area.full_bottom) return &area;
    return nullptr;
}
// FUN_0066c770: every area of `room` with id `from` takes `into`; if any did,
// so do its near rooms of the level (a room without Logicals has none).
inline void rename_areas(std::vector<BuiltRoom>& rooms, std::size_t room_index, std::size_t own, std::uint32_t from, std::uint32_t into) {
    auto& room = rooms[room_index];
    if (!room.upper || room.areas.empty()) return;
    bool renamed = false;
    for (auto& area : room.areas)
        if (area.id == from) { area.id = into; renamed = true; }
    if (!renamed) return;
    for (const auto other : near_rooms(rooms, room_index, own))
        if (other != room_index && other < own) rename_areas(rooms, other, own, from, into);
}
// FUN_0066d040: for each near room of the level up already and touching
// (FUN_0066b860 gap 1), each cell of this room's border (FUN_0066cf60, the
// top and bottom rows, then the left and right columns, edges included)
// that lies on that room too: two ids, both set, differing, alike in skip
// -> this room's id becomes the other's (rename_areas).
inline void merge_areas(std::vector<BuiltRoom>& rooms, std::size_t self, std::size_t own) {
    auto join = [&](std::size_t other, int x, int y) {
        const auto& there = rooms[other];
        if (x < there.x || y < there.y || x > there.x + there.width || y > there.y + there.height) return;
        const Area* theirs = area_cell(there, x, y);
        const Area* ours = area_cell(rooms[self], x, y);
        if (!theirs || !ours || !theirs->id || !ours->id || theirs->id == ours->id || theirs->skip != ours->skip) return;
        rename_areas(rooms, self, own, ours->id, theirs->id);
    };
    const auto& room = rooms[self];
    for (const auto other : near_rooms(rooms, self, own)) {
        const auto& there = rooms[other];
        if (other == self || other >= own || !there.upper) continue;
        const int gap_x = room.x < there.x ? there.x - room.width - room.x : room.x - there.width - there.x;
        const int gap_y = room.y < there.y ? there.y - room.height - room.y : room.y - there.height - there.y;
        if (gap_x >= 1 || gap_y >= 1) continue;
        for (int x = room.x; x <= room.x + room.width; ++x) { join(other, x, room.y); join(other, x, room.y + room.height); }
        for (int y = room.y; y <= room.y + room.height; ++y) { join(other, room.x, y); join(other, room.x + room.width, y); }
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
// `up_count`: how many of `order` are up; the rest are laid for their tiles, but
// their spawn areas join nothing (they aren't up in game.exe yet).
std::vector<BuiltRoom> level_room_tiles(const std::vector<Outdoor::RoomSeed>& made, const std::vector<PlainRoom>& plain,
                                               const OutdoorData& data, const RoomDt1s& dt1s, int level,
                                               const std::array<WarpSlot, 8>& slots, std::vector<std::string>& notes,
                                               const std::vector<std::size_t>& order = {}, std::vector<BuiltRoom> outside = {}, std::size_t up_count = SIZE_MAX,
                                               const std::vector<std::size_t>& come_up_steps = {});

}  // namespace d2d::drlg
