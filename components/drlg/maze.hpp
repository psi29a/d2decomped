// Maze levels (DrlgType 1, .\DRLG\Maze.cpp): act 1's caves, the Den of
// Evil first — rooms grown beside random rooms (FUN_00671210), special
// rooms placed from tables (FUN_00672550), each room a cave preset picked
// by which sides it links through (FUN_006709b0), then split into 8x8
// rooms (FUN_00673a60 -> FUN_00667ed0). docs/research/re/drlg.md "Maze levels".
#pragma once

#include <drlg.hpp>
#include <outdoor.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2d::drlg {

// LvlMaze.txt: rooms to grow by difficulty, room size, merge chance /1000.
struct MazeDef { std::array<int, 3> rooms{}; int w = 0, h = 0, merge = 0; };

namespace maze_detail {

// {def to replace, def to set, file (-1 = roll), side} (0x6ef8a8 + 0x40 k):
// the entrance, then the level's own (0x6ef8e8 Den of Evil, 0x6ef928 the
// rest), then Cave 2's and Cave 3's (levels 9, 10).
struct Special { int from, to, file, dir; };
inline constexpr std::array<std::array<Special, 4>, 5> kSpecials = { {
    { { { 60, 86, -1, 3 }, { 54, 84, -1, 0 }, { 56, 85, -1, 1 }, { 53, 83, -1, 2 } } },
    { { { 60, 98, -1, 3 }, { 54, 96, -1, 0 }, { 56, 97, -1, 1 }, { 53, 95, -1, 2 } } },
    { { { 60, 94, -1, 3 }, { 54, 92, -1, 0 }, { 56, 93, -1, 1 }, { 53, 91, -1, 2 } } },
    { { { 60, 102, -1, 3 }, { 54, 100, -1, 0 }, { 56, 101, -1, 1 }, { 53, 99, -1, 2 } } },
    { { { 60, 90, -1, 3 }, { 54, 88, -1, 0 }, { 56, 89, -1, 1 }, { 53, 87, -1, 2 } } } } };

struct Room {
    int x = 0, y = 0, w = 0, h = 0, def = 0, file = -1;
    bool special = false;                               // preset flags & 2
    d2d::rules::Rng seed;
    std::vector<std::pair<int, int>> links;             // {room, side}, newest first (FUN_0066b560)
};

}  // namespace maze_detail

