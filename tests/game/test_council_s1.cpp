// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Original FUN_005539b0 and FUN_005a7ba0: tools/emu/council_modes.py.

#include <ai.hpp>
#include <gamedata.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData data;
    data.monsters.types.resize(32);
    data.mon_bin.resize(348);
    data.mon_bin[345] = 12;
    data.mon_bin[346] = 10;
    data.mon_bin[347] = 11;
    const auto& missile = data.missiles.try_emplace("test_s1").first->second;
    Monster council;
    council.npc.root = "monsters";
    council.npc.code = "HP";
    council.npc.base_w = "HTH";
    std::string key = "monsters/HP/S1/HTH";
    for (const auto& component : council.npc.comp) key += "/" + component;
    data.npc_timings.emplace(key, GameData::AnimTiming{ .name = "HPS1HTH", .speed = 200, .frames = 14, .action = 3 });

    Level level;
    std::array<Foe, 1> foes{};
    foes.front().x = 2;
    const Crowd crowd;
    constexpr std::uint32_t start = 4000;
    constexpr std::uint32_t tick_ms = 40;
    struct TimingCase { int rate; int action; int end; };
    constexpr std::array cases{
        TimingCase{30, 26, 120}, TimingCase{42, 19, 86}, TimingCase{50, 16, 72},
        TimingCase{56, 14, 64}, TimingCase{70, 11, 52}, TimingCase{73, 11, 50},
        TimingCase{84, 10, 43}, TimingCase{94, 9, 39}, TimingCase{100, 8, 36},
        TimingCase{112, 7, 32}, TimingCase{117, 7, 31}, TimingCase{125, 7, 29},
        TimingCase{126, 7, 29}, TimingCase{140, 6, 26}, TimingCase{150, 6, 24},
        TimingCase{168, 5, 22}, TimingCase{170, 5, 22}, TimingCase{200, 4, 18}, TimingCase{210, 4, 18},
        TimingCase{250, 4, 15}, TimingCase{254, 4, 15}, TimingCase{256, 3, 14},
        TimingCase{300, 3, 12}, TimingCase{350, 3, 11},
    };
    constexpr std::array stock_rates{
        std::array{126, 42, 70, 84}, std::array{168, 56, 94, 112},
        std::array{210, 70, 117, 140},
    };
    bool matches = true;
    for (const int class_id : {345, 346, 347}) {
        const auto type = int(data.mon_bin[std::size_t(class_id)]);
        data.monsters.types[std::size_t(type)].miss_s1 = "test_s1";
        auto& row = data.monsters.types[std::size_t(type)];
        constexpr std::array velocity{6, 8, 10};
        const auto variant = std::size_t(class_id - 345);
        row.walk_base = d2d::rules::monster_walk_base(class_id == 345, 168, velocity[variant], 6);
        Monster rate_probe = council;
        rate_probe.type = type;
        for (std::size_t scenario = 0; scenario < 4; ++scenario) {
            constexpr std::array cold_effect{0, -50, -33, -25};
            rate_probe.walk_pct = std::max(kMonsterVelocityPct + cold_effect[scenario], 25);
            matches &= mode_rate(data, rate_probe, "S1") == stock_rates[variant][scenario];
        }
        for (const auto& expected : cases) {
            data.monsters.types[std::size_t(type)].walk_base = expected.rate;
            Monster monster = council;
            monster.type = type;
            // These class IDs take d2d's movement-rate path for S1.
            // Supply the same effective rates as the original scheduler fixture.
            monster.walk_pct = 100;
            monster.speed_pct = monster.walk_pct - kMonsterVelocityPct;
            d2d::rules::Rng rng{7};
            std::vector<Missile> missiles;
            set_mode(data, monster, "S1", start);
            int end_tick = -1;
            for (std::uint32_t tick = 0; tick <= std::uint32_t(expected.end + 2); ++tick) {
                monster_update(data, level, monster, foes, rng, start + tick * tick_ms, 0.04f, crowd, missiles);
                if (end_tick < 0 && monster.mode != "S1") end_tick = int(tick);
            }

            const auto launch_tick = missiles.empty() ? -1 : int((missiles.front().born - start) / tick_ms);
            std::printf("Council %d at rate %d: %zu missile(s), action tick %d (expected %d), end tick %d (expected %d)\n",
                        class_id, expected.rate, missiles.size(), launch_tick, expected.action, end_tick, expected.end);
            matches &= missiles.size() == 1 && missiles.front().info == &missile
                && launch_tick == expected.action && end_tick == expected.end;
        }
        data.monsters.types[std::size_t(type)].walk_base = 200;
        for (const bool restart : {false, true}) {
            Monster monster = council;
            monster.type = type;
            monster.walk_pct = 100;
            monster.speed_pct = 100 - kMonsterVelocityPct;
            d2d::rules::Rng rng{7};
            std::vector<Missile> missiles;
            set_mode(data, monster, "S1", start);
            int end_tick = -1;
            for (std::uint32_t tick = 0; tick <= 24; ++tick) {
                const auto now = start + tick * tick_ms;
                if (tick == 2) {
                    if (restart) set_mode(data, monster, "S1", now);
                    else monster.speed_pct -= 50;
                }
                monster_update(data, level, monster, foes, rng, now, 0.04f, crowd, missiles);
                if (end_tick < 0 && monster.mode != "S1") end_tick = int(tick);
            }
            const auto launch_tick = missiles.empty() ? -1 : int((missiles.front().born - start) / tick_ms);
            std::printf("Council %d, %s at tick 2: %zu missile(s), launch tick %d, end tick %d\n",
                        class_id, restart ? "S1 restart" : "50% slowdown", missiles.size(), launch_tick, end_tick);
            // Evidence for the original restart: FUN_00553990 clears the
            // old action and mode-end events before FUN_005539b0 queues new ones.
            matches &= missiles.size() == 1 && launch_tick == (restart ? 6 : 4)
                && end_tick == (restart ? 20 : 18);
        }
        for (const int freeze_tick : {4, 5}) {
            Monster monster = council;
            monster.type = type;
            monster.walk_pct = 100;
            monster.speed_pct = 100 - kMonsterVelocityPct;
            d2d::rules::Rng rng{7};
            std::vector<Missile> missiles;
            set_mode(data, monster, "S1", start);
            for (int tick = 0; tick <= 20; ++tick) {
                const auto now = start + std::uint32_t(tick) * tick_ms;
                if (tick == freeze_tick) monster.frozen_until = now + 4 * tick_ms;
                monster_update(data, level, monster, foes, rng, now, 0.04f, crowd, missiles);
            }
            // FUN_005a7f80 discards the action if the unit is already frozen when dispatched.
            const auto expected = std::size_t(freeze_tick == 5);
            std::printf("Council %d, freeze at tick %d: %zu missile(s) (expected %zu)\n",
                        class_id, freeze_tick, missiles.size(), expected);
            matches &= missiles.size() == expected;
        }
    }
    return matches ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_council_s1: %s\n", error.what());
    return 1;
}
