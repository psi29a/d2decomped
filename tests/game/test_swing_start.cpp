// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// How long a player's attack swing is and when it strikes: Fight::set_pmode for an attack mode (FUN_005539b0 files
// the strike and the end of the swing from the start frame FUN_00623b10 returns).
//   `--swing-start`: every class x main-hand weapon x attack mode x IAS x weapon speed (WSM), one line each
//   `D class mode weapon ias wsm rate record frames speed ticks strike`: the swing's length and the tick the strike lands on
//   (counted from the tick the mode is set), as `tools/emu/swing_start.py compare` reads them for game.exe.
//   no argument: SKIPs without D2_MPQ_DIR and the 1.14d patch installer.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

using namespace d2d::game;

namespace {

struct Cell {
    int ticks = 0, strike = 0;   // the swing's length and the strike's tick, from the tick the mode is set
    int rate = 0;
    std::string record;
    unsigned frames = 0, speed = 0;
    bool record_found = false;
};

// A character of class `cls` wearing `weapon` (a weapons.txt code, "" = barehanded) in the main hand, no shield; `strong`: stats that meet any requirement.
struct Hero {
    Character character;
    explicit Hero(const GameData& data, int cls, const std::string& weapon, bool strong = true) {
        d2d::rules::Rng rng(7);
        auto made = new_character(data, cls, "Headless", false, true, rng);
        character.character_class = cls;
        character.name = "Headless";
        character.header = made.header;
        character.stats = made.stats;
        if (strong)   // every weapon of the grid is usable: the swing is timed by the weapon in hand, which game.exe takes only if usable
            for (const int stat : { d2d::d2s::kStr, d2d::d2s::kDex, d2d::d2s::kLevel }) character.stats.values[std::size_t(stat)] = 255;
        character.items = made.items;
        const d2d::d2s::Item* proto = character.items.empty() ? nullptr : &character.items.front();
        d2d::d2s::Item put = proto ? *proto : d2d::d2s::Item{};
        std::erase_if(character.items, [](const auto& item) { return item.location == 1 && (item.slot == 4 || item.slot == 5); });   // main hand and shield out
        if (!weapon.empty()) { put.code = weapon; put.location = 1; put.slot = 4; character.items.push_back(put); }
        character.panel = panel_stats(data, character.header, character.items, character.stats);
    }
};

}   // namespace

int main(int argc, char** argv) try {
    const bool grid = argc > 1 && std::strcmp(argv[1], "--swing-start") == 0;
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR\n"); return D2D_TEST_SKIPPED; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    // none, sword (1hs), dagger (1ht), great sword (2hs), spear (2ht), staff (stf), bow, crossbow (xbw), claws (ht1)
    const std::vector<std::string> weapons = { "", "ssd", "dgr", "gsd", "spr", "sst", "sbw", "lxb", "ktr" };
    const std::array<int, 5> modes = { kModeA1, kModeS1, kModeS3, kModeS4, kModeKK };   // the attack modes d2d plays (attack_mode)
    const std::array<int, 10> ias_list = { 0, 10, 20, 30, 40, 60, 80, 100, 150, 200 };
    const std::array<int, 7> wsm_list = { -30, -20, -10, 0, 10, 20, 30 };

    const auto one = [&](World& world, int mode, int ias, int wsm, std::uint32_t& now) {
        auto& a = *world.actor;
        Cell cell;
        now += 1000;
        a.player_combat.ias = ias; a.player_combat.wsm = wsm; a.player_combat.sias = 0;
        const auto& anim = world.fight.player_anim(mode);
        if (!anim.frames) return cell;
        world.fight.set_pmode(mode, now);
        cell.record_found = true;
        cell.record = anim.name;
        cell.frames = anim.frames;
        cell.speed = anim.speed ? anim.speed : 256;
        cell.rate = d2d::rules::attack_speed(int(cell.speed), ias, wsm, 0);
        cell.ticks = int((a.pmode_until - now) / 40);
        cell.strike = int((a.action_ms + 39) / 40);
        return cell;
    };

    if (grid) {
        for (int cls = 0; cls < 7; ++cls)
            for (const auto& weapon : weapons) {
                if (weapon == "ktr" && cls != d2d::d2s::kAssassin) continue;   // claws are the Assassin's: no other class can wear them
                Hero hero(*data, cls, weapon);
                World world(&*data, -1, -1);
                world.enter(hero.character);
                world.actor->character.appearance = data->look_of(hero.character.items);
                std::uint32_t now = 1000 * kTickMs;
                for (const int mode : modes)
                    for (const int ias : ias_list)
                        for (const int wsm : wsm_list) {
                            const auto cell = one(world, mode, ias, wsm, now);
                            if (!cell.record_found) continue;
                            std::printf("D %d %d %s %d %d %d %s %u %u %d %d\n", cls, mode, weapon.empty() ? "none" : weapon.c_str(), ias, wsm, cell.rate,
                                        cell.record.c_str(), cell.frames, cell.speed, cell.ticks, cell.strike);
                        }
            }
        return 0;
    }
    std::printf("test_swing_start: pass --swing-start for the oracle grid\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "test_swing_start: %s\n", e.what());
    return 1;
}
