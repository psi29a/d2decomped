// SPDX-License-Identifier: GPL-3.0-or-later
// A level's rooms populated as they come into play, as FUN_0052d160 walks
// the act's room1 list (newest first): each room's seed step
// (FUN_0054f060), its preset units (FUN_005559a0), then the random object
// groups (FUN_00552610 and the PopulateFns at 0x731d00;
// docs/research/re/objects.md "Random object groups per room") and its
// monsters, all on the game's one object seed and game seed. Proven
// against game.exe: tools/emu objgroups.py, drlg-dump <seed> <level>
// objgroups; levels in turn in one game: drlg-dump ... game.
#include "gamedata.hpp"
#include <drlg.hpp>
#include <level_ids.hpp>
#include <outdoor_data.hpp>
#include <quests.hpp>
#include <units.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <object_ids.hpp>
#include <rules.hpp>
#include <txt.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2d::game {
namespace {

constexpr std::array kAroundX{ -1, 0, 1, -1, 1, -1, 0, 1 }, kAroundY{ -1, -1, -1, 0, 0, 1, 1, 1 };   // 0x731b7c, 0x731b9c

struct Rect { int x, y, w, h; };                        // a room, level subtiles (FUN_00619730)
using Spot = std::optional<std::pair<int, int>>;

}  // namespace

// A level's object placement (Spawning::LevelState::objects): the game's
// seeds copied in for each room (load) and back out (save).
struct ObjectRooms {
    const GameData& game_data;
    const GameData::LevelBuilder& builder;
    Level& level;                                       // its npcs, room flags: GameData owns its levels mutable
    d2d::rules::Rng rgn;                                // the game's object seed (FUN_00546fa0)
    int width = 0, height = 0;                          // subtiles
    std::vector<std::uint16_t> stamps;                  // the units' collision (room1 +0x20) over the tiles' (Level::tile_walk)
    std::vector<bool> in_room;                          // tiles of the rooms up (Spawning::LevelState::order)
    std::vector<ObjectRooms*> others;                   // the other levels' with rooms up, the act's collision across their edges
    int themes = 0;
    // objrgn's record of the level (FUN_00546f90): +4 rooms counted, +8
    // the target, +0x10 shrines of id 2, +0x14 / +0x18 shrine and well spots.
    int counter = 0, target = 0x7fffffff, refills = 0;
    std::vector<std::pair<int, int>> shrines, wells;
    Rect room{};
    d2d::rules::Rng seed;                               // the room's (room1 +0x6c)
    d2d::rules::Rng game;                               // the game seed (game +0xd0): a step per unit made
    std::array<std::uint8_t, 96> game_quests{};         // Spawning's
    bool orb_smashed = false;                            // Spawning's
    std::bitset<128> superuniques;                      // made (game +0x1d30)
    std::vector<std::array<int, 3>> rescue_cages;       // Spawning's
    int tomb_symbols = 0;                               // Spawning's
    int sands_objects = 0;                              // the Forgotten Sands' preset objects counted (game +0x1de4)
    d2d::rules::Region region;                          // the level's monster region: their looks
    Level::GroupRoom* out = nullptr;
    // FUN_0054db50: where players come in (as populate()'s near_way), within Levels WarpDist.
    std::vector<std::pair<int, int>> ways;
    int warp_dist = 2025;

    static bool none(const Level& level, std::size_t room_index);
    void load(Spawning& spawning, std::size_t room_index);
    void rooms_up(const Spawning& spawning);
    void save(Spawning& spawning) const;

