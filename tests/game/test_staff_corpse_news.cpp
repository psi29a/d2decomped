// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A corpse's a2q2 pickup (World::item_event(code, now, true)): item codes, quest bits
// and event counts only, no real game numbers. See tools/emu/staff_corpse_news.py and
// docs/research/re/quests-act2.md. Made-up rows (test_data.hpp): the rule, not the game's data.

#include <quests.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string_view>
#include <variant>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    World world(&test.data, -1, -1);
    using Staff = d2d::rules::HoradricStaffQuest;
    namespace quest = d2d::rules::quest;

    const auto news = [&](std::string_view code) {
        world.actor->events.clear();
        world.item_event(code, 0, true);
        return std::ranges::count_if(world.actor->events, [](const Event& event) {
            const auto* sent = std::get_if<ev::QuestNews>(&event);
            return sent && sent->quest == Staff::kQuest;
        });
    };

    struct CorpseCase {
        const char* code;
        unsigned owner_bits;
        int events;
    };
    static constexpr CorpseCase kCorpseCases[]{
#include "../staff_corpse_news_cases.inc"
    };
    int failures = 0;
    for (const auto& row : kCorpseCases) {
        world.quests() = {};
        for (int bit = 0; bit < 16; ++bit)
            if (row.owner_bits & (1u << bit)) quest::set(world.quests(), Staff::kQuest, bit);
        const auto got = news(row.code);
        if (got == row.events) continue;
        std::printf("FAIL: corpse %s, owner bits %#x: %td news, want %d\n", row.code, row.owner_bits, got, row.events);
        ++failures;
    }
    if (failures) return 1;
    std::printf("OK: a corpse's a2q2 pickup sends its owner's log as FUN_00599a30 does\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_staff_corpse_news: %s\n", error.what());
    return 1;
}
