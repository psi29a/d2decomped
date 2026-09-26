// Outdoor levels (DrlgType 3): act 1's wilderness generator — the cell
// grid of presets, borders, roads and fills (FUN_00675360 -> FUN_006807f0
// -> FUN_006750f0) — and the tiles of its rooms (OutRoom.cpp,
// FUN_0067d2d0). docs/research/re/drlg.md "Outdoor levels".
#pragma once

#include <drlg.hpp>
#include <ds1.hpp>
#include <tile_pick.hpp>
#include <units.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2d::drlg {

// LvlPrest.txt, what the generator needs: size in tiles, file count, the
// loaded File1..6 (null when absent), and the columns that roll units.
struct Preset {
    int w = 0, h = 0, files = 0, scan = 0, pops = 0;
    std::uint32_t dt1_mask = 0;                         // which LvlTypes files its rooms load
    std::array<const d2d::ds1::Map*, 6> maps{};
};
// LvlSub.txt, one row: the stamp sheet and its odds per theme.
struct Sub {
    int type = 0, check_all = 0, bord_type = 0;
    std::uint32_t dt1_mask = 0;
    std::array<int, 5> prob{}, trials{}, max{};
    const d2d::ds1::Map* map = nullptr;
};
struct OutdoorData {
    std::unordered_map<int, Preset> presets;           // by Def
    std::vector<Sub> subs;                              // file order
    const RoomDt1s* dt1s = nullptr;                     // set: stamps pick their shadow tiles (FUN_0066e060)
    UnitIds ids;                                        // how DS1 units' ids map
};
// A walkable neighbour: its placed rect and its slot in this level's Vis.
struct Neighbour { Placed rect; int slot = 0; };
struct OutdoorLevel {
    Placed rect;                                        // level, tiles, layout flags
    int sub_type = -1, sub_theme = -1, sub_waypoint = -1, sub_shrine = -1;
    std::vector<Neighbour> neighbours;                  // Vis slot order
    Placed town;                                        // the Den goes far from it
};
// A plain room's DS1 words ((8+1)x(8+1), row-major) as game.exe keeps
// them at room data +0x00 / +0x14 / +0x28 (orientation, wall, floor),
// plus stamped shadows; its seed low word and DT1 mask (room +0x50).
struct PlainRoom {
    int x, y;                                           // level-relative tiles
    std::uint32_t flags;                                // +0x18 value
    std::uint32_t seed_low;                             // room +4
    std::uint32_t theme_mask;                           // FUN_006706a0
    std::uint32_t dt1_mask = 0x44103;                   // LevelType 2's files, | chosen LvlSub rows'
    std::array<std::uint32_t, 81> orient{}, wall{}, floor{}, shadow{};
    d2d::rules::Rng seed;                               // the room seed after its init; the tile picks go on from here
    std::vector<RoomTile> tiles;                        // shadows the stamps picked (with OutdoorData::dt1s)
    std::vector<Unit> units;                            // the stamps' objects, room-relative subtiles, newest first
};
struct Outdoor {
    int cw = 0, ch = 0;                                 // cells (8x8 tiles)
    std::uint32_t flags = 0;                            // outdoor flags after generation (layout | 0x20, 0x40)
    std::vector<std::uint32_t> g04, g18, g2c;           // preset def, values, flags
    std::vector<std::vector<std::pair<int, int>>> roads; // polylines, act tiles
    d2d::ds1::Map tiles;                                // the level, level-relative
    // Every room the finish allocated (8x8 tiles, presets split the same
    // way), level-relative tiles, with its seed: monsters populate these.
    struct RoomSeed {
        int x = 0, y = 0;
        std::uint32_t seed = 0;
        int w = 8, h = 8, kind = 1;                     // kind 1 plain, 2 preset
        int def = 0, file = 0, px = 0, py = 0;          // a preset room's LvlPrest def, file and the preset's origin
    };
    std::vector<RoomSeed> rooms;
    std::vector<PlainRoom> plain;                       // plain rooms' words, cell order
    std::vector<std::string> notes;                     // what isn't wired up yet
};

// A level's seed: {act seed's low word (after its first step) + id, 666}.
inline d2d::rules::Rng level_seed(std::uint32_t map_seed, int level) {
    d2d::rules::Rng act{ map_seed };
    act.next();
    return d2d::rules::Rng{ act.low + std::uint32_t(level) };
}

