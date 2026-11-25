// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's exploding arrow (FUN_00645f00: explodingarrow with stat 158, item_explosivearrow): Attack's do
// (FUN_0056f070) makes it at that stat's value (FUN_0056ecb0's level), not at Attack's own level; magicarrow
// (stat 157) the same. The REAL Fight::fire. Needs the game data; SKIPs without it.
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
#include <memory>
#include <string>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAmazon, "Fletcher", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Fletcher";
    character.header = made.header;
    character.stats = made.stats;
    d2d::d2s::Item bow, quiver;
    bow.code = "sbw"; bow.location = d2d::d2s::item_location::kEquipped; bow.slot = d2d::d2s::body_location::kRightArm; bow.wielded = true;
    quiver.code = "aqv"; quiver.location = d2d::d2s::item_location::kEquipped; quiver.slot = d2d::d2s::body_location::kLeftArm; quiver.quantity = 250;
    bow.flags = quiver.flags = 0x10;
    bow.quality = quiver.quality = 2;
    bow.identified = quiver.identified = true;
    character.items = { std::move(bow), std::move(quiver) };
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&*data, -1, -1);
    world->enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world->level = &open;
    world->fight.enter(&open);
    auto& fight = world->fight;
    auto* actor = world->actor;
    fight.skill_level = [](int) { return 1; };
    const auto& attack = *data->skills.get(0);
    actor->cast_x = actor->player.x + 4.f; actor->cast_y = actor->player.y;
    // The shot Attack fires with the bow's stat `stat` at `value` (none: a plain arrow).
    const auto shot = [&](int stat, int value) {
        auto& props = actor->character.items[0].props;
        props.clear();
        if (stat >= 0) props.push_back({ stat, 0, value });
        fight.pending.clear();
        fight.fire(attack, 1000 * kTickMs, fight.player_caster(0));
        assert(fight.pending.size() == 1 && fight.pending[0].info);          // launch's: in play from the next frame
        return std::pair{ fight.pending[0].info->name, fight.pending[0].level };
    };
    const auto plain = shot(-1, 0), exploding = shot(d2d::d2s::kItemExplosiveArrow, 7), magic = shot(d2d::d2s::kItemMagicArrow, 5);
    std::printf("plain: %s at %d; exploding: %s at %d; magic: %s at %d\n", plain.first.c_str(), plain.second, exploding.first.c_str(), exploding.second,
                magic.first.c_str(), magic.second);
    assert(plain.first == "arrow" && plain.second == 1);
    assert(exploding.first == "explodingarrow" && exploding.second == 7);
    assert(magic.first == "magicarrow" && magic.second == 5);
    std::puts("OK: a magic or exploding arrow flies at its stat's level");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_exploding_arrow: %s\n", error.what());
    return 1;
}
