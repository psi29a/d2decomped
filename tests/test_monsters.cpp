// Monster spawning and stats over hand-made tables: the region takes
// every listed type once, rooms fill by density, Fallen come as a leader
// with a party, nothing lands on a blocked subtile or by the entrance.
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>

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
    std::puts("ok");
}