namespace outdoor_detail {

// Border preset by (1-based row, style column): row 1..4 edges (kind
// left/up/right/down), 5..12 corners; column 0 cliffs, 1 fences (0x6f0620).
inline constexpr std::array<std::array<int, 2>, 13> kBorder = { {
    { 0, 0 }, { 0, 4 }, { 16, 5 }, { 17, 6 }, { 0, 7 }, { 18, 8 }, { 19, 9 }, { 22, 10 }, { 0, 11 },
    { 0, 12 }, { 23, 13 }, { 0, 14 }, { 0, 15 } } };
// Corner row by index e (-40..40), 0x6f1088; 0 = none.
inline int corner_row(int e) {
    static const std::unordered_map<int, int> t = {
        { -40, 1 }, { -39, 9 }, { -38, 9 }, { -35, 1 }, { -34, 8 }, { -31, 12 }, { -26, 12 }, { -25, 4 },
        { -22, 5 }, { -21, 2 }, { -20, 2 }, { -19, 10 }, { -14, 10 }, { -13, 1 }, { -12, 9 }, { -11, 9 },
        { 11, 11 }, { 12, 11 }, { 13, 3 }, { 14, 12 }, { 19, 12 }, { 20, 4 }, { 21, 4 }, { 22, 7 },
        { 25, 2 }, { 26, 10 }, { 31, 10 }, { 34, 6 }, { 35, 3 }, { 38, 11 }, { 39, 11 }, { 40, 3 } };
    const auto it = t.find(e);
    return it == t.end() ? 0 : it->second;
}
// River Upper / Lower file by the border def already in the cell (0x6f2680).
inline constexpr std::array<std::array<int, 2>, 16> kRiver = { {
    { 0, 0 }, { 0, 6 }, { 0x182, 0 }, { 0, 8 }, { 2, 2 }, { 0, 3 }, { 1, 1 }, { 3, 0 },
    { 0, 2 }, { 0, 1 }, { 1, 0 }, { 2, 0 }, { 2, 3 }, { 1, 3 }, { 3, 1 }, { 3, 2 } } };
// Road floor sequence by the 8-neighbour mask (0x6f2700).
inline constexpr std::array<std::uint8_t, 256> kRoad = {
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 40, 20,
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 40, 20,
    13, 13, 7, 7, 13, 13, 13, 7, 4, 4, 11, 37, 4, 4, 11, 43, 3, 3, 12, 12, 3, 3, 39, 39, 9, 9, 2, 43, 9, 9, 44, 26,
    13, 13, 7, 7, 13, 13, 13, 7, 23, 23, 41, 17, 23, 23, 41, 17, 3, 3, 12, 12, 3, 3, 39, 39, 42, 42, 46, 42, 42, 42, 33, 31,
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 35, 20,
    0, 0, 16, 16, 0, 0, 16, 16, 14, 14, 6, 19, 14, 14, 6, 19, 15, 15, 5, 5, 15, 15, 21, 21, 8, 8, 10, 38, 8, 8, 40, 20,
    13, 13, 7, 7, 13, 13, 13, 7, 4, 4, 11, 37, 4, 4, 11, 37, 18, 18, 35, 35, 18, 18, 22, 22, 36, 36, 45, 34, 36, 36, 28, 29,
    13, 13, 7, 7, 13, 13, 13, 7, 23, 23, 41, 17, 23, 23, 41, 17, 18, 18, 35, 35, 18, 18, 22, 22, 24, 24, 25, 32, 24, 24, 30, 1 };
// Goal direction (0..7) by 5·cdx + cdy + 12 (0x6f1518).
inline constexpr std::array<int, 25> kDirVal = { 5, 4, 4, 4, 3, 6, 5, 4, 3, 2, 6, 6, 6, 2, 2,
                                                 6, 7, 0, 1, 2, 7, 0, 0, 0, 1 };
// Path search turn rows (0x6f2840) and steps (0x6f2854 / 0x6f2850).
inline constexpr std::array<int, 17> kTurn = { 0, 1, 2, 3, 0, 1, 1, 1, 3, 2, 1, 2, 0, 3, 2, 1, 0 };
inline constexpr std::array<int, 4> kStepX = { 1, 0, -1, 0 }, kStepY = { 0, 1, 0, -1 };

inline int sgn(int v) { return (v > 0) - (v < 0); }
inline int sx2(int t) { return t + 2 * sgn(t); }

// The game's cell grid: writes clip, reads index linearly like game.exe
// does (x one past a row reads the next row's first cell; off the end 0).
struct Cells {
    int w = 0, h = 0;
    std::vector<std::uint32_t> v;
    Cells(int w_, int h_) : w(w_), h(h_), v(std::size_t(w_) * std::size_t(h_), 0) {}
    [[nodiscard]] bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    [[nodiscard]] std::uint32_t get(int x, int y) const {
        const long i = long(y) * w + x;
        return i >= 0 && i < long(v.size()) ? v[std::size_t(i)] : 0;
    }
    // The operator table at 0x749bac.
    void op(int x, int y, std::uint32_t val, int o) {
        if (!in(x, y)) return;
        auto& c = v[std::size_t(y) * std::size_t(w) + std::size_t(x)];
        switch (o) {
        case 0: c |= val; break;
        case 1: c &= val; break;
        case 2: c ^= val; break;
        case 3: c = val; break;
        case 4: if (!c) c = val; break;
        case 5: c &= ~val; break;
        }
    }
};

struct Vert { int x = 0, y = 0; int style = 0; std::uint32_t flags = 0; int next = -1; };

struct Gen {
    const OutdoorData& d;
    const OutdoorLevel& L;
    d2d::rules::Rng seed;
    int cw, ch;
    Cells g04, g18, g2c;
    std::uint32_t flags;
    std::vector<Vert> verts;
    int head = 0;
    struct Node { Placed r; int side; bool town; int slot; };
    std::vector<Node> nodes;                            // +0x264, sorted
    std::unordered_map<int, int> rotation;              // per def, FUN_00674320
    std::vector<std::string>& notes;

    Gen(const OutdoorData& d_, const OutdoorLevel& l, d2d::rules::Rng s, std::vector<std::string>& n)
        : d(d_), L(l), seed(s), cw(l.rect.w / 8), ch(l.rect.h / 8), g04(cw, ch), g18(cw, ch), g2c(cw, ch),
          flags(l.rect.flags), notes(n) {}