    int obj(int id, const char* column) const {
        const auto found = builder.obj_row.find(std::to_string(id));
        return found == builder.obj_row.end() ? 0 : std::atoi(std::string(builder.objects.get(found->second, column)).c_str());
    }
    // FUN_0064ca50: 0x27 off the act's rooms up (a neighbour level's past the edge).
    [[nodiscard]] std::uint16_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            const int act_x = x + level.world_x * 5, act_y = y + level.world_y * 5;
            for (const auto* other : others)
                if (const int other_x = act_x - other->level.world_x * 5, other_y = act_y - other->level.world_y * 5;
                    other_x >= 0 && other_y >= 0 && other_x < other->width && other_y < other->height) return other->own(other_x, other_y);
            return 0x27;
        }
        return own(x, y);
    }
    [[nodiscard]] std::uint16_t own(int x, int y) const {
        if (!in_room[std::size_t(y / 5) * std::size_t(width / 5) + std::size_t(x / 5)]) return 0x27;
        const auto cell = std::size_t(y) * std::size_t(width) + std::size_t(x);
        return std::uint16_t((cell < level.tile_walk.size() ? level.tile_walk[cell] : 0) | stamps[cell]);
    }
    // FUN_0064d800: a size_x x size_y area centred on (x, y) (FUN_0064ceb0 across the rooms).
    [[nodiscard]] bool hit(int x, int y, int size_x, int size_y, int mask) const {
        if (size_x < 2 && size_y < 2) { const auto flags = at(x, y); return flags == 0x27 || (flags & mask); }
        for (int ty = y - (size_y >> 1); ty < y - (size_y >> 1) + size_y; ++ty)
            for (int tx = x - (size_x >> 1); tx < x - (size_x >> 1) + size_x; ++tx)
                if (const auto flags = at(tx, ty); flags == 0x27 || (flags & mask)) return true;
        return false;
    }
    // A unit's collision shape (MonStats2 SizeX): 1 a subtile, 2 a plus (FUN_0064d100), 3+ a box.
    [[nodiscard]] bool hit_shape(int x, int y, int size, int mask) const {
        return size == 2 ? hit(x, y, 3, 1, mask) || hit(x, y - 1, 1, 1, mask) || hit(x, y + 1, 1, 1, mask) : hit(x, y, size, size, mask);
    }
    void stamp_shape(int x, int y, int size, std::uint16_t bits, bool clear = false) {
        if (size == 2) { stamp(x, y, 3, 1, bits, clear); stamp(x, y, 1, 3, bits, clear); }
        else if (size > 0) stamp(x, y, size, size, bits, clear);
    }
    void stamp(int x, int y, int size_x, int size_y, std::uint16_t bits, bool clear = false) {
        for (int ty = y - (size_y >> 1); ty < y - (size_y >> 1) + size_y; ++ty)
            for (int tx = x - (size_x >> 1); tx < x - (size_x >> 1) + size_x; ++tx)
                if (tx >= 0 && ty >= 0 && tx < width && ty < height) {
                    auto& cell = stamps[std::size_t(ty) * std::size_t(width) + std::size_t(tx)];
                    cell = clear ? std::uint16_t(cell & ~bits) : std::uint16_t(cell | bits);
                }
    }
    // FUN_00555230 for an object: add_object's rolls, its footprint
    // (FUN_006209d0: 0x400, | 4 if BlockMissile; 0x8000, no mask's, for a
    // SubClass 4 non-door; a door 0x806 if BlocksVis, else 0x808 if
    // BlockMissile, else 0x400;
    // none without HasCollision in its start mode: ON if preoperated, else NU,
    // whatever the renderer shows; Tristram's bodies block).
    // Returns its shrine id (Npc::shrine).
    int make(int id, int x, int y, bool group) {
        const auto before = level.npcs.size();
        auto gold = rgn;
        add_object(game_data, builder.objects, builder.obj_row, level, id, x, y, rgn);
        const bool door = obj(id, "IsDoor"), missile = obj(id, "BlockMissile");
        const std::uint16_t bits = door ? obj(id, "BlocksVis") ? 0x806 : missile ? 0x808 : 0x400 : obj(id, "SubClass") & 4 ? 0x8000 : missile ? 0x404 : 0x400;
        const bool operated = level.npcs.size() > before && level.npcs.back().preoperated;
        const auto unit = game.next();                                 // FUN_00552df0: its seed
        // InitFn 60 (FUN_005bbba0, object 404 in Travincal), run before the
        // object's footprint (FUN_00554850): with the orb smashed (record 0x13
        // +0xc) the object goes to mode 2 (FUN_00624690: broken, no light, no
        // collision, not selectable); then the compelling orb (0x16e) on its
        // spot (FUN_005b3090: radius -1) unless the record has made it (+0x28).
        // The made bit only matters when a room comes up again, which d2d's
        // never do (populate once a game), so it isn't kept.
        bool broken = false;
        if (obj(id, "InitFn") == 60) {
            if (orb_smashed && level.npcs.size() > before) { broken = true; level.npcs.back().mode = "ON"; level.npcs.back().name.clear(); }
            if (spawns) leader = int(spawns->size());
            monster(int(game_data.mon_bin[0x16e]), x, y, 0);
        }
        if (obj(id, "InitFn") == 62) cage_prisoners(x, y);
        std::vector<std::array<int, 3>> souls;
        if (obj(id, "InitFn") == 46) souls = trapped_souls(x, y);
        if (obj(id, operated || broken ? "HasCollision2" : "HasCollision0")) stamp(x, y, obj(id, "SizeX"), obj(id, "SizeY"), bits);
        if (level.npcs.size() > before) {
            auto& npc = level.npcs.back();
            if (obj(id, "InitFn") != 3 && obj(id, "InitFn") != 57) npc.seed = d2d::rules::Rng{ unit };
            npc.room = int(index);
        }
        if (obj(id, "InitFn") == 28) piles(gold, x, y);
        if (group) {
            out->made.push_back({ id, x, y });
            for (const auto& [soul, soul_x, soul_y] : souls) out->made.push_back({ soul, soul_x, soul_y });   // as objgroups.py lists them
        }
        return level.npcs.size() > before ? level.npcs.back().shrine : 0;
    }

    // InitFn 62 (FUN_005886a0, a5q2's cage, object 473), run before its
    // footprint: with the quest's record open (+9: not shut at the first
    // join), a fourth cage or one on a spot already up makes none; else its
    // slot is the next, and FUN_00588600 makes five act5pows (0x216) round it
    // (FUN_005b2f20 radius 5, mode 1), at most 25 tries.
    void cage_prisoners(int at_x, int at_y) {
        if (d2d::rules::quest::bit(game_quests, 0x20, 15)) return;
        const std::array<int, 3> spot{ level.id, at_x, at_y };
        if (rescue_cages.size() >= 3 || std::ranges::contains(rescue_cages, spot)) return;
        const int slot = int(rescue_cages.size());
        rescue_cages.push_back(spot);
        const int row = int(game_data.mon_bin[0x216]);
        if (spawns) leader = int(spawns->size());                     // the cage's placement: one group
        for (int tries = 0, made_count = 0; tries < 25 && made_count < 5; ++tries)
            if (int put_x, put_y; put(row, at_x, at_y, 5, false, put_x, put_y)) {
                const auto before = spawns ? spawns->size() : 0;
                made(row, put_x, put_y);
                if (spawns && spawns->size() > before) spawns->back().cage = slot;
                ++made_count;
            }
    }

    // InitFn 46 (FUN_005506d0, object 369, the trapped soul placeholder):
    // ((w·h) >> 7)·30 >> 8 souls for its room, up to 12 tries on the object
    // seed: a spot rand(w − 4), rand(h − 4) in, where fits_wide(3, 3) takes
    // it, a soul (odd: 404, else 403) at the placeholder's own spot (mode 1,
    // that spot only); then on from that random spot, hop k (k from 1) going
    // on while rand(k / 2) is 0: up to max(left, 4) · 3 hops of (rand % 5 +
    // 5) · 2 subtiles in direction rand & 7 (0x731b7c / 0x731b9c) until
    // fits_wide takes one, a soul there; none found ends the try (after
    // the loop head's rand(k / 2) once more).
    std::vector<std::array<int, 3>> trapped_souls(int at_x, int at_y) {
        std::vector<std::array<int, 3>> souls;
        static constexpr std::array<int, 8> kDx = { -1, 0, 1, -1, 1, -1, 0, 1 }, kDy = { -1, -1, -1, 0, 0, 1, 1, 1 };
        int left = ((room.h * room.w) >> 7) * 30 >> 8;
        auto soul = [&](int x, int y) {
            const int hc_idx = rgn.next() & 1 ? 0x194 : 0x193;
            if (spawns) leader = int(spawns->size());
            last_made.reset();
            monster(int(game_data.mon_bin[std::size_t(hc_idx)]), x, y, 0);
            if (last_made) { --left; souls.push_back({ hc_idx, last_spot.first, last_spot.second }); }
        };
        for (int tries = 12; left > 0 && tries > 0; --tries) {
            int x = room.x + rgn(room.w - 4), y = room.y + rgn(room.h - 4);
            if (!fits_wide(x, y, 3, 3)) continue;
            soul(at_x, at_y);
            for (int hop = 1;; ++hop) {
                if (hop >> 1 > 0 && rgn(hop >> 1) != 0) break;
                bool found = false;
                for (int step = 0, most = std::max(left, 4) * 3; step < most && !found; ++step) {
                    const int dir = int(rgn.next() & 7);
                    x += int(rgn.next() % 5 + 5) * kDx[std::size_t(dir)] * 2;
                    y += int(rgn.next() % 5 + 5) * kDy[std::size_t(dir)] * 2;
                    found = fits_wide(x, y, 3, 3);
                }
                if (!found) {                                   // back to the loop's head: its draw, then out
                    if (hop >> 1 > 0) rgn(hop >> 1);
                    break;
                }
                soul(x, y);
            }
        }
        return souls;
    }

    // FUN_0054f8c0 (InitFn 28), replaying add_object's rolls: 1..9 piles,
    // each at rand(4), rand(4) off it, dropped (FUN_00559300) where clear
    // (0x3f11) and the last spot tried inside its room is too (sic). A pile
    // is an item: two game-seed steps (FUN_00552df0, FUN_00552e90) and
    // 0x200 at the free subtile nearest (x + 2, y + 3) (FUN_0064dea0) with
    // no 0x801 on the walk back to the spot (FUN_0066a670: expfield.d2's
    // steps, FUN_0066a5d0).
    void piles(d2d::rules::Rng gold, int x, int y) {
        auto inside_room = [&](int at_x, int at_y) { return at_x >= room.x && at_y >= room.y && at_x < room.x + room.w && at_y < room.y + room.h; };
        int last_x = x, last_y = y;
        for (int count = gold(9) + 1; count > 0; --count) {
            const int dx = int(gold.next() & 3), dy = int(gold.next() & 3);
            if (!inside_room(last_x + dx, last_y + dy)) continue;
            last_x = x + dx; last_y = y + dy;
            if (hit(last_x, last_y, 1, 1, 0x3f11)) continue;
            const auto [at_x, at_y] = drop_spot(game_data.field, last_x, last_y, [&](int probe_x, int probe_y) { return at(probe_x, probe_y); });
            stamp(at_x, at_y, 1, 1, 0x200);
            game.next(); game.next();
        }
    }

    // FUN_00550220: clear of walls, objects and doors round a size_x x size_y object.
    [[nodiscard]] bool fits_wide(int x, int y, int size_x, int size_y) const {
        return size_x + 2 <= room.w && size_y + 2 <= room.h && room.x + 1 < x && room.y + 1 < y && x < room.x - size_x - 2 + room.w
            && y < room.y - size_y - 2 + room.h && !hit(x, y, size_x + 7, size_y + 7, 0xc01) && !hit(x, y, size_x, size_y, 0x3f11);
    }
    // FUN_005502f0
    [[nodiscard]] bool fits_near(int x, int y, int size_x, int size_y) const {
        return room.w > 1 && room.h > 1 && room.x + 2 < x && room.y + 2 < y && x < room.x - size_x + room.w && y < room.y - size_y + room.h
            && !hit(x, y, size_x + 2, size_y + 2, 0x3f11);
    }
    [[nodiscard]] bool inside(int x, int y) const {
        return room.x + 1 <= x && room.y + 1 <= y && x < room.x - 1 + room.w && y < room.y - 1 + room.h;
    }
    static bool apart(const std::vector<std::pair<int, int>>& spots, int x, int y, int gap) {   // FUN_00547430 / FUN_005473a0
        return std::ranges::none_of(spots, [&](const auto& spot) { return std::abs(spot.first - x) < gap || std::abs(spot.second - y) < gap; });
    }
    // FUN_00550380: 5 random spots on the object seed.
    Spot at_random(int id, int size_x, int size_y) {
        if (room.w < 2 || room.h < 2) return {};
        for (int tries = 0; tries < 5; ++tries) {
            const int x = room.x + rgn(room.w - size_x - 1), y = room.y + rgn(room.h - size_y - 1);
            if (inside(x, y) && !hit(x, y, size_x + 6, size_y + 6, 0x3f11)) { make(id, x, y, true); return std::pair{ x, y }; }
        }
        return {};
    }
    bool gate(int prob) { return int(rgn.next() % 100) <= prob; }
    int count(int density) const { return ((room.w * room.h >> 7) * density) >> 8; }

    // FUN_00551470: count tries of at_random; the last one's.
    Spot scatter(int id, int density, int prob) {
        if (!gate(prob)) return {};
        Spot last;
        for (int remaining = count(density); remaining > 0; --remaining) last = at_random(id, obj(id, "SizeX"), obj(id, "SizeY"));
        return last;
    }
    // FUN_00551150: 30 in 100, a body (103) where it stands.
    void companion(const Spot& spot) {
        if (spot && rgn.next() % 100 > 70) make(103, spot->first, spot->second, true);
    }
    // FUN_00550c20: clusters of a table's objects, spaced 2 x (gap_base + rand(gap_rand)) along a random direction.
    void cluster(int id, int density, int prob) {
        if (!gate(prob)) return;
        static constexpr std::array<int, 2> kCasket{ 3, 28 }, kUrn{ 89, 284 }, kPot{ 208, 209 };        // 0x731d5c, 0x731d78, 0x731d80
        static constexpr std::array<int, 3> kCorpse{ 79, 53, 1 };                                   // 0x731d50
        static constexpr std::array<int, 5> kRock{ 4, 9, 52, 94, 95 };                              // 0x731d64
        const int* table;
        int size, tries = 12, gap_base = 5, gap_rand = 5;
        bool close = false;                                                                          // FUN_005502f0 for the rest
        if (id == 3) { table = kCasket.data(); size = 2; tries = 18; }
        else if (id == 79 || id == 1) { table = kCorpse.data(); size = 3; }
        else if (id == 4) { table = kRock.data(); size = 5; gap_base = 1; gap_rand = 0; close = true; }
        else if (id == 89) { table = kUrn.data(); size = 2; }
        else if (id == 208 || id == 209) { table = kPot.data(); size = 2; gap_base = 1; gap_rand = 0; close = true; }
        else return;
        const int size_x = obj(id, "SizeX"), size_y = obj(id, "SizeY");
        int left = count(density);
        if (left < 1) return;
        for (; tries > 0; --tries) {
            int pick = table[rgn(size)];
            int x = room.x + rgn(room.w - size_x - 1), y = room.y + rgn(room.h - size_y - 1);
            if (fits_wide(x, y, size_x, size_y)) {
                make(pick, x, y, true);
                bool ok = true;
                for (int placed = 1;;) {
                    if (const int half = placed >> 1; half > 0 && rgn(half) != 0) break;
                    if (!ok) break;
                    ok = false;
                    for (int k = 0; k < std::max(left, 4) * 3 && !ok; ++k) {
                        const int dir = int(rgn.next() & 7);
                        x += (rgn(gap_rand) + gap_base) * kAroundX[std::size_t(dir)] * 2;
                        y += (rgn(gap_rand) + gap_base) * kAroundY[std::size_t(dir)] * 2;
                        ok = close ? fits_near(x, y, size_x, size_y) : fits_wide(x, y, size_x, size_y);
                    }
                    if (!ok) continue;
                    pick = table[rgn(size)];
                    make(pick, x, y, true);
                    ++placed;
                }
                --left;
            }
            if (left < 1) return;
        }
    }
    // FUN_00551850: barrels, objects.txt 7's size and spacing, 8 at most.
    void barrels(int density, int prob) {
        if (!gate(prob)) return;
        const int size_x = obj(7, "SizeX"), size_y = obj(7, "SizeY"), space_x = obj(7, "Xspace"), space_y = obj(7, "Yspace");
        int placed = 0;
        for (int left = count(density), tries = left * 2; left > 0 && tries > 0; --tries) {
            const int first = rgn.next() % 3 ? 7 : 11;                                             // 0x551933: 1 in 3 exploding
            int x = room.x + rgn(room.w), y = room.y + rgn(room.h);
            if (!fits_near(x, y, size_x, size_y)) continue;
            make(first, x, y, true);
            if (++placed >= 8) return;
            bool ok = true;
            for (int in_cluster = 1;;) {
                if (const int half = in_cluster >> 1; half > 0 && rgn(half) != 0) break;
                if (!ok) break;
                ok = false;
                for (int k = 0; k < 15 && !ok; ++k) {
                    const int dir = int(rgn.next() & 7);
                    x += space_x * kAroundX[std::size_t(dir)];
                    y += space_y * kAroundY[std::size_t(dir)];
                    ok = fits_near(x, y, size_x, size_y);
                }
                if (!ok) continue;
                make(rgn.next() & 3 ? 7 : 11, x, y, true);                                         // 0x551b82: 1 in 4 an exploding barrel
                ++in_cluster;
                if (++placed > 7) return;
            }
            --left;
        }
    }
    // FUN_00551200: a spot for one of 4 layouts (0x731eb4), each of them empty in 1.14d.
    void layout(int id, int prob) {
        if (!gate(prob)) return;
        rgn(4);
        const int size_x = obj(id, "SizeX"), size_y = obj(id, "SizeY");
        for (int tries = 8; tries > 0; --tries) {
            const int x = room.x + rgn(room.w - size_x - 1), y = room.y + rgn(room.h - size_y - 1);
            if (fits_wide(x, y, 5, 5)) return;                                                     // ponytail: the layout's list is empty, nothing placed
        }
    }
    // FUN_00551580: the room seed's gate, then the first at_random that lands.
    void single(int id, int density, int prob) {
        if (int(seed.next() % 100) > prob) return;
        for (int remaining = count(density); remaining > 0; --remaining)
            if (at_random(id, obj(id, "SizeX"), obj(id, "SizeY"))) return;
    }
    // FUN_005516c0: a well, 4 at most (and an eighth of the rooms), 100 subtiles off the others on both axes.
    void well(int id, int prob) {
        if (wells.size() == 4 || int(wells.size()) > target / 8) return;
        if (prob < int(seed.next() % 100)) return;
        const int span_x = obj(id, "SizeX") * 2 + 1, span_y = obj(id, "SizeY") * 2 + 1;
        if (room.w < 2 || room.h < 2 || room.w <= span_x || room.h <= span_y) return;           // FUN_00550540, on the room seed
        for (int tries = 0; tries < 5; ++tries) {
            const int x = room.x + seed(room.w - span_x - 1), y = room.y + seed(room.h - span_y - 1);
            if (!inside(x, y) || hit(x, y, span_x + 6, span_y + 6, 0x3f11) || !apart(wells, x, y, 100)) continue;
            make(id, x, y, true);
            wells.push_back({ x, y });
            return;
        }
    }
    // FUN_00552b50: a shrine, 10 at most (and an eighth of the rooms), 50
    // subtiles off the others. Late in a level with no shrine 2 yet
    // (FUN_00547330) it's 30 tries, forced, and one made becomes a refill
    // shrine (FUN_00552ac0, FUN_0054f770's class-2 pick).
    void shrine(int id, int prob) {
        const int size_x = obj(id, "SizeX"), size_y = obj(id, "SizeY"), orient = obj(id, "Orientation");
        const int roll = int(seed.next() % 100);
        const bool forced = target > 0 && (counter << 7) / target > 0x60 && refills == 0;
        if (!forced && prob < roll) return;
        if (shrines.size() == 10 || int(shrines.size()) > target / 8) return;
        for (int tries = forced ? 30 : 3; tries > 0; --tries) {
            if (size_x + 2 > room.w || size_y + 2 > room.h) return;
            for (int k = 0; k < 5; ++k) {                                                            // FUN_00550a30, on the room seed
                int x, y;
                if (orient == 1) { x = room.x + room.w / 4 + seed(room.w / 2); y = room.y + seed(1) + 1; }
                else if (orient == 2) { x = room.x + seed(1) + 1; y = room.y + room.h / 4 + seed(room.h / 2); }
                else { x = room.x + seed(room.w - size_x - 1); y = room.y + seed(room.h - size_y - 1); }
                if (!inside(x, y) || hit(x, y, size_x + 6, size_y + 6, 0x3f11) || !apart(shrines, x, y, 50)) continue;
                const int kind = make(id, x, y, true);
                if (kind == 2) ++refills;
                else if (forced) {
                    std::vector<int> refill;
                    for (std::size_t row = 0; row < game_data.shrines.size(); ++row) if (game_data.shrines[row].effectclass == 2) refill.push_back(int(row));
                    for (int pick_tries = 8; pick_tries > 0 && !refill.empty(); --pick_tries) {
                        const int pick = std::max(refill[std::size_t(rgn(int(refill.size())))], 1);
                        if (1 >= game_data.shrines[std::size_t(pick)].level_min) break;
                    }
                    if (!level.npcs.empty() && level.npcs.back().object_id == id) level.npcs.back().shrine = 2;
                    ++refills;
                }
                shrines.push_back({ x, y });
                return;
            }
        }
    }

    // The area FUN_0054ec90 is populating (FUN_0061ad50's, subtiles; w 0:
    // none, the room's rect bounds a placement), the room's index, its
    // monsters (the level's spawns, Spawning::LevelState) and where the placement in hand starts
    // in them, and the region's counts (+4 rooms done, +0xc total, +0x2c8 uniques).
    struct Box { int x = 0, y = 0, w = 0, h = 0; std::uint32_t id = 0; };
    Box area{};
    std::size_t index = 0;
    std::vector<d2d::rules::Spawn>* spawns = nullptr;
    Spawning::LevelState* level_state = nullptr;
    int leader = 0;
    d2d::rules::Population pop;

    [[nodiscard]] const d2d::rules::MonType* type_at(int type) const {
        return type >= 0 && std::size_t(type) < game_data.monsters.types.size() ? &game_data.monsters.types[std::size_t(type)] : nullptr;
    }
    [[nodiscard]] int size_of(int type) const { const auto* info = type_at(type); return info ? info->size : 2; }
    // FUN_0061b130 → FUN_0066ce30: the id of the room's area holding a subtile.
    [[nodiscard]] std::uint32_t area_id(int x, int y) const {
        if (index < level.room_areas.size())
            for (const auto& room_area : level.room_areas[index])
                if (x / 5 >= room_area.left && y / 5 >= room_area.top && x / 5 < room_area.right && y / 5 < room_area.bottom) return room_area.id;
        return 0xffffffff;
    }
    // FUN_005b2a00's test (FUN_0064d9b0): the type's shape (MonStats2 SizeX:
    // 1 a subtile, 2 a plus, 3 a box, more never fits) against its spawnCol's mask.
    [[nodiscard]] bool fits(int type, int x, int y) const {
        static constexpr int kMask[4] = { 0x3c01, 0x1c0, 0x3f11, 0 };
        const auto* info = type_at(type);
        const int col = info ? info->spawn_col : 0;
        // FUN_005fd350: a nest's (by BaseId) laying spot clear for a plus too.
        // ponytail: vilemother1's (298) check is off for population's calls; left out.
        const int base = info ? info->base : -1;
        if (const auto nest = [&](int dx, int dy, int mask) { return hit_shape(x + dx, y + dy, 2, mask); };
            (base == 206 && nest(0, 3, 0x3c01)) || (base == 228 && nest(0, 2, 0x3c01)) || (base == 334 && nest(-2, -2, 0x1c0)) || (base == 528 && nest(2, 4, 0x3c01)))
            return false;
        return size_of(type) <= 3 && !hit_shape(x, y, size_of(type), col > 0 && col < 4 ? kMask[col] : 0x3c01);
    }
    [[nodiscard]] d2d::rules::SpawnRoom bounds(bool in_area) const {
        return in_area ? d2d::rules::SpawnRoom{ area.x, area.y, area.w, area.h, seed } : d2d::rules::SpawnRoom{ room.x, room.y, room.w, room.h, seed };
    }
    // spawnCol 1 (but rows 0x102..0x107, 0x99): FUN_005b2a00 takes FUN_005b2700's
    // spot instead of the asked one: from rand(n) (0 taken as 1) round the
    // room's floor list, stopping short of the one before it, a water tile
    // (material 2) whose middle subtile holds no unit (0x100) with a plus
    // clear of 0x1c09 at one of (0,-3) (3,0) (0,3) (-3,0).
    [[nodiscard]] bool by_water(int type) const {
        const auto* info = type_at(type);
        return info && info->spawn_col == 1 && (type < 0x102 || type > 0x107) && type != 0x99;
    }
    bool water_spot(std::size_t room_at, d2d::rules::Rng& rng, int& out_x, int& out_y) {
        if (room_at >= level.room_floors.size() || level.room_floors[room_at].empty()) return false;
        const auto& floors = level.room_floors[room_at];
        const int count = int(floors.size());
        const int start = std::max(rng(count), 1), stop = start - 1;
        static constexpr std::array<std::pair<int, int>, 4> kAround = { { { 0, -3 }, { 3, 0 }, { 0, 3 }, { -3, 0 } } };
        for (int at_index = start; at_index != stop; at_index = (at_index + 1) % count) {
            if (at_index >= count || !(floors[std::size_t(at_index)].material & 2)) continue;   // past the list (count 1): game.exe reads beyond it
            const int x = floors[std::size_t(at_index)].x * 5 + 3, y = floors[std::size_t(at_index)].y * 5 + 3;
            if (const auto flags = at(x, y); flags == 0x27 || (flags & 0x100)) continue;
            for (const auto& [dx, dy] : kAround)
                if (!hit_shape(x + dx, y + dy, 2, 0x1c09)) { out_x = x; out_y = y; return true; }
        }
        return false;
    }
    // FUN_005b2a00 round (x, y) out to radius (-1: the spot only), in the
    // area being populated when `bound` (its rect and its id), else the room.
    // A water search round a point (a leader's party or pack, radius > 0)
    // runs in the room holding that point (a water spot can be a room's
    // shared edge row, the next room's), on that room's seed: stepped early
    // when it hasn't populated yet; nothing when it isn't up.
    bool put(int type, int x, int y, int radius, bool bound, int& out_x, int& out_y) {
        if (by_water(type)) {
            std::size_t other = index;
            for (std::size_t i = 0; radius > 0 && i < level.rooms.size(); ++i)
                if (const auto& holder = level.rooms[i]; x >= holder.x * 5 && x < (holder.x + holder.width) * 5 && y >= holder.y * 5 && y < (holder.y + holder.height) * 5) other = i;
            if (other == index || !level_state) return water_spot(index, seed, out_x, out_y);
            if (other < level_state->up.size() && level_state->up[other]) return water_spot(other, level_state->room_seeds[other], out_x, out_y);
            if (std::ranges::find(level_state->order, other) == level_state->order.end()) return false;
            auto& early = level_state->early_seeds[other];
            if (!early) early = d2d::rules::Rng{ other < level.room1_seeds.size() ? level.room1_seeds[other] : level.rooms[other].seed };
            return water_spot(other, *early, out_x, out_y);
        }
        const bool in_area = bound && area.w > 0;
        auto spawn_room = bounds(in_area);
        const bool ok = d2d::rules::monster_detail::place(spawn_room, x, y, radius, [&](int test_x, int test_y) { return (!in_area || area_id(test_x, test_y) == area.id) && fits(type, test_x, test_y); }, out_x, out_y);
        seed = spawn_room.seed;
        return ok;
    }
    // FUN_0054dc40: 20 random spots of the area (else the room) where
    // `type` fits, none within WarpDist of an entrance (FUN_0054db50) if `near`.
    bool spot(int type, bool nearby, int& out_x, int& out_y) {
        const bool in_area = area.w > 0;
        auto spawn_room = bounds(in_area);
        auto by_way = [&](int x, int y) {
            return nearby && std::ranges::any_of(ways, [&](const auto& way) { return (x - way.first) * (x - way.first) + (y - way.second) * (y - way.second) < warp_dist; });
        };
        if (by_water(type)) {                            // FUN_0054dc40's 20 tries, each its area then the water (FUN_005b3040)
            const int left = spawn_room.x + 1, top = spawn_room.y + 1, span_x = spawn_room.x + spawn_room.width - left, span_y = spawn_room.y + spawn_room.height - top;
            for (int tries = 0; tries < 20; ++tries) {
                const int x = seed(span_x) + left, y = seed(span_y) + top;
                if (by_way(x, y) || (in_area && area_id(x, y) != area.id)) continue;
                if (int water_x, water_y; water_spot(index, seed, water_x, water_y)) { out_x = x; out_y = y; return true; }
            }
            return false;
        }
        const bool ok = d2d::rules::room_spot(spawn_room, [&](int test_x, int test_y) { return (!in_area || area_id(test_x, test_y) == area.id) && fits(type, test_x, test_y); }, by_way, out_x, out_y);
        seed = spawn_room.seed;
        return ok;
    }
    // FUN_00555230 for a monster, FUN_00552df0: its seed off the game's.
    // Its footprint: 0x100 in its shape, 0x1000 in the one a size down.
    // A monster's own seed takes its look (FUN_00573cb0, rules::monster_look)
    // and one stat roll as it's made. Recorded in the level's spawns (NPCs aside).
    std::optional<d2d::rules::Rng> last_made;            // the unit seed made() left last (after its look and stat draws)
    std::pair<int, int> last_spot;                       // and where it stood
    d2d::rules::Rng made(int type, int at_x, int at_y) {
        const auto& types = game_data.monsters.types;
        // FUN_0064ea90 by its pattern (DAT_006eb3dc: SizeX 0 none, 1 or 2 a plus, 3 its box), 0x1000 a size down.
        const auto* info = type_at(type);
        const int shape = info && !info->footprint ? 0 : size_of(type) == 3 ? 3 : 2;
        stamp_shape(at_x, at_y, shape, 0x100);
        stamp_shape(at_x, at_y, shape - 1, 0x1000);
        // FUN_005b1cf0: a boss's mods, each FUN_005a4850(mod, 1); FUN_005a0320
        // counts a unique only as it first sets flag 8, so one a boss (a
        // superunique one too: the Smith, Coldworm; monster() leaves those).
        if (info) pop.uniques += d2d::rules::act_boss(type, info->base) ? 1 : 0;
        const auto value = game.next();
        d2d::rules::Rng own{ value };
        const std::vector<d2d::rules::Components>* sets = nullptr;
        for (std::size_t i = 0; i < region.types.size() && i < region.components.size(); ++i)
            if (region.types[i].first == type) sets = &region.components[i];
        // FUN_00547bc0: a type the region lacks joins it (13 at most) with
        // its sets on this seed (FUN_005bdb20) when TotalPieces > 2
        // (MonStats2 +0xec); else its look rolls every layer.
        const bool special = type == 0xc3 || type == 0xc4 || type == 0x126 || type == 0x128;
        if (!sets && !special && region.types.size() < 13 && type_at(type) && types[std::size_t(type)].pieces > 2) {
            region.types.emplace_back(type, 0);
            region.components.push_back(d2d::rules::roll_components(types[std::size_t(type)].choices, own));
            sets = &region.components.back();
        }
        if (type_at(type)) (void)d2d::rules::monster_look(sets, types[std::size_t(type)].choices, own);
        (void)d2d::rules::monster_variant(type, own);
        own.next();
        // FUN_005d6b60: each oninit MonEquip row rolls which of its items on
        // the unit's seed and makes it (2 game steps).
        if (std::size_t(type) < game_data.mon_equip_oninit.size())
            for (const int items : game_data.mon_equip_oninit[std::size_t(type)]) { own(items); game.next(); game.next(); }
        const bool npc = type_at(type) && std::size_t(type) < game_data.mon_is_npc.size() && game_data.mon_is_npc[std::size_t(type)];
        if (spawns && !npc) spawns->push_back({ type, at_x, at_y, leader, -1, d2d::rules::Boss::none, {}, 0, value });
        last_made = own;
        last_spot = { at_x, at_y };
        return own;
    }
    void tag(std::size_t spawn_index, d2d::rules::Boss kind, std::vector<int> mods = {}, int name_seed = 0) {
        if (!spawns || spawn_index >= spawns->size()) return;
        auto& spawn = (*spawns)[spawn_index];
        spawn.boss = kind; spawn.mods = std::move(mods); spawn.name_seed = name_seed;
    }
    // FUN_005b2830: PartyMin..Max on the leader's seed, minion1 / minion2 in
    // turn, radius 4 round it in its room (FUN_005b23c0, no party of their own).
    // A tentacle head's (base 0x105, FUN_005b2570): its minion1s, each at its
    // spot only, at 0x6e2cf0's offsets from a step of its seed % 6 down,
    // row 0 for a north-south head (MonPlace 10), row 1 else.
    bool tentacle_ns = false;
    void party(int row, d2d::rules::Rng& own, int at_x, int at_y) {
        const auto* type = type_at(row);
        if (!type || type->minion[0] < 0) return;
        const int kinds = type->minion[1] >= 0 ? 2 : 1;
        if (type->base == int(game_data.mon_bin[0x105])) {
            static constexpr std::array<std::pair<int, int>, 12> kSpread = { { { -1, -4 }, { 1, 4 }, { 1, -3 }, { -1, 3 }, { 0, 2 }, { 0, -2 },
                                                                                { -3, -1 }, { 3, 1 }, { 2, -1 }, { -2, 1 }, { 1, 0 }, { -1, 0 } } };
            const int count = own.range(type->party_min, type->party_max);
            int turn = int(own.next() % 6);
            for (int i = 0; i < count; ++i) {
                const auto [dx, dy] = kSpread[std::size_t((tentacle_ns ? 0 : 6) + turn)];
                turn = (turn + 5) % 6;
                if (int to_x, to_y; put(type->minion[0], at_x + dx, at_y + dy, -1, false, to_x, to_y)) made(type->minion[0], to_x, to_y);
            }
            return;
        }
        for (int i = 0, count = own.range(type->party_min, type->party_max); i < count; ++i)
            if (int to_x, to_y; put(type->minion[std::size_t(i % kinds)], at_x, at_y, 4, false, to_x, to_y)) made(type->minion[std::size_t(i % kinds)], to_x, to_y);
    }

    // FUN_005b2a00 at a preset monster's spot (FUN_0054e490), then its party.
    // A superunique (FUN_005a49b0): once a game unless Stacks, at a random
    // spot of the room with AutoPos (FUN_0054dc40), else radius 5; then
    // MinGrp..MaxGrp of minion1 (else its own type) at radius 3 (FUN_005a0c00).
    // `pack` (FUN_0054e600, MonPlace 2 / 3, `row` picked off the region):
    // 2 a unique pack (FUN_005a43e0): made at a random spot of the room
    // (FUN_005a09e0, then FUN_005b2f20 there), a unique (FUN_005a0760, no
    // champion roll: its mods on its own seed), 3..6 minions at radius 3
    // (FUN_005a0c00, own seed); 3 a champion (at the spot) and own(3) + 1 more at radius 4 (FUN_0054e1e0).
    void monster(int row, int x, int y, int retry, int superunique = -1, int pack = 0) {
        using d2d::rules::Boss;
        const auto& types = game_data.monsters.types;
        const auto* sup = superunique >= 0 ? &game_data.superuniques[std::size_t(superunique)] : nullptr;
        if (sup) {
            if (sup->type < 0 || (!sup->stacks && superuniques.test(std::size_t(superunique)))) return;
            row = sup->type;
            retry = 5;
        }
        const auto first = spawns ? spawns->size() : 0;
        if (pack == 2) {
            if (spot(row, true, x, y) && put(row, x, y, -1, false, x, y)) {
                auto own = made(row, x, y);
                ++pop.uniques;                                          // FUN_005a0320: the count FUN_005be020 reads
                const auto& type = types[std::size_t(row)];
                auto boss = d2d::rules::roll_boss(game_data.umods, type, pop.difficulty, false, own, false);
                const int minion = type.minion[0] >= 0 ? type.minion[0] : row;
                for (int count = own(4) + 3; count > 0; --count)
                    if (int at_x, at_y; put(minion, x, y, 3, false, at_x, at_y)) { made(minion, at_x, at_y); tag(spawns->size() - 1, Boss::minion); }
                tag(first, Boss::unique, std::move(boss.mods), int(own.next() & 0xffff));
            }
            return;
        }
        if (sup && sup->autopos && !spot(row, false, x, y)) return;
        int spot_x, spot_y;
        if (!put(row, x, y, -1, false, spot_x, spot_y) && !(retry > 0 && put(row, x, y, retry, false, spot_x, spot_y))) return;
        auto own = made(row, spot_x, spot_y);
        if (sup) {
            superuniques.set(std::size_t(superunique));
            if (!d2d::rules::act_boss(row, types[std::size_t(row)].base)) ++pop.uniques;   // FUN_005a0320 as a boss, unless made() did
            // Its mods, `difficulty` more unique ones on its own seed (FUN_005a0600).
            auto mods = d2d::rules::superunique_mods(game_data.umods, types[std::size_t(row)], sup->mods, pop.difficulty, own);
            mods.push_back(22);                                         // questcomplete
            tag(first, Boss::superunique, std::move(mods));
            if (spawns && first < spawns->size()) (*spawns)[first].super = superunique;
            const int minion = types[std::size_t(row)].minion[0] >= 0 ? types[std::size_t(row)].minion[0] : row;
            const int more = sup->min_grp && sup->max_grp ? pop.difficulty : 0;   // each + difficulty when both are set
            for (int count = own(sup->max_grp - sup->min_grp + 1) + sup->min_grp + more; count > 0; --count)   // FUN_005a0c00: a draw even when MinGrp == MaxGrp
                if (int at_x, at_y; put(minion, spot_x, spot_y, 3, false, at_x, at_y)) { made(minion, at_x, at_y); tag(spawns->size() - 1, Boss::minion); }
            own.next();                                                 // FUN_005a0ce0: its name seed (data +0x14), unused for a superunique
            // FUN_005a49b0 by its hcIdx, the units it makes (each FUN_005b23c0
            // round it, in its group: FUN_005b24e0 count times): Radament's (10)
            // rand(5) + 2 skeleton5s on his own seed, then a skeleton mage of each
            // element (276, 382, 385, 389), radius 4; Shenk's (0x2a) 20 minion1s
            // within 20; Nihlathak's (0x3c) 20 of the level's own minion
            // (FUN_0063ec70) within 10; Baal Subject 2's (0x3e) 10 skmage_cold3s
            // within 20. The other cases bind quests and states (the world's
            // quests, ai.cpp's Countess).
            std::vector<int> company;
            int radius = 4;
            if (superunique == 10) {
                company.assign(std::size_t(own(5) + 2), int(game_data.mon_bin[4]));
                for (const int hc_idx : { 0x114, 0x17e, 0x181, 0x185 }) company.push_back(int(game_data.mon_bin[std::size_t(hc_idx)]));
            } else if (superunique == 0x2a) {
                company.assign(20, int(game_data.mon_bin[0x1c5])); radius = 20;
            } else if (superunique == 0x3c) {
                company.assign(20, level_monster(game_data, level, int(game_data.mon_bin[0x1c5]))); radius = 10;
            } else if (superunique == 0x3e) {
                company.assign(10, int(game_data.mon_bin[0x17d])); radius = 20;
            }
            for (const int company_row : company)
                if (int at_x, at_y; put(company_row, spot_x, spot_y, radius, false, at_x, at_y)) made(company_row, at_x, at_y);
            return;
        }
        party(row, own, spot_x, spot_y);
        if (pack == 3) {                                                // each champion through FUN_005b2f20: its own party
            tag(first, Boss::champion, { d2d::rules::umod::champion });
            ++pop.uniques;                                              // FUN_005a48c0 → FUN_005a0320, each champion
            own.next();                                                 // FUN_005a48c0 → FUN_005a0c00: a champion's minion count, rolled and unused
            for (int count = own(3) + 1; count > 0; --count)
                if (int at_x, at_y; put(row, spot_x, spot_y, 4, false, at_x, at_y)) {
                    const auto spawn_index = spawns ? spawns->size() : 0;
                    auto next = made(row, at_x, at_y);
                    tag(spawn_index, Boss::champion, { d2d::rules::umod::champion });
                    ++pop.uniques;
                    party(row, next, at_x, at_y);
                }
        }
    }

    // FUN_0054ec90 (FUN_0054ebc0 counting the room first): per area of the
    // room (id set, not skipped, not empty) (h / 3) * (w / 3) rolls of the
    // game seed against the density; a hit picks a type by rarity
    // (FUN_005bde80: a placespawn type, the crow nests, becomes its spawn when
    // a roll beats 20) and rolls unique or group (FUN_005be020), all on the room seed.
    void populate(bool none) {
        ++pop.rooms_done;
        const int density = std::min(level.mon.density[std::size_t(pop.difficulty)], 10000);
        if (none || density <= 0 || pop.rooms_total == 0 || index >= level.room_areas.size()) return;
        for (const auto& room_area : level.room_areas[index]) {
            if (!room_area.id || room_area.skip || (!room_area.left && !room_area.right)) continue;
            area = { room_area.left * 5, room_area.top * 5, (room_area.right - room_area.left) * 5, (room_area.bottom - room_area.top) * 5, room_area.id };
            for (int tries = (area.h / 3) * (area.w / 3); tries > 0; --tries) {
                if (int(game.next() % 100000) > density) continue;
                if (region.types.empty()) { area = {}; return; }
                const int type = pick(20);
                bool boss = false;                                      // FUN_005be020: its 1 and 2 both a group
                if (pop.uniques < pop.umin) boss = seed(100) < pop.rooms_done * 100 / pop.rooms_total;
                if (!boss && pop.uniques < pop.umax) boss = seed(100) < 6;
                if (!boss) (void)seed(100);
                if (boss) unique();
                else group(type);
            }
        }
        area = {};
    }
    // FUN_005a43e0 from FUN_0054ec90: normal's pick is a Levels umon
    // (FUN_005bde80), made at a random spot of the area (FUN_005a09e0, no
    // party), counted (FUN_005a0320), champion or unique on its own seed
    // (FUN_005a0760); a unique's 3..6 minions at radius 3 (FUN_005a0c00),
    // a champion's 1..3 more champions at radius 4, each with its party (FUN_0054e1e0).
    // FUN_005bde80: a type by rarity off the region; a placespawn type (the
    // crow nests) becomes its spawn when a roll beats `beat` (20 for a
    // group, 0 for a unique: FUN_0054ec90's two calls).
    int pick(int beat) {
        int type = d2d::rules::pick_type(region, seed);
        if (const auto* info = type_at(type); info && info->place_spawn >= 0 && seed(100) > beat) type = info->place_spawn;
        return type;
    }
    // NM / hell's unique pick is the same (FUN_005bde80's unique flag only
    // reads umon in normal).
    void unique() {
        using d2d::rules::Boss;
        const auto& umon = level.mon.umon;
        const int row = pop.difficulty > 0 ? pick(0) : umon.empty() ? 0 : umon[std::size_t(seed(int(umon.size())))];
        if (!type_at(row)) return;
        leader = int(spawns->size());
        int x, y;
        if (!spot(row, true, x, y) || !put(row, x, y, -1, true, x, y)) return;
        const auto first = spawns->size();
        auto own = made(row, x, y);
        ++pop.uniques;
        auto boss = d2d::rules::roll_boss(game_data.umods, *type_at(row), pop.difficulty, true, own, false);
        if (boss.kind == Boss::champion) {
            tag(first, Boss::champion, std::move(boss.mods), boss.name_seed);
            for (int count = own(3) + 1; count > 0; --count)
                if (int at_x, at_y; put(row, x, y, 4, true, at_x, at_y)) {
                    const auto spawn_index = spawns->size();
                    auto next = made(row, at_x, at_y);
                    tag(spawn_index, Boss::champion, { d2d::rules::umod::champion });
                    ++pop.uniques;
                    party(row, next, at_x, at_y);
                }
            return;
        }
        const int minion = type_at(row)->minion[0] >= 0 ? type_at(row)->minion[0] : row;   // FUN_005a0bb0
        for (int count = own(4) + 3; count > 0; --count)
            if (int at_x, at_y; put(minion, x, y, 3, true, at_x, at_y)) { made(minion, at_x, at_y); tag(spawns->size() - 1, Boss::minion); }
        tag(first, Boss::unique, std::move(boss.mods), int(own.next() & 0xffff));   // FUN_005a2120: rndname after the minions
    }
    // FUN_0054df80: MinGrp..MaxGrp (1..1 for Fallen and scarabs,
    // FUN_0054ec40), sparsePopulate on the game seed, a random spot of the
    // area, the leader and its party, then rand(max - min + 1) + min - 1
    // more of its type at radius 3 in the area, each with its party. A
    // leader of class 0x210 (the evil hut, its MonType bit 0x12 set) gets
    // object 562 (Dummy, invisible: a 5x5 block in mode 0) on its spot
    // after its party (FUN_00555230 type 2).
    void group(int row) {
        const auto* type = type_at(row);
        if (!type) return;
        int low = type->min_grp, high = type->max_grp;
        if (type->base == 19 || type->base == 91) low = high = 1;
        if (type->sparse && type->sparse < int(game.next() % 100)) return;
        if (!low || !high || low > high) return;
        leader = int(spawns->size());
        int x, y;
        if (!spot(row, true, x, y) || !put(row, x, y, -1, true, x, y)) return;
        auto own = made(row, x, y);
        party(row, own, x, y);
        if (row == int(game_data.mon_bin[0x210])) make(562, x, y, false);
        for (int extra = own(high - low + 1) + low - 1; extra > 0; --extra)
            if (int at_x, at_y; put(row, x, y, 3, true, at_x, at_y)) { auto next = made(row, at_x, at_y); party(row, next, at_x, at_y); }
    }

    // A preset past objects.txt (FUN_0054f490's table at 0x731d28): 574-579 a
    // shrine (136), 580 / 581 a random chest (FUN_0054f370 a sparkling one,
    // FUN_0054f180), 582 a quest's (FUN_0059d830): the Summoner's six tomb
    // symbols (FUN_0059d720), the table at 0x738fac less the true tomb's, in
    // turn, round again after six.
    // ponytail: 574-579's kind (their range, on the unit's own seed) and
    // 582's quest check aren't taken.
    int special(int id) {
        if (id < 580) return 136;
        if (id == 582) {
            static constexpr std::array<int, 7> kSymbols{ 313, 312, 308, 310, 311, 309, 307 };   // 0x738fac, by tomb 66..72
            auto act = d2d::drlg::act_seed(game_data.map_seed);                                // FUN_0061aeb0: act +0x94
            const int true_tomb = d2d::drlg::roll_act2_tombs(act).boss - d2d::rules::level_ids::kFirstTalRashasTomb;
            std::vector<int> shown;
            for (int tomb = 0; tomb < 7; ++tomb) if (tomb != true_tomb) shown.push_back(kSymbols[std::size_t(tomb)]);
            if (shown.size() != 6) return 0x133;                                                 // no true tomb: FUN_0059d720's fallback
            return shown[std::size_t(tomb_symbols++ % 6)];
        }
        const int act = d2d::rules::level_ids::act_of(level.id);   // FUN_006427f0
        const auto low = rgn.next();
        int pick;
        if (act == 1) pick = level.id == d2d::rules::level_ids::kArcaneSanctuary ? std::array{ 0x183, 0x185, 0x186, 0x187 }[low & 3] : std::array{ 0x57, 0x58 }[low & 1];
        else if (act == 2) pick = level.id == d2d::rules::level_ids::kTravincal ? std::array{ 0x149, 0x14a, 0x14b, 0x14c }[low & 3] : std::array{ 0xb5, 0xb7 }[low & 1];
        else pick = std::array{ 5, 6, 0x8b, 0x8c, 0x8d, 0x90, 0xb0, 0xb1, 0xc6, 0xf0, 0xf1, 0xf2, 0xf3 }[low % 13];
        return id == 580 && level.id == d2d::rules::level_ids::kTowerCellarLevel5 ? 0x173 : pick;
    }
    // A spawn made in mode 12 (unit +0xc4 |= 0x2000000): a corpse, its collision gone.
    void lay_dead(d2d::rules::Spawn& body) {
        body.dead = true;
        const auto* info = type_at(body.type);
        const int shape = info && !info->footprint ? 0 : size_of(body.type) == 3 ? 3 : 2;
        stamp_shape(body.x, body.y, shape, 0x100, true);
        stamp_shape(body.x, body.y, shape - 1, 0x1000, true);
    }
    // MonStats `hc_idx` made at the unit's spot (else within `retry`); an
    // NPC (Flavie, the towns') stands where it was made and walks the unit's
    // DS1 path (`walks`: a marker's unit (FUN_005b2f20) takes none).
    void monster_or_npc(int hc_idx, const d2d::drlg::Unit& unit, int retry, bool walks = true) {
        const auto row = game_data.mon_bin[std::size_t(hc_idx)];
        if (spawns) leader = int(spawns->size());                                              // a placement of its own (FUN_005b2f20)
        last_made.reset();
        monster(int(row), unit.x, unit.y, retry);
        if (!game_data.mon_is_npc[row] || game_data.mon_npc[row].code.empty()) return;
        auto npc = game_data.mon_npc[row];
        const auto [spot_x, spot_y] = last_made ? last_spot : std::pair{ unit.x, unit.y };     // where it was made: its spot, or the free one nearest
        npc.x = (float(spot_x) + 0.5f) / 5;
        npc.y = (float(spot_y) + 0.5f) / 5;
        for (std::size_t k = 0; walks && k < unit.path.size(); ++k) {                          // its DS1 path (the town AIs walk it)
            npc.path.emplace_back((float(unit.x + unit.path[k].first) + 0.5f) / 5, (float(unit.y + unit.path[k].second) + 0.5f) / 5);
            npc.actions.push_back(k < unit.actions.size() ? unit.actions[k] : 0);
        }
        // Its unit seed after its init's draws (the look, the life: made()'s), what its think rolls on (town-npcs.md).
        if (last_made) npc.seed = *last_made;
        level.npcs.push_back(std::move(npc));
    }
    // FUN_005559a0: the room's preset units, the monsters last (`monsters`: normal's, here).
    // The uber levels leave some out: Matron's Den / the Furnace of Pain the
    // sparkly chest (397); the Forgotten Sands its waypoint (402) and the
    // (map seed % 3 + 3)th other object; Uber Tristram Cain's gibbet (26),
    // Wirt's body (268), the gold placeholder (269) and every monster.
    [[nodiscard]] bool uber_skips(const d2d::drlg::Unit& unit) {
        if (unit.type != d2d::rules::unit_type::kObject) return false;
        switch (level.id) {
        case d2d::rules::level_ids::kUberTristram: return unit.id == 26 || unit.id == 268 || unit.id == 269;
        case d2d::rules::level_ids::kMatronsDen: case d2d::rules::level_ids::kFurnaceOfPain: return unit.id == 397;
        case d2d::rules::level_ids::kForgottenSands: return unit.id == 402 || ++sands_objects == int(game_data.map_seed % 3 + 3);
        default: return false;
        }
    }
    void presets(std::size_t room_at) {
        const int nmon = int(game_data.mon_bin.size()), nsu = int(game_data.superuniques.size());
        for (std::size_t i = 0; i < level.units.size(); ++i) {
            const auto& unit = level.units[i];
            if (level.unit_rooms[i] != int(room_at) || unit.type == d2d::rules::unit_type::kMonster || (unit.flags & 1) || uber_skips(unit)) continue;
            if (unit.type == d2d::rules::unit_type::kObject && unit.id != 0x23d) {
                const auto before = level.npcs.size();
                make(unit.id > 0x23d ? special(unit.id) : unit.id, unit.x, unit.y, false);
                // FUN_0054f370: FUN_005540a0(unit, 1); in the Maggot Lair (62-64) only 51 in 100, an object-seed draw after it's made.
                const bool lair = level.id >= d2d::rules::level_ids::kMaggotLairLevel3 - 2 && level.id <= d2d::rules::level_ids::kMaggotLairLevel3;
                if (unit.id == 580 && level.npcs.size() > before && (!lair || rgn.next() % 100 <= 50)) level.npcs.back().sparkle = true;
                // The docks' markers, each making its NPC there as the game's quest
                // words stand: InitFn 49 (FUN_005b70b0) Hratli (0xfd) by the dock
                // till word 0x10 has bit 13, InitFn 50 (FUN_005b7160) at his shop
                // after; InitFn 52 (FUN_005bce80, object 0x17e: the DS1's Natalya,
                // units.hpp) Natalya (0x129), else within 3, till word 0x16 has bit 13.
                // ponytail: their quest records' "made" bits aren't kept: a room comes up once.
                const int init = obj(unit.id, "InitFn");
                const bool hratli_moved = d2d::rules::quest::bit(game_quests, 0x10, 13);
                if ((init == 49 && !hratli_moved) || (init == 50 && hratli_moved)) monster_or_npc(0xfd, unit, 0, false);
                if (init == 52 && !d2d::rules::quest::bit(game_quests, 0x16, 13)) monster_or_npc(0x129, unit, 3, false);
                // InitFn 43 (FUN_005bd1f0, a3q7, object 368 by the jungle's head): the Dark
                // Wanderer (0x170) 7 subtiles east of it, its spot only; a3q7's init (FUN_005bd300)
                // leaves him due in every game.
                // His walk: ai.cpp's DarkWanderer.
                if (init == 43) { auto east = unit; east.x += 7; monster_or_npc(0x170, east, 0, false); }
                // Harrogath's markers (a new game's quest records): InitFn 71 (FUN_00587840,
                // object 543, record 0x1f) Larzuk (0x1ff) within 5 of it; InitFn 68
                // (FUN_0058a610 -> FUN_005459a0, object 461, record 0x21) Nihlathak (0x202)
                // at its spot, else within 5, till he's gone (record +0x88).
                // ponytail: FUN_005459a0's FUN_00545340 free-spot pass and the records' "made"
                // bytes aren't kept. InitFns 66 / 67 / 69 are Act 5's quests'
                // (world_quests_act5.cpp); 70 (FUN_00587830, Larzuk Greeting) is empty.
                if (init == 71) monster_or_npc(0x1ff, unit, 5, false);
                if (init == 68) monster_or_npc(0x202, unit, 5, false);
            }
            else if (unit.type == d2d::rules::unit_type::kWarp) game.next();                                                          // a warp tile: a unit made
        }
        // FUN_0063ec70 + FUN_0054e2a0: a base monster as the level has it (as populate()'s own()).
        const auto& types = game_data.monsters.types;
        auto own = [&](int base_bin) {
            const int row = level_monster(game_data, level, int(game_data.mon_bin[std::size_t(base_bin)]));
            const int base = types[std::size_t(row)].base, id = level.id;
            auto bin = [&](int bin_row) { return int(game_data.mon_bin[std::size_t(bin_row)]); };
            if (base == bin(0x13)) return id == 6 ? bin(0x14) : id == 7 || id == 12 || id == 16 ? bin(0x15) : row;
            if (base == bin(0x3a)) return id == 6 || id == 7 ? bin(0x3b) : id == 12 || id == 16 ? bin(0x3c) : row;
            return row;
        };
        for (std::size_t i = 0; i < level.units.size(); ++i) {
            const auto& unit = level.units[i];
            if (level.unit_rooms[i] != int(room_at) || unit.type != d2d::rules::unit_type::kMonster || (unit.flags & 1) || unit.id < 0 || level.id == d2d::rules::level_ids::kUberTristram) continue;
            if (spawns) leader = int(spawns->size());                                                  // FUN_00555910: a placement of its own
            if (unit.id < nmon) {
                // FUN_0054e600 on a MonStats row: in the Bloody Foothills with
                // quest 0x1f still open (its record's +9, FUN_005444b0: off once
                // a player joins with it done, bit 15) 0x1f2 / 0x205 come as
                // 0x1f3 / 0x206; past normal 0x1c5 / 0x211 there aren't made.
                int hc_idx = unit.id;
                const bool foothills = level.id == d2d::rules::level_ids::kBloodyFoothills, siege_open = !d2d::rules::quest::bit(game_quests, 0x1f, 15);
                if (foothills && siege_open && hc_idx == 0x1f2) hc_idx = 0x1f3;
                else if (foothills && siege_open && hc_idx == 0x205) hc_idx = 0x206;
                if (foothills && pop.difficulty > 0 && (hc_idx == 0x1c5 || hc_idx == 0x211)) continue;
                // FUN_0054e490: a critter (MonStats2 flag 0xd: Harrogath's chickens) is never made.
                if (const auto* info = type_at(int(game_data.mon_bin[std::size_t(hc_idx)])); info && info->critter) continue;
                const bool stay = hc_idx == 0xe5 || (hc_idx >= 0x11c && hc_idx <= 0x120) || hc_idx == 0x188 || hc_idx == 0x189;   // FUN_0054e3a0
                // FUN_0054e490: the prison door (0x1b2) a subtile west, and
                // dead (mode 12) once quest 0x20's record is shut (+9 off).
                if (hc_idx == 0x1b2) {
                    auto door = unit;
                    --door.x;
                    const auto before = spawns ? spawns->size() : 0;
                    monster_or_npc(hc_idx, door, 4);   // never a stay one (FUN_0054e3a0's list)
                    if (d2d::rules::quest::bit(game_quests, 0x20, 15) && spawns && spawns->size() > before) lay_dead(spawns->back());
                    continue;
                }
                last_made.reset();
                monster_or_npc(hc_idx, unit, stay ? 0 : 4);
                // FUN_0054e490, the barricade doors made (their MonType bit 0x12):
                // 0x1b0 object 571, 0x1b1 object 572 on their spot (FUN_00555230, mode 0).
                if (last_made && (hc_idx == 0x1b0 || hc_idx == 0x1b1)) make(hc_idx == 0x1b0 ? 571 : 572, last_spot.first, last_spot.second, false);
            } else if (const int code = unit.id - nmon - nsu; code == 0x11 || code == 0x12 || code == 0x16 || code == 0x17 || code == 0x05) {
                // Fallen, shamans, fetishes, fetish shamans (FUN_0054e600's 0x54eb74: the level's of that base), Blood Raven
                const int base = code == 0x11 ? 0x13 : code == 0x12 ? 0x3a : code == 0x16 ? 0x8d : 0x116;
                monster(code == 0x05 ? int(game_data.mon_bin[0x10b]) : own(base), unit.x, unit.y, code == 0x05 ? 0 : 4);
            }
            else if (code == 10 || code == 11) {                                                       // a tentacle head (0x105, FUN_0054da60):
                const int depth = level.id >= 0x4c && level.id <= 0x4e ? 1 : level.id >= 0x5c && level.id <= 0x5d ? 2 : 0;   // 0x54eb60 by level
                int row = int(game_data.mon_bin[0x105]);
                for (int step = 0; step < depth && types[std::size_t(row)].next_in_class >= 0; ++step) row = types[std::size_t(row)].next_in_class;   // NextInClass
                tentacle_ns = code == 10;
                monster(row, unit.x, unit.y, 0);                                                         // its spot only; NS (10) in mode 4
                tentacle_ns = false;
            }
            else if (code >= 0x1d && code <= 0x20) {
                // Act 5's dead (FUN_0054e600 0x1d..0x20): the level's minion (0x1c5), imp (0x211 in
                // the Bloody Foothills, else 0x1ec), barbarian (0x20a), reanimated horde (0x1b6),
                // its spot then radius 4, made in mode 12 (unit +0xc4 |= 0x2000000).
                // ponytail: a reanimated horde's rise (rand(50) on its seed, FUN_005417d0 event 7 at
                // frame + 250 + that) isn't kept: it lies there.
                const int base = code == 0x1d ? 0x1c5 : code == 0x1e ? (level.id == d2d::rules::level_ids::kBloodyFoothills ? 0x211 : 0x1ec) : code == 0x1f ? 0x20a : 0x1b6;
                const auto before = spawns ? spawns->size() : 0;
                monster(own(base), unit.x, unit.y, 4);
                if (spawns && spawns->size() > before) lay_dead(spawns->back());
            }
            else if (code == 0x18 || code == 0x1a) {
                // FUN_0054e090 (normal only): the level's imp (0x18) / minion (0x1a) at the
                // marker's spot, then rand(MaxGrp - MinGrp + 1) + MinGrp - 1 more on its seed at radius 3.
                const int row = own(code == 0x1a ? 0x1c5 : level.id == d2d::rules::level_ids::kBloodyFoothills ? 0x211 : 0x1ec);
                const auto* type = type_at(row);
                int at_x, at_y;
                if (pop.difficulty == 0 && type && type->min_grp && type->max_grp && type->min_grp <= type->max_grp && put(row, unit.x, unit.y, -1, false, at_x, at_y)) {
                    auto leader_seed = made(row, at_x, at_y);
                    for (int count = leader_seed(type->max_grp - type->min_grp + 1) + type->min_grp - 1; count > 0; --count)
                        if (int to_x, to_y; put(row, at_x, at_y, 3, false, to_x, to_y)) made(row, to_x, to_y);
                }
            }
            else if ((code == 0x02 || code == 0x03) && pop.difficulty > 0) {                             // FUN_005bde80 (1, 0): NM / hell's off the region
                if (!region.types.empty()) monster(pick(0), unit.x, unit.y, 0, -1, code);
            }
            else if ((code == 0x02 || code == 0x03) && !level.mon.umon.empty())                         // normal's pick is a umon
                monster(level.mon.umon[std::size_t(seed(int(level.mon.umon.size())))], unit.x, unit.y, 0, -1, code);
            else if (unit.id < nmon + nsu) {
                monster(-1, unit.x, unit.y, 0, unit.id - nmon);
                if (std::size_t(leader) < spawns->size())                                              // FUN_00555910: the preset's map AI
                    for (const auto& [off_x, off_y] : unit.path) (*spawns)[std::size_t(leader)].path.emplace_back(unit.x + off_x, unit.y + off_y);
            }
        }
    }

    // FUN_00552560: whether the room gets groups, counting it.
    bool open(std::size_t room_at, int room_themes) {
        const auto flags = level.room_flags[room_at];
        if ((flags & 0x30000) || (flags & 0x800000) || (room_at < level.road_rooms.size() && level.road_rooms[room_at])) return false;   // FUN_0066ba90: a plain room on a path
        if (target == 0x7fffffff) target = int(std::ranges::count_if(level.room_flags, [](std::uint32_t room_flag) { return !(room_flag & 0x800000); }));
        ++counter;
        if (room_themes) {                                                                              // FUN_00552400
            // A theme's own fn (0x731e6c) never runs: FUN_00552400 takes the
            // pick only when its number (bit + 1) is under the mask's count, and
            // no Levels.txt mask (8, 60, 48) passes with one that has a fn
            // (bugs.md #37); only its object-seed rolls are taken.
            if (target < 0) return true;
            const int roll = int(rgn.next() % 100);
            int odds = counter > target / 2 ? 5 : 0;
            if (counter > target - target / 4) odds += 5;
            if (roll < odds + 12 && std::popcount(unsigned(room_themes & 0x7f)) > 0) rgn(std::popcount(unsigned(room_themes & 0x7f)));
        }
        return true;
    }
    // FUN_00552610
    void groups() {
        for (std::size_t i = 0; i < 8; ++i) {
            const int group_id = level.mon.obj_group[i];
            int roll = int(seed.next() % 100);
            if (target > 0 && (counter << 7) / target > 0x60 && refills == 0 && obj(group_id, "SubClass")) roll = 100;   // objects.txt by the group's id
            if (group_id == 0 || roll > level.mon.obj_prob[i] || std::size_t(group_id) >= game_data.obj_groups.size()) continue;
            const auto& group = game_data.obj_groups[std::size_t(group_id)];
            const int pick = int(seed.next() % 100);
            for (int j = 0, sum = 0; j < 8 && group.id[std::size_t(j)]; ++j) {
                if (pick >= (sum += group.weight[std::size_t(j)])) continue;
                const int id = group.id[std::size_t(j)], density = group.density[std::size_t(j)];
                switch (obj(id, "PopulateFn")) {
                case 1: cluster(id, density, 100); break;
                case 2: shrine(id, 100); break;
                case 3: scatter(id, density, 100); break;
                case 4: barrels(density, 100); break;
                case 6: companion(scatter(id, density, 100)); break;
                case 7: layout(id, 100); break;
                case 8: well(id, 100); break;
                case 9: single(id, density, 100); break;
                default: break;
                }
                break;
            }
        }
    }
};

