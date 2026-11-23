// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Holy Bolt's party heal (hit 7, FUN_005a9fb0): TargetAlly (FUN_00554d20, the same party: local co-op
// is one) on another player it reaches heals calc1..calc2 whole points, capped at max life, and the bolt
// ends there. Through the real World: a Paladin casts at the second player standing in its way.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    namespace fs = std::filesystem;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    constexpr int kPaladin = 3;
    auto made = new_character(*data, kPaladin, "Healer", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Healer";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto friend_character = character;
    friend_character.name = "Friend";
    auto& ally = world.join(friend_character);
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor);
    world.arrive(moor, float(moor->ds1.width()) / 2, float(moor->ds1.height()) / 2, "a test");
    std::uint32_t now = 60 * kTickMs;
    for (int warm = 0; warm < 5; ++warm, now += kTickMs) world.tick({}, now + kTickMs, now);
    auto& healer = *world.actor;
    world.fight.monsters.clear();
    world.fight.skill_level = [](int) { return 10; };
    world.fight.skill_base = world.fight.skill_level;
    ally.player.x = healer.player.x + 3; ally.player.y = healer.player.y;
    auto& life = ally.character.stats.values[d2d::d2s::kLife];
    life = std::int64_t(1) << 8;
    healer.character.stats.values[d2d::d2s::kMana] = std::int64_t(500) << 8;
    const int holy_bolt = data->skills.by_name.at("Holy Bolt");
    assert(data->skills.get(holy_bolt)->target_ally);
    assert(world.fight.cast_missile(holy_bolt, ally.player.x, ally.player.y, now));
    const auto before = life;
    for (int tick = 0; tick < 60 && life == before; ++tick, now += kTickMs) world.tick({}, now + kTickMs, now);
    const auto& bolt = *data->skills.get(holy_bolt);
    const int low = d2d::rules::eval_calc(data->skills, bolt.calc[0], world.fight.calc_env(), holy_bolt, 10);
    assert(low > 0 && life - before >= std::int64_t(low) << 8 && life <= ally.character.stats.values[d2d::d2s::kMaxLife]);   // calc1 at least: the heal, not regeneration
    std::printf("OK: Holy Bolt healed the other player %lld -> %lld (256ths)\n", static_cast<long long>(before), static_cast<long long>(life));
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