    void note(std::string s) {
        if (std::ranges::find(notes, s) == notes.end()) notes.push_back(std::move(s));
    }
    [[nodiscard]] const Preset* preset(int def) const {
        const auto it = d.presets.find(def);
        return it == d.presets.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::pair<int, int> cells_of(int def) const {
        if (def == 0) return { 1, 1 };
        const auto* p = preset(def);
        return p ? std::pair{ p->w / 8, p->h / 8 } : std::pair{ 0, 0 };
    }

    // ---- presets on the cell grid (FUN_006743c0 / 00674230 / 00674320)
    int rotate(int def) {
        const auto* p = preset(def);
        if (!p || p->files < 1) return 0;
        auto it = rotation.find(def);
        if (it == rotation.end()) it = rotation.emplace(def, seed(p->files)).first;
        return it->second = (it->second + 1) % p->files;
    }
    void place(int x, int y, int def, int file, bool border) {
        const auto [sw, sh] = cells_of(def);
        if (file == -1) file = rotate(def);
        for (int cy = y; cy < y + sh; ++cy)
            for (int cx = x; cx < x + sw; ++cx) {
                g2c.op(cx, cy, 0xf0000, 5);
                g2c.op(cx, cy, std::uint32_t(file) << 16 | 0x200, 0);
                if (border && def > 3 && def < 16) g2c.op(cx, cy, 1, 0);
                g04.op(cx, cy, 0, 3);
            }
        g04.op(x, y, std::uint32_t(def), 3);
    }
    [[nodiscard]] bool free_cell(int x, int y) const { return (g2c.get(x, y) & 0x1b81) == 0; }
    [[nodiscard]] bool fits(int x, int y, int def, int m, int mask) const {
        auto [sw, sh] = cells_of(def);
        if (m) {
            if (mask & 1) { y -= m; sh += m; }
            if (mask & 2) sw += m;
            if (mask & 4) sh += m;
            if (mask & 8) { x -= m; sw += m; }
        }
        for (int cy = y; cy < y + sh; ++cy)
            for (int cx = x; cx < x + sw; ++cx)
                if (!g2c.in(cx, cy) || !free_cell(cx, cy)) return false;
        return true;
    }
    // n entries (i % w, i / w), then n swaps of two rolls.
    std::vector<std::pair<int, int>> shuffled(int w, int h) {
        const int n = w * h;
        std::vector<std::pair<int, int>> l;
        for (int i = 0; i < n; ++i) l.emplace_back(i % w, i / w);
        for (int k = 0; k < n; ++k) {
            const int a = seed(n), b = seed(n);
            std::swap(l[std::size_t(a)], l[std::size_t(b)]);
        }
        return l;
    }
    bool anywhere(int def, int file, int m, int mask) {           // FUN_00674730
        for (auto [x, y] : shuffled(cw - 2, ch - 2))
            if (fits(x + 1, y + 1, def, m, mask)) { place(x + 1, y + 1, def, file, false); return true; }
        return false;
    }
    bool by_road(int def, int file) {                              // FUN_00674920
        static constexpr std::array<int, 8> ox = { -1, 0, 0, 1, -1, 1, 1, -1 }, oy = { 0, -1, 1, 0, -1, 1, -1, 1 };
        for (auto [x, y] : shuffled(cw - 2, ch - 2))
            if (g2c.get(x + 1, y + 1) & 0x80)
                for (int k = 0; k < 8; ++k)
                    if (fits(x + 1 + ox[std::size_t(k)], y + 1 + oy[std::size_t(k)], def, 0, 0xf)) {
                        place(x + 1 + ox[std::size_t(k)], y + 1 + oy[std::size_t(k)], def, file, false);
                        return true;
                    }
        return anywhere(def, file, 0, 0xf);
    }
    bool farthest(const Placed& r, int def, int file, int m, int mask) {   // FUN_006744f0
        const int sx = seed(cw - 2), sy = seed(ch - 2);
        int best = 0, bx = -1, by = -1;
        for (int i = 0; i <= ch - 2; ++i) {
            const int y = (i + sy) % (ch - 2) + 1;
            for (int j = 0; j <= cw - 2; ++j) {
                const int x = (j + sx) % (cw - 2) + 1;
                if (!fits(x, y, def, m, mask)) continue;
                const int dx = std::abs(x * 8 - (r.w / 2 + r.x) + 4 + L.rect.x);
                const int dy = std::abs(y * 8 - (r.h / 2 + r.y) + 4 + L.rect.y);
                const int dd = (dy < dx ? dy + 2 * dx : dx + 2 * dy) / 2;
                if (best < dd) { best = dd; bx = x; by = y; }
            }
        }
        if (bx == -1) return false;
        place(bx, by, def, file, false);
        return true;
    }

    // ---- outline (FUN_00677680, FUN_0067d050, FUN_0067ce20, FUN_00675080)
    void neighbours() {
        const Placed& a = L.rect;
        for (const auto& n : L.neighbours) {
            const Placed& b = n.rect;
            int side = -1;                                          // FUN_00642240
            if (b.x < a.x) { if (a.x == b.x + b.w) side = 0; }
            else if (b.x == a.x + a.w) side = 2;
            if (side < 0) {
                if (b.y < a.y) { if (a.y == b.y + b.h) side = 1; }
                else if (b.y == a.y + a.h) side = 3;
            }
            if (side < 0) { note("drlg: neighbour " + std::to_string(b.level) + " doesn't touch"); continue; }
            const Node nn{ b, side, b.level == 1, n.slot };
            auto before = [](const Node& p, const Node& q) {        // FUN_0066b6a0
                if (p.side != q.side) return p.side < q.side;
                switch (p.side) {
                case 0: return p.r.y < q.r.y;
                case 1: return q.r.x < p.r.x;
                case 2: return q.r.y < p.r.y;
                default: return p.r.x < q.r.x;
                }
            };
            // FUN_0066b720: the head is only compared while it's alone.
            if (nodes.empty()) nodes.push_back(nn);
            else if (nodes.size() == 1) nodes.insert(before(nn, nodes[0]) ? nodes.begin() : nodes.end(), nn);
            else {
                auto it = nodes.begin() + 1;
                while (it != nodes.end() && !before(nn, *it)) ++it;
                nodes.insert(it, nn);
            }
        }
    }
    int add_vert(int after, int x, int y) {
        verts.push_back({ x, y, 0, 0, verts[std::size_t(after)].next });
        verts[std::size_t(after)].next = int(verts.size()) - 1;
        return int(verts.size()) - 1;
    }
    void outline() {
        const int x = L.rect.x, y = L.rect.y, w = L.rect.w - 1, h = L.rect.h - 1;
        verts = { { x, y + h, 0, 0, 1 }, { x, y, 0, 0, 2 }, { x + w, y, 0, 0, 3 }, { x + w, y + h, 0, 0, 0 } };
        head = 0;
        for (const auto& n : nodes) {
            const int nx = n.r.x, ny = n.r.y, nw = n.r.w - 1, nh = n.r.h - 1;
            int lo, hi, c, s, e, corner;
            bool vertical;
            switch (n.side) {
            case 0: lo = ny; hi = ny + nh; c = -1; s = verts[0].y; e = verts[1].y; corner = 0; vertical = true; break;
            case 1: hi = nx; lo = nx + nw; c = 1; s = verts[1].x; e = verts[2].x; corner = 1; vertical = false; break;
            case 2: hi = ny; lo = ny + nh; c = 1; s = verts[2].y; e = verts[3].y; corner = 2; vertical = true; break;
            default: lo = nx; hi = nx + nw; c = -1; s = verts[3].x; e = verts[0].x; corner = 3; vertical = false; break;
            }
            // In c-space the edge runs s -> e; the neighbour spans A (its
            // near end, `hi`) to B (`lo`).
            auto at = [&](int after, int coord) {
                const auto& v = verts[std::size_t(after)];
                return vertical ? add_vert(after, v.x, coord) : add_vert(after, coord, v.y);
            };
            auto mark = [&](int v) { verts[std::size_t(v)].flags |= 1 | (n.town ? 2u : 0u); };
            const int A = hi * c, B = lo * c;
            int p = corner;
            if (A <= s * c) {
                if (s * c <= B) {
                    mark(p);
                    if (B < e * c) at(p, lo);
                }
            } else if (A <= e * c) {
                p = at(p, hi);
                mark(p);
                if (B < e * c) at(p, lo);
            }
        }
        // Level-relative cells, equal neighbours merged.
        int v = head;
        do {
            auto& t = verts[std::size_t(v)];
            t.x = (t.x - L.rect.x) / 8;
            t.y = (t.y - L.rect.y) / 8;
            v = t.next;
        } while (v != head);
        v = head;
        do {
            auto& t = verts[std::size_t(v)];
            const int n = t.next;
            const auto& u = verts[std::size_t(n)];
            if (t.x == u.x && t.y == u.y && n != v) {
                if (n == head) head = v;
                t.next = u.next;
                t.flags |= u.flags;
                t.style = u.style;
            }
            v = verts[std::size_t(v)].next;
        } while (v != head);
    }

    // ---- contact spans and borders (FUN_00675770, FUN_00675850)
    [[nodiscard]] std::uint32_t contact_bit(const Vert& v) const {         // FUN_00674040
        int side;
        if (v.x == 0) side = v.y == 0 ? 1 : 0;
        else if (v.y == 0) side = v.x == cw - 1 ? 2 : 1;
        else if (v.x == cw - 1) side = v.y == ch - 1 ? 3 : 2;
        else if (v.y == ch - 1) side = 3;
        else return 0;
        static constexpr std::array<int, 4> px = { -4, 4, 12, 4 }, py = { 4, -4, 4, 12 };
        const int X = L.rect.x + v.x * 8 + px[std::size_t(side)], Y = L.rect.y + v.y * 8 + py[std::size_t(side)];
        for (const auto& n : nodes)
            if (n.side == side && X >= n.r.x && X < n.r.x + n.r.w && Y >= n.r.y && Y < n.r.y + n.r.h)
                return 1u << (n.slot + 4);
        return 0;
    }
    // FUN_0067c760 walks the cells strictly between v and n, then writes v,
    // then n (its last argument, 1 here): both ends inclusive either way.
    void span(const Vert& v, const Vert& n, Cells& g, std::uint32_t val) {
        for (int y = std::min(v.y, n.y); y <= std::max(v.y, n.y); ++y)
            for (int x = std::min(v.x, n.x); x <= std::max(v.x, n.x); ++x) g.op(x, y, val, 0);
    }
    void contacts() {
        int v = head;
        do {
            const auto& t = verts[std::size_t(v)];
            if (t.flags & 1) {
                const auto& n = verts[std::size_t(t.next)];
                span(t, n, g18, contact_bit(t));
                span(t, n, g2c, t.style ? 3 : 1);
            }
            v = t.next;
        } while (v != head);
    }
    void borders() {
        int v = head;
        do {
            const auto& a = verts[std::size_t(v)];
            const auto& b = verts[std::size_t(a.next)];
            const auto& c = verts[std::size_t(b.next)];
            const int dx1 = sgn(b.x - a.x), dy1 = sgn(b.y - a.y), dx2 = sgn(c.x - b.x), dy2 = sgn(c.y - b.y);
            const int len = dx1 == 0 ? std::abs(a.y - b.y) : std::abs(a.x - b.x);
            std::uint32_t bits = a.style ? 3 : 1;
            // Edge: kind left 0, up 1, right 2, down 3 (0x6f0fd0).
            const int kind = dx1 == -1 ? 0 : dy1 == -1 ? 1 : dx1 == 1 ? 2 : 3;
            const int edge = kBorder[std::size_t(kind + 1)][a.style == 0 ? 1 : 0];
            if (!(a.flags & 2)) {
                int x = a.x, y = a.y;
                while (x != b.x || y != b.y) {
                    y += dy1;
                    x += dx1;
                    place(x, y, edge, -1, false);
                    g2c.op(x, y, bits, 0);
                }
            }
            if ((a.flags & 1) && !(a.flags & 2)) {                             // act 1: the opening
                const int ex = std::min(a.x, b.x) + len * std::abs(dx1) / 2;
                const int ey = std::min(a.y, b.y) + len * std::abs(dy1) / 2;
                g2c.op(ex, ey, 0xf0000, 5);
                g2c.op(ex, ey, L.rect.level == 17 ? 0x40400 : 0x30400, 0);
            }
            const int style = a.style ? a.style : b.style;
            if (style) bits |= 2;
            int e;
            if (!(a.flags & 2)) e = !(b.flags & 2) ? sx2(2 * dx1) + 9 * (sx2(2 * dx2) + 2 * dy2) + 2 * dy1
                                                   : sx2(2 * dx1) + 9 * (sx2(dx2) + dy2) + 2 * dy1;
            else e = !(b.flags & 2) ? sx2(dx1) + 9 * (sx2(2 * dx2) + 2 * dy2) + dy1
                                    : sx2(dx1) + 9 * (sx2(dx2) + dy2) + dy1;
            if (const int row = corner_row(e)) {
                int def = kBorder[std::size_t(row)][style == 0 ? 1 : 0];
                if (def == 19) def = a.style == 1 ? 19 + (b.style != 1) : 21;
                if (def) {
                    place(b.x, b.y, def, -1, false);
                    g2c.op(b.x, b.y, bits, 0);
                }
            }
            v = a.next;
        } while (v != head);
        // ponytail: FUN_00675670 (blanking outside a non-rectangular
        // outline) skipped — every level here is a rectangle.
    }

    // ---- border substitution (FUN_006752a0 -> FUN_00670750)
    static std::uint32_t word(const d2d::ds1::Map& m, const std::vector<d2d::ds1::Layer>& ls, int x, int y) {
        if (ls.empty() || x < 0 || y < 0 || x >= m.width() || y >= m.height()) return 0;
        return d2d::ds1::Map::word(ls[0].cells[std::size_t(y) * std::size_t(m.width()) + std::size_t(x)]);
    }
    bool sub_match(const d2d::ds1::Map& m, const d2d::ds1::Group& g, int x, int y) {   // FUN_0066f3b0
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx) {
                const auto w = word(m, m.walls(), g.x + gx, g.y + gy), f = word(m, m.floors(), g.x + gx, g.y + gy);
                const int cx = x + gx, cy = y + gy;
                if (w & 1) {
                    const int s = int((w >> 8) & 0xff) - 1;
                    if (s != 62 && std::uint32_t(4 + s) != g04.get(cx, cy)) return false;
                    if (g2c.get(cx, cy) & 0x400) return false;
                } else if (f & 2) {
                    if (!g2c.in(cx, cy) || !free_cell(cx, cy)) return false;
                }
            }
        return true;
    }
    void sub_apply(const d2d::ds1::Map& m, const d2d::ds1::Group& g, int x, int y, int xoff) {   // FUN_0066f520
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx) {
                const auto w = word(m, m.walls(), g.x + xoff + gx, g.y + gy);
                const auto f = word(m, m.floors(), g.x + xoff + gx, g.y + gy);
                const int cx = x + gx, cy = y + gy;
                if (w & 1) {
                    const int s = int((w >> 8) & 0xff) - 1;
                    if (4 + s != -5 && s != 62) place(cx, cy, 4 + s, 0, true);
                } else if (f & 2) {
                    g04.op(cx, cy, 0, 3);
                    g2c.op(cx, cy, 0, 3);
                } else {
                    g04.op(cx, cy, 0, 3);
                    g2c.op(cx, cy, 0x100, 3);
                }
            }
    }
    bool sub_group(const Sub& s, const d2d::ds1::Group& g, int type) {       // FUN_0066f690
        const bool act1 = L.rect.level >= 2 && L.rect.level <= 7;
        const int m = type == 1 && (flags & 0xc) ? -1 : 1;
        const int xc = cw - g.w + m, yc = ch - g.h + 1;
        const int n = xc * yc;
        if (n <= 0) return false;
        const bool skip22 = type == 1 && act1 && xc <= 5 && yc <= 5;
        for (auto [x, y] : shuffled(xc, yc)) {
            if (skip22 && x == 2 && y == 2) continue;
            if (!sub_match(*s.map, g, x, y)) continue;
            const int k = seed(g.variants);
            sub_apply(*s.map, g, x, y, (g.w + 1) * (k + 1));
            if (s.bord_type == 0 || s.bord_type == 1) return true;
        }
        return false;
    }
    void border_subs(int type) {
        for (const auto& s : d.subs) {
            if (s.type != type || !s.map) continue;
            const auto& gs = s.map->groups();
            const int n = int(gs.size());
            if (n == 0) continue;
            const int start = s.bord_type == 0 ? seed(n) : 0;
            for (int i = 0; i < n; ++i)
                if (sub_group(s, gs[std::size_t((start + i) % n)], type) && s.bord_type == 0) break;
        }
    }

