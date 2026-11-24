// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// The unplaced spawners' thinks (EvilHole, InvisoSpawner, MosquitoNest,
// 7TIllusion, InvisoPet: rules::mon_think) and the special AIs 3 / 6 / 9 / 12
// (rules::special_think, special 3's init rules::formation_init) against
// game.exe's own runs (tools/emu/ai_group2.py --dump, tests/ai_group2_cases.inc):
// the scene, the walks asked, the effects, what it comes to, the AI control
// and seed after.
#include <monsters.hpp>
#include <rules.hpp>
#include <thinks.hpp>
#include <thinks/thinker.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

const std::string_view kCases[] = {
#include "ai_group2_cases.inc"
};

// tools/emu/ai_group2.py ORDER: a MonAI name, special 3's init, or a special AI.
struct Which { std::string_view ai_name; int special; };
constexpr std::array<Which, 10> kOrder{ { { "EvilHole", 0 }, { "InvisoSpawner", 0 }, { "MosquitoNest", 0 }, { "7TIllusion", 0 }, { "InvisoPet", 0 },
                                          { "", -3 }, { "", 3 }, { "", 6 }, { "", 9 }, { "", 12 } } };

}  // namespace

int main() try {
    using d2d::rules::ThinkEffect;
    int checked = 0;
    for (const std::string_view case_line : kCases) {
        const std::string line(case_line);
        const auto bar = line.find('|'), bar2 = line.find('|', bar + 1), bar3 = line.find('|', bar2 + 1);
        std::istringstream fields(line.substr(0, bar));
        auto read = [&] { long long value = 0; fields >> value; return value; };
        const auto which = std::size_t(read());
        assert(which < kOrder.size());
        std::array<int, 3> ctrl{};
        int command = 0;
        std::vector<ThinkEffect> effects;
        d2d::rules::ThinkIn input;
        input.frame = int(read()); input.dist = int(read()); input.in_melee = read() != 0; input.got_hit = read() != 0;
        for (auto& value : ctrl) value = int(read());
        for (auto& value : input.aip) value = int(read());
        input.skill[0] = read() != 0;
        input.mode = int(read()); input.neutral = input.mode == 1;
        input.demon_hole = read() != 0; input.in_town = read() != 0; input.owner = read() != 0;
        input.owner_mode = int(read()); input.owner_x = int(read()); input.owner_y = int(read());
        input.self_owner = read() != 0 && input.owner;           // FUN_0058f0d0 found none: not itself either
        input.self_x = int(read()); input.self_y = int(read());
        input.group_hit = read() != 0; command = int(read()); input.command_x = int(read()); input.command_y = int(read());
        input.found = read() != 0; input.found_melee = read() != 0; input.reach = read() != 0;
        input.spawned = read() != 0; input.spot_free = read() != 0;
        d2d::rules::Rng seed;
        seed.low = std::uint32_t(read()); seed.high = std::uint32_t(read());
        input.state = &ctrl[0]; input.state2 = &ctrl[1]; input.state3 = &ctrl[2];
        input.command = &command; input.effects = &effects;
        std::vector<int> asks;
        std::istringstream ask_text(line.substr(bar + 1, bar2 - bar - 1));
        for (int ask = 0; ask_text >> ask;) asks.push_back(ask);
        std::size_t asked = 0;
        const d2d::rules::ThinkAway away = [&](int reach, bool) {
            assert(reach == -1 && asked < asks.size());
            return asks[asked++] != 0;
        };
        const auto& run = kOrder[which];
        d2d::rules::Think act{ d2d::rules::MonAct::none };
        if (run.special == -3) d2d::rules::formation_init(input, seed);
        else if (run.special != 0) act = d2d::rules::special_think(run.special, input, seed, away);
        else act = d2d::rules::mon_think(run.ai_name, input, seed, away);
        assert(asked == asks.size());
        for (const auto& effect : effects)                      // the toggle back clears the scratch words (FUN_005b0e00)
            if (effect.kind == ThinkEffect::toggle) ctrl = {};
        std::vector<std::array<int, 3>> want_effects;
        std::istringstream effect_text(line.substr(bar2 + 1, bar3 - bar2 - 1));
        for (std::string effect; std::getline(effect_text, effect, ';');) {
            std::istringstream three(effect);
            std::array<int, 3> value{};
            if (three >> value[0] >> value[1] >> value[2]) want_effects.push_back(value);
        }
        std::istringstream tail(line.substr(bar3 + 1));
        std::array<long long, 12> want{};
        for (auto& value : want) tail >> value;
        const std::array<long long, 12> got{ int(act.act), act.n, act.x, act.y, int(act.unit), act.wait, act.event, ctrl[0], ctrl[1], ctrl[2], seed.low, seed.high };
        bool same = got == want && effects.size() == want_effects.size();
        for (std::size_t index = 0; same && index < effects.size(); ++index)
            same = int(effects[index].kind) == want_effects[index][0] && effects[index].id == want_effects[index][1] && effects[index].value == want_effects[index][2];
        if (!same) std::fprintf(stderr, "ai_group2: %s\n  got act %d %d %d %d unit %d wait %d event %d ctrl %d %d %d, %zu effects\n", line.c_str(),
                                int(act.act), act.n, act.x, act.y, int(act.unit), act.wait, act.event, ctrl[0], ctrl[1], ctrl[2], effects.size());
        assert(same);
        ++checked;
    }
    assert(checked > 150);
    std::printf("ai_group2: %d game.exe runs match\n", checked);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
