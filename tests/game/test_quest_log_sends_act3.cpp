// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Act 3's records send their log (S->C 0x5d as ev::QuestNews, World::act3_update) at their own FUN_00544300
// flag-1 sites, to each player their callback passes, and not at the flag-0 ones: Lam Esen's Tome
// (FUN_005b7340), the Blade (FUN_005b8bf0), the Golden Bird (FUN_005b9e70) and the Blackened Temple
// (FUN_005baf70). a3q2 (Khalim's Will) keeps the View's change: its pickups send straight (FUN_005b81c0).
// No game data.
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <quests_act3.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <memory>
#include <variant>
#include <vector>

using namespace d2d::game;
using namespace d2d::rules;
using d2d::game::Level;
using d2d::game::World;

namespace {

constexpr int kDocks = 75, kBazaar = 80;

// The quests whose log the World sent the actor since the last look (World::act3_update), cleared.
std::vector<int> sent(World& world) {
    world.actor->events.clear();
    world.act3_update(0);
    std::vector<int> out;
    for (const auto& event : world.actor->events)
        if (const auto* news = std::get_if<ev::QuestNews>(&event)) out.push_back(news->quest);
    return out;
}

// A fresh World at the docks, act 3 open (word 0xf bit 0), no Golden Bird carrier to bind.
std::unique_ptr<World> docks_world(GameData& data) {
    auto world = std::make_unique<World>(&data, -1, -1);
    world->level = data.levels[kDocks].get();
    world->golden_bird.armed = false;
    quest::set(world->quests(), 0xf, 0);
    return world;
}

}  // namespace

// Checks each act 3 record's flag-1 sites send through the World and the flag-0 ones don't.
int main() try {
    GameData data;
    for (const int id : { kDocks, kBazaar, level_ids::kKurastCauseway }) (data.levels[id] = std::make_unique<Level>())->id = id;
    {   // Lam Esen's Tome: Alkor's 549 (0x5b77c6), out of the docks only past log 1 (0x5b76af), the drop
        // (0x5b7a4f), the pickup (0x5b79ab); his thanks' 0xd (0x5b78c7) and the stand's 1 (0x5b7b71) are flag 0.
        auto world = docks_world(data);
        world->act3_said(monster_ids::kAlkor, LamEsenQuest::kInit);
        assert(sent(*world) == std::vector{ LamEsenQuest::kQuest });
        world->level = data.levels[kBazaar].get();
        world->act3_enter(*data.levels[kDocks]);                 // log 1 already: under way, no send
        assert(world->lam_esen.state == 3 && sent(*world).empty());
        assert(world->lam_esen.take(world->quests()) && sent(*world).empty());
        world->lam_esen.picked_up(1);
        assert(sent(*world) == std::vector{ LamEsenQuest::kQuest });
        world->lam_esen.dropped(1);
        assert(sent(*world) == std::vector{ LamEsenQuest::kQuest });
        world->level = data.levels[kDocks].get();
        world->act3_said(monster_ids::kAlkor, LamEsenQuest::kSuccess);
        assert(sent(*world).empty());
        auto later = docks_world(data);                          // told on a join (bit 2: log 1), then the docks: no send
        quest::set(later->quests(), LamEsenQuest::kQuest, 2);
        later->act3_join();
        assert(later->lam_esen.state == 2 && sent(*later).empty());
        later->level = data.levels[kBazaar].get();
        later->act3_enter(*data.levels[kDocks]);
        assert(later->lam_esen.state == 3 && sent(*later).empty());
        auto fresh = docks_world(data);                          // told with no log: sent
        fresh->lam_esen.state = 2;
        fresh->level = data.levels[kBazaar].get();
        fresh->act3_enter(*data.levels[kDocks]);
        assert(sent(*fresh) == std::vector{ LamEsenQuest::kQuest });
    }
    {   // The Blade: Hratli's talk closing after 571 (0x5b8c84), the pickup (0x5b9581, 0x5b95e9); Ormus's 587 is flag 0.
        auto world = docks_world(data);
        world->act3_said(monster_ids::kHratli, BladeQuest::kHratliTells);
        assert(sent(*world).empty());
        world->act3_talk_closed(monster_ids::kHratli);
        assert(world->blade.log == 2 && sent(*world) == std::vector{ BladeQuest::kQuest });
        world->blade.picked_up(world->quests());
        assert(world->blade.log == 3 && sent(*world) == std::vector{ BladeQuest::kQuest });
        world->act3_said(monster_ids::kOrmus, BladeQuest::kOrmusTakes);
        assert(world->blade.log == 0xd && sent(*world).empty());
    }
    {   // The Golden Bird: the carrier's death (0x5bac45), Cain's talk closing (0x5ba114), the pickups.
        auto world = docks_world(data);
        world->golden_bird.armed = true;
        world->golden_bird.made(9);
        world->golden_bird.armed = false;
        assert(world->golden_bird.killed(world->quests(), 9) && sent(*world) == std::vector{ GoldenBirdQuest::kQuest });
        world->act3_said(GoldenBirdQuest::kCain, GoldenBirdQuest::kCainFigurine);
        assert(sent(*world).empty());
        world->act3_talk_closed(GoldenBirdQuest::kCain);
        assert(sent(*world) == std::vector{ GoldenBirdQuest::kQuest });
        world->golden_bird.picked_up(world->quests(), true);
        assert(sent(*world) == std::vector{ GoldenBirdQuest::kQuest });
    }
    {   // The Blackened Temple: Ormus's talk closing (0x5bb0ee), a Council member made (0x5bb5ec); the docks' log 2
        // (0x5bb1c0) is flag 0.
        auto world = docks_world(data);
        world->level = data.levels[level_ids::kKurastCauseway].get();
        world->act3_enter(*data.levels[kBazaar]);
        world->level = data.levels[kDocks].get();
        world->act3_said(monster_ids::kOrmus, BlackenedTempleQuest::kTells);
        world->act3_talk_closed(monster_ids::kOrmus);
        assert(sent(*world) == std::vector{ BlackenedTempleQuest::kQuest });
        world->level = data.levels[kBazaar].get();
        world->act3_enter(*data.levels[kDocks]);
        assert(world->temple.log == 2 && world->temple.state == 5 && sent(*world).empty());
        Monster ismail;
        ismail.id = 30; ismail.type = 1; ismail.super = 26;
        world->fight.monsters.push_back(ismail);
        world->fight.made_bosses.push_back({ 30, 1, level_ids::kTravincal });
        world->act3_bind_new_bosses();
        assert(world->temple.log == 3 && sent(*world) == std::vector{ BlackenedTempleQuest::kQuest });
    }
    {   // Whom it reaches (FUN_005b8bf0): not a player done without bit 13 or 14.
        auto world = docks_world(data);
        quest::set(world->quests(), BladeQuest::kQuest, 0);
        world->blade.told_pending = true;
        world->act3_talk_closed(monster_ids::kHratli);
        assert(sent(*world).empty());
        quest::set(world->quests(), BladeQuest::kQuest, 14);
        world->blade.told_pending = true;
        world->act3_talk_closed(monster_ids::kHratli);
        assert(sent(*world) == std::vector{ BladeQuest::kQuest });
    }
    assert(!quest_log_sent_by_event(KhalimQuest::kQuest));
    std::puts("OK: act 3's records send their log at their own flag-1 sites");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_log_sends_act3: %s\n", error.what());
    return 1;
}