    // ---- river, entrances, transitions (FUN_00680200, FUN_006803d0)
    [[nodiscard]] bool river_ok(int x) const {                                 // FUN_0067fc70
        for (int y = 0; y < ch; ++y)
            if ((g2c.get(x, y) & 2) || (g2c.get(x + 1, y) & 2)) return false;
        return true;
    }
    void river(int x) {                                                        // FUN_0067fe90
        auto file = [&](int cx, int cy, int half) {
            const auto f = g2c.get(cx, cy), def = g04.get(cx, cy);
            if (def == 0) return (f & 0x100) ? 0 : 3;
            if (def == 7 && (f & 0xf0000) == 0x30000) return 3;
            return def < kRiver.size() ? kRiver[def][std::size_t(half)] : 0;
        };
        for (int y = 0; y < ch; ++y) {
            place(x, y, 26, file(x, y, 0), false);
            place(x + 1, y, 27, file(x + 1, y, 1), false);
        }
        if (!(flags & 0x14)) return;
        const int n = ch - 2;                                                  // FUN_0067fd20
        const int r = seed(n);
        for (int i = 0; i < n; ++i) {
            const int y = (i + r) % n + 1;
            if (!(flags & 4) && !free_cell(x - 1, y)) continue;
            if (!free_cell(x + 2, y)) continue;
            if ((g2c.get(x, y) & 0xf0000) != 0x30000 || (g2c.get(x + 1, y) & 0xf0000) != 0x30000) continue;
            place(x, y, 28, 1, false);
            place(x + 1, y, 28, 2 + ((flags & 4) ? 1 : 0), false);
            return;
        }
    }
    void features() {
        if ((flags & 0xc) && river_ok(cw - 2)) river(cw - 2);
        if ((flags & 0x20) && !(flags & 0x40)) note("drlg: cliff cave entrance (flag 0x20) not implemented");
        if ((flags & 0x1c) && !(flags & 0x40)) {
            const int r = int(seed.next() & 3);
            const int x = (r & 1) ? 3 : cw - ((flags & 0x10) ? 4 : 5);
            const int y = (r >> 1) ? 3 : ch - 4;
            place(x, y, 51 + (L.rect.level == 2), -1, false);
            flags |= 0x40;
        }
    }
    void transitions() {
        if ((flags & 0x10) && river_ok(cw / 2 - 1)) river(cw / 2 - 1);
        if (flags & 0x80) place(0, 0, 3, 1, false);
        if (flags & 0x100) place(cw - 7, 0, 3, 2, false);
        if (flags & 0x200) place(0, 1, 2, 1, false);
        if (flags & 0x400) place(0, ch - 6, 2, 1, false);
        if (!(flags & 0x40)) {
            const bool ok = L.rect.level == 2 ? farthest(L.town, 52, -1, 1, 0xf) : anywhere(51, -1, 1, 0xf);
            if (!ok) note("drlg: no room for the level's cave entrance");
            flags |= 0x40;
        }
    }

