// Room tiles (RoomTile.cpp): which DT1 tile each cell of a room gets when
// game.exe brings the room up (FUN_0061b730) — the walk over its DS1 words
// (plain rooms FUN_0067d710), the per-word rules (FUN_0066e9b0), the rarity
// pick on the room seed (tile_pick.hpp) and edge tiles shared with the
// rooms next to it (FUN_0066e940). docs/research/re/drlg.md "Room tiles".
//
// Edge sharing makes the result depend on which rooms are already up, so
// this brings them all up in game.exe's room-list order (the reverse of
// the order they were made in); in the game it's whatever order the player
// walks them in.
#pragma once

#include <outdoor.hpp>
#include <tile_pick.hpp>
#include <units.hpp>

#include <map>

#include <array>
#include <string>
#include <vector>

namespace d2d::drlg {

// A level's warp slot (Levels.txt Warp0..7): its LvlWarp row — Id, lit
// (LitVersion), and the warp unit's offset from its tile (OffsetX / Y).
struct WarpSlot { int id = -1; bool lit = false; int off_x = 0, off_y = 0; };

// A tile a room holds: level-relative x, y; the word it came from.
struct PlacedTile {
    int layer, x, y, orient;                            // layer 0 wall, 1 floor, 2 shadow
    const Dt1File* file;
    int index;
    std::uint32_t word;
};
struct BuiltRoom {
    int x, y, w = 8, h = 8, kind = 1;                   // level-relative tiles
    std::vector<PlacedTile> tiles;                      // in the order game.exe adds them
    // Edge tiles other rooms can find (FUN_0066e4c0): chains of tile
    // indices, newest first, chains newest first; floor chains vs the rest.
    struct Chain { bool floor; std::vector<std::size_t> tiles; };
    std::vector<Chain> chains;
    // Hidden warp tiles (orientation 10/11, style = the level's warp slot):
    // level-relative cell and slot — where the warp unit stands (FUN_0066e1c0).
    struct Warp { int x, y, slot; };
    std::vector<Warp> warps;
    // Units for the server (room +0x5c): room-relative subtiles, newest first.
    std::vector<Unit> units;
    bool up = false;
    const PlainRoom* plain = nullptr;
    const Outdoor::RoomSeed* seed = nullptr;
};

namespace room_tiles_detail {

inline constexpr std::array<int, 20> kOrientClass = { -1, 0, 1, 2, -1, 3, 4, 5, -2, -2, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1 };   // 0x6ef620
inline constexpr std::array<int, 42> kOrientMerge = { 0, 1, 3, 3, 4, 1, 3, 1, 1, 2, 3, 4, 3, 2, 2, 3, 3, 3, 4, 3, 3,              // 0x6ef574
                                                      3, 1, 3, 3, 4, 5, 6, 1, 3, 2, 3, 4, 3, 6, 2, 1, 2, 3, 4, 1, 2 };

// FUN_0066bc20 + FUN_0066bbc0: the level's rooms (list order) within a
// 6-tile gap on both axes, itself included, then bubble-sorted so a room
// wholly left of or above the one before it moves ahead.
inline std::vector<std::size_t> near_rooms(const std::vector<BuiltRoom>& rooms, std::size_t self) {
    const auto& a = rooms[self];
    std::vector<std::size_t> n;
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        const auto& b = rooms[i];
        const int gx = a.x < b.x ? b.x - a.w - a.x : a.x - b.w - b.x;
        const int gy = a.y < b.y ? b.y - a.h - a.y : a.y - b.h - b.y;
        if (gx < 6 && gy < 6) n.push_back(i);
    }
    for (std::size_t k = n.size() ? n.size() - 1 : 0; k > 0; --k)
        for (std::size_t i = 0; i + 1 < n.size(); ++i) {
            const auto &p = rooms[n[i]], &q = rooms[n[i + 1]];
            if (q.x + q.w <= p.x || q.y + q.h <= p.y) std::swap(n[i], n[i + 1]);
        }
    return n;
}

}  // namespace room_tiles_detail

