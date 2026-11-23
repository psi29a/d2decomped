// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for room_tiles.hpp.
#include "room_tiles.hpp"

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
#include <tuple>
#include <utility>
#include <vector>

namespace d2d::drlg {

std::vector<BuiltRoom> level_room_tiles(const std::vector<Outdoor::RoomSeed>& made, const std::vector<PlainRoom>& plain,
                                               const OutdoorData& data, const RoomDt1s& dt1s, int level,
                                               const std::array<WarpSlot, 8>& slots, std::vector<std::string>& notes,
                                               const std::vector<std::size_t>& order, std::vector<BuiltRoom> outside, std::size_t up_count) {
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
    for (auto& room : outside) {                        // their shares' owners index `outside`: now after ours
        for (auto& share : room.shares) share.owner += int(own);
        rooms.push_back(std::move(room));
    }
    // Each room's tile seed and DT1s outlive its walk: a later room re-picking a shared tile rolls the owner's.
    std::vector<d2d::rules::Rng> rngs(rooms.size());
    std::vector<decltype(room_dt1_list(0u, dt1s))> lists(rooms.size());
    std::map<std::pair<int, int>, std::vector<Unit>> preset_units;   // by the preset's origin: units no room has taken yet
    std::uint32_t next_label = 0;                       // level +0x1dc: the next area label (logic_areas)
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
                    roll.high = room.seed->seed_high;                    // the room's whole seed, +0x14 (FUN_00667890)
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
                    room.units.insert(room.units.begin(), { unit_it->type, unit_it->id, unit_it->mode, unit_it->x - room.x * 5, unit_it->y - room.y * 5, unit_it->flags, unit_it->path, unit_it->actions });
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
            for (const auto& tile : room.plain->tiles) room.tiles.push_back({ tile.layer, room.x + tile.x, room.y + tile.y, tile.orient, tile.file, tile.index, tile.word });
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
        // Objects 0x5b / 0x5c (the tombs') are made unless rand(3) on the
        // room's seed is 0; the tile's marked either way.
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
                    if (row.type == 1) room.units.insert(room.units.begin(), { 1, row.id, 1, unit_x, unit_y, 0 });   // FUN_004e6c50: a MonStats row
                    else if (!((row.id == 0x5b || row.id == 0x5c) && rng(3) == 0)) room.units.insert(room.units.begin(), { 2, row.id, 0, unit_x, unit_y, 0 });
                    if (tile) tile->door = true;
                    return;
                }
            }
        };
        auto warp_unit = [&](const WarpSlot& warp_slot, int x, int y, int tile_orient) {          // FUN_0066e1c0
            if (warp_slot.id < 0 || x - room.x == room.width || y - room.y == room.height) return false;
            const bool right = tile_orient == 11;
            room.units.insert(room.units.begin(), { 5, warp_slot.id, 0, (x - room.x) * 5 + (right ? warp_slot.right_x : warp_slot.off_x),
                                                    (y - room.y) * 5 + (right ? warp_slot.right_y : warp_slot.off_y), 0 });
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
            if (seq == 0 || seq == 4) {
                if (!warp_unit(warp_slot, x, y, tile_orient)) return;
                room.wall_warps.push_back({ x, y, style });
            }
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
                    warp_unit(warp_slot, x, y, tile_orient);
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
            next_label = 1;                                // FUN_0066ccb0: one area, id 1
            continue;
        }
        // A preset room (FUN_006667d0 then FUN_00666ac0): its (w+1)x(h+1) slice
        // of each DS1 layer, edges | 0x84, layers past the first | index << 18; walked
        // floors (the first with FillBlanks, when the row says so), walls, shadow, one short where
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
            walk(words, nullptr, layer_index == 0 && pre->fill_blanks);
        }
        for (std::size_t layer_index = 0; layer_index < map->walls().size(); ++layer_index) {
            auto words = slice(map->walls()[layer_index], false);
            const auto orients = slice(map->walls()[layer_index], true);
            if (layer_index == 0) edges(words, 0x84);
            else for (auto& tile_word : words) tile_word |= std::uint32_t(layer_index) << 18;   // FUN_0067c590
            walk(words, &orients, false);
            // FUN_00667970 scans the whole DS1 once and flags its 8x8 cell (x / 8,
            // y / 8; cell 0 for a one-room preset); FUN_00667ed0's rooms are those
            // cells, so the room's own cells flag the same (drlg.md "Presets").
            for (int y = 0; y < room.height; ++y)
                for (int x = 0; x < room.width; ++x) {
                    const auto tile_word = words[std::size_t(y * slice_w + x)];
                    const int orient = int(orients[std::size_t(y * slice_w + x)] & 0xff), style = int((tile_word >> 20) & 0x3f), seq = int((tile_word >> 8) & 0xff);
                    if ((orient == 10 || orient == 11) && style < 8 && (seq == 0 || seq == 4 || (tile_word & 0x80000000u))) room.vis |= 0x10u << style;
                    const int info = style == 30 ? seq : style == 31 ? seq + 5 : style == 32 ? 10 : style == 33 ? 11 : -1;   // the tile info's type
                    if ((orient == 10 || orient == 11) && info == 11) room.starts.push_back({ room.x + x, room.y + y });
                    if ((orient == 10 || orient == 11) && info >= 0) room.spawns.push_back({ room.x + x, room.y + y, info });
                }
        }
        if (!map->shadows().empty()) {
            auto words = slice(map->shadows()[0], false);
            edges(words, 0x84);
            walk(words, nullptr, false);
        }
        // FUN_0066d700 / FUN_0066d440 (DrlgAnim.cpp), an Animate preset: each
        // tile of the walls, floors, shadow (as listed so far) whose DT1 says
        // animated (+6 & 0x100) becomes its key's rarity-0 tile, and rarity
        // 1..n-1 (the frames) follow it onto the list, hidden (+0x14 | 8) till their turn
        // (FUN_0066d3b0: world_view's animated_tile).
        if (pre->animate)
            for (const int layer : { 0, 1, 2 }) {
                const std::size_t listed = room.tiles.size();
                for (std::size_t i = 0; i < listed; ++i) {
                    const auto tile = room.tiles[i];
                    if (tile.layer != layer || !tile.file || tile.index < 0 || !(tile.file->tiles[std::size_t(tile.index)].material & 0x100)) continue;
                    const std::uint32_t key = tile.word;
                    const auto frames = tile_candidates(list, tile.orient, key ? int((key >> 20) & 0x3f) : 0, key ? int((key >> 8) & 0xff) : 0);
                    auto frame = [&](int number) {
                        const auto found = std::ranges::find_if(frames, [&](const auto& candidate) { return candidate.first->tiles[std::size_t(candidate.second)].rarity == number; });
                        return found == frames.end() ? std::pair<const Dt1File*, int>{ nullptr, -1 } : *found;
                    };
                    std::tie(room.tiles[i].file, room.tiles[i].index) = frame(0);
                    const auto count = std::uint8_t(std::min<std::size_t>(frames.size(), 0xff));
                    if (count > 1) room.tiles[i].frames = count;
                    for (int number = 1; number < int(count); ++number) {
                        const auto [file, index] = frame(number);
                        room.tiles.push_back({ layer, tile.x, tile.y, tile.orient, file, index, key });
                        room.tiles.back().frame = std::uint8_t(number);
                        room.tiles.back().frames = count;
                    }
                }
            }
        if (pre->logicals)
            logic_areas(rooms, room_index, own, nearby, map->walls().empty() ? Words{} : slice(map->walls()[0], true),
                        [&] { auto words = map->floors().empty() ? Words(std::size_t(slice_w * slice_h)) : slice(map->floors()[0], false); edges(words, 0x84); return words; }(), next_label, step < up_count);
        else
            next_label = 1;                                // FUN_0066ccb0
        room.room1_seed = rng.next();
    }
    return rooms;
}

}  // namespace d2d::drlg