    // ---- roads (FUN_00681420)
    struct End { int x = 0, y = 0, dir = 4; };
    std::array<End, 6> A{}, B{}, C{}, D{};
    int ends = 0;
    [[nodiscard]] End snap(End p) const {                                      // FUN_00680cc0
        int rx = p.x - L.rect.x, ry = p.y - L.rect.y;
        switch (p.dir) {
        case 0: rx = rx / 8 * 8 + 11; break;
        case 1: ry = ry / 8 * 8 + 11; break;
        case 2: rx = rx / 8 * 8 - 5; break;
        case 3: ry = ry / 8 * 8 - 5; break;
        default: break;
        }
        return { rx + L.rect.x, ry + L.rect.y, p.dir };
    }
    void road_ends() {                                                         // FUN_00680d70
        auto add = [&](End e) { if (ends < 6) A[std::size_t(ends)] = e; ++ends; };
        for (const auto& n : nodes) {
            const auto& T = n.r;
            if (T.level == 1) {
                static constexpr std::array<std::pair<int, int>, 4> at = { { { 59, 19 }, { 29, 35 }, { 4, 22 }, { 29, 3 } } };
                add({ T.x + at[std::size_t(n.side)].first, T.y + at[std::size_t(n.side)].second, n.side });
            } else if (T.level == 26) add({ T.x + 27, T.y + 13, 1 });
        }
        for (int x = 0; x < cw; ++x)
            for (int y = 0; y < ch; ++y) {
                const auto def = g04.get(x, y);
                const auto f = (g2c.get(x, y) >> 16) & 0xf;
                int dir = 4;
                switch (def) {
                case 4: if (f == 3) dir = 3; break;
                case 5: if (f == 3) dir = 0; break;
                case 6: if (f == 3) dir = 1; break;
                case 7: if (f == 3) dir = 2; break;
                case 24: dir = 1; break;
                case 25: dir = 0; break;
                case 28: if (f == 1 && x == cw - 2) dir = 2; break;
                case 51: case 52: dir = f != 0; break;
                default: break;
                }
                if (dir != 4) add({ L.rect.x + 3 + x * 8, L.rect.y + 3 + y * 8, dir });
            }
        if (ends > 6) { note("drlg: more than 6 road ends"); ends = 6; }
        for (int i = 0; i < ends; ++i) B[std::size_t(i)] = snap(A[std::size_t(i)]);
    }
    void road_hub() {                                                          // FUN_00681000
        if (flags & 0x10) {
            for (int y = 0; y < ch; ++y)
                for (int x = 1; x < cw - 1; ++x)
                    if (g04.get(x, y) == 28 && ((g2c.get(x, y) >> 16) & 0xf) == 1) {
                        const int hx = L.rect.x + 3 + x * 8;
                        for (int i = 0; i < ends; ++i) {
                            auto& e = D[std::size_t(i)];
                            e.y = L.rect.y + 3 + y * 8;
                            if (hx < A[std::size_t(i)].x) { e.x = hx + 8; e.dir = 0; }
                            else { e.x = hx; e.dir = 2; }
                            C[std::size_t(i)] = snap(e);
                        }
                        return;
                    }
        }
        int cx, cy;
        if (ends == 1) { cx = cw / 2; cy = ch / 2; }
        else {
            int sx = 0, sy = 0;
            for (int i = 0; i < ends; ++i) { sx += A[std::size_t(i)].x - L.rect.x; sy += A[std::size_t(i)].y - L.rect.y; }
            cx = ends ? sx / (ends * 8) : 0;
            cy = ends ? sy / (ends * 8) : 0;
        }
        static constexpr std::array<int, 4> rx = { -1, 0, 0, 1 }, ry = { 0, 1, -1, 0 };
        int hx = cx, hy = cy;
        bool found = false;
        for (int r = 0; r < 8 && !found; ++r)
            for (int k = 0; k < 4; ++k) {
                hx = rx[std::size_t(k)] * r + cx;
                hy = ry[std::size_t(k)] * r + cy;
                if (g2c.in(hx, hy) && free_cell(hx, hy)) { found = true; break; }
            }
        for (int i = 0; i < ends; ++i) {
            D[std::size_t(i)] = { L.rect.x + 3 + hx * 8, L.rect.y + 3 + hy * 8, 4 };
            C[std::size_t(i)] = snap(D[std::size_t(i)]);
        }
    }
    static int dirval(int x, int y, int gx, int gy) {                           // FUN_00678cf0
        const int dx = gx - x, dy = gy - y, ax = std::abs(dx), ay = std::abs(dy);
        auto squash = [](int v) { return v < 0 ? -1 : (v & 1); };
        int cx = std::clamp(dx, -2, 2), cy = std::clamp(dy, -2, 2);
        if (ax >= 2 * ay) cy = squash(dy);
        else if (ay >= 2 * ax) cx = squash(dx);
        return kDirVal[std::size_t(5 * cx + cy + 12)];
    }
    // Cells start -> goal, goal first; empty when there's no way.
    std::vector<std::pair<int, int>> search(int sx, int sy, int gx, int gy) {   // FUN_006817d0 / 00681630
        auto hdist = [](int ax, int ay) { ax = std::abs(ax); ay = std::abs(ay); return std::min(ax, ay) + 2 * std::max(ax, ay); };
        if (std::abs(sx - gx) + std::abs(sy - gy) < 2) return { { sx, sy }, { gx, gy } };
        struct N { int f, h, g, x, y, tries, p, dir, parent, child; };
        std::vector<N> pool(900);
        const int h0 = hdist(sx - gx, sy - gy);
        int bound = h0 / 2 + h0;
        const int limit = bound + 35;
        do {
            int count = 1;
            pool[0] = { h0, h0, 0, sx, sy, -1, 0, (dirval(sx, sy, gx, gy) / 2) & 3, -1, -1 };
            auto advance = [&](int i) -> int {                                  // FUN_00681560
                auto& n = pool[std::size_t(i)];
                if (n.tries < 4) { ++n.p; n.dir = (n.dir + kTurn[std::size_t(n.p)]) & 3; }
                if (++n.tries != 3) return i;
                while (true) {
                    if (i == 0) return -1;
                    i = pool[std::size_t(i)].parent;
                    auto& q = pool[std::size_t(i)];
                    ++q.p;
                    q.dir = (q.dir + kTurn[std::size_t(q.p)]) & 3;
                    if (++q.tries != 3) return i;
                }
            };
            int found = -1, cur = 0;
            while (cur >= 0) {
                const auto& n = pool[std::size_t(cur)];
                if (n.x == gx && n.y == gy) { found = cur; break; }
                const int nx = n.x + kStepX[std::size_t(n.dir)], ny = n.y + kStepY[std::size_t(n.dir)];
                bool ok = nx == gx && ny == gy;
                if (!ok && g2c.in(nx, ny) && !(g2c.get(nx, ny) & 0x200)) {
                    ok = true;
                    for (int a = cur; a >= 0; a = pool[std::size_t(a)].parent)
                        if (pool[std::size_t(a)].x == nx && pool[std::size_t(a)].y == ny) { ok = false; break; }
                }
                if (!ok) { cur = advance(cur); continue; }
                const int g2 = n.g + 2, h = hdist(nx - gx, ny - gy), f = h + g2;
                if (f > bound) { cur = advance(cur); continue; }
                if (n.child < 0) {                             // a reused child keeps its own
                    if (count == 900) { cur = -1; break; }
                    pool[std::size_t(count)] = { 0, 0, 0, 0, 0, 0, 0, 0, cur, -1 };
                    pool[std::size_t(cur)].child = count++;
                }
                const int ci = pool[std::size_t(cur)].child;
                auto& c = pool[std::size_t(ci)];
                c.parent = cur;
                c.h = h; c.f = f; c.g = g2; c.tries = 0;
                const int dd = dirval(nx, ny, gx, gy) / 2;
                c.p = ((pool[std::size_t(cur)].dir - dd) & 3) * 4;
                c.dir = (kTurn[std::size_t(c.p)] + dd) & 3;
                c.x = nx; c.y = ny;
                cur = ci;
            }
            bound += 5;
            if (count > 899) return {};
            if (found >= 0) {
                std::vector<std::pair<int, int>> path;
                for (int a = found; a >= 0; a = pool[std::size_t(a)].parent) path.emplace_back(pool[std::size_t(a)].x, pool[std::size_t(a)].y);
                return path;
            }
        } while (bound < limit);
        return {};
    }
    std::vector<std::vector<std::pair<int, int>>> roads() {
        road_ends();
        road_hub();
        std::vector<std::vector<std::pair<int, int>>> out;
        for (int i = 0; i < ends; ++i) {
            const auto& b = B[std::size_t(i)];
            const auto& c = C[std::size_t(i)];
            auto path = search((b.x - L.rect.x) / 8, (b.y - L.rect.y) / 8, (c.x - L.rect.x) / 8, (c.y - L.rect.y) / 8);
            if (path.empty()) { note("drlg: a road found no way"); continue; }
            for (auto [x, y] : path) if (g2c.in(x, y)) g2c.op(x, y, 0x80, 0);
            // FUN_00681240: goal := C, start := B, the rest jittered, A
            // after B, D before C unless it's the plain hub.
            static constexpr std::array<int, 4> jx = { 1, 0, -1, 0 }, jy = { 0, 1, 0, -1 };
            int k = int(seed.next() & 3);
            std::vector<std::pair<int, int>> line;
            if (D[std::size_t(i)].dir != 4) line.emplace_back(D[std::size_t(i)].x, D[std::size_t(i)].y);
            line.emplace_back(c.x, c.y);
            for (std::size_t j = 1; j + 1 < path.size(); ++j) {
                const auto r1 = seed.next();
                const auto r2 = seed.next();
                line.emplace_back(L.rect.x + path[j].first * 8 + 3 + int((r1 & 1) + 2) * jx[std::size_t(k)],
                                  L.rect.y + path[j].second * 8 + 3 + int((r2 & 1) + 2) * jy[std::size_t(k)]);
                k = (k + 1) & 3;
            }
            line.emplace_back(b.x, b.y);
            line.emplace_back(A[std::size_t(i)].x, A[std::size_t(i)].y);
            out.push_back(std::move(line));
        }
        return out;
    }