namespace {

// The level's ObjectRooms, made the first time one of its rooms comes up:
// FUN_00552560's theme odds, where players come in (FUN_0054db50), the
// region's room counts (FUN_00642be0).
ObjectRooms& rooms_of(const GameData& game_data, Spawning& spawning, const Level& level) {
    auto& state = spawning.levels[&level];
    if (state.objects) return *state.objects;
    const auto& builder = *game_data.builder;
    state.objects = std::make_shared<ObjectRooms>(ObjectRooms{ game_data, builder, const_cast<Level&>(level) });   // GameData owns its levels mutable
    auto& pop = *state.objects;
    pop.width = level.ds1.width() * 5;
    pop.height = level.ds1.height() * 5;
    pop.stamps.assign(std::size_t(pop.width) * std::size_t(pop.height), 0);
    state.group_rooms.assign(level.rooms.size(), {});
    state.room_seeds.assign(level.rooms.size(), {});
    state.early_seeds.assign(level.rooms.size(), std::nullopt);
    int position = 0;
    std::uint32_t slots = 0, shared = 0;                                                           // 0x10 << slot with a Warp (FUN_0066af30); its Vis another slot's too
    for (std::size_t row = 0; row < builder.levels.size(); ++row)
        if (std::atoi(std::string(builder.levels.get(row, "Id")).c_str()) == level.id) {
            pop.themes = std::atoi(std::string(builder.levels.get(row, "Themes")).c_str());
            pop.warp_dist = std::atoi(std::string(builder.levels.get(row, "WarpDist")).c_str());
            position = std::atoi(std::string(builder.levels.get(row, "Position")).c_str());
            int vis[8];
            for (int k = 0; k < 8; ++k) {
                vis[k] = std::atoi(std::string(builder.levels.get(row, "Vis" + std::to_string(k))).c_str());
                if (std::atoi(std::string(builder.levels.get(row, "Warp" + std::to_string(k))).c_str()) != -1) slots |= 0x10u << k;
                for (int j = 0; j < k; ++j)
                    if (vis[k] && vis[j] == vis[k]) shared |= 0x10u << k | 0x10u << j;
            }
        }
    // The level's own warp table (FUN_0066aec0: its +0x90 override, the
    // Blood Moor's one Den of Evil way in of Warp3..6) keeps one of the slots
    // sharing a Vis: the one that placed a warp.
    // ponytail: read off the warps built, not game.exe's override list.
    std::uint32_t warped = 0;
    for (const auto& warp : level.warps) if (warp.slot >= 0 && warp.slot < 8) warped |= 0x10u << warp.slot;
    slots &= ~shared | warped;
    // LAB_00642480: the centres of rooms with a warp slot's wall (a walk-through exit has no warp).
    for (std::size_t i = 0; i < level.rooms.size(); ++i)
        if (level.room_flags[i] & slots) pop.ways.push_back({ (level.rooms[i].x + level.rooms[i].width / 2) * 5, (level.rooms[i].y + level.rooms[i].height / 2) * 5 });
    auto centre = [&](int tile_x, int tile_y) {                                                    // the centre of the room holding a tile
        for (const auto& made : level.rooms)
            if (tile_x >= made.x && tile_y >= made.y && tile_x < made.x + made.width && tile_y < made.y + made.height) {
                pop.ways.push_back({ (made.x + made.width / 2) * 5, (made.y + made.height / 2) * 5 });
                return;
            }
    };
    for (const auto& unit : level.units)
        if (unit.type == d2d::rules::unit_type::kObject && (pop.obj(unit.id, "SubClass") & d2d::rules::object_ids::kSubclassWaypoint)) {                            // a waypoint: its room and its tile
            centre(unit.x / 5, unit.y / 5);
            if (!position) pop.ways.push_back({ unit.x / 5 * 5, unit.y / 5 * 5 });   // the type 0xb tile FUN_0054db50 tests: a Position level's is its own, below
        }
    // FUN_0066b2b0 (type 0xb): with neither a waypoint room nor a warp room,
    // the start spot is the centre of the room holding the level's middle less 2.
    // ponytail: no room there (FUN_0066ae70's pick) adds none.
    // A Levels Position level (Tristram) takes its type 0xb tile instead (FUN_0066ac40).
    // ponytail: the first; with more game.exe rolls the level seed for one each check.
    if (position && !level.starts.empty()) pop.ways.push_back({ level.starts.front().first * 5, level.starts.front().second * 5 });
    else if (pop.ways.empty()) centre(level.ds1.width() / 2 - 2, level.ds1.height() / 2 - 2);
    int total = 0;
    for (std::size_t i = 0; i < level.rooms.size(); ++i) total += !ObjectRooms::none(level, i);
    const auto difficulty = std::size_t(std::clamp(spawning.difficulty, 0, 2));
    pop.pop = { 0, total, 0, level.mon.umin[difficulty], level.mon.umax[difficulty], int(difficulty), &game_data.umods };
    return pop;
}

}  // namespace

