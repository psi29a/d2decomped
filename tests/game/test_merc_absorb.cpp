// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a monster's elemental and magic blows leave of a MERCENARY's life when its gear absorbs: the real Fight::merc_foe (its
// fighter, read from the stats of the items it wears, merc_gear), rules::monster_blow per blow, Foe::take, and the real
// Fight::hurt_side (the heal-back and the life subtraction) on a merc with the case's life.
//   `--life CASES` reads tools/emu/merc_absorb.py's case lines (the layout of gear_absorb.py's, the Cyclone pool column holds
//   the hireling's class here, which d2d does not use) and prints `id life`: the merc's life after the blows, in 256ths;
//   `merc_absorb.py compare` checks it against game.exe's FUN_0057c6c0 run on a type-1 unit of that class. Needs the MPQs and
//   1.14d's tables (D2_MPQ_DIR, D2_PATCH_INSTALLER, as test_game); it skips without.
//   no argument: game.exe's lives for the pinned cases (merc_absorb.py game) through Fight::hurt_side.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <gear_absorb_case.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;
using gear_absorb::Case;
using gear_absorb::parse;
namespace fs = std::filesystem;

namespace {

// The merc's life in 256ths after the case's blows: one frame, one hurt_side, the merc wearing a torso piece that carries the
// case's resistances, magic damage reduction and absorbs. `gear_life` points of the case's `max` come from the piece's maximum
// life (stat 7) instead of the merc's level-up life: game.exe's heal caps at stat 7 whatever adds to it (FUN_00625d10).
std::int64_t life_after(World& world, const Case& one, std::uint32_t now, int gear_life = 0) {
    namespace s = d2d::d2s;
    auto& hero = *world.actor;
    hero.merc = Monster{};
    hero.merc->mode = "NU";
    hero.merc_st = {};
    hero.merc_st.level = 20;
    hero.merc_st.life = int(one.max_life) - gear_life;
    hero.merc_st.resist = 0;
    hero.character.merc_items.clear();
    s::Item armor;
    armor.location = s::item_location::kEquipped;
    armor.slot = s::body_location::kTorso;
    constexpr std::array<int, 3> kResistStats{ 39, 41, 43 };   // fire, lightning, cold (the stat after each: its maximum)
    for (std::size_t k = 0; k < 3; ++k) armor.props.push_back({ .stat = kResistStats[k], .value = int(one.res[k]) });
    armor.props.push_back({ .stat = s::kMagicDamageReduction, .value = int(one.mdr) });
    if (gear_life > 0) armor.props.push_back({ .stat = s::kMaxLife, .value = gear_life });
    constexpr std::array<int, 4> kPercent{ s::kAbsorbFirePercent, s::kAbsorbLightningPercent, s::kAbsorbColdPercent, s::kAbsorbMagicPercent };
    constexpr std::array<int, 4> kFlat{ s::kAbsorbFire, s::kAbsorbLightning, s::kAbsorbCold, s::kAbsorbMagic };
    for (std::size_t k = 0; k < 4; ++k) {                       // the case's columns are fire, lightning, cold, magic
        armor.props.push_back({ .stat = kPercent[k], .value = int(one.pct[k]) });
        armor.props.push_back({ .stat = kFlat[k], .value = int(one.flat[k]) });
    }
    hero.character.merc_items.push_back(std::move(armor));
    hero.merc->set_life_fine(int(one.life));
    std::array<Foe, 2> side{};
    side[1] = world.fight.merc_foe(now);
    for (const auto& blow : one.blows) {
        d2d::rules::MonStats attacker;
        for (std::size_t k = 0; k < blow.fields.size(); ++k)
            attacker.elements[k] = { blow.fields[k].first, 100, blow.fields[k].second, blow.fields[k].second, 0, "A1" };
        d2d::rules::Rng rng{ 1 };
        side[1].take(d2d::rules::monster_blow(side[1].fighter, 1, d2d::rules::Motion::standing, attacker, false, rng, false, true, true));
    }
    world.fight.hurt_side(side, now);
    return hero.merc->hit_points > 0 ? hero.merc->life_fine() : 0;
}

// game.exe's lives for these case lines (merc_absorb.py game, FUN_0057c6c0 on a type-1 unit of the hireling's class; V run), 256ths.
// Columns: id life max mdr res x 3, absorb percent x 4, flat x 4 (fire, lightning, cold, magic), class, blows.
// 0: 90 of 100 life, 20 % cold absorb, a 100-point cold blow: 90 + 20 -> 100, minus 80 = 20 points (a Desert Guard); 1: a fire absorb of
// 40 % and a flat 10 on 40 points (a Rogue Scout); 2: the order resistance (50), percent (30), flat (20) and the heal; 3: a heal that
// does not save the merc (dead: 0); 4: two blows in a frame, the cap taken per blow (30, not 20); 5: the magic reduction 3 and a
// 36 % magic absorb on a 2-point blow (a negative field heals nothing); 6: a life with a fraction (90.5), the cap taken; 7: a life above
// the max (120 of 100) brought down to it by a heal (100 minus 40 = 60); 8: three fields at once, the percent stopping at 40 (an
// Iron Wolf's class); 9: a 60 % absorb stopping at 40, max life 50, dead; 10: a one-point life, a flat absorb heals a blow it takes whole; 11: a life with a fraction (90.5) that the heal does not cap keeps it (90.5 + 2 - 8 = 84.5).
const std::vector<std::pair<std::string, std::int64_t>> kPins = {
    { "0 23040 100 0 0 0 0 0 0 20 0 0 0 0 0 338 1 1 2 100", 5120 },
    { "1 15360 100 0 0 0 0 40 0 0 0 10 0 0 0 271 1 1 0 40", 18432 },
    { "2 15360 100 0 50 0 0 30 0 0 0 20 0 0 0 359 1 1 0 100", 20480 },
    { "3 12800 100 0 0 0 0 0 0 20 0 0 0 0 0 338 1 1 2 100", 0 },
    { "4 25600 100 0 0 0 0 0 0 20 0 0 0 0 0 561 2 1 2 50 1 2 50", 7680 },
    { "5 10240 100 3 0 0 0 0 0 0 36 0 0 0 0 560 1 1 4 2", 10240 },
    { "6 23168 100 0 0 0 0 0 0 20 0 0 0 0 0 338 1 1 2 100", 5120 },
    { "7 30720 100 0 0 0 0 0 0 20 0 0 0 0 0 359 1 1 2 50", 15360 },
    { "8 17920 100 0 0 0 0 40 40 40 40 5 5 5 5 271 1 3 0 30 1 30 2 30", 15616 },
    { "9 7680 50 0 0 0 0 0 0 60 0 0 0 0 0 561 1 1 2 100", 0 },
    { "10 256 100 0 0 0 0 0 0 0 0 0 0 20 0 560 1 1 2 10", 2816 },
    { "11 23168 100 0 0 0 0 0 0 20 0 0 0 0 0 338 1 1 2 10", 21632 },
};

}   // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    const bool oracle = mode == "--life" && argc > 2;
    if (!oracle && argc > 1) {
        std::printf("SKIP: an oracle mode, `test_merc_absorb --life CASES`\n");
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
    d2d::rules::Rng made_rng(7);
    auto made = new_character(*data, d2d::d2s::kSorceress, "Absorbed", false, true, made_rng);
    Character hero;
    hero.character_class = d2d::d2s::kSorceress;
    hero.name = "Absorbed";
    hero.header = made.header;
    hero.stats = made.stats;
    hero.items = made.items;
    hero.panel = panel_stats(*data, hero.header, hero.items, hero.stats);
    World world(&*data, -1, -1);
    world.enter(hero);
    world.fight.update_fighters(1000 * kTickMs);
    const std::uint32_t now = 2000 * kTickMs;
    if (oracle) {
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "FAIL: cannot open %s\n", argv[2]); return 1; }
        std::string line;
        while (std::getline(cases, line)) {
            if (line.empty() || line[0] == '#') continue;
            Case one;
            if (!parse(line, one)) { std::fprintf(stderr, "FAIL: bad case line: %s\n", line.c_str()); return 1; }
            std::printf("%ld %lld\n", one.id, static_cast<long long>(life_after(world, one, now)));
        }
        return 0;
    }
    for (const auto& [line, life] : kPins) {
        Case one;
        const bool ok = parse(line, one);
        assert(ok);
        assert(life_after(world, one, now) == life);            // through Fight::hurt_side
    }
    // The cap is the merc's whole max life: 20 of pin 0's 100 points and 50 of pin 7's from its armor, the same lives.
    for (const auto& [pin, gear_life] : { std::pair<std::size_t, int>{ 0, 20 }, std::pair<std::size_t, int>{ 7, 50 } }) {
        Case one;
        const bool ok = parse(kPins[pin].first, one);
        assert(ok);
        assert(life_after(world, one, now, gear_life) == kPins[pin].second);
    }
    std::printf("ok\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
