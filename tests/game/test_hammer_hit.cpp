// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--hammer-hit CASES`: a Blessed Hammer hit on one monster, the cases of
// tools/emu/hammer_hit.py (game.exe's FUN_00644d50 / FUN_00644e40 for the
// skill, FUN_006461d0 for the caster's Concentration, FUN_005ad2f0 (the
// undead / demon bonus) and FUN_0057c6c0 for the hit), one line each, to diff: the missile's magic min
// and max after the Concentration influence (256ths) and the monster's life
// after rules::missile_blow and hurt() (256ths), the roll drawn on the case's
// seed.

#include <ai.hpp>
#include <combat.hpp>
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
    if (argc < 3 || std::string_view(argv[1]) != "--hammer-hit") {
        std::printf("SKIP: an oracle mode, run `test_hammer_hit --hammer-hit CASES` (see tools/emu/hammer_hit.py)\n");
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
    constexpr int kBlessedHammer = 112, kVigor = 115, kBlessedAim = 108, kNoAura = -1000;
    constexpr std::size_t kMagicRes = 1;                                // Target::res: physical, magic, fire, lightning, cold, poison

    std::ifstream cases(argv[2]);
    if (!cases) { std::fprintf(stderr, "test_hammer_hit: cannot open %s\n", argv[2]); return 1; }
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int case_id = 0, lvl = 0, vigor = 0, aim = 0, percent = 0, monster_id = 0, res = 0, life = 0;
        unsigned long long seed_low = 0, seed_high = 0;
        fields >> case_id >> lvl >> vigor >> aim >> percent >> monster_id >> res >> life >> seed_low >> seed_high;
        const std::map<int, int> levels{ { kBlessedHammer, lvl }, { kVigor, vigor }, { kBlessedAim, aim } };
        d2d::rules::CalcEnv env;
        env.clvl = 30;
        env.base_level = env.level = [&](int other) { const auto found = levels.find(other); return found == levels.end() ? 0 : found->second; };
        env.stat = [](int) { return 0; };
        const auto* skill = data->skills.get(kBlessedHammer);
        assert(skill && skill->etype == 4);
        auto damage = d2d::rules::missile_damage(data->skills, *skill, env, lvl);
        const auto& row = data->missiles.at("blessedhammer");
        assert(row.dmg_func == 5);
        damage.undead_pct = row.dmg_par1;
        damage.demon_pct = row.dmg_par2;
        damage = d2d::rules::influenced(damage, percent == kNoAura ? 0 : d2d::rules::aura_influence(percent, skill->par[0]));
        d2d::rules::Target target;
        const auto& type = data->monsters.types[data->mon_bin[std::size_t(monster_id)]];
        target.undead = type.undead;
        target.demon = type.demon;
        target.res[kMagicRes] = res;
        d2d::rules::Rng rng;
        rng.low = std::uint32_t(seed_low & 0xFFFFFFFFULL);
        rng.high = std::uint32_t(seed_high & 0xFFFFFFFFULL);
        const auto blow = d2d::rules::missile_blow(damage, target, {}, rng);
        Monster monster = made;
        monster.stats.hit_points = monster.hit_points = life;
        monster.life_frac = 0;
        hurt(*data, monster, blow.damage, 0, blow.frac);
        std::printf("%d %d %d %d\n", case_id, damage.elo, damage.ehi, monster.alive() ? monster.hit_points * 256 - monster.life_frac : 0);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_hammer_hit: %s\n", error.what());
    return 1;
}
