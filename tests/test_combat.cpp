// SPDX-License-Identifier: GPL-3.0-or-later
// Combat rules over hand-made tables: hit chance, the fighter built from
// gear, speed breakpoints, both blows (block, dodge, reductions,
// resistances, crits, crushing blow, leech), experience and levels, the
// merc's stats.
#include <combat.hpp>
#include <d2s_items.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace d2d::rules;

int main() {
    assert(hit_chance(100, 100, 1, 1) == 50);
    assert(hit_chance(1000, 1, 5, 1) == 95 && hit_chance(10, 1000, 1, 1) == 5);
    assert(kill_exp(100, 1, 1) == 100 && kill_exp(100, 10, 1) == 24 && kill_exp(100, 20, 1) == 5);
    assert(kill_exp(100, 1, 10) == 10);
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
    auto taken = monster_blow(defender, 1, false, mon, false, rng);
    for (int i = 0; i < 20 && !taken.hit; ++i) taken = monster_blow(defender, 1, false, mon, false, rng);
    assert(taken.hit && taken.damage == 75 + 8);                               // 100 x 80% - 5, 40 x 25% - 2
    {                                                                  // cold chills, poison runs per tick (FUN_0057b7d0, FUN_0057c1e0)
        MonStats cold_mon = mon;
        cold_mon.elements[0] = { 2, 100, 10, 10, 40, "A1" };
        cold_mon.elements[1] = { 3, 100, 8, 8, 50, "A1" };
        Fighter chilly;
        chilly.res[2] = 50; chilly.res[3] = 25; chilly.plr = 20;
        auto cold_hit = monster_blow(chilly, 1, false, cold_mon, false, rng);
        for (int i = 0; i < 20 && !cold_hit.hit; ++i) cold_hit = monster_blow(chilly, 1, false, cold_mon, false, rng);
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
        dodged += monster_blow(agile, 1, false, mon, false, rng).dodged;
        evaded += monster_blow(agile, 1, true, mon, false, rng).dodged;
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
        damage.etype = 3;
        const auto plain_blow = missile_blow(damage, missile_target, {}, rng);
        assert(plain_blow.damage == 0 && plain_blow.poison == 100 * 25 && plain_blow.poison_ticks == 25);
    }
    Fighter claws;                                                     // Weapon Block: standing only
    claws.weapon_block = 100;
    int wblocked = 0, wmoving = 0;
    for (int i = 0; i < 100; ++i) {
        wblocked += monster_blow(claws, 1, false, mon, false, rng).blocked;
        wmoving += monster_blow(claws, 1, true, mon, false, rng).blocked;
    }
    assert(wblocked > 85 && wmoving == 0);
    agile.def_missile = 1000000000;                                    // vs missiles only
    int spikes = 0;
    for (int i = 0; i < 100; ++i) spikes += monster_blow(agile, 99, false, mon, true, rng).hit;
    assert(spikes < 15);
    defender.block = 75;
    int blocks = 0, moving_blocks = 0;
    for (int i = 0; i < 1000; ++i) { blocks += monster_blow(defender, 1, false, mon, false, rng).blocked; moving_blocks += monster_blow(defender, 1, true, mon, false, rng).blocked; }
    assert(blocks > 700 && blocks < 800 && moving_blocks > 200 && moving_blocks < 300);
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
