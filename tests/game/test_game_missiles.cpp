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
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

// Boss and missile cases against the real data; skips without D2_MPQ_DIR.
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
        // Baal's Vile Effigy (FUN_005fc860): about the unit he's at (FUN_00553540), +- 12
        // subtiles by two draws of his seed, a third of his life, no regen (stat 0x4a 0).
        const auto baal_row = data->monsters.by_id.find("baalcrab");
        assert(baal_row != data->monsters.by_id.end());
        d2d::rules::Rng effigy_rng{ 5 };
        Level here{};
        auto baal = make_monster(*data, int(baal_row->second), 10.f, 10.f, effigy_rng, 0);
        baal.seed = d2d::rules::Rng{ 9 };
        auto draws = baal.seed;
        const int want_x = 200 + int(draws.next() % 24) - 12, want_y = 300 + int(draws.next() % 24) - 12;
        const auto clone = vile_effigy(*data, here, baal, 200, 300, effigy_rng, 1);
        assert(clone && int(std::floor(clone->unit.x * 5)) == want_x && int(std::floor(clone->unit.y * 5)) == want_y);
        assert(clone->stats.regen == 0 && clone->hit_points == std::max(baal.hit_points / 3, 1));
        assert(clone->no_exp && clone->no_drop);   // unit flags 0x4020000 (005fca71)
        std::printf("OK: the Vile Effigy rises about Baal's target, with no regen, experience or drop\n");

        // A whipped minion turns itself (FUN_00574370): the same unit id, group and spot, the new class.
        const auto minion_row = data->monsters.by_id.find("minion1");
        const auto suicide_row = data->monsters.by_id.find("suicideminion1");
        assert(minion_row != data->monsters.by_id.end() && suicide_row != data->monsters.by_id.end());
        auto minion = make_monster(*data, int(minion_row->second), 12.f, 7.f, effigy_rng, 0);
        minion.id = 4242; minion.leader = 3;
        morph_monster(*data, minion, int(suicide_row->second), effigy_rng, 1);
        assert(minion.type == int(suicide_row->second) && minion.id == 4242 && minion.leader == 3 && minion.unit.x == 12.f && minion.unit.y == 7.f);
        std::printf("OK: a morph keeps the unit, its group and its spot\n");
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

    {
        // Resurrect (do 97, FUN_005ccb10 -> FUN_005cc960): the corpse back at
        // full life with unit flags 0x4020000: no experience, no drop.
        const auto shaman_row = data->monsters.by_id.find("fallenshaman1");
        const auto fallen_row = data->monsters.by_id.find("fallen1");
        const auto raise = data->skills.by_name.find("Resurrect");
        const auto seq = shaman_row == data->monsters.by_id.end() ? data->mon_seqs.end()
                                                                  : data->mon_seqs.find(data->monsters.types[std::size_t(shaman_row->second)].sk_mode[0]);
        assert(shaman_row != data->monsters.by_id.end() && fallen_row != data->monsters.by_id.end() && raise != data->skills.by_name.end());
        assert(seq != data->mon_seqs.end() && !seq->second.empty());
        d2d::rules::Rng raise_rng{ 3 };
        Level here{};
        here.id = d2d::rules::level_ids::kBloodMoor;
        std::vector<Monster> pack{ make_monster(*data, int(shaman_row->second), 10.f, 10.f, raise_rng, 0),
                                   make_monster(*data, int(fallen_row->second), 11.f, 10.f, raise_rng, 0) };
        pack[1].hit_points = 0;
        set_mode(*data, pack[1], "DD", 0);
        pack[0].skill = raise->second;
        pack[0].skill_unit = 1;
        set_mode(*data, pack[0], seq->second.front().mode, 0);
        std::array<Foe, 1> foes{};
        foes[0].x = 30.f; foes[0].y = 30.f;
        const Crowd crowd;
        std::vector<Missile> missiles;
        for (std::uint32_t frame = 1; frame <= 100 && !pack[1].alive(); ++frame)
            monster_update(*data, here, pack[0], foes, raise_rng, frame * 40, 0.04f, crowd, missiles, pack);
        assert(pack[1].alive() && pack[1].hit_points == pack[1].stats.hit_points);
        assert(pack[1].no_exp && pack[1].no_drop);
        std::printf("OK: a raised corpse gives no experience and drops nothing\n");
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
            world.fight.monsters.push_back(std::move(chilled));
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
        const auto kept_levels = std::move(world.fight.skill_level);
        world.fight.skill_level = [](int) { return 5; };                   // a barbarian has none
        d2d::rules::Rng make_rng{ 3 };
        Monster frozen = make_monster(*data, int(zombie_row->second), world.actor->player.x + 1, world.actor->player.y, make_rng, 0);
        frozen.id = 0x7ffff3;
        frozen.hit_points = 100000; frozen.stats.hit_points = 100000;
        frozen.seed = d2d::rules::Rng{ 77 };
        world.fight.monsters.push_back(std::move(frozen));
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
        // FUN_00580380 after it: a worn weapon with durability (the switch hand's sword) at 0 breaks; a bow has none.
        items = kept_items;
        std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm || item.slot == d2d::d2s::body_location::kRightArmSwitch); });
        items.push_back({ .code = "sbw", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kLeftArm, .identified = true });
        items.push_back({ .code = "ssd", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArmSwitch, .identified = true });
        items.back().max_durability = 20; items.back().durability = 0;
        assert(!world.fight.cast_missile(0, world.actor->player.x + 3, world.actor->player.y, 5000 * kTickMs));
        const auto sword = std::ranges::find(items, std::string("ssd"), &d2d::d2s::Item::code);
        const auto bow = std::ranges::find(items, std::string("sbw"), &d2d::d2s::Item::code);
        assert(sword != items.end() && d2d::rules::broken(*sword) && bow != items.end() && !d2d::rules::broken(*bow));
        // Double Throw (FUN_005d88b0 -> FUN_0056ecb0 last argument 1 -> FUN_0056c3f0): each hand's
        // throw spends one; the hand that runs out is swapped as any spent stack.
        items = kept_items;
        std::erase_if(items, [](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm); });
        items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm, .identified = true, .quantity = 1 });
        items.push_back({ .code = "tkf", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kLeftArm, .identified = true, .quantity = 5 });
        world.fight.pending.clear();
        world.actor->cast_x = world.actor->player.x + 3; world.actor->cast_y = world.actor->player.y;
        const int double_throw = data->skills.by_name.at("Double Throw");
        auto thrower = world.fight.player_caster(double_throw);
        thrower.level = 3;
        world.fight.spot(*data->skills.get(double_throw), 5000 * kTickMs, thrower);
        assert(world.fight.pending.size() == 2);
        // ... each at the skill's toht (stat 19, FUN_006449f0): ToHit 20 + LevToHit 10 a level past 1.
        assert(std::ranges::all_of(world.fight.pending, [](const Missile& thrown) { return thrown.ar_pct == 20 + 10 * 2; }));
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
        Monster enchanted = std::move(struck);
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

        // Struck in GH: event 7 two frames on (FUN_005a37d0), not at once.
        enchanted.mode = "GH";
        enchanted.bolts_at = 0;
        world.fight.monsters.push_back(std::move(enchanted));
        world.fight.boss_events({}, 7000 * kTickMs);
        assert(world.fight.pending.empty());
        world.fight.boss_events({}, 7001 * kTickMs);
        assert(world.fight.pending.empty());
        world.fight.boss_events({}, 7002 * kTickMs);
        assert(world.fight.pending.size() == 8);
        world.fight.pending.clear();
        world.fight.monsters.pop_back();
        std::printf("OK: Lightning Enchanted struck in GH lets off its bolts 2 frames on\n");

        // Andariel dead (MonUMod 22, FUN_005dfe00 -> FUN_005dfbf0(35, 1, 51)):
        // the monsters within 35 subtiles get killself 1..50 frames on, and die.
        Monster andariel;
        andariel.type = int(data->mon_bin[0x9c]);
        andariel.id = 0x7ffff2;
        andariel.mods = { d2d::rules::umod::questcomplete };
        andariel.unit.x = world.actor->player.x + 3; andariel.unit.y = world.actor->player.y;
        Monster near_one, far_one;
        for (auto* minion : { &near_one, &far_one }) {
            minion->type = 0;
            minion->hit_points = 100;
            minion->unit.y = andariel.unit.y;
        }
        near_one.id = 0x7ffff3; near_one.unit.x = andariel.unit.x + 6;   // 30 subtiles
        far_one.id = 0x7ffff4; far_one.unit.x = andariel.unit.x - 8;     // 40
        andariel.mode = "DT";
        const std::size_t first = world.fight.monsters.size();
        world.fight.monsters.push_back(std::move(andariel));
        world.fight.monsters.push_back(std::move(near_one));
        world.fight.monsters.push_back(std::move(far_one));
        world.fight.boss_events({}, 8000 * kTickMs);
        const std::uint32_t due = world.fight.monsters[first + 1].killself_at;
        assert(due > 8000 * kTickMs && due <= 8050 * kTickMs && world.fight.monsters[first + 2].killself_at == 0);
        world.fight.boss_events({}, due);
        assert(!world.fight.monsters[first + 1].alive() && world.fight.monsters[first + 1].mode == "DT" && world.fight.monsters[first + 2].alive());
        world.fight.monsters.resize(first);
        world.fight.pending.clear();
        std::printf("OK: Andariel's death takes the monsters about her with her\n");

        // Blood Raven dead (0x5dfd90 -> FUN_005dfbf0(35, 25, 125, 1)): only the
        // undead within 35 subtiles get killself, 25..124 frames on.
        const auto& types = data->monsters.types;
        const auto undead_type = std::ranges::find_if(types, [](const auto& type) { return type.undead; });
        const auto living_type = std::ranges::find_if(types, [](const auto& type) { return !type.undead; });
        assert(undead_type != types.end() && living_type != types.end());
        Monster raven;
        raven.type = int(data->mon_bin[0x10b]);
        raven.id = 0x7ffff5;
        raven.mods = { d2d::rules::umod::bloodraven, d2d::rules::umod::questcomplete };
        raven.unit.x = world.actor->player.x + 3; raven.unit.y = world.actor->player.y;
        Monster zombie, fallen;
        for (auto* minion : { &zombie, &fallen }) {
            minion->hit_points = 100;
            minion->unit.x = raven.unit.x + 6;                          // 30 subtiles
            minion->unit.y = raven.unit.y;
        }
        zombie.id = 0x7ffff6; zombie.type = int(undead_type - types.begin());
        fallen.id = 0x7ffff7; fallen.type = int(living_type - types.begin());
        raven.mode = "DT";
        const std::size_t raven_at = world.fight.monsters.size();
        world.fight.monsters.push_back(std::move(raven));
        world.fight.monsters.push_back(std::move(zombie));
        world.fight.monsters.push_back(std::move(fallen));
        world.fight.boss_events({}, 9000 * kTickMs);
        const std::uint32_t raven_due = world.fight.monsters[raven_at + 1].killself_at;
        assert(raven_due >= 9025 * kTickMs && raven_due < 9125 * kTickMs && world.fight.monsters[raven_at + 2].killself_at == 0);
        world.fight.boss_events({}, raven_due);
        assert(!world.fight.monsters[raven_at + 1].alive() && world.fight.monsters[raven_at + 2].alive());
        world.fight.monsters.resize(raven_at);
        world.fight.pending.clear();
        std::printf("OK: Blood Raven's death takes the undead about her, not the rest\n");
    }

    // UberIzual's Chilling Armor (do 18): skill_armor_percent on its defense
    // as a curse's aurastat, and a ReturnFire missile answered with
    // chillingarmorbolt at the shooter (event 1, FUN_005cab40).
    {
        const int chilling = data->skills.by_name.at("Chilling Armor");
        Monster izual;
        izual.type = 0;
        izual.id = 0x7ffff0;
        izual.hit_points = 100;
        izual.unit.x = world.actor->player.x + 3; izual.unit.y = world.actor->player.y;
        world.fight.monsters.push_back(std::move(izual));
        const std::size_t index = world.fight.monsters.size() - 1;
        const auto bare = world.fight.target_of(index);
        const auto arrow = std::ranges::find_if(data->missiles, [](const auto& row) { return row.second.return_fire; });
        assert(arrow != data->missiles.end());
        Missile shot;
        shot.info = &arrow->second;
        world.fight.pending.clear();
        world.fight.monster_return_fire(index, shot, false, 5000 * kTickMs);
        assert(world.fight.pending.empty());                    // no state, no answer
        world.fight.monsters[index].self_state = { chilling, 10, ~0u };
        assert(world.fight.target_of(index).armor_class == bare.armor_class + bare.armor_class * (45 + 5 * 9) / 100);   // ln12: 45 + 5 a level
        world.fight.monster_return_fire(index, shot, false, 5000 * kTickMs);
        assert(world.fight.pending.size() == 1 && world.fight.pending[0].info->name == "chillingarmorbolt" && world.fight.pending[0].velocity_x < 0 && !world.fight.pending[0].friendly);
        world.fight.pending.clear();
        world.fight.monsters.pop_back();
        std::printf("OK: a monster's Chilling Armor raises its defense and answers ReturnFire\n");
    }

    // A monster's regen (stat 0x4a, FUN_00573cb0: life in 256ths x DamageRegen >> 12),
    // its life's one rate each frame (FUN_005a6920); none under state 0x34.
    {
        const auto zombie_row = data->monsters.by_id.find("zombie1");
        d2d::rules::Rng make_rng{ 9 };
        Monster healing = make_monster(*data, int(zombie_row->second), world.actor->player.x + 6, world.actor->player.y, make_rng, 0);
        const int rate = data->monsters.types[zombie_row->second].damage_regen;
        assert(rate > 0 && healing.stats.regen == (healing.stats.hit_points << 8) * rate >> 12);
        healing.hit_points = 1;
        world.fight.monsters.push_back(healing);
        auto& mended = world.fight.monsters.back();
        for (std::uint32_t frame = 0; frame < 250; ++frame) world.fight.monster_dots(6000 + frame * kTickMs, 0.04f);
        assert(mended.hit_points == std::min(1 + healing.stats.regen * 250 / 256, healing.stats.hit_points));
        const int before = mended.hit_points;
        mended.no_heal_until = ~0u;
        for (std::uint32_t frame = 0; frame < 250; ++frame) world.fight.monster_dots(20000 + frame * kTickMs, 0.04f);
        assert(mended.hit_points == before);
        world.fight.monsters.pop_back();
        std::printf("OK: a monster regenerates its DamageRegen, not under Prevent Monster Heal\n");
    }

    // State 0x36 (a whirl, leap or nest) takes no freeze (FUN_0057b230's first test).
    {
        const auto fallen_row = data->monsters.by_id.find("fallen1");
        const auto spike = data->skills.by_name.find("Glacial Spike");
        d2d::rules::Rng make_rng{ 5 };
        GameData::MissileInfo spike_info;
        spike_info.name = "freezewhirl";
        spike_info.etype = 2;
        Missile missile;
        missile.info = &spike_info;
        missile.skill = int(spike->second);
        missile.friendly = true;
        for (const bool whirling : { false, true }) {
            Monster frozen = make_monster(*data, int(fallen_row->second), world.actor->player.x + 6, world.actor->player.y, make_rng, 0);
            frozen.stats.hit_points = frozen.hit_points = 1000000;
            frozen.whirl = whirling;
            world.fight.monsters.push_back(std::move(frozen));
            world.fight.strike(missile, world.fight.monsters.size() - 1, 6000, 100);
            assert((world.fight.monsters.back().stun_until > 6000) == !whirling);
            world.fight.monsters.pop_back();
        }
        std::printf("OK: a whirling monster isn't frozen\n");
    }

    // Bone Armor's pool works on the record's fine physical (FUN_005c6370): the 256ths stay.
    {
        Monster knight;
        knight.armor_pool = 300;
        knight.armored = true;
        assert(soak(knight, 512, 700) == 400 && knight.armor_pool == 0 && !knight.armored);
        knight.armor_pool = 1000;
        knight.armored = true;
        assert(soak(knight, 384, 640) == 256 && knight.armor_pool == 616 && knight.armored);
        std::printf("OK: Bone Armor soaks in 256ths\n");
    }

    // Armageddon / Hurricane run every Param4 frames from the cast (8 / 20: FUN_005c8520 /
    // FUN_005c8380 re-file event 5 at FUN_004efc80's Skills +0x154); a run in town ends them.
    {
        const auto* town = world.level;
        world.level = data->level(d2d::rules::level_ids::kBloodMoor);
        for (const auto* name : { "Armageddon", "Hurricane" }) {
            const auto& skill = *data->skills.get(int(data->skills.by_name.at(name)));
            world.fight.start_state(skill, 1, 0);
            const auto strikes = [&] { return std::uint32_t(world.actor->armageddon_next + world.actor->hurricane_next); };
            std::vector<std::uint32_t> runs;
            for (std::uint32_t frame = 1, last = strikes(); frame <= 60; ++frame) {
                world.fight.buff_tick(frame * kTickMs);
                if (strikes() != last) runs.push_back(frame), last = strikes();
            }
            const std::uint32_t every = std::string_view(name) == "Armageddon" ? 8 : 20;
            assert(skill.par[3] == int(every) && !runs.empty() && runs.size() == 60 / every);
            for (std::size_t k = 0; k < runs.size(); ++k) assert(runs[k] == every * (k + 1));
            world.level = town;
            world.fight.world(false, 61 * kTickMs + every * kTickMs, 0.04f, Crowd{});   // buff_tick doesn't run in town
            assert(!world.fight.state_of(skill.aurastate));
            world.level = data->level(d2d::rules::level_ids::kBloodMoor);
        }
        world.level = town;
        world.fight.missiles.clear();
        std::printf("OK: Armageddon / Hurricane strike every Param4 frames, not in town\n");
    }

    // A Shadow's Attack (range both) is ranged with its owner's bow (FUN_00645460 ->
    // FUN_0064f460: weapon class 1 / 7), melee otherwise.
    {
        const auto look = world.actor->character.appearance;
        Fight::Pet shadow;
        world.fight.shadow_give(shadow, 0, 1);
        d2d::d2s::Item bow{ .code = "sbw", .location = d2d::d2s::item_location::kEquipped, .slot = d2d::d2s::body_location::kRightArm };
        world.actor->character.appearance = data->look_of({ bow });
        world.fight.shadow_give(shadow, 1, 1);                          // Kick (h2h) stays melee
        Fight::Pet archer;
        world.fight.shadow_give(archer, 0, 1);
        world.actor->character.appearance = look;
        assert(shadow.skill_list.size() == 2 && shadow.skill_list[0].kind == 1 && shadow.skill_list[1].kind == 1);
        assert(archer.skill_list.size() == 1 && archer.skill_list[0].kind == 2);
        // ... and shot as its missile from afar (skill_missile: weapon_missile).
        auto worn = world.actor->character.items;
        d2d::rules::Rng shadow_rng{ 4 };
        world.actor->character.items.push_back(std::move(bow));
        archer.monster = make_monster(*data, int(data->monsters.by_id.at("shadowwarrior")), world.actor->player.x, world.actor->player.y, shadow_rng, 0);
        archer.cast = 0;
        const auto fallen_far = make_monster(*data, int(data->monsters.by_id.at("fallen1")), world.actor->player.x + 8, world.actor->player.y, shadow_rng, 0);
        world.fight.monsters.push_back(fallen_far);
        archer.target = int(world.fight.monsters.size() - 1);
        const auto shots = world.fight.pending.size() + world.fight.missiles.size();
        world.fight.shadow_cast(archer, 6000, Crowd{});
        assert(world.fight.pending.size() + world.fight.missiles.size() > shots);
        world.fight.monsters.pop_back();
        world.fight.pending.clear(); world.fight.missiles.clear();
        world.actor->character.items = std::move(worn);
        std::printf("OK: a Shadow's Attack is ranged with a bow\n");
    }

    // A skill command adds 1 to pierce_idx (FUN_00549d00); a Pierce row's missile (Magic
    // Arrow) takes FUN_0059f940's charges on it from skill_pierce + item_pierce, and each
    // struck monster spends one as it flies on (FUN_005ada80), no draw; then CollideKill ends it.
    {
        const auto* before = world.level;
        world.level = data->level(d2d::rules::level_ids::kBloodMoor);
        const auto idx = world.actor->pierce_idx;
        world.apply(cmd::UseSkill{ 0, world.actor->player.x + 3, world.actor->player.y, -1, true }, 7000);
        assert(world.actor->pierce_idx == idx + 1);
        const auto& skill = *data->skills.get(int(data->skills.by_name.at("Magic Arrow")));
        const auto& row = data->missiles.at("magicarrow");
        assert(row.pierce && row.collide_kill);
        d2d::rules::Rng pierce_rng{ 3 };
        world.fight.monsters.push_back(make_monster(*data, int(data->monsters.by_id.at("fallen1")), world.actor->player.x + 3, world.actor->player.y, pierce_rng, 0));
        const std::size_t fallen = world.fight.monsters.size() - 1;
        world.fight.monsters[fallen].hit_points = world.fight.monsters[fallen].stats.hit_points = 1 << 20;   // the hits leave it standing
        const auto psum = world.actor->psum;
        world.actor->psum[d2d::d2s::kSkillPierce] = 40;
        world.actor->psum[d2d::d2s::kItemPierce] = 20;
        world.actor->pierce_idx = 1;                                     // {1, 666} under 60: 3 draws land
        Missile arrow = world.fight.launch(row, skill, 1, world.actor->player.x, world.actor->player.y, 1.f, 0.f, row.range, 7000);
        world.fight.pending.pop_back();
        assert(arrow.pierce == 3 && d2d::rules::pierce_charges(60, 1) == 3);
        for (int left = 2; left >= 0; --left) assert(!world.fight.skill_missile_hits(arrow, fallen, 7000) && arrow.pierce == left);
        assert(world.fight.skill_missile_hits(arrow, fallen, 7000));
        world.actor->psum[d2d::d2s::kSkillPierce] = world.actor->psum[d2d::d2s::kItemPierce] = 0;   // none: the charges stay unspent
        arrow.pierce = 2;
        assert(world.fight.skill_missile_hits(arrow, fallen, 7000) && arrow.pierce == 2);
        world.actor->psum = psum;
        world.fight.monsters.pop_back();
        world.fight.pending.clear(); world.fight.missiles.clear();
        world.level = before;
        std::printf("OK: a Pierce row's charges are FUN_0059f940's, one spent a struck monster\n");
    }

    // With the stones done, talking to anyone in Act 1 takes a bkd still carried (FUN_00592580).
    {
        world.cain.stones_done = true;
        world.actor->character.items.push_back({ .code = "bkd", .location = d2d::d2s::item_location::kStored });
        const auto carried = [&] { return std::ranges::count(world.actor->character.items, std::string_view("bkd"), &d2d::d2s::Item::code); };
        (void)world.quest_talk(d2d::rules::monster_ids::kAkara, false);   // a check, not a talk
        assert(carried() == 1);
        (void)world.quest_talk(d2d::rules::monster_ids::kAkara);
        assert(carried() == 0);
        world.cain.stones_done = false;
        std::printf("OK: a talk takes the bkd left once the stones are done\n");
    }

    // Dying with an item in hand: it goes into the corpse's grid first (FUN_0057f700's -1 pass).
    {
        auto kept = world.actor->character.items;
        world.actor->held = d2d::d2s::Item{ .code = "hp1", .location = d2d::d2s::item_location::kCursor, .slot = 4 };
        world.make_corpse();
        const auto& corpse = world.actor->corpses.back();
        assert(!world.actor->held && !corpse.items.empty() && corpse.items[0].code == "hp1");
        assert(corpse.items[0].location == d2d::d2s::item_location::kStored && corpse.items[0].panel == d2d::d2s::item_panel::kInventory && corpse.items[0].slot == 0);
        world.actor->corpses.pop_back();
        world.actor->character.items = std::move(kept);
        std::printf("OK: the item in hand lies in the corpse's grid\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