// FUN_0054ebc0: none in a room flagged 0x800000 or nopop; the level's total (FUN_00642be0) leaves them out.
bool ObjectRooms::none(const Level& level, std::size_t room_index) {
    return (room_index < level.nopop_rooms.size() && level.nopop_rooms[room_index]) || (level.room_flags[room_index] & 0x800000);
}

// The game's seeds in for room `room_index`, the level's rooms up so far.
void ObjectRooms::load(Spawning& spawning, std::size_t room_index) {
    rgn = spawning.objects;
    game = spawning.game;
    superuniques = spawning.superuniques;
    rescue_cages = spawning.rescue_cages;
    tomb_symbols = spawning.tomb_symbols;
    game_quests = spawning.game_quests;
    orb_smashed = spawning.orb_smashed;
    region = std::size_t(level.id) < spawning.regions.size() ? spawning.regions[std::size_t(level.id)] : d2d::rules::Region{};
    auto& state = spawning.levels[&level];
    rooms_up(spawning);
    others.clear();
    for (auto& [other, other_state] : spawning.levels)
        if (other != &level && other_state.objects) {
            other_state.objects->rooms_up(spawning);
            others.push_back(other_state.objects.get());
        }
    index = room_index;
    const auto& made = level.rooms[index];
    room = { made.x * 5, made.y * 5, made.width * 5, made.height * 5 };
    out = &state.group_rooms[index];
    spawns = &state.spawns;
    level_state = &state;
}

