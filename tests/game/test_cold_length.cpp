// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--cold-length SEED N`: a cold missile on one monster through the REAL
// missile_blow and Fight::land (resistance, pierce, the case's MonStats
// coldeffect, MonsterColdDivisor, a running chill), the cases of
// tools/emu/chill_length.py (game.exe's FUN_0057c6c0 -> FUN_0057af80):
// the ticks from now the chill ends, or `none`, one line each, to diff.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 4 || std::string_view(argv[1]) != "--cold-length") {
        std::printf("SKIP: an oracle mode, run `test_cold_length --cold-length SEED N` (see tools/emu/chill_length.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    std::uint32_t state = std::uint32_t(std::atoi(argv[2]));
    if (state == 0) state = 1;
    const auto next = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    auto& cold_type = const_cast<d2d::rules::MonType&>(data->monsters.types[fallen->second]);
    const auto real_diff = cold_type.diff;
    World world(&*data, -1, -1);
    world.enter(character);
    d2d::rules::Rng spawn_rng{ 5 };
    const Monster fallen_made = make_monster(*data, int(fallen->second), world.actor->player.x, world.actor->player.y, spawn_rng, 0);
    d2d::rules::Rng blow_rng{ 3 };
    const std::uint32_t now = 1000 * kTickMs;
    const auto run = [&](const std::string& tag, int roll, int length, int res, int pierce, int cold_effect, int difficulty, int prior) {
        std::printf("%s %d %d %d %d %d %d %d ", tag.c_str(), roll, length, res, pierce, cold_effect, difficulty, prior);
        cold_type.diff[std::size_t(difficulty)].cold_effect = cold_effect;
        Monster monster = fallen_made;
        monster.difficulty = difficulty;
        monster.stats.hit_points = monster.hit_points = 100000;
        monster.life_frac = 0;
        monster.boss_res = {};
        monster.boss_res[4] = res - monster.target(*data).res[4];
        monster.chill_until = prior > 0 ? now + std::uint32_t(prior) * kTickMs : 0;
        world.fight.monsters.clear();
        world.fight.add_monster(monster);
        const auto target = world.fight.target_of(0);
        const auto blow = d2d::rules::missile_blow({ .etype = 2, .elo = roll, .ehi = roll, .elen = length }, target, { 0, 0, pierce, 0 }, blow_rng);
        world.fight.land(0, blow, true, now, -1);
        const std::uint32_t until = world.fight.monsters.back().chill_until;
        if (until > now) std::printf("%u%s\n", (until - now) / kTickMs, target.res[4] == res ? "" : " RESFAIL");
        else std::printf("none%s\n", target.res[4] == res ? "" : " RESFAIL");
        cold_type.diff = real_diff;
    };
    constexpr int kLengths[12] = { 0, 1, 2, 3, 4, 5, 7, 50, 100, 101, 150, 375 };
    int id = 0;
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        for (const int cold_effect : { -50, -1, 0, 25 }) {
            for (const int res : { -150, -100, -99, -50, -1, 0, 1, 25, 50, 75, 90, 99, 100, 101, 150 })
                for (const int roll : { 1, 127, 128, 255, 256, 257, 511, 512, 1024 })
                    for (const int length : kLengths) run("B" + std::to_string(id++), roll, length, res, 0, cold_effect, difficulty, 0);
            for (const int prior : { 1, 37, 1000 })
                for (const int res : { 0, 75 })
                    for (const int roll : { 128, 512 })
                        for (const int length : kLengths) run("B" + std::to_string(id++), roll, length, res, 0, cold_effect, difficulty, prior);
        }
        for (const int pierce : { 1, 40, 100 })
            for (const int res : { 50, 99, 100, 150 })
                for (const int roll : { 128, 256 })
                    for (const int length : { 50, 150 }) run("B" + std::to_string(id++), roll, length, res, pierce, -50, difficulty, 0);
    }
    constexpr int kEffects[8] = { -50, -50, -50, -33, -25, 0, 10, -100 };
    for (int i = 0; i < std::atoi(argv[3]); ++i) {
        const std::uint32_t wide = next(), which = next();
        const int roll = 1 + int(wide % (which % 2 ? 0x800u : 0x4000u));
        const int length = next() % 10 == 0 ? 0 : 1 + int(next() % 500);
        const std::uint32_t bucket = next() % 100;
        int res;
        if (bucket < 10) res = 100 + int(next() % 51);
        else if (bucket < 25) res = -int(next() % 151);
        else res = int(next() % 100);
        const int pierce = res >= 100 || next() % 4 ? 0 : int(next() % 61);
        const int cold_effect = next() % 4 ? kEffects[next() % 8] : int(next() % 201) - 100;
        const int difficulty = int(next() % 3);
        const int prior = next() % 10 < 7 ? 0 : 1 + int(next() % 600);
        run("R" + std::to_string(i), roll, length, res, pierce, cold_effect, difficulty, prior);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
