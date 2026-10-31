// SPDX-License-Identifier: GPL-3.0-or-later
// Combat rules over hand-made tables: hit chance, the fighter built from
// gear, speed breakpoints, both blows (block, dodge, reductions,
// resistances, crits, crushing blow, leech), experience and levels, the
// merc's stats.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <sound_ids.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
    // the kind-1 cases on stdin (tools/emu/hit_render.py's, which runs game.exe's client step
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
    // A walk's animation rate (FUN_00623f50): RN 0x65, WL / TW 0xd5, x the walk % (at least 25).
    assert(moving_anim_rate(false, 100) == 213 && moving_anim_rate(true, 150) == 151);
    assert(moving_anim_rate(false, 0) == 53 && moving_anim_rate(true, 400) == 404);
    assert(hit_chance(100, 100, 1, 1) == 50);
    assert(hit_chance(1000, 1, 5, 1) == 95 && hit_chance(10, 1000, 1, 1) == 5);
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
    // quarter of what's left, resistances cut, leech per Drain.
    Fighter hit = simple_fighter(100, 100, 1000000);
    hit.crushing = 100; hit.life_steal = 10;
    hit.elem[0] = { 10, 10 };
    Target target{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1, .res = { 50, 0, 100, 0, 0, 0 }, .drain = 50 };
    Rng rng{ 5 };
    auto blow = player_blow(hit, target, 99, rng);
    for (int i = 0; i < 20 && !blow.hit; ++i) blow = player_blow(hit, target, 99, rng);   // 95 % to hit
    assert(blow.hit && blow.crushing && blow.damage == 50 + 0 + 50);  // 100 phys at 50%, fire immune, CB 400/4 at 50%
    assert(blow.life == 50 * 10 * 50 / 10000);
    // Critical strike doubles like deadly strike.
    Fighter crit = simple_fighter(10, 10, 1000000);
    crit.critical = 100;
    Target plain{ .hit_points = 400, .max_hp = 400, .armor_class = 1, .level = 1 };
    auto crushing = player_blow(crit, plain, 99, rng);
    for (int i = 0; i < 20 && !crushing.hit; ++i) crushing = player_blow(crit, plain, 99, rng);
    assert(crushing.hit && crushing.deadly && crushing.damage == 20);
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
    // Monster blows: block, damage reduced % then flat, elemental less resistance.
    MonStats mon;
    mon.level = 99; mon.to_hit = 1000000; mon.a1_min = mon.a1_max = 100;
    mon.elements[0] = { 0, 100, 40, 40, 0, "A1" };                           // fire, always
    Fighter defender;
    defender.dr_pct = 20; defender.dr_flat = 5; defender.res[0] = 75; defender.mdr = 2;
    auto taken = monster_blow(defender, 1, Motion::standing, mon, false, rng);
    for (int i = 0; i < 20 && !taken.hit; ++i) taken = monster_blow(defender, 1, Motion::standing, mon, false, rng);
    assert(taken.hit && taken.damage == 75 + 8);                               // 100 x 80% - 5, 40 x 25% - 2
    assert(taken.physical == 75 && taken.elemental == 8);                      // Bone Armor's part, Cyclone Armor's
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
    assert(blocks > 700 && blocks < 800 && walk_blocks > 700 && walk_blocks < 800 && run_blocks > 200 && run_blocks < 300);
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
    std::puts("ok");
}
