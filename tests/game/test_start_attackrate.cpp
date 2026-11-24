// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The attackrate a skill's server start puts on its user for the mode it plays (FUN_0056e520, a flags-4
// stat list SetMode frees at the next mode change): Dragon Tail's start (srvstfunc 27, FUN_005d7090)
// gives Param4 as it is, the Bash family's (srvstfunc 32, FUN_005d7ea0) calc3, any other start nothing,
// as tools/emu/skill_start_attackrate.py runs them in game.exe. Always: rules::start_attack_rate on made-up
// rows (without the game data the rest SKIPs and the test passes). With it: Dragon Tail's row gives -40 and an Assassin's kick with it lasts the swing
// rules::attack_ticks gives at 40 points slower (Fight::set_pmode); Bash's row gives 0.

#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;
using d2d::rules::ServerStartFunction;

int main() try {
    {
        d2d::test::TestData made;
        auto& talon = made.skill(4, "Talon");                                   // the highest id first: the rows don't move after
        talon.srvstfunc = ServerStartFunction::kDragonTalon;                    // another start: no list
        auto& tail = made.skill(1, "Tail");
        tail.srvstfunc = ServerStartFunction::kDragonTail;
        tail.par[3] = -40;
        tail.calc[2] = d2d::rules::compile_calc("7", made.data.skills.names);   // Dragon Tail's start never reads calc3
        auto& bash = made.skill(2, "Bash");
        bash.srvstfunc = ServerStartFunction::kBuildHit;
        bash.par[3] = -40;                                                      // nor the Bash family's Param4
        auto& bash_calc = made.skill(3, "BashCalc");
        bash_calc.srvstfunc = ServerStartFunction::kBuildHit;
        bash_calc.calc[2] = d2d::rules::compile_calc("12", made.data.skills.names);
        talon.par[3] = -40;
        const d2d::rules::CalcEnv env;
        const auto& tables = made.data.skills;
        assert(d2d::rules::start_attack_rate(tables, tail, env, 1) == -40);
        assert(d2d::rules::start_attack_rate(tables, bash, env, 5) == 0);       // calc3 empty: 0
        assert(d2d::rules::start_attack_rate(tables, bash_calc, env, 5) == 12);
        assert(d2d::rules::start_attack_rate(tables, talon, env, 5) == 0);
    }

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    // The rule's checks above passed: only the game-data part is skipped, so the test still passes (tests/README.md).
    if (!mpq_dir) { std::printf("SKIP: D2_MPQ_DIR (the game-data checks)\n"); return 0; }
    const fs::path data_dir = mpq_dir;
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (the game-data checks)\n"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);
    const int tail_id = data->skills.by_name.at("Dragon Tail"), bash_id = data->skills.by_name.at("Bash");
    assert(tail_id == 270 && bash_id == 126);
    const auto* tail = data->skills.get(tail_id);
    assert(tail && tail->anim == "KK");
    assert(d2d::rules::start_attack_rate(data->skills, *tail, {}, 1) == -40);
    assert(d2d::rules::start_attack_rate(data->skills, *data->skills.get(bash_id), {}, 20) == 0);

    d2d::rules::Rng rng(7);
    const auto made = new_character(*data, d2d::d2s::kAssassin, "Headless", false, true, rng);
    Character hero;
    hero.character_class = d2d::d2s::kAssassin;
    hero.name = "Headless";
    hero.header = made.header;
    hero.stats = made.stats;
    hero.items = made.items;
    hero.panel = panel_stats(*data, hero.header, hero.items, hero.stats);
    World world(&*data, -1, -1);
    world.enter(hero);
    world.actor->character.appearance = data->look_of(hero.items);
    world.fight.update_fighters(1000 * kTickMs);
    auto& actor = *world.actor;
    actor.player_combat.ias = 0; actor.player_combat.wsm = 0; actor.player_combat.sias = 0;
    const auto& kick = world.fight.player_anim(kModeKK);
    assert(kick.frames > 0);
    const int rate = int(kick.speed ? kick.speed : 256);
    std::uint32_t now = 1000 * kTickMs;
    actor.swing_skill = 0;
    world.fight.set_pmode(kModeKK, now);
    const auto plain = actor.pmode_until - now;
    assert(plain == std::uint32_t(d2d::rules::attack_ticks(int(kick.frames), rate, 0, 0, 0)) * 40);
    now += 1000;
    actor.swing_skill = tail_id;
    world.fight.set_pmode(kModeKK, now);
    const auto with_tail = actor.pmode_until - now;
    assert(with_tail == std::uint32_t(d2d::rules::attack_ticks(int(kick.frames), rate, 0, 0, -40)) * 40);
    assert(with_tail > plain);
    std::printf("test_start_attackrate: Dragon Tail's kick %u ms, a plain kick %u ms\n", unsigned(with_tail), unsigned(plain));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_start_attackrate: %s\n", error.what());
    return 1;
}
