// SPDX-License-Identifier: GPL-3.0-or-later
// The automap's reveal (automap.md "Adding cells"): FUN_00459020 each
// frame, FUN_00458f40 a room, FUN_00457cf0 a tile.
#include "automap_reveal.hpp"

#include "common.hpp"
#include "panels.hpp"
#include "scene.hpp"

#include <automap_step.hpp>
#include <level_ids.hpp>
#include <outdoor_data.hpp>
#include <room_tiles.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kFloorSlot = 16, kWallSlot = 32;   // a DS1 layer's slot (AutomapReveal::drawn)
constexpr std::uint32_t kGameFrameMs = 40;        // FUN_0044c790's client game frame, 25 a second

// One tile of a room's list as FUN_00457cf0 reads it: where it is, its
// drawn slot, the DT1 header's orientation, main and sub index, the room
// tile's own orientation (a lower wall past 15) and its flag 8.
struct ListTile { int x, y, slot, orientation, main, sub, room_orientation; bool hidden; };

// The room's two lists in their order: a generated level's from
// Level::room_tiles (shown()'s picks), a preset level's from preset_rooms
// (its DS1 words: style, sequence, the layer's index at bit 18).
std::array<std::vector<ListTile>, 2> room_lists(const GameData& game_data, const Level& level, int room, AutomapReveal& reveal) {
    std::array<std::vector<ListTile>, 2> lists;
    const int width = int(level.ds1.width());
    if (width == 0) return lists;
    if (!level.rooms.empty()) {
        if (room < 0 || std::size_t(room) >= level.room_tiles.size() || level.shown().empty()) return lists;
        for (std::size_t list = 0; list < 2; ++list)
            for (const auto& entry : level.room_tiles[std::size_t(room)][list]) {
                const auto& pick = level.shown()[entry.cell][entry.pick];
                lists[list].push_back({ int(entry.cell) % width, int(entry.cell) / width, int(entry.pick), pick.tile->type, pick.tile->style,
                                        pick.tile->sequence, pick.orient, pick.hidden });
            }
        return lists;
    }
    auto made = reveal.presets.find(&level);
    if (made == reveal.presets.end()) {
        const d2d::drlg::OutdoorAssets* assets = nullptr;   // any generated level's: the presets' rows and DS1s
        for (const auto& [id, built] : game_data.levels)
            if (built && built->assets) { assets = built->assets; break; }
        if (!assets) return lists;                           // none built yet: again next time
        made = reveal.presets.emplace(&level, game::preset_rooms(game_data, level, *assets)).first;
    }
    if (room < 0 || std::size_t(room) >= made->second.size()) return lists;
    for (const auto& tile : made->second[std::size_t(room)].tiles) {
        if (tile.layer > 1 || tile.x < 0 || tile.y < 0 || tile.x >= width || tile.y >= int(level.ds1.height())) continue;
        const int layer_index = int((tile.word >> 18) & 3);
        // The picked tile's DT1 header, (10, 0, 0) where none matched the word
        // (FUN_0066d820); no pick (no DT1s loaded): the word's own.
        int orientation = tile.layer == 1 ? 0 : tile.orient, style = int((tile.word >> 20) & 0x3f), sequence = int((tile.word >> 8) & 0xff);
        if (tile.file && tile.index >= 0) {
            const auto& head = tile.file->tiles[std::size_t(tile.index)];
            orientation = head.orient;
            style = head.style;
            sequence = head.seq;
        }
        lists[tile.layer == 1 ? 0 : 1].push_back({ tile.x, tile.y, (tile.layer == 1 ? kFloorSlot : kWallSlot) + layer_index, orientation, style, sequence, tile.orient,
                                                   (tile.word & 0x80000000u) != 0 });
    }
    return lists;
}

}  // namespace

