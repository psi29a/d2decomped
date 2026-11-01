// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Missiles as game.exe makes them, with the game's MPQs (D2_MPQ_DIR; the
// 1.14d ones with D2_PATCH_INSTALLER): a nova's ring, the breath's flames,
// Charged Strike's bolts, the ammo a shot takes, and what monsters cast
// beside them (Baal's tentacles).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
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

    {
        // A breath's flame (FUN_005cc4e0 -> FUN_005cc2e0) lasts calc1 frames, under 1 its row's
        // Param2 + lvl - 1: Fetish Inferno (no calc1) 7 + lvl - 1, not its Range 30.
        const auto shaman_row = data->monsters.by_id.find("fetishshaman1");
        const auto breath = data->skills.by_name.find("FetishInferno");
        assert(shaman_row != data->monsters.by_id.end() && breath != data->skills.by_name.end());
        const auto& shaman_type = data->monsters.types[shaman_row->second];
        const auto slot = std::ranges::find(shaman_type.skill, std::string("FetishInferno")) - shaman_type.skill.begin();
        const int lvl = d2d::rules::monster_skill_level(shaman_type.sk_lvl[std::size_t(slot)], 0);
        d2d::rules::Rng breath_rng{ 5 };
        Level here{};
        here.id = d2d::rules::level_ids::kBloodMoor;
        std::vector<Monster> pack{ make_monster(*data, int(shaman_row->second), 10.f, 10.f, breath_rng, 0) };
        pack[0].breath = breath->second; pack[0].breath_until = 4000; pack[0].breath_next = 0;
        std::array<Foe, 1> foes{};
        foes[0].x = 12.f; foes[0].y = 10.f;
        const Crowd crowd;
        std::vector<Missile> missiles;
        monster_update(*data, here, pack[0], foes, breath_rng, 40, 0.04f, crowd, missiles, pack);
        assert(missiles.size() == 1 && missiles[0].info->param2 == 7 && missiles[0].info->range == 30);
        assert(missiles[0].dies - 40 == std::uint32_t(7 - 1 + lvl) * 40);
        std::printf("OK: a breath's flame lasts Param2 + lvl - 1 frames\n");
    }

    {
        // A monster's Charged Bolt (do 17, FUN_005c9300: Mephisto's PrimeBolt): calc1 at the
        // target, each wandering by its index (FUN_005c9290), not turned at random.
        const auto mephisto_row = data->monsters.by_id.find("mephisto");
        const auto bolt_skill = data->skills.by_name.find("PrimeBolt");
        assert(mephisto_row != data->monsters.by_id.end() && bolt_skill != data->skills.by_name.end());
        d2d::rules::Rng bolt_rng{ 9 };
        Level here{};
        here.id = d2d::rules::level_ids::kBloodMoor;
        std::vector<Monster> pack{ make_monster(*data, int(mephisto_row->second), 10.f, 10.f, bolt_rng, 0) };
        pack[0].skill = bolt_skill->second;
        set_mode(*data, pack[0], "A2", 0);                           // its Sk2mode
        std::array<Foe, 1> foes{};
        foes[0].x = 14.f; foes[0].y = 10.f;
        const Crowd crowd;
        std::vector<Missile> missiles;
        for (std::uint32_t frame = 1; frame <= 100 && missiles.empty(); ++frame)
            monster_update(*data, here, pack[0], foes, bolt_rng, frame * 40, 0.04f, crowd, missiles, pack);
        assert(!missiles.empty());
        for (std::size_t k = 0; k < missiles.size(); ++k)
            assert(missiles[k].bolt == int(k) && missiles[k].to && std::abs(missiles[k].to->first - foes[0].x) < 0.01f && std::abs(missiles[k].to->second - foes[0].y) < 0.01f);
        std::printf("OK: a monster's charged bolts wander by their index\n");
    }

    {
        // Baal's tentacles (FUN_005d2c20): one class a cast (FUN_0063efa0's base 0x220 case),
        // rand(2) + the difficulty down the chain from baaltentacle1 (0x232).
        const auto baal_row = data->monsters.by_id.find("baalcrab");
        const auto tentacle_skill = data->skills.by_name.find("Baal Tentacle");
        const auto first = data->monsters.by_id.find("baaltentacle1");
        assert(baal_row != data->monsters.by_id.end() && tentacle_skill != data->skills.by_name.end() && first != data->monsters.by_id.end());
        assert(baal_row->second == 0x220 && first->second == 0x232);
        for (int difficulty = 0; difficulty < 3; ++difficulty)
            for (std::uint32_t seed = 1; seed <= 8; ++seed) {
                d2d::rules::Rng tentacle_rng{ seed };
                Level here{};
                here.id = d2d::rules::level_ids::kBloodMoor;
                std::vector<Monster> pack{ make_monster(*data, int(baal_row->second), 10.f, 10.f, tentacle_rng, difficulty) };
                pack[0].skill = tentacle_skill->second;
                pack[0].seed = d2d::rules::Rng{ seed };
                set_mode(*data, pack[0], "S2", 0);                       // its Sk3mode
                std::array<Foe, 1> foes{};
                foes[0].x = 14.f; foes[0].y = 10.f;
                const Crowd crowd;
                std::vector<Missile> missiles;
                std::vector<Monster> born;
                for (std::uint32_t frame = 1; frame <= 100 && born.empty(); ++frame)
                    monster_update(*data, here, pack[0], foes, tentacle_rng, frame * 40, 0.04f, crowd, missiles, pack, &born);
                assert(born.size() >= std::size_t(2 + difficulty) && born.size() <= std::size_t(4 + difficulty));
                const int step = born[0].type - int(first->second);
                assert(step == difficulty || step == difficulty + 1);
                assert(std::ranges::all_of(born, [&](const Monster& tentacle) { return tentacle.type == born[0].type; }));
            }
        std::printf("OK: Baal's tentacles are one class a cast, rand(2) + the difficulty down the chain\n");
    }

    // A new barbarian in the camp (20 dexterity: the ammo checks lean on it).
    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    for (std::uint32_t tick = 1; tick <= 25; ++tick) world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);

    // A monster's Hydra (FUN_005ca910 -> FUN_0056d940 -> FUN_005b2f20, radius -1): three heads on
    // the target's subtile; hydra1 is SizeX 0, so none blocks another.
    {
        Spawning::LevelState camp;
        bool kept = false;
        for (int tries = 0; tries < 200 && !(kept = keep_camp_seeds(*data, data->town, camp)); ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(kept);
        camp.up.assign(camp.room_seeds.size(), true);
        const auto council_row = data->monsters.by_id.find("councilmember1");
        const auto hydra_skill = data->skills.by_name.find("Hydra");
        assert(council_row != data->monsters.by_id.end() && hydra_skill != data->skills.by_name.end());
        const float foe_x = world.actor->player.x, foe_y = world.actor->player.y;
        d2d::rules::Rng hydra_rng{ 11 };
        std::vector<Monster> pack{ make_monster(*data, int(council_row->second), foe_x - 2, foe_y, hydra_rng, 0) };
        pack[0].skill = hydra_skill->second;
        set_mode(*data, pack[0], "S1", 0);                                // its Sk1mode
        std::array<Foe, 1> foes{};
        foes[0].x = foe_x; foes[0].y = foe_y;
        const Crowd crowd;
        std::vector<Missile> missiles;
        std::vector<Monster> born;
        for (std::uint32_t frame = 1; frame <= 100 && born.empty(); ++frame)
            monster_update(*data, data->town, pack[0], foes, hydra_rng, frame * 40, 0.04f, crowd, missiles, pack, &born, nullptr, nullptr, &camp);
        const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
        assert(born.size() == 3);
        assert(std::ranges::all_of(born, [&](const Monster& head) { return subtile(head.unit.x) == subtile(foe_x) && subtile(head.unit.y) == subtile(foe_y); }));
        std::printf("OK: a monster's three hydras stand on the target's subtile\n");

        // Teleportation (mod 26, FUN_005b11f0, the driver's step 3): under 30 % life it casts
        // MonTeleport (S1) at a spot FUN_0054dc40 found in its room, and lands there (FUN_005ccc80).
        // The Blood Moor with every room up (monsters take no target in town).
        const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
        assert(moor && !moor->rooms.empty());
        Spawning::LevelState moor_rooms;
        moor_rooms.up.assign(moor->rooms.size(), true);
        for (std::size_t room = 0; room < moor->rooms.size(); ++room) moor_rooms.room_seeds.push_back(d2d::rules::Rng{ std::uint32_t(room + 1) });
        const auto& middle = moor->rooms[moor->rooms.size() / 2];
        const auto [zombie_x, zombie_y] = moor->nearest_free(float(middle.x) + float(middle.width) / 2, float(middle.y) + float(middle.height) / 2);
        const auto zombie_row = data->monsters.by_id.find("zombie1");
        assert(zombie_row != data->monsters.by_id.end());
        std::array<Foe, 1> near_foe{};
        near_foe[0].x = zombie_x + 3; near_foe[0].y = zombie_y;
        bool cast = false, landed = false;
        for (std::uint32_t seed = 1; seed <= 40 && !landed; ++seed) {
            d2d::rules::Rng zombie_rng{ seed };
            std::vector<Monster> zombies{ make_monster(*data, int(zombie_row->second), zombie_x, zombie_y, zombie_rng, 0) };
            auto& zombie = zombies[0];
            zombie.boss = d2d::rules::Boss::unique;
            zombie.mods = { d2d::rules::umod::teleport };
            zombie.seed = d2d::rules::Rng{ seed * 7 };
            zombie.hit_points = std::max(zombie.stats.hit_points / 5, 1);
            for (std::uint32_t frame = 1; frame <= 400 && !landed; ++frame) {
                monster_update(*data, *moor, zombie, near_foe, zombie_rng, frame * 40, 0.04f, crowd, missiles, zombies, &born, nullptr, nullptr, &moor_rooms);
                if (zombie.at_point && zombie.skill == data->skills.by_name.at("MonTeleport")) {
                    cast = true;
                    landed = std::abs(zombie.unit.x - (float(zombie.skill_x) + 0.5f) / 5) < 0.01f && std::abs(zombie.unit.y - (float(zombie.skill_y) + 0.5f) / 5) < 0.01f;
                }
            }
        }
        assert(cast && landed);
        std::printf("OK: a teleporting unique casts MonTeleport at its room's spot and lands there\n");
    }

    // A chill (FUN_0057af80) rolls state 107 (shatter) on the monster's own seed: under 20 of
    // % 100 on, else off; one draw a chill.
    {
        const auto zombie_row = data->monsters.by_id.find("zombie1");
        assert(zombie_row != data->monsters.by_id.end() && !data->mon_npc[zombie_row->second].dead_col);
        int shattered = 0;
        for (std::uint32_t seed = 1; seed <= 50; ++seed) {
            d2d::rules::Rng make_rng{ seed };
            Monster chilled = make_monster(*data, int(zombie_row->second), world.actor->player.x + 4, world.actor->player.y, make_rng, 0);
            chilled.id = 0x7ffff2;
            chilled.hit_points = 1000; chilled.stats.hit_points = 1000;
            chilled.seed = d2d::rules::Rng{ seed };
            world.fight.monsters.push_back(chilled);
            d2d::rules::Rng expected{ seed };
            const bool want = expected.next() % 100 < 20;
            world.fight.land(world.fight.monsters.size() - 1, d2d::rules::Blow{ .hit = true, .damage = 1, .chill_ticks = 10 }, true, 5000 * kTickMs, -1);
            const auto& after = world.fight.monsters.back();
            assert(after.shatter == want && after.seed.low == expected.low);
            shattered += want;
            world.fight.monsters.pop_back();
        }
        assert(shattered > 0 && shattered < 50);
        // Holy Freeze's pulse (FUN_005d0920 -> FUN_005d07c0): after its hit, its own roll is the last word.
        world.actor->aura = data->skills.by_name.at("Holy Freeze");
        world.actor->aura_next = 0;
        const auto kept_levels = world.fight.skill_level;
        world.fight.skill_level = [](int) { return 5; };                   // a barbarian has none
        d2d::rules::Rng make_rng{ 3 };
        Monster frozen = make_monster(*data, int(zombie_row->second), world.actor->player.x + 1, world.actor->player.y, make_rng, 0);
        frozen.id = 0x7ffff3;
        frozen.hit_points = 100000; frozen.stats.hit_points = 100000;
        frozen.seed = d2d::rules::Rng{ 77 };
        world.fight.monsters.push_back(frozen);
        const auto before = world.fight.monsters.back().seed;
        world.fight.aura_pulse(5000 * kTickMs);
        const auto& pulsed = world.fight.monsters.back();
        auto replay = before;
        std::uint32_t last = 0;
        for (int draw = 0; draw < 8 && replay.low != pulsed.seed.low; ++draw) last = replay.next();
        assert(replay.low == pulsed.seed.low && replay.low != before.low && pulsed.shatter == (last % 100 < 20));
        world.fight.monsters.pop_back();
        world.actor->aura = 0;
        world.fight.skill_level = kept_levels;
        std::printf("OK: a chill rolls shatter on the monster's seed, Holy Freeze again after its hit\n");
    }

    // A monster's random curse (FUN_005ce2b0, the Oblivion Knight's MonCurseCast in S2): seed % 5
    // picks the skill, and the stats are the pick's own (bugs.md #43), not the skill's aurastats.
    {
        const auto knight_row = data->monsters.by_id.find("doomknight3");
        const int curse_cast = data->skills.by_name.at("MonCurseCast");
        assert(knight_row != data->monsters.by_id.end());
        const auto stats_of = [&](int skill, int pick) {
            auto stats = world.fight.curse_stats({ skill, 5, ~0u, pick }, world.fight.calc_env());
            std::ranges::sort(stats);
            return stats;
        };
        using StatList = std::vector<std::pair<int, int>>;
        assert((stats_of(91, 4) == StatList{ { 25, -50 }, { 36, -50 }, { 67, 0 }, { 68, 0 } }));     // Lower Resist: Decrepify's list, Param7 0
        assert(stats_of(87, 3).empty() && stats_of(82, 2).empty());                                // Decrepify, Life Tap: events only
        assert((stats_of(66, 0) == StatList{ { 36, -100 } }) && (stats_of(72, 1) == StatList{ { 25, -50 } }));
        assert(std::ranges::any_of(stats_of(91, -1), [](const auto& stat) { return stat.first == 39 && stat.second < 0; }));   // its own aurastats when not a pick
        for (std::uint32_t seed = 1; seed <= 6; ++seed) {
            d2d::rules::Rng knight_rng{ seed };
            Level here{};
            here.id = d2d::rules::level_ids::kBloodMoor;
            std::vector<Monster> pack{ make_monster(*data, int(knight_row->second), 10.f, 10.f, knight_rng, 0) };
            pack[0].skill = curse_cast;
            pack[0].seed = d2d::rules::Rng{ seed };
            set_mode(*data, pack[0], "S2", 0);                           // its Sk6mode
            std::array<Foe, 1> foes{};
            foes[0].x = 12.f; foes[0].y = 10.f;
            const Crowd crowd;
            std::vector<Missile> missiles;
            for (std::uint32_t frame = 1; frame <= 100 && foes[0].cursed.skill < 0; ++frame)
                monster_update(*data, here, pack[0], foes, knight_rng, frame * 40, 0.04f, crowd, missiles, pack);
            d2d::rules::Rng expected{ seed };
            const int pick = int(expected.next() % 5);
            assert(foes[0].cursed.pick == pick && foes[0].cursed.skill == d2d::rules::kMonsterCurses[std::size_t(pick)]);
        }
        std::printf("OK: a monster's random curse takes its pick's own stats\n");
    }

    // Nova's 64 go out along rules::kNovaRing (0x6e1288 / 0x6e1388, FUN_0056d400), not even angles.
    {
        world.fight.pending.clear();
        world.fight.fire(*data->skills.get(data->skills.by_name.at("Nova")), 5000 * kTickMs, world.fight.player_caster(data->skills.by_name.at("Nova")));
        assert(world.fight.pending.size() == 64);
        for (std::size_t way = 0; way < 64; ++way) {
            const auto [aim_x, aim_y] = d2d::rules::kNovaRing[way];
            const auto& shot = world.fight.pending[way];
            assert(std::abs(shot.velocity_x * float(aim_y) - shot.velocity_y * float(aim_x)) < 1e-4f && shot.velocity_x * float(aim_x) + shot.velocity_y * float(aim_y) > 0);
        }
        world.fight.pending.clear();
        std::printf("OK: Nova aims at game.exe's ring\n");

        // A spent stack's slot takes the inventory's next only if it can be worn (FUN_00562a30 -> FUN_0062eaf0):
        // throwing knives need 21 dexterity, a new barbarian has 20.
        auto& items = world.actor->character.items;
        auto& dexterity = world.actor->character.stats.values[d2d::d2s::kDex];
        const auto kept_items = items;
        const auto kept_dexterity = dexterity;
        for (const int dex : { 20, 21 }) {
            items = kept_items; dexterity = dex;
            std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kRightArm; });
            items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm, .identified = true, .quantity = 1 });
            items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kStored, .panel = d2d::d2s::item_panel::kInventory, .identified = true, .quantity = 9 });
            world.fight.out_of_ammo(items.size() - 2);
            assert(std::ranges::count_if(items, [](const d2d::d2s::Item& item) { return item.code == "tkf" && item.location == d2d::d2s::item_location::kEquipped; }) == (dex >= 21 ? 1 : 0));
        }
        // An empty quiver in hand: the shot is refused and the inventory's quiver swapped in (FUN_005da8b0 -> FUN_00580310).
        items = kept_items;
        std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm); });
        items.push_back({ .code = "sbw", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kLeftArm, .identified = true });
        items.push_back({ .code = "aqv", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm, .identified = true, .quantity = 0 });
        items.push_back({ .code = "aqv", .location = d2d::d2s::item_location::kStored, .panel = d2d::d2s::item_panel::kInventory, .identified = true, .quantity = 50 });
        assert(!world.fight.cast_missile(0, world.actor->player.x + 3, world.actor->player.y, 5000 * kTickMs));
        assert(std::ranges::count_if(items, [](const d2d::d2s::Item& item) { return item.code == "aqv"; }) == 1 && world.fight.ammo_index() >= 0);
        // Double Throw (FUN_005d88b0 -> FUN_0056ecb0 last argument 1 -> FUN_0056c3f0): each hand's
        // throw spends one; the hand that runs out is swapped as any spent stack.
        items = kept_items;
        std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm); });
        items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm, .identified = true, .quantity = 1 });
        items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kLeftArm, .identified = true, .quantity = 5 });
        world.fight.pending.clear();
        world.actor->cast_x = world.actor->player.x + 3; world.actor->cast_y = world.actor->player.y;
        const int double_throw = data->skills.by_name.at("Double Throw");
        world.fight.spot(*data->skills.get(double_throw), 5000 * kTickMs, world.fight.player_caster(double_throw));
        assert(world.fight.pending.size() == 2);
        assert(std::ranges::none_of(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kRightArm; }));
        assert(std::ranges::any_of(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kLeftArm && item.quantity == 4; }));
        world.fight.pending.clear();
        items = kept_items; dexterity = kept_dexterity;
        std::printf("OK: a spent stack is swapped for a wearable next, on running out or a refused shot\n");
    }

    // Charged Strike's bolts (FUN_005db850) are Charged Bolt's: flags 0x21, FUN_005c9290 with
    // each one's index, at the subtile twice the struck monster's less the player's.
    {
        const auto& strike = *data->skills.get(data->skills.by_name.at("Charged Strike"));
        Monster struck;
        struck.type = 0;
        struck.id = 0x7ffff1;
        struck.hit_points = 100;
        struck.unit.x = world.actor->player.x + 2; struck.unit.y = world.actor->player.y + 1;
        world.fight.monsters.push_back(struck);
        world.fight.pending.clear();
        world.fight.strike_bolts(strike, world.fight.monsters.size() - 1, 5000 * kTickMs);
        const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
        const int to_x = 2 * subtile(struck.unit.x) - subtile(world.actor->player.x), to_y = 2 * subtile(struck.unit.y) - subtile(world.actor->player.y);
        assert(!world.fight.pending.empty());
        for (std::size_t k = 0; k < world.fight.pending.size(); ++k) {
            const auto& bolt = world.fight.pending[k];
            assert(bolt.bolt == int(k) && bolt.to && subtile(bolt.to->first) == to_x && subtile(bolt.to->second) == to_y);
        }
        world.fight.pending.clear();
        world.fight.monsters.pop_back();
        std::printf("OK: Charged Strike's bolts wander as Charged Bolt's\n");

        // Lightning Enchanted's (FUN_005a29a0): 2 at the subtile beside it each way (0x6e2188 / 0x6e2178), wandering too.
        Monster enchanted = struck;
        enchanted.boss = d2d::rules::Boss::unique;
        enchanted.mods = { d2d::rules::umod::lightning };
        enchanted.last_hp = 100; enchanted.hit_points = 90;
        world.fight.monsters.push_back(enchanted);
        world.fight.boss_events({}, 6000 * kTickMs);
        assert(world.fight.pending.size() == 8);
        for (std::size_t k = 0; k < 8; ++k) {
            const auto& bolt = world.fight.pending[k];
            const auto [dir_x, dir_y] = std::array<std::pair<int, int>, 4>{ { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } } }[k / 2];
            assert(bolt.bolt == int(k % 2) && bolt.to && subtile(bolt.to->first) == subtile(enchanted.unit.x) + dir_x && subtile(bolt.to->second) == subtile(enchanted.unit.y) + dir_y);
        }
        world.fight.pending.clear();
        world.fight.monsters.pop_back();
        std::printf("OK: Lightning Enchanted's bolts wander as Charged Bolt's\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
