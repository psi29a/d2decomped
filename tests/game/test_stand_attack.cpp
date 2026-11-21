// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A shift-click (stand still) on a monster out of reach (FUN_00461c70): a melee skill swings where the
// player stands, at the click (flag 0x20 -> FUN_00461700(0)), and nothing walks; without shift the
// player walks up to it (FUN_00481030); in reach either way it swings at the monster. Zeal at a spot
// swings at the foe in reach (FUN_005daf40).
#include <ai.hpp>
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
#include <string_view>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());

    // A Sorceress with her staff (a melee weapon) on an open level.
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
    std::uint32_t now = 1000 * kTickMs;
    world->tick({}, now, now - kTickMs);
    auto& fight = world->fight;
    auto* actor = world->actor;
    now += 100 * kTickMs;

    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    Monster foe;
    foe.type = int(zombie - data->monsters.types.begin());
    foe.id = 0x7ffff0;
    foe.hit_points = 100;
    fight.monsters.clear();
    fight.monsters.push_back(std::move(foe));
    // One shift-click (or plain click) on it at `apart` cells east, from a standing start.
    const auto click = [&](float apart, bool stand) {
        actor->pmode = -1;
        actor->attack_mon = -1;
        actor->player.walking = false;
        auto& monster = fight.monsters[0];
        monster.unit.x = actor->player.x + apart;
        monster.unit.y = actor->player.y;
        const Order order{ actor->id, cmd::UseSkill{ 0, monster.unit.x, monster.unit.y, monster.id, true, -1, stand } };
        world->tick({ &order, 1 }, now, now - kTickMs);
        now += 100 * kTickMs;
    };
    click(3.f, true);                                              // out of reach, shift: a swing in place, no walk
    assert(actor->pmode == kModeA1 && actor->attack_mon == -1 && !actor->player.walking);
    assert(actor->player.dir == direction16(3, 0));
    click(3.f, false);                                             // out of reach, no shift: walks up to it
    assert(actor->attack_mon == 0 && actor->player.walking && actor->pmode < 0);
    click(0.4f, true);                                             // in reach (FUN_00622c40: 2 subtiles), shift: at the monster
    assert(actor->attack_mon == 0 && actor->pmode == kModeA1);
    // Bash with shift out of reach while a walk goes on, to an item and an NPC queued: the walk stops and
    // both go (FUN_00461700(0) is a skill at the spot, in their place); Bash itself at a spot isn't
    // swung in d2d yet.
    const int bash = data->skills.by_name.at("Bash");
    actor->pmode = -1;
    actor->attack_mon = -1;
    actor->target_x = actor->player.x - 5.f; actor->target_y = actor->player.y;
    actor->player.walking = true;
    actor->pick_item = 12345;
    actor->interact_npc = 0;
    fight.monsters[0].unit.x = actor->player.x + 3.f;
    fight.monsters[0].unit.y = actor->player.y;
    const Order bash_order{ actor->id, cmd::UseSkill{ bash, fight.monsters[0].unit.x, fight.monsters[0].unit.y, fight.monsters[0].id, true, -1, true } };
    world->tick({ &bash_order, 1 }, now, now - kTickMs);
    assert(!actor->player.walking && actor->attack_mon == -1 && actor->pick_item == -1 && actor->interact_npc == -1);

    // Zeal at a spot (no unit named, FUN_005daf40): its start takes the foe in reach with the lowest id
    // (FUN_0056bd10 from -1) and swings at it; with none in reach it refuses, as Bash's does.
    const int zeal = data->skills.by_name.at("Zeal");
    fight.skill_level = [](int) { return 1; };
    const auto zeal_at = [&](float apart) {
        actor->pmode = -1;
        actor->attack_mon = -1;
        actor->player.walking = false;
        actor->character.stats.values[d2d::d2s::kMana] = 100 << 8;
        fight.monsters[0].unit.x = actor->player.x + apart;
        fight.monsters[0].unit.y = actor->player.y;
        const Order order{ actor->id, cmd::UseSkill{ zeal, actor->player.x - 2.f, actor->player.y, -1, true, -1, false } };
        world->tick({ &order, 1 }, now, now - kTickMs);
        now += 100 * kTickMs;
    };
    zeal_at(0.4f);
    assert(actor->attack_mon == 0 && actor->pmode == kModeA1 && actor->swing_skill == zeal && actor->player.dir == direction16(1, 0));
    zeal_at(6.f);
    assert(actor->attack_mon == -1 && actor->pmode < 0);

    std::printf("test_stand_attack: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_stand_attack: %s\n", error.what());
    return 1;
}
