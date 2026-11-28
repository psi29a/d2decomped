// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's get-hit after a Crushing Blow of the player's: the real player_blow and the real Fight::land.
//   `--crush-hit CASES` reads tools/emu/crush_hit.py's case lines (`id maxlife life phys hit_class div seedlo seedhi fire missile`, 256ths)
//   and prints `id gh seedlo seedhi life`: whether the monster went into GH, its seed and its life (256ths) after the blow;
//   `crush_hit.py compare` checks it against game.exe (FUN_005bffc0, FUN_0057c6c0, FUN_0057cee0 natively on the same landed
//   blow). Needs the MPQs and 1.14d's tables
//   (D2_MPQ_DIR, D2_PATCH_INSTALLER); it skips without.
//   no argument: game.exe's decisions for pinned blows (sixteen: melee, missile, fire).

#include <ai.hpp>
#include <ai/units.hpp>
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
    fighter.elem[0] = fighter.elem_stats[0] = { fire, fire };   // melee's, and a missile's record (missile_record)
    fighter.ar_base = 1 << 20;
    fighter.crushing = crushing ? 100 : 0;
    d2d::rules::Swing swing;
    swing.missile = missile;
    // A missile's record takes its physical from the row's own part, in 256ths and unscaled (FUN_0064b860), the oracle's
    // pinned value as it is; a swing's from the weapon's.
    if (missile) { fighter.phys_lo = fighter.phys_hi = 0; swing.row_lo = swing.row_hi = int(phys); }
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


// game.exe's decisions for pinned blows (crush_hit.py game on these cases: FUN_005bffc0, FUN_0057c1e0, FUN_0057c6c0, FUN_0057cee0
// natively; V run). Lives in 256ths. A Crushing Blow's life is not in the record's total (FUN_0057c1e0 sums it before the strike
// events), so a blow under max / 16 (class 0) takes no get-hit however much the crush took; its seed is the unit's after the rolls
// it took (an unrolled blow leaves it); the life lost is the crush and the total. A missile's crush is the doubled divisor's
// (event 6) and is out of the total too; a fire part marks the blow (max / 16 for every class) and is in the total.
struct Pin { long maxlife, life, phys; int hit_class, divisor, fire; bool missile; std::uint32_t seed_low, seed_high; bool staggered; std::uint32_t low, high; long life_after; };
constexpr std::array<Pin, 16> kPins{ {
    { 25600, 25600, 1280, 0, 4, 0, false, 12345, 678, false, 12345, 678, 17920 },             // 5 points of 100: under max / 16 (1600), no roll
    { 25600, 25600, 1792, 0, 4, 0, false, 12345, 678, false, 2359725804u, 9490060, 17408 },   // 7 points: over it, rolls, no get-hit
    { 25600, 25600, 0, 0, 4, 0, false, 12345, 678, false, 12345, 678, 19200 },                // a crush with no physical: no size, no roll
    { 25600, 12800, 1280, 2, 4, 0, false, 777, 999, false, 777, 999, 8320 },                 // class 2 (max / 8) at half life
    { 262144, 262144, 15360, 0, 8, 0, false, 31337, 4242, false, 31337, 4242, 214016 },       // a boss row's eighth, 60 points of 1024
    { 262144, 131072, 16384, 0, 8, 0, false, 31337, 4242, true, 3792358185u, 758789105, 98304 },
    { 6400, 6400, 300, 5, 4, 0, false, 99, 1, false, 1254751280u, 41, 4500 },
    { 6400, 6400, 420, 5, 4, 0, false, 99, 1, true, 99, 1, 4380 },
    { 25600, 12800, 1599, 0, 4, 0, false, 4173236273u, 3100213626u, false, 4173236273u, 3100213626u, 8001 },   // a crush of 12.5 points: no roll (a crush cut to whole points rolls)
    { 25600, 25600, 1280, 0, 4, 0, true, 12345, 678, false, 12345, 678, 21120 },              // a missile: an eighth (event 6), not in the size
    { 25600, 25600, 1792, 0, 4, 0, true, 12345, 678, false, 2359725804u, 9490060, 20608 },
    { 262144, 262144, 15360, 0, 8, 0, true, 31337, 4242, false, 31337, 4242, 230400 },        // a boss row's missile: a sixteenth
    { 25600, 25600, 0, 2, 4, 7, false, 777, 999, false, 346909140u, 324, 17408 },             // 7 points of fire, class 2: marked, max / 16, rolls
    { 25600, 25600, 0, 2, 4, 5, false, 777, 999, false, 777, 999, 17920 },                    // 5 points of fire: under max / 16, no roll
    { 25600, 25600, 256, 5, 4, 6, true, 99, 1, false, 1254751280u, 41, 20608 },               // a missile with fire, class 5 (max / 64 unmarked)
    { 25600, 12800, 0, 0, 4, 6, false, 4173236273u, 3100213626u, false, 4173236273u, 3100213626u, 8064 },     // 6 points of fire under max / 16 with a crush of 12.5
} };

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
    for (const auto& pin : kPins) {
        const auto result = strike(*data, true, type, pin.maxlife, pin.life, pin.phys, pin.hit_class, pin.divisor, pin.seed_low, pin.seed_high, pin.fire, pin.missile);
        assert(result.staggered == pin.staggered && result.low == pin.low && result.high == pin.high && result.life == pin.life_after);
    }
    std::printf("ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
