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

using namespace d2d::game;
using gear_absorb::Case;
using gear_absorb::parse;
namespace fs = std::filesystem;

namespace {

// The merc's life in 256ths after the case's blows: one frame, one hurt_side, the merc wearing a torso piece that carries the
// case's resistances, magic damage reduction and absorbs (and no maximum life of its own: the merc's level-up life is `max`).
std::int64_t life_after(World& world, const Case& one, std::uint32_t now) {
    namespace s = d2d::d2s;
    auto& hero = *world.actor;
    hero.merc = Monster{};
    hero.merc->mode = "NU";
    hero.merc_st = {};
    hero.merc_st.level = 20;
    hero.merc_st.life = int(one.max_life);
    hero.merc_st.resist = 0;
    hero.character.merc_items.clear();
    s::Item armor;
    armor.location = s::item_location::kEquipped;
    armor.slot = s::body_location::kTorso;
    constexpr std::array<int, 3> kResistStats{ 39, 41, 43 };   // fire, lightning, cold (the stat after each: its maximum)
    for (std::size_t k = 0; k < 3; ++k) armor.props.push_back({ .stat = kResistStats[k], .value = int(one.res[k]) });
    armor.props.push_back({ .stat = s::kMagicDamageReduction, .value = int(one.mdr) });
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
    std::printf("ok\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
