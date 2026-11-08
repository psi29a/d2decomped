// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Den of Evil cleared with two players (FUN_00590260): the killer takes
// part (FUN_00590260's list, FUN_005455f0), and every player with bit 13
// brings their party in Act 1 (LAB_00590190 → FUN_00540510 → LAB_00590120):
// local co-op is one party, so the second player, who killed none, gets bits
// 13 and 1 too, not bit 14. A player already done is left alone.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    const auto* den = data->level(d2d::rules::DenQuest::kDen);
    if (!den) { std::printf("SKIP: no Den of Evil\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Killer", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Killer";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    namespace quest_bit = d2d::rules::quest;
    constexpr int kDen = d2d::rules::DenQuest::kQuest;
    const auto bits = [](World::Player& player) -> d2d::rules::QuestBits& { return player.character.header.quests[0]; };
    for (const bool third_done : { false, true }) {
        World world(&*data, -1, -1);
        world.enter(character);
        auto bystander = character;
        bystander.name = "Bystander";
        auto& second = world.join(bystander);
        World::Player* third = nullptr;
        if (third_done) {
            auto finished = character;
            finished.name = "Finished";
            quest_bit::set(finished.header.quests[0], kDen, 0);
            third = &world.join(finished);
        }
        world.arrive(den, float(den->ds1.width()) / 2, float(den->ds1.height()) / 2, "a test");
        std::uint32_t now_ms = 60 * kTickMs;
        for (int warm = 0; warm < 5; ++warm, now_ms += kTickMs) world.tick({}, now_ms + kTickMs, now_ms);
        assert(world.level == den && world.fight.mon_level == den);

        // Every Den room up, its monsters all dead, the last at the first player's hand.
        world.fight.spawning.levels[den].up.assign(den->rooms.size(), true);
        for (auto& monster : world.fight.monsters) monster.hit_points = 0;
        world.den.state = 3;
        world.den_left = 1;
        world.fight.kills.push_back({ .type = 0, .x = 0, .y = 0, .level = den->id, .killer = world.lead().id });
        world.den_count(now_ms);
        world.fight.kills.clear();

        assert(world.den.state == 4);
        assert(quest_bit::bit(bits(world.lead()), kDen, 13) && quest_bit::bit(bits(world.lead()), kDen, 1));
        assert(quest_bit::bit(bits(second), kDen, 13) && quest_bit::bit(bits(second), kDen, 1) && !quest_bit::bit(bits(second), kDen, 14));
        if (third) assert(quest_bit::bit(bits(*third), kDen, 0) && !quest_bit::bit(bits(*third), kDen, 1) && !quest_bit::bit(bits(*third), kDen, 14));
    }
    std::printf("OK: the Den's clear reaches the killer's party in Act 1, not those done\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
