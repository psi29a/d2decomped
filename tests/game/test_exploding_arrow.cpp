// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's exploding arrow (FUN_00645f00: explodingarrow with stat 158, item_explosivearrow): Attack's do
// (FUN_0056f070) makes it at that stat's value (FUN_0056ecb0's level), not at Attack's own level; magicarrow
// (stat 157) the same. Its blast (hit function 4, FUN_005b07a0: explodingarrowexp2 with the arrow's skill and
// level) takes FUN_0064b860's MissileSkill pick: the missile's skill when over 0, else the row's Skill, so off
// Attack (skill 0) Exploding Arrow's fire, off a merc's RogueMissile (336) RogueMissile's (none). The REAL
// Fight::fire, Fight::damage_skill and Fight::strike. Needs the game data; SKIPs without it.
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
#include <memory>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

// Check the loaded rows through the real fight paths; --oracle prints the
// FUN_005b07a0 result compared by tools/emu/merc_exploding_hit.py.
int main(int argc, char** argv) try {
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

    // The blast's skill.
    Missile blast;
    blast.info = &data->missiles.at("explodingarrowexp2");
    blast.friendly = true; blast.caster = actor->id; blast.level = 7;
    assert(blast.info->missile_skill && blast.info->skill == "Exploding Arrow");
    blast.skill = 0;
    const auto* off_attack = fight.damage_skill(blast);
    blast.skill = data->skills.by_name.at("RogueMissile");
    const auto* off_rogue = fight.damage_skill(blast);
    assert(off_attack && off_attack->name == "Exploding Arrow" && off_rogue && off_rogue->name == "RogueMissile");
    // Off Attack it burns: a zombie immune to physical damage loses life to the fire alone.
    blast.skill = 0;
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    fight.monsters.clear();
    Monster foe = make_monster(*data, int(zombie - data->monsters.types.begin()), actor->player.x + 3.f, actor->player.y, rng, 0, true, 1, open.id);
    foe.id = 0x7ffff0;
    foe.hit_points = foe.stats.hit_points = 100000;
    foe.stats.armor_class = -1000;                                    // Exploding Arrow's SrcDam swing lands
    foe.boss_res[0] = 100;                                            // no physical
    fight.monsters.push_back(std::move(foe));
    for (int tries = 0; tries < 20 && fight.monsters[0].hit_points == 100000; ++tries) fight.strike(blast, 0, 1000 * kTickMs, 0, nullptr);
    std::printf("the blast off Attack at level 7: %d off\n", 100000 - fight.monsters[0].hit_points);
    assert(fight.monsters[0].hit_points < 100000);

    // FUN_005adf10 -> FUN_005b07a0: the merc's plain explodingarrow hit also makes
    // explodingarrowexp2, carrying RogueMissile and its level. The old plain path made none.
    fight.pending.clear();
    Missile merc_arrow;
    merc_arrow.info = &data->missiles.at("explodingarrow");
    merc_arrow.x = fight.monsters[0].unit.x;
    merc_arrow.y = fight.monsters[0].unit.y;
    merc_arrow.level = 7;
    merc_arrow.min = merc_arrow.max = 1;
    merc_arrow.attack_rating = 100000;
    merc_arrow.friendly = merc_arrow.by_merc = true;
    merc_arrow.caster = actor->id;
    const bool spent = fight.plain_missile_hits(merc_arrow, 0, 1001 * kTickMs);
    const auto* rogue_missile = data->skills.get(data->skills.by_name.at("RogueMissile"));
    assert(spent && rogue_missile && fight.pending.size() == 1 && fight.pending[0].info);
    assert(fight.pending[0].info->name == "explodingarrowexp2");
    assert(fight.pending[0].skill == rogue_missile->id && fight.pending[0].level == 7);
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") {
        std::printf("3 %d %d %d\n", fight.pending[0].info->id, fight.pending[0].skill, fight.pending[0].level);
        return 0;
    }
    std::puts("OK: a merc exploding arrow makes its RogueMissile blast (FUN_005b07a0)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_exploding_arrow: %s\n", error.what());
    return 1;
}
