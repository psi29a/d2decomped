// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Iron Maiden's hit back, as tools/emu/iron_maiden.py runs game.exe's
// FUN_005c5f50: `--iron-maiden` reads its cases (`wearer merc struck phys
// calc1 calc2 calc3`, 256ths) and prints each with `| damage result class`
// (none: `- - -`): rules::iron_maiden_fine with IronMaiden's ResultFlags |
// 0x20 and HitClass.
// Without arguments, with the game data: a zombie under Iron Maiden striking
// a foe through monster_update takes the reflected physical back.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int iron_maiden_cases(const GameData& data) {
    const auto& maiden = *data.skills.get(data.skills.by_name.at("Iron Maiden"));
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int wearer = 0, merc = 0, struck = 0, phys = 0, calc1 = 0, calc2 = 0, calc3 = 0;
        if (!(fields >> wearer >> merc >> struck >> phys >> calc1 >> calc2 >> calc3)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        if (const auto back = d2d::rules::iron_maiden_fine(wearer == 1, merc != 0, struck == 0, phys, calc1, calc2, calc3))
            std::printf("%s | %d %d %d\n", line.c_str(), *back, (maiden.result_flags | 0x20) & 0xffff, maiden.hit_class);
        else
            std::printf("%s | - - -\n", line.c_str());
    }
    return 0;
}

// A zombie under Iron Maiden (at 5) strikes a foe that takes a quarter off
// physical: it loses MulDiv(the blow's reduced physical, calc1, 100).
int strike_check(const GameData& data) {
    const auto zombie_row = data.monsters.by_id.find("zombie1");
    assert(zombie_row != data.monsters.by_id.end());
    const int maiden_id = data.skills.by_name.at("Iron Maiden");
    const auto& maiden = *data.skills.get(maiden_id);
    d2d::rules::Rng rng{ 3 };
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    std::vector<Monster> pack{ make_monster(data, int(zombie_row->second), 10.f, 10.f, rng, 0) };
    auto& zombie = pack[0];
    zombie.stats.hit_points = zombie.hit_points = 100000;
    zombie.curse = { maiden_id, 5, 1u << 30 };
    zombie.reflect_pct = d2d::rules::eval_calc(data.skills, maiden.calc[0], {}, maiden_id, 5);
    assert(zombie.reflect_pct > 0 && zombie.target(data).res[0] < 100);
    set_mode(data, zombie, "A1", 0);
    std::array<Foe, 1> foes{};
    foes[0].x = 11.f; foes[0].y = 10.f;
    foes[0].fighter.dr_pct = 25;
    const Crowd crowd;
    std::vector<Missile> missiles;
    for (std::uint32_t frame = 1; frame <= 1000 && foes[0].hits == 0; ++frame)
        monster_update(data, here, zombie, foes, rng, frame * 40, 0.04f, crowd, missiles, pack);
    assert(foes[0].hits == 1 && !foes[0].blows.empty());
    const auto& blow = foes[0].blows.back();
    const int physical = d2d::rules::reduce_fine_signed(blow.raw, 25, 0);
    const int back = d2d::rules::resisted_fine(d2d::rules::iron_maiden_fine(true, false, false, physical, zombie.reflect_pct, 0, 0).value_or(0), zombie.target(data).res[0]);
    const std::int64_t lost = (std::int64_t(100000 - zombie.hit_points) << 8) + zombie.life_frac;   // life_frac: the 256ths of damage carried (hurt)
    assert(physical > 0 && back > 0 && lost == back);
    std::printf("OK: a zombie under Iron Maiden lost %lld/256 off a blow of %d/256 physical (calc1 %d)\n", static_cast<long long>(lost), physical, zombie.reflect_pct);
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--iron-maiden";
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--iron-maiden needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--iron-maiden needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    return cases ? iron_maiden_cases(*data) : strike_check(*data);
} catch (const std::exception& error) {
    std::printf("test_iron_maiden: %s\n", error.what());
    return 1;
}
