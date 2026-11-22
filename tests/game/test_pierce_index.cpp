// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Stat 0x148 (pierce_idx) counts the skill packets the server takes (FUN_00549d00 / 0x549fc0: + 1 each),
// through the real World commands. The client asks first (FUN_004610c0) and sends nothing for a click
// it refuses, so that one doesn't count; Attack sent in an AttackNoMana skill's place does; a skill the
// server refuses in town (FUN_0056f640, after the packet) does too.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <protocol.hpp>
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

int main() try {
    namespace fs = std::filesystem;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, 1, "Headless", false, true, rng);   // a Sorceress
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
    auto* actor = world->actor;
    const int fire_bolt = data->skills.by_name.at("Fire Bolt");
    auto& mana = actor->character.stats.values[d2d::d2s::kMana];
    const auto click = [&](int skill, bool refuse = false) {
        now += 100 * kTickMs;
        actor->pmode = -1;
        actor->attack_mon = -1;
        if (refuse) mana = 0;                                       // after the regeneration of the ticks before
        const Order order{ actor->id, cmd::UseSkill{ skill, actor->player.x + 3.f, actor->player.y, -1, false, -1, false } };
        const auto before = actor->pierce_idx;
        world->tick({ &order, 1 }, now, now - kTickMs);
        return actor->pierce_idx - before;
    };
    mana = std::int64_t(500) << 8;
    assert(click(fire_bolt) == 1);                                  // sent
    assert(click(fire_bolt, true) == 0);                            // the ask refuses: no packet
    world->level = &data->town;                                     // in town
    world->fight.enter(&data->town);
    mana = std::int64_t(500) << 8;
    assert(click(fire_bolt) == 1);                                  // the server's town refusal comes after the packet
    std::printf("OK: pierce_idx counts the packets sent\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
