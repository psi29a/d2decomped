// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for outdoor_data.hpp.
#include "outdoor_data.hpp"

#include "drlg.hpp"
#include "outdoor.hpp"
#include "room_tiles.hpp"

#include <level_ids.hpp>
#include <rules.hpp>
#include <txt.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::drlg {

int to_int(std::string_view text, int fallback) {
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

std::optional<std::size_t> level_row(const d2d::txt::Table& levels, int id) {
    for (std::size_t row = 0; row < levels.size(); ++row)
        if (to_int(levels.get(row, "Id"), -1) == id) return row;
    return std::nullopt;
}

std::array<WarpSlot, 8> warp_slots(const OutdoorAssets& assets, int id) {
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

std::pair<int, int> level_origin(const d2d::txt::Table& levels, std::size_t row) {
    int x = to_int(levels.get(row, "OffsetX")), y = to_int(levels.get(row, "OffsetY"));
    if (const auto depend = level_row(levels, to_int(levels.get(row, "Depend"))); depend && to_int(levels.get(row, "Depend")) != 0) {
        const auto [depend_x, depend_y] = level_origin(levels, *depend);
        x += depend_x;
        y += depend_y;
    }
    return { x, y };
}

LevelDefs level_defs(const d2d::txt::Table& levels) {
    LevelDefs defs;
    const auto size = [&](std::size_t row, const char* column) { return std::clamp(to_int(levels.get(row, column)), 0, 0x1000); };
    const auto offset = [&](std::size_t row, const char* column) { return std::clamp(to_int(levels.get(row, column)), -0x10000, 0x10000); };
    for (std::size_t row = 0; row < levels.size(); ++row)
        defs[to_int(levels.get(row, "Id"), -1)] = { size(row, "SizeX"), size(row, "SizeY"), offset(row, "OffsetX"), offset(row, "OffsetY"),
                                                    to_int(levels.get(row, "DrlgType")) == 3 };
    return defs;
}

d2d::rules::Rng act_seed(std::uint32_t map_seed) {
    d2d::rules::Rng act{ map_seed };
    act.next();
    return act;
}

std::vector<Placed> act1_from_map_seed(const LevelDefs& defs, std::uint32_t map_seed) { return act1_layout(defs, act_seed(map_seed)); }

bool edge_shared(const Placed& first, const Placed& second) {
    const int dx = second.x < first.x ? first.x - second.width - second.x : second.x - first.width - first.x;
    const int dy = second.y < first.y ? first.y - second.height - second.y : second.y - first.height - first.y;
    return (dx == 0 && dy < 0) || (dy == 0 && dx < 0);
}

std::pair<std::array<int, 8>, std::array<int, 8>> level_vis(const d2d::txt::Table& levels, const std::vector<Placed>& layout, int id) {
    auto placed = [&](int level_id) -> const Placed* {
        for (const auto& placement : layout) if (placement.level == level_id) return &placement;
        return nullptr;
    };
    std::array<int, 8> vis{}, warp{};
    warp.fill(-1);
    const auto row = level_row(levels, id);
    if (!row) return { vis, warp };
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
    return { vis, warp };
}

OutdoorLevel outdoor_level(const d2d::txt::Table& levels, const std::vector<Placed>& layout, int id) {
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
    const auto [vis, warp] = level_vis(levels, layout, id);
    for (int i = 0; i < 8; ++i)
        if (vis[std::size_t(i)] && warp[std::size_t(i)] == -1)
            if (const auto* placement = placed(vis[std::size_t(i)])) {
                const auto other = level_row(levels, vis[std::size_t(i)]);
                level.neighbours.push_back({ *placement, i, other && to_int(levels.get(*other, "DrlgType")) == 2 });
            }
    return level;
}

OutdoorLevel act3_outdoor_level(const d2d::txt::Table& levels, const Act3Layout& layout, int id) {
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
