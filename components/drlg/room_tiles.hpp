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
// (LitVersion), and the warp unit's offset from its tile (OffsetX / Y).
struct WarpSlot { int id = -1; bool lit = false; int off_x = 0, off_y = 0; };

// A tile a room holds: level-relative x, y; the word it came from.
struct PlacedTile {
    int layer, x, y, orient;                            // layer 0 wall, 1 floor, 2 shadow
    const Dt1File* file;
    int index;
    std::uint32_t word;
};
struct BuiltRoom {
    int x, y, width = 8, height = 8, kind = 1;                   // level-relative tiles
    std::vector<PlacedTile> tiles;                      // in the order game.exe adds them
    // Edge tiles other rooms can find (FUN_0066e4c0): chains of tile
    // indices, newest first, chains newest first; floor chains vs the rest.
    struct Chain { bool floor; std::vector<std::size_t> tiles; };
    std::vector<Chain> chains;
    // Hidden warp tiles (orientation 10/11, style = the level's warp slot):
    // level-relative cell and slot — where the warp unit stands (FUN_0066e1c0).
    struct Warp { int x, y, slot; };
    std::vector<Warp> warps;
    // Units for the server (room +0x5c): room-relative subtiles, newest first.
    std::vector<Unit> units;
    bool upper = false;
    const PlainRoom* plain = nullptr;
    const Outdoor::RoomSeed* seed = nullptr;
};

namespace room_tiles_detail {

inline constexpr std::array<int, 20> kOrientClass = { -1, 0, 1, 2, -1, 3, 4, 5, -2, -2, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1 };   // 0x6ef620
inline constexpr std::array<int, 42> kOrientMerge = { 0, 1, 3, 3, 4, 1, 3, 1, 1, 2, 3, 4, 3, 2, 2, 3, 3, 3, 4, 3, 3,              // 0x6ef574
                                                      3, 1, 3, 3, 4, 5, 6, 1, 3, 2, 3, 4, 3, 6, 2, 1, 2, 3, 4, 1, 2 };

// FUN_0066bc20 + FUN_0066bbc0: the level's rooms (list order) within a
// 6-tile gap on both axes, itself included, then bubble-sorted so a room
// wholly left of or above the one before it moves ahead.
inline std::vector<std::size_t> near_rooms(const std::vector<BuiltRoom>& rooms, std::size_t self) {
    const auto& room = rooms[self];
    std::vector<std::size_t> close_rooms;
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        const auto& other = rooms[i];
        const int gap_x = room.x < other.x ? other.x - room.width - room.x : room.x - other.width - other.x;
        const int gap_y = room.y < other.y ? other.y - room.height - room.y : room.y - other.height - other.y;
        if (gap_x < 6 && gap_y < 6) close_rooms.push_back(i);
    }
    for (std::size_t k = close_rooms.size() ? close_rooms.size() - 1 : 0; k > 0; --k)
        for (std::size_t i = 0; i + 1 < close_rooms.size(); ++i) {
            const auto &first = rooms[close_rooms[i]], &second = rooms[close_rooms[i + 1]];
            if (second.x + second.width <= first.x || second.y + second.height <= first.y) std::swap(close_rooms[i], close_rooms[i + 1]);
        }
    return close_rooms;
}

}  // namespace room_tiles_detail

