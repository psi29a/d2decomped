// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_005539b0 schedules the stock DeathMaul sequence at 13/35 ticks,
// and standalone M5A2HTH at 10/18. See tools/emu/death_maul_timing.py.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <montypes.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) return D2D_TEST_SKIPPED;
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) return D2D_TEST_SKIPPED;
    const auto class_id = int(data->monsters.by_id.at("deathmauler1"));
    const auto skill_id = data->skills.by_name.at("DeathMaul");
    auto& row = const_cast<d2d::rules::MonType&>(data->monsters.types[std::size_t(class_id)]);
    row.diff[0].aip[2] = 100;  // Make the stock AI pick the ranged skill deterministically.
    row.diff[0].aip[3] = 100;

    Level level;
    level.id = d2d::rules::level_ids::kBloodyFoothills;
    level.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    level.walk.assign(80 * 80, 0);
    std::array<Foe, 1> foes{};
    foes.front().x = 11;
    foes.front().y = 5;
    const Crowd crowd;
    constexpr std::uint32_t start = 4000, tick_ms = 40;
    d2d::rules::Rng rng{7};

    for (const bool chilled : {false, true}) {
        auto monster = make_monster(*data, class_id, 5.f, 5.f, rng, 0);
        monster.aware = true;
        monster.next_act = start;
        if (chilled) {
            monster.anim_pct = monster.attack_pct = -50;
            monster.chill_until = start + 50 * tick_ms;
        }
        std::vector<Missile> missiles;
        monster_update(*data, level, monster, foes, rng, start, 0.04f, crowd, missiles);
        assert(monster.mode == "A2" && monster.skill == skill_id);
        monster.next_act = start + 50 * tick_ms;
        int shot = -1, end = -1;
        for (int tick = 1; tick <= 40; ++tick) {
            monster_update(*data, level, monster, foes, rng, start + std::uint32_t(tick) * tick_ms, 0.04f, crowd, missiles);
            if (shot < 0 && !missiles.empty()) shot = tick;
            if (end < 0 && monster.mode != "A2") end = tick;
        }
        std::printf("DeathMaul%s: action %d, end %d, missiles %zu\n", chilled ? " chilled" : "", shot, end, missiles.size());
        assert(shot == 13 && end == 35 && missiles.size() == 1);
    }

    auto monster = make_monster(*data, class_id, 5.f, 5.f, rng, 0);
    std::vector<Missile> missiles;
    set_mode(*data, monster, "A2", start);
    int action = -1, end = -1;
    for (int tick = 1; tick <= 20; ++tick) {
        monster_update(*data, level, monster, foes, rng, start + std::uint32_t(tick) * tick_ms, 0.04f, crowd, missiles);
        if (action < 0 && monster.struck) action = tick;
        if (end < 0 && monster.mode != "A2") end = tick;
    }
    std::printf("Standalone Death Mauler A2: action %d, end %d\n", action, end);
    assert(action == 10 && end == 18);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_death_maul_timing: %s\n", error.what());
    return 1;
}
