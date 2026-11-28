// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster that dies to poison is its poison's owner's kill (the state list's owner, FUN_0057ac50 ->
// FUN_005a6920 -> FUN_0057e7b0): in a two-player game the second player's poison, the first far off, gives
// the second the experience; a monster's poison gives nobody any. Needs the 1.14d game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
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

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kNecromancer, "Venomous", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kNecromancer;
    character.name = "Venomous";
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
    auto& second = world.join(character);
    auto& lead = world.lead();
    std::uint32_t now = 40000;
    world.fight.monsters.clear();
    lead.player.x = 10; lead.player.y = 10;
    second.player.x = 60; second.player.y = 60;                       // far apart: no sharing (0x1900)
    d2d::rules::Rng make_rng{ 3 };
    const auto poisoned = [&](int owner) {
        world.fight.monsters.push_back(make_monster(*data, int(data->monsters.by_id.at("zombie1")), 60.5f, 60.f, make_rng, 0));
        auto& zombie = world.fight.monsters.back();
        zombie.hit_points = 1; zombie.life_frac = 0;
        zombie.poison_rate = 1.0; zombie.poison_until = now + 4000; zombie.poison_owner = owner;
        return world.fight.monsters.size() - 1;
    };
    const auto exp_of = [](const Fight::Hero& hero) { return hero.character.stats.values[d2d::d2s::kExp]; };
    const auto lead_before = exp_of(lead), second_before = exp_of(second);
    const auto index = poisoned(second.id);
    world.fight.monster_dots(now, 0.04f);
    assert(!world.fight.monsters[index].alive());
    assert(exp_of(second) > second_before && exp_of(lead) == lead_before);
    std::printf("the second player's poison kill: +%lld experience to them, none to the lead\n", static_cast<long long>(exp_of(second) - second_before));
    const auto both_before = exp_of(second);
    const auto monster_kill = poisoned(-1);
    world.fight.monster_dots(now, 0.04f);
    assert(!world.fight.monsters[monster_kill].alive() && exp_of(second) == both_before && exp_of(lead) == lead_before);
    std::puts("test_poison_kill_credit: ok (a monster's poison credits nobody)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_poison_kill_credit: %s\n", error.what());
    return 1;
}