// Every room of a level with its tiles; `made` in the order game.exe made
// them (Outdoor::rooms, generate_maze), `plain` an outdoor level's plain rooms.
// `slots`: the level's warp slots (warp_slots).
inline std::vector<BuiltRoom> level_room_tiles(const std::vector<Outdoor::RoomSeed>& made, const std::vector<PlainRoom>& plain,
                                               const OutdoorData& od, const RoomDt1s& d, int level,
                                               const std::array<WarpSlot, 8>& slots, std::vector<std::string>& notes) {
    using namespace room_tiles_detail;
    auto note = [&](std::string n) { if (std::ranges::find(notes, n) == notes.end()) notes.push_back(std::move(n)); };
    std::vector<BuiltRoom> rooms;                       // game.exe's list: newest room first
    for (auto it = made.rbegin(); it != made.rend(); ++it) {
        BuiltRoom b{ it->x, it->y, it->w, it->h, it->kind, {}, {}, {}, {}, false, nullptr, &*it };
        if (it->kind == 1)
            for (const auto& p : plain) if (p.x == it->x && p.y == it->y) b.plain = &p;
        rooms.push_back(std::move(b));
    }
    std::map<std::pair<int, int>, std::vector<Unit>> preset_units;   // by the preset's origin: units no room has taken yet
    for (std::size_t ri = 0; ri < rooms.size(); ++ri) {
        auto& R = rooms[ri];
        R.up = true;
        const Preset* pre = nullptr;
        const d2d::ds1::Map* map = nullptr;
        if (!R.plain) {
            const auto it = od.presets.find(R.seed->def);
            if (it == od.presets.end()) { note("drlg: preset def " + std::to_string(R.seed->def) + " missing"); continue; }
            pre = &it->second;
            map = R.seed->file < 6 ? pre->maps[std::size_t(R.seed->file)] : nullptr;
            if (!map) { note("drlg: preset " + std::to_string(R.seed->def) + " file not loaded"); continue; }
        }
        if (R.plain) {
            R.units = R.plain->units;                   // the stamps' objects (made at the room's init)
        } else {
            // The preset's units, the first time one of its rooms comes up
            // (FUN_00667890 -> FUN_00667620; a maze made them at generation,
            // FUN_00667970), then this room takes the ones inside it (FUN_00666710).
            auto key = std::pair{ R.seed->px, R.seed->py };
            auto pu = preset_units.find(key);
            if (pu == preset_units.end()) {
                auto list = ds1_units(*map, od.ids);
                std::ranges::reverse(list);             // copied into the preset's list front-first again (FUN_00667510)
                for (auto& u : list) {
                    if (rolled_unit(u, od.ids)) note("drlg: preset units that roll to stay (FUN_00667620) not implemented");
                    u.x += R.seed->px * 5;
                    u.y += R.seed->py * 5;
                }
                if (level == 2 && R.seed->def >= 4 && R.seed->def <= 7 && R.seed->file == 3 && od.ids.monstats > 0x10a)
                    list.insert(list.begin(), { 1, 0x10a, 1, (R.seed->px + pre->w / 2) * 5, (R.seed->py + pre->h / 2) * 5, 0 });   // FUN_006664a0: Flavie
                pu = preset_units.emplace(key, std::move(list)).first;
            }
            auto& list = pu->second;
            for (auto it = list.begin(); it != list.end();) {
                if (it->x >= R.x * 5 && it->y >= R.y * 5 && it->x < (R.x + R.w) * 5 && it->y < (R.y + R.h) * 5) {
                    R.units.insert(R.units.begin(), { it->type, it->id, it->mode, it->x - R.x * 5, it->y - R.y * 5, it->flags });
                    it = list.erase(it);
                } else {
                    ++it;
                }
            }
        }
        const auto list = room_dt1_list(R.plain ? R.plain->dt1_mask : pre->dt1_mask, d);
        const auto nearby = near_rooms(rooms, ri);
        // Reset by FUN_0066ee40; a plain room's init (grass, roads, stamps) rolls it on.
        auto s = R.plain ? R.plain->seed : d2d::rules::Rng{ R.seed->seed };
        if (R.plain)
            for (const auto& t : R.plain->tiles) R.tiles.push_back({ t.layer, R.x + t.x, R.y + t.y, t.orient, t.file, t.index, 0 });
        auto pick = [&](int orient, std::uint32_t w) { return pick_tile(list, s, orient, w); };
        auto add = [&](int layer, int x, int y, int orient, std::uint32_t w) {
            const auto [f, i] = pick(orient, w);
            R.tiles.push_back({ layer, x, y, orient, f, i, w });
            return R.tiles.size() - 1;
        };
        auto chain = [&](bool floor) -> BuiltRoom::Chain& {                    // FUN_0066e620's node
            for (auto& c : R.chains) if (c.floor == floor) return c;
            R.chains.insert(R.chains.begin(), BuiltRoom::Chain{ floor, {} });
            return R.chains.front();
        };
        // FUN_0066e4c0: a chained tile of room N at (x, y) this word can share.
        auto find = [&](BuiltRoom& N, bool floor, int x, int y, std::uint32_t w) -> PlacedTile* {
            if (!N.up || x < N.x || y < N.y || x > N.x + N.w || y > N.y + N.h) return nullptr;
            for (auto& c : N.chains) {
                if (c.floor != floor) continue;
                for (const auto k : c.tiles) {
                    auto& t = N.tiles[k];
                    if (t.x == x && t.y == y && t.orient != 4 && (t.orient == 13 || !(w & 0x8000000u))
                        && ((t.word >> 18) & 3) == ((w >> 18) & 3))
                        return &t;
                }
            }
            return nullptr;
        };
        auto shared = [&](std::uint32_t w, int o, int x, int y) {               // FUN_0066e940
            PlacedTile* t = nullptr;
            BuiltRoom* N = nullptr;
            for (const auto k : nearby) {                                        // FUN_0066e580
                if (k == ri) continue;
                if ((t = find(rooms[k], o == 0, x, y, w))) { N = &rooms[k]; break; }
            }
            if (!t) {                                                            // FUN_0066e620
                if ((o == 10 || o == 11) && !(x >= R.x && y >= R.y && x < R.x + R.w && y < R.y + R.h)) return;
                chain(o == 0);
                const auto k = add(o == 0 ? 1 : o == 13 ? 2 : 0, x, y, o, w);
                chain(o == 0).tiles.insert(chain(o == 0).tiles.begin(), k);
                if (o == 3) add(0, x, y, 4, w);
                if (o == 10 || o == 11) note("drlg: warp wall tiles (FUN_0066e260) not implemented");
                return;
            }
            // FUN_0066e740: the neighbour's tile stays unless the orientations merge differently.
            if (t->word & 0x80) return;                                          // its flags & 1
            int orient = o;
            if (!(w & 0x80)) {
                const int cls = o >= 0 && o < 20 ? kOrientClass[std::size_t(o)] : -1;
                bool skip_merge = false;
                if (o == 8 || o == 9) skip_merge = x == R.x || y == R.y;
                else if ((t->orient == 8 || t->orient == 9) && (x == N->x || y == N->y)) return;
                if (!skip_merge) {
                    if (cls < 0 || t->orient > 7) { if (cls != -1) return; }
                    else orient = kOrientMerge[std::size_t(cls * 7 + t->orient)];
                }
            }
            if (t->orient != 3 && orient == 3) {
                chain(false);
                add(0, x, y, 3, w);
            }
            const bool blank = t->orient == 0 && t->file && t->index >= 0 && t->file->tiles[std::size_t(t->index)].style == 30
                               && t->file->tiles[std::size_t(t->index)].seq == 0;
            if (orient != t->orient || blank) {
                const auto [f, i] = pick(orient, w);
                t->orient = orient;
                t->file = f;
                t->index = i;
            }
        };
        auto word = [&](std::uint32_t w, int o, int x, int y, bool fill) {       // FUN_0066e9b0
            const int style = int((w >> 20) & 0x3f), seq = int((w >> 8) & 0xff);
            if ((o == 10 || o == 11) && style > 7) return;
            if (o == 0 && style == 30 && seq <= 1) w |= 0x80000000u;
            if (w & 0x80000000u) {
                if ((o == 8 || o == 9) && (level < 111 || (level > 112 && level != 117))) { note("drlg: hidden orientation 8/9 tiles (FUN_0066d9e0) not implemented"); return; }
                if (o == 10 || o == 11) {                // FUN_0066e1c0 (the warp unit), FUN_0066e360
                    R.warps.push_back({ x, y, style });
                    const auto& ws = slots[std::size_t(style)];   // style <= 7 here: the warp slot
                    if (ws.id >= 0 && x - R.x != R.w && y - R.y != R.h)   // the warp unit
                        R.units.insert(R.units.begin(), { 5, ws.id, 0, (x - R.x) * 5 + ws.off_x, (y - R.y) * 5 + ws.off_y, 0 });
                    if (ws.lit)
                        for (int k = 0; k < 4; ++k) {    // its lit floor, 2x2 up-left of it (0x6ef554)
                            const std::uint32_t lw = std::uint32_t(seq) << 20 | std::uint32_t(k | 4) << 8;
                            const auto [f, i] = pick(0, lw);
                            R.tiles.push_back({ 1, x - 1 + (k & 1), y - 1 + (k >> 1), 0, f, i, lw });
                        }
                    return;
                }
            }
            if (w & 4) {
                if (w & 2) { if (style == 30 && seq <= 1) w &= ~0x80u; shared(w, 0, x, y); return; }
                if (w & 1) { shared(w, o, x, y); return; }
                if ((w & 0x8000000u) && !(w & 0x80000000u)) { shared(w, 13, x, y); return; }
            }
            if (w & 2) add(1, x, y, 0, w);
            else if (fill && x < R.x + R.w && y < R.y + R.h) {                   // FillBlanks, first floor layer
                const std::uint32_t blank = level == 0x4a ? 0x1e00100u : 0x1e00000u;
                const auto [f, i] = pick(0, blank);
                R.tiles.push_back({ 1, x, y, 0, f, i, (w & ~0x80u) | 0x80000000u });
            }
            if (w & 1) {
                add(0, x, y, o, w);
                if (o == 3) add(0, x, y, 4, w);
                if ((o == 10 || o == 11) && level != 0x85) note("drlg: warp wall tiles (FUN_0066e260) not implemented");
            }
            if (w & 0x8000000u) add(2, x, y, 13, w);
        };
        if (R.plain) {                                  // FUN_0067d710: wall grid, then floor grid, 9x9
            const auto& P = *R.plain;
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x)
                    word(P.wall[std::size_t(y * 9 + x)], int(P.orient[std::size_t(y * 9 + x)] & 0xff), R.x + x, R.y + y, false);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) word(P.floor[std::size_t(y * 9 + x)], 0, R.x + x, R.y + y, false);
            continue;
        }
        // A preset room (FUN_006667d0 then FUN_00666ac0): its 9x9 slice of each
        // DS1 layer, edges | 0x84 (floors | 0x80 throughout); walked floors
        // (the first with FillBlanks), walls, shadow, 8 wide / high where
        // KillEdge meets the preset's own right / bottom edge.
        const int ox = R.x - R.seed->px, oy = R.y - R.seed->py;
        auto slice = [&](const d2d::ds1::Layer& l, bool orient) {
            std::array<std::uint32_t, 81> g{};
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) {
                    const int sx = ox + x, sy = oy + y;
                    if (sx < 0 || sy < 0 || sx >= map->width() || sy >= map->height()) continue;
                    const auto& t = l.cells[std::size_t(sy) * std::size_t(map->width()) + std::size_t(sx)];
                    g[std::size_t(y * 9 + x)] = orient ? std::uint32_t(t.wall_type) | t.wall_zero << 8 : d2d::ds1::Map::word(t);
                }
            return g;
        };
        auto edges = [](std::array<std::uint32_t, 81>& g, std::uint32_t v) {
            for (int i = 0; i < 9; ++i) for (const int k : { i, 72 + i, i * 9, i * 9 + 8 }) g[std::size_t(k)] |= v;
        };
        const bool kx = R.x + R.w == R.seed->px + pre->w, ky = R.y + R.h == R.seed->py + pre->h;   // KillEdge is 1 on every act 1 preset
        const int ww = kx ? 8 : 9, hh = ky ? 8 : 9;
        auto walk = [&](const std::array<std::uint32_t, 81>& g, const std::array<std::uint32_t, 81>* orient, bool fill) {   // FUN_0066ec10
            for (int y = 0; y < hh; ++y)
                for (int x = 0; x < ww; ++x)
                    word(g[std::size_t(y * 9 + x)], orient ? int((*orient)[std::size_t(y * 9 + x)] & 0xff) : 0, R.x + x, R.y + y, fill);
        };
        for (std::size_t l = 0; l < map->floors().size(); ++l) {
            auto g = slice(map->floors()[l], false);
            for (auto& v : g) v |= 0x80;                // FUN_0067c590
            edges(g, 0x84);
            walk(g, nullptr, l == 0);
        }
        for (std::size_t l = 0; l < map->walls().size(); ++l) {
            auto g = slice(map->walls()[l], false);
            const auto og = slice(map->walls()[l], true);
            if (l == 0) edges(g, 0x84);
            else for (auto& v : g) v |= 0x80;           // FUN_0067c590
            walk(g, &og, false);
        }
        if (!map->shadows().empty()) {
            auto g = slice(map->shadows()[0], false);
            edges(g, 0x84);
            walk(g, nullptr, false);
        }
    }
    return rooms;
}

}  // namespace d2d::drlg