    // ---- shrines, waypoints, fills (FUN_00674e40, FUN_00674b70, FUN_00680580)
    void shrines(int count) {
        int k = int(seed.next() & 3);
        for (auto [x, y] : shuffled(cw - 2, ch - 2)) {
            if (count < 1) return;
            if (!free_cell(x + 1, y + 1)) continue;
            g18.op(x + 1, y + 1, 0x1000u << k, 0);
            g2c.op(x + 1, y + 1, 0x1000, 0);
            k = (k + 1) & 3;
            --count;
        }
    }
    void fills() {
        if (L.rect.level != 2) { note("drlg: fills for level " + std::to_string(L.rect.level) + " not implemented"); return; }
        by_road(46, -1);                                                        // pond
        const auto r = seed.next();                                             // FUN_006804e0(0, 47)
        by_road(47, -1);
        if ((r & 3) == 0) by_road(47, -1);
        anywhere(29, -1, 0, 0xf);
        anywhere(30, -1, 0, 0xf);
    }
};

// ---- a plain room's tiles (FUN_0067d2d0)
using Room = PlainRoom;

inline void stamp_room(Room& r, const OutdoorData& d, const OutdoorLevel& L, d2d::rules::Rng& s,
                       int type, int theme, std::uint32_t mask, std::vector<std::string>& notes) {
    if (type < 0 || theme < 0 || theme > 4) return;
    std::size_t first = 0;
    while (first < d.subs.size() && d.subs[first].type != type) ++first;
    for (std::size_t i = 0; mask; ++i, mask >>= 1) {
        if (!(mask & 1) || first + i >= d.subs.size() || d.subs[first + i].type != type) continue;
        const Sub& sub = d.subs[first + i];
        if (!sub.map) continue;
        if (sub.check_all) {
            const std::string n = "drlg: LvlSub CheckAll stamps not implemented";
            if (std::ranges::find(notes, n) == notes.end()) notes.push_back(n);
            continue;
        }
        const auto& m = *sub.map;
        const auto& gs = m.groups();
        auto src = [&](const std::vector<d2d::ds1::Layer>& ls, int x, int y) { return Gen::word(m, ls, x, y); };
        auto orient0 = [&](int x, int y) -> std::uint32_t {
            if (m.walls().empty() || x < 0 || y < 0 || x >= m.width() || y >= m.height()) return 0;
            const auto& t = m.walls()[0].cells[std::size_t(y) * std::size_t(m.width()) + std::size_t(x)];
            return std::uint32_t(t.wall_type) | t.wall_zero << 8;
        };
        auto fit = [&](const d2d::ds1::Group& g, int x, int y) {                // FUN_0066fcf0
            for (int gy = 0; gy < g.h; ++gy)
                for (int gx = 0; gx < g.w; ++gx) {
                    const auto f = src(m.floors(), g.x + gx, g.y + gy), w = src(m.walls(), g.x + gx, g.y + gy);
                    if (!(f & 2) && !(!m.walls().empty() && (w & 1))) continue;
                    const auto i = std::size_t((y + gy) * 9 + x + gx);
                    if ((r.floor[i] & 0x3f0ff00) || !(r.floor[i] & 2) || (r.wall[i] & 1)) return false;
                }
            return true;
        };
        auto stamp = [&](const d2d::ds1::Group& g, int x, int y) {              // FUN_0066fad0
            for (int gy = 0; gy < g.h; ++gy)
                for (int gx = 0; gx < g.w; ++gx) {
                    const auto i = std::size_t((y + gy) * 9 + x + gx);
                    const auto f = src(m.floors(), g.x + gx, g.y + gy);
                    if (f & 2) r.floor[i] = f | 0x80;
                    const auto w = src(m.walls(), g.x + gx, g.y + gy);
                    if (w & 1) r.wall[i] = w;
                    if (const auto o = orient0(g.x + gx, g.y + gy)) r.orient[i] = o;
                    const auto sh = src(m.shadows(), g.x + gx, g.y + gy);
                    if (!(sh & 0x8000000)) continue;
                    if (d.dt1s) {                                                // FUN_0066e060: picked now, on the room seed
                        const auto [f, k] = pick_tile(room_dt1_list(r.dt1_mask, *d.dt1s), s, 13, sh);
                        r.tiles.push_back({ 2, x + gx, y + gy, 13, f, k });
                    } else {
                        r.shadow[i] = sh;
                    }
                }
            for (const auto& u : ds1_units(m, d.ids)) {                          // FUN_0066fa10: the group's objects
                const int gx = g.x * 5, gy = g.y * 5;
                if (gx < u.x && gy < u.y && u.x < gx + g.w * 5 && u.y < gy + g.h * 5)
                    r.units.insert(r.units.begin(), { u.type, u.id, u.mode, u.x - gx + x * 5, u.y - gy + y * 5, u.flags });
            }
        };
        const int max = sub.max[std::size_t(theme)];
        if (gs.empty() || max < 1) continue;
        for (int t = 0; t < max; ++t) {                                          // FUN_00670170
            const auto& g = gs[std::size_t(s(int(gs.size())))];
            const int sx = 8 - g.w, sy = 8 - g.h;
            if (sx < 1 || sy < 1) continue;
            const int trials = sub.trials[std::size_t(theme)];
            if (trials == -1) {
                const int n = sx * sy;
                std::vector<std::pair<int, int>> l;
                for (int k = 0; k < n; ++k) l.emplace_back(k % sx, k / sx);
                for (int k = 0; k < n; ++k) {
                    const int a = s(n), b = s(n);
                    std::swap(l[std::size_t(a)], l[std::size_t(b)]);
                }
                for (auto [x, y] : l)
                    if (fit(g, x + 1, y + 1)) { stamp(g, x + 1, y + 1); break; }
            } else {
                for (int k = 0; k < trials; ++k) {
                    const int x = s(sx) + 1, y = s(sy) + 1;
                    if (fit(g, x, y)) { stamp(g, x, y); break; }
                }
            }
        }
    }
    (void)L;
}

