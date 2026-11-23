// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Chain Lightning's hop (hit 12, FUN_005aa730): from the missile where it
// struck (ESI, 0x5aa7a3) a new one (FUN_0059fa30) aimed at the picked
// unit's x / y (EDI, 0x5aa855), a hop fewer. Unaimed, missile_fly would fly
// it at its start + (1, 1), a diagonal past the pack. Level 10: calc1
// ln34 / 5 = 7 bolts, round a pack of four in id order (FUN_0056bd10).

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>
#include <vector>

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
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    // A Chain Lightning cast at the first of a pack of four, a subtile row
    // 2 apart: the bolt strikes the first and hops on (calc1 ln34 / 5 hops).
    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Chain", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Chain";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    int at_x = -1, at_y = -1;
    for (int y = 20; y < 400 && at_x < 0; ++y)
        for (int x = 20; x < 400 && at_x < 0; ++x)
            if (std::ranges::all_of(std::array<std::pair<int, int>, 5>{ { { 0, 0 }, { 8, 0 }, { -8, 0 }, { 0, 8 }, { 0, -8 } } },
                                    [&](auto off) { return !missile_rooms(*data, open, float(x + off.first) + 0.5f, float(y + off.second) + 0.5f).empty(); }))
                at_x = x * 5 + 2, at_y = y * 5 + 2;
    assert(at_x >= 0);
    world.actor->player.x = (float(at_x) + 0.5f) / 5;
    world.actor->player.y = (float(at_y) + 0.5f) / 5;
    const int zombie = int(data->monsters.by_id.at("zombie1"));
    d2d::rules::Rng spawn_rng{ 5 };
    for (int k = 0; k < 4; ++k) {
        Monster monster = make_monster(*data, zombie, (float(at_x + 10) + 0.5f) / 5, (float(at_y - 3 + 2 * k) + 0.5f) / 5, spawn_rng, 0);
        assert(monster.targetable());
        monster.stats.hit_points = monster.hit_points = 1000000;
        monster.next_act = ~0u;
        world.fight.add_monster(std::move(monster));
    }
    const auto& skill = *data->skills.get(data->skills.by_name.at("Chain Lightning"));
    Fight::Caster caster;
    caster.x = world.actor->player.x; caster.y = world.actor->player.y; caster.level = 10;
    caster.to_x = world.fight.monsters[0].unit.x; caster.to_y = world.fight.monsters[0].unit.y; caster.target = 0;
    std::uint32_t now = 1000 * kTickMs;
    world.fight.fire(skill, now, caster);
    assert(world.fight.pending.size() == 1);
    int hopped = 0;
    std::vector<int> struck(world.fight.monsters.size(), 0);
    const Crowd crowd;
    for (int frame = 1; frame <= 60; ++frame) {
        now += kTickMs;
        std::vector<int> before;
        for (auto& monster : world.fight.monsters) { monster.next_act = ~0u; before.push_back(monster.hit_points); }
        world.fight.world(true, now, 0.04f, crowd);
        for (const auto& missile : world.fight.missiles)
            if (missile.info && missile.info->name == "chainlightning" && missile.born == now) ++hopped;
        for (std::size_t i = 0; i < world.fight.monsters.size(); ++i) if (world.fight.monsters[i].hit_points < before[i]) ++struck[i];
    }
    std::fprintf(stderr, "hops launched %d, struck %d %d %d %d\n", hopped, struck[0], struck[1], struck[2], struck[3]);
    assert(hopped == 6 && struck == (std::vector<int>{ 2, 2, 2, 1 }));
    std::printf("ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
}
