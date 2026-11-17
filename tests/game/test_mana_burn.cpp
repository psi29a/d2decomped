// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a Mana Burn monster's landed melee hit takes off a player's mana: the real monster_update on a zombie standing
// at a foe, its Mana Burn range (Monster::mana_lo / mana_hi, whole points, as make_boss sets them) given by the case,
// then the real Fight::hurt_side on a player with 60000 mana.
//   `--mana-burn CASES` reads `lo hi seed` lines and prints `lo hi seed fine`: the player's mana lost to the first landed
//   hit of that seed, in 256ths of a point, or -1 when no hit landed in the swings tried; tools/emu/mana_burn.py
//   `compare` checks it against game.exe. Needs the MPQs and 1.14d's tables (D2_MPQ_DIR, D2_PATCH_INSTALLER, as
//   test_game); it skips without.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
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
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kFullMana = std::int64_t{ 60000 } << 8;

// The mana, in 256ths, the first landed hit of a zombie with Mana Burn `least`..`most` takes off `world`'s player
// (monster_update, then Fight::hurt_side), or -1 when none of 40 swings landed.
std::int64_t burned(const GameData& data, World& world, int zombie, int least, int most, unsigned seed) {
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    const Crowd crowd;
    for (int swing = 0; swing < 40; ++swing) {
        std::vector<Missile> shots;
        std::array<Foe, 1> foes{};
        foes[0].x = 10.4f; foes[0].y = 10.f; foes[0].alive = true;
        foes[0].fighter.defense = 0;
        d2d::rules::Rng rng(seed + unsigned(swing)), spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(data, zombie, 10.f, 10.f, spawn, 0) };
        auto& monster = pack[0];
        monster.aware = true;
        monster.mana_lo = least; monster.mana_hi = most;
        const std::uint32_t start = 1000 * kTickMs;
        set_mode(data, monster, "A1", start);
        for (std::uint32_t now = start; now < monster.mode_until + 4 * kTickMs; now += kTickMs) {
            monster_update(data, here, monster, foes, rng, now, 0.04f, crowd, shots, pack);
            if (foes[0].mana_burn <= 0 && foes[0].hits <= 0) continue;
            auto& mana = world.actor->character.stats.values[d2d::d2s::kMana];
            mana = kFullMana;
            std::array<Foe, 2> side{};
            side[0].mana_burn = foes[0].mana_burn;
            world.fight.hurt_side(side, now);
            return kFullMana - mana;
        }
    }
    return -1;
}

}   // namespace

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--mana-burn") {
        std::printf("SKIP: an oracle mode, `test_mana_burn --mana-burn CASES`\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(zombie != data->monsters.by_id.end());
    d2d::rules::Rng made_rng(7);
    auto made = new_character(*data, d2d::d2s::kSorceress, "Burnt", false, true, made_rng);
    Character hero;
    hero.character_class = d2d::d2s::kSorceress;
    hero.name = "Burnt";
    hero.header = made.header;
    hero.stats = made.stats;
    hero.items = made.items;
    hero.panel = panel_stats(*data, hero.header, hero.items, hero.stats);
    World world(&*data, -1, -1);
    world.enter(hero);
    world.fight.update_fighters(1000 * kTickMs);
    world.actor->character.stats.values[d2d::d2s::kMaxMana] = kFullMana;
    std::ifstream cases(argv[2]);
    if (!cases) { std::fprintf(stderr, "FAIL: cannot open %s\n", argv[2]); return 1; }
    int least = 0, most = 0;
    unsigned seed = 0;
    while (cases >> least >> most >> seed)
        std::printf("%d %d %u %lld\n", least, most, seed, static_cast<long long>(burned(*data, world, int(zombie->second), least, most, seed)));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
