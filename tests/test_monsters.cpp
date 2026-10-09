// SPDX-License-Identifier: GPL-3.0-or-later
// Monster spawning and stats over hand-made tables: the region takes
// every listed type once, rooms fill by density, Fallen come as a leader
// with a party, nothing lands on a blocked subtile or by the entrance.
#include <merc.hpp>
#include <missiles.hpp>
#include <pets.hpp>
#include <shadows.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <town_npcs.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <utility>
#include <vector>

using namespace d2d::rules;

// Missile flights game.exe flew (tools/emu/missiles.py --dump: a Blood
// Moor game, FUN_0059fa30 then srvdofunc 1 a frame at a time), each to its
// range or the foe it struck, against rules::MissileFlight: the frame it
// ended, how, where, and an FNV-1a of every frame's 16.16 position.
static void missile_flights() {
    struct Case { int from_x, from_y, to_x, to_y, vel, vel_lev, max_vel, accel, level, range, activate, bolt, foe_x, foe_y, foe_size, frames, struck;
                  std::uint32_t end_x, end_y, hash; };
    static constexpr Case kCases[] = {
    { 4921, 4749, 4918, 4748, 8, 0, 8, 0, 4, 40, 0, -1, 4921, 4747, 2, 40, 0, 0x132b4300u, 0x1288ca60u, 0x6b670dc1u },   // shafire1 lvl 4
    { 4650, 4684, 4648, 4681, 24, 0, 24, 0, 29, 40, 0, -1, 4647, 4681, 2, 3, 1, 0x1228a37eu, 0x1249af70u, 0x82eceda0u },   // spike5 lvl 29
    { 4932, 4584, 4935, 4579, 12, 0, 12, 0, 2, 77, 0, 2, 4935, 4584, 2, 77, 0, 0x13528000u, 0x11ce8000u, 0x9f6a00d7u },   // chargedbolt lvl 2
    { 4710, 4660, 4705, 4651, 24, 0, 24, 0, 31, 40, 0, -1, 0, 0, 0, 40, 0, 0x1250c7b0u, 0x120d1790u, 0xae0a0f58u },   // spike5 lvl 31
    { 4912, 4662, 4905, 4670, 24, 1, 56, -1, 17, 40, 0, -1, 4908, 4665, 2, 40, 0, 0x131072b4u, 0x125b2d7cu, 0x365c26b8u },   // random lvl 17
    { 5005, 4739, 5008, 4736, 20, 0, 20, 0, 20, 50, 0, -1, 0, 0, 0, 50, 0, 0x13aea460u, 0x12625ba0u, 0x6d57586cu },   // firebolt lvl 20
    { 4942, 4557, 4949, 4555, 24, 0, 24, 0, 20, 40, 0, -1, 4949, 4555, 2, 6, 1, 0x1354fe30u, 0x11cba8c4u, 0x7b68752bu },   // arrow lvl 20
    { 5000, 4662, 4993, 4669, 12, 0, 12, 0, 18, 50, 0, -1, 4994, 4666, 2, 50, 0, 0x13749d60u, 0x124a62a0u, 0x604653fcu },   // icebolt lvl 18
    { 4738, 4729, 4740, 4730, 20, 0, 20, 0, 2, 50, 0, -1, 4737, 4730, 2, 50, 0, 0x12ac7d06u, 0x128e5408u, 0xbf4734a3u },   // andypoisonbolt lvl 2
    { 4995, 4737, 4993, 4737, 8, 0, 8, 0, 31, 40, 0, -1, 4993, 4738, 2, 5, 1, 0x1381a000u, 0x12818000u, 0xc6db7fdfu },   // shafire1 lvl 31
    { 4918, 4727, 4917, 4734, 24, 0, 24, 0, 9, 40, 0, -1, 0, 0, 0, 40, 0, 0x133031a0u, 0x12a40cb0u, 0x3bbd5c24u },   // arrow lvl 9
    { 4607, 4734, 4611, 4728, 10, 8, 10, 0, 9, 40, 0, -1, 4608, 4731, 2, 3, 1, 0x1200f93au, 0x127c458eu, 0x69e2ddbfu },   // spike1 lvl 9
    { 4700, 4726, 4701, 4727, 10, 10, 28, 0, 2, 40, 0, -1, 4702, 4727, 2, 2, 1, 0x125d4ba0u, 0x12774ba0u, 0x0c4b47e9u },   // random lvl 2
    { 4694, 4765, 4683, 4746, 12, 0, 12, 0, 24, 50, 0, -1, 4687, 4750, 3, 28, 1, 0x124ea6e4u, 0x128fd87cu, 0xd36e354fu },   // icebolt lvl 24
    { 4683, 4683, 4684, 4684, 12, 0, 12, 0, 27, 77, 0, 3, 4682, 4685, 2, 4, 1, 0x124b8000u, 0x124d3000u, 0xd4efaa33u },   // chargedbolt lvl 27
    { 4994, 4574, 4991, 4575, 10, 8, 10, 0, 21, 40, 0, -1, 0, 0, 0, 40, 0, 0x134b53a0u, 0x11f0bfb8u, 0x90f60bd2u },   // spike1 lvl 21
    { 4747, 4563, 4750, 4561, 20, 0, 20, 0, 1, 50, 0, -1, 4749, 4563, 2, 2, 1, 0x128d1050u, 0x11d27746u, 0x3e89ec31u },   // andypoisonbolt lvl 1
    { 4979, 4631, 4976, 4634, 20, 0, 20, 0, 11, 50, 0, -1, 4977, 4633, 2, 3, 1, 0x137182f0u, 0x12197d10u, 0xd13fc487u },   // firebolt lvl 11
    };
    for (const auto& test_case : kCases) {
        auto flight = MissileFlight::launch(test_case.from_x, test_case.from_y, test_case.to_x, test_case.to_y,
                                            missile_velocity(test_case.vel, test_case.vel_lev, test_case.level), test_case.max_vel << 8, test_case.accel);
        const bool at_itself = test_case.to_x == test_case.from_x && test_case.to_y == test_case.from_y;
        const int to_x = test_case.to_x + (at_itself ? 1 : 0), to_y = test_case.to_y + (at_itself ? 1 : 0);
        if (test_case.bolt >= 0) flight.points = wiggle_points(test_case.from_x, test_case.from_y, to_x, to_y, test_case.range, Rng{ std::uint32_t(to_x + test_case.bolt) });
        std::uint32_t hash = 0x811C9DC5u;
        int frames = 0, struck = 0;
        for (int left = test_case.range;;) {
            const bool flying = flight.step([](int, int) { return false; });
            ++frames;
            for (const std::uint32_t value : { flight.x, flight.y })
                for (int shift = 0; shift < 32; shift += 8) hash = (hash ^ ((value >> shift) & 0xffu)) * 0x01000193u;
            if (!flying || --left < 1) break;
            if (left > test_case.range - test_case.activate || test_case.foe_size == 0) continue;
            if (std::ranges::any_of(flight.crossed, [&](const auto& spot) { return missile_touches(spot.first, spot.second, test_case.foe_x, test_case.foe_y, test_case.foe_size); })) {
                struck = 1;
                break;
            }
        }
        if (frames != test_case.frames || struck != test_case.struck || flight.x != test_case.end_x || flight.y != test_case.end_y || hash != test_case.hash) {
            std::printf("missile %d,%d -> %d,%d: %d frames struck %d at %#x,%#x hash %#x, game.exe %d %d %#x,%#x %#x\n", test_case.from_x, test_case.from_y,
                        test_case.to_x, test_case.to_y, frames, struck, flight.x, flight.y, hash, test_case.frames, test_case.struck, test_case.end_x, test_case.end_y, test_case.hash);
            std::abort();
        }
    }
    // A wall: the step that reaches it ends the flight at the centre of the
    // last free point (FUN_00650150), not of the step's end.
    auto flight = MissileFlight::launch(100, 100, 120, 100, missile_velocity(24, 0, 1), 24 << 8, 0);
    assert(flight.velocity == 4608 && flight.aim_x == 4096 && flight.aim_y == 0);
    int frames = 1;
    while (flight.step([](int x, int) { return x >= 104; })) ++frames;
    assert(frames == 4 && flight.x == (103u << 16) + 0x8000 && flight.y == (100u << 16) + 0x8000);
    assert(missile_velocity(10, 8, 4) == (14 << 8) * 75 / 100 && missile_range(40, 5, 3) == 55);
}

