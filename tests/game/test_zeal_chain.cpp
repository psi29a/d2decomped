// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Zeal's hits (do 13, FUN_005dbc60): on each hit's action frame with hits
// left, the next target by FUN_0056bd10 (the next id up from the struck one)
// and the swing starts over at once (FUN_0056e210 -> FUN_00553b10 with
// Param2 100: frame 0), so a chain of calc1 hits takes calc1 - 1 action
// frames and one whole swing. Through the REAL Fight on three Fallen in the
// Blood Moor. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
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
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    constexpr int kPaladin = 3;
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, kPaladin, "Headless", false, true, made_rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = data->level(2);                                 // the Blood Moor: out of town, so in reach finds them
    assert(world.level);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    const int zeal = data->skills.by_name.at("Zeal");
    assert(data->skills.get(zeal)->par[1] == 100);                // Param2 (+0x14c): starts over from frame 0
    fight.skill_level = [](int) { return 5; };                    // calc1 = min(Param5 2 + 5 - 1, Param6 5) = 5 hits
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    d2d::rules::Rng rng{ 3 };
    const auto place = [&](int count) {
        fight.monsters.clear();
        const float offsets[3][2] = { { 0.6f, 0 }, { -0.6f, 0 }, { 0, 0.6f } };
        for (int index = 0; index < count; ++index) {
            Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
            made.stats.hit_points = 50000; made.hit_points = 50000;
            made.unit.x = actor.player.x + offsets[index][0]; made.unit.y = actor.player.y + offsets[index][1];
            fight.add_monster(std::move(made));
        }
    };
    // One chain: the target at each action frame, and how long it took.
    struct Chain { std::vector<int> targets; std::uint32_t took = 0, action = 0, swing = 0; };
    const auto chain = [&] {
        Chain out;
        actor.character.stats.values[d2d::d2s::kMana] = 100 << 8;
        actor.attack_skill = zeal; actor.attack_mon = 0; actor.pmode = -1;
        std::uint32_t now = 1000;
        assert(fight.start_swing(now));
        assert(actor.swing_skill == zeal && actor.kicks_left == 4);
        out.action = actor.action_ms; out.swing = actor.pmode_until - now;
        for (int tick = 0; tick < 400 && actor.pmode >= 0; ++tick) {
            now += 40;
            const bool struck = actor.pstruck;
            const int target = actor.attack_mon;
            const auto started = actor.player.mode_ms;
            fight.player_modes(true, now, 0.04f);
            if (!struck && (actor.pstruck || actor.player.mode_ms != started)) out.targets.push_back(target);   // the last hit, or one that started the next
        }
        out.took = now - 1000;
        return out;
    };
    const auto to_tick = [](std::uint32_t span_ms) { return (span_ms + 39) / 40 * 40; };

    place(3);
    const auto three = chain();
    std::printf("three: %zu hits in %u ms (action %u, swing %u)\n", three.targets.size(), three.took, three.action, three.swing);
    assert((three.targets == std::vector<int>{ 0, 1, 2, 0, 1 }));          // round the ring by id
    assert(three.took == 4 * to_tick(three.action) + to_tick(three.swing));   // four action frames, then one whole swing

    place(1);                                                              // alone: every hit on it (FUN_0056bd10 wraps to the lowest)
    const auto one = chain();
    assert((one.targets == std::vector<int>{ 0, 0, 0, 0, 0 }));
    assert(one.took == three.took);
    std::puts("OK: Zeal's next hit starts on the last one's action frame");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_zeal_chain: %s\n", error.what());
    return 1;
}
