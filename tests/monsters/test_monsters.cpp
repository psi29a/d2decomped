// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Monster spawning and stats over hand-made tables: the region takes
// every listed type once, rooms fill by density, Fallen come as a leader
// with a party, nothing lands on a blocked subtile or by the entrance.
// The AIs are in test_monster_ai and test_monster_thinks.
#include <missiles.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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
    // FUN_00641cb0's shapes by missile size / unit size: the farthest offset
    // each strikes, and the nearest it misses. A blank Size strikes none.
    assert(missile_touches(0, 0, 0, 0, 1, 1) && !missile_touches(1, 0, 0, 0, 1, 1));
    assert(missile_touches(1, 0, 0, 0, 1, 2) && !missile_touches(1, 1, 0, 0, 2, 1));
    assert(missile_touches(1, 1, 0, 0, 3, 1) && missile_touches(1, 1, 0, 0, 1, 3) && !missile_touches(2, 0, 0, 0, 1, 3));
    assert(missile_touches(2, 0, 0, 0, 2, 2) && missile_touches(1, 1, 0, 0, 2, 2) && !missile_touches(2, 1, 0, 0, 2, 2));
    assert(missile_touches(2, 1, 0, 0, 3, 2) && missile_touches(1, 2, 0, 0, 2, 3) && !missile_touches(2, 2, 0, 0, 3, 2));
    assert(missile_touches(2, 2, 0, 0, 3, 3) && !missile_touches(3, 0, 0, 0, 3, 3) && !missile_touches(0, 0, 0, 0, 1, 0));
    // A wall: the step that reaches it ends the flight at the centre of the
    // last free point (FUN_00650150), not of the step's end.
    auto flight = MissileFlight::launch(100, 100, 120, 100, missile_velocity(24, 0, 1), 24 << 8, 0);
    assert(flight.velocity == 4608 && flight.aim_x == 4096 && flight.aim_y == 0);
    int frames = 1;
    while (flight.step([](int x, int) { return x >= 104; })) ++frames;
    assert(frames == 4 && flight.x == (103u << 16) + 0x8000 && flight.y == (100u << 16) + 0x8000);
    assert(missile_velocity(10, 8, 4) == (14 << 8) * 75 / 100 && missile_range(40, 5, 3) == 55);
}

// Diablo's target score against game.exe's own (FUN_005e8530, tools/emu/diablo_score.py).
void diablo_scores() {
    static constexpr std::string_view kCases[] = {
#include "diablo_score_cases.inc"
    };
    for (const auto line : kCases) {
        std::istringstream fields{ std::string(line) };
        d2d::rules::DiabloPick pick;
        int damage = 0, state = 0, life = 0, reach = 0, line_clear = 0, bonus = 0, want = 0;
        fields >> pick.damage_resist >> pick.magic_resist >> pick.fire_resist >> pick.light_resist >> pick.cold_resist >> damage
               >> pick.left_skill >> pick.left_level >> pick.left_rank >> pick.right_skill >> pick.right_level >> pick.right_rank
               >> state >> life >> reach >> line_clear >> bonus >> want;
        pick.max_damage = damage; pick.state_b = state != 0;
        assert(d2d::rules::diablo_score(pick, life, reach != 0, line_clear != 0, bonus != 0) == want);
    }
}

// The max damage a swing leaves for Diablo's score (FUN_005a4f50): that mode's, plus its elements.
void swing_max_damages() {
    d2d::rules::MonStats stats;
    stats.a1_max = 10; stats.a2_max = 20; stats.s1_max = 30;
    stats.elements[0] = { 0, 50, 1, 7, 0, "A1" };     // fire on A1
    stats.elements[1] = { 3, 100, 2, 64, 25, "A2" };  // poison on A2: x 10 >> 8
    assert(d2d::rules::swing_max_damage(stats, "") == 0);
    assert(d2d::rules::swing_max_damage(stats, "A1") == 17);
    assert(d2d::rules::swing_max_damage(stats, "A2") == 20 + 64 * 10 / 256);
    assert(d2d::rules::swing_max_damage(stats, "S1") == 30);
    assert(d2d::rules::swing_max_damage(stats, "S2") == 10);
}

// MonProp slots (FUN_00573cb0's end): in order, a chance slot stepping the
// unit seed first, the item property path's min..max draw on it too.
static void monster_prop_rolls() {
    Tables tables;
    tables.properties["thorns"] = { { .func = 1, .stat = 78 } };
    tables.properties["swing2"] = { { .func = 1, .stat = 93 } };
    MonType quill;
    quill.props[2] = { { "thorns", "", 30, 40, 0 }, { "swing2", "", 20, 20, 50 } };
    for (std::uint32_t start = 1; start < 40; ++start) {
        Rng seed(start), expect(start);
        const auto props = monster_props(tables, quill, 2, seed);
        const int thorns = expect.range(30, 40);
        const bool swings = int(expect.next() % 100) < 50;
        assert(props.thorns == thorns && props.ias == (swings ? 20 : 0) && seed.low == expect.low);
    }
    Rng seed(5);
    assert(monster_props(tables, quill, 0, seed).thorns == 0 && seed.low == 5);   // none in normal
}

int main() {
    missile_flights();
    monster_prop_rolls();
    diablo_scores();
    swing_max_damages();
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

    // Room population: objgroups.cpp's port, proven by tools/emu/sweep.sh monsters ($DIFFICULTY 0..2).

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
        // a plain monster isn't; the ubers have their own (0x2c3, 0x2c0).
        const auto andariel = act_boss(0x9c, 0x9c), blood_raven = act_boss(0x10b, 0x10b);
        assert(andariel && andariel->mods == std::vector<int>{ 22 } && !andariel->half_freeze);
        assert(blood_raven && (blood_raven->mods == std::vector<int>{ 12, 22 }) && blood_raven->half_freeze);
        assert(act_boss(0x192, 0x192) && act_boss(0x192, 0x192)->mods == std::vector<int>{ 22 });
        assert(act_boss(0x11c, 0x11c) && (act_boss(0x11c, 0x11c)->mods == std::vector<int>{ 23, 22 }));   // the Maggot Queen: poison hit
        assert(act_boss(0x2c3, 0x9c) && (act_boss(0x2c3, 0x9c)->mods == std::vector<int>{ 23, 6, 29 }));   // uber Andariel (FUN_005b1cf0)
        assert(act_boss(0xf2, 0xf2) && act_boss(0x2c0, 0xf2) && (act_boss(0x2c0, 0xf2)->mods == std::vector<int>{ 22, 30, 17, 8, 6 }) && !act_boss(0xd3, 0xd3));   // Mephisto, uber; Duriel none
        assert(!act_boss(5, 5) && bar_name_colour(Boss::unique, "andariel") == kNameGold);
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
    std::puts("ok");
}
