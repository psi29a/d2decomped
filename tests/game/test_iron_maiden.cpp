// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Iron Maiden's hit back, as tools/emu/iron_maiden.py runs game.exe's
// FUN_005c5f50: `--iron-maiden` reads its cases (`wearer merc struck phys
// calc1 calc2 calc3`, 256ths) and prints each with `| damage result class`
// (none: `- - -`): rules::iron_maiden_fine with IronMaiden's ResultFlags |
// 0x20 and HitClass.
// Without arguments, with the game data: a zombie under Iron Maiden striking
// a foe through monster_update takes the reflected physical back.

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int iron_maiden_cases(const GameData& data) {
    const auto& maiden = *data.skills.get(data.skills.by_name.at("Iron Maiden"));
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int wearer = 0, merc = 0, struck = 0, phys = 0, calc1 = 0, calc2 = 0, calc3 = 0;
        if (!(fields >> wearer >> merc >> struck >> phys >> calc1 >> calc2 >> calc3)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        if (const auto back = d2d::rules::iron_maiden_fine(wearer == 1, merc != 0, struck == 0, phys, calc1, calc2, calc3))
            std::printf("%s | %d %d %d\n", line.c_str(), *back, (maiden.result_flags | 0x20) & 0xffff, maiden.hit_class);
        else
            std::printf("%s | - - -\n", line.c_str());
    }
    return 0;
}

// A zombie under Iron Maiden (at 5) strikes the player, who takes a quarter off physical. The reflect
// is the zombie's domeleedamage event, after the player's own absorbs and reduction (FUN_0057c6c0 runs
// FUN_0057c1e0 first): its blow carries `reflect` and Fight::hurt_side sends back MulDiv(what's left of
// the physical, calc1, 100). With Bone Armor up, its pool comes off the physical first.
int strike_check(const GameData& data) {
    const auto zombie_row = data.monsters.by_id.find("zombie1");
    assert(zombie_row != data.monsters.by_id.end());
    const int maiden_id = data.skills.by_name.at("Iron Maiden");
    const auto& maiden = *data.skills.get(maiden_id);
    d2d::rules::Rng rng{ 3 };
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    std::vector<Monster> pack{ make_monster(data, int(zombie_row->second), 10.f, 10.f, rng, 0) };
    auto& zombie = pack[0];
    zombie.stats.hit_points = zombie.hit_points = 100000;
    zombie.id = 0x7001;                                               // the fight's unit id: the blow names it
    zombie.curse = { maiden_id, 5, 1u << 30 };
    zombie.reflect_pct = d2d::rules::eval_calc(data.skills, maiden.calc[0], {}, maiden_id, 5);
    assert(zombie.reflect_pct > 0 && zombie.target(data).res[0] < 100);
    set_mode(data, zombie, "A1", 0);
    std::array<Foe, 1> foes{};
    foes[0].x = 11.f; foes[0].y = 10.f;
    foes[0].fighter.dr_pct = 25;
    const Crowd crowd;
    std::vector<Missile> missiles;
    for (std::uint32_t frame = 1; frame <= 1000 && foes[0].hits == 0; ++frame)
        monster_update(data, here, zombie, foes, rng, frame * 40, 0.04f, crowd, missiles, pack);
    assert(foes[0].hits == 1 && !foes[0].blows.empty());
    const auto blow = foes[0].blows.back();
    assert(blow.reflect && zombie.hit_points == 100000 && zombie.life_frac == 0);   // not yet: the absorbs come first

    // The blow as the player's frame resolves it, the zombie in the fight.
    World world(&data, -1, -1);
    d2d::rules::Rng made_rng{ 7 };
    auto made = new_character(data, 2, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 2; character.name = "Headless";
    character.header = made.header; character.stats = made.stats; character.items = made.items;
    world.enter(character);
    auto& fight = world.fight;
    const auto resolve = [&](int pool) {
        fight.monsters.clear();
        fight.monsters.push_back(zombie);
        auto& actor = *world.actor;
        actor.character.stats.values[d2d::d2s::kLife] = actor.character.stats.values[d2d::d2s::kMaxLife] = 100000 << 8;
        const int bone_armor = data.skills.by_name.at("Bone Armor");
        actor.self_states.clear();
        actor.absorb_pool = pool; actor.absorb_frac = 0; actor.absorb_skill = pool > 0 ? bone_armor : -1;
        if (pool > 0) actor.self_states.push_back({ bone_armor, 1, 1u << 30 });
        std::array<Foe, 2> side{};
        side[0] = foes[0];
        side[0].blows = { blow };
        fight.hurt_side(side, 5000);
        const auto& struck = fight.monsters[0];
        return (std::int64_t(100000 - struck.hit_points) << 8) + struck.life_frac;   // life_frac: the 256ths of damage carried (hurt)
    };
    const auto reflected = [&](int raw) {
        const int physical = d2d::rules::reduce_fine_signed(raw, 25, 0);
        return d2d::rules::resisted_fine(d2d::rules::iron_maiden_fine(true, false, false, physical, zombie.reflect_pct, 0, 0).value_or(0), zombie.target(data).res[0]);
    };
    const std::int64_t lost = resolve(0);
    assert(blow.raw > 0 && reflected(blow.raw) > 0 && lost == reflected(blow.raw));
    std::printf("OK: a zombie under Iron Maiden lost %lld/256 off a blow of %d/256 raw physical (calc1 %d)\n", static_cast<long long>(lost), blow.raw, zombie.reflect_pct);
    const int pool = std::max(blow.raw / 256 / 2, 1);                // Bone Armor takes half of it first
    const std::int64_t armored = resolve(pool);
    assert(armored == reflected(blow.raw - pool * 256) && armored < lost);
    std::printf("OK: with Bone Armor's %d first, it lost %lld/256\n", pool, static_cast<long long>(armored));

    // A hit back of 0 (calc1 0) still raises event 4: its record is 0x4021, and FUN_0057cee0's
    // 0x4000 branch doesn't look at the damage.
    std::vector<Monster> second{ make_monster(data, int(zombie_row->second), 10.f, 10.f, rng, 0) };
    auto& unhurt = second[0];
    unhurt.stats.hit_points = unhurt.hit_points = 100000;
    unhurt.id = 0x7002;
    unhurt.curse = { maiden_id, 5, 1u << 30 };
    unhurt.reflect_pct = 0;
    set_mode(data, unhurt, "A1", 0);
    std::array<Foe, 1> struck{};
    struck[0].x = 11.f; struck[0].y = 10.f;
    for (std::uint32_t frame = 1; frame <= 1000 && struck[0].hits == 0; ++frame)
        monster_update(data, here, unhurt, struck, rng, frame * 40, 0.04f, crowd, missiles, second);
    assert(struck[0].hits == 1 && !struck[0].blows.empty() && struck[0].blows.back().reflect);
    fight.monsters.clear();
    fight.monsters.push_back(unhurt);
    fight.monsters[0].struck_event = false;
    world.actor->self_states.clear(); world.actor->absorb_pool = 0;
    std::array<Foe, 2> zero_side{};
    zero_side[0] = struck[0];
    fight.hurt_side(zero_side, 6000);
    assert(fight.monsters[0].hit_points == 100000 && fight.monsters[0].life_frac == 0 && fight.monsters[0].struck_event);
    std::puts("OK: a hit back of 0 still raises the zombie's event 4");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--iron-maiden";
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--iron-maiden needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--iron-maiden needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    return cases ? iron_maiden_cases(*data) : strike_check(*data);
} catch (const std::exception& error) {
    std::printf("test_iron_maiden: %s\n", error.what());
    return 1;
}
