// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Venom's self-state stats join the player, and FUN_0057b7d0 uses stat 101
// (skill_poison_override_length) instead of stat 59 for the melee record.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR (the game-data check)\n"); return 0; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (the game-data check)\n"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (data && !data->patched) { std::printf("SKIP: no 1.14d patch data (the game-data check)\n"); return 0; }
    assert(data && data->patched);

    constexpr int kLevel = 5;
    const int venom = data->skills.by_name.at("Venom");
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, d2d::d2s::kAssassin, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAssassin;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    fight.skill_level = [venom](int skill) { return skill == venom ? kLevel : 0; };
    constexpr std::uint32_t kNow = 2000 * kTickMs;
    world.actor->self_states.push_back({ venom, kLevel, kNow + 1000 * kTickMs });

    fight.update_fighters(kNow);
    const auto& stats = world.actor->psum;
    const int length = int(stats[d2d::d2s::kPoisonOverrideLength]);
    assert(length > 0 && stats[d2d::d2s::kPoisonLength] == 0 && stats[d2d::d2s::kPassivePoisonMastery] == 0);
    assert(stats[d2d::d2s::kPoisonMinDamage] > 0 && stats[d2d::d2s::kPoisonMaxDamage] >= stats[d2d::d2s::kPoisonMinDamage]);
    const auto expected = std::pair{ int(stats[d2d::d2s::kPoisonMinDamage] * length / 256),
                                     int(stats[d2d::d2s::kPoisonMaxDamage] * length / 256) };
    assert(world.actor->player_combat.poison_len == length);
    assert(world.actor->player_combat.elem[3] == expected && expected.second > 0);
    std::printf("OK: Venom contributes %d-%d poison over %d ticks\n", expected.first, expected.second, length);
    // Without Venom, two poison mods' lengths over their poison_count (326, one each; 0x57bb9b..).
    world.actor->self_states.clear();
    for (const int len : { 100, 50 }) {
        d2d::d2s::Item charm;
        charm.code = "cm1"; charm.panel = 1; charm.identified = true;
        charm.props = { { d2d::d2s::kPoisonMinDamage, 0, 256 }, { d2d::d2s::kPoisonMaxDamage, 0, 256 }, { d2d::d2s::kPoisonLength, 0, len }, { d2d::d2s::kPoisonCount, 0, 1 } };
        world.actor->character.items.push_back(std::move(charm));
    }
    fight.update_fighters(kNow);
    assert(world.actor->player_combat.poison_len == 75);
    std::printf("OK: two poison mods' 150 frames over their count of 2: %d\n", world.actor->player_combat.poison_len);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_venom_state: %s\n", error.what());
    return 1;
}
