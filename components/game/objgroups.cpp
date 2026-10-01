// A level's rooms populated as they come into play, as FUN_0052d160 walks
// the act's room1 list (newest first): each room's seed step
// (FUN_0054f060), its preset units (FUN_005559a0), then the random object
// groups (FUN_00552610 and the PopulateFns at 0x731d00;
// docs/research/re/objects.md "Random object groups per room") and its
// monsters, all on the game's one object seed and game seed. Proven
// against game.exe: tools/emu objgroups.py, drlg-dump <seed> <level>
// objgroups; levels in turn in one game: drlg-dump ... game.
#include "gamedata.hpp"
#include "log.hpp"
#include <monsters.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <txt.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2d::game {
namespace {

constexpr std::array kDx{ -1, 0, 1, -1, 1, -1, 0, 1 }, kDy{ -1, -1, -1, 0, 0, 1, 1, 1 };   // 0x731b7c, 0x731b9c

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
    std::bitset<128> superuniques;                      // made (game +0x1d30)
    d2d::rules::Region region;                          // the level's monster region: their looks
    Level::GroupRoom* out = nullptr;
    // FUN_0054db50: where players come in (as populate()'s near_way), within Levels WarpDist.
    std::vector<std::pair<int, int>> ways;
    int warp_dist = 2025;