// in_room: the level's rooms up so far.
void ObjectRooms::rooms_up(const Spawning& spawning) {
    const auto tiles_wide = std::size_t(level.ds1.width());
    in_room.assign(tiles_wide * std::size_t(level.ds1.height()), false);
    const auto found = spawning.levels.find(&level);
    if (found == spawning.levels.end()) return;
    for (const auto came_up : found->second.order)
        if (came_up < level.rooms.size())
            for (int y = level.rooms[came_up].y; y < level.rooms[came_up].y + level.rooms[came_up].height && y < level.ds1.height(); ++y)
                for (int x = level.rooms[came_up].x; x < level.rooms[came_up].x + level.rooms[came_up].width && x < level.ds1.width(); ++x) in_room[std::size_t(y) * tiles_wide + std::size_t(x)] = true;
}

void ObjectRooms::save(Spawning& spawning) const {
    spawning.objects = rgn;
    spawning.game = game;
    spawning.superuniques = superuniques;
    spawning.rescue_cages = rescue_cages;
    spawning.tomb_symbols = tomb_symbols;
    if (std::size_t(level.id) < spawning.regions.size()) spawning.regions[std::size_t(level.id)] = region;
}

// At build time: the room flags FUN_00552560 reads. Every unit has a room:
// game.exe never makes a preset unit no room takes (drlg.md "Units off
// every room"), nor does level_room_tiles.
void place_objects(const GameData& game_data, GameData::LevelBuilder& builder, Level& level) {
    ObjectRooms pop{ game_data, builder, level };
    level.room_flags.resize(level.rooms.size());
    level.unit_rooms.resize(level.units.size(), -1);
    // 0x30000: a room with a waypoint (objects.txt SubClass 0x40, LAB_00667e30);
    for (std::size_t i = 0; i < level.units.size(); ++i)
        if (level.units[i].type == d2d::rules::unit_type::kObject && level.unit_rooms[i] >= 0 && (pop.obj(level.units[i].id, "SubClass") & d2d::rules::object_ids::kSubclassWaypoint))
            level.room_flags[std::size_t(level.unit_rooms[i])] |= 0x30000;
    // 0x800000 also: a room with a town room among its near rooms
    // (FUN_0066bd50 on the near list, +0x2c; FUN_006426a0: levels 1, 40, 75,
    // 103, 109) — the Blood Moor's by the camp, the Sewers' by Lut Gholein.
    // The near rooms: within 6 act tiles on both axes.
    std::array<int, 4> town{};                                    // its act's town: x, y, width, height (act tiles)
    bool has_town = false;
    if (level.id < d2d::rules::level_ids::kLutGholein) {
        if (level.id != d2d::rules::level_ids::kRogueEncampment && game_data.town.ds1.width() > 0) {
            town = { game_data.town.world_x, game_data.town.world_y, game_data.town.ds1.width(), game_data.town.ds1.height() };
            has_town = true;
        }
    } else if (const int town_id = d2d::rules::level_ids::kActTowns[std::size_t(d2d::rules::level_ids::act_of(level.id))]; level.id != town_id)
        for (const auto& placed : game_data.layout_of(level.id))
            if (placed.level == town_id) { town = { placed.x, placed.y, placed.width, placed.height }; has_town = true; }
    if (has_town) {
        const auto [town_x, town_y, town_w, town_h] = town;
        for (std::size_t i = 0; i < level.rooms.size(); ++i) {
            const auto& made = level.rooms[i];
            const int x = level.world_x + made.x, y = level.world_y + made.y;
            const int gap_x = x < town_x ? town_x - made.width - x : x - town_w - town_x, gap_y = y < town_y ? town_y - made.height - y : y - town_h - town_y;
            if (gap_x < 6 && gap_y < 6) level.room_flags[i] |= 0x800000;
        }
    }
}

