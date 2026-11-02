// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--missile-hit CASES`: a Sorceress missile's hit on one monster, the cases
// of tools/emu/missile_hit.py (game.exe's FUN_00644d50 / FUN_00644e40, then
// FUN_005a89a0 on the missile's seed and FUN_0057c6c0), one line each, to
// diff: the element's min and max (256ths, rules::missile_damage) and the
// monster's life after rules::missile_blow and hurt() (256ths), the roll
// drawn on the case's seed.

#include <ai.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--missile-hit") {
        std::printf("SKIP: an oracle mode, run `test_missile_hit --missile-hit CASES` (see tools/emu/missile_hit.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);
    d2d::rules::Rng made_rng{ 3 };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    const Monster made = make_monster(*data, fallen->second, 10, 10, made_rng, 0);
    static constexpr std::array<std::size_t, 3> kRes{ 2, 3, 4 };       // fire, lightning, cold -> Target::res

    std::ifstream cases(argv[2]);
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int case_id = 0, skill_id = 0, lvl = 0, clvl = 0, res = 0, pierce = 0, life = 0, count = 0;
        std::array<int, 4> masteries{};
        unsigned long long seed_low = 0, seed_high = 0;
        fields >> case_id >> skill_id >> lvl >> clvl >> masteries[0] >> masteries[1] >> masteries[2] >> masteries[3] >> res >> pierce >> life >> seed_low >> seed_high >> count;
        std::map<int, int> levels;
        for (int k = 0; k < count; ++k) { int other = 0, level = 0; fields >> other >> level; levels[other] = level; }
        levels[skill_id] = lvl;
        d2d::rules::CalcEnv env;
        env.clvl = clvl;
        env.base_level = env.level = [&](int other) { const auto found = levels.find(other); return found == levels.end() ? 0 : found->second; };
        env.stat = [&](int stat) { return stat >= d2d::d2s::kPassiveFireMastery && stat < d2d::d2s::kPassiveFireMastery + 4 ? masteries[std::size_t(stat - d2d::d2s::kPassiveFireMastery)] : 0; };
        const auto* skill = data->skills.get(skill_id);
        if (!skill || skill->etype < 0 || skill->etype > 2) continue;
        const auto damage = d2d::rules::missile_damage(data->skills, *skill, env, lvl);
        d2d::rules::Target target;
        target.res[kRes[std::size_t(skill->etype)]] = res;
        std::array<int, 4> pierces{};
        pierces[std::size_t(skill->etype)] = pierce;
        d2d::rules::Rng rng;
        rng.low = std::uint32_t(seed_low & 0xFFFFFFFFULL);
        rng.high = std::uint32_t(seed_high & 0xFFFFFFFFULL);
        const auto blow = d2d::rules::missile_blow(damage, target, pierces, rng);
        Monster monster = made;
        monster.stats.hit_points = monster.hit_points = life;
        monster.life_frac = 0;
        hurt(*data, monster, blow.damage, 0, blow.frac);
        std::printf("%d %d %d %d\n", case_id, damage.elo, damage.ehi, monster.alive() ? monster.hit_points * 256 - monster.life_frac : 0);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_missile_hit: %s\n", error.what());
    return 1;
}
