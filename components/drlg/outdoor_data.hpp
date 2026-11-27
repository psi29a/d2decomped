// SPDX-License-Identifier: GPL-3.0-or-later
// Loading what the outdoor generator needs from the game's tables and
// DS1s, through a `read(path) -> optional<vector<byte>>` callable (the app
// and the tests hand it their MPQ stack).
#pragma once

#include "drlg.hpp"
#include "outdoor.hpp"
#include "room_tiles.hpp"
#include "tile_pick.hpp"

#include <ds1.hpp>
#include <rules.hpp>
#include <txt.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::drlg {

int to_int(std::string_view text, int fallback = 0);

// The tables plus every DS1 the act 1 wilderness can place or stamp.
// Presets point into `maps`, so fill one in place and don't copy it.
struct OutdoorAssets {
    d2d::txt::Table levels, lvl_types, lvl_warp, lvl_maze;
    std::deque<Dt1File> dt1s;                           // loaded by load_room_dt1s
    std::deque<d2d::ds1::Map> maps;
    OutdoorData data;
    OutdoorAssets() = default;
    OutdoorAssets(const OutdoorAssets&) = delete;
    OutdoorAssets& operator=(const OutdoorAssets&) = delete;
};

template <class Read> void load_outdoor_assets(OutdoorAssets& assets, Read&& read) {
    auto table = [&](const char* name) {
        const auto bytes = read(std::string(R"(data\global\excel\)") + name);
        return bytes ? d2d::txt::Table(*bytes) : d2d::txt::Table{};
    };
    auto ds1 = [&](std::string_view rel) -> const d2d::ds1::Map* {
        if (rel.empty() || rel == "0") return nullptr;
        std::string path = R"(data\global\tiles\)";
        for (const char letter : rel) path.push_back(letter == '/' ? '\\' : letter);
        const auto bytes = read(path);
        if (!bytes) return nullptr;
        try { return &assets.maps.emplace_back(*bytes); } catch (...) { return nullptr; }
    };
    assets.levels = table("Levels.txt");
    assets.lvl_types = table("LvlTypes.txt");
    assets.lvl_warp = table("LvlWarp.txt");
    assets.lvl_maze = table("LvlMaze.txt");
    // Unit ids (FUN_00665950): monpreset.bin is {int count; {byte act, byte kind, u16 id}[count]}.
    if (const auto bytes = read(R"(data\global\excel\monpreset.bin)"); bytes && bytes->size() >= 4) {
        auto byte_at = [&](std::size_t offset) { return int(std::to_integer<unsigned>((*bytes)[offset])); };
        const int count = byte_at(0) | byte_at(1) << 8 | byte_at(2) << 16 | byte_at(3) << 24;
        for (int i = 0; i < count && std::size_t(4 + 4 * i + 3) < bytes->size(); ++i) {
            const auto offset = std::size_t(4 + 4 * i);
            const int act = byte_at(offset) - 1;
            if (act >= 0 && act < 5) assets.data.ids.monpreset[std::size_t(act)].emplace_back(byte_at(offset + 1), byte_at(offset + 2) | byte_at(offset + 3) << 8);
        }
    }
    for (const auto* name : { "MonStats.txt", "SuperUniques.txt" }) {
        const auto loaded = table(name);
        int rows = 0;
        for (std::size_t row = 0; row < loaded.size(); ++row) rows += loaded.get(row, std::optional<std::size_t>{ 0 }) != "Expansion";
        (std::string_view(name) == "MonStats.txt" ? assets.data.ids.monstats : assets.data.ids.superuniques) = rows;
    }
    const auto prest = table("LvlPrest.txt");
    for (std::size_t row = 0; row < prest.size(); ++row) {
        const int def = to_int(prest.get(row, "Def"));
        const auto name = prest.get(row, std::optional<std::size_t>{ 0 });
        if (def < 1 || !name.starts_with("Act ")) continue;   // the acts' presets (not the Expansion header row)
        Preset preset{ to_int(prest.get(row, "SizeX")), to_int(prest.get(row, "SizeY")), to_int(prest.get(row, "Files")),
                  to_int(prest.get(row, "Scan")), to_int(prest.get(row, "Pops")),
                  to_int(prest.get(row, "LevelId")), to_int(prest.get(row, "KillEdge")), std::uint32_t(std::stoul("0" + std::string(prest.get(row, "Dt1Mask")))), {} };
        for (int i = 0; i < 6; ++i) preset.maps[std::size_t(i)] = ds1(prest.get(row, "File" + std::to_string(i + 1)));
        preset.populate = to_int(prest.get(row, "Populate"));
        preset.logicals = to_int(prest.get(row, "Logicals"));
        preset.animate = to_int(prest.get(row, "Animate"));
        preset.fill_blanks = to_int(prest.get(row, "FillBlanks"));
        assets.data.presets[def] = preset;
    }
    const auto sub = table("LvlSub.txt");
    for (std::size_t row = 0; row < sub.size(); ++row) {
        Sub sub_row;
        sub_row.type = to_int(sub.get(row, "Type"), -1);
        sub_row.check_all = to_int(sub.get(row, "CheckAll"));
        sub_row.bord_type = to_int(sub.get(row, "BordType"));
        sub_row.grid_size = std::max(1, to_int(sub.get(row, "GridSize"), 1));
        sub_row.dt1_mask = std::uint32_t(std::stoul("0" + std::string(sub.get(row, "Dt1Mask"))));
        for (int type = 0; type < 5; ++type) {
            sub_row.prob[std::size_t(type)] = to_int(sub.get(row, "Prob" + std::to_string(type)));
            sub_row.trials[std::size_t(type)] = to_int(sub.get(row, "Trials" + std::to_string(type)));
            sub_row.max[std::size_t(type)] = to_int(sub.get(row, "Max" + std::to_string(type)));
        }
        if (sub_row.type >= 0) sub_row.map = ds1(sub.get(row, "File"));
        assets.data.subs.push_back(sub_row);
    }
}