d2d::rules::Rng room_objects(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t index) {
    if (index >= level.rooms.size() || !game_data.builder) return {};
    auto& pop = rooms_of(game_data, spawning, level);
    pop.load(spawning, index);
    auto& early = spawning.levels[&level].early_seeds;
    pop.seed = index < early.size() && early[index] ? *early[index] : d2d::rules::Rng{ index < level.room1_seeds.size() ? level.room1_seeds[index] : level.rooms[index].seed };
    // FUN_0054f060; on a step with its low 15 bits clear, a MonWndr level
    // rolls a wanderer (FUN_0054eff0: under 3 in 100).
    // ponytail: the wanderer itself (FUN_0054ef50) isn't made; 3 in 3.3M rooms.
    if ((pop.seed.next() & 0x7fff) == 0 && level.mon.wander) pop.seed.next();
    pop.presets(index);
    pop.out->pre = pop.seed.low;
    if (pop.open(index, pop.themes)) pop.groups();
    pop.out->post = pop.seed.low;
    pop.out->rgn = pop.rgn.low;
    pop.populate(ObjectRooms::none(level, index));
    spawning.levels[&level].room_seeds[index] = pop.seed;
    pop.save(spawning);
    return pop.seed;
}

std::vector<std::uint16_t> object_collision(const Spawning& spawning, const Level& level) {
    const auto found = spawning.levels.find(&level);
    if (found == spawning.levels.end() || !found->second.objects) return {};
    const auto& pop = *found->second.objects;
    std::vector<std::uint16_t> grid(std::size_t(pop.width) * std::size_t(pop.height));
    for (int y = 0; y < pop.height; ++y)
        for (int x = 0; x < pop.width; ++x) grid[std::size_t(y) * std::size_t(pop.width) + std::size_t(x)] = pop.at(x, y);
    return grid;
}

}  // namespace d2d::game
