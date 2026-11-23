// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for tile_pick.hpp.
#include "tile_pick.hpp"

#include <rules.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace d2d::drlg {

std::vector<const Dt1File*> room_dt1_list(std::uint32_t mask, const RoomDt1s& dt1s) {
    std::vector<const Dt1File*> list;
    for (int bit = 0; bit < 32; ++bit)
        if ((mask >> bit & 1) && dt1s.by_bit[std::size_t(bit)]) list.push_back(dt1s.by_bit[std::size_t(bit)]);
    for (const auto* file : dt1s.always) if (file) list.push_back(file);
    return list;
}

std::vector<std::pair<const Dt1File*, int>> tile_candidates(const std::vector<const Dt1File*>& list, int orient, int style, int seq) {
    std::vector<std::pair<const Dt1File*, int>> candidates;
    for (const auto* file : list) {
        for (int i = int(file->tiles.size()) - 1; i >= 0 && candidates.size() < 40; --i) {
            const auto& tile = file->tiles[std::size_t(i)];
            if (tile.orient == orient && tile.style == style && tile.seq == seq) candidates.emplace_back(file, i);
        }
        if (candidates.size() >= 40) break;
    }
    return candidates;
}

std::pair<const Dt1File*, int> pick_tile(const std::vector<const Dt1File*>& list, d2d::rules::Rng& rng, int orient,
                                           std::uint32_t word) {
    const int style = word ? int((word >> 20) & 0x3f) : 0, seq = word ? int((word >> 8) & 0xff) : 0;
    auto candidates = tile_candidates(list, orient, style, seq);
    if (candidates.empty()) {
        candidates = tile_candidates(list, 10, 0, 0);
        return candidates.empty() ? std::pair<const Dt1File*, int>{ nullptr, -1 } : candidates.front();
    }
    int total = 0;
    for (auto [file, tile_index] : candidates) total += file->tiles[std::size_t(tile_index)].rarity;
    if (total < 1) return candidates.front();
    int roll = int(rng(total)) + 1;
    std::size_t index = 0;
    while (candidates.size() > 1 && roll > 0) { roll -= candidates[index].first->tiles[std::size_t(candidates[index].second)].rarity; ++index; }
    return candidates[index ? index - 1 : 0];
}

}  // namespace d2d::drlg
