// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Act III quest IDs must fit every quest-state representation.
#include <gamedata.hpp>
#include <level_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>

using namespace d2d::game;
using namespace d2d::rules;

static_assert(kQuestLogCount >= 41);
static_assert(kQuestLogCount > GuardianQuest::kQuest);
static_assert(kQuestFlagCount == QuestBits{}.size() / 2);
static_assert(kQuestFlagCount > kRespecQuest);

int main() try {
    World world(nullptr, -1, -1);
    Level level{};
    level.id = level_ids::kKurastDocks;
    world.level = &level;

    QuestLogStates log;
    log.fill(0xff);
    world.act3_log(log, QuestBits{});

    for (const int quest : { LamEsenQuest::kQuest, GoldenBirdQuest::kQuest,
                             BladeQuest::kQuest, KhalimQuest::kQuest,
                             BlackenedTempleQuest::kQuest, GuardianQuest::kQuest })
        assert(log[std::size_t(quest)] != 0xff);
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_bounds: %s\n", error.what());
    return 1;
}
