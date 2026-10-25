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

inline int to_int(std::string_view text, int fallback = 0) {
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

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

// Levels.txt row by Id.
inline std::optional<std::size_t> level_row(const d2d::txt::Table& levels, int id);

// Level `id`'s warp slots (Levels.txt Warp0..7) with their LvlWarp rows.
inline std::array<WarpSlot, 8> warp_slots(const OutdoorAssets& assets, int id) {
    std::array<WarpSlot, 8> slots{};
    const auto row = level_row(assets.levels, id);
    if (!row) return slots;
    for (int i = 0; i < 8; ++i) {
        const int warp = to_int(assets.levels.get(*row, "Warp" + std::to_string(i)), -1);
        auto& slot = slots[std::size_t(i)];
        for (std::size_t warp_row = 0; warp >= 0 && warp_row < assets.lvl_warp.size(); ++warp_row) {
            if (to_int(assets.lvl_warp.get(warp_row, "Id"), -1) != warp) continue;
            const int off_x = to_int(assets.lvl_warp.get(warp_row, "OffsetX")), off_y = to_int(assets.lvl_warp.get(warp_row, "OffsetY"));
            if (slot.id < 0) slot = { warp, to_int(assets.lvl_warp.get(warp_row, "LitVersion")) != 0, off_x, off_y, to_int(assets.lvl_warp.get(warp_row, "Tiles")), off_x, off_y };
            if (assets.lvl_warp.get(warp_row, "Direction") == "r") { slot.right_x = off_x; slot.right_y = off_y; }
        }
    }
    return slots;
}

// Levels.txt row by Id.
inline std::optional<std::size_t> level_row(const d2d::txt::Table& levels, int id) {
    for (std::size_t row = 0; row < levels.size(); ++row)
        if (to_int(levels.get(row, "Id"), -1) == id) return row;
    return std::nullopt;
}

// A level apart from the act layout: its OffsetX / Y, from its Depend
// level's when it has one (FUN_00642d10).
inline std::pair<int, int> level_origin(const d2d::txt::Table& levels, std::size_t row) {
    int x = to_int(levels.get(row, "OffsetX")), y = to_int(levels.get(row, "OffsetY"));
    if (const auto depend = level_row(levels, to_int(levels.get(row, "Depend"))); depend && to_int(levels.get(row, "Depend")) != 0) {
        const auto [depend_x, depend_y] = level_origin(levels, *depend);
        x += depend_x;
        y += depend_y;
    }
    return { x, y };
}

// Sizes and anchors for the act layout (normal difficulty).
// Sizes and offsets kept to what a level can be (1.14d's largest is 400
// tiles), so the layout's sums stay far from overflowing.
inline LevelDefs level_defs(const d2d::txt::Table& levels) {
    LevelDefs defs;
    const auto size = [&](std::size_t row, const char* column) { return std::clamp(to_int(levels.get(row, column)), 0, 0x1000); };
    const auto offset = [&](std::size_t row, const char* column) { return std::clamp(to_int(levels.get(row, column)), -0x10000, 0x10000); };
    for (std::size_t row = 0; row < levels.size(); ++row)
        defs[to_int(levels.get(row, "Id"), -1)] = { size(row, "SizeX"), size(row, "SizeY"), offset(row, "OffsetX"), offset(row, "OffsetY"),
                                                    to_int(levels.get(row, "DrlgType")) == 3 };
    return defs;
}

// Act 1's layout from the map seed: the act seed is `{map seed, 666}`
// stepped once (FUN_00642da0).
inline d2d::rules::Rng act_seed(std::uint32_t map_seed) {
    d2d::rules::Rng act{ map_seed };
    act.next();
    return act;
}
inline std::vector<Placed> act1_from_map_seed(const LevelDefs& defs, std::uint32_t map_seed) { return act1_layout(defs, act_seed(map_seed)); }

// Two rects that touch on one axis and overlap on the other
// (FUN_0066b880 with overlap -1).
inline bool edge_shared(const Placed& first, const Placed& second) {
    const int dx = second.x < first.x ? first.x - second.width - second.x : second.x - first.width - first.x;
    const int dy = second.y < first.y ? first.y - second.height - second.y : second.y - first.height - first.y;
    return (dx == 0 && dy < 0) || (dy == 0 && dx < 0);
}

// A placed outdoor level's generator input. Vis starts as Levels.txt's
// and each chain link adds the pair both ways in the first free slot
// (vis 0, warp -1; FUN_006772c0 -> FUN_00642920); the walkable ones
// (warp -1) that were placed are its neighbours.
inline OutdoorLevel outdoor_level(const d2d::txt::Table& levels, const std::vector<Placed>& layout, int id) {
    OutdoorLevel level;
    auto placed = [&](int level_id) -> const Placed* {
        for (const auto& placement : layout) if (placement.level == level_id) return &placement;
        return nullptr;
    };
    if (const auto* placement = placed(id)) level.rect = *placement;
    if (const auto* town = placed(level_ids::kActTowns[std::size_t(level_ids::act_of(id))])) level.town = *town;
    const auto row = level_row(levels, id);
    if (!row) return level;
    level.sub_type = to_int(levels.get(*row, "SubType"), -1);
    level.sub_theme = to_int(levels.get(*row, "SubTheme"), -1);
    level.sub_waypoint = to_int(levels.get(*row, "SubWaypoint"), -1);
    level.sub_shrine = to_int(levels.get(*row, "SubShrine"), -1);
    level.level_type = to_int(levels.get(*row, "LevelType"), 2);
    std::array<int, 8> vis{}, warp{};
    for (int i = 0; i < 8; ++i) {
        vis[std::size_t(i)] = to_int(levels.get(*row, "Vis" + std::to_string(i)));
        warp[std::size_t(i)] = to_int(levels.get(*row, "Warp" + std::to_string(i)), -1);
    }
    auto add = [&](int other) {
        for (int i = 0; i < 8; ++i) if (vis[std::size_t(i)] == other) { warp[std::size_t(i)] = -1; return; }
        for (int i = 0; i < 8; ++i)
            if (vis[std::size_t(i)] == 0 && warp[std::size_t(i)] == -1) { vis[std::size_t(i)] = other; return; }
    };
    for (const auto* chain : { &kAct1Outdoors, &kAct1Highlands, &kAct2Outdoors, &kAct2Canyon, &kAct4Outdoors, &kAct4Chaos })   // act 5's add none
        for (const auto& record : *chain) {
            if (record.link < 0) continue;
            const int other = (*chain)[std::size_t(record.link)].level;
            if (record.level == id) add(other);
            else if (other == id) add(record.level);
        }
    // Act 3's (FUN_006775c0): each of 75..83 whose rect shares an edge with
    // this one's (FUN_0066b880, overlap -1), in id order.
    // Act 5's (FUN_00678a70): the same over 111..112, 110..111, 109..110 in turn.
    auto share_edges = [&](int first, int last) {
        if (const auto* self = placed(id); self && id >= first && id <= last)
            for (int other = first; other <= last; ++other)
                if (const auto* rect = placed(other); rect && other != id && edge_shared(*self, *rect)) add(other);
    };
    share_edges(level_ids::kKurastDocks, level_ids::kTravincal);
    // FUN_00677680 makes 111's and 112's neighbours right after the first
    // pass, so the Bloody Foothills is never the Frigid Highlands' (its way
    // through is FUN_0067dcf0's own opening).
    share_edges(level_ids::kFrigidHighlands, level_ids::kArreatPlateau);
    if (id != level_ids::kFrigidHighlands) share_edges(level_ids::kBloodyFoothills, level_ids::kFrigidHighlands);
    share_edges(level_ids::kHarrogath, level_ids::kBloodyFoothills);
    for (int i = 0; i < 8; ++i)
        if (vis[std::size_t(i)] && warp[std::size_t(i)] == -1)
            if (const auto* placement = placed(vis[std::size_t(i)])) {
                const auto other = level_row(levels, vis[std::size_t(i)]);
                level.neighbours.push_back({ *placement, i, other && to_int(levels.get(*other, "DrlgType")) == 2 });
            }
    return level;
}

// An act 3 level's input: its layout's rects and, for the jungle, its cells.
inline OutdoorLevel act3_outdoor_level(const d2d::txt::Table& levels, const Act3Layout& layout, int id) {
    auto level = outdoor_level(levels, layout.levels, id);
    level.act_roll = layout.roll;
    if (id >= level_ids::kSpiderForest && id <= level_ids::kFlayerJungle) {
        const auto& cells = layout.jungle[std::size_t(id - level_ids::kSpiderForest)];
        level.jungle = cells.presets;
        level.jungle_specials = cells.specials;
    }
    return level;
}

}  // namespace d2d::drlg
