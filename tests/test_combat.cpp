// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Combat rules over hand-made tables: hit chance, the fighter built from
// gear, speed breakpoints, both blows (block, dodge, reductions,
// resistances, crits, crushing blow, leech), experience and levels, the
// merc's stats.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <sound_ids.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::rules;

int main(int argc, char** argv) {
    // `--walk-rates`: the walk / run animation rate at every walk % 0..400,
    // as tools/emu/walk_rates.py prints game.exe's (`mode percent rate`).
    if (argc > 1 && std::string_view(argv[1]) == "--walk-rates") {
        for (const int mode : { 2, 3, 6 })
            for (int percent = 0; percent <= 400; ++percent) std::printf("%d %d %d\n", mode, percent, moving_anim_rate(mode == 3, percent));
        return 0;
    }
    // `--get-hit-length`: the ticks a get-hit mode holds for the cases on stdin (`id kind speed
    // frames fhr ...`, as tools/emu/hit_len.py writes and runs game.exe's FUN_00623f50 branch and
    // FUN_005539b0 on): `id rate ticks` a case, kind 0 a player and 1 a monster (the same rule).
    if (argc > 1 && std::string_view(argv[1]) == "--get-hit-length") {
        long case_id, kind, speed, frames, fhr;
        char line[512];
        while (std::fgets(line, sizeof line, stdin)) {
            if (line[0] == '#' || std::sscanf(line, "%ld %ld %ld %ld %ld", &case_id, &kind, &speed, &frames, &fhr) != 5) continue;
            std::printf("%ld %d %d\n", case_id, get_hit_rate(int(speed), int(fhr)), get_hit_ticks(int(frames), int(speed), int(fhr)));
        }
        return 0;
    }
    // `--get-hit-frames`: the frame drawn at each tick of a monster's (or the merc's) get-hit hold for
    // the kind-1 cases on stdin (an uncommitted oracle's, hit_render.py, which ran game.exe's client step
    // FUN_004b1280 and the draw's FUN_00621810 on them): `id rate ticks f0 f1 ...` a case.
    if (argc > 1 && std::string_view(argv[1]) == "--get-hit-frames") {
        long case_id, kind, speed, frames, fhr;
        char line[512];
        while (std::fgets(line, sizeof line, stdin)) {
            if (line[0] == '#' || std::sscanf(line, "%ld %ld %ld %ld %ld", &case_id, &kind, &speed, &frames, &fhr) != 5 || kind != 1) continue;
            const int rate = get_hit_rate(int(speed), int(fhr)), ticks = get_hit_ticks(int(frames), int(speed), int(fhr));
            std::printf("%ld %d %d", case_id, rate, ticks);
            for (int tick = 0; tick < ticks; ++tick) std::printf(" %d", get_hit_frame(int(frames), rate, tick));
            std::printf("\n");
        }
        return 0;
    }
    // `--cast-rates`: the cast speed and the ticks of a plain cast at every FCR 0..400, as
    // tools/emu/cast_rates.py prints game.exe's (`rate FCR SPEED RATE`, `ticks plain FRAMES SPEED FCR TICKS`).
    if (argc > 1 && std::string_view(argv[1]) == "--cast-rates") {
        for (const int speed : { 208, 256 })
            for (int fcr = 0; fcr <= 400; ++fcr) std::printf("rate %d %d %d\n", fcr, speed, cast_speed(speed, fcr));
        for (const auto& [frames, speed] : std::to_array<std::array<int, 2>>({ { 14, 256 }, { 20, 256 }, { 16, 256 }, { 17, 256 }, { 15, 208 } }))
            for (int fcr = 0; fcr <= 400; ++fcr) std::printf("ticks plain %d %d %d %d\n", frames, speed, fcr, cast_ticks(frames, speed, fcr));
        return 0;
    }
    // `--monster-damage CASES`: a frame of up to three sure monster blows on a player with a Bone Armor pool, as
    // Foe::take and Fight::absorb build them (rules::monster_blow, then rules::absorb_blows), for the cases of
    // tools/emu/monster_damage.py: `id kind class pool pct flat n seed_low seed_high {min max} x 3`. One line each,
    // `id life pool seed_low seed_high`: what the life loses and the pool left (256ths), the attacker's seed after.
    // game.exe's: `python monster_damage.py game CASES`. A kind `exact` is a record made without the roll
    // (Corpse Explosion, Diablo's cold: monster_blow's `exact`).
    if (argc > 2 && std::string_view(argv[1]) == "--monster-damage") {
        std::ifstream cases(argv[2]);
        if (!cases) return 2;
        std::string line;
        while (std::getline(cases, line)) {
            std::istringstream fields(line);
            std::string kind, name;
            long case_id = 0, pool = 0, pct = 0, flat = 0, blows = 0;
            unsigned seed_low = 0, seed_high = 0;
            std::array<long, 6> range{};
            fields >> case_id >> kind >> name >> pool >> pct >> flat >> blows >> seed_low >> seed_high;
            for (auto& edge : range) fields >> edge;
            Rng seed;
            seed.low = seed_low;
            seed.high = seed_high;
            Fighter defender;
            defender.dr_pct = int(pct);
            defender.dr_flat = int(flat);
            std::vector<StruckBlow> landed;
            for (long index = 0; index < blows; ++index) {
                MonStats stats;
                stats.a1_min = int(range[std::size_t(2 * index)]);
                stats.a1_max = int(range[std::size_t(2 * index + 1)]);
                const auto taken = monster_blow(defender, 1, Motion::standing, stats, false, seed, false, true, kind == "exact");
                landed.push_back({ taken.damage, 0, false, taken.physical, 0, false, taken.frac, taken.raw, defender.dr_pct, defender.dr_flat });
            }
            int left = int(pool);
            if (left > 0) (void)absorb_blows(landed, left, false);
            long life = 0;
            for (const auto& blow : landed) life += long(blow.damage) * 256 + blow.frac;
            std::printf("%ld %ld %d %u %u\n", case_id, life, left, unsigned(seed.low), unsigned(seed.high));
        }
        return 0;
    }
    // `--get-hit`: FUN_0057cb00's decision for the damage records on stdin (`id kind maxlife damage hc
    // rec64 fire ltng cold poison f2000 mgh stun frozen seedlo seedhi next`, as hit_rule.py (never committed)
    // writes and runs game.exe's FUN_0057cee0 on): one line a frame, `id gh -1`. `next` 1 means the
    // following record lands on the same unit in the same frame (its seed carries on; the frame is a
    // get-hit if any record is, each one rolling, as Fight::hurt_side's blows). The stun, frozen,
    // poison-only and GH-mode tests sit around rules::get_hit in game.exe too (FUN_0057cee0 /
    // FUN_0057cb00's prologue).
    if (argc > 1 && std::string_view(argv[1]) == "--get-hit") {
        long case_id, kind, maxlife, damage, hit_class, rec64, fire, ltng, cold, poison, f2000, mgh, stun, frozen, next;
        unsigned long seed_lo, seed_hi;
        char line[512];
        Rng seed;
        bool carry = false, frame_recovers = false;
        long frame_id = 0;
        while (std::fgets(line, sizeof line, stdin)) {
            if (line[0] == '#' || std::sscanf(line, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %lu %lu %ld", &case_id, &kind, &maxlife, &damage, &hit_class, &rec64,
                                               &fire, &ltng, &cold, &poison, &f2000, &mgh, &stun, &frozen, &seed_lo, &seed_hi, &next) != 17) continue;
            if (!carry) { seed = Rng{}; seed.low = std::uint32_t(seed_lo); seed.high = std::uint32_t(seed_hi); frame_id = case_id; frame_recovers = false; }
            const bool marked = rec64 == 0 && (fire > 0 || ltng > 0 || cold > 0 || poison > 0 || f2000 != 0);
            const bool recovers = stun != 0 || (frozen == 0 && !(poison != 0 && poison == damage) && (kind == 0 || mgh != 0) && get_hit(maxlife, damage, int(hit_class), marked, seed));
            frame_recovers = frame_recovers || recovers;
            carry = next != 0;
            if (!carry) std::printf("%ld %d -1\n", frame_id, frame_recovers ? 1 : 0);
        }
        return 0;
    }
    // A walk's animation rate (FUN_00623f50): RN 0x65, WL / TW 0xd5, x the walk % (at least 25).
    assert(moving_anim_rate(false, 100) == 213 && moving_anim_rate(true, 150) == 151);
    assert(moving_anim_rate(false, 0) == 53 && moving_anim_rate(true, 400) == 404);
    assert(hit_chance(100, 100, 1, 1) == 50);
    assert(hit_chance(1000, 1, 5, 1) == 95 && hit_chance(10, 1000, 1, 1) == 5);
    // FUN_0057cb00: the divisor by hit class (0x57cbf8) and the bands: none under max / d, 37.5 % from
    // it, 75 % from 2 max / d, sure from 4 max / d; an element (marked) divides by 16; under a point none.
    assert(get_hit_divisor(2) == 8 && get_hit_divisor(4) == 32 && get_hit_divisor(5) == 64 && get_hit_divisor(3) == 16 && get_hit_divisor(0) == 16 && get_hit_divisor(64) == 16);
    {
        Rng seed{ 7u };
        int band[4] = {};
        for (int sample = 0; sample < 4000; ++sample) {
            band[0] += get_hit(100 << 8, (100 << 8) / 16 - 1, 3, false, seed);
            band[1] += get_hit(100 << 8, (100 << 8) / 16, 3, false, seed);
            band[2] += get_hit(100 << 8, (100 << 8) / 8, 3, false, seed);
            band[3] += get_hit(100 << 8, (100 << 8) / 4, 3, false, seed);
        }
        assert(band[0] == 0 && band[1] > 1400 && band[1] < 1600 && band[2] > 2900 && band[2] < 3100 && band[3] == 4000);
        assert(!get_hit(100 << 8, 255, 3, false, seed) && !get_hit(100 << 8, (100 << 8) / 8 - 1, 2, false, seed) && get_hit(100 << 8, (100 << 8) / 2, 2, false, seed));
        assert(!get_hit(100 << 8, (100 << 8) / 16 - 1, 2, true, seed) && get_hit(100 << 8, (100 << 8) / 4, 2, true, seed) && get_hit(100 << 8, (100 << 8) / 2, 5, false, seed));
    }
    {   // An armor's pool per record in landing order, each blow losing its own part (state event 0xb in
        // FUN_0057c1e0: Bone Armor FUN_005c6370 the physical, Cyclone Armor FUN_005c87a0 fire / cold / lightning).
        // A sword's 32 then a fire blow's 32 on 64 life: Cyclone Armor takes the fire, the sword's blow stays sure.
        std::vector<StruckBlow> blows{ { 32, 2, false, 32, 0 }, { 32, 0, true, 0, 32 } };
        int pool = 100 * 256;
        Rng seed{ 9u };
        assert(absorb_blows(blows, pool, true) == 32 * 256 && pool == 68 * 256 && blows[0].damage == 32 && blows[1].damage == 0 && !blows[1].marked);
        assert(struck_big(blows, 64 << 8, seed));
        // Bone Armor, the mirror: a fire blow first keeps its 32; the pool empties inside the sword's blow.
        blows = { { 32, 0, true, 0, 32 }, { 32, 2, false, 32, 0 } };
        pool = 20 * 256;
        assert(absorb_blows(blows, pool, false) == 20 * 256 && pool == 0 && blows[0].damage == 32 && blows[1].damage == 12 && blows[1].physical == 12);
        // A fire blow with poison keeps its mark when the fire goes (FUN_0057ce30 reads the poison field too).
        blows = { { 10, 2, true, 4, 6, true } };
        pool = 6 * 256;
        assert(absorb_blows(blows, pool, true) == 6 * 256 && blows[0].damage == 4 && blows[0].marked);
        // Two class-2 blows of 6 on 64 life: each under max / 8, no get-hit (their sum of 12 would roll).
        blows = { { 6, 2, false, 6, 0 }, { 6, 2, false, 6, 0 } };
        for (int sample = 0; sample < 64; ++sample) assert(!struck_big(blows, 64 << 8, seed));
    }
    // FUN_0057e2f0's 256ths (0x6e1668 / 0x6e1694), clvl / mlvl from level 25.
    assert(kill_exp(100, 1, 1) == 100 && kill_exp(100, 10, 1) == 100 * 61 / 256 && kill_exp(100, 20, 1) == 100 * 13 / 256);
    assert(kill_exp(100, 1, 10) == 100 * 38 / 256 && kill_exp(1000, 30, 40) == 750 && kill_exp(100, 1, 6) == 100);
    // game.exe's own (tools/emu/kill_exp.py 400 3): exp clvl mlvl award, MulDiv's shortcuts included.
    for (const auto& [exp, clvl, mlvl, award] : std::to_array<std::array<int, 4>>({ { 1950, 48, 54, 1733 }, { 537, 61, 34, 27 }, { 3007937, 70, 78, 2699410 },
                                                                                   { 1471826, 75, 30, 74737 }, { 2188, 9, 62, 42 }, { 1741519, 3, 49, 34010 }, { 1056068, 20, 28, 379500 } }))
        assert(kill_exp(exp, clvl, mlvl) == award);
    // FUN_0057e480: ExpRatio past 69 (976 at 70, >> 10), item_addexperience %, 1 for none, 0 at the cap.
    const std::vector<int> ratios(100, 1024);
    auto ratio70 = ratios; ratio70[70] = 976;
    assert(kill_award(1000, 70, 70, 0, ratio70, 10, 99) == 1000 * 976 >> 10 && kill_award(1000, 10, 10, 50, ratios, 10, 99) == 1500);
    assert(kill_award(0, 10, 10, 0, ratios, 10, 99) == 1 && kill_award(1000, 99, 99, 0, ratios, 10, 99) == 0);
    assert(scale(0x200000, 3, 0x10) == 0x200000 / 0x10 * 3 && scale(7, 3, 2) == 10 && scale(1, 1, 0) == 0);
    // The fighter: weapon damage with enhanced damage and the strength
    // bonus, attack rating, shield block, the rest straight from the sums.
    Tables tables;
    tables.item_base["hax"] = { .mindam = 3, .maxdam = 6, .str_bonus = 100, .speed = -10 };
    tables.item_base["buc"] = { .block = 25 };
    d2d::d2s::Item hax; hax.code = "hax";
    d2d::d2s::Item buc; buc.code = "buc";
    d2d::d2s::Stats stats;
    stats.values[d2d::d2s::kStr] = 20; stats.values[d2d::d2s::kDex] = 20; stats.values[d2d::d2s::kLevel] = 1;
    StatSum sum{}, wsum{};
    auto fighter = make_fighter(tables, &hax, nullptr, sum, wsum, stats, { .to_hit = 15, .block = 25 }, 50, { 1, 2, 3, 4 });
    assert(fighter.min == 3 && fighter.max == 7 && fighter.attack_rating == 80 && fighter.block == 0 && fighter.wsm == -10 && fighter.defense == 50 && fighter.res[3] == 4);
    // A group's member (FUN_00482680): never one of its last `keep` plays.
    {
        namespace snd = d2d::rules::sound_ids;
        std::array<int, 2> last{};
        int next = 0;
        const auto scripted = [&](int bound) { static constexpr int kDraws[] = { 1, 0, 0, 1, 4, 4, 0, 2 }; return kDraws[next++] % bound; };
        assert(snd::group_member(100, 1, last, scripted) == 100 && next == 0);
        snd::group_played(last, 100);
        assert(snd::group_member(100, 3, last, scripted) == 101 && next == 4);       // keep 2: 100 twice redrawn
        snd::group_played(last, 101);
        assert(last == (std::array{ 101, 100 }));
        assert(snd::group_member(100, 3, last, scripted) == 100 && next == 7);       // rand(4) 0: keep 1, only 101 redrawn
    }
    // The hit class (unit sounds, FUN_004ca560 / the swing table 0x727ed8):
    // the weapon's, else hand to hand; the blow carries it with the skill's.
    {
        namespace snd = d2d::rules::sound_ids;
        assert(fighter.hit_class == 1 && make_fighter(tables, nullptr, nullptr, sum, wsum, stats, {}, 0, {}).hit_class == 1);
        tables.item_base["hax"].hit_class = snd::hit_class("1hsl");
        auto axe = make_fighter(tables, &hax, nullptr, sum, wsum, stats, {}, 0, {});
        assert(axe.hit_class == 3 && snd::kSwing[3].sound == 268 && snd::kSwing[10].sound == 0);
        Rng roll(1);
        Target dummy;
        assert(player_blow(axe, dummy, 1, roll, Swing{ .hit_class = 0x70 }).hit_class == 0x73);   // Bash's 112
        assert(snd::impact(3, false).impact == 341 && snd::impact(3, false).element == 0);         // impact_blade_swing
        assert(snd::impact(0x21, false).impact == 321 && snd::impact(0x21, false).element == 0x164);
        assert(snd::impact(0x36, false).element == 0x16a && snd::impact(0x36, true).element == 0);  // cold: no layer on a death
        assert(snd::impact(0x30, true).element == 0x167 && snd::impact(10, false).impact == 347);
        // A block (FUN_004ca810): block_weapon for 1..9, block_arrow for bow / crossbow, else none.
        assert(snd::block(1) == 398 && snd::block(9) == 398 && snd::block(0x73) == 398);
        assert(snd::block(10) == 401 && snd::block(11) == 401);
        assert(snd::block(0) == 0 && snd::block(12) == 0 && snd::block(13) == 0);
        // Footsteps (FUN_004caf60 / FUN_004caeb0): the tick nearest each half of the cycle; the row by floor and run.
        int steps = 0;
        for (int tick = 0; tick < 16; ++tick) steps += snd::footfall(tick * 256 % 2048, 256, 2048, 2, 0);
        assert(steps == 4 && snd::footfall(0, 256, 2048, 2, 0) && snd::footfall(1024, 256, 2048, 2, 0) && !snd::footfall(512, 256, 2048, 2, 0));
        assert(snd::footstep(2816, 1, false) == 2816 && snd::footstep(2816, 6, true) == 2816 + 20 + 24 && snd::footstep(0, 3, false) == 0);
        assert(snd::floor_material(0x20) == 1 && snd::floor_material(0x400) == 5 && snd::floor_material(0x80) == 6 && snd::floor_material(2) == 0);
        // FUN_004cadb0: the first tile at the cell. A flagless one keeps the level default.
        struct FloorTile { int x, y; unsigned material; };
        const std::vector<std::vector<FloorTile>> sand_then_wood{ { { 1, 2, 0x40 }, { 1, 2, 0x80 } } };
        const std::vector<std::vector<FloorTile>> blank_then_sand{ { { 1, 2, 0 }, { 1, 2, 0x40 } } };
        const std::vector<std::vector<FloorTile>> other_cell{ { { 9, 9, 0x80 } } };
        assert(snd::first_floor_material(sand_then_wood, 1, 2, 3) == 4);
        assert(snd::first_floor_material(blank_then_sand, 1, 2, 3) == 3);
        assert(snd::first_floor_material(other_cell, 1, 2, 3) == 3);
        tables.item_base["hax"].hit_class = 0;
    }
    sum[17] = sum[18] = 100;                                          // enhanced damage off the weapon: no effect (op 13)
    assert(make_fighter(tables, &hax, nullptr, sum, wsum, stats, {}, 0, {}).max == 7);
    sum[17] = sum[18] = 0;
    wsum[17] = wsum[18] = 100;                                        // +100% on the weapon
    sum[20] = 10; sum[136] = 40; sum[36] = 80; sum[34] = 3; sum[60] = 10; sum[54] = 3; sum[55] = 14; sum[56] = 50;
    sum[57] = 256; sum[58] = 512; sum[59] = 75;                         // poison: 1-2 a tick for 75 ticks
    fighter = make_fighter(tables, &hax, &buc, sum, wsum, stats, { .to_hit = 15, .block = 25 }, 50, {});
    assert(fighter.min == 7 && fighter.max == 14);                                // 6-12, +20% from strength
    assert(fighter.block == 75);                                            // 60 x 5 / 2 = 150: capped
    stats.values[d2d::d2s::kLevel] = 10;
    assert(make_fighter(tables, &hax, &buc, sum, wsum, stats, { .block = 25 }, 0, {}).block == 60 * 5 / 20);   // (block) x (dex - 15) / (clvl x 2)
    stats.values[d2d::d2s::kLevel] = 1;
    assert(fighter.crushing == 40 && fighter.dr_pct == 50 && fighter.dr_flat == 3 && fighter.life_steal == 10);   // DR% caps at 50
    assert(fighter.elem[2] == std::pair(3, 14) && fighter.cold_len == 50 && fighter.elem[3] == std::pair(75, 150));
    StatSum s25{};
    s25[25] = 50;                                                     // damagepercent joins the strength bonus: +70%
    fighter = make_fighter(tables, &hax, nullptr, s25, StatSum{}, stats, {}, 0, {});
    assert(fighter.min == 5 && fighter.max == 10);
    s25[25] = 0; s25[111] = 2;                                        // "+2 damage": both ends, before the bonus
    fighter = make_fighter(tables, &hax, nullptr, s25, StatSum{}, stats, {}, 0, {});
    assert(fighter.min == 6 && fighter.max == 9);
    fighter = make_fighter(tables, nullptr, nullptr, StatSum{}, StatSum{}, stats, {}, 0, {});
    assert(fighter.min == 1 && fighter.max == 2);                                 // fists, no strength bonus
    StatSum mst{};                                                    // a mastery: +20 % to hit, +50 % damage
    mst[342] = 20; mst[343] = 50; mst[344] = 7; mst[348] = 30;
    fighter = make_fighter(tables, &hax, nullptr, mst, StatSum{}, stats, {}, 0, {});
    assert(fighter.min == 5 && fighter.max == 10 && fighter.ar_pct == 20 && fighter.mastery_crit == 7 && fighter.weapon_block == 30);
    mst[48] = 10; mst[49] = 20; mst[329] = 50;                        // Fire Mastery on the gear's fire
    assert(make_fighter(tables, &hax, nullptr, mst, StatSum{}, stats, {}, 0, {}).elem[0] == std::pair(15, 30));
    // Hit chance rounds the percent first (FUN_0057d9b0); negative defense helps.
    assert(hit_chance(1, 2, 3, 1) == 49 && hit_chance(10, -10, 1, 1) == 95);

    // Speed breakpoints: 1.10's attack frames.
    assert(effective_speed(20) == 17 && effective_speed(0) == 0 && effective_speed(30, 150) == 25);
    assert(attack_ticks(16, 256, 0, 0) == 16);                        // no IAS: a frame a tick
    assert(attack_ticks(16, 256, 20, 0) == 14);                       // EIAS 17: ceil(4096 / 299)
    assert(attack_ticks(16, 256, 0, 20) == 21);                       // a slow weapon: ceil(4096 / 204)
    // FCR / FHR / FBR: Sorc SC base 14 sweeps published Sorc breakpoints
    // 0/9/20/37/63/105/200 -> 14/13/12/11/10/9/8.
    assert(speed_frames(14, 0) == 14 && speed_frames(14, 9) == 13 && speed_frames(14, 20) == 12);
    assert(speed_frames(14, 37) == 11 && speed_frames(14, 63) == 10);
    assert(speed_frames(14, 105) == 9 && speed_frames(14, 200) == 8);
    assert(speed_frames(0, 105) == 0 && speed_frames(-3, 105) == 0);   // no anim, no shortening
    // The get-hit's rate and the ticks its mode holds (tools/emu/hit_len.py runs FUN_00623f50's
    // branch and FUN_005539b0): the Sorceress's 8 frames at 256 by FHR, a Zombie's 5 at 152.
    assert(get_hit_rate(256, 0) == 128 && get_hit_rate(256, 20) == 171 && get_hit_rate(256, 86) == 256 && get_hit_rate(152, 0) == 76);
    // Negative FHR: signed effective speed, then game.exe's unsigned tail (native hit_len.py cases).
    assert(get_hit_rate(256, -1) == 125 && get_hit_rate(256, -20) == 66 && get_hit_rate(152, -20) == 39);
    assert(get_hit_rate(256, -35) == 2 && get_hit_rate(1, -35) == 0 && get_hit_rate(256, -36) == 0x7fff);
    assert(get_hit_rate(256, -50) == 0x7fff && get_hit_rate(256, -119) == 0x7fff && get_hit_rate(256, -121) == 0x7fff);
    assert(get_hit_rate(256, -200) == 896 && get_hit_rate(256, -240) == 742 && get_hit_rate(256, -1000) == 476);
    assert(get_hit_ticks(8, 256, -1) == 17 && get_hit_ticks(8, 256, -20) == 32 && get_hit_ticks(8, 256, -35) == 1024);
    assert(get_hit_ticks(8, 1, -35) == 1 && get_hit_ticks(8, 256, -36) == 2 && get_hit_ticks(8, 256, -200) == 3);
    assert(get_hit_rate(256, -120) == 0x7fff);                        // d2d's defined fallback for game.exe's divide by zero
    assert(get_hit_ticks(8, 256, 0) == 16 && get_hit_ticks(8, 256, 5) == 15 && get_hit_ticks(8, 256, 9) == 14 && get_hit_ticks(8, 256, 14) == 13);
    assert(get_hit_ticks(8, 256, 20) == 12 && get_hit_ticks(8, 256, 30) == 11 && get_hit_ticks(8, 256, 42) == 10 && get_hit_ticks(8, 256, 60) == 9);
    assert(get_hit_ticks(8, 256, 86) == 8 && get_hit_ticks(8, 256, 142) == 7 && get_hit_ticks(8, 256, 280) == 6 && get_hit_ticks(8, 256, 1000) == 6);
    assert(get_hit_ticks(5, 152, 0) == 17 && get_hit_ticks(7, 256, 0) == 14 && get_hit_ticks(1, 512, 0) == 2 && get_hit_ticks(8, 1, 0) == 1);
    // The drawn frame a tick at a time (FUN_004b1280 / FUN_00621810): a Zombie's 5 frames at 76 over its 17 ticks, once
    // and held; a Sorceress at 0 FHR two ticks a frame; past the hold the last stepped frame stays; a rate of 0 stays on 0.
    {
        const std::array<int, 17> zombie{ 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4 };
        for (int tick = 0; tick < 17; ++tick) assert(get_hit_frame(5, get_hit_rate(152, 0), tick) == zombie[std::size_t(tick)]);
        for (int tick = 0; tick < 16; ++tick) assert(get_hit_frame(8, get_hit_rate(256, 0), tick) == tick / 2);
        assert(get_hit_frame(8, 128, 40) == 7 && get_hit_frame(8, 512, 3) == 6 && get_hit_frame(8, 512, 9) == 6 && get_hit_frame(8, 0, 5) == 0);
    }
    // Casts (game.exe's own code, tools/emu/cast_rates.py): a plain cast is a tick under
    // ceil(256 x frames / speed). Sorc SC, 14 frames: 13 at 0 FCR, then 12 / 11 / 10 / 9 / 8 / 7
    // from 9 / 20 / 37 / 63 / 105 / 200.
    assert(cast_ticks(14, 256, 0) == 13 && cast_ticks(14, 256, 8) == 13 && cast_ticks(14, 256, 9) == 12);
    assert(cast_ticks(14, 256, 20) == 11 && cast_ticks(14, 256, 37) == 10 && cast_ticks(14, 256, 63) == 9);
    assert(cast_ticks(14, 256, 105) == 8 && cast_ticks(14, 256, 200) == 7);
    assert(cast_ticks(15, 208, 0) == 18 && cast_ticks(15, 208, 4) == 17 && cast_ticks(15, 208, 163) == 10);   // Druid SC: AnimData speed 208
    assert(cast_ticks(20, 256, 0) == 19 && cast_ticks(20, 256, 7) == 18);                                      // Amazon SC
    assert(cast_ticks(17, 256, 0) == 16 && cast_ticks(17, 256, 8) == 15 && cast_ticks(17, 256, 174) == 9);   // Assassin SC
    assert(cast_speed(256, 200) == 448 && cast_speed(256, 400) == 448);                                       // the rate stops at 175
    assert(cast_ticks(0, 256, 105) == 0 && cast_ticks(1, 256, 0) == 1);
    assert(open_wounds_per_sec(1) == 3 && open_wounds_per_sec(81) == 227);

    // Player blows: sure hits (AR vastly over defense), crushing blow takes a
    // quarter of what the hit leaves (FUN_005bffc0), resistances cut, leech per Drain.
    Fighter hit = simple_fighter(100, 100, 1000000);
    hit.crushing = 100; hit.life_steal = 10;
    hit.elem[0] = { 10, 10 };
    Target target{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1, .res = { 50, 0, 100, 0, 0, 0 }, .drain = 50 };
    Rng rng{ 5 };
    auto blow = player_blow(hit, target, 99, rng);
    for (int i = 0; i < 20 && !blow.hit; ++i) blow = player_blow(hit, target, 99, rng);   // 95 % to hit
    assert(blow.hit && blow.crushing && blow.damage == 50 + 0 + 44);  // 100 phys at 50%, fire immune, CB (400 - 50)/4 = 87, less 50 %
    target.crush_divisor = 8;                                        // a boss or unique; a missile doubles it
    blow = player_blow(hit, target, 99, rng, Swing{ .missile = true });
    for (int i = 0; i < 20 && !blow.hit; ++i) blow = player_blow(hit, target, 99, rng, Swing{ .missile = true });
    assert(blow.hit && blow.damage == 50 + 11);                       // 350 / 16 = 21, less 50 %
    assert(blow.life == 50 * 10 * 50 / 10000);
    // Critical strike doubles like deadly strike.
    Fighter crit = simple_fighter(10, 10, 1000000);
    crit.critical = 100;
    Target plain{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1 };
    auto crushing = player_blow(crit, plain, 99, rng);
    for (int i = 0; i < 20 && !crushing.hit; ++i) crushing = player_blow(crit, plain, 99, rng);
    assert(crushing.hit && crushing.deadly && crushing.damage == 20);
    // Prevent Monster Heal (stat 117) rides a passed to-hit roll, blocked or
    // not (FUN_0057d810); a missed roll or a Smite (no roll) carries none.
    Fighter no_heal = simple_fighter(10, 10, 1000000);
    no_heal.prevent_heal = true;
    Target blocker{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1, .block = 100 };
    auto stopped = player_blow(no_heal, blocker, 99, rng);
    for (int i = 0; i < 20 && !stopped.blocked; ++i) stopped = player_blow(no_heal, blocker, 99, rng);
    assert(stopped.blocked && !stopped.hit && stopped.prevent_heal);
    Fighter blind = no_heal; blind.ar_base = 1;
    Target armored{ .hit_points = 400, .max_hp = 400, .armor_class = 1000000, .level = 99 };
    auto missed = player_blow(blind, armored, 1, rng);
    for (int i = 0; i < 20 && missed.prevent_heal; ++i) missed = player_blow(blind, armored, 1, rng);   // 5 % still hit
    assert(!missed.hit && !missed.prevent_heal);
    assert(!player_blow(no_heal, plain, 99, rng, Swing{ .smite = true }).prevent_heal);
    // The gear's stun length (stat 66) stuns when the skill gives none, at the SrcDam share
    // (FUN_0057b7d0: [0x11] += stat 66 x share >> 7 when 0); the skill's own wins.
    Fighter stunner = simple_fighter(10, 10, 1000000);
    stunner.stun_len = 40;
    const auto landed = [&](const Swing& swing) {
        auto stun_blow = player_blow(stunner, plain, 99, rng, swing);
        for (int i = 0; i < 20 && !stun_blow.hit; ++i) stun_blow = player_blow(stunner, plain, 99, rng, swing);
        assert(stun_blow.hit);
        return stun_blow.stun_ticks;
    };
    assert(landed({}) == 40 && landed({ .srcdam = 64 }) == 20 && landed({ .stun_ticks = 7 }) == 7);
    // A skill's swing: its enhanced damage joins the gear's %, its flat
    // damage comes after (and isn't doubled), SrcDam scales the weapon part.
    Fighter sure_hit = simple_fighter(100, 100, 1000000);
    sure_hit.phys_pct = 20;
    auto swing = [&](const Fighter& attacker, const Swing& swing_opts) {
        auto x = player_blow(attacker, plain, 99, rng, swing_opts);
        for (int i = 0; i < 20 && !x.hit; ++i) x = player_blow(attacker, plain, 99, rng, swing_opts);
        assert(x.hit);
        return x.damage;
    };
    assert(swing(sure_hit, {}) == 120);
    assert(swing(sure_hit, { .ed_pct = 50 }) == 170);                     // (20 + 50)%
    assert(swing(sure_hit, { .ed_pct = 50, .flat = 7 }) == 177);
    assert(swing(sure_hit, { .srcdam = 64 }) == 60);
    assert(swing(sure_hit, { .ed_pct = -500 }) == 10);                    // the % floors at -90
    Fighter critter = sure_hit;
    critter.critical = 100;
    assert(swing(critter, { .flat = 5 }) == 245);                   // doubled before the flat add
    // A kick: the boots' damage with its own %, the skill's damage with the
    // skill's %; no doubling.
    Fighter kicker = critter;
    kicker.kick_lo = kicker.kick_hi = 10; kicker.kick_pct = 100;
    assert(swing(kicker, { .ed_pct = 50, .kick = true, .skill_lo = 4 << 8, .skill_hi = 4 << 8 }) == 10 * 250 / 100 + 4 * 150 / 100);
    auto knocked = player_blow(kicker, plain, 99, rng, { .knockback = true });
    for (int i = 0; i < 20 && !knocked.hit; ++i) knocked = player_blow(kicker, plain, 99, rng, { .knockback = true });
    assert(knocked.hit && knocked.knockback);
    // Conversion: calc4 % of the physical to the element, less its
    // resistance; calc2's flat add stays physical. A stun's length, capped.
    Target mres{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1, .res = { 0, 50, 0, 0, 0, 0 } };
    auto conv = [&](const Swing& swing_opts) {
        auto x = player_blow(sure_hit, mres, 99, rng, swing_opts);
        for (int i = 0; i < 20 && !x.hit; ++i) x = player_blow(sure_hit, mres, 99, rng, swing_opts);
        assert(x.hit);
        return x;
    };
    assert(conv({ .conv_type = 4, .conv_pct = 25 }).damage == 90 + 30 / 2);          // 120: 90 physical, 30 magic at 50 %
    assert(conv({ .flat = 5, .conv_type = 4, .conv_pct = 100 }).damage == 5 + 60);    // all magic, the flat add physical
    assert(conv({ .stun_ticks = 300 }).stun_ticks == 250);
    // FUN_0057b7d0: poison takes an eighth a tick over at least 50 ticks
    // (120 x 256 / 8 x 50 / 256 = 750); cold chills at least 50.
    const auto poisoned = conv({ .conv_type = 3, .conv_pct = 100 });
    assert(poisoned.damage == 0 && poisoned.poison == 750 && poisoned.poison_ticks == 50);
    assert(conv({ .conv_type = 2, .conv_pct = 100 }).chill_ticks == 50);
    // Smite: the shield's damage with its %, sure to hit (defense 10^6),
    // no crit, no gear elements or leech; the stun.
    Fighter smiter = critter;
    smiter.smite_lo = smiter.smite_hi = 10; smiter.smite_pct = 20; smiter.life_steal = 50; smiter.elem[0] = { 30, 30 };
    const Target wall{ .hit_points = 400, .max_hp = 400, .armor_class = 1000000, .level = 99 };
    const auto smite = player_blow(smiter, wall, 1, rng, { .ed_pct = 30, .stun_ticks = 20, .smite = true });
    assert(smite.hit && !smite.deadly && smite.damage == 15 && smite.life == 0 && smite.stun_ticks == 20);
    // A block does not stop Smite: FUN_005ce9f0 sets the hit bit again after the
    // block check (0x5cebb0), so it still deals its damage and stun and keeps the
    // knockback, and the blow stays marked blocked. Every other swing it stops.
    Target smite_blocker = wall;
    smite_blocker.block = 100;
    const auto blocked_smite = player_blow(smiter, smite_blocker, 1, rng, { .ed_pct = 30, .knockback = true, .stun_ticks = 20, .smite = true });
    assert(blocked_smite.blocked && blocked_smite.hit && blocked_smite.damage == 15 && blocked_smite.stun_ticks == 20 && blocked_smite.knockback);
    Fighter swinger = simple_fighter(10, 10, 1000000);
    const Target swing_blocker{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1, .block = 100 };
    for (const auto& other : { Swing{}, Swing{ .ed_pct = 30, .stun_ticks = 20 }, Swing{ .srcdam = 128, .kick = true } }) {
        int stopped_swings = 0;                                       // the to-hit roll (95 % at most) comes first
        for (int i = 0; i < 40; ++i) {
            const auto stopped_swing = player_blow(swinger, swing_blocker, 99, rng, other);
            assert(!stopped_swing.hit && stopped_swing.damage == 0);
            stopped_swings += stopped_swing.blocked;
        }
        assert(stopped_swings >= 30);
    }
    // Block chance 0 never marks a Smite blocked; at 100 it always does; neither changes the damage.
    for (int chance : { 0, 100 }) {
        Target defender = wall;
        defender.block = chance;
        const auto through = player_blow(smiter, defender, 1, rng, { .smite = true });
        assert(through.hit && through.blocked == (chance == 100) && through.damage == 12);
    }
    // With Holy Shield up its damage joins the shield's before the %.
    const auto holy_shield = player_blow(smiter, wall, 1, rng, { .ed_pct = 30, .skill_lo = 4 << 8, .skill_hi = 4 << 8, .smite = true });
    assert(holy_shield.hit && holy_shield.damage == (10 + 4) * 150 / 100);
    // Vengeance: elements as % of the physical rolled (before crit), each
    // less its resistance; the cold chills.
    Target vres{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1, .res = { 0, 0, 50, 0, 0, 0 } };   // fire 50 %
    auto vs_resists = player_blow(critter, vres, 99, rng, { .fire_pct = 100, .cold_pct = 50, .ltng_pct = 10, .cold_len = 40 });
    for (int i = 0; i < 20 && !vs_resists.hit; ++i) vs_resists = player_blow(critter, vres, 99, rng, { .fire_pct = 100, .cold_pct = 50, .ltng_pct = 10, .cold_len = 40 });
    assert(vs_resists.hit && vs_resists.damage == 240 + 60 + 60 + 12 && vs_resists.chill_ticks == 40);   // crit 240 phys; 120 x (100 % at 50, 50 %, 10 %)
    // Boots make the kick: their kick damage, StrBonus on strength, + stat 137.
    tables.item_base["lbt"] = { .mindam = 3, .maxdam = 8, .str_bonus = 120 };
    d2d::d2s::Item lbt; lbt.code = "lbt";
    StatSum kick_sum{};
    kick_sum[137] = 2;
    const auto kicker_fighter = make_fighter(tables, &hax, nullptr, kick_sum, StatSum{}, stats, {}, 0, {}, &lbt);
    assert(kicker_fighter.kick_lo == 5 && kicker_fighter.kick_hi == 10 && kicker_fighter.kick_pct == 20 * 120 / 100);
    target.block = 100;
    int blocked = 0;
    for (int i = 0; i < 20; ++i) blocked += player_blow(hit, target, 99, rng).blocked;
    assert(blocked >= 15);                                            // every blow that connects
    {   // The physical keeps its 256ths through the resistance (FUN_0057bf80 on the record's fine value; the life stat takes it as is, 0057c852).
        const auto land_blow = [](const Fighter& attacker, const Target& defender, Rng& dice, Swing with = {}) {
            auto struck = player_blow(attacker, defender, 99, dice, with);
            for (int i = 0; i < 40 && !struck.hit; ++i) struck = player_blow(attacker, defender, 99, dice, with);   // 95 % to hit
            assert(struck.hit);
            return struck;
        };
        Rng fine_rng{ 21 };
        Fighter fine = simple_fighter(1, 1, 1000000);
        fine.phys_lo = fine.phys_hi = 300;                           // 1.17 points
        Target exposed{ .hit_points = 1000, .max_hp = 1000, .armor_class = 1, .level = 1 };
        auto fine_blow = land_blow(fine, exposed, fine_rng);
        assert(fine_blow.phys == 1 && fine_blow.damage == 1 && fine_blow.frac == 44);
        exposed.res[0] = 50;
        fine_blow = land_blow(fine, exposed, fine_rng);
        assert(fine_blow.phys == 1 && fine_blow.damage == 0 && fine_blow.frac == 150);   // half a point is not lost, not rounded up
        exposed.res[0] = 100;
        fine_blow = land_blow(fine, exposed, fine_rng);
        assert(fine_blow.damage == 0 && fine_blow.frac == 0);
        exposed.res[0] = -50;
        fine_blow = land_blow(fine, exposed, fine_rng);
        assert(fine_blow.damage == 1 && fine_blow.frac == 194);        // 450 / 256ths
        Fighter whole = simple_fighter(10, 10, 1000000);              // whole points: the rest of a resistance still lands in `frac`
        exposed.res[0] = 33;
        fine_blow = land_blow(whole, exposed, fine_rng);
        assert(fine_blow.phys == 10 && fine_blow.damage == 6 && fine_blow.frac == 179);   // 2560 x 67 / 100 = 1715 = 6 x 256 + 179
        exposed.res[0] = 0;
        assert(land_blow(whole, exposed, fine_rng).damage == 10 && land_blow(whole, exposed, fine_rng).frac == 0);
        Fighter sliver = simple_fighter(1, 1, 1000000);                // no one-point floor: a tenth of a point stays a tenth
        sliver.phys_lo = sliver.phys_hi = 256;
        fine_blow = land_blow(sliver, exposed, fine_rng, Swing{ .srcdam = 32 });   // a quarter of the weapon (Blade Shield's share)
        assert(fine_blow.phys == 0 && fine_blow.damage == 0 && fine_blow.frac == 64);
        fine_blow = land_blow(whole, exposed, fine_rng, Swing{ .flat = 3 });       // a skill's added damage is whole points
        assert(fine_blow.phys == 13 && fine_blow.damage == 13 && fine_blow.frac == 0);
        // A skill missile with a weapon share (Fire Arrow style: player_blow with srcdam, then missile_blow on top): the
        // share keeps its 256ths too, and the element adds its own; frac may pass 255 and the life stat takes it whole.
        auto share = land_blow(sliver, exposed, fine_rng, Swing{ .srcdam = 64 });   // half the weapon: 128 / 256ths
        assert(share.phys == 0 && share.damage == 0 && share.frac == 128);
        share = missile_blow(MissileDamage{ .etype = 0, .elo = 200, .ehi = 201 }, exposed, {}, fine_rng, share);   // 200 / 256ths of fire
        assert(share.damage == 0 && share.frac == 328);
    }

    {   // Against an undead monster a Blunt weapon adds 50 % and stat 122 its own to the enhanced damage, only
        // when that sum is over 0 (FUN_0057b660, called from FUN_0057b7d0 for a player attacker).
        const auto land_blow = [](const Fighter& attacker, const Target& defender, Rng& dice, Swing with = {}) {
            auto struck = player_blow(attacker, defender, 99, dice, with);
            for (int i = 0; i < 40 && !struck.hit; ++i) struck = player_blow(attacker, defender, 99, dice, with);
            assert(struck.hit);
            return struck;
        };
        Rng undead_rng{ 33 };
        Fighter mace = simple_fighter(10, 10, 1000000);
        Target living{ .hit_points = 1000, .max_hp = 1000, .armor_class = 1, .level = 1 };
        Target skeleton = living;
        skeleton.undead = true;
        assert(land_blow(mace, skeleton, undead_rng).damage == 10);                         // not Blunt, no stat: nothing
        mace.blunt = true;
        assert(land_blow(mace, living, undead_rng).damage == 10 && land_blow(mace, skeleton, undead_rng).damage == 15);
        assert(land_blow(mace, skeleton, undead_rng, Swing{ .ed_pct = 50 }).damage == 20);  // it joins the skill's enhanced damage
        mace.undead_pct = 30;
        assert(land_blow(mace, skeleton, undead_rng).damage == 18 && land_blow(mace, living, undead_rng).damage == 10);
        mace.blunt = false;
        assert(land_blow(mace, skeleton, undead_rng).damage == 13);                         // stat 122 alone
        mace.undead_pct = -60;
        assert(land_blow(mace, skeleton, undead_rng).damage == 10);                         // under 0: not added
        mace.blunt = true;
        assert(land_blow(mace, skeleton, undead_rng).damage == 10);                         // 50 - 60 is under 0 too
        mace.undead_pct = -20;
        assert(land_blow(mace, skeleton, undead_rng).damage == 13);                         // 50 - 20
        Fighter foot = simple_fighter(10, 10, 1000000);
        foot.blunt = true;
        assert(land_blow(foot, skeleton, undead_rng, Swing{ .kick = true, .skill_lo = 10 << 8, .skill_hi = 10 << 8 }).damage == 10);   // a kick is not a weapon blow
        // A skill missile's weapon share (player_blow with srcdam, as Fight::strike builds it) takes the same bonus against an
        // undead: 1024 / 256ths is 4 points, +50 % (Blunt) +30 % (stat 122) is 7.
        Fighter bow = simple_fighter(1, 1, 1000000);
        bow.phys_lo = bow.phys_hi = 1024;
        bow.blunt = true; bow.undead_pct = 30;
        assert(land_blow(bow, skeleton, undead_rng, Swing{ .srcdam = 128 }).damage == 7);
        assert(land_blow(bow, living, undead_rng, Swing{ .srcdam = 128 }).damage == 4);
        // make_fighter: the weapon's Blunt type through the Equiv chain (ItemTypes mace -> blun), either of its types; stat 122 from the sums.
        Tables types_tables;
        types_tables.types["weap"] = {};
        types_tables.types["blun"] = { .equiv = { "weap", "" } };
        types_tables.types["mace"] = { .equiv = { "blun", "" } };
        types_tables.types["swor"] = { .equiv = { "weap", "" } };
        types_tables.item_base["mac"] = { .mindam = 3, .maxdam = 6 };
        types_tables.item_info["mac"].type = "mace";
        types_tables.item_base["ssd"] = { .mindam = 3, .maxdam = 6 };
        types_tables.item_info["ssd"].type = "swor";
        types_tables.item_base["bld"] = { .mindam = 3, .maxdam = 6, .type2 = "mace" };
        types_tables.item_info["bld"].type = "swor";
        d2d::d2s::Item weapon_mace, weapon_sword, weapon_both;
        weapon_mace.code = "mac"; weapon_sword.code = "ssd"; weapon_both.code = "bld";
        d2d::d2s::Stats owner;
        owner.values[d2d::d2s::kStr] = 20; owner.values[d2d::d2s::kDex] = 20; owner.values[d2d::d2s::kLevel] = 1;
        StatSum gear{}, weapon_gear{};
        gear[d2d::d2s::kUndeadDamagePercent] = 25;
        auto built = make_fighter(types_tables, &weapon_mace, nullptr, gear, weapon_gear, owner, {}, 0, {});
        assert(built.blunt && built.undead_pct == 25);
        assert(!make_fighter(types_tables, &weapon_sword, nullptr, gear, weapon_gear, owner, {}, 0, {}).blunt);
        assert(make_fighter(types_tables, &weapon_both, nullptr, gear, weapon_gear, owner, {}, 0, {}).blunt);
        built = make_fighter(types_tables, nullptr, nullptr, gear, weapon_gear, owner, {}, 0, {});
        assert(!built.blunt && built.undead_pct == 25);                                   // barehanded: stat 122 still counts
    }
    // Monster blows: block, damage reduced flat then % (FUN_0057bf80), elemental less resistance.
    MonStats mon;
    mon.level = 99; mon.to_hit = 1000000; mon.a1_min = mon.a1_max = 100;
    mon.elements[0] = { 0, 100, 40, 40, 0, "A1" };                           // fire, always
    Fighter defender;
    defender.dr_pct = 20; defender.dr_flat = 5; defender.res[0] = 75; defender.mdr = 2;
    auto taken = monster_blow(defender, 1, Motion::standing, mon, false, rng);
    for (int i = 0; i < 20 && !taken.hit; ++i) taken = monster_blow(defender, 1, Motion::standing, mon, false, rng);
    assert(taken.hit && taken.damage * 256 + taken.frac == reduce_fine(taken.raw, 20, 5) + (40 - 2) * 256 / 4);   // (100.x - 5) x 80%, (40 - 2) x 25% (FUN_0057bf80: the flat first)
    assert(taken.physical == 76 && taken.elemental == 9 && taken.damage >= 85 && taken.damage <= 86);   // Bone Armor's part, Cyclone Armor's
    {                                                                  // the physical roll (FUN_0057b420): min << 8 + rand((max - min) << 8), the top never rolled
        MonStats fallen;
        fallen.a1_min = 1; fallen.a1_max = 2;
        const Fighter bare;
        long long total = 0;
        int lowest = 1 << 20, highest = 0;
        for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
            Rng seeded{ seed }, rolled{ seed };
            seeded.next();
            const auto sure = monster_blow(bare, 1, Motion::standing, fallen, false, rolled, false, true);
            const int fine = sure.damage * 256 + sure.frac;
            assert(fine == 256 + int(seeded.low & 255) && sure.physical == sure.damage && sure.frac >= 0 && sure.frac < 256);
            assert(rolled.low == seeded.low && rolled.high == seeded.high);        // one draw on the attacker's seed
            total += fine; lowest = std::min(lowest, fine); highest = std::max(highest, fine);
        }
        assert(lowest >= 256 && highest <= 511);                                   // never the max, 2.0 hp
        assert(std::abs(double(total) / 2000 / 256 - 1.5) < 0.05);
        MonStats flat;                                                             // an empty or inverted range is min .. min + 1 and still draws; min 0 is 1
        for (const auto& bounds : { std::pair{ 3, 3 }, std::pair{ 5, 4 }, std::pair{ 0, 0 } }) {
            flat.a1_min = bounds.first; flat.a1_max = bounds.second;
            Rng flat_seed{ 77 }, flat_rolled{ 77 };
            flat_seed.next();
            const auto sure = monster_blow(bare, 1, Motion::standing, flat, false, flat_rolled, false, true);
            assert(sure.damage * 256 + sure.frac == std::max(bounds.first, 1) * 256 + int(flat_seed.low & 255) && flat_rolled.low == flat_seed.low);
        }
        Fighter armoured;                                                          // the reduction takes the 256ths: 1 hp flat off 1.x
        armoured.dr_flat = 1;
        Rng flat_dr_seed{ 9 }, flat_dr_rolled{ 9 };
        flat_dr_seed.next();
        const auto reduced = monster_blow(armoured, 1, Motion::standing, fallen, false, flat_dr_rolled, false, true);
        assert(reduced.damage == 0 && reduced.frac == int(flat_dr_seed.low & 255));
        armoured.dr_flat = 0; armoured.dr_pct = 50;
        Rng pct_seed{ 9 }, pct_rolled{ 9 };
        pct_seed.next();
        const auto halved = monster_blow(armoured, 1, Motion::standing, fallen, false, pct_rolled, false, true);
        assert(halved.damage * 256 + halved.frac == (256 + int(pct_seed.low & 255)) * 50 / 100);
        MonStats ten;                                                              // flat first, then the percent: 10.x hp, 3 flat, 20 %
        ten.a1_min = 10; ten.a1_max = 11;
        armoured.dr_flat = 3; armoured.dr_pct = 20;
        Rng order_seed{ 5 }, order_rolled{ 5 };
        order_seed.next();
        const auto ordered = monster_blow(armoured, 1, Motion::standing, ten, false, order_rolled, false, true);
        assert(ordered.damage * 256 + ordered.frac == (2560 + int(order_seed.low & 255) - 3 * 256) * 80 / 100);
        assert(reduce_fine(2560, 20, 3) == 1433 && reduce_fine(2560, 0, 0) == 2560 && reduce_fine(0, 20, 3) == 0);
        assert(reduce_fine(200, 0, 1) == 0 && reduce_fine(200, 50, 1) == 0 && reduce_fine(300, 50, 1) == 22);       // the flat takes it all: nothing left for the percent
        assert(reduce_fine(1000, -50, 0) == 1500 && reduce_fine(1000, 150, 0) == 0);   // a negative percent adds, 100 caps
    }
    {                                                                  // FUN_0057bf80 on a monster's record: the flat reduction off a dr_field, THEN the resistance; a dr_field left under 0 is kept and summed
        const auto dr_blow = [](const Fighter& victim, int physical, std::initializer_list<std::pair<int, int>> elements) {
            MonStats attacker;
            attacker.level = 1; attacker.a1_min = attacker.a1_max = physical;
            for (auto& element : attacker.elements) element.type = -1;
            std::size_t slot = 0;
            for (const auto& [type, roll] : elements) attacker.elements[slot++] = { type, 100, roll, roll, 25, "A1" };
            Rng dice{ 1 };
            return monster_blow(victim, 1, Motion::standing, attacker, false, dice, false, true, true);
        };
        Fighter dr_field;
        dr_field.res[0] = 50; dr_field.mdr = 10;
        auto dr_taken = dr_blow(dr_field, 0, { { 0, 100 } });                     // game.exe, run natively: 100 fire, 50 %, mdr 10 deals 45, not 40
        assert(dr_taken.damage == 45 && dr_taken.frac == 0 && dr_taken.elemental == 45);
        dr_field.res[0] = 75;
        dr_taken = dr_blow(dr_field, 0, { { 0, 100 } });                          // (100 - 10) x 25 % = 22.5
        assert(dr_taken.damage == 22 && dr_taken.frac == 128);
        dr_field.res[0] = -100;
        dr_taken = dr_blow(dr_field, 0, { { 0, 100 } });                          // (100 - 10) x 2
        assert(dr_taken.damage == 180 && dr_taken.frac == 0);
        dr_field.res[0] = 0; dr_field.res[2] = 0; dr_field.mdr = 6;               // fire 5 and cold 100, mdr 6: fire is -1 and takes it off the cold 94
        dr_taken = dr_blow(dr_field, 0, { { 0, 5 }, { 2, 100 } });
        assert(dr_taken.damage == 93 && dr_taken.frac == 0 && dr_taken.elemental == 94 && dr_taken.element_fine[0] == -256 && dr_taken.sum == 93 * 256);
        dr_field.mdr = 0;                                                   // zero-flat control: nothing goes negative
        dr_taken = dr_blow(dr_field, 0, { { 0, 5 }, { 2, 100 } });
        assert(dr_taken.damage == 105 && dr_taken.element_fine[0] == 5 * 256);
        dr_field.mdr = 6;                                                   // all fields under the reduction: the total is clamped once, at 0
        dr_taken = dr_blow(dr_field, 0, { { 0, 5 }, { 2, 2 } });
        assert(dr_taken.damage == 0 && dr_taken.frac == 0 && dr_taken.sum == -5 * 256);
        Fighter dr_armoured;                                                // physical 10 under flat 15 is -5 and takes it off fire 50: 45, not 50
        dr_armoured.dr_flat = 15;
        dr_taken = dr_blow(dr_armoured, 10, { { 0, 50 } });
        assert(dr_taken.damage == 45 && dr_taken.physical == 0 && dr_taken.elemental == 50);
        dr_armoured.dr_flat = 0;
        dr_taken = dr_blow(dr_armoured, 10, { { 0, 50 } });
        assert(dr_taken.damage == 60);
    }
    {                                                                  // Bone Armor takes its pool from the fine physical: the rest keeps its 256ths
        std::vector<StruckBlow> blows{ { 5, 2, false, 5, 0, false, 100 } };        // 5.39 hp
        int pool = 2 * 256;
        assert(absorb_blows(blows, pool, false) == 512 && pool == 0);
        assert(blows[0].damage == 3 && blows[0].physical == 3 && blows[0].frac == 100);
        blows = { { 5, 2, false, 5, 0, false, 100 } };
        pool = 2 * 256 + 128;                                                      // a 2.5 hp pool: the remainder is 2.89
        assert(absorb_blows(blows, pool, false) == 640 && pool == 0 && blows[0].damage == 2 && blows[0].frac == 228);
        blows = { { 5, 2, false, 5, 0, false, 100 } };
        pool = 10 * 256;                                                           // a pool past the field takes the fraction too
        assert(absorb_blows(blows, pool, false) == 1380 && pool == 10 * 256 - 1380 && blows[0].damage == 0 && blows[0].frac == 0);
        blows = { { 1, 2, false, 0, 1, false, 77 } };                              // Cyclone Armor leaves the physical fraction alone
        pool = 3 * 256;
        assert(absorb_blows(blows, pool, true) == 256 && pool == 2 * 256 && blows[0].damage == 0 && blows[0].frac == 77);
    }
    {                                                                  // absorb first, then the reduction (state event 0xb at 0x57c2fc, the loop at 0x57c378)
        MonStats ten;
        ten.a1_min = 10; ten.a1_max = 11;
        Fighter armoured;
        armoured.dr_flat = 2; armoured.dr_pct = 50;
        Rng seed{ 5 };
        const auto blow_hit = monster_blow(armoured, 1, Motion::standing, ten, false, seed, false, true);
        assert(blow_hit.raw >= 2560 && blow_hit.raw < 2816 && blow_hit.damage * 256 + blow_hit.frac == reduce_fine(blow_hit.raw, 50, 2));
        const auto blow_of = [&] { return StruckBlow{ blow_hit.damage, 0, false, blow_hit.physical, 0, false, blow_hit.frac, blow_hit.raw, 50, 2 }; };
        std::vector<StruckBlow> blows{ blow_of() };
        int pool = 3 * 256;                                            // 3 hp of pool off the 10.x, then 2 flat, then half
        assert(absorb_blows(blows, pool, false) == 3 * 256 && pool == 0);
        const int game_order = reduce_fine(blow_hit.raw - 3 * 256, 50, 2);
        assert(blows[0].damage * 256 + blows[0].frac == game_order && blows[0].physical == blows[0].damage);
        assert(game_order != reduce_fine(blow_hit.raw, 50, 2) - 3 * 256 && game_order > reduce_fine(blow_hit.raw, 50, 2) - 3 * 256);   // d2d's old order took 3 hp off the halved hit
        blows = { blow_of() };
        pool = 100 * 256;                                              // a pool past the field: nothing left, whatever the reduction
        assert(absorb_blows(blows, pool, false) == blow_hit.raw && pool == 100 * 256 - blow_hit.raw && blows[0].damage == 0 && blows[0].frac == 0);
        blows = { blow_of() };
        pool = 2 * 256;                                                // a pool under the flat: the flat still takes its 2 hp off the rest
        assert(absorb_blows(blows, pool, false) == 512 && blows[0].damage * 256 + blows[0].frac == reduce_fine(blow_hit.raw - 512, 50, 2));
    }
    {                                                                  // a fixed record (Corpse Explosion FUN_005c4d10, Diablo's cold FUN_005cce80): not FUN_0057b420's roll
        const Fighter bare;
        MonStats corpse;
        for (const int x : { 0, 1, 7, 40 }) {
            corpse.a1_min = corpse.a1_max = x;
            Rng seed{ 31 }, drawn{ 31 };
            const auto exact = monster_blow(bare, 1, Motion::standing, corpse, false, drawn, false, true, true);
            assert(exact.hit && exact.damage == x && exact.frac == 0 && exact.physical == x && exact.raw == x * 256);   // X..X is X, no 256ths
            assert(drawn.low == seed.low && drawn.high == seed.high);                                                   // and no draw
        }
        corpse.a1_min = corpse.a1_max = 10;                            // the reduction and the absorbs still apply (57c6c0 calls 57c1e0)
        Fighter armoured;
        armoured.dr_flat = 2; armoured.dr_pct = 50;
        Rng seed{ 31 };
        const auto reduced = monster_blow(armoured, 1, Motion::standing, corpse, false, seed, false, true, true);
        assert(reduced.damage == 4 && reduced.frac == 0);              // (10 - 2) x 50 %
        MonStats rolled = corpse;                                      // the same row through the roll is not exact: it draws and clamps
        Rng again{ 31 }, base{ 31 };
        base.next();
        (void)monster_blow(bare, 1, Motion::standing, rolled, false, again, false, true);
        assert(again.low == base.low);
        MonStats cold;                                                 // Diablo's cold touch: 0..0 physical, the element on top
        Rng cold_seed{ 17 }, cold_drawn{ 17 };
        const auto touch = monster_blow(bare, 1, Motion::standing, cold, false, cold_drawn, false, true, true);
        assert(touch.hit && touch.damage == 0 && touch.physical == 0 && touch.frac == 0 && cold_drawn.low == cold_seed.low);
    }
    {                                                                  // a sure hit (DiabRun, DiabCold, a monster's Charge): no FUN_0057ec10
        Fighter guard = defender;
        guard.block = 100; guard.dodge = 100; guard.defense = 1000000;
        MonStats feeble = mon;
        feeble.to_hit = 1;
        for (int i = 0; i < 20; ++i) {
            assert(!monster_blow(guard, 99, Motion::standing, feeble, false, rng).hit);
            assert(std::abs(monster_blow(guard, 99, Motion::standing, feeble, false, rng, false, true).damage - 85) <= 1);   // 76.x + (40 - 2) x 25 % = 9.5 (the flat first)
        }
        // A ToHit 0 missile (FUN_005adf10): no to-hit roll, but block and avoid still come (FUN_005ad730).
        MonStats shot = feeble;
        shot.no_hit_roll = true;
        shot.a2_min = shot.a2_max = 100; shot.elements = {};
        Fighter open = defender;
        open.defense = 1000000;
        for (int i = 0; i < 20; ++i) assert(monster_blow(open, 99, Motion::standing, shot, true, rng, true).hit);
        Fighter shield = open;
        shield.block = 100;
        assert(monster_blow(shield, 99, Motion::standing, shot, true, rng, true).blocked);
        shot.no_hit_roll = false;
        int connected = 0;
        for (int i = 0; i < 200; ++i) connected += monster_blow(open, 99, Motion::standing, shot, true, rng, true).hit;
        assert(connected < 30);                                           // a ToHit row rolls: about 5 %

    }
    {                                                                  // cold chills, poison runs per tick (FUN_0057b7d0, FUN_0057c1e0)
        MonStats cold_mon = mon;
        cold_mon.elements[0] = { 2, 100, 10, 10, 40, "A1" };
        cold_mon.elements[1] = { 3, 100, 8, 8, 50, "A1" };
        Fighter chilly;
        chilly.res[2] = 50; chilly.res[3] = 25; chilly.plr = 20;
        MonStats poisoner = mon;                         // poison hit's own stats: every hit, length as set
        poisoner.poison_min = poisoner.poison_max = 40; poisoner.poison_len = 300;
        auto poison_taken = monster_blow(defender, 1, Motion::standing, poisoner, false, rng);
        for (int i = 0; i < 20 && !poison_taken.hit; ++i) poison_taken = monster_blow(defender, 1, Motion::standing, poisoner, false, rng);
        assert(poison_taken.hit && poison_taken.poison == 40 && poison_taken.poison_ticks == 300);
        auto cold_hit = monster_blow(chilly, 1, Motion::standing, cold_mon, false, rng);
        for (int i = 0; i < 20 && !cold_hit.hit; ++i) cold_hit = monster_blow(chilly, 1, Motion::standing, cold_mon, false, rng);
        assert(cold_hit.hit && cold_hit.damage == 100 + 5 && cold_hit.chill_ticks == 20);   // 40 less 50 % cold
        assert(cold_hit.poison == 60 && cold_hit.poison_ticks == 80);                     // 80 / 256 a tick less 25 %, 100 ticks less 20 %
        chilly.half_freeze = true;
        assert(chill_length(chilly, 40) == 10);
        chilly.cannot_freeze = true;
        assert(chill_length(chilly, 40) == 0);
        assert(attack_ticks(16, 256, 0, 0, -50) == attack_ticks(16, 256, 0, 50));       // chill's -50 attackrate
    }
    // Dodge a swing standing, avoid a missile, evade on the move.
    Fighter agile;
    agile.dodge = 100;
    int dodged = 0, evaded = 0;
    for (int i = 0; i < 100; ++i) {
        dodged += monster_blow(agile, 1, Motion::standing, mon, false, rng).dodged;
        evaded += monster_blow(agile, 1, Motion::walking, mon, false, rng).dodged;
    }
    assert(dodged > 85 && evaded == 0);
    {                                                                  // a skill's missile (FUN_0064b860's record)
        Target missile_target;
        missile_target.res = { 0, 0, 50, 100, 20, 0 };
        MissileDamage damage{ .etype = 0, .elo = 100 << 8, .ehi = 100 << 8, .elen = 25 };
        assert(missile_blow(damage, missile_target, { 30, 0, 0, 0 }, rng).damage == 80);      // fire 50 % less 30 pierce
        damage.etype = 1;
        assert(missile_blow(damage, missile_target, { 0, 90, 0, 0 }, rng).damage == 0);       // immune: pierce doesn't reach
        damage.etype = 2;
        const auto cold_kill = missile_blow(damage, missile_target, { 0, 0, 500, 0 }, rng);
        assert(cold_kill.damage == 200 && cold_kill.chill_ticks == 50);                    // -100 % at the least, the length too
        // A Charged Bolt of 2-4 (FUN_005a89a0 rolls in 256ths and life takes them as is): the mean is 3.0, not 2.5.
        MissileDamage bolt{ .etype = 1, .elo = 2 << 8, .ehi = 4 << 8, .elen = 25 };
        Rng bolt_rng{ 7 };                                          // its own, so the seeded checks below keep their draws
        std::int64_t dealt = 0;
        constexpr int kBolts = 20000;
        for (int i = 0; i < kBolts; ++i) {
            const auto bolt_blow = missile_blow(bolt, Target{}, {}, bolt_rng);
            assert(bolt_blow.frac >= 0 && bolt_blow.frac < 256);
            dealt += std::int64_t(bolt_blow.damage) * 256 + bolt_blow.frac;
        }
        assert(std::abs(double(dealt) / kBolts / 256 - 3.0) < 0.03);
        // The top 256th is never rolled (FUN_005a8910: min + rand(max - min)), and nothing without a min over 0.
        const MissileDamage edge{ .etype = 1, .elo = 256, .ehi = 257, .elen = 25 };
        Rng edge_rng{ 11 };
        for (int i = 0; i < 1000; ++i) assert(missile_blow(edge, Target{}, {}, edge_rng).frac == 0);
        assert(missile_blow(MissileDamage{ .etype = 1, .elo = 0, .ehi = 512 }, Target{}, {}, edge_rng).damage == 0);
        {   // The physical rolls in 256ths too (FUN_005a89a0) and keeps its part under a point through the resistance (FUN_0057bf80).
            Rng phys_rng{ 13 };
            const MissileDamage phys{ .phys_lo = 300, .phys_hi = 300 };
            Target half;
            half.res[0] = 50;
            auto phys_hit = missile_blow(phys, Target{}, {}, phys_rng);
            assert(phys_hit.phys == 1 && phys_hit.damage == 1 && phys_hit.frac == 44);
            phys_hit = missile_blow(phys, half, {}, phys_rng);
            assert(phys_hit.damage == 0 && phys_hit.frac == 150);
            half.res[0] = 100;
            phys_hit = missile_blow(phys, half, {}, phys_rng);
            assert(phys_hit.damage == 0 && phys_hit.frac == 0);
            half.res[0] = -150;   // floored at -100: x2, not x2.5
            phys_hit = missile_blow(phys, half, {}, phys_rng);
            assert(phys_hit.damage == 2 && phys_hit.frac == 88);
        }
        {   // A burst rolls once (FUN_005a9a70 -> FUN_005a8c70): every blow on its record takes the first one's rolls, each resisting its own.
            Rng burst_rng{ 17 };
            const MissileDamage burst{ .phys_lo = 256, .phys_hi = 5000, .etype = 0, .elo = 1000, .ehi = 5000 };
            Target fireproof;
            fireproof.res[2] = 50;                                  // fire is Target::res[2]
            int apart = 0;
            for (int i = 0; i < 200; ++i) {
                MissileRoll roll;
                assert(roll.phys < 0 && roll.element < 0);
                const auto first = missile_blow(burst, Target{}, {}, burst_rng, { .hit = true }, &roll);
                assert(roll.phys >= 256 && roll.phys <= 5000 && roll.element >= 1000 && roll.element < 5000);
                const auto again = missile_blow(burst, Target{}, {}, burst_rng, { .hit = true }, &roll);
                const auto halved = missile_blow(burst, fireproof, {}, burst_rng, { .hit = true }, &roll);
                assert(first.damage == again.damage && first.frac == again.frac && first.phys == again.phys);
                assert(halved.phys == first.phys && halved.damage < first.damage);
                const auto own = missile_blow(burst, Target{}, {}, burst_rng);
                apart += own.damage != first.damage || own.frac != first.frac;
            }
            assert(apart > 150);                                    // with no shared record each blow draws its own
        }
        {   // MulDiv (FUN_00483360), cases run on game.exe in tools/emu: each branch, the wraps.
            constexpr std::array<std::array<int, 4>, 11> kCases{ { { 25600, 40, 100, 10240 }, { 1280000, 37, 100, 473600 }, { 1048577, 3, 7, 449388 },
                { 8388607, 99, 3, 276823998 }, { 1000, 65537, 9, 7281000 }, { 70000, 131072, 3, -1236667296 }, { 70000, 131072, 36864, 248888 },
                { 1048576, 65536, 1, 0 }, { 5, 5, 0, 0 }, { -512, 30, 100, -153 }, { 2097152, 50, 196608, 533 } } };
            for (const auto& [value, times, over, want] : kCases) assert(mul_div(value, times, over) == want);
        }
        // The resistance applies to the fine roll (FUN_0057bf80, MulDiv FUN_00483360).
        assert(resisted_fine(767, 33) == 513 && resisted_fine(767, 0) == 767 && resisted_fine(767, 100) == 0 && resisted_fine(767, -100) == 1534);
        assert(resisted_fine(0x100001, 50) == 524250 && resisted_fine(0x100000, 50) == 524288);   // over 0x100000 the quotient comes first
        assert(resisted_fine(0, 50) == 0 && resisted_fine(-256, -100) == 0 && resisted_fine(0x7fffffff, -100) == 0);   // nothing at or under 0 (LAB_0057bf94); a 32-bit wrap deals nothing
        damage.etype = 3;
        const auto plain_blow = missile_blow(damage, missile_target, {}, rng);
        assert(plain_blow.damage == 0 && plain_blow.poison == 100 * 25 && plain_blow.poison_ticks == 25);
        {   // The get-hit's element mark: game.exe's FUN_0057ce30 reads the record's fire / lightning / cold / poison field
            // after FUN_0057bf80 resisted it in place (256ths, over 0; magic is not in its table). Run on game.exe in tools/emu.
            Target half_resisted;
            half_resisted.res = { 0, 0, 50, 100, 0, 0 };
            Rng mark_rng{ 3 };
            const auto marked = [&](int etype, int fine, const Target& foe) {
                const MissileDamage record{ .etype = etype, .elo = fine, .ehi = fine, .elen = 25 };
                return missile_blow(record, foe, {}, mark_rng).elemental;
            };
            assert(marked(0, 128, Target{}) && marked(0, 1, Target{}) && marked(0, 256, Target{}) && marked(0, 0x10000, Target{}));   // under a point still marks
            assert(!marked(0, 0, Target{}) && !marked(4, 256, Target{}));                       // an empty record, magic
            assert(!marked(1, 256, half_resisted) && !marked(0, 1, half_resisted) && marked(0, 2, half_resisted));   // resisted to nothing, to 1/256 (fine 1 * 50 / 100 = 0)
            assert(marked(3, 1, Target{}) && !marked(3, 0, Target{}));                           // poison: its per-tick field, though 1 * 25 >> 8 is 0 points
            assert(missile_blow(MissileDamage{ .etype = 0, .elo = 128, .ehi = 128 }, Target{}, {}, mark_rng).damage == 0);   // the half point is kept in frac, not dropped
        }
    }
    Fighter claws;                                                     // Weapon Block: standing only
    claws.weapon_block = 100;
    int wblocked = 0, wmoving = 0;
    for (int i = 0; i < 100; ++i) {
        wblocked += monster_blow(claws, 1, Motion::standing, mon, false, rng).blocked;
        wmoving += monster_blow(claws, 1, Motion::walking, mon, false, rng).blocked;
    }
    assert(wblocked > 85 && wmoving == 0);
    agile.def_missile = 1000000000;                                    // vs missiles only
    int spikes = 0;
    for (int i = 0; i < 100; ++i) spikes += monster_blow(agile, 99, Motion::running, mon, false, rng, true).hit;   // a missile rolls even at a runner
    assert(spikes < 15);
    defender.block = 75;
    int blocks = 0, walk_blocks = 0, run_blocks = 0;                  // block a third only running (FUN_0057dfb0)
    for (int i = 0; i < 1000; ++i) {
        blocks += monster_blow(defender, 1, Motion::standing, mon, false, rng).blocked;
        walk_blocks += monster_blow(defender, 1, Motion::walking, mon, false, rng).blocked;
        run_blocks += monster_blow(defender, 1, Motion::running, mon, false, rng).blocked;
    }
    assert(blocks > 650 && blocks < 800 && walk_blocks > 650 && walk_blocks < 800 && run_blocks > 200 && run_blocks < 300);
    {                                                                  // a swing at a runner always lands (FUN_0057ec10)
        Fighter fortress;
        fortress.defense = 1000000000;
        MonStats weak = mon;
        weak.to_hit = 1;
        int standing_hits = 0, running_hits = 0;
        for (int i = 0; i < 100; ++i) {
            standing_hits += monster_blow(fortress, 99, Motion::standing, weak, false, rng).hit;
            running_hits += monster_blow(fortress, 99, Motion::running, weak, false, rng).hit;
        }
        assert(standing_hits < 15 && running_hits == 100);
    }
    ClassGains gains{ .life_per_level = 8, .stamina_per_level = 4, .mana_per_level = 6, .stat_per_level = 5 };
    stats.values[d2d::d2s::kMaxLife] = stats.values[d2d::d2s::kLife] = 50 << 8;
    const std::vector<std::int64_t> next{ 0, 500, 1500, 3750 };
    assert(gain_exp(stats, 400, next, gains) == 0 && stats.get(d2d::d2s::kLevel) == 1);
    assert(gain_exp(stats, 1200, next, gains) == 2);                          // 1600: past 500 and 1500
    assert(stats.get(d2d::d2s::kLevel) == 3 && stats.get(d2d::d2s::kStatPts) == 10 && stats.get(d2d::d2s::kSkillPts) == 2);
    assert(stats.fixed(d2d::d2s::kMaxLife) == 54 && stats.fixed(d2d::d2s::kLife) == 54);   // 2 levels x 8 quarters
    stats.values[d2d::d2s::kLife] = 1 << 8; stats.values[d2d::d2s::kMana] = 0;
    assert(gain_exp(stats, 3000, next, gains) == 1 && stats.fixed(d2d::d2s::kLife) == 56 && stats.get(d2d::d2s::kMana) == stats.get(d2d::d2s::kMaxMana));   // a level fills them
    // Stamina: a run frame costs RunDrain x 2, x (armor speed / 10 + 1),
    // less the slower-drain percent, at least 1 (FUN_0057f240).
    assert(stamina_drain(20, 0, 0) == 40 && stamina_drain(20, 10, 0) == 80 && stamina_drain(20, 20, 0) == 120);
    assert(stamina_drain(20, 0, 25) == 30 && stamina_drain(20, 0, 100) == 1 && stamina_drain(20, 0, 150) == 1);
    // Regen (FUN_00580500): max >> 8 standing, >> 9 walking (none below
    // 1.0 walking), none running unless the bonus is 1000+; plus bonus %.
    const std::int64_t stamina_max = 20 << 8;
    assert(stamina_regen(1000, stamina_max, 1, 0) == 1020 && stamina_regen(1000, stamina_max, 5, 0) == 1020);
    assert(stamina_regen(1000, stamina_max, 2, 0) == 1010 && stamina_regen(100, stamina_max, 2, 0) == 100 && stamina_regen(100, stamina_max, 6, 0) == 110);
    assert(stamina_regen(1000, stamina_max, 3, 0) == 1000 && stamina_regen(1000, stamina_max, 3, 1000) == 1220);
    assert(stamina_regen(1000, stamina_max, 1, 50) == 1030 && stamina_regen(stamina_max - 5, stamina_max, 1, 0) == stamina_max);
    // The merc: level from experience, stats from its band.
    Tables merc_tables;
    merc_tables.hirelings = { { .version = 0, .id = 1, .level = 1, .exp_per_level = 50, .hit_points = 999 },        // classic: not LoD's
                     { .version = 100, .id = 1, .level = 3, .exp_per_level = 100, .hit_points = 100, .hp_per_level = 10, .def = 10, .def_per_level = 2,
                       .dmg_min = 2, .dmg_max = 5, .dmg_per_level = 8, .attack_rating = 20, .ar_per_level = 5, .resist = 10, .resist_per_level = 6 },
                     { .version = 100, .id = 1, .level = 20, .exp_per_level = 100, .hit_points = 500, .dmg_min = 10, .dmg_max = 20 } };
    auto merc = merc_stats(merc_tables, 1, 5 * 100 * 4 * 4);                    // level 4: 1600; level 5 needs 3000
    assert(merc.level == 4 && merc.life == 110 && merc.def == 12 && merc.dmg_min == 3 && merc.dmg_max == 6 && merc.attack_rating == 25);
    assert(merc.resist == 10 + 6 / 4);
    merc = merc_stats(merc_tables, 1, 21u * 100 * 20 * 20);                      // level 20: the second band
    assert(merc.level == 20 && merc.life == 500 && merc.dmg_min == 10);
    // Melee cold resisted under a point still chills (FUN_0057b7d0 sets the length on the rolled cold).
    {
        Fighter cold_hand;
        cold_hand.ar_base = 100000;
        cold_hand.elem[2] = { 1, 1 };
        cold_hand.cold_len = 50;
        Target resists;
        resists.res[4] = 75;
        Rng hit_rng{ 3 };
        const auto chilled = player_blow(cold_hand, resists, 1, hit_rng);
        assert(chilled.hit && chilled.chill_ticks == 12);   // 50 less 75 %, no cold point left
    }
    std::puts("ok");
}