int main() {
    missile_flights();
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
    // A noRatio row (FUN_006538a0, flag +0xc & 4) takes its MonStats values
    // as written: life 101..181, defense 84, experience 111, damage 51..151.
    {
        auto raw = monsters;
        raw.types[0].no_ratio = true;
        Rng raw_rng{ 3 };
        const auto stats = monster_stats(raw, 0, 0, raw_rng);
        assert(stats.hit_points >= 101 && stats.hit_points <= 181 && stats.armor_class == 84 && stats.exp == 111);
        assert(stats.to_hit == 101 && stats.a1_min == 51 && stats.a1_max == 151);
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
        // The name over the life bar (FUN_00454ad0): uniques gold, champions
        // blue, Duriel gold whatever he is; Andariel white.
        assert(bar_name_colour(Boss::superunique, "corruptrogue3") == kNameGold && bar_name_colour(Boss::champion, "zombie1") == kNameBlue);
        assert(bar_name_colour(Boss::none, "duriel") == kNameGold && bar_name_colour(Boss::none, "andariel") == kNameWhite && bar_name_colour(Boss::minion, "fallen1") == kNameWhite);
        // Act bosses (FUN_005b1cf0, by BaseId): Andariel mod 22, Blood Raven
        // 12, 22 and half freeze, the Smith 22, all flag 8 (gold, as a unique);
        // the uber Andariel (0x2c3) and a plain monster aren't.
        const auto andariel = act_boss(0x9c, 0x9c), blood_raven = act_boss(0x10b, 0x10b);
        assert(andariel && andariel->mods == std::vector<int>{ 22 } && !andariel->half_freeze);
        assert(blood_raven && (blood_raven->mods == std::vector<int>{ 12, 22 }) && blood_raven->half_freeze);
        assert(act_boss(0x192, 0x192) && act_boss(0x192, 0x192)->mods == std::vector<int>{ 22 });
        assert(!act_boss(0x2c3, 0x9c) && !act_boss(5, 5) && bar_name_colour(Boss::unique, "andariel") == kNameGold);
        // A monster's sound set (FUN_004ca410): the Countess's own; a boss
        // or minion zombie's UMonSound; a plain one's MonSound.
        MonType zombie_sounds;
        zombie_sounds.sound = "zombie"; zombie_sounds.usound = "zombieunique";
        assert(boss_sound(zombie_sounds, Boss::superunique, "countess") == "countess" && boss_sound(zombie_sounds, Boss::superunique, "") == "zombieunique");
        assert(boss_sound(zombie_sounds, Boss::minion, "") == "zombieunique" && boss_sound(zombie_sounds, Boss::none, "") == "zombie");
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
        // A monster's door (FUN_005dcd50): the nearest under 9 squared, the
        // first on a tie; in reach (FUN_00623660): touching, else 2 about
        // its rect, a small unit's corners 1 in (the object's spot less
        // half its size, so a 1x1's far corner is in, its near one out).
        {
            const std::array<std::pair<int, int>, 4> spots{ { { 2, 2 }, { 1, -1 }, { -1, 1 }, { 3, 0 } } };
            assert(door_pick(spots) == 1 && door_pick(std::span(spots).subspan(3)) == -1 && door_pick(std::span(spots).first(1)) == 0);
            assert(object_reach(2, 0, 2, 1, 3) && object_reach(-2, -2, 1, 3, 1) && object_reach(2, 2, 1, 7, 1));
            assert(object_reach(2, 2, 1, 1, 1) && !object_reach(-2, -2, 1, 1, 1) && object_reach(-2, -2, 3, 1, 1) && !object_reach(-3, 0, 1, 1, 1));
            assert(object_reach(1, 1, 1, 0, 0) && !object_reach(-1, 2, 1, 0, 0));
        }
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
            for (const auto& entry : cases) {
                const auto pick = search_pick(entry.foes, entry.best, entry.sight);
                assert(pick.target == entry.target && (entry.target >= 0 ? pick.best : pick.nearest) == entry.distance);
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
            for (const auto& route : paths)
                assert(toward_path(0, 0, route.to_x, route.to_y, route.steps, route.nearby, [&](int x, int y) {
                    return std::ranges::contains(route.walls, std::pair(x, y)); }) == route.points);
            // A player's path (type 7, FUN_00679ed0; its search leg FUN_0067b850,
            // moves.py --dump): to, near, to a unit, points, walls.
            struct PlayerRoute { int to_x, to_y, nearby; bool to_unit; std::vector<std::pair<int, int>> points, walls; };
            const std::vector<PlayerRoute> player_routes{
                { -8, 5, 1, false, { {-2, 2}, {-2, 2}, {-3, 3}, {-5, 3}, {-6, 4}, {-7, 4} }, { {-3, -1}, {-3, 0}, {-3, 2}, {1, -1} } },
                { -3, -3, 0, false, { {-1, -1}, {-1, -1}, {-1, -3}, {-2, -4} },
                  { {-2, -3}, {-2, -2}, {-2, -1}, {-1, -5}, {-1, 0}, {-1, 1}, {0, 1}, {1, -3}, {1, 0} } },
                { 9, 6, 0, false, { {-1, 1}, {1, 3}, {5, 3}, {7, 5}, {8, 5}, {9, 6} },
                  { {-2, 1}, {-1, -2}, {-1, -1}, {-1, 0}, {-1, 3}, {0, 1}, {1, -1}, {1, 0}, {1, 1}, {1, 2}, {3, 2}, {4, 2}, {4, 4}, {7, 3}, {7, 4}, {7, 6}, {9, 5} } },
                { -13, 6, 1, true, { {0, 1}, {-1, 2}, {-5, 2}, {-9, 6}, {-13, 6} },
                  { {-13, 5}, {-13, 7}, {-12, 7}, {-11, 2}, {-11, 4}, {-11, 5}, {-10, 4}, {-9, 2}, {-9, 5}, {-9, 7}, {-8, 3}, {-7, 1}, {-7, 6}, {-6, 1},
                    {-6, 4}, {-5, 1}, {-5, 4}, {-3, 1}, {-3, 3}, {-3, 4}, {-2, -1}, {-2, 0}, {-1, 0}, {-1, 1}, {-1, 4}, {1, 1} } },
                { 3, 2, 0, true, { {0, 2}, {-1, 3}, {0, 4}, {1, 4}, {3, 2} },
                  { {-3, 2}, {-2, -1}, {-1, 1}, {0, 3}, {1, -3}, {1, -2}, {1, -1}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 5}, {2, 5} } },
            };
            for (const auto& route : player_routes)
                assert(player_path(0, 0, route.to_x, route.to_y, route.nearby, route.to_unit, [&](int x, int y) {
                    return std::ranges::contains(route.walls, std::pair(x, y)); }) == route.points);
        // The merc's think (MonAI 61 Hireable, FUN_005e52d0; tools/emu/merc.py
        // --dump: game.exe's own runs): class, size, mode, owner (from the merc,
        // at 5000, 5000), his mode, his path end, the footstep cursor, bit 0x40,
        // town, the target's distance, the x where level 2 starts, the seed;
        // what it comes to, the seed after, the moves tried (spot, mode, type,
        // pace, steps, found a path) and the footsteps.
        {
            using Kind = d2d::rules::MercAct::Kind;
            struct Tried { int x, y, mode, type, pct, steps, found; };
            struct MercCase {
                int cls, size, mode, owner_x, owner_y, owner_mode, end_x, end_y, cursor, bit40, town, target, split;
                std::uint32_t low, high; Kind kind; std::uint32_t after_low, after_high;
                std::vector<Tried> tried; std::vector<std::pair<int, int>> ring;
            };
            const std::vector<MercCase> merc_cases{
                { 560, 3, 1, 3, 0, 3, 3, 0, 19, 0, 0, 2, 9999, 2669600009u, 2981770389u, Kind::attack, 2669600009u, 2981770389u, {  }, { { 6, -8 }, { 32, -3 }, { 1, -4 }, { 7, 24 }, { 15, -23 }, { 4, 25 }, { 33, -30 }, { 26, 14 }, { 0, 18 }, { -5, 0 }, { 29, 17 }, { 20, -13 }, { -15, -21 }, { -6, -7 }, { -18, -14 }, { 7, 15 }, { 33, 6 }, { 31, 12 }, { 3, 19 }, { 11, 20 } } },
                { 359, 2, 1, 1, 1, 4, 1, 1, 9, 1, 0, 6, -11, 3695689187u, 1405483338u, Kind::attack, 224184569u, 1541443759u, {  }, { { 4, -16 }, { 1, -28 }, { 6, -21 }, { 23, 23 }, { 16, -21 }, { 18, -22 }, { 22, 7 }, { 1, 27 }, { 29, 3 }, { 6, 16 }, { -23, 17 }, { 13, -26 }, { -23, -18 }, { -4, 6 }, { 30, 21 }, { -5, -27 }, { 22, 28 }, { 27, 29 }, { 25, -27 }, { -11, 17 } } },
                { 560, 1, 1, 19, 79, 2, 13, 82, 18, 0, 0, 21, 9999, 3433846359u, 3868466399u, Kind::moved, 3433846359u, 3868466399u, { { 13, 82, 2, 0, 0, 77, 1 } }, { { 1, 64 }, { 17, 75 }, { 20, 51 }, { 3, 75 }, { 17, 64 }, { 30, 105 }, { 16, 102 }, { 2, 80 }, { 1, 51 }, { -9, 65 }, { 5, 64 }, { 22, 62 }, { 38, 63 }, { 15, 104 }, { 5, 58 }, { 9, 52 }, { 46, 108 }, { 9, 85 }, { -4, 85 }, { 14, 106 } } },
                { 338, 3, 1, 0, 1, 6, 7, 13, 13, 1, 0, -1, 9999, 641268514u, 1402542203u, Kind::moved, 3074273634u, 632672597u, { { -5, 4, 2, 0, 0, 0, 1 } }, { { -22, -27 }, { -7, -24 }, { 19, 27 }, { 3, 28 }, { -30, -10 }, { 23, -7 }, { 22, -25 }, { -25, 5 }, { -1, -5 }, { -17, 21 }, { -11, -5 }, { -16, 19 }, { 1, 25 }, { -5, -23 }, { -26, -22 }, { 28, 10 }, { 21, 27 }, { -7, 3 }, { -3, -3 }, { 24, 16 } } },
                { 359, 3, 1, -7, 8, 1, -19, 2, 5, 0, 1, -1, -5, 3072168131u, 2374095845u, Kind::moved, 3072168131u, 2374095845u, { { -19, -6, 2, 0, 60, 40, 1 } }, { { -34, 1 }, { -2, 27 }, { -26, 14 }, { -11, 32 }, { -36, -11 }, { -3, 33 }, { -21, 9 }, { 4, -15 }, { 10, 34 }, { 19, 17 }, { 19, 18 }, { 20, 1 }, { -6, 10 }, { -6, 18 }, { -15, -2 }, { -21, -18 }, { -4, 26 }, { -5, 16 }, { -3, -7 }, { -23, -6 } } },
                { 561, 1, 1, -19, 8, 2, -17, 8, 2, 0, 0, 10, 9999, 2923568628u, 2665762972u, Kind::moved, 2923568628u, 2665762972u, { { -9, 16, 2, 0, 0, 40, 1 } }, { { -17, 3 }, { 3, -2 }, { -31, 6 }, { -23, 15 }, { -49, -6 }, { -38, 35 }, { -15, 7 }, { -5, 13 }, { -10, 30 }, { -26, 3 }, { 11, 2 }, { -10, -21 }, { -40, 10 }, { -45, 7 }, { -9, 36 }, { -27, 16 }, { 1, -3 }, { 9, -17 }, { -1, -6 }, { -19, -8 } } },
                { 560, 3, 1, 1, 0, 2, -9, -10, 7, 0, 1, 35, 9999, 1253879914u, 2458296693u, Kind::moved, 1509945283u, 166271432u, { { 5, -3, 2, 7, 0, 0, 1 } }, { { -28, -5 }, { 6, 28 }, { -11, 21 }, { 19, -27 }, { -15, 3 }, { 5, -7 }, { -12, 19 }, { -18, 22 }, { -23, -14 }, { -16, 30 }, { 30, -29 }, { 24, 11 }, { 22, -14 }, { 22, -13 }, { -17, -20 }, { -10, -12 }, { 11, 25 }, { 17, 24 }, { 25, 26 }, { -6, -25 } } },
                { 271, 1, 1, 26, -19, 1, 26, -19, 5, 0, 0, -1, 9999, 2502527425u, 1069698752u, Kind::moved, 2502527425u, 1069698752u, { { 51, -10, 2, 0, 60, 77, 1 } }, { { 14, -29 }, { 5, 0 }, { 37, -23 }, { 51, 11 }, { 51, -10 }, { 39, 3 }, { 0, -31 }, { 35, -37 }, { 53, -21 }, { 14, -41 }, { 12, -25 }, { 34, -39 }, { 17, -13 }, { -4, -26 }, { -2, -20 }, { 6, -26 }, { 46, 2 }, { 19, -31 }, { 32, -43 }, { 24, -36 } } },
                { 338, 2, 1, -130, 54, 4, -130, 54, 13, 0, 1, 13, 9999, 1045999619u, 2483460015u, Kind::teleport, 1045999619u, 2483460015u, {  }, { { -144, 65 }, { -122, 27 }, { -114, 58 }, { -111, 83 }, { -109, 59 }, { -132, 74 }, { -118, 28 }, { -130, 50 }, { -131, 56 }, { -130, 59 }, { -105, 78 }, { -113, 55 }, { -140, 60 }, { -158, 57 }, { -147, 67 }, { -118, 61 }, { -150, 49 }, { -123, 42 }, { -105, 62 }, { -138, 55 } } },
                { 359, 1, 1, 26, 16, 3, 36, 27, 14, 0, 0, 32, 9999, 1964112674u, 368388989u, Kind::moved, 1964112674u, 368388989u, { { 28, 13, 2, 0, 60, 77, 0 }, { 28, 13, 15, 15, 60, 77, 1 } }, { { 8, 40 }, { 27, 1 }, { 6, 46 }, { 44, -4 }, { 17, 32 }, { -2, 45 }, { 9, -2 }, { 42, 20 }, { 25, 46 }, { 29, 6 }, { 4, 25 }, { 6, 38 }, { 19, 23 }, { 28, 13 }, { 48, 33 }, { 24, 9 }, { 16, 44 }, { 40, 34 }, { 44, -10 }, { 47, 15 } } },
                { 359, 1, 1, -10, 4, 3, -8, -8, 17, 0, 1, 22, -9, 1439147873u, 4145136543u, Kind::moved, 1439147873u, 4145136543u, { { -16, 0, 2, 0, 60, 40, 0 }, { -16, -8, 2, 0, 60, 40, 1 } }, { { 7, 24 }, { 19, 20 }, { 5, 0 }, { -27, -19 }, { -19, -8 }, { -2, 9 }, { -37, 31 }, { -35, 4 }, { -30, 17 }, { -7, -23 }, { -29, 27 }, { -1, -11 }, { -2, -5 }, { -2, -25 }, { 6, 16 }, { -39, -2 }, { -13, -12 }, { 11, 11 }, { -32, 13 }, { -38, -18 } } },
                { 560, 2, 1, 1, 0, 1, -6, 0, 4, 0, 0, 25, -18, 1069461526u, 1496492920u, Kind::moved, 3146213266u, 578236269u, { { 4, 4, 2, 7, 0, 0, 0 }, { -4, 0, 2, 0, 0, 0, 0 }, { -6, 0, 2, 0, 0, 40, 1 } }, { { 3, 26 }, { -18, 13 }, { -5, 0 }, { -18, -28 }, { -9, -16 }, { 11, -15 }, { -4, 4 }, { -19, -18 }, { 1, 15 }, { 13, -1 }, { -10, -6 }, { -12, -9 }, { -2, -17 }, { 15, 11 }, { -27, 17 }, { 14, 6 }, { -26, 19 }, { 27, 5 }, { -17, 17 }, { 23, 23 } } },
                { 359, 3, 4, -18, -9, 2, -28, 3, 19, 0, 1, -1, -4, 4013210570u, 4007475079u, Kind::moved, 4013210570u, 4007475079u, { { -36, 3, 2, 0, 0, 40, 0 }, { -36, -5, 2, 0, 0, 40, 0 }, { -28, -5, 2, 0, 0, 40, 0 }, { -20, -5, 2, 0, 0, 40, 0 }, { -20, 3, 2, 0, 0, 40, 0 }, { -20, 11, 2, 0, 0, 40, 0 }, { -28, 11, 2, 0, 0, 40, 0 }, { -36, 11, 2, 0, 0, 40, 1 } }, { { -16, 16 }, { -16, -20 }, { -13, 5 }, { -30, 11 }, { -35, 6 }, { -3, -12 }, { -28, -28 }, { -35, -38 }, { -18, 17 }, { -21, 20 }, { -31, -18 }, { 0, 19 }, { -22, 21 }, { -8, 20 }, { 5, -13 }, { -28, 9 }, { -9, -26 }, { -32, -22 }, { -16, -34 }, { -47, -14 } } },
                { 271, 3, 4, -6, 19, 3, -6, 19, 9, 1, 0, -1, 9999, 2020693362u, 4212245276u, Kind::failed, 447995667u, 1416854706u, { { -14, 27, 2, 0, 60, 40, 0 }, { -14, 19, 2, 0, 60, 40, 0 }, { -14, 11, 2, 0, 60, 40, 0 }, { -6, 11, 2, 0, 60, 40, 0 }, { 2, 11, 2, 0, 60, 40, 0 }, { 2, 19, 2, 0, 60, 40, 0 }, { 2, 27, 2, 0, 60, 40, 0 }, { -6, 27, 2, 0, 60, 40, 0 }, { 0, -4, 2, 0, 0, 0, 0 } }, { { -21, 43 }, { 7, 8 }, { 1, 45 }, { -13, 25 }, { -14, 24 }, { -6, 15 }, { -32, 42 }, { -14, 19 }, { -11, 41 }, { 12, 28 }, { -7, -5 }, { -19, 29 }, { -28, -9 }, { -1, 16 }, { 9, 40 }, { 23, 43 }, { 13, 43 }, { -29, 42 }, { -6, 20 }, { 13, 49 } } },
                { 561, 1, 4, -15, -13, 2, -3, -12, 17, 0, 0, 34, 9999, 3850828433u, 1583846203u, Kind::failed, 3700035158u, 1577596882u, { { 5, -4, 2, 0, 0, 40, 0 }, { -3, -4, 2, 0, 0, 40, 0 }, { -11, -4, 2, 0, 0, 40, 0 }, { -11, -12, 2, 0, 0, 40, 0 }, { -11, -20, 2, 0, 0, 40, 0 }, { -3, -20, 2, 0, 0, 40, 0 }, { 5, -20, 2, 0, 0, 40, 0 }, { 5, -12, 2, 0, 0, 40, 0 }, { 2, 4, 2, 0, 0, 0, 0 } }, { { 5, -41 }, { -17, -14 }, { 14, -4 }, { -31, -23 }, { 0, 8 }, { -31, -3 }, { -19, 4 }, { -6, -23 }, { -11, -13 }, { -14, -2 }, { -22, -13 }, { 6, 5 }, { -35, 12 }, { -15, -43 }, { -22, -23 }, { -19, 10 }, { -42, 15 }, { -24, 0 }, { -42, -6 }, { -18, 8 } } },
                { 271, 3, 1, 28, -51, 1, 36, -41, 3, 1, 0, 13, -7, 3827529393u, 1053630074u, Kind::failed, 602117580u, 833866254u, { { -1, -32, 2, 0, 60, 77, 0 }, { -1, -32, 15, 15, 60, 77, 0 }, { -1, -32, 15, 1, 60, 77, 0 }, { 42, -77, 2, 0, 60, 77, 0 }, { 42, -77, 15, 15, 60, 77, 0 }, { 43, -57, 2, 0, 60, 77, 0 }, { 43, -57, 15, 15, 60, 77, 0 }, { 6, -70, 2, 0, 60, 77, 0 }, { 6, -70, 15, 15, 60, 77, 0 }, { 30, -66, 2, 0, 60, 77, 0 }, { 30, -66, 15, 15, 60, 77, 0 }, { 54, -46, 2, 0, 60, 77, 0 }, { 54, -46, 15, 15, 60, 77, 0 }, { 33, -67, 2, 0, 60, 77, 0 }, { 33, -67, 15, 15, 60, 77, 0 }, { 9, -54, 2, 0, 60, 77, 0 }, { 9, -54, 15, 15, 60, 77, 0 }, { 57, -23, 2, 0, 60, 77, 0 }, { 57, -23, 15, 15, 60, 77, 0 }, { 12, -37, 2, 0, 60, 77, 0 }, { 12, -37, 15, 15, 60, 77, 0 }, { 57, -57, 2, 0, 60, 77, 0 }, { 57, -57, 15, 15, 60, 77, 0 }, { 24, -45, 2, 0, 60, 77, 0 }, { 24, -45, 15, 15, 60, 77, 0 }, { 20, -25, 2, 0, 60, 77, 0 }, { 20, -25, 15, 15, 60, 77, 0 }, { 5, -49, 2, 0, 60, 77, 0 }, { 5, -49, 15, 15, 60, 77, 0 }, { 51, -63, 2, 0, 60, 77, 0 }, { 51, -63, 15, 15, 60, 77, 0 }, { 23, -53, 2, 0, 60, 77, 0 }, { 23, -53, 15, 15, 60, 77, 0 }, { 5, -58, 2, 0, 60, 77, 0 }, { 5, -58, 15, 15, 60, 77, 0 }, { 23, -44, 2, 0, 60, 77, 0 }, { 23, -44, 15, 15, 60, 77, 0 }, { 47, -31, 2, 0, 60, 77, 0 }, { 47, -31, 15, 15, 60, 77, 0 }, { 15, -67, 2, 0, 60, 77, 0 }, { 15, -67, 15, 15, 60, 77, 0 }, { 15, 5, 2, 0, 15, 0, 0 } }, { { 43, -57 }, { 42, -77 }, { -1, -32 }, { 15, -67 }, { 47, -31 }, { 23, -44 }, { 5, -58 }, { 23, -53 }, { 51, -63 }, { 5, -49 }, { 20, -25 }, { 24, -45 }, { 57, -57 }, { 12, -37 }, { 57, -23 }, { 9, -54 }, { 33, -67 }, { 54, -46 }, { 30, -66 }, { 6, -70 } } },
            };
            for (const auto& entry : merc_cases) {
                d2d::rules::MercView view{ .cls = entry.cls, .x = 5000, .y = 5000, .size = entry.size, .mode = entry.mode,
                    .owner_x = 5000 + entry.owner_x, .owner_y = 5000 + entry.owner_y, .owner_mode = entry.owner_mode,
                    .end_x = 5000 + entry.end_x, .end_y = 5000 + entry.end_y, .cursor = entry.cursor,
                    .on_bit40 = entry.bit40 != 0, .town = entry.town != 0, .target = entry.target };
                for (std::size_t k = 0; k < view.ring.size(); ++k) view.ring[k] = { 5000 + entry.ring[k].first, 5000 + entry.ring[k].second };
                d2d::rules::Rng seed;
                seed.low = entry.low; seed.high = entry.high;
                std::size_t next = 0;
                const auto act = d2d::rules::hireable_think(view, seed, [&](int x, int) { return x - 5000 >= entry.split ? 2 : 1; },
                    [&](const d2d::rules::MercMove& move) {
                        assert(next < entry.tried.size());
                        const auto& want = entry.tried[next++];
                        assert(move.x == 5000 + want.x && move.y == 5000 + want.y && move.mode == want.mode && move.type == want.type && move.pct == want.pct && move.steps == want.steps);
                        return want.found != 0;
                    });
                assert(next == entry.tried.size() && act.kind == entry.kind && seed.low == entry.after_low && seed.high == entry.after_high);
            }
        }
            // The pets' think (MonAI 67 NecroPet, FUN_005e4cf0; tools/emu/necropet.py
            // --dump: game.exe's own runs). From the pet (at 5000, 5000): size, the
            // owner, its mode, its path spot and end, the footstep cursor, its last
            // arrival, its pets, those crowding this one, town, Velocity / Run, the
            // foe (found, melee, bit 30, spot, distance), a clear line, the x where
            // level 2 starts, the seed; what it comes to, the frames, unreachable,
            // the seed after, the moves tried and the footsteps.
            {
                using Kind = d2d::rules::PetAct::Kind;
                struct Tried { int x, y, mode, foe, type, pct, steps, found; };
                struct PetCase {
                    int size, owner_x, owner_y, owner_mode, cur_x, cur_y, end_x, end_y, cursor, arrive_x, arrive_y, pets, crowd, town, velocity, run;
                    int foe, melee, ignored, foe_x, foe_y, distance, clear, split;
                    std::uint32_t low, high; Kind kind; int frames, unreachable; std::uint32_t after_low, after_high;
                    std::vector<Tried> tried; std::vector<std::pair<int, int>> ring;
                };
                const std::vector<PetCase> pet_cases{
                { 3, 101, 130, 2, 101, 132, 95, 123, 15, 61, 100, 10, 1, 0, 9, 30, 0, 0, 0, 0, 0, 0, 1, -8, 2202822778u, 1241417136u, Kind::teleport, 0, 0, 2202822778u, 1241417136u, {  }, { { 110, 150 }, { 82, 106 }, { 99, 119 }, { 80, 105 }, { 105, 151 }, { 127, 144 }, { 111, 102 }, { 109, 125 }, { 99, 141 }, { 118, 139 }, { 112, 110 }, { 110, 100 }, { 124, 133 }, { 75, 103 }, { 73, 112 }, { 127, 115 }, { 109, 101 }, { 120, 129 }, { 91, 128 }, { 108, 153 } } },
                { 3, 0, -3, 1, 3, -1, 0, -13, 13, 9, -29, 10, 3, 0, 0, 8, 1, 1, 0, 4, -1, 1, 0, 8, 430925917u, 180851701u, Kind::moved, 0, 0, 3009753990u, 179735911u, { { 0, -5, 2, 0, 0, 0, 40, 1 } }, { { -30, -20 }, { -17, 26 }, { 28, -30 }, { 0, -9 }, { 15, -8 }, { -4, -29 }, { 6, 7 }, { -18, 16 }, { 13, -16 }, { -9, -28 }, { -11, -12 }, { -30, -7 }, { 18, 26 }, { -23, -25 }, { -15, 12 }, { -24, -33 }, { -27, -4 }, { 21, -2 }, { -19, 10 }, { 5, -21 } } },
                { 2, 2, -126, 1, 0, -129, 0, -129, 3, -25, -162, 20, 1, 0, 11, 9, 0, 0, 0, 0, 0, 0, 0, 10, 1260295230u, 2233701748u, Kind::teleport, 0, 0, 1260295230u, 2233701748u, {  }, { { -24, -151 }, { -15, -119 }, { 12, -141 }, { -28, -118 }, { -5, -133 }, { 11, -127 }, { -20, -119 }, { 2, -103 }, { 8, -148 }, { 27, -132 }, { -17, -116 }, { -19, -137 }, { 30, -142 }, { 24, -117 }, { -13, -110 }, { -16, -146 }, { 19, -116 }, { 32, -121 }, { -16, -113 }, { 32, -132 } } },
                { 3, 10, 15, 4, 17, 22, 17, 22, 2, 31, -17, 40, 1, 0, 0, 0, 1, 0, 0, 0, -2, 8, 1, 9999, 3869060923u, 333587128u, Kind::chase, 0, 0, 3943344927u, 1613755786u, { { 0, 0, 2, 1, 13, 0, 12, 1 } }, { { -7, 43 }, { 38, -2 }, { 27, -14 }, { -16, 2 }, { 6, 13 }, { -5, -12 }, { -18, -4 }, { -2, 8 }, { 13, 21 }, { -12, -10 }, { 3, -7 }, { 37, 13 }, { 1, 27 }, { 26, 29 }, { 13, 22 }, { 40, -7 }, { 17, -13 }, { 39, -14 }, { 10, 43 }, { 2, 29 } } },
                { 3, -123, 65, 6, -135, 72, -135, 72, 6, -97, 46, 5, 3, 0, 5, 8, 1, 0, 0, -1, -2, 40, 0, 9999, 1651065684u, 943077948u, Kind::teleport, 0, 0, 1651065684u, 943077948u, {  }, { { -148, 75 }, { -146, 51 }, { -97, 61 }, { -107, 56 }, { -129, 94 }, { -106, 79 }, { -116, 64 }, { -125, 64 }, { -100, 69 }, { -148, 68 }, { -105, 67 }, { -152, 54 }, { -115, 40 }, { -123, 36 }, { -139, 79 }, { -146, 66 }, { -104, 74 }, { -111, 93 }, { -122, 51 }, { -96, 35 } } },
                { 1, -6, 5, 1, -17, 15, -17, 15, 8, -16, -17, 1, 0, 0, 11, 10, 1, 1, 0, 3, -16, 12, 1, 9999, 2028081411u, 2297156900u, Kind::swing, 0, 0, 942961344u, 215358412u, {  }, { { -19, 27 }, { -26, -18 }, { -8, 5 }, { -19, 34 }, { -23, 28 }, { -10, -1 }, { 4, 8 }, { -5, 18 }, { -16, 20 }, { 17, 28 }, { 3, 3 }, { -16, -21 }, { 17, -23 }, { -19, 30 }, { 2, -23 }, { 7, 20 }, { -19, 11 }, { -14, -6 }, { 5, 25 }, { 0, -24 } } },
                { 2, -3, 2, 1, 0, 0, 1, 6, 9, 25, 3, 5, 0, 0, 0, 0, 1, 0, 1, 3, 1, 17, 0, 9999, 3435371986u, 150771832u, Kind::moved, 0, 0, 488148655u, 1783967588u, { { 2, -8, 2, 0, 7, 0, 0, 1 } }, { { -8, 26 }, { 17, 16 }, { 24, -14 }, { -28, -2 }, { 26, 29 }, { 13, -4 }, { -25, 0 }, { -4, -16 }, { 7, 28 }, { 25, -28 }, { -9, 7 }, { 3, 13 }, { 23, 4 }, { 17, 24 }, { 27, -7 }, { -4, -8 }, { 8, -15 }, { -27, 18 }, { 22, 24 }, { 18, 13 } } },
                { 1, 5, -6, 6, 7, -6, 16, -2, 0, 42, 2, 1, 0, 0, 8, 10, 1, 1, 0, 32, -25, 31, 0, 9999, 3954732770u, 4109704211u, Kind::moved, 0, 0, 1863159805u, 1649488861u, { { 24, 6, 2, 0, 0, 0, 40, 1 } }, { { 35, 20 }, { 6, 1 }, { 35, 8 }, { -20, 12 }, { -11, -8 }, { 8, -1 }, { -7, 17 }, { 21, -1 }, { 15, -26 }, { 8, -4 }, { 28, 22 }, { 10, -20 }, { -6, 6 }, { -1, 18 }, { 30, 21 }, { 14, -23 }, { -6, 18 }, { -16, -2 }, { 8, -19 }, { 11, -5 } } },
                { 2, 10, 11, 1, 10, 11, 10, 11, 12, 22, 38, 20, 0, 0, 13, 15, 1, 0, 0, -13, 27, 13, 0, -8, 3181589678u, 2696741227u, Kind::stand, 15, 0, 597741726u, 276691931u, {  }, { { 32, 40 }, { 37, 39 }, { 10, 14 }, { 0, -13 }, { -8, 7 }, { 19, -18 }, { 39, -3 }, { -12, 25 }, { 29, -18 }, { -18, -7 }, { -11, -5 }, { -20, 24 }, { -2, 1 }, { 26, 3 }, { -5, 20 }, { 11, -13 }, { 11, 27 }, { 17, -12 }, { 34, 13 }, { 19, -3 } } },
                { 1, 32, -5, 1, 30, -8, 26, -11, 1, 62, 33, 20, 3, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 9999, 3980972189u, 2890246053u, Kind::failed, 0, 0, 1636273095u, 614735410u, { { 26, -19, 2, 0, 0, -100, 40, 0 }, { 13, -10, 2, 0, 0, 0, 0, 0 }, { 34, -19, 2, 0, 0, -100, 40, 0 }, { 17, -10, 2, 0, 0, 0, 0, 0 }, { 34, -11, 2, 0, 0, -100, 40, 0 }, { 17, -6, 2, 0, 0, 0, 0, 0 }, { 34, -3, 2, 0, 0, -100, 40, 0 }, { 17, -2, 2, 0, 0, 0, 0, 0 }, { 26, -3, 2, 0, 0, -100, 40, 0 }, { 13, -2, 2, 0, 0, 0, 0, 0 }, { 18, -3, 2, 0, 0, -100, 40, 0 }, { 9, -2, 2, 0, 0, 0, 0, 0 }, { 18, -11, 2, 0, 0, -100, 40, 0 }, { 9, -6, 2, 0, 0, 0, 0, 0 }, { 18, -19, 2, 0, 0, -100, 40, 0 }, { 9, -10, 2, 0, 0, 0, 0, 0 }, { 0, -4, 2, 0, 0, 0, 0, 0 }, { 16, -3, 2, 0, 0, 0, 0, 0 }, { 32, -5, 2, 0, 0, 0, 0, 0 } }, { { 10, 21 }, { 17, -27 }, { 48, -30 }, { 18, -11 }, { 8, -8 }, { 55, 25 }, { 28, -1 }, { 52, 10 }, { 10, -23 }, { 27, 5 }, { 45, 16 }, { 3, -29 }, { 14, 1 }, { 45, -13 }, { 60, 17 }, { 25, -28 }, { 47, -3 }, { 42, 13 }, { 23, -3 }, { 45, 18 } } },
                { 3, 4, -8, 2, 2, -8, 2, -8, 8, -1, -34, 40, 0, 0, 8, 6, 1, 0, 0, 28, 7, 27, 1, 9999, 1467670483u, 1295746146u, Kind::moved, 0, 1, 3615766256u, 1183090370u, { { 0, 0, 2, 1, 13, 0, 12, 0 }, { 1, 4, 2, 0, 0, 0, 0, 1 } }, { { 6, -16 }, { 1, -25 }, { 12, -15 }, { -26, 2 }, { 18, -36 }, { 28, -26 }, { -15, -12 }, { 3, -15 }, { 21, -15 }, { -1, 20 }, { -14, 0 }, { -16, -32 }, { 6, 12 }, { -26, -18 }, { 31, -33 }, { 25, 15 }, { 19, 18 }, { 29, 20 }, { 14, 22 }, { -1, -2 } } },
                { 3, 36, 34, 3, 36, 34, 36, 34, 16, 68, 6, 40, 3, 0, 0, 10, 1, 1, 0, -35, 9, 10, 0, 1, 2562215347u, 251450251u, Kind::moved, 0, 0, 2562215347u, 251450251u, { { 28, 42, 2, 0, 0, 100, 40, 1 } }, { { 48, 23 }, { 27, 17 }, { 48, 29 }, { 42, 15 }, { 40, 8 }, { 30, 36 }, { 37, 45 }, { 19, 48 }, { 13, 52 }, { 31, 40 }, { 7, 11 }, { 45, 10 }, { 53, 19 }, { 22, 32 }, { 31, 36 }, { 9, 52 }, { 18, 45 }, { 30, 4 }, { 12, 20 }, { 22, 21 } } },
                { 1, 3, -5, 4, 2, -1, 2, -1, 14, -19, -14, 20, 0, 1, 8, 9, 0, 0, 0, 0, 0, 0, 0, 9999, 664535100u, 533975705u, Kind::stand, 15, 0, 1634320581u, 277172519u, {  }, { { 14, -18 }, { 8, 24 }, { -9, 7 }, { 18, 12 }, { -9, -34 }, { -27, 14 }, { -12, 2 }, { -25, 5 }, { -17, -9 }, { 21, 8 }, { -3, -32 }, { 30, -15 }, { 20, -10 }, { -24, 2 }, { 19, 25 }, { -7, -31 }, { 24, 22 }, { -13, -8 }, { 19, -5 }, { -11, 13 } } },
                { 1, -2, 16, 2, -2, 15, -2, 16, 8, 12, -21, 20, 0, 1, 9, 8, 0, 0, 0, 0, 0, 0, 1, -18, 2364931898u, 1008261136u, Kind::moved, 0, 0, 2144526258u, 986395048u, { { -10, 24, 2, 0, 0, 0, 40, 1 } }, { { 17, 33 }, { 1, 38 }, { -24, 46 }, { -14, 32 }, { -24, 1 }, { -2, -7 }, { 0, 43 }, { -13, 36 }, { 0, 25 }, { 22, 8 }, { -15, 3 }, { 7, 29 }, { 14, 22 }, { 12, 23 }, { -20, 26 }, { -15, 34 }, { -17, -2 }, { 28, 1 }, { 0, 27 }, { 26, -2 } } },
                { 3, -2, 3, 2, -5, 5, -5, 5, 0, 12, -29, 20, 1, 0, 0, 8, 0, 0, 0, 0, 0, 0, 1, -17, 363783341u, 111496651u, Kind::moved, 0, 0, 1293614286u, 232015216u, { { -13, 5, 2, 0, 0, 0, 40, 0 }, { -7, 2, 2, 0, 0, 0, 0, 0 }, { -13, -3, 2, 0, 0, 0, 40, 0 }, { -7, -2, 2, 0, 0, 0, 0, 0 }, { -5, -3, 2, 0, 0, 0, 40, 0 }, { -3, -2, 2, 0, 0, 0, 0, 0 }, { 3, -3, 2, 0, 0, 0, 40, 0 }, { 1, -2, 2, 0, 0, 0, 0, 0 }, { 3, 5, 2, 0, 0, 0, 40, 0 }, { 1, 2, 2, 0, 0, 0, 0, 0 }, { 3, 13, 2, 0, 0, 0, 40, 0 }, { 1, 6, 2, 0, 0, 0, 0, 0 }, { -5, 13, 2, 0, 0, 0, 40, 0 }, { -3, 6, 2, 0, 0, 0, 0, 0 }, { -13, 13, 2, 0, 0, 0, 40, 0 }, { -7, 6, 2, 0, 0, 0, 0, 0 }, { -4, -1, 2, 0, 0, 0, 0, 0 }, { -1, 1, 2, 0, 0, 0, 0, 0 }, { -2, 3, 2, 0, 0, 0, 0, 0 }, { -4, 2, 2, 0, 0, 0, 0, 1 } }, { { -7, 32 }, { 14, 30 }, { 1, -12 }, { -27, -4 }, { 21, -4 }, { -31, -4 }, { 11, -2 }, { 5, -3 }, { 18, -15 }, { 15, 9 }, { -9, 24 }, { -8, 7 }, { -23, 26 }, { 5, 9 }, { -21, -16 }, { -27, 21 }, { -3, 29 }, { 22, -9 }, { 19, -26 }, { -18, 6 } } },
                { 1, -29, 31, 1, -28, 30, -29, 31, 5, -21, 12, 5, 0, 0, 9, 8, 0, 0, 0, 0, 0, 0, 0, 9999, 3138633804u, 493116845u, Kind::moved, 0, 0, 3138633804u, 493116845u, { { -37, 39, 2, 0, 0, -12, 40, 0 }, { -19, 19, 2, 0, 0, 0, 0, 1 } }, { { -11, 30 }, { -34, 23 }, { -38, 52 }, { -38, 44 }, { -51, 32 }, { -28, 35 }, { -8, 47 }, { -55, 46 }, { -20, 59 }, { -57, 27 }, { -11, 53 }, { -17, 23 }, { -5, 1 }, { -8, 25 }, { -6, 6 }, { -3, 30 }, { -25, 41 }, { -58, 34 }, { -36, 46 }, { -59, 52 } } },
                { 2, 3, 4, 2, 1, 1, 3, 4, 9, 14, -27, 20, 1, 0, 11, 15, 0, 0, 0, 0, 0, 0, 1, 9999, 2867582038u, 1693232064u, Kind::moved, 0, 0, 1784377838u, 1196046586u, { { -5, 12, 2, 0, 0, 0, 40, 0 }, { -3, 6, 2, 0, 0, 0, 0, 1 } }, { { -14, 26 }, { 31, 28 }, { -18, 26 }, { -19, -21 }, { -5, -9 }, { 12, 19 }, { 6, -20 }, { 29, 4 }, { 17, 3 }, { -22, 21 }, { 10, 8 }, { 27, 6 }, { -8, -25 }, { 23, -14 }, { -1, -14 }, { 31, 17 }, { -22, 24 }, { 25, 2 }, { -14, -24 }, { 7, 0 } } },
                { 2, 10, -4, 6, 20, -4, 20, -4, 16, 35, -26, 20, 0, 0, 9, 15, 1, 0, 0, -2, 4, 1, 1, -1, 2475991270u, 1793131370u, Kind::moved, 0, 1, 2730333427u, 1543519334u, { { 0, 0, 2, 1, 13, 0, 12, 0 }, { 4, -2, 2, 0, 0, 0, 0, 1 } }, { { 38, -8 }, { 19, -34 }, { 26, -9 }, { 19, 10 }, { 0, 16 }, { 30, -19 }, { 22, 4 }, { 13, -33 }, { -4, -13 }, { -8, -13 }, { -1, 7 }, { 8, -28 }, { 15, -10 }, { -7, -2 }, { 1, 16 }, { -2, 18 }, { -14, 5 }, { 25, -24 }, { -4, 15 }, { 14, 23 } } },
                { 2, -15, 6, 4, -13, 8, -26, 14, 19, 0, 14, 20, 1, 0, 6, 10, 1, 1, 0, -1, -2, 28, 0, 9999, 452574072u, 2260095739u, Kind::moved, 0, 0, 1898387027u, 188765192u, { { -34, 14, 2, 0, 0, 0, 40, 1 } }, { { 5, -15 }, { -20, 7 }, { -7, -18 }, { -15, -2 }, { -19, 33 }, { -6, 34 }, { -1, 12 }, { 3, 2 }, { 3, -6 }, { -14, 32 }, { -16, 29 }, { 12, 30 }, { -1, 2 }, { 7, 3 }, { -38, 12 }, { -38, 33 }, { 15, 9 }, { -24, 9 }, { -12, 3 }, { -19, 33 } } },
                { 1, 12, -5, 1, 14, -6, 2, 6, 9, -25, 13, 5, 1, 0, 5, 30, 1, 1, 0, 20, -14, 12, 0, 2, 3598827253u, 3690137481u, Kind::moved, 0, 0, 1856223892u, 88858914u, { { 32, -27, 2, 0, 0, 52, 77, 1 } }, { { 37, -1 }, { 8, 1 }, { 33, -25 }, { -4, 6 }, { 29, -9 }, { 18, -28 }, { 39, -31 }, { -12, -2 }, { 32, -27 }, { -11, 21 }, { 16, 1 }, { 42, -6 }, { -18, -17 }, { -3, 11 }, { 1, -34 }, { 2, -24 }, { -14, -7 }, { 39, -7 }, { 34, -2 }, { 7, -26 } } },
                { 3, 10, 7, 1, 8, 4, 10, 7, 9, -7, -5, 5, 0, 0, 0, 9, 1, 0, 1, 16, 27, 4, 1, 9999, 1077621290u, 1818693869u, Kind::chase, 0, 0, 1453869887u, 449467617u, { { 0, 0, 2, 1, 13, 0, 12, 1 } }, { { 21, -17 }, { -1, 23 }, { 32, -3 }, { 37, -6 }, { 35, 3 }, { 25, 33 }, { -12, -2 }, { 24, 37 }, { 29, 5 }, { 6, -18 }, { -10, -9 }, { 35, -4 }, { -13, 22 }, { 24, -11 }, { -3, -12 }, { 26, -8 }, { 12, 19 }, { -2, 27 }, { -3, 0 }, { 31, 19 } } },
                { 3, 14, 4, 1, 3, 14, 3, 14, 9, 24, 41, 2, 3, 1, 5, 10, 1, 0, 0, -36, -14, 19, 1, 9999, 2354268708u, 1118372199u, Kind::moved, 0, 0, 124150848u, 617906300u, { { -10, -3, 2, 0, 0, 48, 77, 0 }, { -10, -3, 2, 0, 15, 48, 77, 0 }, { -10, -3, 2, 0, 1, 48, 77, 1 } }, { { 44, -9 }, { 23, -25 }, { 17, -15 }, { 31, -10 }, { 16, -1 }, { 20, 33 }, { 25, 19 }, { -3, -9 }, { -10, -3 }, { -13, -17 }, { 30, 33 }, { 33, -3 }, { -8, 23 }, { -1, 20 }, { 37, 32 }, { -9, -25 }, { 4, 27 }, { 8, 2 }, { -11, 28 }, { 40, -2 } } },
                { 1, -4, -2, 3, -2, -2, -2, -2, 15, -37, 20, 10, 1, 0, 11, 8, 0, 0, 0, 0, 0, 0, 0, 9999, 3299798234u, 716048591u, Kind::moved, 0, 0, 1595993233u, 1376320663u, { { 6, 6, 2, 0, 0, 0, 40, 1 } }, { { -1, 23 }, { -12, -18 }, { -28, -14 }, { -11, -10 }, { 5, -19 }, { -26, -30 }, { 20, -31 }, { 13, 8 }, { -4, 4 }, { -34, 19 }, { 14, 15 }, { -15, 10 }, { 8, -28 }, { 24, 22 }, { -21, 3 }, { 10, -10 }, { 8, -25 }, { 15, 23 }, { -11, -17 }, { 16, 8 } } },
                { 1, -23, 24, 3, -11, 33, -11, 33, 19, -26, 13, 40, 0, 0, 8, 10, 1, 0, 0, -3, -1, 17, 1, 9999, 4246894278u, 595978277u, Kind::moved, 0, 0, 4246894278u, 595978277u, { { -11, 41, 2, 0, 0, 25, 40, 0 }, { -6, 20, 2, 0, 0, 0, 0, 1 } }, { { -43, 33 }, { 2, 20 }, { 2, 24 }, { -50, 16 }, { -37, 19 }, { -21, 41 }, { -3, 38 }, { -28, 10 }, { -8, 34 }, { -37, 17 }, { -8, 37 }, { -33, 5 }, { -32, 48 }, { -5, 7 }, { 0, 30 }, { 0, 2 }, { -17, 20 }, { -12, 39 }, { -45, 33 }, { -6, 47 } } },
                { 1, 0, 5, 6, 3, 5, 0, 5, 14, -39, -9, 1, 1, 1, 9, 8, 1, 0, 0, 5, 2, 19, 1, -19, 433096235u, 4020679959u, Kind::failed, 0, 0, 3847817758u, 1711706110u, { { -8, 13, 2, 0, 0, 0, 40, 0 }, { -4, 6, 2, 0, 0, 0, 0, 0 }, { -8, 5, 2, 0, 0, 0, 40, 0 }, { -4, 2, 2, 0, 0, 0, 0, 0 }, { -8, -3, 2, 0, 0, 0, 40, 0 }, { -4, -2, 2, 0, 0, 0, 0, 0 }, { 0, -3, 2, 0, 0, 0, 40, 0 }, { 0, -2, 2, 0, 0, 0, 0, 0 }, { 8, -3, 2, 0, 0, 0, 40, 0 }, { 4, -2, 2, 0, 0, 0, 0, 0 }, { 8, 5, 2, 0, 0, 0, 40, 0 }, { 4, 2, 2, 0, 0, 0, 0, 0 }, { 8, 13, 2, 0, 0, 0, 40, 0 }, { 4, 6, 2, 0, 0, 0, 0, 0 }, { 0, 13, 2, 0, 0, 0, 40, 0 }, { 0, 6, 2, 0, 0, 0, 0, 0 }, { 4, 1, 2, 0, 0, 0, 0, 0 }, { 0, 2, 2, 0, 0, 0, 0, 0 }, { 0, 5, 2, 0, 0, 0, 0, 0 }, { -1, 4, 2, 0, 0, 0, 0, 0 } }, { { 0, -23 }, { -4, -3 }, { 21, 10 }, { -5, 10 }, { -27, -12 }, { 5, 14 }, { 12, 27 }, { 9, -9 }, { -13, -13 }, { 30, -7 }, { 25, -10 }, { 21, 3 }, { 6, 28 }, { 30, 21 }, { -27, -14 }, { 24, -24 }, { 30, -15 }, { 4, -18 }, { 13, -21 }, { -13, -3 } } },
                { 1, -28, 55, 3, -30, 54, -40, 43, 16, -62, 66, 1, 1, 0, 13, 30, 0, 0, 0, 0, 0, 0, 0, 9999, 3238978850u, 2530056063u, Kind::teleport, 0, 0, 3238978850u, 2530056063u, {  }, { { -34, 71 }, { -35, 57 }, { -16, 42 }, { -7, 48 }, { -18, 65 }, { -30, 47 }, { -39, 42 }, { -54, 82 }, { -17, 45 }, { -20, 31 }, { -42, 46 }, { -45, 72 }, { -47, 38 }, { -55, 72 }, { -50, 59 }, { -39, 68 }, { -45, 36 }, { -36, 31 }, { -49, 34 }, { -35, 47 } } },
                { 1, 14, 20, 1, 14, 20, 25, 14, 8, 28, 27, 20, 3, 0, 5, 10, 0, 0, 0, 0, 0, 0, 1, 9999, 1794707677u, 1009574681u, Kind::moved, 0, 0, 1588165487u, 365659305u, { { -5, 39, 2, 0, 0, 47, 77, 1 } }, { { -16, 42 }, { 5, 34 }, { 4, 44 }, { 24, 50 }, { 1, 32 }, { 22, 3 }, { 28, 3 }, { -5, 39 }, { -8, 0 }, { 28, 27 }, { 13, -8 }, { -8, 7 }, { -9, 6 }, { 44, 12 }, { 23, 29 }, { 6, 9 }, { -4, -10 }, { -12, 34 }, { -3, 37 }, { 29, 13 } } },
                { 3, -23, 25, 1, -25, 25, -33, 34, 14, -37, -4, 5, 0, 0, 11, 10, 1, 1, 1, 4, -38, 40, 1, 9999, 2060026139u, 2881359599u, Kind::moved, 0, 0, 2060026139u, 2881359599u, { { -41, 34, 2, 0, 0, -10, 40, 0 }, { -21, 17, 2, 0, 0, 0, 0, 1 } }, { { -29, 7 }, { -6, 21 }, { -15, 9 }, { -25, 35 }, { -49, 18 }, { -27, 16 }, { -35, 32 }, { -25, 37 }, { -36, 13 }, { -11, 39 }, { -53, 38 }, { -14, 10 }, { -53, 21 }, { -30, 3 }, { -16, 52 }, { 2, 11 }, { -29, 21 }, { -36, 16 }, { -47, 47 }, { 3, 49 } } },
                { 1, 6, 13, 3, 6, 13, 6, 13, 4, 14, 30, 40, 3, 0, 8, 8, 1, 0, 0, -4, -4, 22, 1, 9999, 1331684344u, 2967539754u, Kind::chase, 0, 0, 3082223106u, 555435378u, { { 0, 0, 2, 1, 13, 0, 12, 1 } }, { { -10, 36 }, { -8, 4 }, { -22, 3 }, { -15, 35 }, { -15, 33 }, { -6, 24 }, { -6, -14 }, { 24, 14 }, { -21, 8 }, { 15, -12 }, { 11, -9 }, { 35, 4 }, { 16, 28 }, { -23, 22 }, { 35, 26 }, { 24, -4 }, { 13, 17 }, { -24, 35 }, { 23, 13 }, { 2, 4 } } },
                { 3, -14, 6, 3, -22, -3, -22, -3, 11, 20, -16, 10, 3, 0, 11, 9, 0, 0, 0, 0, 0, 0, 1, 9999, 2469514168u, 193798617u, Kind::moved, 0, 0, 331769457u, 1030015515u, { { -22, -11, 2, 0, 0, 0, 40, 0 }, { -11, -6, 2, 0, 0, 0, 0, 0 }, { -14, -11, 2, 0, 0, 0, 40, 0 }, { -7, -6, 2, 0, 0, 0, 0, 0 }, { -14, -3, 2, 0, 0, 0, 40, 0 }, { -7, -2, 2, 0, 0, 0, 0, 0 }, { -14, 5, 2, 0, 0, 0, 40, 0 }, { -7, 2, 2, 0, 0, 0, 0, 0 }, { -22, 5, 2, 0, 0, 0, 40, 1 } }, { { -27, 6 }, { -18, -22 }, { 13, 3 }, { 15, 0 }, { -32, 19 }, { 0, 35 }, { -13, 7 }, { -17, 20 }, { 7, 0 }, { -12, 25 }, { -14, -18 }, { -9, 23 }, { 10, -14 }, { 5, 13 }, { -24, -12 }, { -44, -2 }, { 8, -14 }, { -3, -2 }, { -20, 13 }, { 7, 9 } } },
                { 2, -29, 31, 1, -32, 32, -29, 31, 5, -45, 65, 2, 1, 0, 6, 0, 1, 1, 0, -3, 37, 16, 1, 14, 2868049142u, 1258584083u, Kind::moved, 0, 0, 2868049142u, 1258584083u, { { -37, 39, 2, 0, 0, -100, 40, 0 }, { -19, 19, 2, 0, 0, 0, 0, 0 }, { -37, 31, 2, 0, 0, -100, 40, 1 } }, { { -48, 27 }, { -1, 34 }, { -30, 25 }, { -28, 8 }, { -45, 12 }, { -11, 40 }, { -53, 44 }, { -40, 33 }, { -5, 10 }, { -57, 45 }, { -37, 37 }, { -49, 36 }, { -28, 22 }, { -28, 10 }, { -31, 61 }, { -11, 23 }, { -8, 20 }, { -53, 16 }, { -9, 10 }, { -46, 13 } } },
                { 3, -2, 5, 6, -2, 5, -2, 5, 8, 30, 9, 2, 0, 0, 0, 10, 1, 0, 0, 5, -4, 19, 1, 9999, 4246962295u, 1578061787u, Kind::chase, 0, 0, 3299744622u, 1771375565u, { { 0, 0, 2, 1, 13, 0, 12, 1 } }, { { 18, -8 }, { 28, 7 }, { -23, 30 }, { -8, -15 }, { 20, 30 }, { 2, 29 }, { -24, 14 }, { -27, 9 }, { 19, 21 }, { -22, 9 }, { 16, 17 }, { 12, 33 }, { 5, 24 }, { 8, -9 }, { -13, -22 }, { -5, 34 }, { 17, -2 }, { 9, 25 }, { 2, -11 }, { -2, 3 } } },
                { 2, -1, 3, 1, -13, 4, -13, 4, 6, -22, -10, 5, 3, 0, 13, 8, 0, 0, 0, 0, 0, 0, 1, 9, 2557733434u, 2245223802u, Kind::moved, 0, 0, 2384741006u, 502379406u, { { -6, 12, 2, 0, 7, 0, 0, 0 }, { 9, -9, 2, 0, 0, 0, 9, 0 }, { -13, 4, 2, 0, 0, 0, 40, 0 }, { -3, 4, 2, 0, 0, 0, 0, 1 } }, { { 22, -16 }, { 13, 11 }, { 2, 27 }, { 6, 22 }, { 17, 15 }, { 20, -22 }, { -31, -4 }, { -27, 23 }, { -18, -2 }, { 22, -13 }, { -5, -5 }, { -10, -13 }, { -9, -2 }, { -31, 30 }, { 18, -12 }, { -15, -24 }, { 6, -4 }, { 2, 19 }, { -10, 6 }, { 12, -5 } } },
                { 1, -1, -1, 3, 0, 0, -13, -4, 7, 19, -31, 1, 3, 0, 11, 9, 1, 0, 0, 15, 12, 16, 1, 20, 781144945u, 3290252109u, Kind::stand, 10, 1, 971617670u, 970870086u, { { 0, 0, 2, 1, 13, 0, 12, 0 } }, { { -12, -2 }, { 18, -28 }, { -10, -25 }, { -14, -18 }, { 2, 0 }, { -27, -29 }, { -25, 24 }, { 14, 3 }, { -28, -1 }, { 17, 16 }, { 2, -16 }, { -13, -19 }, { 28, 6 }, { -8, -16 }, { -4, -11 }, { -6, -27 }, { -10, -28 }, { -9, -13 }, { 2, 24 }, { 29, 23 } } },
                { 3, -5, 7, 6, -6, 6, 6, 13, 4, -14, -26, 20, 3, 0, 8, 0, 1, 1, 0, 1, 2, 6, 1, 9999, 3752764873u, 1355479334u, Kind::swing, 0, 0, 720383639u, 244251088u, {  }, { { 14, 28 }, { 6, -21 }, { 10, -20 }, { -6, -20 }, { -8, -17 }, { 25, 7 }, { -23, -7 }, { 21, 34 }, { -32, 12 }, { -31, -23 }, { -18, 6 }, { 9, 16 }, { -7, 32 }, { -27, 4 }, { -4, 13 }, { 15, 24 }, { -20, 2 }, { 17, 18 }, { -20, 20 }, { 20, 16 } } },
                { 3, -29, -16, 1, -27, -16, -18, -12, 9, -40, 21, 20, 3, 0, 11, 8, 0, 0, 0, 0, 0, 0, 1, 5, 4208886955u, 3122724643u, Kind::moved, 0, 0, 4208886955u, 3122724643u, { { -10, -4, 2, 0, 0, -28, 40, 1 } }, { { -4, 10 }, { -47, -30 }, { -57, 4 }, { -54, -1 }, { -42, -18 }, { -46, -23 }, { -17, -19 }, { -46, -18 }, { -32, -26 }, { -46, -7 }, { -58, -20 }, { -4, -27 }, { -20, 3 }, { -54, -17 }, { -12, -16 }, { -19, 10 }, { -24, -17 }, { -17, -22 }, { -16, 11 }, { -27, 5 } } },
                { 3, 0, 0, 2, -2, 3, -4, -10, 16, 23, 1, 2, 3, 0, 6, 30, 1, 1, 0, -6, -7, 38, 1, 9999, 96844170u, 4095145211u, Kind::swing, 0, 0, 2970523558u, 464260102u, {  }, { { -5, -11 }, { -18, 17 }, { 28, -3 }, { -27, 1 }, { 7, -27 }, { 22, -17 }, { 23, 11 }, { -5, -26 }, { 7, 15 }, { 15, 18 }, { -9, 17 }, { -7, 7 }, { 0, -14 }, { -14, 15 }, { 13, 5 }, { -4, 4 }, { -20, -29 }, { -22, 18 }, { 19, 7 }, { -10, 21 } } },
                { 2, -1, -1, 1, 2, -3, 9, -8, 19, 5, 0, 5, 0, 0, 11, 8, 0, 0, 0, 0, 0, 0, 0, -19, 1208699392u, 2616866758u, Kind::failed, 0, 0, 624406934u, 1751400829u, { { 8, 4, 2, 0, 7, 0, 0, 0 }, { 9, 9, 2, 0, 0, 0, 9, 0 }, { 9, -8, 2, 0, 0, 0, 40, 0 }, { 4, 0, 2, 0, 0, 0, 0, 0 } }, { { -7, 14 }, { -14, -22 }, { -9, 6 }, { 3, 1 }, { -30, 3 }, { -8, -6 }, { 11, -6 }, { -22, 19 }, { -12, 9 }, { 24, 22 }, { 28, 8 }, { -16, -18 }, { -20, -30 }, { -24, 27 }, { -26, 9 }, { 19, -30 }, { 16, -19 }, { -15, -30 }, { 0, -28 }, { 13, 6 } } },
                { 2, 18, 19, 6, 18, 17, 26, 17, 17, 27, -13, 1, 3, 1, 11, 10, 0, 0, 0, 0, 0, 0, 1, -18, 2987144626u, 2914795165u, Kind::moved, 0, 0, 2427387024u, 1420568890u, { { 41, 32, 2, 0, 0, 64, 77, 1 } }, { { 40, 0 }, { 46, 26 }, { 31, 37 }, { -2, 19 }, { 10, 27 }, { 33, 11 }, { 1, 38 }, { 44, 9 }, { 43, 43 }, { 2, 19 }, { 11, -8 }, { 2, 20 }, { 33, 31 }, { -9, 43 }, { 32, 36 }, { -8, 38 }, { 41, 32 }, { 22, 49 }, { 25, -10 }, { -6, 9 } } },
                { 2, -16, 5, 3, -16, 5, -16, 5, 16, 17, 33, 2, 1, 1, 6, 9, 0, 0, 0, 0, 0, 0, 1, 3, 1226321510u, 2241238311u, Kind::moved, 0, 0, 2419396921u, 883006583u, { { 5, 17, 2, 0, 0, 41, 77, 0 }, { 5, 17, 2, 0, 15, 41, 77, 0 }, { 5, 17, 2, 0, 1, 41, 77, 1 } }, { { -46, -10 }, { 13, -7 }, { -27, -9 }, { -46, -24 }, { -23, -6 }, { -20, 12 }, { -35, -8 }, { -35, -18 }, { -36, 29 }, { -40, 5 }, { -3, 7 }, { -34, -6 }, { -12, 31 }, { 10, 23 }, { 9, 14 }, { 5, 17 }, { -18, 19 }, { -1, 7 }, { -31, 1 }, { 6, -1 } } },
                };
                for (const auto& entry : pet_cases) {
                    d2d::rules::PetView view{ .x = 5000, .y = 5000, .size = entry.size, .owner_x = 5000 + entry.owner_x, .owner_y = 5000 + entry.owner_y,
                        .owner_mode = entry.owner_mode, .cur_x = 5000 + entry.cur_x, .cur_y = 5000 + entry.cur_y, .end_x = 5000 + entry.end_x, .end_y = 5000 + entry.end_y,
                        .cursor = entry.cursor, .arrive_x = 5000 + entry.arrive_x, .arrive_y = 5000 + entry.arrive_y, .pets = entry.pets, .crowd = entry.crowd,
                        .town = entry.town != 0, .velocity = entry.velocity, .run = entry.run, .foe = entry.foe != 0, .foe_melee = entry.melee != 0,
                        .foe_ignored = entry.ignored != 0, .clear = entry.clear != 0, .foe_x = 5000 + entry.foe_x, .foe_y = 5000 + entry.foe_y, .foe_distance = entry.distance };
                    for (std::size_t k = 0; k < view.ring.size(); ++k) view.ring[k] = { 5000 + entry.ring[k].first, 5000 + entry.ring[k].second };
                    d2d::rules::Rng seed;
                    seed.low = entry.low; seed.high = entry.high;
                    std::size_t next = 0;
                    const auto act = d2d::rules::necropet_think(view, seed, [&](int x, int) { return x - 5000 >= entry.split ? 2 : 1; },
                        [&](const d2d::rules::MercMove& move) {
                            assert(next < entry.tried.size());
                            const auto& want = entry.tried[next++];
                            assert(move.mode == want.mode && (move.unit != 0) == (want.foe != 0) && move.type == want.type && move.pct == want.pct && move.steps == want.steps);
                            assert(move.unit != 0 || (move.x == 5000 + want.x && move.y == 5000 + want.y));
                            return want.found != 0;
                        });
                    assert(next == entry.tried.size() && act.kind == entry.kind && act.frames == entry.frames && act.unreachable == (entry.unreachable != 0));
                    assert(seed.low == entry.after_low && seed.high == entry.after_high);
                }
            }
            // The other pets' thinks (rules::*_think on PetBrain; tools/emu/pet_ais.py
            // --dump: game.exe's own runs, the follow / decide answers replayed):
            // the scene, the calls each makes and what it comes to.
            {
                static constexpr std::string_view kCases =
#include "pet_ais_cases.inc"
                    ;
                using d2d::rules::PetAct;
                using d2d::rules::PetUnit;
                struct Replay {
                    std::vector<std::string> calls;
                    std::size_t next = 0;
                    std::vector<int> call(char kind) {
                        assert(next < calls.size());
                        std::istringstream fields(calls[next++]);
                        char got = 0;
                        fields >> got;
                        assert(got == kind);
                        std::vector<int> values;
                        for (int value = 0; fields >> value;) values.push_back(value);
                        return values;
                    }
                    bool try_move(const d2d::rules::MercMove& move) {
                        const auto want = call('m');
                        assert(move.mode == want[2] && move.unit == want[3] && move.type == want[4] && move.pct == want[5] && move.steps == want[6]);
                        assert(move.unit != 0 || move.mode == 0 || ((move.x & 0xffff) == want[0] && (move.y & 0xffff) == want[1]));
                        return want[7] != 0;
                    }
                    bool follow(int mode, bool run, int pct, int reach) {
                        const auto want = call('f');
                        assert(mode == want[0] && int(run) == want[1] && pct == want[2] && reach == want[3]);
                        return want[4] != 0;
                    }
                    bool decide(PetUnit foe, bool melee, bool stay, int reach) {
                        const auto want = call('d');
                        assert(int(foe) == want[0] && int(melee) == want[1] && int(stay) == want[2] && reach == want[3]);
                        return want[4] != 0;
                    }
                    bool teleport() { return call('t')[0] != 0; }
                    [[nodiscard]] PetAct followed() const { return { .kind = PetAct::Kind::stand, .frames = -7 }; }
                };
                std::istringstream lines{ std::string(kCases) };
                int checked = 0;
                for (std::string line; std::getline(lines, line);) {
                    if (line.empty()) continue;
                    const auto bar = line.find('|'), bar2 = line.rfind('|');
                    std::istringstream fields(line.substr(0, bar));
                    auto read = [&] { int value = 0; fields >> value; return value; };
                    d2d::rules::PetScene scene;
                    const int which = read();
                    scene.cls = read(); scene.owner = read() != 0; scene.owner_id = read(); scene.owner_mode = read(); scene.town = read() != 0; scene.frame = read();
                    for (std::size_t unit = 1; unit < 6; ++unit) { scene.spot[unit].first = read(); scene.spot[unit].second = read(); }
                    for (std::size_t from = 1; from < 6; ++from) for (std::size_t to = 1; to < 6; ++to) scene.gap[from][to] = read();
                    for (std::size_t from = 1; from < 6; ++from) for (std::size_t to = 1; to < 6; ++to) scene.distance[from][to] = read();
                    scene.foe = read() != 0; scene.foe_melee = read() != 0; scene.foe2 = read() != 0; scene.foe2_melee = read() != 0; scene.nearby = read() != 0;
                    scene.foe_distance = read(); scene.foe2_distance = read();
                    scene.driver = read() != 0; scene.driver_melee = read() != 0; scene.driver_distance = read();
                    for (std::size_t unit = 1; unit < 6; ++unit) scene.clear[unit] = read() != 0;
                    for (std::size_t unit = 1; unit < 6; ++unit) scene.melee_of[unit] = read() != 0;
                    for (std::size_t unit = 1; unit < 6; ++unit) scene.dying[unit] = read() != 0;
                    scene.poisoned = read() != 0; scene.slowed = read() != 0; scene.raging = read() != 0;
                    scene.poison_resist = read(); scene.life = read(); scene.mana = read(); scene.max_life = read(); scene.max_mana = read();
                    scene.skill_calc = read(); scene.skill_level = read(); scene.skill_mode = read(); scene.corpse_id = read(); scene.radius = read();
                    for (auto& value : scene.aip) value = read();
                    scene.skill1 = read(); scene.skill2 = read(); scene.mode1 = read(); scene.mode2 = read(); scene.velocity = read(); scene.run = read();
                    if (const bool corpse = read() != 0; which == 6) scene.nearby = corpse;   // the Death Sentry's corpse is its `nearby`
                    scene.ends = read() != 0;
                    for (auto& [x, y] : scene.end) { x = read(); y = read(); }
                    scene.left = read(); scene.right = read(); scene.owner_class = read(); scene.pet_attack = read() != 0;
                    scene.aip8_nightmare = read(); scene.aip8_hell = read();
                    for (auto& skill : scene.skills) { skill.id = read(); skill.cls = read(); skill.ai_ok = read() != 0; skill.mana = read(); skill.kind = read(); skill.mode = read(); skill.delay = scene.skill_calc; }
                    std::array<int, 3> ctrl{};
                    for (auto& value : ctrl) value = read();
                    d2d::rules::Rng seed;
                    seed.low = std::uint32_t(std::stoll([&] { std::string text; fields >> text; return text; }()));
                    seed.high = std::uint32_t(std::stoll([&] { std::string text; fields >> text; return text; }()));
                    Replay world;
                    std::istringstream call_text(line.substr(bar + 1, bar2 - bar - 1));
                    for (std::string call; std::getline(call_text, call, ';');)
                        if (call.find_first_not_of(' ') != std::string::npos) world.calls.push_back(call.substr(call.find_first_not_of(' ')));
                    d2d::rules::PetBrain brain(scene, seed, ctrl, world);
                    switch (which) {
                    case 0: d2d::rules::hydra_think(brain); break;
                    case 1: d2d::rules::totem_think(brain); break;
                    case 2: d2d::rules::poison_creeper_think(brain); break;
                    case 3: case 4: d2d::rules::cycle_vine_think(brain); break;
                    case 5: d2d::rules::sentry_think(brain); break;
                    case 6: d2d::rules::death_sentry_think(brain); break;
                    case 7: d2d::rules::blade_sentinel_think(brain); break;
                    case 8: d2d::rules::raven_think(brain); break;
                    case 9: d2d::rules::druid_bear_think(brain); break;
                    case 10: d2d::rules::spirit_wolf_think(brain); break;
                    case 12: d2d::rules::shadow_warrior_think(brain); break;
                    default: d2d::rules::fenris_think(brain); break;
                    }
                    assert(world.next == world.calls.size());
                    std::istringstream tail(line.substr(bar2 + 1));
                    std::string kind;
                    int frames = 0, unreachable = 0, unit = 0, skill = 0, mode = 0, x = 0, y = 0;
                    tail >> kind >> frames >> unreachable >> unit >> skill >> mode >> x >> y;
                    using Kind = PetAct::Kind;
                    const auto& act = brain.act;
                    const bool same = kind == "followed" ? act.kind == Kind::stand && act.frames == -7
                        : kind == "stand"  ? act.kind == Kind::stand && act.frames == frames
                        : kind == "failed" ? act.kind == Kind::failed
                        : kind == "die"    ? act.kind == Kind::die
                        : kind == "moved"  ? act.kind == Kind::moved
                        : kind == "chase"  ? act.kind == Kind::chase && int(act.unit) == unit
                        : kind == "swing"  ? act.kind == Kind::swing && int(act.unit) == unit && act.frames == frames
                        : kind == "skill"  ? act.kind == Kind::skill && int(act.unit) == unit && act.skill == skill && act.mode == mode && act.frames == frames && act.x == x && act.y == y
                        : kind == "seq"    ? act.kind == Kind::seq && int(act.unit) == unit && act.skill == skill
                                           : false;
                    assert(same);
                    assert(int(act.unreachable) == unreachable);
                    for (auto& value : ctrl) { int want = 0; tail >> want; assert(value == want); }
                    std::uint32_t low = 0, high = 0;
                    tail >> low >> high;
                    assert(seed.low == low && seed.high == high);
                    ++checked;
                }
                assert(checked > 100);
            }
            // The Shadows (tools/emu/shadow_init.py / shadow_master.py --dump:
            // game.exe's own runs): the Warrior's init and the Master's, then the
            // Master's think (the log of what it asks and does), then its scan.
            {
                static constexpr std::array<std::string_view, 2> kInits{
#include "shadow_init_cases.inc"
                };
                std::istringstream lines{ std::string(kInits[0]) };
                int checked = 0;
                for (std::string line; std::getline(lines, line);) {
                    if (line.empty() || line.find('|') == std::string::npos) continue;
                    std::istringstream fields(line);
                    auto read = [&] { long long value = 0; fields >> value; return value; };
                    const bool owner = read() != 0;
                    const int owner_type = int(read()), summoned = int(read()), summon_level = int(read()), pet_class = int(read()), pet_type = int(read());
                    const bool has_attack = read() != 0;
                    const int aip3 = int(read());
                    d2d::rules::Rng seed;
                    seed.low = std::uint32_t(read()); seed.high = std::uint32_t(read());
                    struct Row { int id, summon, pet_type, hard; };
                    std::vector<Row> rows(static_cast<std::size_t>(read()));
                    std::vector<int> ids;
                    for (auto& row : rows) { row.id = int(read()); row.summon = int(read()); row.pet_type = int(read()); row.hard = int(read()); ids.push_back(row.id); }
                    std::string bar;
                    fields >> bar;
                    std::array<int, 3> ctrl{ 7, 7, 7 };
                    const auto gifts = d2d::rules::shadow_warrior_init(ctrl, owner && owner_type == 0, summoned ? summon_level : -1, has_attack, ids,
                        [&](int id) {
                            const auto& row = *std::ranges::find(rows, id, &Row::id);
                            return id >= 0 && id < 400 && d2d::rules::shadow_may_have(row.summon, row.pet_type, pet_class, pet_type, 20);
                        },
                        [&](int id) { return std::ranges::find(rows, id, &Row::id)->hard; });
                    std::vector<int> want;
                    for (std::string token; fields >> token && token != "|";) want.push_back(std::stoi(token));
                    std::vector<int> got;
                    for (const auto& gift : gifts) { got.push_back(gift.skill); got.push_back(gift.level); }
                    assert(got == want);
                    for (const int value : ctrl) assert(value == read());
                    fields >> bar;
                    const auto master = d2d::rules::shadow_master_init(aip3, seed);
                    for (const int value : master) assert(value == read());
                    assert(seed.low == std::uint32_t(read()) && seed.high == std::uint32_t(read()));
                    ++checked;
                }
                assert(checked > 40);
                std::istringstream uses{ std::string(kInits[1]) };
                checked = 0;
                for (std::string line; std::getline(uses, line);) {
                    if (line.empty()) continue;
                    std::istringstream fields(line);
                    auto read = [&] { int value = 0; fields >> value; return value; };
                    const bool may_have = read() != 0;
                    const int aitype = read();
                    const bool melee = read() != 0, target = read() != 0, in_state = read() != 0, target_in_state2 = read() != 0, progressive = read() != 0;
                    const int charges = read();
                    assert(d2d::rules::shadow_ai_may_use(may_have, aitype, melee, target, in_state, target_in_state2, progressive,
                                                         charges < 0 ? std::nullopt : std::optional<int>(charges)) == (read() != 0));
                    ++checked;
                }
                assert(checked > 300);
                static constexpr std::array<std::string_view, 3> kMaster{
#include "shadow_master_cases.inc"
                };
                struct World {
                    std::vector<int> decides, casts, moves;
                    std::string log;
                    void note(const std::string& text) { log += (log.empty() ? "" : " ; ") + text; }
                    static bool pop(std::vector<int>& answers) { const bool answer = answers.front() != 0; answers.erase(answers.begin()); return answer; }
                    bool decide(int foe, bool melee) { note("decide " + std::to_string(foe) + " " + std::to_string(int(melee))); return pop(decides); }
                    bool skill(int mode, int id, int target) { note("skill " + std::to_string(mode) + " " + std::to_string(id) + " " + std::to_string(target)); return pop(casts); }
                    bool approach(int target) { note("approach " + std::to_string(target)); return pop(moves); }
                    bool away(int x, int y) { note("away " + std::to_string(x) + " " + std::to_string(y)); return pop(moves); }
                    void run(int unit) { note("run " + std::to_string(unit)); }
                    void set_left(int id) { note("left " + std::to_string(id)); }
                    void stand(int frames) { note("stand " + std::to_string(frames)); }
                };
                std::istringstream thinks{ std::string(kMaster[0]) + std::string(kMaster[1]) };
                checked = 0;
                for (std::string line; std::getline(thinks, line);) {
                    if (line.empty()) continue;
                    const auto bar = line.find('|'), bar2 = line.rfind('|');
                    std::istringstream fields(line.substr(0, bar));
                    auto read = [&] { long long value = 0; fields >> value; return value; };
                    d2d::rules::ShadowMasterScene scene;
                    scene.has_list = read() != 0; scene.owner = int(read()); scene.driver = int(read()); scene.driver_melee = read() != 0; scene.driver_distance = int(read());
                    scene.units.resize(8);
                    for (auto& unit : scene.units) {
                        unit.type = int(read()); unit.x = int(read()); unit.y = int(read());
                        unit.targetable = read() != 0; unit.dying = read() != 0; unit.foe = read() != 0; unit.melee = read() != 0; unit.worth = read() != 0;
                        unit.target = int(read()); unit.owner = int(read()); unit.monster_level = int(read());
                        unit.states.resize(std::size_t(read()));
                        for (auto& state : unit.states) state = int(read());
                        for (auto& resist : unit.resist) resist = int(read());
                    }
                    scene.groups.resize(std::size_t(read()));
                    for (auto& group : scene.groups) group = int(read());
                    scene.skills.resize(std::size_t(read()));
                    for (auto& skill : scene.skills) { skill.id = int(read()); skill.level = int(read()); skill.kind = int(read()); skill.mode = int(read()); }
                    for (auto rows = read(); rows-- > 0;) {
                        const int id = int(read());
                        auto& row = scene.rows[id];
                        row.aitype = int(read()); row.bonus = int(read()); row.reqlevel = int(read()); row.etype = int(read());
                        row.state = int(read()); row.state2 = int(read()); row.progressive = read() != 0;
                        row.srvmissile = int(read()); row.missile = int(read()); row.missile_range = int(read()); row.repeat = read() != 0;
                    }
                    for (auto& value : scene.fixed) value = int(read());
                    scene.aip3 = int(read());
                    auto& scan = scene.scan;
                    scan.closest = int(read()); scan.closest_distance = int(read()); scan.close_count = int(read());
                    scan.owner_closest = int(read()); scan.owner_close_count = int(read()); scan.all = int(read()); scan.traps = int(read()); scan.worth = int(read());
                    scene.life = int(read()); scene.left = read() != 0; scene.low = read() != 0; scene.blocked = read() != 0;
                    if (const int charges = int(read()); charges >= 0) scene.charges = charges;
                    scene.town = read() != 0;
                    std::array<int, 3> ctrl{};
                    for (auto& value : ctrl) value = int(read());
                    d2d::rules::Rng seed;
                    seed.low = std::uint32_t(read()); seed.high = std::uint32_t(read());
                    World world;
                    for (int k = 0; k < 4; ++k) world.decides.push_back(int(read()));
                    for (int k = 0; k < 40; ++k) world.casts.push_back(int(read()));
                    for (int k = 0; k < 4; ++k) world.moves.push_back(int(read()));
                    d2d::rules::shadow_master_think(scene, seed, ctrl, world);
                    std::string want = line.substr(bar + 1, bar2 - bar - 1);
                    want = want.substr(want.find_first_not_of(' '));
                    want = want.substr(0, want.find_last_not_of(' ') + 1);
                    assert(world.log == want);
                    std::istringstream tail(line.substr(bar2 + 1));
                    for (const int value : ctrl) { int expected = 0; tail >> expected; assert(value == expected); }
                    std::uint32_t low = 0, high = 0;
                    tail >> low >> high;
                    assert(seed.low == low && seed.high == high);
                    ++checked;
                }
                assert(checked > 80);
                std::istringstream scans{ std::string(kMaster[2]) };
                checked = 0;
                for (std::string line; std::getline(scans, line);) {
                    if (line.empty()) continue;
                    std::istringstream fields(line);
                    auto read = [&] { int value = 0; fields >> value; return value; };
                    const bool owner = read() != 0;
                    d2d::rules::ShadowUnit owner_unit{ .type = 0, .x = read(), .y = read(), .targetable = false, .foe = false };
                    std::vector<d2d::rules::ShadowUnit> units(static_cast<std::size_t>(read()));
                    int pet = -1;
                    for (int index = 0; index < int(units.size()); ++index) {
                        auto& unit = units[std::size_t(index)];
                        unit.type = read(); unit.x = read(); unit.y = read();
                        unit.targetable = read() != 0; unit.dying = read() != 0; unit.foe = read() != 0; unit.side = read() != 0;
                        unit.monster_id = read(); unit.worth = read() != 0;
                        if (unit.type < 0) { pet = index; unit = { .x = 5000, .y = 5000 }; }
                    }
                    units.push_back(owner_unit);
                    const auto scan = d2d::rules::shadow_scan(units, pet, owner ? int(units.size()) - 1 : -1);
                    std::string bar;
                    fields >> bar;
                    assert(scan.closest == read() && scan.closest_distance == read() && scan.close_count == read());
                    assert(scan.owner_closest == read() && scan.owner_closest_distance == read() && scan.owner_close_count == read());
                    assert(scan.all == read() && scan.traps == read() && scan.worth == read());
                    ++checked;
                }
                assert(checked > 30);
            }
            // The merc's attack think and skill pick (FUN_005e5050 / FUN_005e4d30;
            // tools/emu/merc_attack.py --dump, game.exe's own hireling rows):
            // class, level, aip1, gap, melee, +0x14, seed; the row's Level,
            // DefaultChance, skills, chances, per level, modes; its skill levels,
            // auras, running; Skill1 / Sk1mode; the moves' results; the outcome,
            // its skill and mode, +0x14 and the seed after.
            {
                using AttackKind = d2d::rules::MercAttack::Kind;
                struct AttackCase {
                    int cls, level, aip1, gap, melee, growth; std::uint32_t low, high; int row_level, default_chance;
                    std::array<int, 6> ids, chance, per, mode, levels, aura, running; int skill1, sk1mode;
                    std::vector<int> moves; AttackKind kind; int skill, use_mode, growth_after; std::uint32_t after_low, after_high;
                };
                const std::vector<AttackCase> attack_cases{
                    { 359, 33, 0, 5, 0, 0, 1069673014u, 2787324501u, 49, 10, { 55, 40, 45, 0, 0, 0 }, { 60, 1000, 240, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 7, 7, 7, 0, 0, 0 }, { 23, 0, 30, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, {  }, AttackKind::stand, -1, 0, 10, 1272892899u, 446152452u },
                    { 271, 21, 0, 0, 1, 30, 1752995436u, 1200367645u, 3, 75, { 8, 11, 0, 0, 0, 0 }, { 10, 25, 0, 0, 0, 0 }, { 0, 5, 0, 0, 0, 0 }, { 4, 4, 0, 0, 0, 0 }, { 28, 18, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 336, 4, { 0, 1 }, AttackKind::moved, -1, 0, 0, 1561708745u, 60308774u },
                    { 271, 50, 0, 3, 1, 0, 22819762u, 899608315u, 67, 75, { 8, 11, 0, 0, 0, 0 }, { 10, 69, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 4, 4, 0, 0, 0, 0 }, { 3, 20, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 336, 4, { 0, 0 }, AttackKind::skill, 11, 4, 0, 3071597439u, 623125764u },
                    { 561, 28, 1, 5, 0, 60, 4159919668u, 2966284567u, 28, 50, { 126, 139, 0, 0, 0, 0 }, { 15, 15, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 5, 5, 0, 0, 0, 0 }, { 0, 7, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, { 1 }, AttackKind::moved, -1, 0, 0, 2479128347u, 1735070750u },
                    { 338, 50, 1, 2, 1, 0, 2517457551u, 14530381u, 43, 30, { 10, 99, 0, 0, 0, 0 }, { 104, 10, 0, 0, 0, 0 }, { 4, 0, 0, 0, 0, 0 }, { 14, 1, 0, 0, 0, 0 }, { 14, 5, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 10, 14, {  }, AttackKind::skill, 10, 14, 0, 1213407590u, 424638134u },
                    { 359, 87, 0, 3, 1, 60, 4033025023u, 98206823u, 79, 10, { 55, 40, 45, 0, 0, 0 }, { 60, 1000, 240, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 7, 7, 7, 0, 0, 0 }, { 10, 10, 11, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, -1, 0, {  }, AttackKind::skill, 45, 7, 0, 353992406u, 808010879u },
                    { 338, 16, 1, 2, 1, 30, 3331189597u, 1987924596u, 9, 30, { 10, 99, 0, 0, 0, 0 }, { 70, 10, 0, 0, 0, 0 }, { 4, 0, 0, 0, 0, 0 }, { 14, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 10, 14, {  }, AttackKind::swing, -1, 0, 0, 2578390618u, 1133776383u },
                    { 359, 21, 0, 0, 0, 0, 1074638861u, 1957066694u, 15, 10, { 41, 47, 0, 0, 0, 0 }, { 60, 30, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 7, 7, 0, 0, 0, 0 }, { 20, 13, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, { 1 }, AttackKind::moved, -1, 0, 0, 1818072910u, 807067680u },
                    { 338, 46, 1, 2, 1, 0, 4109290207u, 1644190109u, 75, 30, { 10, 108, 0, 0, 0, 0 }, { 104, 10, 0, 0, 0, 0 }, { 4, 0, 0, 0, 0, 0 }, { 14, 1, 0, 0, 0, 0 }, { 1, 5, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 10, 14, {  }, AttackKind::aura, 108, 0, 0, 3904325964u, 772198829u },
                    { 561, 64, 1, 2, 1, 30, 1514990962u, 4268459348u, 80, 50, { 126, 139, 0, 0, 0, 0 }, { 70, 70, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 5, 5, 0, 0, 0, 0 }, { 21, 9, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, {  }, AttackKind::stand, -1, 0, 40, 2606390798u, 631891170u },
                    { 359, 66, 0, 3, 1, 20, 493077469u, 2641610868u, 79, 10, { 41, 47, 0, 0, 0, 0 }, { 60, 30, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 7, 7, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, {  }, AttackKind::swing, -1, 0, 0, 4122923759u, 514194593u },
                    { 359, 56, 0, 3, 0, 20, 1193839240u, 3146637366u, 79, 10, { 38, 49, 0, 0, 0, 0 }, { 60, 30, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 7, 7, 0, 0, 0, 0 }, { 9, 12, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, { 0, 0 }, AttackKind::stand, -1, 0, 0, 15533903u, 1399659081u },
                    { 359, 21, 0, 3, 1, 20, 1330804564u, 2236366800u, 79, 10, { 38, 49, 0, 0, 0, 0 }, { 60, 30, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 7, 7, 0, 0, 0, 0 }, { 0, 25, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, -1, 0, { 0, 0 }, AttackKind::swing, -1, 0, 0, 1761635358u, 1037823986u },
                    { 338, 33, 1, 1, 1, 0, 934367838u, 2555213081u, 75, 30, { 10, 99, 0, 0, 0, 0 }, { 104, 10, 0, 0, 0, 0 }, { 4, 0, 0, 0, 0, 0 }, { 14, 1, 0, 0, 0, 0 }, { 24, 6, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 10, 14, {  }, AttackKind::aura, 99, 0, 0, 708982570u, 1475047410u },
                    { 338, 54, 1, 5, 0, 0, 2503952625u, 4070378921u, 75, 30, { 10, 104, 0, 0, 0, 0 }, { 104, 10, 0, 0, 0, 0 }, { 4, 0, 0, 0, 0, 0 }, { 14, 1, 0, 0, 0, 0 }, { 18, 27, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 10, 14, { 0 }, AttackKind::failed, -1, 0, 0, 2579349278u, 1044379533u },
                    { 338, 71, 1, 0, 1, 0, 2779566913u, 1803954443u, 43, 30, { 10, 104, 0, 0, 0, 0 }, { 104, 10, 0, 0, 0, 0 }, { 4, 0, 0, 0, 0, 0 }, { 14, 1, 0, 0, 0, 0 }, { 14, 10, 0, 0, 0, 0 }, { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0 }, 10, 14, {  }, AttackKind::aura, 104, 0, 0, 3887533861u, 774095468u },
                };
                for (const auto& entry : attack_cases) {
                    d2d::rules::Hireling row;
                    row.version = 100; row.level = entry.row_level; row.default_chance = entry.default_chance;
                    for (std::size_t k = 0; k < 6; ++k) row.skills[k] = { "", entry.mode[k], entry.chance[k], entry.per[k], 0, 0 };
                    d2d::rules::MercFightView fight{ .cls = entry.cls, .level = entry.level, .aip1 = entry.aip1, .gap = entry.gap, .distance = entry.gap + 1,
                                                     .melee = entry.melee != 0, .row = &row, .ids = entry.ids, .levels = entry.levels, .skill1 = entry.skill1, .sk1mode = entry.sk1mode };
                    for (std::size_t k = 0; k < 6; ++k) { fight.aura[k] = entry.aura[k] != 0; fight.running[k] = entry.running[k] != 0; }
                    d2d::rules::Rng seed;
                    seed.low = entry.low; seed.high = entry.high;
                    int growth = entry.growth;
                    std::size_t next = 0;
                    const auto attack = d2d::rules::merc_attack(fight, growth, 5000, 5000, 5001 + entry.gap, 5000, seed, [&](int, int, int) {
                        assert(next < entry.moves.size());
                        return entry.moves[next++] != 0;
                    });
                    assert(next == entry.moves.size() && attack.kind == entry.kind && growth == entry.growth_after && seed.low == entry.after_low && seed.high == entry.after_high);
                    if (attack.kind == AttackKind::skill || attack.kind == AttackKind::aura) assert(attack.skill == entry.skill && attack.mode == entry.use_mode);
                }
            }
            // A merc's search, mode 6 (FUN_005ddc30; search.py checks it against
            // game.exe): the primary (threat 2+) over a nearer secondary, the
            // first at a tie, sight, 0x31 out.
            {
                using d2d::rules::SightFoe;
                int distance = 0;
                auto pick_of = [&](const std::vector<SightFoe>& foes, bool path) {
                    return d2d::rules::sight_choice(foes, d2d::rules::search_sight(foes), [&](int) { return path; }, distance);
                };
                const std::vector<SightFoe> picks{ { 10, 0, true, false }, { 20, 3, true, false }, { 20, 5, true, false }, { 5, 14, true, true } };
                assert(pick_of(picks, true) == 1 && distance == 20);
                const std::vector<SightFoe> lows{ { 0x31, 3, true, false }, { 12, 1, true, false }, { 9, 0, false, false } };
                assert(pick_of(lows, true) == 1 && distance == 12);
                assert(pick_of(std::vector<SightFoe>{ { 0x30, 2, true, true } }, true) == -1 && distance == 0x7fffffff);
                // No path to the primary with the secondary under 6: mode 7's
                // other threat within 0x13, else the secondary.
                const std::vector<SightFoe> walled{ { 10, 3, true, false }, { 4, 0, true, false }, { 15, 2, true, false } };
                assert(pick_of(walled, false) == 2 && distance == 15 && pick_of(walled, true) == 0);
                const std::vector<SightFoe> alone{ { 10, 3, true, false }, { 4, 0, true, false }, { 25, 2, true, false } };
                assert(pick_of(alone, false) == 1 && distance == 4);
                // An invisible monster in melee isn't a foe (state 0x92).
                assert(pick_of(std::vector<SightFoe>{ { 1, 3, true, false, true, true }, { 3, 3, true, false, true, false } }, true) == 1);
            }
            // The wall pather (FUN_0067c2d0, moves.py --dump): to, steps, to a
            // unit, points, walls.
            struct WallRoute { int to_x, to_y, steps; bool to_unit; std::vector<std::pair<int, int>> points, walls; };
            const std::vector<WallRoute> wall_routes{
                { 5, -2, 40, false, { {2, 0}, {3, -1}, {4, -1}, {5, -2} }, {} },
                { -8, -1, 40, false, { {-1, -1}, {-2, 0}, {-7, 0}, {-8, -1} }, { {-1, 0} } },
                { 12, 2, 40, false, { {3, 0}, {4, 1}, {5, 0}, {6, 1}, {11, 1} }, { {4, 0}, {12, 1}, {12, 2} } },
                { 3, 27, 40, false, { {-1, 1}, {0, 2}, {0, 6}, {-1, 7}, {0, 8}, {1, 9}, {1, 16}, {0, 17} }, { {0, 1}, {0, 7}, {1, 2}, {1, 17} } },
                { 4, -7, 40, false, { {0, -1}, {1, -2}, {2, -3}, {6, -3}, {6, -5}, {2, -5}, {4, -7} },
                  { {-1, -4}, {0, -4}, {0, -3}, {1, -4}, {2, -4}, {3, -4}, {4, -4}, {5, -4} } },
            };
            for (const auto& route : wall_routes)
                assert(wall_path(0, 0, route.to_x, route.to_y, route.steps, route.to_unit, [&](int x, int y) {
                    return std::ranges::contains(route.walls, std::pair(x, y)); }) == route.points);
            assert(wall_path(0, 0, 2, 0, 40, false, [](int, int) { return false; }).empty());          // two subtiles: none
            assert(wall_path(0, 0, 9, 0, 5, false, [](int, int) { return false; }).empty());           // past steps - 1: none
            // The search round a wall across the way (game.exe's own answer,
            // tools/emu); none from where it stands.
            auto across = [](int x, int y) { return x == 3 && y > -6 && y < 5; };
            assert((search_path(0, 0, 6, 0, false, across) == std::vector<std::pair<int, int>>{ { 2, 2 }, { 2, 4 }, { 3, 5 }, { 4, 4 }, { 4, 2 }, { 6, 0 } }));
            assert(search_path(0, 0, 0, 0, false, across).empty());
            const std::vector<std::array<int, 12>> chases{ {
                { 0, 0, 1, 0, 0, 0, 4, 5, 1, 20, 1, 20 }, { 1, 25, 1, 1, -7, 7, 0, 5, 1, 1, 2, 1 }, { 1, 6, 0, 1, 1, 3, 0, 2, 1, 0, 1, 0 },
                { 1, 4, 3, 1, -4, 7, 1, 6, 1, 20, 2, 19 }, { 1, 6, 3, 0, 3, 1, 1, 2, 0, 1, 1, 1 }, { 0, 0, 3, 0, 0, 0, 6, 2, 0, 0, 0, 0 },
                { 1, 6, 0, 1, -2, 7, 1, 2, 0, 3, 2, 2 }, { 1, 10, 3, 1, 4, -8, 4, 5, 1, 20, 2, 16 }, { 1, 0, 3, 1, 2, -3, 0, 6, 0, 0, 0, 0 },
                { 1, 0, 3, 1, 1, 2, 2, 4, 1, 1, 0, 1 }, { 1, 3, 0, 1, 8, 3, 0, 2, 1, 20, 2, 20 }, { 1, 0, 3, 1, -1, 0, 1, 1, 0, 1, 0, 1 },
            } };
            for (const auto& chase : chases) {
                int budget = chase[9];
                assert(chase_check(chase[0], chase[1], chase[2], chase[3], chase[4], chase[5], chase[6], chase[7], chase[8], budget) == chase[10] && budget == chase[11]);
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
            for (const auto& entry : cases) {
                const auto pick = search_near(entry.foes, entry.align, entry.sight);
                assert(pick.target == entry.want.target && pick.best == entry.want.best && pick.second == entry.want.second && pick.second_best == entry.want.second_best);
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
        auto steps = [](std::uint32_t seed, Rng& stepped) { Rng count{ seed }; int step_count = 0; while (count.low != stepped.low || count.high != stepped.high) { (void)count.next(); ++step_count; } return step_count; };
        auto no_away = [](int, bool) { return false; };
        auto ok_away = [](int, bool) { return true; };
        int state = 0;
        ThinkIn think_in{ .aip = { 60, 15, 75, 75 }, .state = &state };
        for (std::uint32_t seed = 1; seed < 50; ++seed) {           // Skeleton: aip1 % walk, in melee aip3 % (aip4 % A1) else stand aip2
            for (const bool in_melee : { false, true }) {
                think_in.in_melee = in_melee;
                Rng roll{ seed }, mirror{ seed };
                const auto act = mon_think("Skeleton", think_in, roll, no_away);
                MonAct want = MonAct::idle;
                if (!in_melee) { if (mirror(100) < 60) want = MonAct::walk; }
                else if (mirror(100) < 75) want = mirror(100) < 75 ? MonAct::a1 : MonAct::a2;
                assert(act.act == want && roll.low == mirror.low && (want != MonAct::idle || act.n == 15));
            }
        }
        think_in = { .aip = { 30, 10, 0, 20 }, .dist = 12, .state = &state };   // Zombie: out of aip2, wanders 3 (4 steps), in the Burial Grounds runs
        Rng roll{ 5 };
        const auto wander = mon_think("Zombie", think_in, roll, no_away);
        assert(wander.act == MonAct::wander && steps(5, roll) == 4 && std::abs(wander.x) <= 3 && std::abs(wander.y) <= 3 && (std::abs(wander.x) == 3 || std::abs(wander.y) == 3));
        think_in.level = 17; roll = Rng{ 5 };
        assert(mon_think("Zombie", think_in, roll, no_away).act == MonAct::run && steps(5, roll) == 0);
        think_in.level = 2; think_in.got_hit = true; roll = Rng{ 5 };
        assert(mon_think("Zombie", think_in, roll, no_away).act == MonAct::run && steps(5, roll) == 0);
        // QuillRat: past aip2's roll it backs off aip4; blocked and close, spikes.
        think_in = { .aip = { 10, 0, 0, 2 }, .dist = 3, .state = &state };
        roll = Rng{ 9 };
        assert(mon_think("QuillRat", think_in, roll, ok_away).act == MonAct::none && steps(9, roll) == 1);
        roll = Rng{ 9 };
        assert(mon_think("QuillRat", think_in, roll, no_away).act == MonAct::a2 && steps(9, roll) == 1);
        think_in.dist = 10; roll = Rng{ 9 };
        assert(mon_think("QuillRat", think_in, roll, no_away).act == MonAct::wander && steps(9, roll) == 4);
        // CorruptArcher: close, a blocked run-off falls through to the shot rolls.
        think_in = { .aip = { 60, 100, 14, 100, 20, 0, 0, 12 }, .dist = 4, .state = &state };
        roll = Rng{ 3 };
        assert(mon_think("CorruptArcher", think_in, roll, no_away).act == MonAct::a1 && steps(3, roll) == 2);
        think_in.dist = 15; roll = Rng{ 3 };
        const auto closer = mon_think("CorruptArcher", think_in, roll, no_away);
        assert(closer.act == MonAct::approach && closer.n == 12 && steps(3, roll) == 1);
        think_in.dist = 30; think_in.aip[0] = 0; roll = Rng{ 3 };
        assert(mon_think("CorruptArcher", think_in, roll, no_away).act == MonAct::run && steps(3, roll) == 1);
        // CorruptLancer: a run in past aip5 strikes on arrival without the aip2 roll.
        think_in = { .aip = { 60, 0, 9, 0, 15 }, .dist = 20, .state = &state };
        roll = Rng{ 4 };
        assert(mon_think("CorruptLancer", think_in, roll, no_away).act == MonAct::run && state == 1 && steps(4, roll) == 0);
        think_in.dist = 1; think_in.in_melee = true; roll = Rng{ 4 };
        assert(mon_think("CorruptLancer", think_in, roll, no_away).act == MonAct::a1 && state == 0 && steps(4, roll) == 0);
        roll = Rng{ 4 };
        assert(mon_think("CorruptLancer", think_in, roll, no_away).act == MonAct::idle && steps(4, roll) == 1);
        // CorruptRogue runs in past 20 - 3 x difficulty; Brute's circle is a seed step more.
        think_in = { .aip = { 60, 15, 75, 100, 20 }, .dist = 15, .difficulty = 2, .state = &state };
        roll = Rng{ 6 };
        assert(mon_think("CorruptRogue", think_in, roll, no_away).act == MonAct::run && steps(6, roll) == 0);
        think_in = { .aip = { 0, 0, 100, 45 }, .in_melee = true, .state = &state };
        think_in.aip[2] = 0; roll = Rng{ 6 };
        assert(mon_think("Brute", think_in, roll, no_away).act == MonAct::idle && steps(6, roll) == 2);
        think_in.aip[2] = 100; roll = Rng{ 6 };
        assert(mon_think("Brute", think_in, roll, no_away).act != MonAct::circle && steps(6, roll) == 2);
        assert(!traced_ai("Imp") && mon_think("Imp", think_in, roll, no_away).act == MonAct::untraced);
        // Griswold: in melee 80 % A1, else 50 % walks in, else stands 10;
        // the Smith walks in at half the life % he's lost.
        int state2 = 0, state3 = 0, pace = 0;
        for (std::uint32_t seed = 1; seed < 30; ++seed)
            for (const bool in_melee : { false, true }) {
                think_in = { .in_melee = in_melee, .state = &state };
                Rng mirror{ seed };
                roll = Rng{ seed };
                const auto act = mon_think("Griswold", think_in, roll, no_away);
                assert(act.act == (in_melee ? (mirror(100) < 80 ? MonAct::a1 : MonAct::idle) : (mirror(100) < 50 ? MonAct::walk : MonAct::idle)) && roll.low == mirror.low);
            }
        think_in = { .life_pct = 40, .state = &state, .pace = &pace };
        roll = Rng{ 2 };
        const auto smith = mon_think("Smith", think_in, roll, no_away);
        assert(traced_ai("Smith") && smith.act == MonAct::walk && smith.n == 7 && pace == 30 && steps(2, roll) == 0);
        // Fetish: squares up in melee; after aip3 thinks, its target over
        // aip4 % life, backs off at pace 50 (blocked: no pace, a think at
        // once); out of melee walks in at pace 50.
        think_in = { .aip = { 100, 7, 2, 50 }, .in_melee = true, .state = &state, .state2 = &state2, .pace = &pace, .target_life_pct = 90 };
        state = 0; pace = 0; roll = Rng{ 3 };
        assert(mon_think("Fetish", think_in, roll, no_away).act == MonAct::a1 && state == 1 && state2 == 0 && steps(3, roll) == 1);
        state2 = 2; roll = Rng{ 3 };
        assert(mon_think("Fetish", think_in, roll, ok_away).act == MonAct::none && state == 2 && state2 == 0 && pace == 50 && steps(3, roll) == 0);
        state = 1; state2 = 2; roll = Rng{ 3 };
        assert(mon_think("Fetish", think_in, roll, no_away).act == MonAct::none && state == 2 && pace == 0);
        think_in.in_melee = false; state = 0; roll = Rng{ 3 };
        const auto fetish_walk = mon_think("Fetish", think_in, roll, no_away);
        assert(fetish_walk.act == MonAct::walk && fetish_walk.n == 7 && pace == 50 && steps(3, roll) == 0);
        think_in.dist = 20; state = 2; state2 = 1; roll = Rng{ 3 };
        assert(mon_think("Fetish", think_in, roll, no_away).act != MonAct::walk && state == 0 && state2 == 0);
        // Arach: hurt, healed past 75 % it charges (aip3 %); hurt in melee
        // under aip5 % life it lays (Skill1), or laid, backs off 8 (blocked:
        // stands aidel).
        think_in = { .aip = { 0, 0, 100, 10, 50 }, .life_pct = 80, .state = &state, .state2 = &state2, .state3 = &state3, .aidel = 9 };
        think_in.skill[0] = true;
        state = 1; roll = Rng{ 4 };
        const auto arach_charge = mon_think("Arach", think_in, roll, no_away);
        assert(traced_ai("Arach") && arach_charge.act == MonAct::walk && arach_charge.n == 0 && state == 2 && steps(4, roll) == 1);
        think_in.in_melee = true; think_in.life_pct = 10; state = 0; roll = Rng{ 4 };
        const auto lay = mon_think("Arach", think_in, roll, no_away);
        assert(lay.act == MonAct::skill && lay.n == 0 && state == 1 && steps(4, roll) == 1);
        think_in.laying = true; state = 0; roll = Rng{ 4 };
        const auto stuck = mon_think("Arach", think_in, roll, no_away);
        assert(stuck.act == MonAct::idle && stuck.n == 9 && state == 1);
        // Vampire (aip5 bit 0: shots): hit in melee 30 % a shot (Skill1 or
        // Skill4), else A1; fleeing near it backs off 8 at Run / Velocity's
        // pace; within aip3, aip2 % a shot.
        think_in = { .aip = { 100, 100, 30, 0, 1 }, .in_melee = true, .got_hit = true, .dist = 1, .life_pct = 100, .state = &state, .state2 = &state2,
               .pace = &pace, .state3 = &state3, .velocity = 6, .run = 9 };
        for (std::uint32_t seed = 1; seed < 30; ++seed) {
            state = 0; state2 = 0; roll = Rng{ seed };
            Rng mirror{ seed };
            const auto act = mon_think("Vampire", think_in, roll, no_away);
            const MonAct want = mirror(100) > 30 ? MonAct::a1 : MonAct::skill;
            assert(act.act == want && state == 1 && state2 == 1 && (want == MonAct::a1 || act.n == (mirror(100) < 50 ? 0 : 3)) && roll.low == mirror.low);
        }
        think_in.got_hit = false; think_in.in_melee = false; think_in.life_pct = 50; think_in.dist = 5; state = 2; pace = 0; roll = Rng{ 5 };
        assert(mon_think("Vampire", think_in, roll, ok_away).act == MonAct::none && pace == 50 && steps(5, roll) == 0);
        state = 1; think_in.dist = 10; roll = Rng{ 5 };
        const auto shoot = mon_think("Vampire", think_in, roll, no_away).act;           // 25 % circles 4 first
        assert((shoot == MonAct::skill || shoot == MonAct::circle) && state == 1 && steps(5, roll) == 3);
        // Fallen: a scare backs off (and a scream roll) or falls through;
        // charging walks in without a draw; a leader taunts, rallying.
        int command = 1;
        bool rally = false;
        think_in = { .aip = { 100, 10, 0, 20 }, .dist = 12, .state = &state, .dying = true, .command = &command, .rally = &rally };
        state = 0; roll = Rng{ 8 };
        assert(mon_think("Fallen", think_in, roll, ok_away).act == MonAct::none && state == 1 && command == 0 && steps(8, roll) == 1);
        command = 1; think_in.aip[1] = 15; roll = Rng{ 8 };                     // blocked: on as uncommanded, within aip2
        const auto fell_through = mon_think("Fallen", think_in, roll, no_away);
        assert(fell_through.act == MonAct::walk && fell_through.n == 7 && command == 0 && steps(8, roll) == 0);
        think_in.dying = false; command = 1; roll = Rng{ 8 };
        const auto charge = mon_think("Fallen", think_in, roll, no_away);
        assert(charge.act == MonAct::walk && charge.n == 0 && steps(8, roll) == 0);
        think_in.in_melee = true; roll = Rng{ 8 };
        assert(mon_think("Fallen", think_in, roll, no_away).act == MonAct::idle && steps(8, roll) == 1);
        command = 0; think_in.leader = true; think_in.in_melee = false; roll = Rng{ 8 };
        assert(mon_think("Fallen", think_in, roll, no_away).act == MonAct::s2 && rally && steps(8, roll) == 1);
        think_in.leader = false; think_in.in_melee = true; state = 1; roll = Rng{ 8 };   // scared: swings without the aip3 roll
        const auto swing = mon_think("Fallen", think_in, roll, no_away);
        assert((swing.act == MonAct::a1 || swing.act == MonAct::a2) && state == 0 && steps(8, roll) == 1);
        // FallenShaman: the rally roll, then a corpse raised, else two fire
        // rolls within aip5, then circle or stand.
        rally = false;
        think_in = { .aip = { 100, 0, 0, 24, 15 }, .dist = 5, .skill = { true, true }, .state = &state, .corpse = true, .command = &command, .rally = &rally };
        roll = Rng{ 2 };
        const auto raise = mon_think("FallenShaman", think_in, roll, no_away);
        assert(raise.act == MonAct::skill && raise.n == 0 && rally && steps(2, roll) == 2);
        think_in.corpse = false; roll = Rng{ 2 };
        assert(mon_think("FallenShaman", think_in, roll, no_away).act == MonAct::idle && steps(2, roll) == 4);
        think_in.dist = 15; think_in.aip[2] = 100; roll = Rng{ 2 };
        assert(mon_think("FallenShaman", think_in, roll, no_away).act == MonAct::circle && steps(2, roll) == 3);
        think_in.aip[1] = 100; think_in.dist = 14; roll = Rng{ 2 };
        const auto fire = mon_think("FallenShaman", think_in, roll, no_away);
        assert(fire.act == MonAct::skill && fire.n == 1 && steps(2, roll) == 2);
        // FoulCrowNest: lays every aip1 frames (no draw), else stands 20..29
        // on a seed step; blocked, the clock restarts all the same; done
        // laying aip3, it collapses.
        int laid = 0;
        state = 0;
        think_in = { .aip = { 100, 0, 2 }, .dist = 10, .skill = { true }, .state = &state, .frame = 150, .state2 = &laid };
        roll = Rng{ 4 };
        assert(mon_think("FoulCrowNest", think_in, roll, no_away).act == MonAct::skill && state == 150 && laid == 1 && steps(4, roll) == 0);
        think_in.frame = 200; roll = Rng{ 4 };
        Rng expect{ 4 };
        const auto wait = mon_think("FoulCrowNest", think_in, roll, no_away);
        assert(wait.act == MonAct::idle && wait.n == int(expect.next() % 10) + 20 && steps(4, roll) == 1);
        think_in.frame = 250; think_in.spot_free = false; roll = Rng{ 4 };
        assert(mon_think("FoulCrowNest", think_in, roll, no_away).act == MonAct::idle && state == 250 && laid == 1);
        laid = 2;
        assert(mon_think("FoulCrowNest", think_in, roll, no_away).act == MonAct::die);
        think_in.dist = 21;
        assert(mon_think("FoulCrowNest", think_in, roll, no_away).n == 25);
        // BloodRaven: past 45 stands 5; far from home she heads back (the
        // flag holds until within 5); past 20 closes in (4 steps); a raise
        // is rand(100) + rand(15) + three bits (6 steps), 5..19 off.
        int back = 0, raised = 0;
        state = 0;
        think_in = { .dist = 46, .skill = { true, true }, .state = &state, .command = &back, .state2 = &raised };
        roll = Rng{ 5 };
        assert(mon_think("BloodRaven", think_in, roll, no_away).n == 5 && steps(5, roll) == 0);
        think_in.dist = 10; think_in.home_dist = 50;
        assert(mon_think("BloodRaven", think_in, roll, no_away).act == MonAct::home && back == 1);
        think_in.home_dist = 6;
        assert(mon_think("BloodRaven", think_in, roll, no_away).act == MonAct::home && steps(5, roll) == 0);
        think_in.dist = 30; think_in.home_dist = 5;
        const auto br_close = mon_think("BloodRaven", think_in, roll, no_away);
        assert(back == 0 && br_close.act == MonAct::around && br_close.n == 15 && steps(5, roll) == 4 && state == 0);
        think_in.dist = 10; state = 97; roll = Rng{ 5 };
        const auto br_raise = mon_think("BloodRaven", think_in, roll, no_away);
        const int reach = std::max(std::abs(br_raise.x), std::abs(br_raise.y));
        assert(br_raise.act == MonAct::skill && br_raise.n == 0 && reach >= 5 && reach < 20 && raised == 1 && state == 0 && steps(5, roll) == 6);
        think_in.in_melee = true; think_in.dist = 3; roll = Rng{ 5 };                  // in melee no raise; 30 % backs off, else A1
        assert(mon_think("BloodRaven", think_in, roll, no_away).act == MonAct::a1 && steps(5, roll) == 1 && state == 3);
        think_in.in_melee = false; think_in.got_hit = true; think_in.dist = 10; raised = 8; roll = Rng{ 5 };   // raised enough; hit: no strike
        expect = Rng{ 5 };
        const bool about = expect(100) < 5;
        const auto hit = mon_think("BloodRaven", think_in, roll, no_away);
        assert(hit.act == (about ? MonAct::around : MonAct::circle) && steps(5, roll) == (about ? 5 : 2));
        // SkeletonMage (aip 35 9 30 5 0 18 20 5, skmage_*1): within 18 aip1 %
        // shoots (aip5 0: the back-off draws but never goes); else circles 4
        // (one more step) or stands 5; past 9 the close-in is rolled twice.
        think_in = { .aip = { 100, 9, 30, 5, 0, 18, 20, 5 }, .dist = 4, .state = &state };
        roll = Rng{ 6 };
        assert(mon_think("SkeletonMage", think_in, roll, no_away).act == MonAct::a1 && steps(6, roll) == 2);
        think_in.dist = 30; think_in.aip[2] = 100; roll = Rng{ 6 };
        const auto mage_in = mon_think("SkeletonMage", think_in, roll, no_away);
        assert(mage_in.act == MonAct::approach && mage_in.n == 9 && steps(6, roll) == 1);
        think_in.dist = 20; think_in.aip[2] = 0; think_in.aip[6] = 100; roll = Rng{ 6 };
        assert(mon_think("SkeletonMage", think_in, roll, no_away).act == MonAct::circle && steps(6, roll) == 4);
        // GargoyleTrap (aip 24 20 12 15): a shot's aip3 waits first; within
        // 5 of an axis and aip1 it rolls aip2 %; else stands aip4, no draw.
        state = 7;
        think_in = { .aip = { 24, 100, 12, 15 }, .dist = 10, .skill = { true }, .state = &state, .off_x = 3, .off_y = 9 };
        roll = Rng{ 7 };
        const auto trap_wait = mon_think("GargoyleTrap", think_in, roll, no_away);
        assert(trap_wait.act == MonAct::idle && trap_wait.n == 7 && state == 0 && steps(7, roll) == 0);
        assert(mon_think("GargoyleTrap", think_in, roll, no_away).act == MonAct::skill && state == 12 && steps(7, roll) == 1);
        state = 0; think_in.off_x = 6; roll = Rng{ 7 };
        const auto trap_off = mon_think("GargoyleTrap", think_in, roll, no_away);
        assert(trap_off.act == MonAct::idle && trap_off.n == 15 && steps(7, roll) == 0);
        // Its shot (FUN_005cc050): square on, from a sixth of the way less 1.
        assert((d2d::rules::gargoyle_shot(10, 10, 12, 30) == std::array{ 9, 12, 2, 20 }));
        assert((d2d::rules::gargoyle_shot(10, 10, 30, 11) == std::array{ 12, 9, 20, 1 }));
        assert((d2d::rules::gargoyle_shot(10, 10, 16, 16) == std::array{ 10, 9, 6, 4 }));
        assert((d2d::rules::gargoyle_shot(10, 10, 4, 0) == std::array{ 9, 8, -4, -10 }));
        // The Countess (FUN_005e5c50): away from home's room she walks back;
        // a target elsewhere gets the firewall from home only; the map AI
        // points in turn, then rand(100); 700 frames on they start over.
        const std::pair<int, int> path[] = { { 1, 2 }, { 3, 4 } };
        int fired = 0, when = 0;
        think_in = { .aip = { 0, 5, 100 }, .in_melee = true, .dist = 30, .state = &fired, .frame = 10, .state2 = &when };
        roll = Rng{ 8 };
        assert(countess_think(think_in, { .away = true }, path, roll).act == MonAct::home);
        assert(countess_think(think_in, { .target_away = true }, path, roll).act == MonAct::home);
        assert(countess_think(think_in, { .target_away = true, .at_home = true }, path, roll).n == 10 && steps(8, roll) == 0);
        think_in.dist = 5;
        const auto think_a = countess_think(think_in, {}, path, roll), think_b = countess_think(think_in, {}, path, roll);
        assert(think_a.act == MonAct::point && think_a.x == 1 && think_b.y == 4 && fired == 2 && when == 10 && steps(8, roll) == 0);
        assert(countess_think(think_in, {}, path, roll).act == MonAct::a1 && steps(8, roll) == 1);
        think_in.frame = 711;
        assert(countess_think(think_in, {}, path, roll).act == MonAct::a1 && fired == 0);
        assert(countess_think(think_in, {}, path, roll).act == MonAct::point);
        think_in.home_dist = 41;
        assert(countess_think(think_in, {}, path, roll).act == MonAct::home);
        assert(d2d::rules::monster_skill_level(10, 2) == 17);
    }
    {   // A town NPC's think (MonAI Npc, FUN_005e7130; tools/emu npcs.py).
        using d2d::rules::NpcAct;
        assert(d2d::rules::npc_distance(0, 0, 4, 1) == 4 && d2d::rules::npc_distance(0, 0, 1, 4) == 4);
        d2d::rules::NpcBrain brain;
        Rng seed{ 5 };
        const d2d::rules::NpcPoint path[] = { { 4, 30, 30 } };
        auto act = d2d::rules::npc_think(brain, seed, path, 10, 10, 0x9a, 1u << 8, 1);
        assert(act.kind == NpcAct::Kind::stand && act.frames == 20 && brain.home_x == 10 && seed.low == 5);   // home, no draw
        auto peek = seed;
        const bool roams = peek(100) < 66;
        act = d2d::rules::npc_think(brain, seed, path, 10, 10, 0x9a, 1u << 8, 1);
        assert(roams ? act.kind == NpcAct::Kind::walk && act.x == 30 && brain.linger_count == 12 && brain.special_mode == 8
                     : act.kind == NpcAct::Kind::stand && act.frames == 8);
        brain.linger_count = 1;                             // the last linger, 4 off: back to it
        brain.special_mode = 8; brain.special_x = brain.linger_x = 30; brain.special_y = brain.linger_y = 30; brain.special_tries = 4;
        act = d2d::rules::npc_think(brain, seed, path, 34, 30, 0x9a, 1u << 8, 1);
        assert(act.kind == NpcAct::Kind::walk && brain.linger_count == 0);
        act = d2d::rules::npc_think(brain, seed, path, 30, 30, 0x9a, 1u << 8, 1);   // there: S1 at the anvil
        assert(act.kind == NpcAct::Kind::mode && act.mode == 8 && act.face == 0x38 && brain.special_mode == 0);
        // The visitor (FUN_005e68f0): a player with a "!" 8 off is walked up
        // to, 3 subtiles a think (FUN_005de4e0's split); 2 off it's greeted,
        // then again 60 thinks on; past 16 from home the NPC goes home first.
        assert(d2d::rules::npc_reach(10, 10, 20, 13, 2) == 8);
        assert(d2d::rules::npc_step_toward(10, 10, 20, 13, 8, 3, 2) == std::pair(13, 11));
        d2d::rules::NpcBrain host;
        host.homed = true; host.home_x = 10; host.home_y = 10;
        const auto before = seed.low;
        act = d2d::rules::npc_think(host, seed, path, 10, 10, 0x9a, 0, 1, { .present = true, .x = 20, .y = 13, .distance = 8 });
        assert(act.kind == NpcAct::Kind::walk && act.x == 13 && act.y == 11 && seed.low == before);
        act = d2d::rules::npc_think(host, seed, path, 18, 13, 0x9a, 0, 1, { .present = true, .x = 20, .y = 13, .distance = 2 });
        assert(act.kind == NpcAct::Kind::stand && act.frames == 20 && act.greet && host.greeted == 60);
        act = d2d::rules::npc_think(host, seed, path, 18, 13, 0x9a, 0, 1, { .present = true, .x = 20, .y = 13, .distance = 2 });
        assert(!act.greet && host.greeted == 59);
        act = d2d::rules::npc_think(host, seed, path, 30, 10, 0x9a, 0, 1, { .present = true, .x = 40, .y = 10, .distance = 8 });
        assert(act.kind == NpcAct::Kind::stand && act.frames == 10 && host.linger_x == 10 && host.linger_count == 12);
        // Talked to (+0x14 = 40): 4 thinks 15 frames on, then 8, counting down.
        d2d::rules::NpcBrain talked;
        talked.homed = true; talked.held = 40;
        for (int think = 0; think < 5; ++think) {
            act = d2d::rules::npc_think(talked, seed, path, 10, 10, 0x9a, 0, 1, { .talking = true });
            assert(act.kind == NpcAct::Kind::stand && act.frames == (think < 4 ? 15 : 8));
        }
        assert(talked.held == 35 && seed.low == before);
    }
    std::puts("ok");
}
