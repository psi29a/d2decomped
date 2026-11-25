// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A leaper not on its landing when the landing event comes (FUN_005da120's monster branch, with FUN_006212c0) waits: the sequence goes
// back to a row by its BaseId and its length grows a row, so the event comes round each tick. `--oracle <cases>` prints d2d's side
// for tools/emu/leap_land_wait.py.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <leap_wait.hpp>
#include <level.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc == 3 && std::strcmp(argv[1], "--oracle") == 0) {
        std::ifstream cases(argv[2]);
        long id = 0, base = 0, distance = 0;
        unsigned long position = 0, length = 0;
        while (cases >> id >> base >> distance >> position >> length) {
            const auto wait = d2d::rules::leap_wait(int(base), int(distance), std::uint32_t(position), std::uint32_t(length));
            std::printf("%ld %u %u\n", id, wait.position, wait.length);
        }
        return 0;
    }
    using d2d::rules::leap_wait;
    assert(leap_wait(0x4e, 9, 0x1ff, 0x1234).position == 0x8ff && leap_wait(0x4e, 9, 0x1ff, 0x1234).length == 0x1300);
    assert(leap_wait(0x21c, 5, 0, 0).position >> 8 == 10 && leap_wait(0x21c, 1, 0, 0).position >> 8 == 12 && leap_wait(7, 5, 0, 0).position == 0);
    // A Leap Attack flight slower than its sequence: at the landing event (row 11) it waits a tick, its row held there.
    Monster leaper;
    leaper.unit.mode_ms = 1000;
    leaper.leap_from_x = 10.1f; leaper.leap_from_y = 10.1f; leaper.leap_to_x = 20.1f; leaper.leap_to_y = 10.1f;
    leaper.leap_pace = leap_cells_per_frame(10);     // 0.125 cells a frame: 80 frames for 10 cells
    leaper.leaping = true;
    leaper.leap_until = 1000 + 11 * 40; leaper.mode_until = 1000 + 17 * 40;
    assert(leap_hold(leaper, 0x21c, 40));
    assert(leaper.leap_until == 1000 + 12 * 40 && leaper.mode_until == 1000 + 18 * 40 && leaper.leap_hold_row == 11);
    // Close to the landing (under 2 subtiles) it goes back to row 12, its event the row after.
    leaper.leap_until = 1000 + 79 * 40;
    assert(leap_hold(leaper, 0x21c, 40) && leaper.leap_hold_row == 13);
    // On the landing, or a BaseId whose rewind isn't traced, it lands.
    leaper.leap_until = 1000 + 90 * 40;
    assert(!leap_hold(leaper, 0x21c, 40));
    leaper.leap_until = 1000 + 11 * 40;
    assert(!leap_hold(leaper, 5, 40));
    // On the landing's subtile short of its centre (FUN_005da120 compares the path's subtile words): 10 cells at Run 13
    // (0.1625 a frame) are on subtile 100 at frame 61, 0.44 subtile short; it lands there, not a tick later. Frame 60 waits.
    leaper.leap_pace = leap_cells_per_frame(13);
    leaper.leap_until = 1000 + 60 * 40;
    assert(leap_hold(leaper, 0x21c, 40));
    leaper.leap_until = 1000 + 61 * 40;
    assert(!leap_hold(leaper, 0x21c, 40));
    // The real update path (monster_update): Korlic's Leap Attack (ancientbarb1's BaseId 0x21c, seq_ancientleap, made-up rows) in the
    // air at the landing event, row 11. On the old logic it snaps to the landing there and its mode ends on row 17; now it waits a
    // tick at a time (FUN_005da120) till the flight is done (80 frames at pace 0.125 cells), its row held, its mode's end moved on as far.
    GameData data;
    data.monsters.types.resize(0x21d);
    auto& type = data.monsters.types[0x21c];
    type.id = "ancientbarb3"; type.base = 0x21c; type.run = 10;
    type.skill[0] = "Leap Attack"; type.sk_mode[0] = "seq_ancientleap"; type.sk_lvl[0] = 1;
    data.mon_npc.resize(0x21d);
    constexpr int kLeapAttack = 143;
    data.skills.rows.resize(std::size_t(kLeapAttack) + 1);
    auto& skill = data.skills.rows[std::size_t(kLeapAttack)];
    skill.id = kLeapAttack;
    skill.srvstfunc = d2d::rules::ServerStartFunction::kLeapAttack;
    skill.srvdofunc = d2d::rules::ServerDoFunction::kLeapAttack;
    data.skills.by_name["Leap Attack"] = kLeapAttack;
    auto& rows = data.mon_seqs["seq_ancientleap"];
    for (int row = 0; row < 17; ++row) rows.push_back({ "S1", row, row == 5 || row == 11 || row == 13 || row == 14 ? 1 : 0 });
    Monster korlic;
    korlic.npc.root = "monsters"; korlic.npc.code = "AB"; korlic.npc.base_w = "HTH";
    std::string key = "monsters/AB/S1/HTH";
    for (const auto& component : korlic.npc.comp) key += "/" + component;
    data.npc_timings.emplace(key, GameData::AnimTiming{ .name = "ABS1HTH", .speed = 256, .frames = 17, .action = 5 });
    korlic.type = 0x21c;
    korlic.hit_points = korlic.stats.hit_points = 100;
    constexpr std::uint32_t start = 4000, tick_ms = 40;
    set_mode(data, korlic, "S1", start);
    korlic.skill = kLeapAttack;
    korlic.unit.x = korlic.leap_from_x = 10.1f; korlic.unit.y = korlic.leap_from_y = 10.1f;
    korlic.leap_to_x = 20.1f; korlic.leap_to_y = 10.1f;
    korlic.leap_pace = leap_cells_per_frame(10);
    korlic.leaping = true;
    korlic.leap_until = start + 11 * tick_ms;
    korlic.mode_until = start + 17 * tick_ms;
    Level level;
    std::vector<Foe> foes(1);
    foes.front().x = 40; foes.front().y = 40;
    const Crowd crowd;
    d2d::rules::Rng rng{7};
    std::vector<Missile> missiles;
    const Monster fresh = korlic;                                                    // the setup, before it runs
    int landed = -1;
    for (std::uint32_t tick = 1; tick <= 120; ++tick) {
        monster_update(data, level, korlic, foes, rng, start + tick * tick_ms, 0.04f, crowd, missiles);
        if (tick == 11) {
            assert(korlic.leaping && korlic.unit.x < 20.f);                          // not snapped to the landing
            assert(korlic.leap_hold_row == 11 && korlic.seq_frame == 11);            // waiting, its row held
            assert(korlic.leap_until == start + 12 * tick_ms && korlic.mode_until == start + 18 * tick_ms);
        }
        if (landed < 0 && !korlic.leaping) landed = int(tick);
    }
    assert(landed > 70 && landed < 90);                                              // down when the flight is: 80 frames
    assert(korlic.unit.x == korlic.leap_to_x);
    assert(korlic.leap_hold_row == -1);
    // Held at row 13 (it nears the landing, under 2 subtiles off), the row's event waits for the landing: it strikes once, a foe in reach, when the unit is down.
    rows.clear();
    for (int row = 0; row < 17; ++row) rows.push_back({ "S1", row, row == 13 || row == 14 ? 1 : 0 });   // two strike rows: one strike (unit flag 0x40)
    Monster ancient = fresh;
    set_mode(data, ancient, "S1", start);
    ancient.skill = kLeapAttack;
    ancient.unit.x = ancient.leap_from_x = 10.1f; ancient.unit.y = ancient.leap_from_y = 10.1f;
    ancient.seq_done = 0; ancient.seq_once = false; ancient.leap_hold_row = -1; ancient.seq_shift_ms = 0;
    ancient.leaping = true;
    std::vector<Foe> reach(1);
    reach.front().x = 20.5f; reach.front().y = 10.1f;
    std::size_t struck_while_leaping = 0;
    for (std::uint32_t tick = 1; tick <= 120; ++tick) {
        monster_update(data, level, ancient, reach, rng, start + tick * tick_ms, 0.04f, crowd, missiles);
        if (ancient.leaping) struck_while_leaping += reach.front().melee_by.size();
    }
    assert(struck_while_leaping == 0 && reach.front().melee_by.size() == 1);
    std::puts("OK: a leaper short of its landing waits there (FUN_005da120)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_leap_landing_wait: %s\n", error.what());
    return 1;
}