// Every room of a level with its tiles; `made` in the order game.exe made
// them (Outdoor::rooms, generate_maze), `plain` an outdoor level's plain rooms.
// `slots`: the level's warp slots (warp_slots).
inline std::vector<BuiltRoom> level_room_tiles(const std::vector<Outdoor::RoomSeed>& made, const std::vector<PlainRoom>& plain,
                                               const OutdoorData& data, const RoomDt1s& dt1s, int level,
                                               const std::array<WarpSlot, 8>& slots, std::vector<std::string>& notes) {
    using namespace room_tiles_detail;
    auto note = [&](std::string message) { if (std::ranges::find(notes, message) == notes.end()) notes.push_back(std::move(message)); };
    std::vector<BuiltRoom> rooms;                       // game.exe's list: newest room first
    for (auto made_room = made.rbegin(); made_room != made.rend(); ++made_room) {
        BuiltRoom room{ made_room->x, made_room->y, made_room->width, made_room->height, made_room->kind, {}, {}, {}, {}, false, nullptr, &*made_room };
        if (made_room->kind == 1)
            for (const auto& plain_room : plain) if (plain_room.x == made_room->x && plain_room.y == made_room->y) room.plain = &plain_room;
        rooms.push_back(std::move(room));
    }
    std::map<std::pair<int, int>, std::vector<Unit>> preset_units;   // by the preset's origin: units no room has taken yet
    for (std::size_t room_index = 0; room_index < rooms.size(); ++room_index) {
        auto& room = rooms[room_index];
        room.upper = true;
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
                auto list = ds1_units(*map, data.ids);
                // FUN_00667620: some roll to stay, in the loader's order, on
                // the seed of the room bringing them up (outdoors; a maze
                // rolls at generation on the level's seed). The tiles' seed
                // is reset after (FUN_0066ee40), so they don't shift.
                if (plain.empty()) {                    // a maze (generate_maze has no plain rooms)
                    if (std::ranges::any_of(list, [&](const Unit& unit) { return rolled_unit(unit, data.ids); }))
                        note("drlg: a maze's preset units that roll to stay (FUN_00667970, the level seed) not implemented");
                } else {
                    d2d::rules::Rng roll{ room.seed->seed };
                    std::erase_if(list, [&](const Unit& unit) { return !stays(unit, data.ids, roll); });
                }
                std::ranges::reverse(list);             // copied into the preset's list front-first again (FUN_00667510)
                for (auto& unit : list) {
                    unit.x += room.seed->preset_x * 5;
                    unit.y += room.seed->preset_y * 5;
                }
                if (level == 2 && room.seed->def >= 4 && room.seed->def <= 7 && room.seed->file == 3 && data.ids.monstats > 0x10a)
                    list.insert(list.begin(), { 1, 0x10a, 1, (room.seed->preset_x + pre->width / 2) * 5, (room.seed->preset_y + pre->height / 2) * 5, 0 });   // FUN_006664a0: Flavie
                found = preset_units.emplace(key, std::move(list)).first;
            }
            auto& list = found->second;
            for (auto unit_it = list.begin(); unit_it != list.end();) {
                if (unit_it->x >= room.x * 5 && unit_it->y >= room.y * 5 && unit_it->x < (room.x + room.width) * 5 && unit_it->y < (room.y + room.height) * 5) {
                    room.units.insert(room.units.begin(), { unit_it->type, unit_it->id, unit_it->mode, unit_it->x - room.x * 5, unit_it->y - room.y * 5, unit_it->flags });
                    unit_it = list.erase(unit_it);
                } else {
                    ++unit_it;
                }
            }
        }
        const auto list = room_dt1_list(room.plain ? room.plain->dt1_mask : pre->dt1_mask, dt1s);
        const auto nearby = near_rooms(rooms, room_index);
        // Reset by FUN_0066ee40; a plain room's init (grass, roads, stamps) rolls it on.
        auto rng = room.plain ? room.plain->seed : d2d::rules::Rng{ room.seed->seed };
        if (room.plain)
            for (const auto& tile : room.plain->tiles) room.tiles.push_back({ tile.layer, room.x + tile.x, room.y + tile.y, tile.orient, tile.file, tile.index, 0 });
        auto pick = [&](int orient, std::uint32_t word) { return pick_tile(list, rng, orient, word); };
        auto add = [&](int layer, int x, int y, int orient, std::uint32_t word) {
            const auto [tile_file, tile_index] = pick(orient, word);
            room.tiles.push_back({ layer, x, y, orient, tile_file, tile_index, word });
            return room.tiles.size() - 1;
        };
        auto chain = [&](bool floor) -> BuiltRoom::Chain& {                    // FUN_0066e620's node
            for (auto& existing : room.chains) if (existing.floor == floor) return existing;
            room.chains.insert(room.chains.begin(), BuiltRoom::Chain{ floor, {} });
            return room.chains.front();
        };
        // FUN_0066e4c0: a chained tile of room N at (x, y) this word can share.
        auto find = [&](BuiltRoom& neighbour, bool floor, int x, int y, std::uint32_t word) -> PlacedTile* {
            if (!neighbour.upper || x < neighbour.x || y < neighbour.y || x > neighbour.x + neighbour.width || y > neighbour.y + neighbour.height) return nullptr;
            for (auto& existing : neighbour.chains) {
                if (existing.floor != floor) continue;
                for (const auto tile_index : existing.tiles) {
                    auto& tile = neighbour.tiles[tile_index];
                    if (tile.x == x && tile.y == y && tile.orient != 4 && (tile.orient == 13 || !(word & 0x8000000u))
                        && ((tile.word >> 18) & 3) == ((word >> 18) & 3))
                        return &tile;
                }
            }
            return nullptr;
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
                chain(tile_orient == 0).tiles.insert(chain(tile_orient == 0).tiles.begin(), added);
                if (tile_orient == 3) add(0, x, y, 4, word);
                if (tile_orient == 10 || tile_orient == 11) note("drlg: warp wall tiles (FUN_0066e260) not implemented");
                return;
            }
            // FUN_0066e740: the neighbour's tile stays unless the orientations merge differently.
            if (tile->word & 0x80) return;                                          // its flags & 1
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
                chain(false);
                add(0, x, y, 3, word);
            }
            const bool blank = tile->orient == 0 && tile->file && tile->index >= 0 && tile->file->tiles[std::size_t(tile->index)].style == 30
                               && tile->file->tiles[std::size_t(tile->index)].seq == 0;
            if (orient != tile->orient || blank) {
                const auto [tile_file, tile_index] = pick(orient, word);
                tile->orient = orient;
                tile->file = tile_file;
                tile->index = tile_index;
            }
        };
        auto word = [&](std::uint32_t tile_word, int tile_orient, int x, int y, bool fill) {       // FUN_0066e9b0
            const int style = int((tile_word >> 20) & 0x3f), seq = int((tile_word >> 8) & 0xff);
            if ((tile_orient == 10 || tile_orient == 11) && style > 7) return;
            if (tile_orient == 0 && style == 30 && seq <= 1) tile_word |= 0x80000000u;
            if (tile_word & 0x80000000u) {
                if ((tile_orient == 8 || tile_orient == 9) && (level < 111 || (level > 112 && level != 117))) { note("drlg: hidden orientation 8/9 tiles (FUN_0066d9e0) not implemented"); return; }
                if (tile_orient == 10 || tile_orient == 11) {                // FUN_0066e1c0 (the warp unit), FUN_0066e360
                    room.warps.push_back({ x, y, style });
                    const auto& warp_slot = slots[std::size_t(style)];   // style <= 7 here: the warp slot
                    if (warp_slot.id >= 0 && x - room.x != room.width && y - room.y != room.height)   // the warp unit
                        room.units.insert(room.units.begin(), { 5, warp_slot.id, 0, (x - room.x) * 5 + warp_slot.off_x, (y - room.y) * 5 + warp_slot.off_y, 0 });
                    if (warp_slot.lit)
                        for (int k = 0; k < 4; ++k) {    // its lit floor, 2x2 up-left of it (0x6ef554)
                            const std::uint32_t warp_word = std::uint32_t(seq) << 20 | std::uint32_t(k | 4) << 8;
                            const auto [tile_file, tile_index] = pick(0, warp_word);
                            room.tiles.push_back({ 1, x - 1 + (k & 1), y - 1 + (k >> 1), 0, tile_file, tile_index, warp_word });
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
                const std::uint32_t blank = level == 0x4a ? 0x1e00100u : 0x1e00000u;
                const auto [tile_file, tile_index] = pick(0, blank);
                room.tiles.push_back({ 1, x, y, 0, tile_file, tile_index, (tile_word & ~0x80u) | 0x80000000u });
            }
            if (tile_word & 1) {
                add(0, x, y, tile_orient, tile_word);
                if (tile_orient == 3) add(0, x, y, 4, tile_word);
                if ((tile_orient == 10 || tile_orient == 11) && level != 0x85) note("drlg: warp wall tiles (FUN_0066e260) not implemented");
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
            continue;
        }
        // A preset room (FUN_006667d0 then FUN_00666ac0): its 9x9 slice of each
        // DS1 layer, edges | 0x84 (floors | 0x80 throughout); walked floors
        // (the first with FillBlanks), walls, shadow, 8 wide / high where
        // KillEdge meets the preset's own right / bottom edge.
        const int origin_x = room.x - room.seed->preset_x, origin_y = room.y - room.seed->preset_y;
        auto slice = [&](const d2d::ds1::Layer& source, bool orient) {
            std::array<std::uint32_t, 81> words{};
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) {
                    const int source_x = origin_x + x, source_y = origin_y + y;
                    if (source_x < 0 || source_y < 0 || source_x >= map->width() || source_y >= map->height()) continue;
                    const auto& tile = source.cells[std::size_t(source_y) * std::size_t(map->width()) + std::size_t(source_x)];
                    words[std::size_t(y * 9 + x)] = orient ? std::uint32_t(tile.wall_type) | tile.wall_zero << 8 : d2d::ds1::Map::word(tile);
                }
            return words;
        };
        auto edges = [](std::array<std::uint32_t, 81>& words, std::uint32_t value) {
            for (int i = 0; i < 9; ++i) for (const int edge : { i, 72 + i, i * 9, i * 9 + 8 }) words[std::size_t(edge)] |= value;
        };
        const bool kill_x = room.x + room.width == room.seed->preset_x + pre->width, kill_y = room.y + room.height == room.seed->preset_y + pre->height;   // KillEdge is 1 on every act 1 preset
        const int words_wide = kill_x ? 8 : 9, words_high = kill_y ? 8 : 9;
        auto walk = [&](const std::array<std::uint32_t, 81>& words, const std::array<std::uint32_t, 81>* orient, bool fill) {   // FUN_0066ec10
            for (int y = 0; y < words_high; ++y)
                for (int x = 0; x < words_wide; ++x)
                    word(words[std::size_t(y * 9 + x)], orient ? int((*orient)[std::size_t(y * 9 + x)] & 0xff) : 0, room.x + x, room.y + y, fill);
        };
        for (std::size_t layer_index = 0; layer_index < map->floors().size(); ++layer_index) {
            auto words = slice(map->floors()[layer_index], false);
            for (auto& tile_word : words) tile_word |= 0x80;                // FUN_0067c590
            edges(words, 0x84);
            walk(words, nullptr, layer_index == 0);
        }
        for (std::size_t layer_index = 0; layer_index < map->walls().size(); ++layer_index) {
            auto words = slice(map->walls()[layer_index], false);
            const auto orients = slice(map->walls()[layer_index], true);
            if (layer_index == 0) edges(words, 0x84);
            else for (auto& tile_word : words) tile_word |= 0x80;           // FUN_0067c590
            walk(words, &orients, false);
        }
        if (!map->shadows().empty()) {
            auto words = slice(map->shadows()[0], false);
            edges(words, 0x84);
            walk(words, nullptr, false);
        }
    }
    return rooms;
}

}  // namespace d2d::drlg