    static bool none(const Level& level, std::size_t i);
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
        const auto at = std::size_t(y) * std::size_t(width) + std::size_t(x);
        return std::uint16_t((at < level.tile_walk.size() ? level.tile_walk[at] : 0) | stamps[at]);
    }
    // FUN_0064d800: a w x h area centred on (x, y) (FUN_0064ceb0 across the rooms).
    [[nodiscard]] bool hit(int x, int y, int w, int h, int mask) const {
        if (w < 2 && h < 2) { const auto flags = at(x, y); return flags == 0x27 || (flags & mask); }
        for (int ty = y - (h >> 1); ty < y - (h >> 1) + h; ++ty)
            for (int tx = x - (w >> 1); tx < x - (w >> 1) + w; ++tx)
                if (const auto flags = at(tx, ty); flags == 0x27 || (flags & mask)) return true;
        return false;
    }
    // A unit's collision shape (MonStats2 SizeX): 1 a subtile, 2 a plus (FUN_0064d100), 3+ a box.
    [[nodiscard]] bool hit_shape(int x, int y, int size, int mask) const {
        return size == 2 ? hit(x, y, 3, 1, mask) || hit(x, y - 1, 1, 1, mask) || hit(x, y + 1, 1, 1, mask) : hit(x, y, size, size, mask);
    }
    void stamp_shape(int x, int y, int size, std::uint16_t bits) {
        if (size == 2) { stamp(x, y, 3, 1, bits); stamp(x, y, 1, 3, bits); }
        else if (size > 0) stamp(x, y, size, size, bits);
    }
    void stamp(int x, int y, int w, int h, std::uint16_t bits) {
        for (int ty = y - (h >> 1); ty < y - (h >> 1) + h; ++ty)
            for (int tx = x - (w >> 1); tx < x - (w >> 1) + w; ++tx)
                if (tx >= 0 && ty >= 0 && tx < width && ty < height) stamps[std::size_t(ty) * std::size_t(width) + std::size_t(tx)] |= bits;
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
        const bool on = level.npcs.size() > before && level.npcs.back().preoperated;
        if (obj(id, on ? "HasCollision2" : "HasCollision0")) stamp(x, y, obj(id, "SizeX"), obj(id, "SizeY"), bits);
        const auto unit = game.next();                                 // FUN_00552df0: its seed
        if (level.npcs.size() > before) {
            auto& npc = level.npcs.back();
            if (obj(id, "InitFn") != 3 && obj(id, "InitFn") != 57) npc.seed = d2d::rules::Rng{ unit };
            npc.room = int(index);
        }
        if (obj(id, "InitFn") == 28) piles(gold, x, y);
        if (group) out->made.push_back({ id, x, y });
        return level.npcs.size() > before ? level.npcs.back().shrine : 0;
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
            const auto [at_x, at_y] = drop_spot(game_data.field, last_x, last_y, [&](int fx, int fy) { return at(fx, fy); });
            stamp(at_x, at_y, 1, 1, 0x200);
            game.next(); game.next();
        }
    }

    // FUN_00550220: clear of walls, objects and doors round a sx x sy object.
    [[nodiscard]] bool fits_wide(int x, int y, int sx, int sy) const {
        return sx + 2 <= room.w && sy + 2 <= room.h && room.x + 1 < x && room.y + 1 < y && x < room.x - sx - 2 + room.w
            && y < room.y - sy - 2 + room.h && !hit(x, y, sx + 7, sy + 7, 0xc01) && !hit(x, y, sx, sy, 0x3f11);
    }
    // FUN_005502f0
    [[nodiscard]] bool fits_near(int x, int y, int sx, int sy) const {
        return room.w > 1 && room.h > 1 && room.x + 2 < x && room.y + 2 < y && x < room.x - sx + room.w && y < room.y - sy + room.h
            && !hit(x, y, sx + 2, sy + 2, 0x3f11);
    }
    [[nodiscard]] bool inside(int x, int y) const {
        return room.x + 1 <= x && room.y + 1 <= y && x < room.x - 1 + room.w && y < room.y - 1 + room.h;
    }
    static bool apart(const std::vector<std::pair<int, int>>& spots, int x, int y, int gap) {   // FUN_00547430 / FUN_005473a0
        return std::ranges::none_of(spots, [&](const auto& spot) { return std::abs(spot.first - x) < gap || std::abs(spot.second - y) < gap; });
    }
    // FUN_00550380: 5 random spots on the object seed.
    Spot at_random(int id, int sx, int sy) {
        if (room.w < 2 || room.h < 2) return {};
        for (int tries = 0; tries < 5; ++tries) {
            const int x = room.x + rgn(room.w - sx - 1), y = room.y + rgn(room.h - sy - 1);
            if (inside(x, y) && !hit(x, y, sx + 6, sy + 6, 0x3f11)) { make(id, x, y, true); return std::pair{ x, y }; }
        }
        return {};
    }
    bool gate(int prob) { return int(rgn.next() % 100) <= prob; }
    int count(int density) const { return ((room.w * room.h >> 7) * density) >> 8; }

    // FUN_00551470: count tries of at_random; the last one's.
    Spot scatter(int id, int density, int prob) {
        if (!gate(prob)) return {};
        Spot last;
        for (int n = count(density); n > 0; --n) last = at_random(id, obj(id, "SizeX"), obj(id, "SizeY"));
        return last;
    }
    // FUN_00551150: 30 in 100, a body (103) where it stands.
    void companion(const Spot& spot) {
        if (spot && rgn.next() % 100 > 70) make(103, spot->first, spot->second, true);
    }
    // FUN_00550c20: clusters of a table's objects, spaced 2 x (l8 + rand(l10)) along a random direction.
    void cluster(int id, int density, int prob) {
        if (!gate(prob)) return;
        static constexpr std::array<int, 2> kCasket{ 3, 28 }, kUrn{ 89, 284 }, kPot{ 208, 209 };        // 0x731d5c, 0x731d78, 0x731d80
        static constexpr std::array<int, 3> kCorpse{ 79, 53, 1 };                                   // 0x731d50
        static constexpr std::array<int, 5> kRock{ 4, 9, 52, 94, 95 };                              // 0x731d64
        const int* table;
        int size, tries = 12, l8 = 5, l10 = 5;
        bool close = false;                                                                          // FUN_005502f0 for the rest
        if (id == 3) { table = kCasket.data(); size = 2; tries = 18; }
        else if (id == 79 || id == 1) { table = kCorpse.data(); size = 3; }
        else if (id == 4) { table = kRock.data(); size = 5; l8 = 1; l10 = 0; close = true; }
        else if (id == 89) { table = kUrn.data(); size = 2; }
        else if (id == 208 || id == 209) { table = kPot.data(); size = 2; l8 = 1; l10 = 0; close = true; }
        else return;
        const int sx = obj(id, "SizeX"), sy = obj(id, "SizeY");
        int left = count(density);
        if (left < 1) return;
        for (; tries > 0; --tries) {
            int pick = table[rgn(size)];
            int x = room.x + rgn(room.w - sx - 1), y = room.y + rgn(room.h - sy - 1);
            if (fits_wide(x, y, sx, sy)) {
                make(pick, x, y, true);
                bool ok = true;
                for (int placed = 1;;) {
                    if (const int half = placed >> 1; half > 0 && rgn(half) != 0) break;
                    if (!ok) break;
                    ok = false;
                    for (int k = 0; k < std::max(left, 4) * 3 && !ok; ++k) {
                        const int d = int(rgn.next() & 7);
                        x += (rgn(l10) + l8) * kDx[std::size_t(d)] * 2;
                        y += (rgn(l10) + l8) * kDy[std::size_t(d)] * 2;
                        ok = close ? fits_near(x, y, sx, sy) : fits_wide(x, y, sx, sy);
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
        const int sx = obj(7, "SizeX"), sy = obj(7, "SizeY"), space_x = obj(7, "Xspace"), space_y = obj(7, "Yspace");
        int placed = 0;
        for (int left = count(density), tries = left * 2; left > 0 && tries > 0; --tries) {
            const int first = rgn.next() % 3 ? 7 : 11;                                             // 0x551933: 1 in 3 exploding
            int x = room.x + rgn(room.w), y = room.y + rgn(room.h);
            if (!fits_near(x, y, sx, sy)) continue;
            make(first, x, y, true);
            if (++placed >= 8) return;
            bool ok = true;
            for (int in_cluster = 1;;) {
                if (const int half = in_cluster >> 1; half > 0 && rgn(half) != 0) break;
                if (!ok) break;
                ok = false;
                for (int k = 0; k < 15 && !ok; ++k) {
                    const int d = int(rgn.next() & 7);
                    x += space_x * kDx[std::size_t(d)];
                    y += space_y * kDy[std::size_t(d)];
                    ok = fits_near(x, y, sx, sy);
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
        const int sx = obj(id, "SizeX"), sy = obj(id, "SizeY");
        for (int tries = 8; tries > 0; --tries) {
            const int x = room.x + rgn(room.w - sx - 1), y = room.y + rgn(room.h - sy - 1);
            if (fits_wide(x, y, 5, 5)) return;                                                     // ponytail: the layout's list is empty, nothing placed
        }
    }
    // FUN_00551580: the room seed's gate, then the first at_random that lands.
    void single(int id, int density, int prob) {
        if (int(seed.next() % 100) > prob) return;
        for (int n = count(density); n > 0; --n)
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
        const int sx = obj(id, "SizeX"), sy = obj(id, "SizeY"), orient = obj(id, "Orientation");
        const int roll = int(seed.next() % 100);
        const bool forced = target > 0 && (counter << 7) / target > 0x60 && refills == 0;
        if (!forced && prob < roll) return;
        if (shrines.size() == 10 || int(shrines.size()) > target / 8) return;
        for (int tries = forced ? 30 : 3; tries > 0; --tries) {
            if (sx + 2 > room.w || sy + 2 > room.h) return;
            for (int k = 0; k < 5; ++k) {                                                            // FUN_00550a30, on the room seed
                int x, y;
                if (orient == 1) { x = room.x + room.w / 4 + seed(room.w / 2); y = room.y + seed(1) + 1; }
                else if (orient == 2) { x = room.x + seed(1) + 1; y = room.y + room.h / 4 + seed(room.h / 2); }
                else { x = room.x + seed(room.w - sx - 1); y = room.y + seed(room.h - sy - 1); }
                if (!inside(x, y) || hit(x, y, sx + 6, sy + 6, 0x3f11) || !apart(shrines, x, y, 50)) continue;
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
    int leader = 0;
    d2d::rules::Population pop;

    [[nodiscard]] const d2d::rules::MonType* type_at(int type) const {
        return type >= 0 && std::size_t(type) < game_data.monsters.types.size() ? &game_data.monsters.types[std::size_t(type)] : nullptr;
    }
    [[nodiscard]] int size_of(int type) const { const auto* t = type_at(type); return t ? t->size : 2; }
    // FUN_0061b130 → FUN_0066ce30: the id of the room's area holding a subtile.
    [[nodiscard]] std::uint32_t area_id(int x, int y) const {
        if (index < level.room_areas.size())
            for (const auto& a : level.room_areas[index])
                if (x / 5 >= a.left && y / 5 >= a.top && x / 5 < a.right && y / 5 < a.bottom) return a.id;
        return 0xffffffff;
    }
    // FUN_005b2a00's test (FUN_0064d9b0): the type's shape (MonStats2 SizeX:
    // 1 a subtile, 2 a plus, 3 a box, more never fits) against its spawnCol's mask.
    // ponytail: spawnCol 1 takes FUN_005b2700 (no ring walk) in game.exe; act 1 has none.
    [[nodiscard]] bool fits(int type, int x, int y) const {
        static constexpr int kMask[4] = { 0x3c01, 0x1c0, 0x3f11, 0 };
        const auto* t = type_at(type);
        const int col = t ? t->spawn_col : 0;
        // FUN_005fd350: a nest's (by BaseId) laying spot clear for a plus too.
        // ponytail: vilemother1's (298) check is off for population's calls; left out.
        const int base = t ? t->base : -1;
        if (const auto nest = [&](int dx, int dy, int mask) { return hit_shape(x + dx, y + dy, 2, mask); };
            (base == 206 && nest(0, 3, 0x3c01)) || (base == 228 && nest(0, 2, 0x3c01)) || (base == 334 && nest(-2, -2, 0x1c0)) || (base == 528 && nest(2, 4, 0x3c01)))
            return false;
        return size_of(type) <= 3 && !hit_shape(x, y, size_of(type), col > 0 && col < 4 ? kMask[col] : 0x3c01);
    }
    [[nodiscard]] d2d::rules::SpawnRoom bounds(bool in_area) const {
        return in_area ? d2d::rules::SpawnRoom{ area.x, area.y, area.w, area.h, seed } : d2d::rules::SpawnRoom{ room.x, room.y, room.w, room.h, seed };
    }
    // FUN_005b2a00 round (x, y) out to radius (-1: the spot only), in the
    // area being populated when `bound` (its rect and its id), else the room.
    bool put(int type, int x, int y, int radius, bool bound, int& out_x, int& out_y) {
        const bool in_area = bound && area.w > 0;
        auto at = bounds(in_area);
        const bool ok = d2d::rules::monster_detail::place(at, x, y, radius, [&](int tx, int ty) { return (!in_area || area_id(tx, ty) == area.id) && fits(type, tx, ty); }, out_x, out_y);
        seed = at.seed;
        return ok;
    }
    // FUN_0054dc40: 20 random spots of the area (else the room) where
    // `type` fits, none within WarpDist of an entrance (FUN_0054db50) if `near`.
    bool spot(int type, bool near, int& out_x, int& out_y) {
        const bool in_area = area.w > 0;
        auto at = bounds(in_area);
        auto by_way = [&](int x, int y) {
            return near && std::ranges::any_of(ways, [&](const auto& w) { return (x - w.first) * (x - w.first) + (y - w.second) * (y - w.second) < warp_dist; });
        };
        const bool ok = d2d::rules::room_spot(at, [&](int tx, int ty) { return (!in_area || area_id(tx, ty) == area.id) && fits(type, tx, ty); }, by_way, out_x, out_y);
        seed = at.seed;
        return ok;
    }
    // FUN_00555230 for a monster, FUN_00552df0: its seed off the game's.
    // Its footprint: 0x100 in its shape, 0x1000 in the one a size down.
    // A monster's own seed takes its look (FUN_00573cb0, rules::monster_look)
    // and one stat roll as it's made. Recorded in the level's spawns (NPCs aside).
    d2d::rules::Rng made(int type, int at_x, int at_y) {
        const auto& types = game_data.monsters.types;
        stamp_shape(at_x, at_y, size_of(type), 0x100);
        stamp_shape(at_x, at_y, size_of(type) - 1, 0x1000);
        // FUN_005b1cf0: a boss's mods, each FUN_005a4850(mod, 1) a unique counted (FUN_005a0320).
        // ponytail: Act 1's bosses by BaseId (Andariel, Blood Raven, the Maggot Queen); later acts' aren't.
        if (const auto* t = type_at(type)) pop.uniques += t->base == 156 ? 1 : (t->base == 267 || t->base == 284) ? 2 : 0;
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
        own.next();
        // FUN_005d6b60: an oninit MonEquip row rolls its slot and makes its item (2 game steps).
        // ponytail: act 1's only oninit row is Blood Raven's bow; read MonEquip.txt for more.
        if (type_at(type) && types[std::size_t(type)].id == "bloodraven") { own(1); game.next(); game.next(); }
        const bool npc = type_at(type) && std::size_t(type) < game_data.mon_is_npc.size() && game_data.mon_is_npc[std::size_t(type)];
        if (spawns && !npc) spawns->push_back({ type, at_x, at_y, leader, -1, d2d::rules::Boss::none, {}, 0, value });
        return own;
    }
    void tag(std::size_t at, d2d::rules::Boss kind, std::vector<int> mods = {}, int name_seed = 0) {
        if (!spawns || at >= spawns->size()) return;
        auto& s = (*spawns)[at];
        s.boss = kind; s.mods = std::move(mods); s.name_seed = name_seed;
    }
    // FUN_005b2830: PartyMin..Max on the leader's seed, minion1 / minion2 in
    // turn, radius 4 round it in its room (FUN_005b23c0, no party of their own).
    void party(int row, d2d::rules::Rng& own, int at_x, int at_y) {
        const auto* type = type_at(row);
        if (!type || type->minion[0] < 0) return;
        const int kinds = type->minion[1] >= 0 ? 2 : 1;
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
                auto boss = d2d::rules::roll_boss(game_data.umods, type, 0, false, own, false);
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
            ++pop.uniques;                                              // FUN_005a0320 as a boss
            std::vector<int> mods;
            for (const int id : sup->mods) if (id != 24) mods.push_back(id);
            mods.push_back(22);                                         // questcomplete
            tag(first, Boss::superunique, std::move(mods));
            if (spawns && first < spawns->size()) (*spawns)[first].super = superunique;
            const int minion = types[std::size_t(row)].minion[0] >= 0 ? types[std::size_t(row)].minion[0] : row;
            for (int count = own.range(sup->min_grp, sup->max_grp); count > 0; --count)
                if (int at_x, at_y; put(minion, spot_x, spot_y, 3, false, at_x, at_y)) { made(minion, at_x, at_y); tag(spawns->size() - 1, Boss::minion); }
            return;
        }
        party(row, own, spot_x, spot_y);
        if (pack == 3) {                                                // each champion through FUN_005b2f20: its own party
            tag(first, Boss::champion, { d2d::rules::umod::champion });
            ++pop.uniques;                                              // FUN_005a48c0 → FUN_005a0320, each champion
            own.next();                                                 // FUN_005a48c0 → FUN_005a0c00: a champion's minion count, rolled and unused
            for (int count = own(3) + 1; count > 0; --count)
                if (int at_x, at_y; put(row, spot_x, spot_y, 4, false, at_x, at_y)) {
                    const auto at = spawns ? spawns->size() : 0;
                    auto next = made(row, at_x, at_y);
                    tag(at, Boss::champion, { d2d::rules::umod::champion });
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
        const int density = std::min(level.mon.density[0], 10000);
        if (none || density <= 0 || pop.rooms_total == 0 || index >= level.room_areas.size()) return;
        for (const auto& a : level.room_areas[index]) {
            if (!a.id || a.skip || (!a.left && !a.right)) continue;
            area = { a.left * 5, a.top * 5, (a.right - a.left) * 5, (a.bottom - a.top) * 5, a.id };
            for (int tries = (area.h / 3) * (area.w / 3); tries > 0; --tries) {
                if (int(game.next() % 100000) > density) continue;
                if (region.types.empty()) { area = {}; return; }
                int type = d2d::rules::pick_type(region, seed);
                if (const auto* t = type_at(type); t && t->place_spawn >= 0 && seed(100) > 20) type = t->place_spawn;
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
    // ponytail: NM / hell's pick is the region's; this is normal's.
    void unique() {
        using d2d::rules::Boss;
        const auto& umon = level.mon.umon;
        const int row = umon.empty() ? 0 : umon[std::size_t(seed(int(umon.size())))];
        if (!type_at(row)) return;
        leader = int(spawns->size());
        int x, y;
        if (!spot(row, true, x, y) || !put(row, x, y, -1, true, x, y)) return;
        const auto first = spawns->size();
        auto own = made(row, x, y);
        ++pop.uniques;
        auto boss = d2d::rules::roll_boss(game_data.umods, *type_at(row), 0, true, own, false);
        if (boss.kind == Boss::champion) {
            tag(first, Boss::champion, std::move(boss.mods), boss.name_seed);
            for (int count = own(3) + 1; count > 0; --count)
                if (int at_x, at_y; put(row, x, y, 4, true, at_x, at_y)) {
                    const auto at = spawns->size();
                    auto next = made(row, at_x, at_y);
                    tag(at, Boss::champion, { d2d::rules::umod::champion });
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
    // more of its type at radius 3 in the area, each with its party.
    // ponytail: the extra object for a leader of class 0x210 isn't made; act 1 has none.
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
        for (int extra = own(high - low + 1) + low - 1; extra > 0; --extra)
            if (int at_x, at_y; put(row, x, y, 3, true, at_x, at_y)) { auto next = made(row, at_x, at_y); party(row, next, at_x, at_y); }
    }

    // A preset past objects.txt (FUN_0054f490's table at 0x731d28): 574-579 a
    // shrine (136), 580 / 581 a random chest (FUN_0054f370 a sparkling one,
    // FUN_0054f180), 582 a quest's (FUN_0059d830).
    // ponytail: 574-579's kind (their range, on the unit's own seed), 580's
    // level-62..64 roll and 582's quest check aren't taken: act 1 has none of them.
    int special(int id) {
        if (id < 580) return 136;
        if (id == 582) return 0x133;
        const int act = level.id < 40 ? 0 : level.id < 75 ? 1 : level.id < 103 ? 2 : 3;   // FUN_006427f0
        const auto low = rgn.next();
        int pick;
        if (act == 1) pick = level.id == 0x4a ? std::array{ 0x183, 0x185, 0x186, 0x187 }[low & 3] : std::array{ 0x57, 0x58 }[low & 1];
        else if (act == 2) pick = level.id == 0x53 ? std::array{ 0x149, 0x14a, 0x14b, 0x14c }[low & 3] : std::array{ 0xb5, 0xb7 }[low & 1];
        else pick = std::array{ 5, 6, 0x8b, 0x8c, 0x8d, 0x90, 0xb0, 0xb1, 0xc6, 0xf0, 0xf1, 0xf2, 0xf3 }[low % 13];
        return id == 580 && level.id == 0x19 ? 0x173 : pick;
    }
    // FUN_005559a0: the room's preset units, the monsters last (`monsters`: normal's, here).
    void presets(std::size_t index, bool monsters) {
        const int nmon = int(game_data.mon_bin.size()), nsu = int(game_data.superuniques.size());
        for (std::size_t i = 0; i < level.units.size(); ++i) {
            const auto& unit = level.units[i];
            if (level.unit_rooms[i] != int(index) || unit.type == 1 || (unit.flags & 1)) continue;
            if (unit.type == 2 && unit.id != 0x23d) {
                const auto before = level.npcs.size();
                make(unit.id > 0x23d ? special(unit.id) : unit.id, unit.x, unit.y, false);
                if (unit.id == 580 && level.npcs.size() > before) level.npcs.back().sparkle = true;   // FUN_0054f370: FUN_005540a0(unit, 1)
            }
            else if (unit.type == 5) game.next();                                                          // a warp tile: a unit made
        }
        if (!monsters) return;
        // FUN_0063ec70 + FUN_0054e2a0: a base monster as the level has it (as populate()'s own()).
        const auto& types = game_data.monsters.types;
        auto own = [&](int base_bin) {
            int row = int(game_data.mon_bin[std::size_t(base_bin)]);
            const auto listed = std::ranges::find_if(level.mon.mon, [&](int m) { return m >= 0 && std::size_t(m) < types.size() && types[std::size_t(m)].base == types[std::size_t(row)].base; });
            if (listed != level.mon.mon.end()) row = *listed;
            else if (std::ranges::any_of(level.mon.mon, [](int m) { return m >= 0; })) {
                // FUN_0063ec70: else up its class while the next's Level is at most MonLvl1Ex + 1.
                // ponytail: NextInClass as the rows of its base in order.
                const int top = std::size_t(level.id) < game_data.area_level.size() ? game_data.area_level[std::size_t(level.id)][0] + 1 : 0;
                for (int next = types[std::size_t(row)].base + 1; std::size_t(next) < types.size() && types[std::size_t(next)].base == types[std::size_t(row)].base && types[std::size_t(next)].level[0] <= top; ++next) row = next;
            }
            const int base = types[std::size_t(row)].base, id = level.id;
            auto bin = [&](int bin_row) { return int(game_data.mon_bin[std::size_t(bin_row)]); };
            if (base == bin(0x13)) return id == 6 ? bin(0x14) : id == 7 || id == 12 || id == 16 ? bin(0x15) : row;
            if (base == bin(0x3a)) return id == 6 || id == 7 ? bin(0x3b) : id == 12 || id == 16 ? bin(0x3c) : row;
            return row;
        };
        for (std::size_t i = 0; i < level.units.size(); ++i) {
            const auto& unit = level.units[i];
            if (level.unit_rooms[i] != int(index) || unit.type != 1 || (unit.flags & 1) || unit.id < 0) continue;
            leader = int(spawns->size());                                                              // FUN_00555910: a placement of its own
            if (unit.id < nmon) {
                const auto row = game_data.mon_bin[std::size_t(unit.id)];
                const bool stay = unit.id == 0xe5 || (unit.id >= 0x11c && unit.id <= 0x120) || unit.id == 0x188 || unit.id == 0x189;   // FUN_0054e3a0
                monster(int(row), unit.x, unit.y, stay ? 0 : 4);
                if (game_data.mon_is_npc[row] && !game_data.mon_npc[row].code.empty()) {                // an NPC (Flavie) stands at its spot
                    auto npc = game_data.mon_npc[row];
                    npc.x = (float(unit.x) + 0.5f) / 5;
                    npc.y = (float(unit.y) + 0.5f) / 5;
                    level.npcs.push_back(std::move(npc));
                }
            } else if (const int code = unit.id - nmon - nsu; code == 0x11 || code == 0x12 || code == 0x05) {
                monster(code == 0x05 ? int(game_data.mon_bin[0x10b]) : own(code == 0x11 ? 0x13 : 0x3a), unit.x, unit.y, code == 0x05 ? 0 : 4);   // Fallen, shamans, Blood Raven
            }
            else if ((code == 0x02 || code == 0x03) && !level.mon.umon.empty())                         // FUN_005bde80: normal's pick is a umon
                monster(level.mon.umon[std::size_t(seed(int(level.mon.umon.size())))], unit.x, unit.y, 0, -1, code);
            else if (unit.id < nmon + nsu) {
                monster(-1, unit.x, unit.y, 0, unit.id - nmon);
                if (std::size_t(leader) < spawns->size())                                              // FUN_00555910: the preset's map AI
                    for (const auto& [off_x, off_y] : unit.path) (*spawns)[std::size_t(leader)].path.emplace_back(unit.x + off_x, unit.y + off_y);
            }
        }
    }

    // FUN_00552560: whether the room gets groups, counting it.
    bool open(std::size_t index, int themes) {
        const auto flags = level.room_flags[index];
        if ((flags & 0x30000) || (flags & 0x800000) || (index < level.road_rooms.size() && level.road_rooms[index])) return false;   // FUN_0066ba90: a plain room on a path
        if (target == 0x7fffffff) target = int(std::ranges::count_if(level.room_flags, [](std::uint32_t f) { return !(f & 0x800000); }));
        ++counter;
        if (themes) {                                                                              // FUN_00552400
            // ponytail: a theme's own fn (0x731e6c) never runs for act 1's
            // masks (8, 60: the pick's bit number isn't under the count, or its fn is null);
            // only its object-seed rolls are taken.
            if (target < 0) return true;
            const int roll = int(rgn.next() % 100);
            int odds = counter > target / 2 ? 5 : 0;
            if (counter > target - target / 4) odds += 5;
            if (roll < odds + 12 && std::popcount(unsigned(themes & 0x7f)) > 0) rgn(std::popcount(unsigned(themes & 0x7f)));
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
    // FUN_00642480: the centres of rooms with a warp slot's wall (a walk-through exit has no warp).
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
        if (unit.type == 2 && (pop.obj(unit.id, "SubClass") & 0x40)) {                            // a waypoint: its room and its tile
            centre(unit.x / 5, unit.y / 5);
            pop.ways.push_back({ unit.x / 5 * 5, unit.y / 5 * 5 });
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
    pop.pop = { 0, total, 0, level.mon.umin[0], level.mon.umax[0], 0, &game_data.umods };
    return pop;
}

}  // namespace

// FUN_0054ebc0: none in a room flagged 0x800000 or nopop; the level's total (FUN_00642be0) leaves them out.
bool ObjectRooms::none(const Level& level, std::size_t i) {
    return (i < level.nopop_rooms.size() && level.nopop_rooms[i]) || (level.room_flags[i] & 0x800000);
}

// The game's seeds in for room `room_index`, the level's rooms up so far.
void ObjectRooms::load(Spawning& spawning, std::size_t room_index) {
    rgn = spawning.objects;
    game = spawning.game;
    superuniques = spawning.superuniques;
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
}

// in_room: the level's rooms up so far.
void ObjectRooms::rooms_up(const Spawning& spawning) {
    const auto tiles_wide = std::size_t(level.ds1.width());
    in_room.assign(tiles_wide * std::size_t(level.ds1.height()), false);
    const auto found = spawning.levels.find(&level);
    if (found == spawning.levels.end()) return;
    for (const auto up : found->second.order)
        if (up < level.rooms.size())
            for (int y = level.rooms[up].y; y < level.rooms[up].y + level.rooms[up].height && y < level.ds1.height(); ++y)
                for (int x = level.rooms[up].x; x < level.rooms[up].x + level.rooms[up].width && x < level.ds1.width(); ++x) in_room[std::size_t(y) * tiles_wide + std::size_t(x)] = true;
}

void ObjectRooms::save(Spawning& spawning) const {
    spawning.objects = rgn;
    spawning.game = game;
    spawning.superuniques = superuniques;
    if (std::size_t(level.id) < spawning.regions.size()) spawning.regions[std::size_t(level.id)] = region;
}

// At build time: the room flags FUN_00552560 reads, and the level's units
// off its rooms (the camp's edge).
void place_objects(const GameData& game_data, GameData::LevelBuilder& builder, Level& level) {
    ObjectRooms pop{ game_data, builder, level };
    level.room_flags.resize(level.rooms.size());
    level.unit_rooms.resize(level.units.size(), -1);
    // 0x30000: a room with a waypoint (objects.txt SubClass 0x40, FUN_00667e30);
    // 0x800000: the Blood Moor's rooms next to the camp (FUN_0066bd92).
    for (std::size_t i = 0; i < level.units.size(); ++i)
        if (level.units[i].type == 2 && level.unit_rooms[i] >= 0 && (pop.obj(level.units[i].id, "SubClass") & 0x40))
            level.room_flags[std::size_t(level.unit_rooms[i])] |= 0x30000;
    if (level.id == 2 && game_data.town.ds1.width() > 0) {
        const auto& town = game_data.town;
        const int tx = town.world_x, ty = town.world_y, tw = town.ds1.width(), th = town.ds1.height();
        for (std::size_t i = 0; i < level.rooms.size(); ++i) {
            const auto& made = level.rooms[i];
            const int x = level.world_x + made.x, y = level.world_y + made.y;
            const int gap_x = x < tx ? tx - made.width - x : x - tw - tx, gap_y = y < ty ? ty - made.height - y : y - th - ty;
            if (gap_x < 6 && gap_y < 6) level.room_flags[i] |= 0x800000;
        }
    }
    // ponytail: no room brings these up in game.exe; their rolls take a
    // throwaway copy of the object seed as a game starts.
    auto rgn = object_seed(game_data.map_seed);
    for (std::size_t i = 0; i < level.units.size(); ++i)
        if (level.unit_rooms[i] < 0 && level.units[i].type == 2) add_object(game_data, builder.objects, builder.obj_row, level, level.units[i].id, level.units[i].x, level.units[i].y, rgn);
}

d2d::rules::Rng room_objects(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t index, bool all) {
    if (index >= level.rooms.size() || !game_data.builder) return {};
    auto& pop = rooms_of(game_data, spawning, level);
    pop.load(spawning, index);
    pop.seed = d2d::rules::Rng{ index < level.room1_seeds.size() ? level.room1_seeds[index] : level.rooms[index].seed };
    // FUN_0054f060; on a step with its low 15 bits clear, a MonWndr level
    // rolls a wanderer (FUN_0054eff0: under 3 in 100).
    // ponytail: the wanderer itself (FUN_0054ef50) isn't made; 3 in 3.3M rooms.
    if ((pop.seed.next() & 0x7fff) == 0 && level.mon.wander) pop.seed.next();
    pop.presets(index, all);
    if (all) {
        pop.out->pre = pop.seed.low;
        if (pop.open(index, pop.themes)) pop.groups();
        pop.out->post = pop.seed.low;
        pop.out->rgn = pop.rgn.low;
        pop.populate(ObjectRooms::none(level, index));
        spawning.levels[&level].room_seeds[index] = pop.seed;
    }
    pop.save(spawning);
    return pop.seed;
}

void room_groups(const GameData& game_data, Spawning& spawning, const Level& level, std::size_t index, d2d::rules::Rng& seed) {
    if (index >= level.rooms.size() || !game_data.builder) return;
    auto& pop = rooms_of(game_data, spawning, level);
    pop.load(spawning, index);
    pop.seed = seed;
    pop.out->pre = pop.seed.low;
    if (pop.open(index, pop.themes)) pop.groups();
    pop.out->post = pop.seed.low;
    pop.out->rgn = pop.rgn.low;
    seed = pop.seed;
    pop.save(spawning);
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
