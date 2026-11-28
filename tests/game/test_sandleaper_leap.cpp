// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A Sand Leaper's Leap (do 77, seq_leaperleap: event 1 on rows 1, 9 and 11): it takes off on row 1 (FUN_005d9f70),
// standing till then, and lands on row 9 (FUN_005da120). On it, with the unit it leapt at still there, FUN_005da020
// takes the sequence to row 12 (rows 10 and 11 passed by) and keeps the length it has left, its whole rows and one
// more (+0x48 = (+0x48 & ~0xff) + 0x100): at row 9 of 20, 11 left, so the mode ends 12 rows on; with that unit gone
// the sequence runs on as it was. The real monster_update on made-up rows.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData data;
    data.monsters.types.resize(0x4f);
    auto& type = data.monsters.types[0x4e];
    type.id = "sandleaper1"; type.base = 0x4e; type.run = 40;
    type.skill[0] = "Leap"; type.sk_mode[0] = "seq_leaperleap"; type.sk_lvl[0] = 1;
    data.mon_npc.resize(0x4f);
    constexpr int kLeap = 142;
    data.skills.rows.resize(std::size_t(kLeap) + 1);
    auto& skill = data.skills.rows[std::size_t(kLeap)];
    skill.id = kLeap;
    skill.srvstfunc = d2d::rules::ServerStartFunction::kLeap;
    skill.srvdofunc = d2d::rules::ServerDoFunction::kLeap;
    data.skills.by_name["Leap"] = kLeap;
    auto& rows = data.mon_seqs["seq_leaperleap"];
    for (int row = 0; row < 20; ++row) rows.push_back({ "A1", row, row == 1 || row == 9 || row == 11 ? 1 : row == 7 ? 2 : 0 });
    Monster leaper;
    leaper.npc.root = "monsters"; leaper.npc.code = "SL"; leaper.npc.base_w = "HTH";
    std::string key = "monsters/SL/A1/HTH";
    for (const auto& component : leaper.npc.comp) key += "/" + component;
    data.npc_timings.emplace(key, GameData::AnimTiming{ .name = "SLA1HTH", .speed = 256, .frames = 20, .action = 5 });
    leaper.type = 0x4e;
    leaper.hit_points = leaper.stats.hit_points = 100;
    constexpr std::uint32_t start = 4000, tick_ms = 40;
    Level level;
    const Crowd crowd;
    d2d::rules::Rng rng{ 7 };
    std::vector<Missile> missiles;
    // A short hop: down before row 9 comes.
    const auto run = [&](bool target_there) {
        Monster hop = leaper;
        set_mode(data, hop, "A1", start);
        hop.skill = kLeap;
        hop.unit.x = hop.leap_from_x = 10.1f; hop.unit.y = hop.leap_from_y = 10.1f;
        hop.leap_to_x = 11.1f; hop.leap_to_y = 10.1f;
        hop.leap_pace = leap_cells_per_frame(40);
        hop.leaping = true;
        hop.leap_takeoff = start + 1 * tick_ms;
        hop.leap_until = start + 9 * tick_ms;
        hop.mode_until = start + 20 * tick_ms;
        hop.leap_target = 77;
        std::vector<Foe> foes(1);
        foes.front().x = 40; foes.front().y = 40; foes.front().id = 77; foes.front().alive = target_there;
        float before_takeoff = -1;
        std::uint32_t until_landed = 0;
        for (std::uint32_t tick = 0; tick <= 9; ++tick) {
            monster_update(data, level, hop, foes, rng, start + tick * tick_ms, 0.04f, crowd, missiles);
            if (tick == 1) before_takeoff = hop.unit.x;
            if (!hop.leaping && until_landed == 0) until_landed = hop.mode_until;
        }
        assert(before_takeoff == 10.1f);                                         // standing till its takeoff row
        assert(!hop.leaping && hop.unit.x == hop.leap_to_x);
        return until_landed;
    };
    const auto jumped = run(true), ran_on = run(false);
    std::printf("Sand Leaper's landing: ends %u ms on with its target there, %u without\n", jumped - start, ran_on - start);
    assert(jumped == start + 9 * tick_ms + (20 - 9 + 1) * tick_ms);
    assert(ran_on == start + 20 * tick_ms);
    std::puts("OK: a Sand Leaper takes off on row 1, lands on row 9, and goes to row 12 when its target's there");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_sandleaper_leap: %s\n", error.what());
    return 1;
}
