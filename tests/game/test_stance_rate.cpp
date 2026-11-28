// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's other_animrate (stat 69), its stand's (NU, TN) pace in
// FUN_00623f50's last branch: 100 (FUN_00460bf0), plus a self state's
// attackrate (FUN_005c6cc0 writes 0x44 also as 0x45), and chill's -50
// (FUN_0057af80 on 0x43..0x45); the View clamps it to 15..175.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <replication.hpp>
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

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR (the game-data check)\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (the game-data check)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (data && !data->patched) { std::printf("SKIP: no 1.14d patch data (the game-data check)\n"); return D2D_TEST_SKIPPED; }
    assert(data && data->patched);

    d2d::rules::Rng rng(7);
    auto made = new_character(*data, d2d::d2s::kPaladin, "Stander", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kPaladin;
    character.name = "Stander";
    character.header = made.header;
    character.stats = made.stats;
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    constexpr int kLevel = 10;
    fight.skill_level = [](int) { return kLevel; };
    constexpr std::uint32_t kNow = 2000 * kTickMs;

    fight.update_fighters(kNow);
    assert(world.actor->other_rate == 100);

    // A self state (do 18, FUN_005c9480 -> FUN_005c6cc0) with attackrate (68) among its aurastats.
    const auto& rows = data->skills.rows;
    const auto quick = std::ranges::find_if(rows, [](const auto& row) {
        return row.srvdofunc == d2d::rules::ServerDoFunction::kSelfState && std::ranges::contains(row.aurastat, int(d2d::d2s::kAttackRate));
    });
    assert(quick != rows.end());
    world.actor->self_states.push_back({ quick->id, kLevel, kNow + 1000 * kTickMs });
    fight.update_fighters(kNow);
    const int attack_rate = int(world.actor->psum[d2d::d2s::kAttackRate]);
    assert(attack_rate != 0 && world.actor->other_rate == 100 + attack_rate);
    std::printf("OK: %s's attackrate %+d is the stand's too: %d %%\n", quick->name.c_str(), attack_rate, world.actor->other_rate);

    // Chill's -50 on it as well.
    world.actor->self_states.clear();
    world.actor->chilled = kNow + 100 * kTickMs;
    fight.update_fighters(kNow);
    assert(world.actor->other_rate == 50);
    std::printf("OK: a chilled stand plays at %d %%\n", world.actor->other_rate);
    // A client's View carries it (the view codec: encode_view / apply_view).
    ViewEncoder encoder;
    View received;
    auto sent = world.view();
    sent.level = &data->town;
    assert(sent.stance_rate == 50 && apply_view(*data, encode_view(*data, sent, encoder), received) && received.stance_rate == 50);
    std::puts("OK: the stand's rate reaches a client's View");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_stance_rate: %s\n", error.what());
    return 1;
}
