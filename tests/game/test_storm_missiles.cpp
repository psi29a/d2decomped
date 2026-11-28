// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Firestorm (do 117, FUN_005c7160) and Twister / Tornado (FUN_005c72f0 -> FUN_005c7040): Firestorm one
// missile straight at the point (FUN_0056ecb0), then calc1 - 1; Twister calc1. Each of those is made with
// the create callback FUN_005c9290, Charged Bolt's: it wanders on path type 10 from {target x + its index,
// 666}, its index from 0 (Missile::bolt), all aimed at the point. The REAL Fight::fire. Needs the game data.
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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string_view>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kDruid, "Stormy", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kDruid;
    character.name = "Stormy";
    character.header = made.header;
    character.stats = made.stats;
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    auto& fight = world->fight;
    auto* actor = world->actor;
    fight.skill_level = [](int) { return 10; };
    actor->cast_x = actor->player.x + 4.f; actor->cast_y = actor->player.y;
    for (const char* name : { "Firestorm", "Twister", "Tornado" }) {
        const auto& skill = *data->skills.get(data->skills.by_name.at(name));
        const int calc1 = fight.calc(skill, skill.calc[0], 10);
        fight.pending.clear();
        fight.fire(skill, 1000 * kTickMs, fight.player_caster(skill.id));
        const bool firestorm = std::string_view(name) == "Firestorm";
        const auto wandering = std::ranges::count_if(fight.pending, [](const Missile& one) { return one.bolt >= 0; });
        std::printf("%s at level 10: calc1 %d, %zu made, %td wandering\n", name, calc1, fight.pending.size(), wandering);
        assert(calc1 > 0 && int(fight.pending.size()) == calc1 && wandering == calc1 - (firestorm ? 1 : 0));
        for (std::size_t k = 0; k < fight.pending.size(); ++k) {
            const auto& one = fight.pending[k];
            assert(one.aim_at && one.aim_at->first == actor->cast_x && one.aim_at->second == actor->cast_y);   // all at the point, none turned
            if (firestorm) assert(one.bolt == int(k) - 1);
            else assert(one.bolt == int(k));
        }
    }
    std::puts("OK: Firestorm and Twister wander as Charged Bolt's, one straight first for Firestorm");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_storm_missiles: %s\n", error.what());
    return 1;
}
