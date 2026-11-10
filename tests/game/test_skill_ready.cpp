// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Whether a Sorceress's skill can go now (Fight::skill_status, FUN_00647960) and the client's ask
// before it goes (Fight::ask, FUN_004610c0), on the real Skills.txt rows. The statuses are
// game.exe's: tools/emu/skill_status.py (cases usable, no_mana, aura, passive, inferno_start_5 /
// _6, inferno_burning_5, delay_meteor, delay_meteor_off, delay_fire_bolt, delay_meteor_no_mana)
// and `skill_status.py client` (Jab: status 2 goes as Attack, 0).

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

constexpr int kCriticalStrike = 9, kJab = 10, kFireBolt = 36, kInferno = 41, kMeteor = 56, kMight = 98, kWerewolf = 223, kWerebear = 228;

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    // A Sorceress with her staff on an open level, every skill at level 1.
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, 1, "Headless", false, true, rng);
    Character character;
    character.character_class = 1;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    world->fight.skill_level = [](int) { return 1; };
    world->fight.skill_base = world->fight.skill_level;
    std::uint32_t now = 1000 * kTickMs;
    world->tick({}, now, now - kTickMs);
    auto& fight = world->fight;
    auto* actor = world->actor;
    auto& mana = actor->character.stats.values[d2d::d2s::kMana];
    actor->character.stats.values[d2d::d2s::kMaxMana] = std::int64_t(1000) << 8;
    now += 100 * kTickMs;

    mana = 100 * 256;
    assert(fight.skill_status(kFireBolt, now) == 0);                    // usable
    assert(fight.skill_status(kMight, now) == 6);                       // aura
    assert(fight.skill_status(kCriticalStrike, now) == 5);              // passive
    assert(fight.skill_status(kJab, now) == 2);                         // no spear
    assert(fight.ask(kJab, now) == 0);                                  // AttackNoMana: Attack goes
    assert(fight.ask(kCriticalStrike, now) == kCriticalStrike);         // 5 goes
    assert(fight.ask(kMight, now) < 0);
    mana = 0;
    assert(fight.skill_status(kFireBolt, now) == 1);                    // no_mana
    assert(fight.ask(kFireBolt, now) < 0);

    // Startmana (FUN_006440f0): Inferno needs 6 to start, not while it burns.
    mana = 5 * 256;
    assert(fight.skill_status(kInferno, now) == 1);                     // inferno_start_5
    actor->burning = now + 6 * 40;
    assert(fight.skill_status(kInferno, now) == 0);                     // inferno_burning_5
    actor->burning = 0;
    mana = 6 * 256;
    assert(fight.skill_status(kInferno, now) == 0);                     // inferno_start_6

    // Skilldelay (FUN_0056ef90 / FUN_006478f0): Meteor's do keeps Meteor, not Fire Bolt, off for 30 frames.
    mana = 100 * 256;
    actor->swing_skill = kMeteor;
    fight.start_delay(now);
    assert(actor->skill_delay == now + 30 * 40);
    assert(fight.skill_status(kMeteor, now) == 8);                      // delay_meteor
    assert(fight.skill_status(kFireBolt, now) == 0);                    // delay_fire_bolt
    assert(fight.ask(kMeteor, now) < 0);
    mana = 0;
    assert(fight.skill_status(kMeteor, now) == 1);                      // delay_meteor_no_mana
    mana = 100 * 256;
    assert(fight.skill_status(kMeteor, now + 30 * 40) == 0);            // delay_meteor_off
    actor->swing_skill = kFireBolt;
    actor->skill_delay = 0;
    fight.start_delay(now);
    assert(actor->skill_delay == 0);                                    // Fire Bolt has no delay

    // Werewolf (do 116, FUN_005c6ec0): the first cast puts the wolf on, pays and starts the delay (25 frames); the
    // next shifts back: every state of its group off (FUN_0056c740), nothing paid (FUN_0056f7f0 stops before the
    // cost), the delay again (FUN_0056f020). Werebear on the wolf is a shift back too: no bear.
    // Max mana pinned at the mana before each cast, so its regen can't hide a cost.
    auto& max_mana = actor->character.stats.values[d2d::d2s::kMaxMana];
    const auto cast_through = [&](int skill) {
        while (now < actor->skill_delay) { now += kTickMs; world->tick({}, now, now - kTickMs); }
        max_mana = mana;
        assert(fight.cast(skill, now));
        for (int frame = 0; frame < 100 && actor->pmode >= 0; ++frame) { now += kTickMs; world->tick({}, now, now - kTickMs); }
    };
    mana = 100 * 256;
    cast_through(kWerewolf);
    assert(fight.state_of("wolf") && mana < max_mana && actor->skill_delay > now);
    cast_through(kWerewolf);
    assert(!fight.state_of("wolf") && mana == max_mana && actor->skill_delay > now);
    cast_through(kWerewolf);
    cast_through(kWerebear);
    assert(!fight.state_of("wolf") && !fight.state_of("bear"));

    // A walked-up Jab with no spear swings Attack (start_swing asks again).
    actor->swing_skill = 0;
    actor->attack_skill = kJab;
    assert(fight.start_swing(now) && actor->swing_skill == 0);
    actor->pmode = -1;
    actor->attack_skill = kMight;
    assert(!fight.start_swing(now));

    std::printf("test_skill_ready: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_skill_ready: %s\n", error.what());
    return 1;
}
