// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a monster's elemental and magic blows leave of a player's LIFE when the player's gear absorbs: the real
// rules::monster_blow per blow, Foe::take, and the real Fight::hurt_side (the heal-back, the Cyclone Armor pool, the
// life subtraction) on a player with the case's life.
//   `--life CASES` reads tools/emu/gear_absorb.py's case lines and prints `id life`: the player's life after the blows,
//   in 256ths; `gear_absorb.py compare` checks it against game.exe's FUN_0057c6c0. Needs the MPQs and 1.14d's tables
//   (D2_MPQ_DIR, D2_PATCH_INSTALLER, as test_game); it skips without.
//   no argument: game.exe's lives for the pinned cases (gear_absorb.py game) through Fight::hurt_side; the same cases
//   through the rules alone are test_gear_absorb_rules; a merc's are test_merc_absorb.

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

using namespace d2d::game;
using gear_absorb::Case;
using gear_absorb::parse;
namespace fs = std::filesystem;

namespace {

// The player's life in 256ths after the case's blows: one frame, one hurt_side. `cyclone` is the Cyclone Armor skill id.
std::int64_t life_after(World& world, int cyclone, const Case& one, std::uint32_t now) {
    auto& hero = *world.actor;
    auto& stats = hero.character.stats.values;
    hero.pmode = kModeNU;
    hero.self_states.clear();
    hero.absorb_pool = 0; hero.absorb_frac = 0; hero.absorb_skill = -1;
    stats[d2d::d2s::kLife] = one.life;
    stats[d2d::d2s::kMaxLife] = std::int64_t(one.max_life) << 8;
    if (one.pool > 0) {
        hero.self_states.push_back({ cyclone, 1, ~0u });
        hero.absorb_skill = cyclone;
        hero.absorb_pool = int(one.pool >> 8); hero.absorb_frac = int(one.pool & 255);
    }
    std::array<Foe, 2> side{};
    auto& fighter = side[0].fighter;
    fighter.mdr = int(one.mdr);
    for (std::size_t k = 0; k < 3; ++k) fighter.res[k] = int(one.res[k]);
    constexpr std::array<std::size_t, 4> kTypes{ 0, 1, 2, 4 };   // the case's columns are fire, lightning, cold, magic
    for (std::size_t k = 0; k < 4; ++k) { fighter.absorb_pct[kTypes[k]] = int(one.pct[k]); fighter.absorb_flat[kTypes[k]] = int(one.flat[k]); }
    for (const auto& blow : one.blows) {
        d2d::rules::MonStats attacker;
        for (std::size_t k = 0; k < blow.fields.size(); ++k)
            attacker.elements[k] = { blow.fields[k].first, 100, blow.fields[k].second, blow.fields[k].second, 0, "A1" };
        d2d::rules::Rng rng{ 1 };
        side[0].take(d2d::rules::monster_blow(fighter, 1, d2d::rules::Motion::standing, attacker, false, rng, false, true, true));
    }
    world.fight.hurt_side(side, now);
    return stats[d2d::d2s::kLife];
}

}   // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    const bool oracle = mode == "--life" && argc > 2;
    if (!oracle && argc > 1) {
        std::printf("SKIP: an oracle mode, `test_gear_absorb --life CASES`\n");
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
    const auto cyclone_row = data->skills.by_name.find("Cyclone Armor");
    assert(cyclone_row != data->skills.by_name.end());
    const int cyclone = int(cyclone_row->second);
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
            std::printf("%ld %lld\n", one.id, static_cast<long long>(life_after(world, cyclone, one, now)));
        }
        return 0;
    }
    // game.exe's lives for these case lines (tools/emu/gear_absorb.py game, FUN_0057c6c0 on a player), 256ths.
    // Columns: id life max mdr res x 3, absorb percent x 4, flat x 4 (fire, lightning, cold, magic), Cyclone pool, blows.
    const auto run = [&](const std::string& line) { Case one; const bool ok = parse(line, one); assert(ok); return life_after(world, cyclone, one, now); };
    for (const auto& [line, life] : gear_absorb::kPins) {
        Case one;
        const bool ok = parse(line, one);
        assert(ok);
        assert(run(line) == life);            // through Fight::hurt_side
    }
    std::printf("ok\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
