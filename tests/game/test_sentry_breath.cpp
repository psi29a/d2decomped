// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Inferno Sentry's breath (mon inferno sentry, do 95): its start (FUN_005cc240) sets its end calc2
// frames on and state 0xc; FUN_005cc4e0 makes one flame at the target, its range calc1 (else Param2 +
// level - 1, 1..255), and runs again calc3 frames on till the end. Not eight at once. The REAL
// Fight::trap_fire and Fight::pet_breath. Needs the game data.
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
#include <skills.hpp>
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
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAssassin, "Trapper", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAssassin;
    character.name = "Trapper";
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
    const auto& breath = *data->skills.get(data->skills.by_name.at("mon inferno sentry"));
    assert(breath.srvdofunc == d2d::rules::ServerDoFunction::kMonsterBreath);
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    fight.monsters.clear();
    fight.monsters.push_back(make_monster(*data, int(zombie - data->monsters.types.begin()), actor->player.x + 3.f, actor->player.y, rng, 0, true, 1, open.id));
    fight.monsters.back().hit_points = fight.monsters.back().stats.hit_points = 100000;
    Fight::Pet sentry;
    sentry.monster.unit.x = actor->player.x; sentry.monster.unit.y = actor->player.y;
    sentry.monster.hit_points = 1;
    sentry.shot_skill = breath.id; sentry.shot_level = 5;
    actor->pets.push_back(std::move(sentry));
    auto& pet = actor->pets.back();
    const int lasts = std::max(fight.calc(breath, breath.calc[1], 5), 1), every = std::max(fight.calc(breath, breath.calc[2], 5), 1);
    std::uint32_t now = 1000 * kTickMs;
    fight.missiles.clear();
    fight.trap_fire(pet, 0, now);
    assert(fight.missiles.size() == 1 && pet.monster.breath == breath.id && pet.monster.breath_until == now + std::uint32_t(lasts) * 40);
    std::size_t flames = 1;
    for (int frame = 1; frame <= lasts + 2; ++frame) {
        fight.pet_breath(pet, now + std::uint32_t(frame) * 40);
        if (frame < lasts && frame % every == 0) ++flames;
    }
    std::printf("the breath at level 5: %d frames, a flame every %d: %zu flames\n", lasts, every, fight.missiles.size());
    assert(fight.missiles.size() == flames && pet.monster.breath < 0);
    std::puts("OK: the Inferno Sentry breathes one flame every calc3 frames till its end");
    // While it breathes its think doesn't run (the breath holds its mode): no shot is spent till the breath ends
    // (CodeRabbit on #815). A real Inferno Sentry, the world's own pets_turn.
    actor->pets.clear();
    const auto& wake = *data->skills.get(data->skills.by_name.at("Inferno Sentry"));
    fight.skill_level = [&](int id) { return id == wake.id ? 5 : 0; };
    fight.summon(wake, 5, actor->player.x + 1.f, actor->player.y, now);
    assert(!actor->pets.empty());
    auto& trap = actor->pets.back();
    assert(trap.shot_skill == breath.id);                       // its Skill1's breath, not Fire Trauma's bombs (trap_shot)
    std::puts("OK: a summoned Inferno Sentry breathes its Skill1");

    return 0;
} catch (const std::exception& error) {
    std::printf("test_sentry_breath: %s\n", error.what());
    return 1;
}