// An act 1 cave (LevelType 3) from its seed: every 8x8 (or smaller preset)
// room in the order game.exe makes them, level-relative, kind 2 with its
// preset def, file and the preset's origin.
inline std::vector<Outdoor::RoomSeed> generate_maze(const OutdoorData& d, const MazeDef& m, int level, int level_w,
                                                   int level_h, d2d::rules::Rng seed, int difficulty,
                                                   std::vector<std::string>& notes) {
    using namespace maze_detail;
    std::vector<Room> rooms;
    std::vector<int> list;                              // the level's room list, newest first
    auto alloc = [&](int w, int h) {                    // FUN_0066b3e0
        seed.next();
        Room r;
        r.seed = d2d::rules::Rng{ seed.low };
        r.seed.next();
        r.w = w;
        r.h = h;
        rooms.push_back(r);
        return int(rooms.size()) - 1;
    };
    auto add = [&](int i) { list.insert(list.begin(), i); };                   // FUN_0066b970
    auto gap = [&](const Room& a, const Room& b) {
        return std::pair{ a.x < b.x ? b.x - a.w - a.x : a.x - b.w - b.x, a.y < b.y ? b.y - a.h - a.y : a.y - b.h - b.y };
    };
    // FUN_00670880: beside room `at` on `side` (0 left, 1 above, 2 right, 3
    // below), clear of every room but `at`.
    auto place = [&](int n, int at, int side) {
        auto& r = rooms[std::size_t(n)];
        const auto& a = rooms[std::size_t(at)];
        r.x = side == 0 ? a.x - a.w : side == 2 ? a.x + a.w : a.x;
        r.y = side == 1 ? a.y - a.h : side == 3 ? a.y + a.h : a.y;
        for (const int o : list) {
            if (o == at) continue;
            const auto [gx, gy] = gap(r, rooms[std::size_t(o)]);
            if (gx < 0 && gy < 0) return false;
        }
        return true;
    };
    auto link1 = [&](int a, int b, int side) {                                 // FUN_0066b560
        auto& l = rooms[std::size_t(a)].links;
        if (std::ranges::find(l, b, &std::pair<int, int>::first) == l.end()) l.insert(l.begin(), { b, side });
    };
    auto link = [&](int a, int b, int side) { link1(a, b, side); link1(b, a, (side - 2) & 3); };   // FUN_0066b5e0
    auto set_def = [&](int i) {                                                // FUN_006709b0(1)
        auto& r = rooms[std::size_t(i)];
        int mask = 0;
        for (const auto& [o, side] : r.links) mask |= side == 0 ? 1 : side == 1 ? 8 : side == 2 ? 2 : 4;
        r.def = 0x34 + mask;
        r.file = -1;
        r.special = false;
    };
    auto side_of = [&](const Room& a, const Room& b) {                         // FUN_00642240
        if (b.x < a.x) { if (a.x == b.x + b.w) return 0; }
        else if (b.x == a.x + a.w) return 2;
        if (b.y < a.y) { if (a.y == b.y + b.h) return 1; }
        else if (b.y == a.y + a.h) return 3;
        return -1;
    };

    // The first room, centred (FUN_00673b30).
    const int first = alloc(m.w, m.h);
    rooms[std::size_t(first)].x = (level_w - m.w) / 2;
    rooms[std::size_t(first)].y = (level_h - m.h) / 2;
    add(first);

    // Grow (FUN_00671210).
    const int want = m.rooms[std::size_t(std::clamp(difficulty, 0, 2))];
    while (int(list.size()) < want) {
        const int at = list[std::size_t(seed(int(list.size())))];            // FUN_006711a0
        const int side = int(rooms[std::size_t(at)].seed.next() & 3);
        if (rooms[std::size_t(at)].special) continue;
        const int n = alloc(m.w, m.h);
        if (!place(n, at, side)) continue;                                     // freed (FUN_0066c100)
        link(at, n, side);
        for (const int o : list) {                                             // FUN_00670c70: merge
            auto& other = rooms[std::size_t(o)];
            if (other.special) continue;
            const auto [gx, gy] = gap(rooms[std::size_t(n)], other);
            if (!(gx < 1 && gy < 1) || gx == gy) continue;
            const auto& nl = rooms[std::size_t(n)].links;
            if (std::ranges::find(nl, o, &std::pair<int, int>::first) != nl.end()) continue;
            if (int(other.seed.next() % 1000) >= m.merge) continue;
            const int s = side_of(rooms[std::size_t(n)], other);
            if (s == -1) continue;
            link(n, o, s);
            set_def(o);
        }
        add(n);
        set_def(n);
        set_def(at);
    }

    // Special rooms (FUN_00672550 -> FUN_006724e0 / FUN_00670eb0).
    {                                                   // LevelType 3 (act 1 caves)
        int k = int(seed.next() & 3);
        auto special = [&](const Special& s) {
            bool done = false;
            for (const int o : list) {
                auto& r = rooms[std::size_t(o)];
                if (!r.special && r.def == s.from) { r.def = s.to; r.file = s.file; r.special = true; done = true; break; }
            }
            for (std::size_t i = 0; !done && i < list.size(); ++i) {
                const int o = list[i];
                if (rooms[std::size_t(o)].special) continue;
                const int n = alloc(m.w, m.h);
                if (!place(n, o, s.dir)) continue;
                link(o, n, s.dir);
                add(n);
                set_def(o);
                auto& r = rooms[std::size_t(n)];
                r.special = true;
                r.def = s.to;
                r.file = s.file;
                done = true;
            }
            k = (k + 1) & 3;
        };
        special(kSpecials[0][std::size_t(k)]);
        special(kSpecials[level == 8 ? 1 : 2][std::size_t(k)]);
        if (level == 9) special(kSpecials[3][std::size_t(k)]);
        if (level == 10) special(kSpecials[4][std::size_t(k)]);
    }
    if (level != 8) notes.push_back("drlg: cave theme rooms (FUN_006735f0) not implemented");

    // Level-relative from the rooms' top-left (FUN_00642590).
    int mx = rooms[std::size_t(list.front())].x, my = rooms[std::size_t(list.front())].y;
    for (const int o : list) { mx = std::min(mx, rooms[std::size_t(o)].x); my = std::min(my, rooms[std::size_t(o)].y); }
    for (const int o : list) { rooms[std::size_t(o)].x -= mx; rooms[std::size_t(o)].y -= my; }

    // Each maze room becomes its preset's rooms (FUN_00673a60), list order.
    std::unordered_map<int, int> rotation;              // level +0x1cc
    std::vector<Outdoor::RoomSeed> out;
    for (const int o : std::vector<int>(list)) {
        const auto& r = rooms[std::size_t(o)];
        const auto it = d.presets.find(r.def);
        if (it == d.presets.end()) { notes.push_back("drlg: maze preset " + std::to_string(r.def) + " missing"); continue; }
        const auto& p = it->second;
        int file = seed(p.files);                        // FUN_00666ed0
        if (r.file != -1) file = r.file;                 // FUN_006738c0
        else if (r.def > 0x34 && r.def < 0x44) {
            auto rot = rotation.find(r.def);
            if (rot == rotation.end()) rot = rotation.emplace(r.def, seed(p.files)).first;
            file = rot->second = p.files ? (rot->second + 1) % p.files : 0;
        }
        const int pw = p.w && p.h ? p.w : r.w, ph = p.w && p.h ? p.h : r.h;
        if (pw < 13 && ph < 13) {                        // FUN_00667ed0: one room
            seed.next();
            d2d::rules::Rng rs{ seed.low };
            rs.next();
            out.push_back({ r.x, r.y, rs.low, pw, ph, 2, r.def, file, r.x, r.y });
            continue;
        }
        for (int ty = 0; ty < ph; ty += 8)               // else 8x8 rooms (FUN_00666680)
            for (int tx = 0; tx < pw; tx += 8) {
                seed.next();
                d2d::rules::Rng rs{ seed.low };
                rs.next();
                out.push_back({ r.x + tx, r.y + ty, rs.low, std::min(8, pw - tx), std::min(8, ph - ty), 2, r.def, file, r.x, r.y });
            }
    }
    return out;
}

}  // namespace d2d::drlg
