// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Game rules that need no game data: skill tooltips, the camp's rooms, a
// missile's frame, the view codec, MonProp's stats on a monster, and the like.

#include <ai.hpp>
#include <character.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <item_text.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <replication.hpp>
#include <rules.hpp>
#include <shake.hpp>
#include <skill_text.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace d2d::game;

int main() try {
    {
        // Skill tooltips: game.exe's string is built bottom line first, colour codes carrying on.
        const auto lines = d2d::game::skill_text::to_lines("Mana Cost: 2\n\nFire: 3-6\n\xff" "c2Fire Bolt\n\n\xff" "c1Not learned");
        assert(lines.size() == 6 && lines[0].text == "Not learned" && lines[0].rgb == d2d::game::kTxtRed);
        assert(lines[2].text == "Fire Bolt" && lines[2].rgb == d2d::game::kTxtGreen && lines[1].text.empty());
        assert(lines[3].text == "Fire: 3-6" && lines[4].text.empty() && lines[5].text == "Mana Cost: 2" && lines[5].rgb == d2d::game::kTxtWhite);
        std::printf("OK: skill tooltip lines: bottom first, colour codes\n");
    }
    {
        // The camp keeps no Level::rooms: level_room is its 8x8 grid, row by
        // row, level tiles (the DS1's last column and row are its edge).
        Level camp;
        camp.ds1 = d2d::ds1::Map(21, 13, 1, 1);
        camp.world_x = 100, camp.world_y = 200;
        const auto edge = level_room(camp, 2), below = level_room(camp, 3);
        assert(edge && edge->x == 16 && edge->y == 0 && edge->width == 4 && edge->height == 8);
        assert(below && below->x == 0 && below->y == 8 && below->height == 4);
        assert(!level_room(camp, 6) && room_of(camp, 17.5f, 9.5f) == 5);
        std::printf("OK: the camp's rooms: an 8x8 grid\n");
    }
    {
        // A missile's frame (tools/emu/missile_dirs.py): game.exe's table at 0x6e45a0
        // for 16 and 8 directions, every 64-step facing, and Fire Bolts it fired
        // ("dx dy: d8 d16 d32", subtiles from the shooter to the target).
        constexpr std::array<int, 64> kGame16{ 4, 4, 8, 8, 8, 8, 0, 0, 0, 0, 9, 9, 9, 9, 5, 5, 5, 5, 10, 10, 10, 10, 1, 1, 1, 1, 11, 11, 11, 11, 6, 6,
                                               6, 6, 12, 12, 12, 12, 2, 2, 2, 2, 13, 13, 13, 13, 7, 7, 7, 7, 14, 14, 14, 14, 3, 3, 3, 3, 15, 15, 15, 15, 4, 4 };
        constexpr std::array<int, 64> kGame8{ 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 1, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 6,
                                              6, 6, 6, 6, 2, 2, 2, 2, 2, 2, 2, 2, 7, 7, 7, 7, 7, 7, 7, 7, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4 };
        for (int dir64 = 0; dir64 < 64; ++dir64)
            assert(missile_direction(dir64, 16) == kGame16[std::size_t(dir64)] && missile_direction(dir64, 8) == kGame8[std::size_t(dir64)]);
        struct Fired { int dx, dy, d8, d16, d32; };
        for (const auto& [dx, dy, d8, d16, d32] : { Fired{ -4, 2, 5, 10, 10 }, Fired{ 0, 8, 0, 0, 0 }, Fired{ -12, -2, 1, 1, 22 }, Fired{ 2, -2, 7, 7, 7 },
                                                    Fired{ 19, 26, 4, 4, 16 }, Fired{ -10, 4, 1, 10, 10 }, Fired{ 1, -3, 2, 13, 13 }, Fired{ 2, 7, 0, 8, 17 } }) {
            const int facing = missile_facing(0.1f, 0.1f, float(dx) / 5, float(dy) / 5);   // from subtile (0, 0)'s centre
            assert(missile_direction(facing, 8) == d8 && missile_direction(facing, 16) == d16 && missile_direction(facing, 32) == d32);
        }
        for (int dir64 = 0; dir64 < 64; dir64 += 4)   // a unit's 8-direction composite takes the same table (FUN_005ff760)
            assert(cof_direction(missile_direction(dir64, 16), 8) == missile_direction(dir64, 8));
        std::printf("OK: a missile's frame direction, as game.exe's table and its Fire Bolts\n");
    }
    {
        GameData empty_data;
        empty_data.monsters.types.emplace_back();
        View sent;
        sent.level = &empty_data.town;
        sent.quest_log.back() = 9;
        sent.game_quests.back() = 0x8000;
        sent.events = { ev::Reassigned{}, ev::Lightning{}, ev::QuestQuake{ d2d::rules::QuestQuake::diablo } };
        sent.party_pets.push_back({ Npc{}, UnitState{}, "WL", "hireable", 100 });   // a partner's merc (local co-op)
        sent.panel.next = 3'000'000'000; sent.panel.defense = 345; sent.panel.res = { 75, -100, 12, 40 }; sent.panel.bonus[3] = 7;
        sent.panel.blue = d2d::rules::kPanelDefense; sent.panel.red = d2d::rules::kPanelFire | d2d::rules::kPanelPoison;
        sent.stat_sum = { { 0, 25 }, { 329, 40 } };
        Monster sent_monster;
        sent_monster.id = 1;
        sent_monster.type = 0;
        sent_monster.anim_pct = 25;
        sent_monster.props.ias = 60;
        sent_monster.props.fcr = 40;
        sent.monsters = { sent_monster };
        ViewEncoder encoder;
        const auto bytes = encode_view(empty_data, sent, encoder);
        View received;
        assert(apply_view(empty_data, bytes, received));
        assert(received.quest_log == sent.quest_log);
        assert(received.panel.next == sent.panel.next && received.panel.defense == 345 && received.panel.res == sent.panel.res && received.panel.bonus[3] == 7);
        assert(received.panel.blue == sent.panel.blue && received.panel.red == sent.panel.red && received.stat_sum == sent.stat_sum);
        assert(received.game_quests == sent.game_quests);
        assert(received.events.size() == 3 && std::holds_alternative<ev::Reassigned>(received.events[0]) && std::holds_alternative<ev::Lightning>(received.events[1]));
        assert(std::get<ev::QuestQuake>(received.events[2]).kind == d2d::rules::QuestQuake::diablo);
        assert(received.pets.empty() && received.party_pets.size() == 1 && received.party_pets[0].pettype == "hireable" && received.party_pets[0].mode == "WL");
        auto bad = bytes;
        assert(bad[bad.size() - 4] == 7);   // final event, before the empty sound count
        bad[bad.size() - 3] = 2;
        assert(!apply_view(empty_data, bad, received));
        assert(!apply_view(empty_data, std::span(bytes).first(bytes.size() - 3), received));
        View rates;
        ViewEncoder rate_encoder;
        assert(apply_view(empty_data, encode_view(empty_data, sent, rate_encoder), rates));
        assert(rates.monsters.size() == 1 && rates.monsters[0].anim_pct == 25 && rates.monsters[0].props.ias == 60 && rates.monsters[0].props.fcr == 40);
        assert(anim_rate_pct(rates.monsters[0], "A1") == 65 && anim_rate_pct(rates.monsters[0], "SC") == 55);
        sent.monsters[0].anim_pct = 30;
        assert(apply_view(empty_data, encode_view(empty_data, sent, rate_encoder), rates));
        assert(anim_rate_pct(rates.monsters[0], "A1") == 70);
        sent.monsters[0].aura_rate = 20;   // Fanaticism's: a host's aura reaches the swing as drawn
        assert(apply_view(empty_data, encode_view(empty_data, sent, rate_encoder), rates));
        assert(anim_rate_pct(rates.monsters[0], "A1") == 90 && anim_rate_pct(rates.monsters[0], "SC") == 60);
    }
    {
        // Event 1's deadline is specific to the operate function: most
        // container functions and the shrine queue FrameCnt1 + 1, while the
        // Gidbinn altar and Moldy Tome queue FrameCnt1.
        Npc object;
        object.op_frames = 10;
        object.mode2 = true;
        object.sync = true;            // no per-object variation: FrameDelta1 as it stands
        object.frames[1] = 10;
        object.frame_delta[1] = 120;   // client reaches FrameCnt1 - 1 after 20 ticks
        object.operate_fn = d2d::rules::operate_fn::kChest;
        assert(object_op_ticks(object) == 11);
        object.mode2 = false;
        assert(object_op_ticks(object) == 20);  // callback 0x581490 requires Mode2
        object.mode2 = true;
        object.operate_fn = d2d::rules::operate_fn::kMoldyTome;
        assert(object_op_ticks(object) == 10);
        object.operate_fn = d2d::rules::operate_fn::kGidbinnAltar;
        assert(object_op_ticks(object) == 10);
        object.operate_fn = d2d::rules::operate_fn::kShrine;
        assert(object_op_ticks(object) == 11);
        object.frames[1] = 1;          // no client transition on a one-frame mode
        assert(object_op_ticks(object) == 11);  // server event still transitions it
        object.mode2 = false;
        assert(object_op_ticks(object) == -1);
        object.operate_fn = d2d::rules::operate_fn::kChest;
        object.mode2 = true;
        assert(object_op_ticks(object) == 11);
    }
    {
        // MonProp's stats on a monster: IAS on A1 / A2, FCR on SC (120 x v /
        // (120 + v)); knock and crush on its seed (FUN_005bf960, FUN_005bffc0).
        Monster monster;
        monster.props.ias = 30; monster.props.fcr = 15; monster.anim_pct = 10;
        assert(anim_rate_pct(monster, "A1") == 34 && anim_rate_pct(monster, "A2") == 34);
        assert(anim_rate_pct(monster, "SC") == 23 && anim_rate_pct(monster, "WL") == 10 && anim_rate_pct(monster, "S1") == 10);
        monster.seed = d2d::rules::Rng(77);
        monster.props.knock = 1;
        for (int k = 0; k < 8; ++k) {
            Foe foe;
            auto expect = monster.seed;
            monprop_hit(monster, foe, false);
            assert(foe.knocked_from.has_value() == ((expect.next() & 0x7f) < 0x40) && foe.crushed.empty());
        }
        for (int k = 0; k < 8; ++k) {                          // a small monster always, a large one a quarter of the time
            Foe small, large;
            small.knock_under = 0x80; large.knock_under = 0x20;
            monprop_hit(monster, small, false);
            auto expect = monster.seed;
            monprop_hit(monster, large, false);
            assert(small.knocked_from && large.knocked_from.has_value() == ((expect.next() & 0x7f) < 0x20));
        }
        monster.props.knock = 0; monster.props.crush = 100;    // always
        Foe player, confused;
        confused.of = &monster; confused.crush_divisor = 4;    // monster_foe_props' for a plain monster
        monprop_hit(monster, player, false); monprop_hit(monster, player, true); monprop_hit(monster, confused, false);
        assert((player.crushed == std::vector{ 10, 20 }) && (confused.crushed == std::vector{ 4 }) && !player.knocked_from);
        monster.props.crush = 0;
        auto untouched = monster.seed;
        monprop_hit(monster, player, false);
        assert(monster.seed.low == untouched.low && player.crushed.size() == 2);   // no prop, no draw
        // life / divisor, less the raw damageresist up to 100; none below 1 (FUN_005bffc0).
        assert(crushing_blow(1000, 4, 0) == 250 && crushing_blow(1000, 4, 60) == 100 && crushing_blow(1000, 4, 150) == 0 && crushing_blow(1000, 4, -100) == 250);
        std::printf("OK: MonProp IAS / FCR rates, knock and crush rolls\n");
    }
    {
        // FUN_0064dea0's search (FUN_0064e7b0 / FUN_0064e810: 50 rings, step
        // 1): the spot itself; else per ring its columns x - r, x + r top to
        // bottom (left, right a row), then its rows y - r, y + r; the first
        // nearest wins.
        const auto clear_at = [](std::vector<std::pair<int, int>> spots) {
            return [clear_spots = std::move(spots)](int at_x, int at_y) { return std::ranges::contains(clear_spots, std::pair{ at_x, at_y }); };
        };
        assert(nearest_spot(10, 10, clear_at({ { 10, 10 } })) == std::pair(10, 10));
        assert(nearest_spot(10, 10, clear_at({ { 9, 11 }, { 11, 9 } })) == std::pair(11, 9));             // corners: the top row's first
        assert(nearest_spot(10, 10, clear_at({ { 11, 11 }, { 10, 9 } })) == std::pair(10, 9));           // the side's middle (1 off) beats a corner (2)
        assert(nearest_spot(10, 10, clear_at({ { 12, 10 }, { 9, 9 } })) == std::pair(9, 9));             // ring 1 before ring 2
        assert(nearest_spot(10, 10, clear_at({ { 10, 59 } })) == std::pair(10, 59));                     // ring 49, the last
        assert(!nearest_spot(10, 10, clear_at({ { 10, 60 } })));                                         // ring 50: never
        std::printf("OK: nearest_spot's rings (FUN_0064dea0)\n");
    }
    {
        using Cue = std::pair<std::string, int>;
        GameData sounds;
        sounds.rules.item_info["rin"].drop_sound = "item_ring";
        sounds.rules.item_info["amu"] = { .drop_sound = "item_amulet", .drop_frame = 5 };
        sounds.unique_sounds = { {}, { "item_ring_unique", "", 0 }, { "item_amulet_unique", "", 9 } };
        d2d::d2s::Item ring{ .code = "rin" };
        assert(flippy_sound(sounds, ring) == Cue("item_ring", 12));          // no dropsfxframe: 12
        ring.quality = 7;
        ring.unique_id = 1;
        assert(flippy_sound(sounds, ring) == Cue("item_ring_unique", 12));   // the unique's sound, the base's frame
        d2d::d2s::Item amulet{ .code = "amu", .quality = 7, .unique_id = 2 };
        assert(flippy_sound(sounds, amulet) == Cue("item_amulet_unique", 9));
        amulet.unique_id = 0;
        assert(flippy_sound(sounds, amulet) == Cue("item_amulet", 5));     // an empty row: the base's
        std::printf("OK: the flippy's drop sound (FUN_004c16d0)\n");
    }
    {
        // A set item's lists (FUN_004e6560): add func 1 by which other member is worn, 2 by the count.
        GameData sets;
        sets.rules.sets.resize(3);
        for (int member = 0; member < 3; ++member) { sets.rules.sets[std::size_t(member)].set = 0; sets.rules.sets[std::size_t(member)].member = member; }
        sets.rules.sets[0].add_func = 1;
        auto worn = [](int set_id) {
            d2d::d2s::Item piece{ .code = "cap" };
            piece.quality = 5; piece.set_id = set_id;
            piece.location = d2d::d2s::item_location::kEquipped; piece.slot = d2d::d2s::body_location::kFirst;
            return piece;
        };
        auto ward = worn(0);
        ward.set_lists = 3; ward.set_list_sizes = { 1, 1 };
        ward.set_props = { { .stat = 1, .value = 1 }, { .stat = 2, .value = 1 } };
        const auto lists = set_bonus_props(sets, { ward, worn(2) }, ward);
        assert(lists.size() == 1 && lists[0].stat == 2);                                       // place 2 is list 1 (its own place 0 skipped)
        sets.rules.sets[0].add_func = 2;
        assert(set_bonus_props(sets, { ward, worn(2) }, ward).size() == 1 && set_bonus_props(sets, { ward, worn(2) }, ward)[0].stat == 1);   // 2 worn: list 0
        std::printf("OK: a set item's own lists by add func\n");
    }
    {
        // State 0x36 (a whirl, leap or nest under way) keeps a curse's think
        // off (FUN_005dd480), as it stands Talic (FUN_005eeb70).
        GameData walkers;
        walkers.monsters.types.resize(1);
        walkers.monsters.types[0].can_walk = walkers.monsters.types[0].switch_ai = true;
        Monster walker;
        walker.type = 0; walker.hit_points = 10;
        assert(swaps_ai(walkers, walker));
        for (bool Monster::* busy : { &Monster::whirl, &Monster::leaping, &Monster::nesting }) {
            auto held = walker;
            held.*busy = true;
            assert(held.uninterruptable() && !swaps_ai(walkers, held));
        }
        std::printf("OK: state 0x36 holds a monster's own think\n");
    }
    {
        // The Cursed umod's hook (FUN_005a2530), shared by melee and missile
        // hits: 3 in 4 on the boss's seed, Amplify Damage round the one struck.
        GameData no_skills;                                   // no row 66: radius 1 subtile
        Monster boss;
        boss.boss = d2d::rules::Boss::unique; boss.mods = { d2d::rules::umod::curse };
        boss.stats.level = 24; boss.seed = d2d::rules::Rng(3);
        std::array<Foe, 3> foes{};
        foes[1].x = 0.1f;                                     // half a subtile off
        foes[2].x = 2;
        for (int k = 0; k < 16; ++k) {
            for (auto& foe : foes) foe.cursed = {};
            auto expect = boss.seed;
            const bool rolled = (expect.next() & 3) != 0;
            cursed_hit(no_skills, boss, foes, foes[0]);
            assert(boss.seed.next() == expect.next());          // one draw a hit
            assert((foes[0].cursed.skill == 66) == rolled && (foes[1].cursed.skill == 66) == rolled && foes[2].cursed.skill != 66);
            assert(!rolled || foes[0].cursed.level == 24 / 5 + 1);
        }
        boss.boss = d2d::rules::Boss::champion;
        foes[0].cursed = {};
        for (int k = 0; k < 8; ++k) cursed_hit(no_skills, boss, foes, foes[0]);
        assert(foes[0].cursed.skill != 66);                   // a champion's never
        std::printf("OK: the Cursed umod's hit hook\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
