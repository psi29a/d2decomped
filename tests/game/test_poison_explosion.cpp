// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Poison Explosion (do 63, FUN_005c5e60 -> FUN_005a9370) through the REAL Fight::spot: a corpse sends
// poisonexplosioncloud the 8 even ways at its Param1 (2) << 7, x 75 / 100, each its Range (60) frames, from the
// corpse's subtile. Each against game.exe's own, tools/emu/poison_explosion.py.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(*data, d2d::d2s::kNecromancer, "Corpses", false, true, rng);
    character.character_class = d2d::d2s::kNecromancer;
    character.name = "Corpses";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    world->level = &open;
    world->fight.enter(&open);
    auto& fight = world->fight;
    const std::uint32_t now = 1000 * kTickMs;
    const auto& skill = *data->skills.get(data->skills.by_name.at("Poison Explosion"));
    const auto& cloud = data->missiles.at(skill.srvmissilea);

    // game.exe's own (tools/emu/poison_explosion.py), in the order made.
    struct Made { int skill, level, unit, returned, state, row, flags, way_x, way_y, velocity, life; };
    static constexpr Made kMade[] = {
#include "poison_explosion_cases.inc"
    };
    for (const int level : { 1, 10, 20 }) {
        fight.monsters.clear();
        fight.pending.clear();
        Monster corpse;
        corpse.type = int(data->monsters.by_id.at("zombie1"));
        corpse.unit.x = 30.1f;
        corpse.unit.y = 40.7f;
        corpse.hit_points = 0;
        corpse.mode = "DD";
        fight.monsters.push_back(corpse);
        fight.skill_level = [&](int id) { return id == skill.id ? level : 0; };
        world->actor->cast_x = corpse.unit.x;
        world->actor->cast_y = corpse.unit.y;
        fight.spot(skill, now, fight.player_caster(skill.id));
        assert(fight.monsters[0].corpse_used);
        std::size_t next = 0;
        for (const auto& ring : kMade) {
            if (ring.skill != skill.id || ring.level != level || ring.unit != 2) continue;
            assert(ring.state == 0x76 && ring.returned == 1 && ring.row == int(cloud.id) && ring.life == 0);
            assert(next < fight.pending.size());
            const auto& got = fight.pending[next++];
            assert(got.info == &cloud);
            const float speed = std::hypot(got.velocity_x, got.velocity_y), span = std::hypot(float(ring.way_x), float(ring.way_y));
            assert(std::abs(speed - missile_speed(ring.velocity * 75 / 100)) < 1e-4f);
            assert(std::abs(got.velocity_x / speed - float(ring.way_x) / span) < 1e-4f && std::abs(got.velocity_y / speed - float(ring.way_y) / span) < 1e-4f);
            assert(std::abs(got.x - (std::floor(corpse.unit.x * 5) + 0.5f) / 5) < 1e-4f && std::abs(got.y - (std::floor(corpse.unit.y * 5) + 0.5f) / 5) < 1e-4f);
            assert(got.dies == now + std::uint32_t((cloud.range + cloud.lev_range * level) * 40));
        }
        assert(next == 8 && next == fight.pending.size());
    }
    std::printf("OK: Poison Explosion sends 8 clouds out from the corpse as game.exe's (FUN_005c5e60 -> FUN_005a9370)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_poison_explosion: %s\n", error.what());
    return 1;
}
