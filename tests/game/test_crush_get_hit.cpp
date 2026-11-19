// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's get-hit after a Crushing Blow of the player's: the real player_blow and the real Fight::land.
//   `--crush-hit CASES` reads tools/emu/crush_hit.py's case lines (`id maxlife life phys hit_class div seedlo seedhi fire missile`, 256ths)
//   and prints `id gh seedlo seedhi life`: whether the monster went into GH, its seed and its life (256ths) after the blow;
//   `crush_hit.py compare` checks it against game.exe (FUN_005bffc0, FUN_0057c6c0, FUN_0057cee0 natively on the same landed
//   blow). Needs the MPQs and 1.14d's tables
//   (D2_MPQ_DIR, D2_PATCH_INSTALLER); it skips without.
//   no argument: game.exe's decisions for pinned blows.

#include <ai.hpp>
#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <uniques.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

struct Result { bool staggered = false; std::uint32_t low = 0, high = 0; long life = 0; };

// One landed blow of a Crushing Blow weapon (100 percent, a fixed physical, `fire` whole points of fire, always hitting; a
// missile's when `missile`) on a plain or boss-row zombie with `life` of `maxlife` (256ths) and the seed `seed`: player_blow,
// then Fight::land, the player's landed blow on a monster.
Result strike(GameData& data, bool crushing, int type, long maxlife, long life, long phys, int hit_class, int divisor, std::uint32_t seed_low, std::uint32_t seed_high,
              int fire, bool missile) {
    d2d::rules::Rng spawn{ 0x5eed };
    Monster monster = make_monster(data, type, 10.f, 10.f, spawn, 0);
    monster.stats.hit_points = int((maxlife + 255) >> 8);
    monster.max_life_frac = int(std::int64_t(monster.stats.hit_points) * 256 - maxlife);
    monster.hit_points = int((life + 255) >> 8);
    monster.life_frac = int(std::int64_t(monster.hit_points) * 256 - life);
    monster.boss = divisor == 8 ? d2d::rules::Boss::superunique : d2d::rules::Boss::none;
    monster.seed.low = seed_low;
    monster.seed.high = seed_high;
    const Level* level = nullptr;
    d2d::rules::Rng fight_rng{ 1 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, fight_rng, cues };
    Fight fight{ &data, level, fight_rng, loot, cues };
    Fight::Hero hero;
    hero.character.stats.values[d2d::d2s::kMaxLife] = hero.character.stats.values[d2d::d2s::kLife] = 100 << 8;
    fight.act_as(hero);
    fight.add_monster(std::move(monster));
    d2d::rules::Fighter fighter;
    fighter.phys_lo = fighter.phys_hi = int(phys);
    fighter.elem[0] = { fire, fire };
    fighter.ar_base = 1 << 20;
    fighter.crushing = crushing ? 100 : 0;
    d2d::rules::Swing swing;
    swing.missile = missile;
    const auto target = fight.monsters[0].target(data);
    assert(target.crush_divisor == divisor && target.res[2] == 0 && target.res[0] == 0);
    d2d::rules::Blow blow;
    for (std::uint32_t roll = 1; !(blow.hit && blow.crushing == crushing); ++roll) {
        d2d::rules::Rng rng{ roll };
        blow = d2d::rules::player_blow(fighter, target, 30, rng, swing);
    }
    blow.hit_class = hit_class;
    const bool landed = fight.land(0, blow, true, 1000, -1);
    const Monster& struck = fight.monsters[0];
    assert(landed && struck.alive());
    return { struck.mode == "GH", struck.seed.low, struck.seed.high, long(struck.life_fine()) };
}

}   // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    const bool oracle = mode == "--crush-hit" && argc > 2;
    if (!oracle && argc > 1) {
        std::printf("SKIP: an oracle mode, `test_crush_get_hit --crush-hit CASES`\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return oracle ? D2D_TEST_SKIPPED : 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return oracle ? D2D_TEST_SKIPPED : 0; }
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(zombie != data->monsters.by_id.end());
    const int type = int(zombie->second);
    data->monsters.types[std::size_t(type)].can_get_hit = true;
    const bool control = argc > 3 && std::string_view(argv[3]) == "--no-crush";
    if (oracle) {
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "FAIL: cannot open %s\n", argv[2]); return 1; }
        std::string line;
        while (std::getline(cases, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream words(line);
            long id = 0, maxlife = 0, life = 0, phys = 0, hit_class = 0, div = 0, fire = 0, missile = 0;
            std::uint32_t seed_low = 0, seed_high = 0;
            if (!(words >> id >> maxlife >> life >> phys >> hit_class >> div >> seed_low >> seed_high >> fire >> missile) || (div != 4 && div != 8) || life < 1
                || life > maxlife || fire < 0 || (fire & 255) != 0 || (missile != 0 && missile != 1)) {
                std::fprintf(stderr, "FAIL: bad case line: %s\n", line.c_str());
                return 1;
            }
            const auto result = strike(*data, !control, type, maxlife, life, phys, int(hit_class), int(div), seed_low, seed_high, int(fire >> 8), missile != 0);
            std::printf("%ld %d %u %u %ld\n", id, int(result.staggered), result.low, result.high, result.life);
        }
        return 0;
    }
    std::printf("ok\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
