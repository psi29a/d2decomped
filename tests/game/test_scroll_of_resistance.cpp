// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Scroll of Resistance used (C->S 0x20, its branch at 0x55e4b4): only
// with Prison of Ice bit 8 and not bit 7; bit 7 set, the scroll gone.
// FUN_0058a0a0 -> FUN_00589ff0: +10 to stats 39 / 41 / 43 / 45 for each
// difficulty whose word has bit 7, in an expansion game only (0x539a1b).
// No game data.
#include <character.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <protocol.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    using d2d::rules::PrisonQuest;
    namespace quest = d2d::rules::quest;
    {
        d2d::rules::QuestBits bits{};
        assert(!PrisonQuest::read_scroll(bits));               // not given
        quest::set(bits, PrisonQuest::kQuest, 8);
        assert(PrisonQuest::read_scroll(bits) && quest::bit(bits, PrisonQuest::kQuest, 7));
        assert(!PrisonQuest::read_scroll(bits));               // read already
        std::array<d2d::rules::QuestBits, 3> by_difficulty{};
        assert(PrisonQuest::scroll_resist(by_difficulty) == 0);
        quest::set(by_difficulty[0], PrisonQuest::kQuest, 7);
        quest::set(by_difficulty[2], PrisonQuest::kQuest, 7);
        quest::set(by_difficulty[1], PrisonQuest::kQuest, 8);  // given, not read
        assert(PrisonQuest::scroll_resist(by_difficulty) == 20);
        std::puts("OK: read once with bit 8, 10 resistance for each difficulty read");
    }
    GameData data;
    Level town;
    town.id = 109;
    town.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    town.walk.assign(std::size_t(8 * 5) * std::size_t(8 * 5), 0);
    World world(&data, -1, -1);
    world.level = &town;
    auto& character = world.actor->character;
    character.header.status |= 0x20;                           // expansion
    d2d::d2s::Item scroll;
    scroll.code = PrisonQuest::kScrollCode;
    scroll.id = 9;
    character.items.push_back(scroll);
    const auto resist = [&] { return panel_stats(data, character.header, character.items, character.stats).res; };
    assert((resist() == std::array<std::int64_t, 4>{ 0, 0, 0, 0 }));

    world.apply(cmd::UseItem{ 9 }, 1000);                        // not given yet: kept, nothing
    assert(character.items.size() == 1 && !quest::bit(world.quests(), PrisonQuest::kQuest, 7));
    quest::set(world.quests(), PrisonQuest::kQuest, 8);
    world.apply(cmd::UseItem{ 9 }, 2000);
    assert(character.items.empty() && quest::bit(world.quests(), PrisonQuest::kQuest, 7));
    assert((resist() == std::array<std::int64_t, 4>{ 10, 10, 10, 10 }));
    std::puts("OK: the scroll read on a right-click goes, +10 to the four resistances");

    character.header.status = std::uint8_t(character.header.status & ~0x20);   // a classic game: no stat list
    assert((resist() == std::array<std::int64_t, 4>{ 0, 0, 0, 0 }));
    std::puts("OK: none in a classic game");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_scroll_of_resistance: %s\n", error.what());
    return 1;
}
