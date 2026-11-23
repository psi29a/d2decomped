// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The cycle vines' recycler delays (missile do 29 / 33, FUN_005afd70 /
// FUN_005afec0): on the missile's Param1th frame (45) the owner, alive and
// under its max life (29: recycler delay, CorpseCycler's) or mana (33: vine
// recycler delay, VineCycler's), takes MulDiv(max, calc1, 100) of the skill
// at the missile's level, up to the max, and its ProgOverlay plays on the owner.
// Needs the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <span>
#include <tuple>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: 1.14d's rows: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kDruid, "Headless", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kDruid;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);   // out of town
    world.level = &open;
    world.fight.enter(&open);
    std::uint32_t now = 1000 * kTickMs;
    world.tick({}, now, now - kTickMs);
    auto& values = world.actor->character.stats.values;
    for (const auto& [cycler, missile, stat, max_stat] : { std::tuple{ "CorpseCycler", "recycler delay", d2d::d2s::kLife, d2d::d2s::kMaxLife },
                                                         std::tuple{ "VineCycler", "vine recycler delay", d2d::d2s::kMana, d2d::d2s::kMaxMana } }) {
        const auto* skill = data->skills.get(data->skills.by_name.at(cycler));
        assert(skill && skill->srvstfunc == d2d::rules::ServerStartFunction::kCorpseCycler && skill->srvmissilea == missile);
        const auto& row = data->missiles.at(missile);
        assert(row.param1 == 45);
        constexpr int kLevel = 4;
        values[max_stat] = std::int64_t(1000) << 8;                   // a gain of whole points, far over regen's
        const int most = 1000;
        values[stat] = 1 << 8;
        world.fight.launch(row, *skill, kLevel, world.actor->player.x, world.actor->player.y, 0, 0, row.range, now);
        const int gain = d2d::rules::mul_div(most, d2d::rules::calc_dm(skill->par[0], skill->par[1], kLevel), 100);
        int healed_at = -1, jump = 0;
        for (int frame = 1; frame < 60 && healed_at < 0; ++frame) {
            const auto before = values[stat];
            now += kTickMs;
            world.tick({}, now, now - kTickMs);
            if (values[stat] - before >= std::int64_t(2) << 8) { healed_at = frame; jump = int((values[stat] - before) >> 8); }
        }
        std::printf("%s: +%d at frame %d (want +%d)\n", cycler, jump, healed_at, gain);
        assert(gain >= 2 && healed_at >= 44 && healed_at <= 47 && (jump == gain || jump == gain - 1));   // the heal on whole points, regen's part off
        const int overlay = data->overlay_row(row.prog_overlay);                                         // steallife / stealmana on the owner
        assert(overlay > 0 && std::ranges::contains(world.actor->one_shots, overlay, &d2d::rules::OneShot::row));
    }
    std::printf("test_recycler_heal: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_recycler_heal: %s\n", error.what());
    return 1;
}