inline void room_tiles(Room& r, const OutdoorData& d, const OutdoorLevel& L,
                       const std::vector<std::vector<std::pair<int, int>>>& roads, std::vector<std::string>& notes) {
    d2d::rules::Rng s{ r.seed_low };                    // FUN_0066ee40
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) r.floor[std::size_t(y * 9 + x)] = 0x40002;
    // Roads (FUN_00680c80): a mask over the room grown by one.
    std::array<std::uint8_t, 121> mask{};
    const int ox = L.rect.x + r.x - 1, oy = L.rect.y + r.y - 1;
    auto dot = [&](int x, int y) {
        x -= ox; y -= oy;
        if (x >= 0 && y >= 0 && x < 11 && y < 11) mask[std::size_t(y * 11 + x)] = 1;
    };
    for (const auto& line : roads)
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {                     // FUN_0067c8e0
            int x = line[i].first, y = line[i].second;
            const int dx = std::abs(line[i + 1].first - x), dy = std::abs(line[i + 1].second - y);
            const int sx = line[i + 1].first < x ? -1 : 1, sy = line[i + 1].second < y ? -1 : 1;
            int err = 0;
            if (dx < dy) {
                for (int k = 0; k < 2; ++k) dot(x + k, y);
                for (int n = 0; n < dy; ++n) {
                    y += sy;
                    err += dx;
                    if (err > dy) { x += sx; err -= dy; }
                    for (int k = 0; k < 2; ++k) dot(x + k, y);
                }
            } else {
                for (int k = 0; k < 2; ++k) dot(x, y + k);
                for (int n = 0; n < dx; ++n) {
                    x += sx;
                    err += dy;
                    if (err > dx) { y += sy; err -= dx; }
                    for (int k = 0; k < 2; ++k) dot(x, y + k);
                }
            }
        }
    auto mk = [&](int x, int y) { return mask[std::size_t(y * 11 + x)] != 0; };
    for (int x = 1; x <= 9; ++x)
        for (int y = 9; y >= 1; --y) {
            if (!mk(x, y)) continue;
            const int idx = mk(x - 1, y + 1) | mk(x - 1, y) << 1 | mk(x - 1, y - 1) << 2 | mk(x, y + 1) << 3
                          | mk(x, y - 1) << 4 | mk(x + 1, y + 1) << 5 | mk(x + 1, y) << 6 | mk(x + 1, y - 1) << 7;
            if (idx && kRoad[std::size_t(idx)])
                r.floor[std::size_t((y - 1) * 9 + x - 1)] = std::uint32_t(kRoad[std::size_t(idx)]) << 8 | 0x82;
        }
    stamp_room(r, d, L, s, L.sub_waypoint, 0, (r.flags >> 16) & 3, notes);
    stamp_room(r, d, L, s, L.sub_shrine, 0, (r.flags >> 12) & 0xf, notes);
    stamp_room(r, d, L, s, L.sub_type, L.sub_theme, r.theme_mask, notes);
    r.seed = s;
    // FUN_0067c600 on the wall and floor grids: every edge cell | 4 (its tile
    // may be shared with the room next to it, FUN_0066e940).
    for (int i = 0; i < 9; ++i)
        for (const int k : { i, 72 + i, i * 9, i * 9 + 8 }) { r.wall[std::size_t(k)] |= 4; r.floor[std::size_t(k)] |= 4; }
}

}  // namespace outdoor_detail

