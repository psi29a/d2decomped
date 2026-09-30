// Picking a room's DT1 tile (RoomTile.cpp, FUN_0066d820): the DT1s a
// room lists and the rarity pick on the room seed. docs/research/re/drlg.md
// "Room tiles".
#pragma once

#include <rules.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace d2d::drlg {

// A DT1's tile headers, what the pick reads (0x60-byte records from +0x110).
struct Dt1Head { int orient = 0, style = 0, seq = 0, rarity = 0, material = 0; };   // material: +6
struct Dt1File {
    std::string name;                                   // lower-case file name
    std::vector<Dt1Head> tiles;                         // file order
};
template <class Bytes> Dt1File dt1_heads(std::string name, const Bytes& bytes) {
    Dt1File file{ std::move(name), {} };
    auto read_i32 = [&](std::size_t offset) { std::int32_t value = 0; if (offset + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + offset, 4); return value; };
    const int count = read_i32(0x10c), off = read_i32(0x110);
    for (int i = 0; i < count; ++i) {
        const auto header = std::size_t(off) + std::size_t(i) * 0x60;
        file.tiles.push_back({ read_i32(header + 0x14), read_i32(header + 0x18), read_i32(header + 0x1c), read_i32(header + 0x20), (read_i32(header + 4) >> 16) & 0xffff });
    }
    return file;
}

// The DT1s a level's rooms can list: LvlTypes File1..32 of its type (index
// = mask bit), then Blank, InvisWal, Warp (always appended).
struct RoomDt1s {
    std::array<const Dt1File*, 32> by_bit{};
    std::array<const Dt1File*, 3> always{};
};

// One placed tile: layer 0 wall, 1 floor, 2 shadow; room-relative x, y; the
// orientation it was picked with; which DT1 of the room's list and which
// tile of that file (-1 = nothing matched).
struct RoomTile { int layer, x, y, orient; const Dt1File* file; int index; };

// A room's DT1 list (FUN_0066f240): its mask's files in bit order, then the three.
inline std::vector<const Dt1File*> room_dt1_list(std::uint32_t mask, const RoomDt1s& dt1s) {
    std::vector<const Dt1File*> list;
    for (int bit = 0; bit < 32; ++bit)
        if ((mask >> bit & 1) && dt1s.by_bit[std::size_t(bit)]) list.push_back(dt1s.by_bit[std::size_t(bit)]);
    for (const auto* file : dt1s.always) if (file) list.push_back(file);
    return list;
}


// FUN_0066d820: up to 40 candidates with the orientation / style /
// sequence, DT1 by DT1 in list order, each DT1's in reverse file order
// (its hash bucket list is built by inserting at the front); the first
// whose running rarity reaches rand(total) + 1.
inline std::pair<const Dt1File*, int> pick_tile(const std::vector<const Dt1File*>& list, d2d::rules::Rng& rng, int orient,
                                           std::uint32_t word) {
    const int style = word ? int((word >> 20) & 0x3f) : 0, seq = word ? int((word >> 8) & 0xff) : 0;
    std::vector<std::pair<const Dt1File*, int>> candidates;
    auto gather = [&](int want_orient, int want_style, int want_sequence) {
        for (const auto* file : list) {
            for (int i = int(file->tiles.size()) - 1; i >= 0 && candidates.size() < 40; --i) {
                const auto& tile = file->tiles[std::size_t(i)];
                if (tile.orient == want_orient && tile.style == want_style && tile.seq == want_sequence) candidates.emplace_back(file, i);
            }
            if (candidates.size() >= 40) break;
        }
    };
    gather(orient, style, seq);
    if (candidates.empty()) {
        gather(10, 0, 0);
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
