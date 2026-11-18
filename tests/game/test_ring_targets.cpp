// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A ring's missiles each get a target point (Missile::to) at the start's
// subtile plus the ring's offset: FUN_0059fa30's flag 2, as FUN_0056d400
// (the novas), FUN_005ccd10 (PrimePoisonNova), FUN_005af030 (Frozen Orb's
// spin) and FUN_005abb00 (hit 29) pass it. With no point, missile_fly aims
// every one at the start plus (1, 1), so the ring flies off as one clump.
// Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <montypes.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

// The missile aims at its start's subtile plus (offset_x, offset_y).
bool aims_at(const Missile& missile, int offset_x, int offset_y) {
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return missile.to && subtile(missile.to->first) == subtile(missile.x) + offset_x && subtile(missile.to->second) == subtile(missile.y) + offset_y;
}

// The missile starts on the subtile of (x, y).
bool on_subtile(const Missile& missile, float x, float y) {
    const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
    return subtile(missile.x) == subtile(x) && subtile(missile.y) == subtile(y);
}

// The last 64 of `made` start on the subtile of (origin_x, origin_y) and aim
// round the nova's ring (0x6e1288 / 0x6e1388) from it.
bool nova_ring(std::span<const Missile> made, float origin_x, float origin_y) {
    if (made.size() < 64) return false;
    const auto ring = made.last(64);
    for (std::size_t k = 0; k < 64; ++k)
        if (!on_subtile(ring[k], origin_x, origin_y) || !aims_at(ring[k], d2d::rules::kNovaRing[k][0], d2d::rules::kNovaRing[k][1])) return false;
    return true;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }

    // A monster's nova (do 22, FUN_005c9b50 -> FUN_0056d400): the Summoner's Frost Nova.
    {
        const int summoner_row = data->monsters.row("summoner");
        assert(summoner_row >= 0);
        Level here;
        here.id = d2d::rules::level_ids::kArcaneSanctuary;
        here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
        here.walk.assign(80 * 80, 0);
        const Crowd crowd;
        std::vector<Missile> shots;
        std::array<Foe, 1> player{};
        player[0].x = 10.1f; player[0].y = 8.1f;
        d2d::rules::Rng rng{ 7 };
        std::vector<Monster> pack{ make_monster(*data, summoner_row, 8.3f, 8.7f, rng, 0) };
        pack[0].aware = true;
        pack[0].skill = data->skills.by_name.at("Frost Nova");
        set_mode(*data, pack[0], "SC", 0);
        for (std::uint32_t frame = 1; frame <= 100 && shots.size() < 64; ++frame)
            monster_update(*data, here, pack[0], player, rng, frame * 40, 0.04f, crowd, shots, pack);
        assert(shots.size() == 64 && shots.front().info->name == "frostnova");
        assert(nova_ring(shots, pack[0].unit.x, pack[0].unit.y));
        std::puts("OK: a monster's Frost Nova aims round its ring");
    }

    // Arcane Tower (do 106, FUN_005cd870 -> FUN_0056d400): with a target, the nova of its srvmissilea.
    {
        const auto& types = data->monsters.types;
        const auto tower = std::ranges::find(types, std::string_view("ArcaneTower"), &d2d::rules::MonType::ai_name);
        assert(tower != types.end());
        Level here;
        here.id = d2d::rules::level_ids::kArcaneSanctuary;
        here.ds1 = d2d::ds1::Map(16, 16, 1, 1);
        here.walk.assign(80 * 80, 0);
        const Crowd crowd;
        std::vector<Missile> shots;
        std::array<Foe, 1> player{};
        player[0].x = 10.1f; player[0].y = 8.1f;
        d2d::rules::Rng rng{ 7 };
        std::vector<Monster> pack{ make_monster(*data, int(std::distance(types.begin(), tower)), 8.3f, 8.7f, rng, 0) };
        pack[0].aware = true;
        for (std::uint32_t frame = 1; frame <= 200 && shots.size() < 64; ++frame)
            monster_update(*data, here, pack[0], player, rng, frame * 40, 0.04f, crowd, shots, pack);
        assert(shots.size() == 64);
        assert(nova_ring(shots, pack[0].unit.x, pack[0].unit.y));
        std::puts("OK: Arcane Tower's nova aims round its ring");
    }

    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 1, "Ringed", false, true, made_rng);
    Character character;
    character.character_class = 1;
    character.name = "Ringed";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(world.level && !world.level->rooms.empty());
    auto& fight = world.fight;

    // A charge release's nova (prg 36, FUN_005d4db0 -> FUN_0056d400): round the target.
    {
        const d2d::rules::Skill* released = nullptr;
        int count = 0;
        for (const auto& row : data->skills.rows)
            for (int k = 0; k < 3 && !released; ++k)
                if (row.prgfunc[std::size_t(k)] == d2d::rules::ServerDoFunction::kReleaseNova) { released = &row; count = k + 1; }
        assert(released);
        fight.missiles.clear();
        fight.prg(*released, d2d::rules::ServerDoFunction::kReleaseNova, count, 1, 100.3f, 100.7f, 1000);
        assert(fight.missiles.size() == 64);
        assert(nova_ring(fight.missiles, 100.3f, 100.7f));
        fight.missiles.clear();
        std::printf("OK: %s's release nova aims round its ring\n", released->name.c_str());
    }

    // Cold Enchanted's death (MonUMod 18, FUN_005a2bd0 -> FUN_0056d400): coldunique round it.
    {
        const int fallen_row = data->monsters.row("fallen1");
        assert(fallen_row >= 0);
        d2d::rules::Rng rng{ 3 };
        Monster dying = make_monster(*data, fallen_row, 100.3f, 100.7f, rng, 0);
        dying.boss = d2d::rules::Boss::unique;
        dying.mods = { d2d::rules::umod::cold };
        dying.hit_points = 0;
        set_mode(*data, dying, "DT", 2000);
        fight.monsters = { std::move(dying) };
        fight.pending.clear();
        fight.boss_events({}, 2000);
        fight.boss_events({}, 2000 + 4 * 40);
        assert(fight.pending.size() == 64 && fight.pending.front().info->name == "coldunique");
        assert(nova_ring(fight.pending, 100.3f, 100.7f));
        fight.pending.clear();
        fight.monsters.clear();
        std::puts("OK: Cold Enchanted's death nova aims round its ring");
    }

    // A Shadow's death nova (MonUMod 42, FUN_005a2910 -> FUN_0056d400).
    {
        const int shadow_row = data->monsters.row("shadowwarrior");
        const auto* shadow_skill = data->skills.get(268);
        assert(shadow_row >= 0 && shadow_skill);
        d2d::rules::Rng rng{ 3 };
        auto& pets = world.actor->pets;
        pets.clear();
        auto& pet = pets.emplace_back();
        pet.monster = make_monster(*data, shadow_row, 100.3f, 100.7f, rng, 0);
        pet.monster.mods = { shadow_skill->sumumod };
        pet.where = world.level;
        pet.monster.hit_points = 0;
        set_mode(*data, pet.monster, "DT", 3000);
        fight.pending.clear();
        assert(fight.golem_blast(pet, 3000 + 4 * 40));
        assert(fight.pending.size() == 64);
        assert(nova_ring(fight.pending, 100.3f, 100.7f));
        fight.pending.clear();
        pets.clear();
        std::puts("OK: a Shadow's death nova aims round its ring");
    }

    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_ring_targets: %s\n", error.what());
    return 1;
}
