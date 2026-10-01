// Monster spawning and stats over hand-made tables: the region takes
// every listed type once, rooms fill by density, Fallen come as a leader
// with a party, nothing lands on a blocked subtile or by the entrance.
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace d2d::rules;

int main() {
    Monsters monsters;
    MonType zombie;
    zombie.id = "zombie1"; zombie.spawnable = true; zombie.base = 5;
    zombie.min_grp = 1; zombie.max_grp = 2; zombie.rarity = 2; zombie.level = { 1, 36, 67 };
    zombie.diff[0] = { .min_hp = 101, .max_hp = 181, .armor_class = 84, .exp = 111, .a1_min = 51, .a1_max = 151, .a1_th = 101 };
    MonType fallen;
    fallen.id = "fallen1"; fallen.spawnable = true; fallen.base = 19;
    fallen.min_grp = 2; fallen.max_grp = 3; fallen.party_min = 2; fallen.party_max = 3; fallen.rarity = 2;
    fallen.minion = { 1, -1 };
    MonType off;
    off.id = "off"; off.rarity = 5;                               // not isSpawn: never in a region
    monsters.types = { zombie, fallen, off };
    for (std::size_t i = 0; i < monsters.types.size(); ++i) monsters.by_id[monsters.types[i].id] = int(i);
    monsters.lvl.resize(2);
    monsters.lvl[1] = { .armor_class = { 6, 0, 0 }, .to_hit = { 8, 0, 0 }, .hit_points = { 7, 0, 0 }, .damage = { 2, 0, 0 }, .experience = { 30, 0, 0 } };

    // Region: all three listed, the disabled one dropped.
    LevelMon level_mon{ .density = { 10000, 0, 0 }, .num_mon = 3, .mon = { 0, 1, 2 } };
    Rng rng{ 42 };
    const auto reg = monster_region(monsters, level_mon, 0, rng);
    assert(reg.types.size() == 2 && reg.total == 4);

    // A 40x40-subtile room (one 8x8-tile room) with a blocked column.
    auto fits = [](int x, int y) { return x != 20 && x >= 0 && y >= 0; };
    auto near_way = [](int x, int y) { return x < 5 && y < 5; };
    std::vector<Spawn> first, second;
    Rng rng1{ 7 }, rng2{ 7 };
    for (int i = 0; i < 20; ++i) {
        populate_room(monsters, reg, 10000, { 0, 0, 40, 40, Rng{ std::uint32_t(100 + i) } }, rng1, fits, near_way, first);
        populate_room(monsters, reg, 10000, { 0, 0, 40, 40, Rng{ std::uint32_t(100 + i) } }, rng2, fits, near_way, second);
    }
    assert(first.size() == second.size() && !first.empty());                  // same seeds, same monsters
    int leaders = 0, fallen_groups = 0;
    for (std::size_t i = 0; i < first.size(); ++i) {
        const auto& spawn = first[i];
        assert(spawn.x == second[i].x && spawn.y == second[i].y && spawn.type == second[i].type);
        assert(spawn.x >= 0 && spawn.y >= 0 && spawn.x < 40 && spawn.y < 40 && spawn.x != 20);
        if (spawn.leader != int(i)) continue;
        ++leaders;
        assert(!near_way(spawn.x, spawn.y));
        std::size_t count = 1;
        while (i + count < first.size() && first[i + count].leader == int(i)) ++count;
        if (spawn.type == 1) { ++fallen_groups; assert(count >= 1 && count <= 4); }   // leader + party 2..3 (fewer if crowded)
        else assert(count <= 2);                                               // zombies: 1..2
    }
    std::printf("%zu monsters in %d groups (%d fallen) over 20 rooms\n", first.size(), leaders, fallen_groups);
    assert(leaders >= 5 && fallen_groups > 0);

    // Density 0: nobody.
    std::vector<Spawn> none;
    populate_room(monsters, reg, 0, { 0, 0, 40, 40, Rng{ 1 } }, rng1, fits, near_way, none);
    assert(none.empty());

    // FUN_0054ec90's areas: (h/3)*(w/3) game-seed tries each, spots only in
    // the area drawn (nothing fits here, so every hit is one step too).
    {
        SpawnRoom split{ 0, 0, 40, 40, Rng{ 5 }, { { 0, 0, 30, 30 }, { 30, 30, 10, 9 } } };
        Rng game{ 9 }, mirror{ 9 };
        populate_room(monsters, reg, 10000, split, game, [](int, int) { return false; }, near_way, none);
        for (int step = 0; step < 100 + 9; ++step) (void)mirror.next();
        assert(none.empty() && game.low == mirror.low && game.high == mirror.high);
        std::vector<Spawn> in_area;
        populate_room(monsters, reg, 10000, SpawnRoom{ 0, 0, 40, 40, Rng{ 5 }, { { 30, 30, 10, 10 } } }, game, fits, near_way, in_area);
        for (const auto& spawn : in_area) assert(spawn.leader != &spawn - in_area.data() || (spawn.x > 30 && spawn.y > 30));
    }

    // Stats: zombie level 1 in normal = 7 HP x 101..181 %.
    Rng stats_rng{ 3 };
    for (int i = 0; i < 50; ++i) {
        const auto stats = monster_stats(monsters, 0, 0, stats_rng);
        assert(stats.level == 1 && stats.hit_points >= 7 && stats.hit_points <= 12);
        assert(stats.armor_class == 5 && stats.to_hit == 8 && stats.a1_min == 1 && stats.a1_max == 3 && stats.exp == 33);
    }
    // Champions and uniques (uniques.hpp) on a small MonUMod: champion
    // chance 20; hell uniques take 3 mods, never twice; the stat rules.
    {
        UMods umods;
        umods.constants = { 20, 100, 75, 50, 200, 150, 100, 300, 200, 100, 75, 100, 50, 100, 75, 150, 0, 33, 33, 0, 50, 50, 33, 33, 33,
                 50, 50, 50, 66, 66, 66, 100, 100, 100 };
        for (int id : { 5, 6, 8, 9, 17, 18, 27, 28 }) umods.rows.push_back({ id, true, false, 0, "", "", {}, { 6, 6, 6 } });
        umods.rows.push_back({ 16, true, true, 0, "", "", { 1, 1, 1 }, {} });
        MonType type;
        type.velocity = 6;
        type.diff[2].res = { 0, 0, 0, 0, 0, 0 };
        int champions = 0;
        for (std::uint32_t seed = 1; seed <= 1000; ++seed) {
            Rng boss_rng{ seed };
            const auto boss = roll_boss(umods, type, 2, true, boss_rng);
            if (boss.kind == Boss::champion) { ++champions; assert(boss.mods == std::vector<int>{ 16 }); continue; }
            assert(boss.mods.size() == 3 && boss.mods[0] != boss.mods[1] && boss.mods[1] != boss.mods[2] && boss.mods[0] != boss.mods[2]);
        }
        assert(champions > 140 && champions < 260);                     // ~20 %
        const auto unique_stats = boss_stats(umods, type, Boss::unique, { 9, 28 }, 2);   // hell: fire enchanted, stone skin
        assert(unique_stats.level_add == 3 && unique_stats.exp_mult == 5 && unique_stats.hp_pct == 100 && unique_stats.elem == 0 && unique_stats.elem_min_pct == 66);
        assert(unique_stats.res_add[2] == 75 && unique_stats.res_add[0] == 50 && unique_stats.double_defense);
        const auto champion_stats = boss_stats(umods, type, Boss::champion, { 16 }, 1);
        assert(champion_stats.level_add == 2 && champion_stats.exp_mult == 3 && champion_stats.hp_pct == 150 && champion_stats.dmg_pct == 75 && champion_stats.tohit_pct == 56 && champion_stats.velocity_pct == 20);   // nightmare keeps 75 % (kBossBonus)
        const auto fast_stats = boss_stats(umods, type, Boss::unique, { 6 }, 0);       // fast: 2048 / 6 - 128 -> 100 max
        assert(fast_stats.velocity_pct == 100);
        // The champion kinds (FUN_005a1080 .. FUN_005a1280), nightmare.
        const auto ghostly = boss_stats(umods, type, Boss::champion, { 36 }, 1);
        assert(ghostly.velocity_pct == -33 && ghostly.phys_resist == 80 && ghostly.elem == 2 && ghostly.elem_len == 150 && ghostly.level_add == 2);
        const auto fanatic = boss_stats(umods, type, Boss::champion, { 37 }, 1);
        assert(fanatic.velocity_pct == 100 && fanatic.defense_pct == -70);
        assert(boss_stats(umods, type, Boss::champion, { 38 }, 1).life_after_pct == 100);
        const auto berserk = boss_stats(umods, type, Boss::champion, { 39 }, 1);
        assert(berserk.life_after_pct == -75 && berserk.level_add == 0 && berserk.exp_mult == 1 && berserk.velocity_pct == 0
               && berserk.dmg_pct == 225 && berserk.tohit_pct == 225 && berserk.hp_pct == 150);
        // A champion's name word (FUN_004ac870): its champion mod's; the
        // fixed rndname alone falls through to the table's last.
        assert(champion_word({ 16 }) == 0xc94 && champion_word({ 36 }) == 0x2b4c && champion_word({ 39 }) == 0x2b4f);
        assert(champion_word({ 16, 5 }) == 0xc94 && champion_word({}) == 0x2b4f);
        // Fire Enchanted's death blast (FUN_005a2620): max life x CE % (35 in
        // nightmare), less a third; rolled from 60 % up.
        assert((fire_blast(2037, 1) == std::pair{ 285, 475 }) && (fire_blast(100, 0) == std::pair{ 22, 38 }) && fire_blast(800, 2).second == 20);
        // Aura Enchanted: one of the six below level 20 at mlvl / 5..8;
        // superunique 37 Fanaticism.
        for (int seed = 0; seed < 200; ++seed) {
            const auto aura = boss_aura(18, seed, -1);
            assert(aura.skill != 118 && aura.level >= 2 && aura.level <= 3);
        }
        assert(boss_aura(40, 5, 37).skill == 122 && boss_aura(40, 5, 37).level == 5);
    }
    // A monster's look (FUN_005739d0, checked against game.exe on 2000
    // unit seeds): rand(sets) on its unit seed; without sets, a roll per
    // layer with a choice, a one-choice layer included.
    {
        const std::vector<Components> sets = { Components{ 1 }, Components{ 2 }, Components{ 3 } };
        for (std::uint32_t seed = 1; seed < 50; ++seed) {
            Rng unit_seed{ seed }, expect{ seed };
            assert(monster_look(&sets, {}, unit_seed)[0] == sets[std::size_t(expect(3))][0]);
        }
        Components choices{};
        choices[0] = 1; choices[2] = 3;
        Rng unit_seed{ 7 }, expect{ 7 };
        const auto look = monster_look(nullptr, choices, unit_seed);
        (void)expect(1);
        assert(look[0] == 0 && look[1] == 0 && look[2] == expect(3) && unit_seed.low == expect.low);
    }
    // Random object groups (FUN_00552610): 8 unconditional room-seed steps,
    // plus one more per slot whose roll <= ObjPrb picks an entry; the
    // entry is picked by cumulative weight on the second roll.
    {
        std::vector<ObjGroup> groups(6);
        groups[5] = { .id = { 100, 200, 0, 0, 0, 0, 0, 0 }, .density = { 30, 30 }, .weight = { 40, 60 } };   // 40 % id 100, 60 % id 200
        LevelMon slotted;
        slotted.obj_group = { 5, 0, 5, 0, 0, 0, 0, 0 };   // slots 0 and 2 rolled with a group
        slotted.obj_prob  = { 100, 0, 100, 0, 0, 0, 0, 0 };  // always fire
        Rng room_seed{ 7 }, mirror{ 7 };
        const auto picks = place_object_groups(slotted, groups, room_seed);
        // 8 outer + 2 inner (slots 0 and 2 both fired) = 10 steps.
        for (int step = 0; step < 10; ++step) (void)mirror.next();
        assert(room_seed.low == mirror.low);
        assert(picks.size() == 2);
        for (const auto& pick : picks) {
            assert((pick.object_id == 100 || pick.object_id == 200) && pick.density == 30);
        }
        // ObjPrb 0 fires nothing: 8 steps only, no picks.
        LevelMon empty_level;                    // all zeros
        Rng seed2{ 11 }, mirror2{ 11 };
        const auto no_picks = place_object_groups(empty_level, groups, seed2);
        for (int step = 0; step < 8; ++step) (void)mirror2.next();
        assert(seed2.low == mirror2.low);
        assert(no_picks.empty());
        // Throttle: past 75 % of the level's rooms, every roll's forced
        // to 100 (a guaranteed miss for any ObjPrb < 100); seed still
        // steps once per slot.
        LevelMon rarer = slotted;
        rarer.obj_prob = { 99, 0, 99, 0, 0, 0, 0, 0 };
        Rng throttled_seed{ 7 }, throttled_mirror{ 7 };
        const auto throttled_picks = place_object_groups(rarer, groups, throttled_seed, 25, 30);
        for (int step = 0; step < 8; ++step) (void)throttled_mirror.next();
        assert(throttled_seed.low == throttled_mirror.low);
        assert(throttled_picks.empty());
    }
    // Andariel (MonAI 34): distances, facing, her think's draws, the spray's sweep.
    {
        assert(ai_distance(3, -4) == 5 && ai_distance(0, 0) == 0);
        assert(unit_distance(0, 0, 3, 2) == 0 && unit_distance(4, 0, 3, 2) == 1 && unit_distance(5, 0, 3, 2) == 3 && unit_distance(0, 9, 3, 2) == 14);
        // Line of sight (FUN_00622920; tools/emu/sight.py has game.exe's word on these).
        {
            const auto wall = [](int wall_x, int wall_y) { return [=](int x, int y) { return x == wall_x && y == wall_y; }; };
            assert(sight_blocked(0, 0, 2, 10, 0, 2, wall(5, 0)) && sight_blocked(10, 0, 2, 0, 0, 2, wall(8, 0)));
            assert(!sight_blocked(0, 0, 2, 10, 0, 2, wall(1, 0)) && !sight_blocked(0, 0, 2, 3, 0, 2, wall(1, 0)));   // inside its own size; touching
            assert(sight_blocked(0, 0, 0, 2, 5, 0, wall(1, 3)) && !sight_blocked(0, 0, 0, 2, 5, 0, wall(1, 2)));
            assert(sight_blocked(0, 0, 1, 4, 4, 1, wall(2, 2)) && !sight_blocked(0, 0, 1, 4, 4, 1, wall(0, 0)));
        }
        // The target search's pick (FUN_005dd7f0; tools/emu/search.py --dump):
        // best, need sight, target, its distance (none: the nearest), foes
        // {distance, pet, away, dead, blocked, list}.
        {
            struct Case { int best; bool sight; int target, distance; std::vector<SearchFoe> foes; };
            const std::vector<Case> cases{
                { 50, 0, 2, 5, { {10, 0, 0, 0, 0}, {7, 1, 0, 0, 0}, {5, 1, 0, 0, 1}, {13, 0, 0, 0, 0}, {7, 1, 0, 0, 0}, {7, 1, 0, 0, 0}, {7, 1, 0, 0, 1} } },
                { 20, 1, -1, 56, { {72, 0, 0, 1, 0}, {60, 0, 0, 1, 0}, {56, 0, 0, 1, 0}, {91, 1, 0, 0, 1}, {68, 1, 0, 0, 1}, {21, 1, 0, 0, 1} } },
                { 50, 1, 2, 7, { {14, 0, 0, 0, 0}, {11, 0, 0, 0, 0}, {7, 1, 0, 0, 0} } },
                { 20, 0, 3, 16, { {33, 0, 1, 0, 0}, {49, 1, 0, 0, 1}, {10, 0, 0, 1, 1}, {16, 1, 0, 0, 1}, {34, 0, 1, 1, 0}, {34, 1, 0, 0, 0} } },
                { 35, 1, 4, 7, { {10, 0, 0, 0, 0}, {8, 0, 0, 0, 0}, {12, 0, 1, 1, 0}, {10, 0, 0, 0, 0}, {7, 1, 0, 0, 0}, {13, 1, 0, 0, 0}, {7, 0, 0, 0, 0}, {12, 0, 0, 1, 0} } },
                { 20, 1, -1, 59, { {59, 0, 0, 1, 0}, {69, 1, 0, 0, 1}, {66, 1, 0, 0, 1}, {27, 1, 0, 0, 1}, {103, 0, 0, 0, 1} } },
                { 20, 0, 5, 5, { {4, 0, 0, 1, 0, 0}, {8, 0, 0, 0, 0, 0}, {10, 0, 0, 0, 0, 0}, {7, 0, 1, 0, 0, 0}, {11, 1, 0, 0, 0, 0}, {5, 0, 0, 0, 1, 0}, {6, 0, 1, 0, 1, 8} } },
                { 35, 0, 6, 31, { {46, 0, 0, 0, 1, 0}, {49, 1, 0, 0, 0, 0}, {53, 1, 0, 0, 0, 0}, {58, 0, 1, 1, 0, 0}, {109, 0, 0, 0, 0, 0}, {22, 1, 0, 0, 0, 0}, {31, 0, 0, 0, 0, 8}, {39, 0, 1, 0, 0, 9} } },
                { 35, 1, 1, 12, { {21, 0, 0, 1, 1, 0}, {12, 1, 0, 0, 0, 0}, {39, 0, 0, 1, 1, 0}, {39, 1, 0, 0, 1, 0}, {39, 1, 0, 0, 1, 0}, {40, 1, 0, 0, 1, 0}, {18, 0, 0, 0, 0, 8}, {13, 0, 0, 0, 0, 9} } },
                { 50, 0, 1, 2, { {15, 0, 0, 1, 0, 0}, {2, 1, 0, 0, 1, 0}, {5, 0, 0, 0, 1, 8}, {2, 0, 0, 0, 0, 8}, {1, 0, 1, 0, 1, 8}, {4, 0, 0, 0, 0, 9} } },
                { 35, 1, 6, 38, { {36, 0, 0, 0, 0, 0}, {101, 0, 1, 0, 0, 0}, {70, 1, 0, 0, 0, 0}, {76, 1, 0, 0, 0, 0}, {44, 0, 0, 0, 0, 9}, {16, 0, 0, 0, 1, 9}, {38, 0, 0, 0, 0, 9} } },
                { 20, 1, 6, 32, { {45, 0, 0, 1, 0, 0}, {93, 0, 0, 0, 0, 0}, {92, 0, 1, 0, 0, 0}, {48, 0, 0, 0, 1, 8}, {38, 0, 0, 0, 1, 9}, {42, 0, 0, 0, 1, 9}, {32, 0, 0, 0, 0, 9}, {29, 0, 0, 0, 1, 9} } },
            };
            for (const auto& c : cases) {
                const auto pick = search_pick(c.foes, c.best, c.sight);
                assert(pick.target == c.target && (c.target >= 0 ? pick.best : pick.nearest) == c.distance);
            }
        }
        // A move's path (FUN_00679c80) and its chase check (FUN_006503f0 ->
        // FUN_00650350; tools/emu/moves.py --dump): to, steps, near, points,
        // walls (all from the start); then target, distance, stop, mover,
        // moved x / y, idx, count, at its end, budget, result, budget after.
        {
            struct Path { int to_x, to_y, steps, nearby; std::vector<std::pair<int, int>> points, walls; };
            const std::vector<Path> paths{
                { 4, -1, 5, 1, { {1, -1}, {2, -1}, {3, -2} }, { {1, 0}, {3, -1}, {3, 0} } },
                { 11, -11, 5, 1, { {4, -4}, {4, -4}, {5, -4}, {8, -7} }, { {5, -5}, {9, -8}, {9, -7} } },
                { -6, 3, 5, 1, { {-3, 2}, {-3, 2}, {-4, 3}, {-6, 3} }, { {-4, 2} } },
                { -1, -6, 2, 1, { {0, -2}, {0, -2}, {-1, -3} }, { {0, -3} } },
                { -7, -2, 5, 0, { {-1, 0}, {-1, 0}, {-2, -1}, {-4, -1}, {-5, -2} }, { {-5, -1}, {-2, 0} } },
                { 14, -12, 14, 0, { {2, -2}, {2, -2}, {2, -4}, {6, -8}, {8, -8}, {9, -9}, {10, -9}, {12, -11}, {12, -12} },
                  { {3, -4}, {3, -3}, {3, -2}, {8, -9}, {10, -10}, {13, -12}, {13, -11} } },
                { 6, 2, 5, 1, { {6, 2} }, {} },
            };
            for (const auto& c : paths)
                assert(toward_path(0, 0, c.to_x, c.to_y, c.steps, c.nearby, [&](int x, int y) {
                    return std::ranges::contains(c.walls, std::pair(x, y)); }) == c.points);
            const std::vector<std::array<int, 12>> chases{ {
                { 0, 0, 1, 0, 0, 0, 4, 5, 1, 20, 1, 20 }, { 1, 25, 1, 1, -7, 7, 0, 5, 1, 1, 2, 1 }, { 1, 6, 0, 1, 1, 3, 0, 2, 1, 0, 1, 0 },
                { 1, 4, 3, 1, -4, 7, 1, 6, 1, 20, 2, 19 }, { 1, 6, 3, 0, 3, 1, 1, 2, 0, 1, 1, 1 }, { 0, 0, 3, 0, 0, 0, 6, 2, 0, 0, 0, 0 },
                { 1, 6, 0, 1, -2, 7, 1, 2, 0, 3, 2, 2 }, { 1, 10, 3, 1, 4, -8, 4, 5, 1, 20, 2, 16 }, { 1, 0, 3, 1, 2, -3, 0, 6, 0, 0, 0, 0 },
                { 1, 0, 3, 1, 1, 2, 2, 4, 1, 1, 0, 1 }, { 1, 3, 0, 1, 8, 3, 0, 2, 1, 20, 2, 20 }, { 1, 0, 3, 1, -1, 0, 1, 1, 0, 1, 0, 1 },
            } };
            for (const auto& c : chases) {
                int budget = c[9];
                assert(chase_check(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], budget) == c[10] && budget == c[11]);
            }
        }
        // Mode 5 (FUN_005dcf70 / FUN_005dca70; search.py --dump): alignment,
        // need sight, {target, best, second, best}, foes {distance, align,
        // threat, self, monster, dead, skip, blocked, waking}.
        {
            struct Case { int align; bool sight; NearPick want; std::vector<NearFoe> foes; };
            const std::vector<Case> cases{
                { 0, 0, { 6, 2, -1, 2147483647 }, { {4, 2, 14, 0, 0, 1, 1, 0, 0}, {8, 2, 14, 0, 0, 0, 1, 0, 0}, {6, 2, 0, 0, 1, 1, 1, 0, 0}, {8, 2, 14, 0, 0, 1, 1, 1, 0}, {7, 2, 14, 0, 1, 0, 1, 1, 0}, {0, 2, 0, 0, 1, 1, 0, 1, 0}, {2, 2, 2, 0, 1, 0, 0, 1, 0}, {0, 1, 0, 1, 1, 0, 0, 0, 0} } },
                { 1, 1, { 3, 1, -1, 2147483647 }, { {4, 2, 2, 0, 1, 1, 0, 0, 0}, {4, 2, 14, 0, 0, 1, 0, 1, 0}, {7, 2, 14, 0, 0, 0, 0, 1, 0}, {1, 2, 14, 0, 0, 0, 0, 0, 0}, {5, 2, 14, 0, 1, 0, 0, 1, 0}, {0, 1, 0, 1, 1, 0, 0, 0, 0} } },
                { 2, 1, { -1, 2147483647, -1, 2147483647 }, { {64, 2, 1, 0, 1, 0, 0, 0, 0}, {55, 2, 14, 0, 0, 0, 0, 0, 0}, {0, 1, 0, 1, 1, 0, 0, 0, 1}, {54, 2, 2, 0, 1, 0, 1, 1, 0}, {86, 2, 1, 0, 1, 0, 1, 1, 0}, {57, 2, 14, 0, 0, 1, 0, 1, 0} } },
                { 2, 0, { -1, 2147483647, -1, 2147483647 }, { {36, 1, 2, 0, 1, 0, 1, 1, 0}, {0, 1, 10, 0, 1, 0, 1, 1, 0}, {35, 2, 1, 0, 1, 1, 0, 0, 0}, {0, 0, 0, 1, 1, 0, 0, 0, 0}, {34, 2, 14, 0, 1, 1, 0, 0, 0}, {45, 2, 1, 0, 1, 0, 0, 0, 1}, {9, 0, 1, 0, 1, 1, 0, 1, 0} } },
                { 1, 0, { -1, 2147483647, -1, 2147483647 }, { {0, 1, 0, 1, 1, 0, 0, 0, 1}, {10, 0, 10, 0, 1, 1, 0, 0, 0} } },
                { 1, 1, { -1, 2147483647, 4, 5 }, { {3, 2, 14, 0, 0, 0, 1, 0, 0}, {2, 2, 14, 0, 0, 0, 1, 1, 0}, {7, 2, 1, 0, 1, 1, 0, 1, 0}, {0, 1, 0, 1, 1, 0, 0, 0, 1}, {5, 2, 0, 0, 1, 0, 0, 0, 0} } },
                { 1, 0, { 3, 33, 2, 24 }, { {106, 2, 14, 0, 1, 0, 1, 0, 0}, {28, 0, 2, 0, 1, 0, 1, 0, 1}, {24, 0, 1, 0, 1, 0, 0, 1, 1}, {33, 2, 14, 0, 0, 0, 0, 0, 0}, {0, 1, 0, 1, 1, 0, 0, 0, 0}, {68, 2, 14, 0, 0, 1, 0, 0, 0} } },
                { 0, 1, { -1, 2147483647, 0, 26 }, { {26, 1, 1, 0, 1, 0, 0, 0, 1}, {72, 2, 14, 0, 0, 1, 0, 0, 0}, {0, 1, 0, 1, 1, 0, 0, 0, 1}, {52, 2, 1, 0, 1, 0, 1, 1, 0}, {69, 2, 14, 0, 0, 0, 1, 1, 0}, {50, 2, 1, 0, 1, 0, 1, 0, 0} } },
            };
            for (const auto& c : cases) {
                const auto pick = search_near(c.foes, c.align, c.sight);
                assert(pick.target == c.want.target && pick.best == c.want.best && pick.second == c.want.second && pick.second_best == c.want.second_best);
            }
            assert(confuse_align(1, false) == 0 && confuse_align(1, true) == 2 && confuse_align(0, true) == 2 && confuse_align(2, true) == 0 && confuse_align(0, false) == 0);
        }
        assert(direction64(0, 0, 5, 0) == 56 && direction64(0, 0, 0, 5) == 7 && direction64(0, 0, 3, 3) == 0 && direction64(9, 9, 6, 6) == 32);
        const std::array<int, 8> aip{ 30, 10, 30, 50 };
        for (const bool in_melee : { true, false })
            for (std::uint32_t seed = 1; seed < 50; ++seed) {
                Rng roll{ seed }, mirror{ seed };
                const auto act = andariel_think(in_melee, aip, roll);
                AndarielAct want = AndarielAct::walk;
                if (in_melee) want = mirror(100) < 30 ? AndarielAct::spray : AndarielAct::melee;
                else if (mirror(100) < 10) want = AndarielAct::idle;
                else if (mirror(100) < 30) want = mirror(100) < 50 ? AndarielAct::spray : AndarielAct::bolt;
                assert(act == want && roll.low == mirror.low);
            }
        Rng roll{ 3 };
        assert(andariel_think(false, { 0, 0, 100, 0 }, roll) == AndarielAct::bolt && andariel_think(true, { 100 }, roll) == AndarielAct::spray);
        // Facing +x+y: frame 4 aims 3 across the ring point, 8 at it, 12 3 the other way.
        assert(andariel_spray_aim(0, 4) == std::pair(0, 6) && andariel_spray_aim(0, 8) == std::pair(3, 3) && andariel_spray_aim(0, 12) == std::pair(6, 0));
        assert(andariel_spray_aim(56, 8) == std::pair(3, 0) && andariel_spray_aim(0, 0) == andariel_spray_aim(0, 4));
    }
    // The MonAI thinks: each rand(100) one seed step, a wander four, a circle one.
    {
        auto steps = [](std::uint32_t seed, Rng& rng) { Rng count{ seed }; int n = 0; while (count.low != rng.low || count.high != rng.high) { (void)count.next(); ++n; } return n; };
        auto no_away = [](int, bool) { return false; };
        auto ok_away = [](int, bool) { return true; };
        int state = 0;
        ThinkIn in{ .aip = { 60, 15, 75, 75 }, .state = &state };
        for (std::uint32_t seed = 1; seed < 50; ++seed) {           // Skeleton: aip1 % walk, in melee aip3 % (aip4 % A1) else stand aip2
            for (const bool in_melee : { false, true }) {
                in.in_melee = in_melee;
                Rng roll{ seed }, mirror{ seed };
                const auto act = mon_think("Skeleton", in, roll, no_away);
                MonAct want = MonAct::idle;
                if (!in_melee) { if (mirror(100) < 60) want = MonAct::walk; }
                else if (mirror(100) < 75) want = mirror(100) < 75 ? MonAct::a1 : MonAct::a2;
                assert(act.act == want && roll.low == mirror.low && (want != MonAct::idle || act.n == 15));
            }
        }
        in = { .aip = { 30, 10, 0, 20 }, .dist = 12, .state = &state };   // Zombie: out of aip2, wanders 3 (4 steps), in the Burial Grounds runs
        Rng roll{ 5 };
        const auto wander = mon_think("Zombie", in, roll, no_away);
        assert(wander.act == MonAct::wander && steps(5, roll) == 4 && std::abs(wander.x) <= 3 && std::abs(wander.y) <= 3 && (std::abs(wander.x) == 3 || std::abs(wander.y) == 3));
        in.level = 17; roll = Rng{ 5 };
        assert(mon_think("Zombie", in, roll, no_away).act == MonAct::run && steps(5, roll) == 0);
        in.level = 2; in.got_hit = true; roll = Rng{ 5 };
        assert(mon_think("Zombie", in, roll, no_away).act == MonAct::run && steps(5, roll) == 0);
        // QuillRat: past aip2's roll it backs off aip4; blocked and close, spikes.
        in = { .aip = { 10, 0, 0, 2 }, .dist = 3, .state = &state };
        roll = Rng{ 9 };
        assert(mon_think("QuillRat", in, roll, ok_away).act == MonAct::none && steps(9, roll) == 1);
        roll = Rng{ 9 };
        assert(mon_think("QuillRat", in, roll, no_away).act == MonAct::a2 && steps(9, roll) == 1);
        in.dist = 10; roll = Rng{ 9 };
        assert(mon_think("QuillRat", in, roll, no_away).act == MonAct::wander && steps(9, roll) == 4);
        // CorruptArcher: close, a blocked run-off falls through to the shot rolls.
        in = { .aip = { 60, 100, 14, 100, 20, 0, 0, 12 }, .dist = 4, .state = &state };
        roll = Rng{ 3 };
        assert(mon_think("CorruptArcher", in, roll, no_away).act == MonAct::a1 && steps(3, roll) == 2);
        in.dist = 15; roll = Rng{ 3 };
        const auto closer = mon_think("CorruptArcher", in, roll, no_away);
        assert(closer.act == MonAct::approach && closer.n == 12 && steps(3, roll) == 1);
        in.dist = 30; in.aip[0] = 0; roll = Rng{ 3 };
        assert(mon_think("CorruptArcher", in, roll, no_away).act == MonAct::run && steps(3, roll) == 1);
        // CorruptLancer: a run in past aip5 strikes on arrival without the aip2 roll.
        in = { .aip = { 60, 0, 9, 0, 15 }, .dist = 20, .state = &state };
        roll = Rng{ 4 };
        assert(mon_think("CorruptLancer", in, roll, no_away).act == MonAct::run && state == 1 && steps(4, roll) == 0);
        in.dist = 1; in.in_melee = true; roll = Rng{ 4 };
        assert(mon_think("CorruptLancer", in, roll, no_away).act == MonAct::a1 && state == 0 && steps(4, roll) == 0);
        roll = Rng{ 4 };
        assert(mon_think("CorruptLancer", in, roll, no_away).act == MonAct::idle && steps(4, roll) == 1);
        // CorruptRogue runs in past 20 - 3 x difficulty; Brute's circle is a seed step more.
        in = { .aip = { 60, 15, 75, 100, 20 }, .dist = 15, .difficulty = 2, .state = &state };
        roll = Rng{ 6 };
        assert(mon_think("CorruptRogue", in, roll, no_away).act == MonAct::run && steps(6, roll) == 0);
        in = { .aip = { 0, 0, 100, 45 }, .in_melee = true, .state = &state };
        in.aip[2] = 0; roll = Rng{ 6 };
        assert(mon_think("Brute", in, roll, no_away).act == MonAct::idle && steps(6, roll) == 2);
        in.aip[2] = 100; roll = Rng{ 6 };
        assert(mon_think("Brute", in, roll, no_away).act != MonAct::circle && steps(6, roll) == 2);
        assert(!traced_ai("Imp") && mon_think("Imp", in, roll, no_away).act == MonAct::untraced);
        // Fallen: a scare backs off (and a scream roll) or falls through;
        // charging walks in without a draw; a leader taunts, rallying.
        int command = 1;
        bool rally = false;
        in = { .aip = { 100, 10, 0, 20 }, .dist = 12, .state = &state, .dying = true, .command = &command, .rally = &rally };
        state = 0; roll = Rng{ 8 };
        assert(mon_think("Fallen", in, roll, ok_away).act == MonAct::none && state == 1 && command == 0 && steps(8, roll) == 1);
        command = 1; in.aip[1] = 15; roll = Rng{ 8 };                     // blocked: on as uncommanded, within aip2
        const auto fell_through = mon_think("Fallen", in, roll, no_away);
        assert(fell_through.act == MonAct::walk && fell_through.n == 7 && command == 0 && steps(8, roll) == 0);
        in.dying = false; command = 1; roll = Rng{ 8 };
        const auto charge = mon_think("Fallen", in, roll, no_away);
        assert(charge.act == MonAct::walk && charge.n == 0 && steps(8, roll) == 0);
        in.in_melee = true; roll = Rng{ 8 };
        assert(mon_think("Fallen", in, roll, no_away).act == MonAct::idle && steps(8, roll) == 1);
        command = 0; in.leader = true; in.in_melee = false; roll = Rng{ 8 };
        assert(mon_think("Fallen", in, roll, no_away).act == MonAct::s2 && rally && steps(8, roll) == 1);
        in.leader = false; in.in_melee = true; state = 1; roll = Rng{ 8 };   // scared: swings without the aip3 roll
        const auto swing = mon_think("Fallen", in, roll, no_away);
        assert((swing.act == MonAct::a1 || swing.act == MonAct::a2) && state == 0 && steps(8, roll) == 1);
        // FallenShaman: the rally roll, then a corpse raised, else two fire
        // rolls within aip5, then circle or stand.
        rally = false;
        in = { .aip = { 100, 0, 0, 24, 15 }, .dist = 5, .skill = { true, true }, .state = &state, .corpse = true, .command = &command, .rally = &rally };
        roll = Rng{ 2 };
        const auto raise = mon_think("FallenShaman", in, roll, no_away);
        assert(raise.act == MonAct::skill && raise.n == 0 && rally && steps(2, roll) == 2);
        in.corpse = false; roll = Rng{ 2 };
        assert(mon_think("FallenShaman", in, roll, no_away).act == MonAct::idle && steps(2, roll) == 4);
        in.dist = 15; in.aip[2] = 100; roll = Rng{ 2 };
        assert(mon_think("FallenShaman", in, roll, no_away).act == MonAct::circle && steps(2, roll) == 3);
        in.aip[1] = 100; in.dist = 14; roll = Rng{ 2 };
        const auto fire = mon_think("FallenShaman", in, roll, no_away);
        assert(fire.act == MonAct::skill && fire.n == 1 && steps(2, roll) == 2);
        // FoulCrowNest: lays every aip1 frames (no draw), else stands 20..29
        // on a seed step; blocked, the clock restarts all the same; done
        // laying aip3, it collapses.
        int laid = 0;
        state = 0;
        in = { .aip = { 100, 0, 2 }, .dist = 10, .skill = { true }, .state = &state, .frame = 150, .state2 = &laid };
        roll = Rng{ 4 };
        assert(mon_think("FoulCrowNest", in, roll, no_away).act == MonAct::skill && state == 150 && laid == 1 && steps(4, roll) == 0);
        in.frame = 200; roll = Rng{ 4 };
        Rng expect{ 4 };
        const auto wait = mon_think("FoulCrowNest", in, roll, no_away);
        assert(wait.act == MonAct::idle && wait.n == int(expect.next() % 10) + 20 && steps(4, roll) == 1);
        in.frame = 250; in.spot_free = false; roll = Rng{ 4 };
        assert(mon_think("FoulCrowNest", in, roll, no_away).act == MonAct::idle && state == 250 && laid == 1);
        laid = 2;
        assert(mon_think("FoulCrowNest", in, roll, no_away).act == MonAct::die);
        in.dist = 21;
        assert(mon_think("FoulCrowNest", in, roll, no_away).n == 25);
        // BloodRaven: past 45 stands 5; far from home she heads back (the
        // flag holds until within 5); past 20 closes in (4 steps); a raise
        // is rand(100) + rand(15) + three bits (6 steps), 5..19 off.
        int back = 0, raised = 0;
        state = 0;
        in = { .dist = 46, .skill = { true, true }, .state = &state, .command = &back, .state2 = &raised };
        roll = Rng{ 5 };
        assert(mon_think("BloodRaven", in, roll, no_away).n == 5 && steps(5, roll) == 0);
        in.dist = 10; in.home_dist = 50;
        assert(mon_think("BloodRaven", in, roll, no_away).act == MonAct::home && back == 1);
        in.home_dist = 6;
        assert(mon_think("BloodRaven", in, roll, no_away).act == MonAct::home && steps(5, roll) == 0);
        in.dist = 30; in.home_dist = 5;
        const auto br_close = mon_think("BloodRaven", in, roll, no_away);
        assert(back == 0 && br_close.act == MonAct::around && br_close.n == 15 && steps(5, roll) == 4 && state == 0);
        in.dist = 10; state = 97; roll = Rng{ 5 };
        const auto br_raise = mon_think("BloodRaven", in, roll, no_away);
        const int reach = std::max(std::abs(br_raise.x), std::abs(br_raise.y));
        assert(br_raise.act == MonAct::skill && br_raise.n == 0 && reach >= 5 && reach < 20 && raised == 1 && state == 0 && steps(5, roll) == 6);
        in.in_melee = true; in.dist = 3; roll = Rng{ 5 };                  // in melee no raise; 30 % backs off, else A1
        assert(mon_think("BloodRaven", in, roll, no_away).act == MonAct::a1 && steps(5, roll) == 1 && state == 3);
        in.in_melee = false; in.got_hit = true; in.dist = 10; raised = 8; roll = Rng{ 5 };   // raised enough; hit: no strike
        expect = Rng{ 5 };
        const bool about = expect(100) < 5;
        const auto hit = mon_think("BloodRaven", in, roll, no_away);
        assert(hit.act == (about ? MonAct::around : MonAct::circle) && steps(5, roll) == (about ? 5 : 2));
        // SkeletonMage (aip 35 9 30 5 0 18 20 5, skmage_*1): within 18 aip1 %
        // shoots (aip5 0: the back-off draws but never goes); else circles 4
        // (one more step) or stands 5; past 9 the close-in is rolled twice.
        in = { .aip = { 100, 9, 30, 5, 0, 18, 20, 5 }, .dist = 4, .state = &state };
        roll = Rng{ 6 };
        assert(mon_think("SkeletonMage", in, roll, no_away).act == MonAct::a1 && steps(6, roll) == 2);
        in.dist = 30; in.aip[2] = 100; roll = Rng{ 6 };
        const auto mage_in = mon_think("SkeletonMage", in, roll, no_away);
        assert(mage_in.act == MonAct::approach && mage_in.n == 9 && steps(6, roll) == 1);
        in.dist = 20; in.aip[2] = 0; in.aip[6] = 100; roll = Rng{ 6 };
        assert(mon_think("SkeletonMage", in, roll, no_away).act == MonAct::circle && steps(6, roll) == 4);
        // GargoyleTrap (aip 24 20 12 15): a shot's aip3 waits first; within
        // 5 of an axis and aip1 it rolls aip2 %; else stands aip4, no draw.
        state = 7;
        in = { .aip = { 24, 100, 12, 15 }, .dist = 10, .skill = { true }, .state = &state, .off_x = 3, .off_y = 9 };
        roll = Rng{ 7 };
        const auto trap_wait = mon_think("GargoyleTrap", in, roll, no_away);
        assert(trap_wait.act == MonAct::idle && trap_wait.n == 7 && state == 0 && steps(7, roll) == 0);
        assert(mon_think("GargoyleTrap", in, roll, no_away).act == MonAct::skill && state == 12 && steps(7, roll) == 1);
        state = 0; in.off_x = 6; roll = Rng{ 7 };
        const auto trap_off = mon_think("GargoyleTrap", in, roll, no_away);
        assert(trap_off.act == MonAct::idle && trap_off.n == 15 && steps(7, roll) == 0);
        // The Countess (FUN_005e5c50): away from home's room she walks back;
        // a target elsewhere gets the firewall from home only; the map AI
        // points in turn, then rand(100); 700 frames on they start over.
        const std::pair<int, int> path[] = { { 1, 2 }, { 3, 4 } };
        int fired = 0, when = 0;
        in = { .aip = { 0, 5, 100 }, .in_melee = true, .dist = 30, .state = &fired, .frame = 10, .state2 = &when };
        roll = Rng{ 8 };
        assert(countess_think(in, { .away = true }, path, roll).act == MonAct::home);
        assert(countess_think(in, { .target_away = true }, path, roll).act == MonAct::home);
        assert(countess_think(in, { .target_away = true, .at_home = true }, path, roll).n == 10 && steps(8, roll) == 0);
        in.dist = 5;
        const auto think_a = countess_think(in, {}, path, roll), think_b = countess_think(in, {}, path, roll);
        assert(think_a.act == MonAct::point && think_a.x == 1 && think_b.y == 4 && fired == 2 && when == 10 && steps(8, roll) == 0);
        assert(countess_think(in, {}, path, roll).act == MonAct::a1 && steps(8, roll) == 1);
        in.frame = 711;
        assert(countess_think(in, {}, path, roll).act == MonAct::a1 && fired == 0);
        assert(countess_think(in, {}, path, roll).act == MonAct::point);
        in.home_dist = 41;
        assert(countess_think(in, {}, path, roll).act == MonAct::home);
        assert(d2d::rules::monster_skill_level(10, 2) == 17);
    }
    std::puts("ok");
}
