// A level's rooms populated up to their monsters, as FUN_0052d160 walks
// the act's room1 list (newest first: Level::rooms order): each room's
// seed step (FUN_0054f060), its preset units (FUN_005559a0), then the
// random object groups (FUN_00552610 and the PopulateFns at 0x731d00;
// docs/research/re/objects.md "Random object groups per room"). Proven
// against game.exe: tools/emu objgroups.py, drlg-dump <seed> <level> objgroups.
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

struct Populator {
    const GameData& game_data;
    GameData::LevelBuilder& builder;
    Level& level;
    d2d::rules::Rng rgn;                                // the game's object seed (FUN_00546fa0)
    int width = 0, height = 0;                          // subtiles
    std::vector<std::uint16_t> grid;                    // the rooms' collision (room1 +0x20), tiles' flags and units'
    std::vector<bool> in_room;
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

    int obj(int id, const char* column) const {
        const auto found = builder.obj_row.find(std::to_string(id));
        return found == builder.obj_row.end() ? 0 : std::atoi(std::string(builder.objects.get(found->second, column)).c_str());
    }
    // FUN_0064ca50: 0x27 off the level's rooms.
    [[nodiscard]] std::uint16_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height || !in_room[std::size_t(y / 5) * std::size_t(width / 5) + std::size_t(x / 5)]) return 0x27;
        return grid[std::size_t(y) * std::size_t(width) + std::size_t(x)];
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
                if (tx >= 0 && ty >= 0 && tx < width && ty < height) grid[std::size_t(ty) * std::size_t(width) + std::size_t(tx)] |= bits;
    }
    // FUN_00555230 for an object: add_object's rolls, its footprint
    // (FUN_006209d0: 0x400; 0x8000, no mask's, for a SubClass 4 non-door;
    // none without HasCollision in its start mode).
    // Returns its shrine id (Npc::shrine).
    int make(int id, int x, int y, bool group) {
        const auto before = level.npcs.size();
        auto gold = rgn;
        add_object(game_data, builder.objects, builder.obj_row, level, id, x, y, rgn);
        const bool quiet = !obj(id, "IsDoor") && (obj(id, "SubClass") & 4);
        const bool on = level.npcs.size() > before && level.npcs.back().mode == "ON";
        if (obj(id, on ? "HasCollision2" : "HasCollision0")) stamp(x, y, obj(id, "SizeX"), obj(id, "SizeY"), quiet ? 0x8000 : 0x400);
        game.next();                                                   // FUN_00552df0: its seed
        if (obj(id, "InitFn") == 28) piles(gold, x, y);
        if (group) out->made.push_back({ id, x, y });
        return level.npcs.size() > before ? level.npcs.back().shrine : 0;
    }

    // FUN_0054f8c0 (InitFn 28), replaying add_object's rolls: 1..9 piles,
    // each at rand(4), rand(4) off it, dropped (FUN_00559300) where clear
    // (0x3f11) and the last spot tried inside its room is too (sic). A pile
    // is an item: two game-seed steps (FUN_00552df0, FUN_00552e90) and
    // 0x200 at the free subtile nearest (x + 2, y + 3) (FUN_0064dea0).
    // ponytail: FUN_0064dea0's path check (FUN_0066a670) isn't taken.
    void piles(d2d::rules::Rng gold, int x, int y) {
        auto inside_room = [&](int at_x, int at_y) { return at_x >= room.x && at_y >= room.y && at_x < room.x + room.w && at_y < room.y + room.h; };
        auto clear = [&](int at_x, int at_y) { return !hit(at_x, at_y, 1, 1, 0x3e01); };
        int last_x = x, last_y = y;
        for (int count = gold(9) + 1; count > 0; --count) {
            const int dx = int(gold.next() & 3), dy = int(gold.next() & 3);
            if (!inside_room(last_x + dx, last_y + dy)) continue;
            last_x = x + dx; last_y = y + dy;
            if (hit(last_x, last_y, 1, 1, 0x3f11)) continue;
            int at_x = last_x + 2, at_y = last_y + 3;
            if (at(at_x, at_y) == 0x27) { at_x = last_x; at_y = last_y; }
            if (!clear(at_x, at_y)) {
                int best = -1, best_x = at_x, best_y = at_y;
                for (int r = 1; r < 50 && best < 0; ++r) {
                    auto test = [&](int tx, int ty) {
                        const int d = std::abs(tx - at_x) + std::abs(ty - at_y);
                        if (clear(tx, ty) && (best < 0 || d < best)) { best = d; best_x = tx; best_y = ty; }
                    };
                    for (int ty = at_y - r; ty <= at_y + r; ++ty) { test(at_x - r, ty); test(at_x + r, ty); }
                    for (int tx = at_x - r + 1; tx <= at_x + r - 1; ++tx) { test(tx, at_y - r); test(tx, at_y + r); }
                }
                at_x = best_x; at_y = best_y;
            }
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

    // FUN_005b2a00 at a preset monster's spot (FUN_0054e490), then its
    // footprint, then its party (FUN_005b2830: PartyMin..Max on its own
    // seed, minion1 / minion2 in turn, radius 4 round it, FUN_005b23c0).
    // A superunique (FUN_005a49b0): once a level unless Stacks, at a random
    // spot of the room with AutoPos (FUN_0054dc40), else radius 5; then
    // MinGrp..MaxGrp of minion1 (else its own type) at radius 3 (FUN_005a0c00).
    // ponytail: fits is a plus of walls, objects and monsters (0x3c01);
    // game.exe's FUN_0064d9b0 tests the monster's own collision shape.
    // A monster's own seed takes its look (FUN_00573cb0, rules::monster_look)
    // and one stat roll as it's made, then the counts.
    // ponytail: the superunique once-a-game bit is per level here.
    // `pack` (FUN_0054e600, MonPlace 2 / 3, `row` picked off the region):
    // 2 a unique pack (FUN_005a43e0): made at a random spot of the room
    // (FUN_005a09e0, then FUN_005b2f20 there), a unique (FUN_005a0760, no
    // champion roll: its mods on its own seed), 3..6 minions at radius 3
    // (FUN_005a0c00, own seed); 3 a champion (at the spot) and own(3) + 1 more at radius 4 (FUN_0054e1e0).
    void monster(int row, int x, int y, int retry, int superunique = -1, int pack = 0) {
        using d2d::rules::monster_detail::place;
        const auto& types = game_data.monsters.types;
        d2d::rules::SpawnRoom spawn{ room.x, room.y, room.w, room.h, seed };
        auto size_of = [&](int type) { return type >= 0 && std::size_t(type) < types.size() ? types[std::size_t(type)].size : 2; };
        int size = 2;                                                   // the shape of the one being placed
        auto fits = [&](int at_x, int at_y) { return !hit_shape(at_x, at_y, size, 0x3c01); };
        auto made = [&](int type, int at_x, int at_y) {                // FUN_00552df0: its seed off the game's
            // its footprint: 0x100 in its shape, 0x1000 in the one a size down
            stamp_shape(at_x, at_y, size_of(type), 0x100);
            stamp_shape(at_x, at_y, size_of(type) - 1, 0x1000);
            d2d::rules::Rng own{ game.next() };
            const std::vector<d2d::rules::Components>* sets = nullptr;
            for (std::size_t i = 0; i < region.types.size() && i < region.components.size(); ++i)
                if (region.types[i].first == type) sets = &region.components[i];
            // FUN_00547bc0: a type the region lacks joins it (13 at most) with
            // its sets on this seed (FUN_005bdb20) when it has 3+ layers
            // (MonStats2 +0xec); else its look rolls every layer.
            const bool special = type == 0xc3 || type == 0xc4 || type == 0x126 || type == 0x128;
            if (!sets && !special && region.types.size() < 13 && type >= 0 && std::size_t(type) < types.size()
                && std::ranges::count_if(types[std::size_t(type)].choices, [](int c) { return c > 0; }) > 2) {
                region.types.emplace_back(type, 0);
                region.components.push_back(d2d::rules::roll_components(types[std::size_t(type)].choices, own));
                sets = &region.components.back();
            }
            if (type >= 0 && std::size_t(type) < types.size()) (void)d2d::rules::monster_look(sets, types[std::size_t(type)].choices, own);
            own.next();
            // FUN_005d6b60: an oninit MonEquip row rolls its slot and makes its item (2 game steps).
            // ponytail: act 1's only oninit row is Blood Raven's bow; read MonEquip.txt for more.
            if (type >= 0 && std::size_t(type) < types.size() && types[std::size_t(type)].id == "bloodraven") { own(1); game.next(); game.next(); }
            return own;
        };
        const auto* sup = superunique >= 0 ? &game_data.superuniques[std::size_t(superunique)] : nullptr;
        if (sup) {
            if (sup->type < 0 || (!sup->stacks && superuniques.test(std::size_t(superunique)))) return;
            row = sup->type;
            retry = 5;
        }
        int spot_x, spot_y;
        size = size_of(row);
        if (pack == 2) {
            auto near = [&](int at_x, int at_y) { return std::ranges::any_of(ways, [&](const auto& w) { return (at_x - w.first) * (at_x - w.first) + (at_y - w.second) * (at_y - w.second) < warp_dist; }); };
            if (d2d::rules::room_spot(spawn, fits, near, x, y) && place(spawn, x, y, -1, fits, x, y)) {
                auto own = made(row, x, y);
                const auto& type = types[std::size_t(row)];
                (void)d2d::rules::roll_boss(game_data.umods, type, 0, false, own, false);
                size = size_of(type.minion[0] >= 0 ? type.minion[0] : row);
                for (int count = own(4) + 3; count > 0; --count)
                    if (int at_x, at_y; place(spawn, x, y, 3, fits, at_x, at_y)) made(type.minion[0] >= 0 ? type.minion[0] : row, at_x, at_y);
            }
            seed = spawn.seed;
            return;
        }
        if (sup && sup->autopos && !d2d::rules::room_spot(spawn, fits, [](int, int) { return false; }, x, y)) { seed = spawn.seed; return; }
        if (place(spawn, x, y, -1, fits, spot_x, spot_y) || (retry > 0 && place(spawn, x, y, retry, fits, spot_x, spot_y))) {
            auto own = made(row, spot_x, spot_y);
            auto party = [&](d2d::rules::Rng& leader, int at_x, int at_y) {      // FUN_005b2830
                if (row < 0 || std::size_t(row) >= types.size() || types[std::size_t(row)].minion[0] < 0) return;
                const auto& type = types[std::size_t(row)];
                const int kinds = type.minion[1] >= 0 ? 2 : 1;
                for (int i = 0, count = leader.range(type.party_min, type.party_max); i < count; ++i)
                    if (int to_x, to_y; (size = size_of(type.minion[std::size_t(i % kinds)])) && place(spawn, at_x, at_y, 4, fits, to_x, to_y)) made(type.minion[std::size_t(i % kinds)], to_x, to_y);
            };
            if (sup) {
                superuniques.set(std::size_t(superunique));
                const int minion = types[std::size_t(row)].minion[0] >= 0 ? types[std::size_t(row)].minion[0] : row;
                size = size_of(minion);
                for (int count = own.range(sup->min_grp, sup->max_grp); count > 0; --count)
                    if (int at_x, at_y; place(spawn, spot_x, spot_y, 3, fits, at_x, at_y)) made(minion, at_x, at_y);
            } else {
                party(own, spot_x, spot_y);
                if (pack == 3) {                                            // each champion through FUN_005b2f20: its own party
                    own.next();                                             // FUN_005a48c0 → FUN_005a0c00: a champion's minion count, rolled and unused
                    for (int count = own(3) + 1; count > 0; --count)
                        if (int at_x, at_y; (size = size_of(row)) && place(spawn, spot_x, spot_y, 4, fits, at_x, at_y)) { auto next = made(row, at_x, at_y); party(next, at_x, at_y); }
                }
            }
        }
        seed = spawn.seed;
    }

    // A preset past objects.txt (FUN_0054f490's table at 0x731d28): 574-579 a
    // shrine (136), 580 / 581 a random chest (FUN_0054f370, FUN_0054f180),
    // 582 a quest's (FUN_0059d830).
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
    // FUN_005559a0: the room's preset units, the monsters last.
    void presets(std::size_t index) {
        const int nmon = int(game_data.mon_bin.size()), nsu = int(game_data.superuniques.size());
        for (std::size_t i = 0; i < level.units.size(); ++i) {
            const auto& unit = level.units[i];
            if (level.unit_rooms[i] != int(index) || unit.type == 1 || (unit.flags & 1)) continue;
            if (unit.type == 2 && unit.id != 0x23d) make(unit.id > 0x23d ? special(unit.id) : unit.id, unit.x, unit.y, false);
            else if (unit.type == 5) game.next();                                                          // a warp tile: a unit made
        }
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
            else if (unit.id < nmon + nsu) monster(-1, unit.x, unit.y, 0, unit.id - nmon);
        }
    }

    // FUN_00552560: whether the room gets groups, counting it.
    bool open(std::size_t index, int themes) {
        const auto flags = level.room_flags[index];
        if ((flags & 0x30000) || (flags & 0x800000) || (flags & 0x80)) return false;
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

}  // namespace

void place_objects(const GameData& game_data, GameData::LevelBuilder& builder, Level& level) {
    Populator pop{ game_data, builder, level, object_seed(game_data.map_seed) };
    // ponytail: the game seed as a fresh game has it when the level's the
    // first made (start_spawning); a real game's has moved on by then.
    // The region (the monsters' looks) is normal's.
    const auto spawning = start_spawning(game_data, 0);
    pop.game = spawning.game;
    if (std::size_t(level.id) < spawning.regions.size()) pop.region = spawning.regions[std::size_t(level.id)];
    pop.width = level.ds1.width() * 5;
    pop.height = level.ds1.height() * 5;
    pop.grid.assign(level.walk.begin(), level.walk.end());
    pop.grid.resize(std::size_t(pop.width) * std::size_t(pop.height));
    pop.in_room.assign(std::size_t(level.ds1.width()) * std::size_t(level.ds1.height()), level.rooms.empty());
    for (const auto& made : level.rooms)
        for (int y = made.y; y < made.y + made.height && y < level.ds1.height(); ++y)
            for (int x = made.x; x < made.x + made.width && x < level.ds1.width(); ++x) pop.in_room[std::size_t(y) * std::size_t(level.ds1.width()) + std::size_t(x)] = true;
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
    int themes = 0;
    for (std::size_t row = 0; row < builder.levels.size(); ++row)
        if (std::atoi(std::string(builder.levels.get(row, "Id")).c_str()) == level.id) {
            themes = std::atoi(std::string(builder.levels.get(row, "Themes")).c_str());
            pop.warp_dist = std::atoi(std::string(builder.levels.get(row, "WarpDist")).c_str());
        }
    auto centre = [&](int tile_x, int tile_y) {                                                    // the centre of the room holding a tile
        for (const auto& made : level.rooms)
            if (tile_x >= made.x && tile_y >= made.y && tile_x < made.x + made.width && tile_y < made.y + made.height) {
                pop.ways.push_back({ (made.x + made.width / 2) * 5, (made.y + made.height / 2) * 5 });
                return;
            }
    };
    for (const auto& warp : level.warps) centre(int(std::floor(warp.x)), int(std::floor(warp.y)));
    for (const auto& unit : level.units)
        if (unit.type == 2 && (pop.obj(unit.id, "SubClass") & 0x40)) {                            // a waypoint: its room and its tile
            centre(unit.x / 5, unit.y / 5);
            pop.ways.push_back({ unit.x / 5 * 5, unit.y / 5 * 5 });
        }
    level.group_rooms.assign(level.rooms.size(), {});
    level.post_object_group_seeds.assign(level.rooms.size(), {});
    std::size_t placed = 0;
    for (std::size_t i = 0; i < level.rooms.size(); ++i) {
        const auto& made = level.rooms[i];
        pop.room = { made.x * 5, made.y * 5, made.width * 5, made.height * 5 };
        pop.seed = d2d::rules::Rng{ i < level.room1_seeds.size() ? level.room1_seeds[i] : made.seed };
        pop.out = &level.group_rooms[i];
        pop.seed.next();                                                                           // FUN_0054f060
        pop.presets(i);
        pop.out->pre = pop.seed.low;
        if (pop.open(i, themes)) pop.groups();
        pop.out->post = pop.seed.low;
        level.post_object_group_seeds[i] = pop.seed;
        pop.out->rgn = pop.rgn.low;
        placed += pop.out->made.size();
    }
    level.group_rgn = pop.rgn.low;
    for (std::size_t i = 0; i < level.units.size(); ++i)                                           // units off the rooms: no room brings them up
        if (level.unit_rooms[i] < 0 && level.units[i].type == 2) add_object(game_data, builder.objects, builder.obj_row, level, level.units[i].id, level.units[i].x, level.units[i].y, pop.rgn);
    if (placed) d2d::log::info("  {}: {} random object-group placements", level.name, placed);
}

}  // namespace d2d::game
