// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Single MonAI thinks step by step: each one's seed steps, and the
// rolls of QuillRat, the Corrupt Rogues, Baal's taunts, Griswold, Fallen,
// Blood Raven, the traps, the Countess, town visitors and more.
#include <monsters.hpp>
#include <rules.hpp>
#include <town_npcs.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::rules;

int main() {
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
        // CorruptArcher (by its second target's distance): close, a blocked
        // run-off falls through to the shot rolls.
        think_in = { .aip = { 60, 100, 14, 100, 20, 0, 0, 12 }, .dist = 4, .state = &state };
        int archer_pace = 0;
        think_in.pace = &archer_pace; think_in.second = true; think_in.second_dist = 4; roll = Rng{ 3 };
        assert(mon_think("CorruptArcher", think_in, roll, no_away).act == MonAct::a1 && steps(3, roll) == 2);
        think_in.second_dist = 15; roll = Rng{ 3 };
        const auto closer = mon_think("CorruptArcher", think_in, roll, no_away);
        assert(closer.act == MonAct::approach && closer.n == 12 && closer.unit == ThinkUnit::second && steps(3, roll) == 1);
        think_in.second_dist = 30; think_in.aip[0] = 0; roll = Rng{ 3 };
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
        // BaalTaunt (FUN_005ef710; tools/emu/baal_taunt.py --dump): game.exe's runs.
        {
            static constexpr std::string_view kTaunt[] = {
#include "baal_taunt_cases.inc"
            };
            for (const std::string_view taunt_case : kTaunt) {
                std::istringstream fields{ std::string(taunt_case) };
                int kind = 0, mode = 0, count = 0, dist = 0, aip1 = 0, aip2 = 0, aip3 = 0, want_n = 0, want_count = 0;
                std::string bar, want;
                fields >> kind >> mode >> count >> dist >> aip1 >> aip2 >> aip3 >> bar >> want >> want_n >> want_count;
                ThinkIn taunt_in{ .aip = { aip1, aip2, aip3 }, .dist = dist, .state = &count };
                taunt_in.target_still = kind < 2 && mode == 1;
                const auto got = mon_think("BaalTaunt", taunt_in, roll, no_away);
                const bool same = want == "skill" ? got.act == MonAct::skill && got.n == 0 && want_n == 284   // Skill1, Baal Taunt
                                : want == "warp" ? got.act == MonAct::warp : want == "direct" ? got.act == MonAct::direct
                                : got.act == MonAct::idle && got.n == want_n;
                assert(same && count == want_count);
            }
        }
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
        state = 1; think_in.dist = 10; think_in.second = true; think_in.second_dist = 10; roll = Rng{ 5 };
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
        // (one more step) or stands 5; past 9 the close-in is rolled twice
        // (the second target's distance, then the target's).
        think_in = { .aip = { 100, 9, 30, 5, 0, 18, 20, 5 }, .dist = 4, .state = &state };
        think_in.pace = &pace; think_in.second = true; think_in.second_dist = 4; roll = Rng{ 6 };
        assert(mon_think("SkeletonMage", think_in, roll, no_away).act == MonAct::a1 && steps(6, roll) == 2);
        think_in.second_dist = 30; think_in.aip[2] = 100; roll = Rng{ 6 };
        const auto mage_in = mon_think("SkeletonMage", think_in, roll, no_away);
        assert(mage_in.act == MonAct::approach && mage_in.n == 9 && steps(6, roll) == 1);
        think_in.second_dist = 20; think_in.aip[2] = 0; think_in.aip[6] = 100; roll = Rng{ 6 };
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
        // The desert turret's fan (FUN_005cd6a0; tools/emu/turret_fan.py): x, y,
        // the point, calc1 | each missile's start and offset.
        static constexpr std::string_view kFans[] = {
#include "turret_fan_cases.inc"
        };
        for (const std::string_view fan_line : kFans) {
            const std::string line(fan_line);
            std::istringstream fields(line.substr(0, line.find('|')));
            int from_x = 0, from_y = 0, to_x = 0, to_y = 0, count = 0;
            fields >> from_x >> from_y >> to_x >> to_y >> count;
            std::vector<std::array<int, 4>> want;
            std::istringstream shots(line.substr(line.find('|') + 1));
            for (std::string shot; std::getline(shots, shot, ';');) {
                std::istringstream four(shot);
                std::array<int, 4> value{};
                if (four >> value[0] >> value[1] >> value[2] >> value[3]) want.push_back(value);
            }
            assert(d2d::rules::turret_fan(from_x, from_y, to_x, to_y, count) == want);
        }
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
    {   // The palace guard (MonAI JarJar, FUN_005e7590): home, back to it, aside 3 north.
        using d2d::rules::NpcAct;
        d2d::rules::NpcBrain brain;
        Rng seed{ 9 };
        auto act = d2d::rules::jarjar_think(brain, 50, 50, 1, false, false, {});
        assert(act.kind == NpcAct::Kind::stand && act.frames == 20 && brain.home_x == 50);
        act = d2d::rules::jarjar_think(brain, 53, 50, 2, false, false, {});              // the first 200 frames: stand
        assert(act.kind == NpcAct::Kind::stand && act.frames == 20);
        act = d2d::rules::jarjar_think(brain, 53, 50, 250, false, false, {});            // 3 off: home, the frame kept
        assert(act.kind == NpcAct::Kind::walk && act.x == 50 && act.y == 50 && brain.guard_frame == 250);
        act = d2d::rules::jarjar_think(brain, 50, 50, 300, false, false, {});            // within 200 frames: stand
        assert(act.kind == NpcAct::Kind::stand && act.frames == 20);
        act = d2d::rules::jarjar_think(brain, 50, 50, 400, true, true, {});              // aside: 3 north of home
        assert(act.kind == NpcAct::Kind::walk && act.x == 50 && act.y == 47);
        act = d2d::rules::jarjar_think(brain, 50, 47, 401, true, true, {});              // there: stand 120
        assert(act.kind == NpcAct::Kind::stand && act.frames == 120);
        // game.exe's own runs (tools/emu/jarjar.py --dump): the scene | kind
        // (0 stand, 1 walk, 2 the heed's), frames, x, y, a stand after the
        // heed, the home and last frame after (the seed's never drawn).
        static constexpr std::string_view kCases[] = {
#include "jarjar_cases.inc"
        };
        int checked = 0;
        for (const std::string_view case_line : kCases) {
            const std::string line(case_line);
            std::istringstream fields(line.substr(0, line.find('|'))), tail(line.substr(line.find('|') + 1));
            int homed = 0, home_x = 0, home_y = 0, x = 0, y = 0, frame = 0, last = 0, aside = 0, stood = 0, present = 0, reach = 0;
            fields >> homed >> home_x >> home_y >> x >> y >> frame >> last >> aside >> stood >> present >> reach;
            int kind = 0, frames = 0, to_x = 0, to_y = 0, heed_stand = 0, after_x = 0, after_y = 0, after_last = 0;
            tail >> kind >> frames >> to_x >> to_y >> heed_stand >> after_x >> after_y >> after_last;
            d2d::rules::NpcBrain guard;
            guard.homed = homed != 0;
            if (homed) { guard.home_x = home_x; guard.home_y = home_y; }
            guard.guard_frame = last;
            const d2d::rules::NpcVisitor visitor{ .present = present != 0 && reach < 16, .x = x + 6, .y = y, .distance = reach };   // as ai.cpp: within 16
            auto peek = guard;
            const auto heeded = d2d::rules::npc_heed(peek, x, y, visitor);
            const auto got = d2d::rules::jarjar_think(guard, x, y, frame, aside != 0, stood != 0, visitor);
            if (kind == 2 && !heed_stand) {                                              // the heed's own act
                assert(heeded && got.kind == heeded->kind && got.frames == heeded->frames && got.x == heeded->x && got.y == heeded->y);
            } else {
                assert(int(got.kind) == (kind == 1 ? 1 : 0) && got.frames == frames);
                if (kind == 1) assert(got.x == to_x && got.y == to_y);
            }
            assert(guard.home_x == after_x && guard.home_y == after_y && guard.guard_frame == after_last);
            ++checked;
        }
        assert(checked > 30);
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
