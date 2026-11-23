// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a Mana Burn monster's landed melee hit takes off a player's mana: the real monster_update on a zombie standing
// at a foe, its Mana Burn range (Monster::mana_lo / mana_hi, whole points, as make_boss sets them) given by the case,
// then the real Fight::hurt_side on a player with 60000 mana.
//   `--mana-burn CASES` reads `lo hi seed` lines and prints `lo hi seed fine`: the player's mana lost to the first landed
//   hit of that seed, in 256ths of a point, or -1 when no hit landed in the swings tried; tools/emu/mana_burn.py
//   `compare` checks it against game.exe. Needs the MPQs and 1.14d's tables (D2_MPQ_DIR, D2_PATCH_INSTALLER, as
//   test_game); it skips without.
//   `--mana-burn-roll CASES`: the same lines from rules::mana_burn_fine alone, on the seed state game.exe rolls it from
//   (`compare GAME ROLL exact`: byte for byte).
//   no argument: game.exe's amounts for a few cases (mana_burn.py game) from the rule, and with the data the player's
//   mana after a burn through Fight::hurt_side and a Normal level 9 unique's hit (4..7 points: 1,024 to 1,792 mana).

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <uniques.hpp>
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

// rules::mana_burn_fine on the seed state game.exe's FUN_0057a880 rolls from: the attacker's unit seed {seed, 666} after one draw.
int roll(int least, int most, unsigned seed) {
    d2d::rules::Rng draw;
    draw.low = seed; draw.high = 666;
    draw.next();
    return d2d::rules::mana_burn_fine(least, most, draw);
}

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
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    if (mode == "--mana-burn-roll" && argc > 2) {
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "FAIL: cannot open %s\n", argv[2]); return 1; }
        int least = 0, most = 0;
        unsigned seed = 0;
        while (cases >> least >> most >> seed) std::printf("%d %d %u %d\n", least, most, seed, roll(least, most, seed));
        return 0;
    }
    const bool oracle = mode == "--mana-burn" && argc > 2;
    if (!oracle && argc > 1) {
        std::printf("SKIP: an oracle mode, `test_mana_burn --mana-burn CASES`\n");
        return D2D_TEST_SKIPPED;
    }
    if (!oracle) {   // game.exe's own amounts (mana_burn.py game): `least << 16` up to `most << 16` in 256ths, a power-of-two bound masked
        for (const auto& [least, most, seed, fine] : { std::array{ 1, 3, 7, 75187 }, { 4, 7, 7, 271795 }, { 10, 10, 7, 655360 }, { 100, 150, 7, 6825395 },
                                                  { 1, 2, 5007, 70177 }, { 0, 5, 5007, 70177 }, { 60, 111, 5007, 7213601 }, { 0, 0, 7, 0 } })
            assert(roll(least, most, unsigned(seed)) == fine);
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return oracle ? D2D_TEST_SKIPPED : 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return oracle ? D2D_TEST_SKIPPED : 0; }
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
    if (!oracle) {
        // FUN_0057aa60: the player's mana (256ths) less the burn; a burn over the pool leaves 0.
        auto& mana = world.actor->character.stats.values[d2d::d2s::kMana];
        for (const auto& [before, burn, after] : { std::array<std::int64_t, 3>{ 2000 << 8, 271795, (2000 << 8) - 271795 }, { 1000 << 8, 6825395, 0 } }) {
            mana = before;
            std::array<Foe, 2> side{};
            side[0].mana_burn = int(burn);
            world.fight.hurt_side(side, 2000 * kTickMs);
            assert(mana == after);
        }
        // A Normal level 9 unique (MonLvl damage 7 x MonUMod 66 % / 100 %: 4..7 points) takes 1,024 to 1,792 mana a hit.
        const auto fine = burned(*data, world, int(zombie->second), 4, 7, 7);
        assert(fine >= std::int64_t{ 4 } << 16 && fine < std::int64_t{ 7 } << 16);
        std::printf("ok\n");
        return 0;
    }
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