// The act 1 wilderness level `L` from its seed. The tiles come out
// level-relative: 4 wall layers, 2 floors, 1 shadow.
// ponytail: plain-room tiles take a DT1 variant at draw time (first
// match), not game.exe's rarity pick on the room seed (FUN_0066d820).
inline Outdoor generate_outdoor(const OutdoorData& d, const OutdoorLevel& L, d2d::rules::Rng seed) {
    using namespace outdoor_detail;
    Outdoor out;
    Gen g(d, L, seed, out.notes);
    if (L.rect.level < 2 || L.rect.level > 7) g.note("drlg: outdoor level " + std::to_string(L.rect.level) + " is act 1 only for now");
    if (L.rect.level != 2 && L.rect.level != 3 && L.rect.level != 17) g.note("drlg: cliff styles (FUN_00680070) not implemented");
    g.neighbours();
    g.outline();
    g.contacts();
    g.borders();
    g.border_subs(0);
    g.features();
    g.border_subs(1);
    g.border_subs(2);
    g.transitions();
    g.border_subs(3);
    out.roads = g.roads();
    if (L.rect.level >= 3 && L.rect.level <= 6) g.note("drlg: waypoint placement (FUN_00674b70) not implemented");
    g.shrines(5);
    g.fills();

    // The finish (FUN_006750f0): rooms in cell order, each allocation
    // stepping the level seed.
    const int W = L.rect.w, H = L.rect.h;
    out.tiles = d2d::ds1::Map(W, H, 4, 2);
    auto alloc = [&] {                                  // FUN_0066b3e0
        g.seed.next();
        d2d::rules::Rng r{ g.seed.low };
        r.next();
        return r;
    };
    auto put = [&](int layer_kind, int layer, int x, int y, std::uint32_t w, std::uint32_t orient) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        auto& ls = layer_kind == 0 ? out.tiles.floors() : layer_kind == 1 ? out.tiles.walls() : out.tiles.shadows();
        if (std::size_t(layer) >= ls.size()) return;
        auto t = d2d::ds1::Map::tile(w);
        t.wall_type = std::uint8_t(orient & 0xff);
        t.wall_zero = orient >> 8;
        ls[std::size_t(layer)].cells[std::size_t(y) * std::size_t(W) + std::size_t(x)] = t;
    };
    std::vector<Room> rooms;
    for (int cy = 0; cy < g.ch; ++cy)
        for (int cx = 0; cx < g.cw; ++cx) {
            const auto f = g.g2c.get(cx, cy);
            if (f & 0x200) {
                const int def = int(g.g04.get(cx, cy));
                if (!def) continue;
                const auto* p = g.preset(def);
                if (!p) { g.note("drlg: LvlPrest def " + std::to_string(def) + " missing"); continue; }
                (void)g.seed(p->files);                   // FUN_00666ed0: rolled, then replaced
                const int file = int((f >> 16) & 0xf);
                if (p->scan || p->pops) g.note("drlg: preset units (FUN_00667620: the preset DS1s' monsters and objects) not placed");
                for (int ty = 0; ty < p->h; ty += 8)
                    for (int tx = 0; tx < p->w; tx += 8)
                        out.rooms.push_back({ cx * 8 + tx, cy * 8 + ty, alloc().low, 8, 8, 2, def, int((f >> 16) & 0xf), cx * 8, cy * 8 });
                const auto* m = file < 6 ? p->maps[std::size_t(file)] : nullptr;
                if (!m) { g.note("drlg: preset " + std::to_string(def) + " file " + std::to_string(file) + " not loaded"); continue; }
                const int ox = cx * 8, oy = cy * 8;
                for (int y = 0; y < p->h && y < m->height() && oy + y < H; ++y)
                    for (int x = 0; x < p->w && x < m->width() && ox + x < W; ++x) {
                        const auto i = std::size_t(y) * std::size_t(m->width()) + std::size_t(x);
                        for (std::size_t l = 0; l < m->floors().size(); ++l)
                            out.tiles.floors()[std::min<std::size_t>(l, 1)].cells[std::size_t(oy + y) * std::size_t(W) + std::size_t(ox + x)]
                                = m->floors()[l].cells[i];
                        for (std::size_t l = 0; l < m->walls().size() && l < 4; ++l)
                            out.tiles.walls()[l].cells[std::size_t(oy + y) * std::size_t(W) + std::size_t(ox + x)] = m->walls()[l].cells[i];
                        if (!m->shadows().empty())
                            out.tiles.shadows()[0].cells[std::size_t(oy + y) * std::size_t(W) + std::size_t(ox + x)] = m->shadows()[0].cells[i];
                    }
            } else if (!(f & 0x100)) {                    // FUN_0067d540
                auto r = alloc();
                out.rooms.push_back({ cx * 8, cy * 8, r.low });
                Room room{ cx * 8, cy * 8, g.g18.get(cx, cy), r.low, 0, 0x44103, {}, {}, {}, {}, {}, {} };
                if (L.sub_type != -1 && L.sub_theme != -1) {                     // FUN_006706a0
                    std::uint32_t bit = 0;
                    for (const auto& sub : d.subs) {
                        if (sub.type != L.sub_type) { if (bit) break; continue; }
                        if (int(r.next() % 100) < sub.prob[std::size_t(L.sub_theme)]) { room.theme_mask |= 1u << bit; room.dt1_mask |= sub.dt1_mask; }
                        ++bit;
                    }
                }
                rooms.push_back(room);
            }
        }
    for (auto& room : rooms) {
        room_tiles(room, d, L, out.roads, out.notes);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const auto i = std::size_t(y * 9 + x);
                put(0, 0, room.x + x, room.y + y, room.floor[i], 0);
                if (room.wall[i] & 1) put(1, 0, room.x + x, room.y + y, room.wall[i], room.orient[i]);
                if (room.shadow[i]) put(2, 0, room.x + x, room.y + y, room.shadow[i], 13);
            }
    }
    out.plain = std::move(rooms);
    out.cw = g.cw;
    out.flags = g.flags;
    out.ch = g.ch;
    out.g04 = g.g04.v;
    out.g18 = g.g18.v;
    out.g2c = g.g2c.v;
    return out;
}

}  // namespace d2d::drlg