// The DT1s rooms of LvlTypes row `type` can list (by mask bit), plus the
// three every room gets (FUN_0066f240).
template <class Read> RoomDt1s load_room_dt1s(OutdoorAssets& assets, Read&& read, int type) {
    RoomDt1s dt1s;
    auto load = [&](const std::string& rel) -> const Dt1File* {
        if (rel.empty() || rel == "0") return nullptr;
        std::string path = R"(data\global\tiles\)", name;
        for (const char letter : rel) path.push_back(letter == '/' ? '\\' : letter);
        for (const char letter : rel.substr(rel.find_last_of("/\\") + 1)) name.push_back(char(std::tolower(static_cast<unsigned char>(letter))));
        const auto bytes = read(path);
        if (!bytes) return nullptr;
        return &assets.dt1s.emplace_back(dt1_heads(std::move(name), *bytes));
    };
    for (std::size_t row = 0; row < assets.lvl_types.size(); ++row)
        if (to_int(assets.lvl_types.get(row, "Id"), -1) == type)
            for (int i = 0; i < 32; ++i) dt1s.by_bit[std::size_t(i)] = load(std::string(assets.lvl_types.get(row, "File " + std::to_string(i + 1))));
    dt1s.always = { load("Act1/Outdoors/Blank.dt1"), load("Act1/Barracks/InvisWal.dt1"), load("Act1/Barracks/Warp.dt1") };
    return dt1s;
}

// Level `id`'s warp slots (Levels.txt Warp0..7) with their LvlWarp rows.
std::array<WarpSlot, 8> warp_slots(const OutdoorAssets& assets, int id);

// Levels.txt row by Id.
std::optional<std::size_t> level_row(const d2d::txt::Table& levels, int id);

// A level apart from the act layout: its OffsetX / Y, from its Depend
// level's when it has one (FUN_00642d10).
std::pair<int, int> level_origin(const d2d::txt::Table& levels, std::size_t row);

// Sizes and anchors for the act layout (normal difficulty).
// Sizes and offsets kept to what a level can be (1.14d's largest is 400
// tiles), so the layout's sums stay far from overflowing.
LevelDefs level_defs(const d2d::txt::Table& levels);

// Act 1's layout from the map seed: the act seed is `{map seed, 666}`
// stepped once (FUN_00642da0).
d2d::rules::Rng act_seed(std::uint32_t map_seed);
std::vector<Placed> act1_from_map_seed(const LevelDefs& defs, std::uint32_t map_seed);

// Two rects that touch on one axis and overlap on the other
// (FUN_0066b880 with overlap -1).
bool edge_shared(const Placed& first, const Placed& second);

// A level's Vis0..7 / Warp0..7 as its level record holds them once the act
// is made (FUN_0066c040's): Levels.txt's, and each chain link adds the pair
// both ways in the first free slot (vis 0, warp -1; FUN_006772c0 ->
// FUN_00642920). {vis, warp}. `neighbours_pass`: as FUN_00677680 reads it
// while it makes the act's outdoor neighbours (the Frigid Highlands not yet
// linked to the Bloody Foothills).
std::pair<std::array<int, 8>, std::array<int, 8>> level_vis(const d2d::txt::Table& levels, const std::vector<Placed>& layout, int id, bool neighbours_pass = false);

// A placed outdoor level's generator input: its Vis (level_vis); the
// walkable ones (warp -1) that were placed are its neighbours.
OutdoorLevel outdoor_level(const d2d::txt::Table& levels, const std::vector<Placed>& layout, int id);

// An act 3 level's input: its layout's rects and, for the jungle, its cells.
OutdoorLevel act3_outdoor_level(const d2d::txt::Table& levels, const Act3Layout& layout, int id);

}  // namespace d2d::drlg
