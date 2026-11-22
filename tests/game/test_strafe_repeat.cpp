// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Strafe in play (start 8, FUN_005dacd0: as many arrows as monsters within aurarange, held between calc3
// and calc1; do 12, FUN_005dba40): one arrow on each action frame, and with arrows left the swing starts
// over (FUN_0056e210 -> FUN_00553b10) from (100 - Param6) % of its frames, so the arrows come one a
// shortened swing, all in one A1, at the targets in turn.
#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
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
#include <vector>

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

    // An Amazon with a short bow and a quiver on an open level, three zombies in range.
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kAmazon, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kAmazon;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items.clear();
    d2d::d2s::Item bow, quiver;
    bow.code = "sbw"; bow.location = d2d::d2s::item_location::kEquipped; bow.slot = d2d::d2s::body_location::kRightArm; bow.wielded = true;
    quiver.code = "aqv"; quiver.location = d2d::d2s::item_location::kEquipped; quiver.slot = d2d::d2s::body_location::kLeftArm; quiver.quantity = 250;
    bow.flags = quiver.flags = 0x10;                                // identified
    bow.quality = quiver.quality = 2;                               // normal
    bow.identified = quiver.identified = true;
    character.items = { std::move(bow), std::move(quiver) };
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
    fight.skill_level = [](int) { return 1; };
    now += 100 * kTickMs;
    using MonType = decltype(data->monsters.types)::value_type;
    const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
    assert(zombie != data->monsters.types.end());
    fight.monsters.clear();
    for (int k = 0; k < 3; ++k) {
        Monster foe;
        foe.type = int(zombie - data->monsters.types.begin());
        foe.id = 0x7ffff0 + k;
        foe.hit_points = foe.stats.hit_points = 100000;
        foe.unit.x = actor->player.x + 3.f; foe.unit.y = actor->player.y + float(k - 1) * 0.6f;
        fight.monsters.push_back(std::move(foe));
    }
    actor->character.stats.values[d2d::d2s::kMana] = 100 << 8;
    const int strafe = data->skills.by_name.at("Strafe");
    const Order order{ actor->id, cmd::UseSkill{ strafe, fight.monsters[1].unit.x, fight.monsters[1].unit.y, -1, true, -1, true } };
    world->tick({ &order, 1 }, now, now - kTickMs);
    assert(actor->swing_skill == strafe);
    std::vector<int> shot_at;
    std::vector<std::pair<float, float>> aims;                     // each arrow's heading, in the order made
    std::vector<std::uint32_t> seen;                               // the arrows recorded, by birth (one a swing): one gone doesn't hide the next
    for (int frame = 1; frame < 120 && actor->pmode >= 0; ++frame) {
        now += kTickMs;
        world->tick({}, now, now - kTickMs);
        std::vector<const Missile*> arrows;
        for (const auto& missile : fight.missiles) if (missile.friendly && missile.skill == strafe) arrows.push_back(&missile);
        for (const auto& missile : fight.pending) if (missile.friendly && missile.skill == strafe) arrows.push_back(&missile);
        std::ranges::sort(arrows, {}, &Missile::born);
        for (const Missile* arrow : arrows) {
            if (std::ranges::contains(seen, arrow->born)) continue;
            seen.push_back(arrow->born);
            assert(arrow->to.has_value());                          // aimed at its monster's spot, not start + (1, 1)
            shot_at.push_back(frame);
            aims.emplace_back(arrow->velocity_x, arrow->velocity_y);
        }
    }
    std::printf("Strafe: %zu arrows at frames", shot_at.size());
    for (const int frame : shot_at) std::printf(" %d", frame);
    std::printf("\n");
    assert(shot_at.size() == 3);                                    // three in range, calc3 (2) .. calc1 (5)
    assert(shot_at[1] - shot_at[0] > 1 && shot_at[1] - shot_at[0] == shot_at[2] - shot_at[1]);   // one a restarted swing
    // At the ground: FUN_0056bd10's pick from -1, the lowest id, then each next above the last (monsters 0, 1, 2:
    // south of east, east, north of east).
    assert(aims[0].second < aims[1].second && aims[1].second < aims[2].second && aims[0].second < 0.f && aims[2].second > 0.f);   // they walk in meanwhile
    std::printf("test_strafe_repeat: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_strafe_repeat: %s\n", error.what());
    return 1;
}
