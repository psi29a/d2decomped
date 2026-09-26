// Picking a room's DT1 tile (RoomTile.cpp, FUN_0066d820): the DT1s a
// room lists and the rarity pick on the room seed. docs/research/re/drlg.md
// "Room tiles".
#pragma once

#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace d2d::drlg {

// A DT1's tile headers, what the pick reads (0x60-byte records from +0x110).
struct Dt1Head { int orient = 0, style = 0, seq = 0, rarity = 0; };
struct Dt1File {
    std::string name;                                   // lower-case file name
    std::vector<Dt1Head> tiles;                         // file order
};
template <class Bytes> Dt1File dt1_heads(std::string name, const Bytes& b) {
    Dt1File f{ std::move(name), {} };
    auto rd = [&](std::size_t o) { std::int32_t v = 0; if (o + 4 <= b.size()) std::memcpy(&v, b.data() + o, 4); return v; };
    const int n = rd(0x10c), off = rd(0x110);
    for (int i = 0; i < n; ++i) {
        const auto h = std::size_t(off) + std::size_t(i) * 0x60;
        f.tiles.push_back({ rd(h + 0x14), rd(h + 0x18), rd(h + 0x1c), rd(h + 0x20) });
    }
    return f;
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
inline std::vector<const Dt1File*> room_dt1_list(std::uint32_t mask, const RoomDt1s& d) {
    std::vector<const Dt1File*> list;
    for (int b = 0; b < 32; ++b)
        if ((mask >> b & 1) && d.by_bit[std::size_t(b)]) list.push_back(d.by_bit[std::size_t(b)]);
    for (const auto* f : d.always) if (f) list.push_back(f);
    return list;
}


// FUN_0066d820: up to 40 candidates with the orientation / style /
// sequence, DT1 by DT1 in list order, each DT1's in reverse file order
// (its hash bucket list is built by inserting at the front); the first
// whose running rarity reaches rand(total) + 1.
inline std::pair<const Dt1File*, int> pick_tile(const std::vector<const Dt1File*>& list, d2d::rules::Rng& s, int orient,
                                           std::uint32_t word) {
    const int style = word ? int((word >> 20) & 0x3f) : 0, seq = word ? int((word >> 8) & 0xff) : 0;
    std::vector<std::pair<const Dt1File*, int>> c;
    auto gather = [&](int o, int st, int sq) {
        for (const auto* f : list) {
            for (int i = int(f->tiles.size()) - 1; i >= 0 && c.size() < 40; --i) {
                const auto& t = f->tiles[std::size_t(i)];
                if (t.orient == o && t.style == st && t.seq == sq) c.emplace_back(f, i);
            }
            if (c.size() >= 40) break;
        }
    };
    gather(orient, style, seq);
    if (c.empty()) {
        gather(10, 0, 0);
        return c.empty() ? std::pair<const Dt1File*, int>{ nullptr, -1 } : c.front();
    }
    int total = 0;
    for (auto [f, i] : c) total += f->tiles[std::size_t(i)].rarity;
    if (total < 1) return c.front();
    int r = int(s(total)) + 1;
    std::size_t k = 0;
    while (c.size() > 1 && r > 0) { r -= c[k].first->tiles[std::size_t(c[k].second)].rarity; ++k; }
    return c[k ? k - 1 : 0];
}


}  // namespace d2d::drlg
