// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--static-field SEED N`: Static Field's hit on one monster (life,
// resistance, pierce, floor, calc1, calc2), the cases of
// tools/emu/static_field.py (game.exe's FUN_005c96a0 -> FUN_0057c6c0), one
// line each, to diff. Three results (life after, 256ths), as the oracle prints:
//   spot   the monster's life after the REAL Fight::spot casts Static Field in
//          a World (the caster's pierce, the difficulty and expansion floor,
//          the skill's calc1 / calc2 in its loaded row);
//   second a second monster in the same cast (the previous case's life, max
//          life and resistance), the per-unit loop;
//   inner  the arithmetic alone (static_field_fine + resisted_fine, or `skip`).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;

int main(int argc, char** argv) try {
    if (argc < 4 || std::string_view(argv[1]) != "--static-field") {
        std::printf("SKIP: an oracle mode, run `test_static_field --static-field SEED N` (see tools/emu/static_field.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());

    std::uint32_t state = std::uint32_t(std::atoi(argv[2]));
    if (state == 0) state = 1;
    const auto next = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
    d2d::rules::Rng rng{ 3 };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    const Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 4, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 4;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    character.header.status |= 0x20;   // an expansion game: StaticFieldMin counts
    World world(&*data, -1, -1);
    world.enter(character);
    const World::Player start = *world.actor;
    const auto skill_row = data->skills.by_name.find("Static Field");
    assert(skill_row != data->skills.by_name.end());
    // Fight reads the skill's calc1 / calc2 from the loaded table, so the cases' values go into its row.
    auto& skill = *std::ranges::find(data->skills.rows, skill_row->second, &d2d::rules::Skill::id);
    int prev_hp = 0, prev_life = 0, prev_res = 0;
    const auto run = [&](const std::string& tag, int max_hp, int life, int res, int pierce, int floor_pct, int pct, int low) {
        if (prev_hp == 0) { prev_hp = max_hp; prev_life = life; prev_res = res; }
        *world.actor = start;
        world.actor->psum[d2d::d2s::kPassiveLightningPierce] = pierce;
        skill.par[3] = pct;
        skill.par[2] = low;
        data->static_field_min[0] = floor_pct;
        world.fight.game_difficulty = 0;
        world.fight.monsters.clear();
        bool res_ok = true;
        const auto place = [&](int life_max, int fine_life, int raw_res, float dx, float dy) {
            Monster monster = made;
            monster.unit.x = world.actor->player.x + dx;
            monster.unit.y = world.actor->player.y + dy;
            monster.stats.hit_points = life_max;
            monster.hit_points = (fine_life + 255) >> 8;
            monster.life_frac = monster.hit_points * 256 - fine_life;
            monster.boss_res = {};
            monster.boss_res[3] = raw_res - monster.target(*data).res[3];
            world.fight.add_monster(std::move(monster));
            res_ok = res_ok && world.fight.monsters.back().target(*data).res[3] == raw_res;
        };
        place(max_hp, life, res, 0.4F, 0);
        place(prev_hp, prev_life, prev_res, 0, 0.4F);
        const Fight::Caster caster{ world.actor->player.x, world.actor->player.y, 1 + int(life % 20), -1, 0, 0 };
        world.fight.spot(skill, 1000 * kTickMs, caster);
        const auto after = [&](std::size_t index) {
            const auto& struck = world.fight.monsters[index];
            return struck.alive() ? struck.hit_points * 256 - struck.life_frac : 0;
        };
        std::printf("%s %d %d %d %d %d %d %d %d %d%s ", tag.c_str(), max_hp, life, res, pierce, floor_pct, pct, low, after(0), after(1), res_ok ? "" : " RESFAIL");
        prev_hp = max_hp; prev_life = life; prev_res = res;
        Monster monster = made;
        monster.stats.hit_points = max_hp;
        monster.hit_points = (life + 255) >> 8;
        monster.life_frac = monster.hit_points * 256 - life;
        const int fine = d2d::rules::static_field_fine(life, max_hp << 8, pct, low, floor_pct, res);
        if (fine < 0) { std::printf("skip\n"); return; }
        const int got = d2d::rules::resisted_fine(fine, res < 100 ? std::max(res - pierce, -100) : res);
        hurt(*data, monster, got >> 8, 0, got & 255);
        std::printf("%d\n", monster.alive() ? monster.hit_points * 256 - monster.life_frac : 0);
    };
    int id = 0;
    for (const int floor_pct : { 0, 33, 50 })
        for (const int res : { -150, -100, -99, -50, -1, 0, 1, 33, 50, 99, 100, 101, 150 })
            for (int max_hp = 1; max_hp <= 6; ++max_hp)
                for (int life = 1; life <= max_hp << 8; ++life) run("B" + std::to_string(id++), max_hp, life, res, 0, floor_pct, 25, 0);
    for (int i = 0; i < std::atoi(argv[3]); ++i) {
        const std::uint32_t wide = next(), which = next();
        const int max_hp = 1 + int(wide % (which % 4 == 0 ? 3000u : 300u));
        const int life = 1 + int(next() % std::uint32_t(max_hp << 8));
        const std::uint32_t bucket = next() % 100;
        int res;
        if (bucket < 10) res = 100 + int(next() % 51);
        else if (bucket < 30) res = -int(next() % 201);
        else res = int(next() % 100);
        const int pierce = res >= 100 || next() % 4 ? 0 : int(next() % 61);
        const int any_floor = int(next() % 101);
        const int floors[4] = { 0, 33, 50, any_floor };
        const int floor_pct = floors[next() % 4];
        const int pct = next() % 4 ? 25 : int(next() % 151);
        const int low = next() % 4 ? 0 : int(next() % 0x2000);
        run("R" + std::to_string(i), max_hp, life, res, pierce, floor_pct, pct, low);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