void automap_mark(AutomapReveal& reveal, const Level& level, std::span<const DrawnCell> cells, int shift_x, int shift_y) {
    const auto& map = level.ds1;
    const int width = int(map.width()), height = int(map.height());
    if (width == 0) return;
    for (const auto& drawn : cells) {
        const int tile_x = drawn.x - shift_x, tile_y = drawn.y - shift_y;
        if (tile_x < 0 || tile_y < 0 || tile_x >= width || tile_y >= height) continue;
        const auto index = std::size_t(tile_y * width + tile_x);
        const auto mark = [&](int slot) { reveal.drawn.insert({ &level, std::uint32_t(index), slot }); };
        if (!level.shown().empty()) {
            const auto& picks = level.shown()[index];
            for (std::size_t slot = 0; slot < picks.size(); ++slot)
                if (!drawn.source || drawn.source == &picks[slot]) mark(int(slot));
            continue;
        }
        for (std::size_t layer = 0; layer < map.floors().size(); ++layer)
            if (!drawn.source || drawn.source == &map.floors()[layer].cells[index]) mark(kFloorSlot + int(layer));
        for (std::size_t layer = 0; layer < map.walls().size(); ++layer)
            if (!drawn.source || drawn.source == &map.walls()[layer].cells[index]) mark(kWallSlot + int(layer));
    }
}

void automap_room(const Scene& scene, const GameData& game_data, const Level& level, int room, Automap& automap, AutomapReveal& reveal, bool all) {
    const int width = int(level.ds1.width());
    const auto lists = room_lists(game_data, level, room, reveal);
    for (std::size_t list = 0; list < 2; ++list)
        for (std::size_t place = 0; place < lists[list].size(); ++place) {
            const auto& tile = lists[list][place];
            if (tile.hidden) continue;
            if (!all && !reveal.drawn.contains({ &level, std::uint32_t(tile.y * width + tile.x), tile.slot })) continue;
            if (!reveal.added.insert({ &level, room, int(list), place }).second) continue;   // flag 0x40000, before the lookup
            const int cel = automap_cel(scene, level, tile.orientation, tile.main, tile.sub, reveal.seed);
            if (cel < 0) continue;
            const int act_x = tile.x + level.world_x, act_y = tile.y + level.world_y;     // FUN_00643310, then / 10
            const int automap_x = (act_x - act_y) * 80 / 10, automap_y = (act_x + act_y) * 40 / 10;
            automap.add({ cel, automap_x, automap_y + (tile.room_orientation > 15 ? 24 : 0), int(list) });
        }
}

void automap_step(const Scene& scene, const GameData& game_data, const Level& level, float x, float y, std::uint32_t now_ms, Automap& automap,
                  AutomapReveal& reveal) {
    const float act_x = x + float(level.world_x), act_y = y + float(level.world_y);   // the unit's client px (path +8 / +0xc)
    const int pixel_x = int(std::lround((act_x - act_y) * 80)), pixel_y = int(std::lround((act_x + act_y) * 40));
    if (!reveal.frame_ms) reveal.frame_ms = now_ms;                                  // a game's first frame is now
    bool moved = false;
    for (; std::int32_t(now_ms - *reveal.frame_ms) >= 0; *reveal.frame_ms += kGameFrameMs)   // each 40 ms game frame due
        moved = d2d::rules::automap_frame(reveal.step, pixel_x, pixel_y) || moved;
    if (!moved) return;
    for (const auto& near_room : game::automap_rooms(game_data, level, x, y))
        if (near_room.level->layer == level.layer) automap_room(scene, game_data, *near_room.level, near_room.room, automap, reveal, false);
}

void automap_whole(const Scene& scene, const GameData& game_data, const Level& level, Automap& automap, AutomapReveal& reveal) {
    if (std::ranges::contains(d2d::rules::level_ids::kMiniatureTowns, level.id)) return;
    if (!game_data.prest_automap.contains(level.id) || reveal.whole.contains(&level)) return;
    if (automap_all(scene, game_data, level, automap, reveal)) reveal.whole.insert(&level);
}

bool automap_all(const Scene& scene, const GameData& game_data, const Level& level, Automap& automap, AutomapReveal& reveal) {
    if (level.rooms.empty()) room_lists(game_data, level, 0, reveal);   // makes a preset level's rooms
    const auto made = reveal.presets.find(&level);
    if (level.rooms.empty() && made == reveal.presets.end()) return false;   // no rooms to walk yet
    const int rooms = level.rooms.empty() ? int(made->second.size()) : int(level.rooms.size());
    for (int room = rooms - 1; room >= 0; --room) automap_room(scene, game_data, level, room, automap, reveal, true);
    return true;
}

}  // namespace d2d::client
