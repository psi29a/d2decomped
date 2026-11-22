// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_005539b0 schedules HPA1HTH; tools/emu/council_modes.py checks its deadlines.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::game;

int main() try {
    // HPA1HTH at 168: FUN_00623e00 gives frame 1 on tick 3 and
    // frame 3 on tick 6. Dividing by a rounded 60 ms gives 2 and 4.
    using d2d::rules::fixed_animation_position;
    constexpr std::array original_positions{0u, 168u, 336u, 504u, 672u, 840u, 1008u, 1176u, 1344u};
    for (std::size_t tick = 0; tick < original_positions.size(); ++tick) {
        const auto elapsed = std::uint32_t(tick) * 40;
        if (fixed_animation_position(elapsed, 168, 10) != original_positions[tick]
            || fixed_animation_position(elapsed + 39, 168, 10) != original_positions[tick]) return 1;
    }
    if (fixed_animation_position(639, 168, 10) != 2520u || fixed_animation_position(640, 168, 10)) return 1;

    GameData data;
    data.monsters.types.resize(32);
    data.mon_bin.resize(348);
    data.mon_bin[345] = 12;
    data.mon_bin[346] = 10;
    data.mon_bin[347] = 11;
    Monster council;
    council.npc.root = "monsters";
    council.npc.code = "HP";
    council.npc.base_w = "HTH";
    std::string key = "monsters/HP/A1/HTH";
    for (const auto& component : council.npc.comp) key += "/" + component;
    data.npc_timings.emplace(key, GameData::AnimTiming{ .name = "HPA1HTH", .speed = 168, .frames = 10, .action = 5 });

    Level level;
    const Crowd crowd;
    constexpr std::uint32_t start = 4000, tick_ms = 40;
    enum class Interruption { none, freeze, freeze_after, neutral, slow };
    struct Case {
        int percent, action, end;
        Interruption interruption = Interruption::none;
        int interrupt_tick = 0;
    };
    constexpr std::array cases{
        Case{100, 8, 16}, Case{50, 16, 31}, Case{67, 12, 23}, Case{75, 11, 21},
        Case{100, -1, 16, Interruption::freeze, 8},
        Case{100, 8, 16, Interruption::freeze_after, 8},
        Case{100, 8, 16, Interruption::freeze, 9},
        Case{100, -1, 8, Interruption::neutral, 8},
        Case{100, 8, 16, Interruption::slow, 3},
    };
    bool matches = true;
    for (const int class_id : {345, 346, 347}) {
        const auto type = int(data.mon_bin[std::size_t(class_id)]);
        for (const auto& expected : cases) {
            Monster monster = council;
            monster.type = type;
            monster.attack_pct = expected.percent - 100;
            monster.next_act = start + 40 * tick_ms;
            std::array<Foe, 1> foes{};
            foes.front().x = 0.5f;
            d2d::rules::Rng rng{7};
            std::vector<Missile> missiles;
            set_mode(data, monster, "A1", start);
            int action = -1, end = -1;
            for (int tick = 0; tick <= 34; ++tick) {
                const auto now = start + std::uint32_t(tick) * tick_ms;
                if (tick == expected.interrupt_tick) {
                    if (expected.interruption == Interruption::freeze) monster.frozen_until = now + 3 * tick_ms;
                    if (expected.interruption == Interruption::neutral) set_mode(data, monster, "NU", now);
                    if (expected.interruption == Interruption::slow) monster.attack_pct = -50;
                }
                monster_update(data, level, monster, foes, rng, now, 0.04f, crowd, missiles);
                if (tick == expected.interrupt_tick && expected.interruption == Interruption::freeze_after)
                    monster.frozen_until = now + 3 * tick_ms;
                if (expected.interruption == Interruption::slow && tick > 0 && tick <= 8) {
                    constexpr std::array positions{168u, 336u, 420u, 504u, 588u, 672u, 756u, 840u};
                    matches &= monster.scheduled_anim_position == positions[std::size_t(tick - 1)];
                }
                if (action < 0 && !foes.front().melee_by.empty()) action = tick;
                if (monster.mode != "A1") {
                    end = tick;
                    break;
                }
            }
            std::printf("Council %d A1 at %d%%, interrupt tick %d: action %d (expected %d), end %d (expected %d)\n",
                        class_id, expected.percent, expected.interrupt_tick, action, expected.action, end, expected.end);
            matches &= action == expected.action && end == expected.end
                && foes.front().melee_by.size() == std::size_t(expected.action >= 0) && missiles.empty();
        }
    }
    return matches ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_council_a1: %s\n", error.what());
    return 1;
}
