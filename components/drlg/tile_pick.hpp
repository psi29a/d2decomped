// SPDX-License-Identifier: GPL-3.0-or-later
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
struct RoomTile { int layer, x, y, orient; const Dt1File* file; int index; std::uint32_t word = 0; };   // word: what flags it (FUN_0066db20)

// A room's DT1 list (FUN_0066f240): its mask's files in bit order, then the three.
std::vector<const Dt1File*> room_dt1_list(std::uint32_t mask, const RoomDt1s& dt1s);


// FUN_0066d820: up to 40 candidates with the orientation / style /
// sequence, DT1 by DT1 in list order, each DT1's in reverse file order
// (its hash bucket list is built by inserting at the front); the first
// whose running rarity reaches rand(total) + 1.
// The candidates for a key (FUN_00604ae0): up to 40, the room's DT1s in
// list order, each one's tiles in reverse file order.
std::vector<std::pair<const Dt1File*, int>> tile_candidates(const std::vector<const Dt1File*>& list, int orient, int style, int seq);

std::pair<const Dt1File*, int> pick_tile(const std::vector<const Dt1File*>& list, d2d::rules::Rng& rng, int orient,
                                           std::uint32_t word);


}  // namespace d2d::drlg
