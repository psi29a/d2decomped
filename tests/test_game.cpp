// SPDX-License-Identifier: GPL-3.0-or-later
// The game runs on its own: components/game needs nothing from the client
// (apps/d2d: SDL, sound, sprites, fonts), so a standalone server links
// just this library. With the game's MPQs (D2_MPQ_DIR, as test_outdoor)
// it loads GameData and plays a new character for a second.
#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <cues.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <drops.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <inventory.hpp>
#include <item_text.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>
#include <montypes.hpp>
#include <object_ids.hpp>
#include <protocol.hpp>
#include <quests.hpp>
#include <replication.hpp>
#include <rules.hpp>
#include <shake.hpp>
#include <sequences.hpp>
#include <skill_text.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace d2d::game;

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
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
        std::printf("OK: a missile's frame direction, as game.exe's table and its Fire Bolts\n");
    }
    {
        GameData empty_data;
        View sent;
        sent.level = &empty_data.town;
        sent.quest_log.back() = 9;
        sent.game_quests.back() = 0x8000;
        sent.events = { ev::Reassigned{}, ev::Lightning{}, ev::QuestQuake{ d2d::rules::QuestQuake::diablo } };
        sent.party_pets.push_back({ Npc{}, UnitState{}, "WL", "hireable", 100 });   // a partner's merc (local co-op)
        ViewEncoder encoder;
        const auto bytes = encode_view(empty_data, sent, encoder);
        View received;
        assert(apply_view(empty_data, bytes, received));
        assert(received.quest_log == sent.quest_log);
        assert(received.game_quests == sent.game_quests);
        assert(received.events.size() == 3 && std::holds_alternative<ev::Reassigned>(received.events[0]) && std::holds_alternative<ev::Lightning>(received.events[1]));
        assert(std::get<ev::QuestQuake>(received.events[2]).kind == d2d::rules::QuestQuake::diablo);
        assert(received.pets.empty() && received.party_pets.size() == 1 && received.party_pets[0].pettype == "hireable" && received.party_pets[0].mode == "WL");
        auto bad = bytes;
        assert(bad[bad.size() - 4] == 7);   // final event, before the empty sound count
        bad[bad.size() - 3] = 2;
        assert(!apply_view(empty_data, bad, received));
        assert(!apply_view(empty_data, std::span(bytes).first(bytes.size() - 3), received));
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
        const auto clear_at = [](std::vector<std::pair<int, int>> clear_spots) {
            return [clear_spots](int at_x, int at_y) { return std::ranges::contains(clear_spots, std::pair{ at_x, at_y }); };
        };
        assert(nearest_spot(10, 10, clear_at({ { 10, 10 } })) == std::pair(10, 10));
        assert(nearest_spot(10, 10, clear_at({ { 9, 11 }, { 11, 9 } })) == std::pair(11, 9));             // corners: the top row's first
        assert(nearest_spot(10, 10, clear_at({ { 11, 11 }, { 10, 9 } })) == std::pair(10, 9));           // the side's middle (1 off) beats a corner (2)
        assert(nearest_spot(10, 10, clear_at({ { 12, 10 }, { 9, 9 } })) == std::pair(9, 9));             // ring 1 before ring 2
        assert(nearest_spot(10, 10, clear_at({ { 10, 59 } })) == std::pair(10, 59));                     // ring 49, the last
        assert(!nearest_spot(10, 10, clear_at({ { 10, 60 } })));                                         // ring 50: never
        std::printf("OK: nearest_spot's rings (FUN_0064dea0)\n");
    }
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s (the checks above ran)\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");

    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    {
        // The camp's room seeds (a merc's teleport rolls on them, fight.cpp
        // room_teleport): one per level_room, each a DRLG room's.
        Spawning::LevelState camp;
        bool kept = false;
        for (int tries = 0; tries < 200 && !(kept = keep_camp_seeds(*data, data->town, camp)); ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(kept && !camp.room_seeds.empty() && level_room(data->town, camp.room_seeds.size() - 1) && !level_room(data->town, camp.room_seeds.size()));
        assert(std::ranges::all_of(camp.room_seeds, [](const d2d::rules::Rng& seed) { return seed.low != 0; }));
        std::printf("OK: the camp keeps %zu room seeds\n", camp.room_seeds.size());
    }
    if (data->patched) {
        // The antidote's and thawing's states (misc.txt pSpell 6, FUN_005beda0):
        // +50 resist, +10 max, for len 750.
        const auto& antidote = data->rules.potions.at("yps");
        const auto& thawing = data->rules.potions.at("wms");
        using StatValue = std::pair<int, int>;
        assert(antidote.antidote && antidote.ticks == 750 && (antidote.state_stats == std::vector<StatValue>{ { 45, 50 }, { 46, 10 } }));
        assert(thawing.thawing && thawing.ticks == 750 && (thawing.state_stats == std::vector<StatValue>{ { 43, 50 }, { 44, 10 } }));
        std::printf("OK: the antidote's and thawing's state stats\n");
    }
    {
        // MonProp knock and crush on a monster foe (FUN_005bf960, FUN_005bffc0):
        // MonStats2 small / large, a hireling's 10, a boss's or unique's 8, +50 % a player past one.
        const auto row = [&](std::string_view id) { return int(data->monsters.by_id.at(std::string(id))); };
        const auto props_of = [&](int type, d2d::rules::Boss boss, int players) {
            Monster monster; monster.type = type; monster.boss = boss; monster.players = players;
            Foe foe; monster_foe_props(*data, monster, foe);
            return std::pair{ foe.knock_under, foe.crush_divisor };
        };
        const int zombie = row("zombie1"), quill = row("quillrat1");
        assert(!data->monsters.types[std::size_t(zombie)].small && !data->monsters.types[std::size_t(zombie)].large);
        assert(props_of(zombie, d2d::rules::Boss::none, 1) == std::pair(0x40, 4) && props_of(zombie, d2d::rules::Boss::champion, 1).second == 4);
        assert(props_of(zombie, d2d::rules::Boss::unique, 1).second == 8 && props_of(zombie, d2d::rules::Boss::superunique, 3).second == 16);
        assert(props_of(zombie, d2d::rules::Boss::none, 2).second == 6 && props_of(row("andariel"), d2d::rules::Boss::none, 1).second == 8);
        assert(props_of(row("roguehire"), d2d::rules::Boss::none, 4).second == 10 && row("roguehire") == 0x10f);
        assert(data->monsters.types[std::size_t(quill)].small == (props_of(quill, d2d::rules::Boss::none, 1).first == 0x80));
        assert(std::ranges::any_of(data->monsters.types, &d2d::rules::MonType::large) && std::ranges::any_of(data->monsters.types, &d2d::rules::MonType::small));
        std::printf("OK: MonProp knock and crush on monster foes\n");
    }
    if (data->patched) {                                          // the 1.14d tables (the CD's may differ)
        // MonProp rows as game.exe compiled them (tools/emu/monprop_rows.py: the
        // emulator's DataTables +0xc30), per MonStats row and difficulty;
        // every other row has none.
        struct Case { int row, difficulty; std::string_view code; int min, max; };
        static constexpr Case kCases[] = {
            { 295, 0, "fade", 1, 1 }, { 295, 1, "fade", 2, 2 }, { 295, 2, "fade", 2, 2 },
            { 242, 1, "cast2", 15, 15 }, { 242, 1, "swing2", 15, 15 }, { 242, 2, "cast2", 30, 30 }, { 242, 2, "swing2", 30, 30 },
            { 704, 1, "cast2", 15, 15 }, { 704, 1, "swing2", 15, 15 }, { 704, 2, "cast2", 30, 30 }, { 704, 2, "swing2", 30, 30 },
            { 186, 1, "crush", 15, 15 }, { 186, 2, "crush", 25, 25 }, { 187, 1, "crush", 15, 15 }, { 187, 2, "crush", 25, 25 },
            { 188, 1, "crush", 15, 15 }, { 188, 2, "crush", 25, 25 }, { 189, 1, "crush", 15, 15 }, { 189, 2, "crush", 25, 25 },
            { 618, 1, "crush", 15, 15 }, { 618, 2, "crush", 25, 25 }, { 619, 1, "crush", 15, 15 }, { 619, 2, "crush", 25, 25 },
            { 345, 1, "extra-fire", 40, 40 }, { 345, 2, "extra-fire", 100, 100 }, { 346, 1, "extra-fire", 40, 40 }, { 346, 2, "extra-fire", 100, 100 },
            { 347, 1, "extra-fire", 40, 40 }, { 347, 2, "extra-fire", 100, 100 }, { 557, 1, "extra-fire", 60, 60 }, { 557, 2, "extra-fire", 120, 120 },
            { 267, 2, "knock", 1, 1 },
            { 605, 1, "thorns", 15, 20 }, { 605, 2, "thorns", 30, 40 }, { 606, 1, "thorns", 15, 20 }, { 606, 2, "thorns", 30, 40 },
            { 607, 1, "thorns", 15, 20 }, { 607, 2, "thorns", 30, 40 },
            { 419, 0, "stupidity", -1, -1 }, { 419, 1, "stupidity", -1, -1 }, { 419, 2, "stupidity", -1, -1 },
        };
        std::size_t slots = 0;
        for (const auto& type : data->monsters.types)
            for (const auto& per_difficulty : type.props) slots += per_difficulty.size();
        assert(slots == std::size(kCases));
        std::array<std::array<std::size_t, 3>, 800> seen{};
        for (const auto& entry : kCases) {
            const auto& slot = data->monsters.types.at(std::size_t(entry.row)).props[std::size_t(entry.difficulty)].at(seen[std::size_t(entry.row)][std::size_t(entry.difficulty)]++);
            assert(slot.code == entry.code && slot.min == entry.min && slot.max == entry.max && slot.chance == 0);
        }
        // What they come to: Mephisto's Hell IAS and FCR, a Hell quill rat's
        // thorns drawn on its seed, Blunderbore's crush.
        d2d::rules::Rng seed(1234);
        const auto mephisto = d2d::rules::monster_props(data->rules, data->monsters.types[242], 2, seed);
        assert(mephisto.ias == 30 && mephisto.fcr == 30 && seed.low == 1234);       // min == max: no draw
        auto drawn = seed;
        const auto quill = d2d::rules::monster_props(data->rules, data->monsters.types[607], 2, seed);
        assert(quill.thorns == drawn.range(30, 40) && quill.thorns >= 30 && quill.thorns <= 40);
        assert(d2d::rules::monster_props(data->rules, data->monsters.types[189], 2, seed).crush == 25);
        assert(d2d::rules::monster_props(data->rules, data->monsters.types[267], 1, seed).knock == 0);
        assert(d2d::rules::monster_props(data->rules, data->monsters.types[267], 2, seed).knock == 1);
        std::printf("OK: MonProp rows match game.exe's (%zu slots)\n", slots);
    }
    // `--life-apply SEED N`: a lightning missile's rolls (256ths) and heals on a
    // monster's life, the cases of tools/emu/life_apply.py (game.exe's own
    // FUN_0057c6c0 / FUN_0057c1e0 / FUN_0057a980), one line each, to diff.
    if (argc > 3 && std::string_view(argv[1]) == "--life-apply") {
        std::uint32_t state = std::uint32_t(std::atoi(argv[2]));
        if (state == 0) state = 1;
        const auto next = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
        d2d::rules::Rng rng{ 3 };
        const auto fallen = data->monsters.by_id.find("fallen1");
        assert(fallen != data->monsters.by_id.end());
        const Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
        const auto run = [&](const std::string& tag, int max_hp, int res, int pierce, const std::vector<std::pair<char, int>>& events) {
            Monster monster = made;
            monster.stats.hit_points = monster.hit_points = max_hp;
            monster.life_frac = 0;
            d2d::rules::Target target;
            target.res[3] = res;
            std::printf("%s %d %d %d:", tag.c_str(), max_hp, res, pierce);
            for (const auto& [kind, value] : events) {
                if (kind == 'h') {
                    const auto blow = d2d::rules::missile_blow({ .etype = 1, .elo = value, .ehi = value, .elen = 25 }, target, { 0, pierce, 0, 0 }, rng);
                    hurt(*data, monster, blow.damage, 0, blow.frac);
                } else heal(monster, value);
                const int life = monster.alive() ? monster.hit_points * 256 - monster.life_frac : 0;
                std::printf(" %c%d=%d", kind, value, life);
                if (life == 0) break;
            }
            std::printf("\n");
        };
        for (const int res : { -100, -99, -50, -1, 0, 1, 33, 50, 67, 99, 100, 101, 150 }) {
            for (const int life : { 1, 2, 3, 4 })
                for (int roll = 1; roll <= 1100; ++roll) run("B", life, res, 0, { { 'h', roll } });
            for (const int roll : { 0x100000 - 1, 0x100000, 0x100000 + 1, 0x100000 + 99, 0x100000 + 100, 0x100000 + 101, 0x200000, 0x7fff00, 0, -1, -256, 0x7fffffff })
                run("M", 0x7fffff >> 8, res, 0, { { 'h', roll } });
        }
        for (int i = 0; i < std::atoi(argv[3]); ++i) {
            const int max_hp = 1 + int(next() % 300);
            const std::uint32_t bucket = next() % 100;
            int res;
            if (bucket < 10) res = 100 + int(next() % 51);
            else if (bucket < 20) res = -int(next() % 101);
            else res = int(next() % 100);
            const int pierce = res >= 100 || next() % 4 ? 0 : int(next() % 61);
            std::vector<std::pair<char, int>> events;
            const std::uint32_t count = 1 + next() % 12;
            for (std::uint32_t step = 0; step < count; ++step) {
                if (next() % 5 == 0) { events.emplace_back('+', int(next() % 40)); continue; }
                const std::uint32_t kind = next() % 8;
                int roll;
                if (kind == 0) roll = 1 + int(next() % 0x400);
                else if (kind == 1) {
                    const int edges[12] = { 255, 256, 257, 511, 512, 513, (max_hp << 8) - 1, max_hp << 8, (max_hp << 8) + 1, (max_hp << 8) - 255, (max_hp << 8) - 256, (max_hp << 8) - 257 };
                    roll = edges[next() % 12];
                } else if (kind == 2) roll = 1 + int(next() % 0x300000);
                else roll = 1 + int(next() % 0x2000);
                events.emplace_back('h', std::max(roll, 1));
            }
            run("R" + std::to_string(i), max_hp, res, pierce, events);
        }
        return 0;
    }
    // `--skill-tips N [difficulty]`: every SkillDesc line at levels 1..N with
    // no unit, as tools/emu/skill_tips.py prints game.exe's (FUN_004edea0).
    if (argc > 2 && std::string_view(argv[1]) == "--skill-tips") {
        data->difficulty = argc > 3 ? std::atoi(argv[3]) : 0;
        d2d::rules::CalcEnv env;
        env.clvl = 0;
        const auto line_env = skill_text::with_missiles(*data, env);
        for (const auto& skill : data->skills.rows) {
            if (skill.id < 0 || skill.desc.empty()) continue;
            for (std::size_t line = 0; line < skill.desc_lines.size(); ++line)
                for (int level = 1; level <= std::atoi(argv[2]); ++level) {
                    skill_text::Builder out{ *data, {}, {} };
                    const bool wrote = skill_text::desc_line(out, data->skills, skill, line_env, level, skill.desc_lines[line], 0);
                    std::ranges::replace(out.text, '\n', '|');
                    if (wrote || !out.text.empty()) std::printf("%d %zu %d: %d %s\n", skill.id, line, level, wrote ? 1 : 0, out.text.c_str());
                }
        }
        return 0;
    }
    // Skill and missile sounds (unit-sounds.md): Fire Bolt's cast, Bash's grunt (the
    // Barbarian's own), its swing held back; the fire bolt's flight and impact.
    if (const auto* fire_bolt = data->skills.get(36); fire_bolt && data->missiles.contains("firebolt")) {
        assert(fire_bolt->st_sound == data->sound_index.at("sorceress_cast_fire"));
        const auto* bash = data->skills.get(126);
        assert(bash && bash->st_sound_class == data->sound_index.at("barbarian_grunt_small_1") && bash->st_sound_delay && bash->hit_class == 112);
        const auto& bolt = data->missiles.at("firebolt");
        // Missiles.txt HitClass is a number (FUN_00661b20: type 4, +0x94): an arrow's bow class
        // is the struck player's impact_arrow, and a later block's block_arrow.
        const int arrow = data->missiles.at("arrow").hit_class;
        assert(arrow == 10 && data->missiles.at("javelin").hit_class == 6);
        if (const auto spit = data->missiles.find("goospit1"); spit != data->missiles.end()) assert(spit->second.hit_class == 0xb0);
        assert(d2d::rules::sound_ids::impact(arrow, false).impact == 347 && d2d::rules::sound_ids::block(arrow) == 401);
        assert(std::ranges::any_of(data->monsters.types, [](const auto& type) { return type.id == "zombie1" && type.hit_class == 12; }));
        const auto& zombie = data->mon_sounds.at("zombie");   // its Neutral every 300 ticks at most, two light steps a walk
        assert(zombie.neutral == data->sound_index.at("zombie_neutral_1") && zombie.neutral_time == 300 && zombie.footstep == data->sound_index.at("light_walk_dirt_1") && zombie.fs_count == 2);
        assert(data->states.at("amplifydamage").on_sound == data->sound_index.at("necromancer_amplifydamage") && data->states.at("wolf").off_sound > 0);
        assert(bolt.travel_sound == data->sound_index.at("sorceress_firebolt_1") && bolt.hit_sound == data->sound_index.at("sorceress_firebolt_impact_1"));
    }
    // Collision shapes (FUN_006484e0): the Rogue merc stamps 0x2000 and
    // opens doors, Akara 0x1000; a player walks through the merc, not back.
    if (data->mon_npc.size() > 0x10f) {                  // MonStats: the patch's
        const auto rogue = data->mon_npc[0x10f].shape, akara = data->mon_npc[std::size_t(d2d::rules::monster_ids::kAkara)].shape;
        assert(rogue.bit() == 0x2000 && rogue.mask == 0x3401 && akara.bit() == 0x1000);
        UnitState player{ .x = 10.1f, .y = 10.1f }, merc{ .x = 10.3f, .y = 10.1f, .shape = rogue };
        const Crowd crowd{ { &player, &merc } };
        assert(!crowd.at(player.x, player.y, &player) && crowd.at(merc.x, merc.y, &merc));
        assert(!crowd.at(10.5f, 10.1f, &merc));          // two subtiles off: clear of the player's subtile
        // SizeX 0 (the chicken): no footprint, its own subtile tested; a pet's 1 → 3.
        const auto chicken_row = std::ranges::find(data->monsters.types, std::string("chicken"), &d2d::rules::MonType::id) - data->monsters.types.begin();
        const auto chicken = data->mon_npc[std::size_t(chicken_row)].shape;
        assert(chicken.pattern == 0 && !chicken.stamps() && akara.owned().bit() == 0x2000);
        UnitState bird{ .x = 10.1f, .y = 10.3f, .shape = chicken };
        const Crowd yard{ { &player, &bird } };
        assert(!yard.at(player.x, player.y, &player) && yard.at(bird.x, 10.1f, &bird) && !yard.at(bird.x, bird.y, &bird));
    }
    // A server's GameData keeps the tiles' walk flags, not their pixels.
    for (const auto& archive : data->town.dt1s)
        for (const auto& tile : archive.tiles()) assert(tile.pixels.empty());

    // Every level's monster region, as game.exe makes them at game start
    // (tools/emu regions.py 0x1234 prints the same lines): seed 0x1234, normal.
    std::string regions;
    d2d::rules::Rng region_seed{ d2d::rules::Rng{ 0x1234 }.next() };
    for (std::size_t id = 1; id < data->level_mon.size(); ++id) {
        const auto region = d2d::rules::monster_region(data->monsters, data->level_mon[id], 0, region_seed);
        if (region.types.empty()) continue;
        regions += std::to_string(id);
        for (std::size_t i = 0; i < region.types.size(); ++i) {
            regions += " " + std::to_string(region.types[i].first) + ":";
            for (std::size_t set = 0; set < region.components[i].size(); ++set) {
                if (set) regions += "/";
                for (const auto layer : region.components[i][set]) { char hex[3]; std::snprintf(hex, sizeof hex, "%02x", layer); regions += hex; }
            }
        }
        regions += "\n";
    }
    std::uint64_t hash = 0xcbf29ce484222325;                      // FNV-1a of game.exe's lines
    for (const char letter : regions) hash = (hash ^ std::uint8_t(letter)) * 0x100000001b3;
    if (patch) {                                                  // the 1.14d tables (the CD's differ)
        if (hash != 0x69b556c850c6632eull) std::printf("%s", regions.c_str());
        assert(hash == 0x69b556c850c6632eull);
    }

    // Act 1's drops as game.exe rolls them (tools/emu drops.py act1 prints
    // the same lines): its classes sorted, the jobs of drops.py jobs().
    std::vector<std::string> act1;
    for (const auto& [name, treasure] : data->rules.treasure)
        if (name.starts_with("Act 1 ") || name.starts_with("Andariel") || name.starts_with("Countess") || name.starts_with("Cow")) act1.push_back(name);
    std::sort(act1.begin(), act1.end());
    std::string drops;
    for (std::uint32_t job = 1; job <= 3000; ++job) {
        const std::string& name = act1[job % act1.size()];
        const int level = int(job * 37 % 100), ilvl = int(1 + job * 13 % 99), players = int(1 + job % 8);
        const int magic_find = std::array{ 0, 0, 0, 50, 110, 250, 600, -100 }[job / 8 % 8];
        d2d::rules::Rng seed{ job * 0x9E3779B1u };
        const auto moved = d2d::rules::tc_upgrade(data->rules, name, level);
        std::vector<d2d::rules::Drop> dropped;
        d2d::rules::roll_drops(data->rules, moved, ilvl, seed, dropped, players, magic_find);
        char text[128];
        std::snprintf(text, sizeof text, "%08x %s@%d>%s i%d p%d m%d:", job * 0x9E3779B1u, name.c_str(), level, moved.c_str(), ilvl, players, magic_find);
        drops += text;
        for (const auto& drop : dropped) drops += " " + drop.code + ":" + std::to_string(drop.quality) + (drop.mul ? "*" + std::to_string(drop.mul) : "");
        std::snprintf(text, sizeof text, " -> %08x\n", seed.low);
        drops += text;
    }
    std::uint64_t drops_hash = 0xcbf29ce484222325;
    for (const char letter : drops) drops_hash = (drops_hash ^ std::uint8_t(letter)) * 0x100000001b3;
    if (patch) {
        if (drops_hash != 0x86f78a5cf5373648ull) std::printf("%s%016llx\n", drops.c_str(), (unsigned long long)drops_hash);
        assert(drops_hash == 0x86f78a5cf5373648ull);
    }

    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    {
        // npc.txt's mults are the vendor's side: Akara sells at "sell mult"
        // (1024) and buys at "buy mult" (512); reduced prices come off a buy.
        d2d::d2s::Item potion;
        potion.code = "mp1";
        assert(d2d::rules::item_price(data->rules, potion, "akara", false, made.header) == 60);
        assert(d2d::rules::item_price(data->rules, potion, "akara", true, made.header) == 30);
        assert(d2d::rules::item_price(data->rules, potion, "akara", false, made.header, 14) == 52);
    }
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    {
        // A fallen shaman's rally goes to its leader's group only. One made
        // outside a group (leader -1) sends nothing, and doesn't reach others
        // with none (FUN_0058f730).
        const auto shaman_row = data->monsters.by_id.find("fallenshaman1"), fallen_row = data->monsters.by_id.find("fallen1");
        assert(shaman_row != data->monsters.by_id.end() && fallen_row != data->monsters.by_id.end());
        auto rallied = [&](int leader) {
            d2d::rules::Rng pack_rng{ 1 };
            Level here{};
            here.id = d2d::rules::level_ids::kBloodMoor;
            std::vector<Monster> pack{ make_monster(*data, int(shaman_row->second), 10.f, 10.f, pack_rng, 0),
                                       make_monster(*data, int(fallen_row->second), 10.f, 14.f, pack_rng, 0) };
            for (auto& monster : pack) monster.leader = leader;
            std::array<Foe, 1> foes{};
            foes[0].x = 16.f; foes[0].y = 10.f;
            const Crowd crowd;
            std::vector<Missile> missiles;
            for (std::uint32_t frame = 1; frame <= 2000 && pack[1].ai_command == 0; ++frame)
                monster_update(*data, here, pack[0], foes, pack_rng, frame * 40, 0.04f, crowd, missiles, pack);
            return pack[1].ai_command == 1;
        };
        assert(rallied(0));
        assert(!rallied(-1));
    }
    {
        // Diablo's cold touch (DiabCold, FUN_005cce80) lands one sure hit: the foe keeps
        // Diablo's hit class for its get-hit's impact sound and a later block's.
        const auto diablo_row = data->monsters.by_id.find("diablo");
        const auto cold = data->skills.by_name.find("DiabCold");
        assert(diablo_row != data->monsters.by_id.end() && cold != data->skills.by_name.end());
        d2d::rules::Rng cold_rng{ 3 };
        Level here{};
        here.id = d2d::rules::level_ids::kBloodMoor;
        std::vector<Monster> pack{ make_monster(*data, int(diablo_row->second), 10.f, 10.f, cold_rng, 0) };
        auto& diablo = pack[0];
        diablo.skill = cold->second;
        set_mode(*data, diablo, "SC", 0);
        std::array<Foe, 1> foes{};
        foes[0].x = 11.f; foes[0].y = 10.f;
        const Crowd crowd;
        std::vector<Missile> missiles;
        for (std::uint32_t frame = 1; frame <= 100 && foes[0].hits == 0; ++frame)
            monster_update(*data, here, diablo, foes, cold_rng, frame * 40, 0.04f, crowd, missiles, pack);
        const int diablo_class = data->monsters.types[diablo_row->second].hit_class;
        assert(diablo_class != 0 && foes[0].hits == 1 && foes[0].hit_class == diablo_class);
    }

    {
        // The Golden Bird binds to the first qualifying boss made in Act 3,
        // including a nearby level while the player is still elsewhere.
        World bird_world(&*data, -1, -1);
        bird_world.enter(character);
        Level act2{}, act3{};
        act2.id = d2d::rules::level_ids::kLutGholein;
        act3.id = d2d::rules::level_ids::kSpiderForest;
        int type = 0;
        while (std::size_t(type) < data->mon_is_npc.size() && (type == 0x197 || data->mon_is_npc[std::size_t(type)])) ++type;
        assert(std::size_t(type) < data->mon_is_npc.size());
        Monster boss;
        boss.type = type;
        boss.boss = d2d::rules::Boss::unique;
        bird_world.hosted_quests = true;                       // the game's own records: nothing binds
        bird_world.fight.add_monster(boss, &act3);
        bird_world.act3_bind_new_bosses();
        assert(bird_world.golden_bird.bound == -1 && bird_world.golden_bird.armed && bird_world.fight.made_bosses.empty());
        bird_world.hosted_quests = false;
        bird_world.fight.add_monster(boss, &act2);             // the earlier boss is in another act
        const int first_act3 = bird_world.fight.next_id;
        bird_world.fight.add_monster(boss, &act3);             // made in a nearby level
        bird_world.fight.add_monster(boss, &act3);             // later Act 3 boss cannot replace it
        bird_world.act3_bind_new_bosses();
        assert(bird_world.golden_bird.bound == first_act3 && bird_world.fight.made_bosses.empty());
        bird_world.fight.add_monster(std::move(boss), &act3);  // unarmed: later bosses are dropped
        bird_world.act3_bind_new_bosses();
        assert(bird_world.golden_bird.bound == first_act3 && !bird_world.golden_bird.armed && bird_world.fight.made_bosses.empty());
    }

    {
        // Every local participant gets the effect even after quest credit;
        // the all-seals trigger starts Diablo's sequence only once.
        World effects(&*data, -1, -1);
        effects.enter(character);
        auto partner = character;
        partner.name = "QuakePartner";
        effects.join(partner);
        for (auto& participant : effects.players) {
            participant->events.clear();
            d2d::rules::quest::set(participant->character.header.quests[0], 6, 0);
        }
        effects.act1_kill({ d2d::rules::monster_ids::kAndariel, 0, 0, 1 }, 1000);
        for (auto& participant : effects.players) {
            assert(participant->events.size() == 1);
            assert(std::get<ev::QuestQuake>(participant->events[0]).kind == d2d::rules::QuestQuake::andariel);
            participant->events.clear();
        }
        Level sanctuary;
        sanctuary.id = d2d::rules::level_ids::kChaosSanctuary;
        effects.level = &sanctuary;
        effects.diablo_release(1000);
        effects.diablo_release(1040);
        for (const auto& participant : effects.players) {
            assert(participant->events.size() == 1);
            assert(std::get<ev::QuestQuake>(participant->events[0]).kind == d2d::rules::QuestQuake::diablo);
        }
    }
    World world(&*data, -1, -1);
    world.enter(character);
    for (std::uint32_t tick = 1; tick <= 25; ++tick) world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
    const auto view = world.view();
    assert(view.level && view.level->id == 1 && !view.dead);
    assert(!view.level->unit_blocked(view.player.x, view.player.y));
    // FUN_00545340's free spot (the seals' flash): its 7 x 7, centred a
    // subtile up-left, is clear.
    {
        const int start_x = int(view.player.x * 5), start_y = int(view.player.y * 5);
        const auto spot = view.level->free_area(start_x, start_y, 3, 0x3f11, 100);
        assert(spot);
        for (int row = -3; row <= 3; ++row)
            for (int column = -3; column <= 3; ++column)
                assert(!view.level->blocked((float(spot->first - 1 + column) + 0.5f) / 5, (float(spot->second - 1 + row) + 0.5f) / 5, 0x0f11));
        assert(std::abs(spot->first - start_x) < 100 && std::abs(spot->second - start_y) < 100);
    }
    std::printf("OK: %s in level %d at (%.1f, %.1f)\n", character.name.c_str(), view.level->id, view.player.x, view.player.y);
    {   // A walk's % (FUN_00623f50): 100, a run's run x 100 / walk (stamina left),
        // effective FRW 150 x v / (150 + v), chill's -50, at least 25.
        auto& walker = *world.actor;
        const auto stamina = walker.character.stats.values[d2d::d2s::kStamina];
        const int run_percent = data->run_velocity[kBarbarian] * 100 / data->walk_velocity[kBarbarian];
        const int frw = std::exchange(walker.player_combat.frw, 0);
        assert(stamina > 0 && run_percent > 100 && !walker.running && world.walk_percent(walker, 26 * kTickMs) == 100);
        walker.running = true;
        assert(world.walk_percent(walker, 26 * kTickMs) == run_percent);
        walker.player_combat.frw = 50;
        assert(world.walk_percent(walker, 26 * kTickMs) == run_percent + 37);
        walker.character.stats.values[d2d::d2s::kStamina] = 0;                    // out of stamina: a walk
        assert(world.walk_percent(walker, 26 * kTickMs) == 137);
        walker.character.stats.values[d2d::d2s::kStamina] = stamina;
        walker.running = false; walker.player_combat.frw = 0; walker.chilled = 27 * kTickMs;
        assert(world.walk_percent(walker, 26 * kTickMs) == 50 && world.walk_percent(walker, 27 * kTickMs) == 100);
        walker.player_combat.frw = -100;                                            // the floor
        assert(world.walk_percent(walker, 26 * kTickMs) == 25);
        walker.player_combat.frw = frw; walker.chilled = 0;
        // A joined game's other player: its class's run / walk, no FRW, chill from its states.
        assert(World::walk_percent(*data, kBarbarian, true, 0, false) == run_percent);
        assert(World::walk_percent(*data, kBarbarian, false, 0, true) == 50 && World::walk_percent(*data, 99, false, 0, false) == 100);
        assert(d2d::rules::moving_anim_rate(true, World::walk_percent(*data, kBarbarian, true, 0, false)) == 0x65 * run_percent / 100);
    }

    // d2d's autoloot (deviations.md #5): gold under the player goes into the purse.
    const auto purse = world.actor->character.stats.get(d2d::d2s::kGold);
    world.loot.put({ .code = "gld", .gold = 7 }, world.actor->player.x, world.actor->player.y, 1, world.fight.spawning.game, 26 * kTickMs);
    std::tie(world.loot.ground.back().x, world.loot.ground.back().y) = std::pair{ world.actor->player.x, world.actor->player.y };
    world.tick({}, 26 * kTickMs, 25 * kTickMs);
    assert(world.actor->character.stats.get(d2d::d2s::kGold) == purse + 7 && world.loot.ground.empty());
    {   // Diablo's death (FUN_004cc410): diablo_death on its DeaDelay, then the schwing 0x6a ticks
        // on and the hit 0xc1 ticks on; another monster's death plays no more than its own row.
        namespace snd = d2d::rules::sound_ids;
        const std::uint32_t died_ms = 26 * kTickMs;
        assert(data->sound_index.at("diablo_death") == snd::kDiabloDeath && data->sound_index.at("diablo_death_schwing") == snd::kDiabloDeathSchwing
               && data->sound_index.at("diablo_death_hit") == snd::kDiabloDeathHit && data->sound_index.at("cain_act1_help") == snd::kCainHelp);
        const auto count = [&](int sound, std::uint32_t when_ms) {
            return std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == sound && cue.when_ms == when_ms; });
        };
        const auto type_of = [&](std::string_view id) {
            const auto found = std::ranges::find(data->monsters.types, id, [](const auto& type) { return std::string_view(type.id); });
            assert(found != data->monsters.types.end());
            return int(found - data->monsters.types.begin());
        };
        const auto dies = [&](std::string_view id) {
            Monster dying;
            dying.type = type_of(id);
            dying.sounded = true;
            dying.mode = "DT";
            world.cues.due.clear();
            world.fight.monster_sounds(dying, died_ms);
        };
        dies("diablo");
        const auto& diablo = data->mon_sounds.at("diablo");
        assert(diablo.death == snd::kDiabloDeath);
        assert(count(snd::kDiabloDeath, died_ms + std::uint32_t(diablo.death_del) * kTickMs) == 1);
        assert(count(snd::kDiabloDeathSchwing, died_ms + snd::kDiabloSchwingTicks * kTickMs) == 1);
        assert(count(snd::kDiabloDeathHit, died_ms + snd::kDiabloHitTicks * kTickMs) == 1);
        dies("zombie1");
        assert(std::ranges::none_of(world.cues.due, [](const Cues::Cue& cue) { return cue.sound == snd::kDiabloDeathSchwing || cue.sound == snd::kDiabloDeathHit; }));
        world.cues.due.clear();
        std::printf("OK: Diablo's death plays its schwing and hit after it\n");
    }
    {   // A missile's hit class (Foe::hit_class) is the struck player's, a get-hit keeps it, and a
        // later block plays its block sound (FUN_004cb860): an arrow's block_arrow. A kick plays
        // weapon_kick (FUN_004cc5b0), an Assassin's 4 ticks on.
        namespace snd = d2d::rules::sound_ids;
        const std::uint32_t hit_ms = 26 * kTickMs;
        auto& hero = *world.actor;
        const auto life = hero.character.stats.values[d2d::d2s::kLife];
        const auto items = hero.character.items;
        const auto count = [&](int sound, std::uint32_t when_ms) {
            return std::ranges::count_if(world.cues.due, [&](const Cues::Cue& cue) { return cue.sound == sound && cue.when_ms == when_ms; });
        };
        const int arrow = data->missiles.contains("arrow") ? data->missiles.at("arrow").hit_class : 10;
        world.cues.due.clear();
        auto side = std::array{ world.fight.player_foe(hit_ms), world.fight.merc_foe(hit_ms) };
        side[0].hits = 1;
        side[0].hit_class = arrow;
        side[0].damage = side[0].physical = std::max(int(hero.character.stats.fixed(d2d::d2s::kMaxLife) / 4), 1);
        hero.pmode = -1;
        world.fight.hurt_side(side, hit_ms);
        assert(hero.pmode == kModeGH && hero.shown_class == arrow && hero.struck_class == 0);
        assert(count(snd::impact(arrow, false).impact, hit_ms) == 1);
        hero.pmode = -1;
        world.cues.due.clear();
        auto blocked = std::array{ world.fight.player_foe(hit_ms), world.fight.merc_foe(hit_ms) };
        blocked[0].blocked = true;
        world.fight.hurt_side(blocked, hit_ms);
        assert(hero.pmode == kModeBL && count(snd::kBlockArrow, hit_ms) == 1);
        // The landed blow's class, not a melee swing that missed the same frame; a small hit
        // that knocks back plays it on the knockback's get-hit.
        const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), [](const auto& type) { return std::string_view(type.id); });
        assert(zombie != data->monsters.types.end() && zombie->hit_class != arrow);
        Monster swinger;
        swinger.type = int(zombie - data->monsters.types.begin());
        world.fight.add_monster(swinger);
        hero.pmode = -1;
        world.cues.due.clear();
        auto knocked = std::array{ world.fight.player_foe(hit_ms), world.fight.merc_foe(hit_ms) };
        knocked[0].hits = 1;
        knocked[0].hit_class = arrow;
        knocked[0].melee_by.push_back(world.fight.monsters.back().id);
        knocked[0].damage = knocked[0].physical = 1;
        for (const auto& [step_x, step_y] : { std::pair{ 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f } })
            if (!world.level->unit_blocked(hero.player.x + step_x, hero.player.y + step_y, hero.player.shape)) {
                knocked[0].knocked_from = std::pair{ hero.player.x - step_x, hero.player.y - step_y };
                break;
            }
        assert(knocked[0].knocked_from);
        world.fight.hurt_side(knocked, hit_ms);
        assert(hero.pmode == kModeGH && hero.shown_class == arrow && count(snd::impact(arrow, false).impact, hit_ms) == 1);
        world.fight.monsters.pop_back();
        hero.knock.reset();
        // A hit too small for a get-hit moves the class too (FUN_0057cee0 sets unit flag 0x8000,
        // FUN_00597cf0 sends +0xb0 in S->C 0x0c): the next block plays a weapon's, not the arrow's.
        const auto weapon = std::ranges::find_if(data->monsters.types, [](const auto& type) { return snd::block(type.hit_class) == snd::kBlockWeapon; });
        assert(weapon != data->monsters.types.end());
        hero.pmode = -1;
        world.cues.due.clear();
        auto grazed = std::array{ world.fight.player_foe(hit_ms), world.fight.merc_foe(hit_ms) };
        grazed[0].hits = 1;
        grazed[0].hit_class = weapon->hit_class;
        grazed[0].damage = grazed[0].physical = 1;
        world.fight.hurt_side(grazed, hit_ms);
        assert(hero.pmode != kModeGH && hero.shown_class == weapon->hit_class);
        hero.pmode = -1;
        auto blocked_again = std::array{ world.fight.player_foe(hit_ms), world.fight.merc_foe(hit_ms) };
        blocked_again[0].blocked = true;
        world.fight.hurt_side(blocked_again, hit_ms);
        assert(hero.pmode == kModeBL && count(snd::kBlockWeapon, hit_ms) == 1 && count(snd::kBlockArrow, hit_ms) == 0);
        hero.pmode = -1;
        world.cues.due.clear();
        world.fight.set_pmode(kModeKK, hit_ms);
        assert(count(snd::kKick, hit_ms) == 1);
        const auto was = hero.character.character_class;
        hero.character.character_class = d2d::d2s::kAssassin;
        world.cues.due.clear();
        world.fight.set_pmode(kModeKK, hit_ms);
        assert(count(snd::kKick, hit_ms) == 0 && count(snd::kKick, hit_ms + snd::kKickDelayMs) == 1);
        hero.character.character_class = was;
        hero.pmode = -1; hero.player.mode_ms = hit_ms;
        hero.character.stats.values[d2d::d2s::kLife] = life;
        hero.character.items = items;
        hero.struck_class = hero.shown_class = 0;
        world.cues.due.clear();
        std::printf("OK: an arrow's get-hit, then block_arrow on a block; a graze's class blocked; weapon_kick, an Assassin's late\n");
    }
    {   // A vendor's stock and Kashya's hire list last the game, not one opening.
        const auto& npcs = world.level->npcs;
        const auto index_of = [&](int hc_idx) { return int(std::ranges::find(npcs, hc_idx, &Npc::hc_idx) - npcs.begin()); };
        const int charsi = index_of(d2d::rules::monster_ids::kCharsi), kashya = index_of(d2d::rules::monster_ids::kKashya);
        auto codes = [&] {
            std::vector<std::string> out;
            for (const auto& tab : world.actor->store.tabs) for (const auto& item : tab) out.push_back(item.code + std::to_string(item.id));
            return out;
        };
        world.apply(d2d::game::cmd::OpenTrade{ charsi }, 27 * kTickMs);
        const auto first = codes();
        world.apply(d2d::game::cmd::CloseTrade{}, 27 * kTickMs);
        world.apply(d2d::game::cmd::OpenTrade{ charsi }, 27 * kTickMs);
        assert(!first.empty() && codes() == first);
        world.apply(d2d::game::cmd::CloseTrade{}, 27 * kTickMs);
        world.apply(d2d::game::cmd::OpenHire{ kashya }, 27 * kTickMs);
        const auto offers = world.hire_offers(0);
        world.apply(d2d::game::cmd::OpenHire{ kashya }, 27 * kTickMs);
        assert(!offers.empty() && world.hire_offers(0).size() == offers.size() && world.hire_offers(0).front().seed == offers.front().seed);
        std::printf("OK: Charsi's stock and Kashya's offers outlast their windows\n");
    }
    {   // gold find (FUN_005589a0): the coins times (100 + stat 79) / 100
        auto seed = world.fight.spawning.game;
        d2d::rules::Rng unit{ seed.next() };
        const int coins = d2d::rules::gold_amount(10, 0, unit);
        world.loot.put({ .code = "gld" }, world.actor->player.x, world.actor->player.y, 10, world.fight.spawning.game, 27 * kTickMs, false, 50);
        assert(!world.loot.ground.empty() && world.loot.ground.back().gold == coins * 150 / 100);
        world.loot.ground.pop_back();
    }

    // Gear: a set piece's bonus list 0 is on with two of its set worn;
    // charms in the inventory count, ones in the stash don't.
    {
        const auto& sets = data->rules.sets;
        std::size_t first = 0, second = 1;
        for (; first < sets.size(); ++first) {
            for (second = first + 1; second < sets.size() && sets[second].set != sets[first].set; ++second) {}
            if (second < sets.size()) break;
        }
        assert(second < sets.size());
        auto piece = [](std::size_t set_id, int slot) {
            d2d::d2s::Item worn;
            worn.quality = 5; worn.set_id = int(set_id); worn.location = 1; worn.slot = slot;
            return worn;
        };
        std::vector<d2d::d2s::Item> gear{ piece(first, 1), piece(second, 3), {} };
        gear[0].set_props = { { 0, 0, 15 } }; gear[0].set_lists = 1; gear[0].set_list_sizes = { 1 };
        gear[2].code = "cm1"; gear[2].panel = 1; gear[2].props = { { 80, 0, 20 } };
        auto total = [&](int stat) {
            int sum = 0;
            for (const auto& prop : gear_props(*data, gear)) if (prop.stat == stat) sum += prop.value;
            return sum;
        };
        assert(total(0) == 15 && total(80) == 20);
        assert(wearer(*data, 0, gear, character.stats).str == int(character.stats.get(d2d::d2s::kStr)) + 15);
        gear[1].slot = 0; gear[2].panel = 5;                      // one piece worn, the charm stashed
        assert(total(0) == 0 && total(80) == 0);
    }
    {
        // Charged skills (stat 204: param skill << 6 | level, value max << 8 |
        // left): an entry per worn item and skill with charges left; a
        // socketed item's under its holder's id (FUN_0065c820).
        namespace loc = d2d::d2s::item_location;
        namespace body = d2d::d2s::body_location;
        const int fire_bolt = data->skills.by_name.at("Fire Bolt");
        const auto charges = [&](int level, int left, int most) { return d2d::d2s::ItemProp{ .stat = d2d::d2s::kChargedSkill, .param = fire_bolt << 6 | level, .value = most << 8 | left }; };
        d2d::d2s::Item wand;
        wand.code = "wnd"; wand.location = loc::kEquipped; wand.slot = body::kRightArm; wand.id = 9001;
        wand.props = { charges(4, 2, 2) };
        assert((charged_skills({ wand }) == std::vector<ChargedSkill>{ { 9001, fire_bolt, 4, 2, 2 } }));
        d2d::d2s::Item stored = wand; stored.location = loc::kStored;
        d2d::d2s::Item empty = wand; empty.props = { charges(4, 0, 2) };
        d2d::d2s::Item jewel; jewel.props = { charges(2, 1, 5) };
        d2d::d2s::Item holder = wand; holder.props.clear(); holder.socketed_items = { jewel };
        assert(charged_skills({ stored }).empty() && charged_skills({ empty }).empty());
        assert((charged_skills({ holder }) == std::vector<ChargedSkill>{ { 9001, fire_bolt, 2, 1, 5 } }));
        // A cast from the entry (UseSkill::item) is at its own level, +skills
        // or not, and costs a charge, no mana (FUN_0056bfe0 -> FUN_0056bec0);
        // at 0 the entry's gone and the item turned away.
        auto& caster = *world.actor;
        const auto saved_items = caster.character.items;
        const auto mana = caster.character.stats.values[d2d::d2s::kMana];
        d2d::d2s::Item ring;
        ring.code = "rin"; ring.location = loc::kEquipped; ring.slot = body::kRightRing; ring.id = 9002;
        ring.props = { { .stat = d2d::d2s::kAllSkills, .value = 3 } };
        caster.character.items.push_back(wand);
        caster.character.items.push_back(ring);
        const std::uint32_t cast_ms = 26 * kTickMs;
        const float aim_x = caster.player.x + 2, aim_y = caster.player.y;
        {   // Taken off between the command and the cast: checked where it's paid (FUN_00647840), no cast, nothing spent.
            caster.pmode = -1;
            world.apply(cmd::UseSkill{ fire_bolt, aim_x, aim_y, -1, false, 9001 }, cast_ms);
            auto& worn = caster.character.items[saved_items.size()];
            worn.location = loc::kStored;
            assert(world.fight.cast_level(fire_bolt) == 0 && !world.fight.cast_missile(fire_bolt, aim_x, aim_y, cast_ms));
            assert(worn.props[0].value == (2 << 8 | 2) && caster.character.stats.values[d2d::d2s::kMana] == mana);
            worn.location = loc::kEquipped;
        }
        for (int cast = 0; cast < 2; ++cast) {
            caster.pmode = -1;
            world.apply(cmd::UseSkill{ fire_bolt, aim_x, aim_y, -1, false, 9001 }, cast_ms);   // in town: no cast, the entry taken
            assert(caster.item_charge == (ChargedSkill{ 9001, fire_bolt, 4, 2 - cast, 2 }) && world.fight.skill_level(fire_bolt) == 4);
            assert(world.fight.cast_missile(fire_bolt, aim_x, aim_y, cast_ms));
            assert(caster.character.stats.values[d2d::d2s::kMana] == mana);
            // The cast's over: the entry's level goes with it (the panel and auras read the character's).
            assert(caster.pmode >= 0);
            world.fight.player_modes(false, caster.pmode_until, 0.f);
            assert(caster.pmode == -1 && caster.item_charge.item == -1 && world.fight.skill_level(fire_bolt) == 0);
        }
        assert(caster.character.items[saved_items.size()].props[0].value == (2 << 8 | 0) && charged_skills(caster.character.items).empty());
        caster.pmode = -1;
        caster.attack_mon = 0;                                    // a held attack from the entry: stops with it
        caster.attack_charge = ChargedSkill{ 9001, fire_bolt, 4, 1, 2 };
        world.apply(cmd::UseSkill{ fire_bolt, aim_x, aim_y, -1, false, 9001 }, cast_ms);
        assert(caster.item_charge.item == -1 && world.fight.skill_level(fire_bolt) == 0);
        assert(caster.attack_mon == -1 && caster.attack_charge.item == -1);
        caster.character.items = saved_items;
    }

    // Throw (docs/research/re/throw.md): every class keeps it at level 1
    // (FUN_00647ee0 -> FUN_00647110); the weapon is a separate gate
    // (FUN_00647640: a throwable in hand, or stat 125). Without it the skill
    // is refused (status 2): no swing, the class's impossible_1 (0x711ddc
    // reason 0x13, the class table's +8: amazon 0xb89), once per 75 frames.
    if (const auto throw_id = data->skills.by_name.find("Throw"); throw_id != data->skills.by_name.end()) {
        namespace loc = d2d::d2s::item_location;
        namespace body = d2d::d2s::body_location;
        auto& thrower = *world.actor;
        const auto saved_items = thrower.character.items;
        const int skill = throw_id->second;
        std::erase_if(thrower.character.items, [](const d2d::d2s::Item& item) { return item.location == loc::kEquipped && (item.slot == body::kRightArm || item.slot == body::kLeftArm); });
        assert(skill_base_level(*data, thrower.character, skill) == 1 && skill_refused(*data, thrower.character, skill));
        d2d::d2s::Item axe;
        axe.code = "hax"; axe.location = loc::kEquipped; axe.slot = body::kRightArm; axe.id = 9101;
        thrower.character.items.push_back(axe);
        assert(skill_refused(*data, thrower.character, skill));                        // a weapon, not a throwable
        thrower.character.items.back().props = { { .stat = 125, .value = 1 } };
        assert(!skill_refused(*data, thrower.character, skill));                       // stat 125 lets it throw
        d2d::d2s::Item javelins;
        javelins.code = "jav"; javelins.location = loc::kEquipped; javelins.slot = body::kLeftArm; javelins.id = 9102; javelins.quantity = 5;
        thrower.character.items.back() = javelins;
        assert(skill_base_level(*data, thrower.character, skill) == 1 && !skill_refused(*data, thrower.character, skill));
        thrower.character.items.back().flags |= 0x100u;                                // a spent magic stack, broken in hand (FUN_0062a4e0)
        assert(skill_refused(*data, thrower.character, skill));
        d2d::d2s::Item shield;
        shield.code = "buc"; shield.location = loc::kEquipped; shield.slot = body::kLeftArm; shield.id = 9103;
        shield.props = { { .stat = 125, .value = 1 } };
        thrower.character.items.back() = shield;
        assert(skill_refused(*data, thrower.character, skill));                        // stat 125 on a non-weapon (FUN_0063c9b0 wants weap)
        thrower.character.items.pop_back();                                            // the last one's gone: a held Throw stops
        const std::uint32_t throw_ms = 30 * kTickMs;
        thrower.pmode = -1;
        thrower.attack_mon = 0;
        thrower.attack_skill = skill;
        world.cues.due.clear();
        assert(Fight::spot_skill(*data->skills.get(skill)));                           // a held Throw goes engage -> cast_missile
        assert(!world.fight.cast_missile(skill, thrower.player.x + 2, thrower.player.y, throw_ms) && thrower.attack_mon == -1 && thrower.pmode == -1);
        static constexpr std::array<const char*, 7> kClass{ "amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin" };
        const auto voice = data->sound_index.find(std::format("{}_impossible_1", kClass[std::size_t(std::clamp(thrower.character.character_class, 0, 6))]));
        assert(voice != data->sound_index.end() && world.cues.due.size() == 1 && world.cues.due[0].sound == voice->second);
        thrower.attack_mon = 0;
        assert(!world.fight.cast_missile(skill, thrower.player.x + 2, thrower.player.y, throw_ms + 40) && world.cues.due.size() == 1);   // within 75 frames: not again
        world.display_swing(skill, thrower.player.x + 2, thrower.player.y, throw_ms + 80);   // the host's swing, for show: shown
        assert(thrower.pmode >= 0 && std::ranges::count(world.cues.due, voice->second, &Cues::Cue::sound) == 1);
        thrower.pmode = -1;
        thrower.character.items.push_back(javelins);                                   // javelins back: it throws
        assert(world.fight.cast_missile(skill, thrower.player.x + 2, thrower.player.y, throw_ms + 120) && thrower.pmode >= 0);
        thrower.pmode = -1;
        if (data->patched) assert(data->sound_index.at("amazon_impossible_1") == 0xb89);
        thrower.attack_skill = 0;
        thrower.character.items = saved_items;
    }

    // A healer's / well's cure (FUN_00578d30 / FUN_00585720): poison and the
    // curses go, chill stays; a thawing potion's takes the chill.
    auto& fight = world.fight;
    auto& hero = *world.actor;
    // Jab (cltdofunc 16, FUN_004f4590): the client's do runs on each event
    // frame and plays dosound a on the event-3 ones of the sequence.
    {
        const auto* jab = data->skills.get(10);
        assert(jab && jab->clt_do_func == 16 && jab->do_sound_a == data->sound_index.at("weapon_1ht_1") && jab->do_sound_b == data->sound_index.at("weapon_2ht_1"));
        const int barbarian = hero.character.character_class;
        const auto look = hero.character.appearance;
        hero.character.character_class = d2d::d2s::kAmazon;      // her starting javelin: 1ht, which has Jab's sequence
        hero.character.appearance.reset();
        auto levels = std::move(fight.skill_level);
        const int attack = hero.attack_skill;
        const auto mana = hero.character.stats.values[d2d::d2s::kMana];
        fight.skill_level = [](int) { return 1; };
        hero.attack_skill = 10;
        hero.character.stats.values[d2d::d2s::kMana] = 100 << 8;
        world.cues.due.clear();
        assert(fight.start_swing(2000 * kTickMs) && hero.swing_skill == 10);
        assert(hero.seq.size() == d2d::rules::sequence(1, "1ht").size());
        // SQ (0x12) plays no swing (FUN_004cc5b0): only Jab's start, its stsoundclass (amazon_jab).
        assert(world.cues.due.size() == 1 && world.cues.due[0].sound == jab->st_sound_class);
        world.cues.due.clear();
        const auto sound_frames = std::ranges::count(hero.seq, 3, &d2d::rules::SeqFrame::event);
        assert(sound_frames == 3);
        fight.strike(2000 * kTickMs + std::uint32_t(hero.seq.size()) * hero.seq_frame_ms);
        assert(std::ranges::count(world.cues.due, jab->do_sound_a, &Cues::Cue::sound) == sound_frames);
        fight.strike(2000 * kTickMs + std::uint32_t(hero.seq.size()) * hero.seq_frame_ms);   // once each
        assert(std::ranges::count(world.cues.due, jab->do_sound_a, &Cues::Cue::sound) == sound_frames);
        hero.character.character_class = barbarian;
        hero.character.appearance = look;
        fight.skill_level = std::move(levels);
        hero.attack_skill = attack;
        hero.character.stats.values[d2d::d2s::kMana] = mana;
        hero.swing_skill = 0;
        hero.seq = {};
        hero.smove = {};
        hero.pmode = -1;
        world.cues.due.clear();
        std::printf("OK: Jab's dosound a on its %td event-3 frames\n", sound_frames);
    }
    // A monster's Jab: ai.cpp counts its MonSeq's event-3 rows (only Duriel's
    // has them), monster_sounds plays dosound b once each, none once dead.
    {
        const auto* jab = data->skills.get(10);
        const int row = int(data->mon_bin[std::size_t(d2d::rules::monster_ids::kDuriel)]);
        const auto& duriel_type = data->monsters.types[std::size_t(row)];
        const auto named = std::ranges::find_if(duriel_type.sk_mode, [&](const std::string& mode) { return data->mon_seqs.contains(mode); });
        assert(named != duriel_type.sk_mode.end());
        const auto sound_rows = std::ranges::count(data->mon_seqs.at(*named), 3, &GameData::MonSeqFrame::event);
        assert(sound_rows > 0);
        d2d::rules::Rng spawn_rng{ 7 };
        auto duriel = make_monster(*data, row, hero.player.x + 2, hero.player.y, spawn_rng, 0);
        const std::uint32_t start = 3000 * kTickMs;
        set_mode(*data, duriel, "S1", start);                   // Jab's mode (FUN_005db2d0: mode 8)
        duriel.skill = 10;
        std::vector<Foe> foes(1);
        foes[0].alive = false;
        std::vector<Missile> shots;
        const Crowd nobody{};
        monster_update(*data, *world.level, duriel, foes, spawn_rng, start + 60000, 0.04f, nobody, shots);
        assert(duriel.seq_sounds == sound_rows && duriel.seq_sound == jab->do_sound_b);
        duriel.sounded = true;
        duriel.last_mode = duriel.mode;
        world.cues.due.clear();
        fight.monster_sounds(duriel, start + 60000);
        assert(std::ranges::count(world.cues.due, jab->do_sound_b, &Cues::Cue::sound) == sound_rows && duriel.seq_sounds == 0);
        fight.monster_sounds(duriel, start + 60040);            // nothing replays
        assert(std::ranges::count(world.cues.due, jab->do_sound_b, &Cues::Cue::sound) == sound_rows);
        duriel.seq_sounds = 1;                                   // a row on the frame it dies: dropped
        duriel.hit_points = 0;
        fight.monster_sounds(duriel, start + 60080);
        assert(std::ranges::count(world.cues.due, jab->do_sound_b, &Cues::Cue::sound) == sound_rows && duriel.seq_sounds == 0);
        world.cues.due.clear();
        std::printf("OK: Duriel's Jab plays dosound b on its %td event-3 rows\n", sound_rows);
    }
    const auto poisoned = [&] { return std::ranges::any_of(hero.regen, &Fight::Regen::poison); };
    hero.regen.push_back({ -1.0, 0, 1000 * kTickMs, true });
    // A monster's curse on the player is a self state's aurastats: Weaken's
    // damagepercent -33 (par5) comes off the damage %, Amplify Damage's -100 off DR.
    fight.update_fighters(26 * kTickMs);
    const auto uncursed = hero.player_combat;
    hero.curses[0] = { 72, 1, 1000 * kTickMs };
    fight.update_fighters(26 * kTickMs);
    assert(hero.player_combat.phys_pct == uncursed.phys_pct - 33);
    hero.curses[0] = { 66, 1, 1000 * kTickMs };
    fight.update_fighters(26 * kTickMs);
    assert(hero.player_combat.dr_pct == uncursed.dr_pct - 100 && hero.player_combat.phys_pct == uncursed.phys_pct);
    // Max mana % (item_maxmana_percent 77, op 11): a share of the own maximum
    // plus the items' flat +mana, not of energy's (op 8).
    {
        auto items = std::move(hero.character.items);
        d2d::d2s::Item charm;
        charm.code = "cm1"; charm.panel = 1; charm.props = { { d2d::d2s::kMaxMana, 0, 100 }, { d2d::d2s::kEne, 0, 10 }, { d2d::d2s::kMaxManaPercent, 0, 50 } };
        hero.character.items.clear();
        hero.character.items.push_back(std::move(charm));
        fight.update_fighters(26 * kTickMs);
        const std::int64_t own = fight.own_stats().values[d2d::d2s::kMaxMana], flat = own + 100 * 256;
        const auto& gains = data->class_gains[std::size_t(hero.character.header.cls % 7)];
        const std::int64_t energy = 10LL * gains.mana_per_energy / 4 * 256;
        assert(hero.character.stats.values[d2d::d2s::kMaxMana] == flat + flat * 50 / 100 + energy);
        // Battle Orders' % (a state's 77) adds to the items': one % of the
        // same raw maximum, mana up with it.
        const auto mana = hero.character.stats.values[d2d::d2s::kMana];
        hero.boost = { 0, { { d2d::d2s::kMaxManaPercent, 20 } }, 1000 * kTickMs };
        fight.update_fighters(26 * kTickMs);
        assert(hero.character.stats.values[d2d::d2s::kMaxMana] == flat + flat * 70 / 100 + energy);
        assert(hero.character.stats.values[d2d::d2s::kMana] == std::min(mana + (flat * 70 / 100 - flat * 50 / 100), flat + flat * 70 / 100 + energy));
        hero.boost = {};
        hero.character.items = std::move(items);
        fight.update_fighters(26 * kTickMs);
        assert(hero.character.stats.values[d2d::d2s::kMaxMana] == own);
    }
    hero.chilled = 1000 * kTickMs;
    assert(fight.cure(26 * kTickMs, false) && !poisoned() && hero.curses[0].until == 0 && fight.chill_rate(26 * kTickMs) == -50);
    fight.update_fighters(26 * kTickMs);
    assert(!fight.cure(26 * kTickMs, false));
    hero.regen.push_back({ -1.0, 0, 1000 * kTickMs, true });
    fight.potion("yps", 26 * kTickMs);
    fight.potion("wms", 26 * kTickMs);
    assert(!poisoned() && fight.chill_rate(26 * kTickMs) == 0);

    // Tristram Cain (FUN_00593290 -> FUN_005e7880): the Gibbet opened, he
    // comes out, walks off, opens his portal, walks back in: camp Cain due.
    // (The 1.14d tables: the CD's leave Tristram's presets unplaced.)
    if (!patch) return 0;
    const auto* tristram = data->level(d2d::rules::CainQuest::kTristram);
    d2d::game::populate_level(*data, world.fight.spawning, *tristram);   // its rooms up: the Gibbet made
    const auto gibbet = std::ranges::find(tristram->npcs, 10, &Npc::operate_fn);
    const auto cain_npc = std::ranges::find(tristram->npcs, d2d::rules::monster_ids::kCain, &Npc::hc_idx);
    assert(gibbet != tristram->npcs.end() && cain_npc != tristram->npcs.end());
    const auto cain_index = std::size_t(cain_npc - tristram->npcs.begin());
    const auto* camp = world.level;
    world.level = tristram;
    std::tie(world.actor->player.x, world.actor->player.y) = tristram->nearest_free(gibbet->x + 2, gibbet->y + 2);
    world.swap_npcs(camp);
    assert(world.npc_states[cain_index].hidden);
    world.operate(int(gibbet - tristram->npcs.begin()), 26 * kTickMs);
    assert(world.cain_walk.npc == int(cain_index) && !world.cain.camp_due);
    float cain_moved = 0;
    for (std::uint32_t tick = 27; tick < 27 + 750 && !world.cain.camp_due; ++tick) {
        world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
        const auto& cain_state = world.npc_states[cain_index];
        if (!cain_state.hidden) cain_moved = std::max(cain_moved, std::hypot(cain_state.x - cain_npc->x, cain_state.y - cain_npc->y));
    }
    assert(world.cain.camp_due && world.cain_walk.npc < 0 && world.npc_states[cain_index].hidden && cain_moved > 0.3f);
    // His portal (object 189, FUN_005943b0) at the subtile he first stood
    // on (FUN_005944b0: the Gibbet's x + 3, y + 3), drawn as a town portal.
    const int spot_x = int(std::floor(cain_npc->x * 5)), spot_y = int(std::floor(cain_npc->y * 5));
    assert(spot_x == int(std::floor(gibbet->x * 5)) + 3 && spot_y == int(std::floor(gibbet->y * 5)) + 3);
    assert(world.cain_portal.level == tristram && world.cain_portal.x == (float(spot_x) + 0.5f) / 5 && world.cain_portal.y == (float(spot_y) + 0.5f) / 5);
    const auto cain_view = world.view();
    assert(std::ranges::count_if(cain_view.portals, [&](const auto& shown) { return shown.which == int(d2d::game::World::kGamePortals) && shown.x == world.cain_portal.x && shown.y == world.cain_portal.y; }) == 1);
    std::printf("OK: Tristram Cain walked %.2f cells and took his portal at subtile (%d, %d)\n", cain_moved, spot_x, spot_y);
    // A spot no room near his holds (FUN_00463740 null): it moves on 3 and
    // no portal opens; he steps in at once, his own spot the target.
    world.cain_portal = {};
    world.npc_states[cain_index].hidden = false;
    world.cain_walk = { .npc = int(cain_index), .stage = 1, .tries = 6, .x = spot_x, .y = spot_y, .spot_x = spot_x + 400, .spot_y = spot_y + 400 };
    for (std::uint32_t tick = 800; tick < 800 + 60 && world.cain_walk.npc >= 0; ++tick) world.tick({}, tick * kTickMs, (tick - 1) * kTickMs);
    assert(world.cain_walk.npc < 0 && world.cain_walk.stage == 3 && world.cain_walk.spot_x == spot_x + 403 && world.cain_walk.spot_y == spot_y + 403 && !world.cain_portal.level);
    std::printf("OK: a spot with no room moves on 3 and opens no portal\n");

    // Hover text (FUN_0048dd90), top line first, for a level 1 Amazon:
    // a plain short sword and cap.
    if (patch) {
        const d2d::rules::Wearer amazon{ 0, 20, 25, 1 };
        auto text = [&](const d2d::d2s::Item& item, const d2d::rules::Wearer* wearer) {
            std::string joined;
            for (const auto& line : item_lines(*data, item, 1, wearer)) joined += line.text + "|";
            return joined;
        };
        d2d::d2s::Item sword{ .code = "ssd" };
        sword.identified = true; sword.quality = 2; sword.durability = sword.max_durability = 24;
        d2d::d2s::Item cap{ .code = "cap" };
        cap.identified = true; cap.quality = 2; cap.defense = 3; cap.durability = cap.max_durability = 12;
        assert(text(sword, &amazon) == "Short Sword|One-Hand Damage: 2 to 7|Durability: 24 of 24|Sword Class - Fast Attack Speed|");
        assert(text(sword, nullptr) == "Short Sword|One-Hand Damage: 2 to 7|Durability: 24 of 24|");   // no player: no speed line
        assert(text(cap, &amazon) == "Cap|Defense: 3|Durability: 12 of 12|");
        // +50% defense: the number blue after "Defense: ".
        cap.props.push_back({ .stat = 16, .value = 50 });
        const auto cap_lines = item_lines(*data, cap, 1, &amazon);
        assert(cap_lines[1].text == "Defense: 4" && cap_lines[1].split == 9 && cap_lines[1].tail == kTxtBlue);
        std::printf("OK: hover text of a short sword and a cap\n");
    }

    // A Blood Moor shrine (FUN_00583c70): stamina (row 14) fills stamina,
    // its message overhead, its sound; a resist (row 8) ends it (one
    // shrine state: stamina filled again, FUN_00583a40) for 3600 frames.
    const auto* moor = data->level(2);
    d2d::game::populate_level(*data, world.fight.spawning, *moor);
    const auto shrine = std::ranges::find(moor->npcs, 2, &Npc::operate_fn);
    assert(shrine != moor->npcs.end());
    const auto shrine_index = int(shrine - moor->npcs.begin());
    world.level = moor;
    world.swap_npcs(tristram);
    auto& stats = world.actor->character.stats.values;
    stats[d2d::d2s::kStamina] = 0;
    world.cues.due.clear();
    world.operate(shrine_index, 900 * kTickMs, 14);
    assert(stats[d2d::d2s::kStamina] == stats[d2d::d2s::kMaxStamina] && world.shrine_code(world.actor->boost.shrine) == 14);
    assert(std::ranges::count(world.cues.due, data->sound_index.at("shrine_recharge"), &Cues::Cue::sound) == 1);
    world.tick({}, 901 * kTickMs, 900 * kTickMs);
    assert(world.npc_states[std::size_t(shrine_index)].says == 3683 + shrine->shrine && world.view().boost_code == 14);
    // OP from its use, then ON once its counter reaches FrameCnt1 - 1 (Start1 0 at FrameDelta1 a tick).
    assert(world.npc_states[std::size_t(shrine_index)].mode == "OP" && world.npc_states[std::size_t(shrine_index)].mode_ms == 900 * kTickMs);
    assert(shrine->loop_from[1] == 0 && shrine->frame_delta[1] > 0);
    const std::uint32_t on_ms = (900 + std::uint32_t(((shrine->frames[1] - 1) * 256 + shrine->frame_delta[1] - 1) / shrine->frame_delta[1])) * kTickMs;
    world.tick({}, on_ms - kTickMs, 901 * kTickMs);
    assert(world.npc_states[std::size_t(shrine_index)].mode == "OP");
    world.tick({}, on_ms, on_ms - kTickMs);
    assert(world.npc_states[std::size_t(shrine_index)].mode == "ON" && world.npc_states[std::size_t(shrine_index)].mode_ms == on_ms);
    const int fire = world.actor->player_combat.res[0];
    stats[d2d::d2s::kStamina] = 0;
    world.operate(shrine_index, 902 * kTickMs, 8);
    world.tick({}, 903 * kTickMs, 902 * kTickMs);
    assert(stats[d2d::d2s::kStamina] == stats[d2d::d2s::kMaxStamina] && world.view().boost_code == 8);
    assert(world.actor->player_combat.res[0] == std::min(fire + 75, int(world.actor->character.panel.res_cap[0])) && world.actor->boost.until == (902 + 3600) * kTickMs);
    world.tick({}, (902 + 3600) * kTickMs, (901 + 3600) * kTickMs);
    assert(world.actor->boost.shrine == 0 && world.actor->player_combat.res[0] == fire);
    std::printf("OK: shrines: stamina filled, one state at a time, resist fire for 3600 frames\n");
    {   // A converted monster is no target: game.exe's cursor drops a unit the player can't
        // interact with (HitTestScreen: CanInteractWith is !IsAllyOf), so an attack names it only
        // through devctl or the net; World turns that away too and the hero stays put.
        auto& attacker = *world.actor;
        using MonType = decltype(data->monsters.types)::value_type;
        const auto zombie = std::ranges::find(data->monsters.types, std::string_view("zombie1"), &MonType::id);
        assert(zombie != data->monsters.types.end() && !zombie->interact);
        // Conversion's own state on it: each tick's Fight::monster_states makes it align 2 from that.
        const int conversion = data->skills.by_name.at("Conversion");
        const Monster::SkillState converted{ conversion, 1, 0xffffffffu };
        Monster turned;
        turned.type = int(zombie - data->monsters.types.begin());   // a real one: a tick reads its MonStats row
        turned.id = 0x7ffff0;
        turned.hit_points = 100;
        turned.cry = converted;
        turned.align = 2;
        turned.unit.x = attacker.player.x + 2;
        turned.unit.y = attacker.player.y;
        world.fight.monsters.push_back(turned);
        const auto index = int(world.fight.monsters.size() - 1);
        attacker.attack_mon = -1;
        const std::vector<Order> attack{ { attacker.id, cmd::UseSkill{ 0, turned.unit.x, turned.unit.y, turned.id, true } } };
        world.tick(attack, (4502 + 1) * kTickMs, 4502 * kTickMs);
        assert(attacker.attack_mon == -1 && world.fight.monsters[std::size_t(index)].align == 2);
        {   // the client's View carries its side: no hover name, no pad pick there either
            View shown = world.view();
            shown.monsters = { world.fight.monsters[std::size_t(index)] };
            ViewEncoder encoder;
            View received;
            assert(apply_view(*data, encode_view(*data, shown, encoder), received));
            assert(received.monsters.size() == 1 && received.monsters[0].align == 2 && received.monsters[0].ally_not_interact(*data));
        }
        world.fight.monsters[std::size_t(index)].cry = {};    // the same monster, hostile: attacked
        world.fight.monsters[std::size_t(index)].align = 0;
        world.tick(attack, (4502 + 2) * kTickMs, (4502 + 1) * kTickMs);
        assert(attacker.attack_mon == index && attacker.pmode < 0);   // still walking up to it
        // Converted while the button is held: the walk-up drops it, no swing.
        world.fight.monsters[std::size_t(index)].cry = converted;   // as Conversion's hit sets both
        world.fight.monsters[std::size_t(index)].align = 2;
        world.tick({}, (4502 + 3) * kTickMs, (4502 + 2) * kTickMs);
        assert(attacker.attack_mon == -1 && attacker.pmode < 0);
        world.fight.monsters.pop_back();
        // A town NPC is align 2 as well (MonStats Align 1) but interact: it stays clickable.
        const auto akara = std::ranges::find(data->monsters.types, std::string_view("akara"), &MonType::id);
        assert(akara != data->monsters.types.end() && akara->interact);
        turned.type = int(akara - data->monsters.types.begin());
        assert(turned.ally_not_interact(*data) == false);
        turned.type = -1;                                      // no MonStats row: no interact either
        assert(turned.ally_not_interact(*data));
        // A joined game's host converts with its state (town.cpp net_overlay): Conversion's aura target.
        const auto* conversion_row = data->skills.get(conversion);
        assert(conversion_row && conversion_row->auratarget == "conversion" && std::ranges::contains(data->state_names, std::string("conversion")));
        std::printf("OK: a converted monster isn't attacked\n");
    }

    // A row with no Skill (Meteor's fire): its EDmgSymPerCalc, skill('Inferno'.blvl) * 3, adds
    // that percent to EMin + brackets before HitShift (FUN_0064b100 / FUN_0064b1d0).
    {
        const auto& meteor_fire = data->missiles.at("meteorfire");
        const int inferno = data->skills.by_name.at("Inferno");
        const d2d::rules::CalcEnv none{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, 1, nullptr };
        const d2d::rules::CalcEnv twenty{ [&](int id) { return id == inferno ? 20 : 0; }, [](int) { return 0; }, [](int) { return 0; }, 1, nullptr };
        const int meteor = data->skills.by_name.at("Meteor");   // the skill that cast it
        const auto plain = world.fight.row_damage(meteor_fire, 10, none, meteor), synergy = world.fight.row_damage(meteor_fire, 10, twenty, meteor);
        const int base_lo = plain.elo >> meteor_fire.hitshift, base_hi = plain.ehi >> meteor_fire.hitshift;
        assert(plain.elo > 0 && synergy.elo == (base_lo + base_lo * 60 / 100) << meteor_fire.hitshift && synergy.ehi == (base_hi + base_hi * 60 / 100) << meteor_fire.hitshift);
        std::printf("OK: Meteor's fire takes Inferno's synergy in the fight\n");

        // Meteor's centre (14) and Immolation Arrow (9) leave fire burning its own row (no fixed
        // damage) for Param3 + (lvl - 1) x Param4 / SHitCalc1 frames (FUN_0059fa30's flag 0x8000).
        const auto burst_fires = [&](const char* row, const char* skill_name, int lvl) {
            const auto& skill_row = *data->skills.get(data->skills.by_name.at(skill_name));
            world.fight.pending.clear();
            auto shot = world.fight.launch(data->missiles.at(row), skill_row, lvl, world.actor->player.x + 3, world.actor->player.y, 0, 0, 1, 5000 * kTickMs);
            world.fight.pending.clear();
            world.fight.burst(shot, 5000 * kTickMs);
            std::vector<std::uint32_t> lives;
            for (const auto& flame : world.fight.pending)
                if (flame.info->name == data->missiles.at(row).hit_sub) { assert(flame.fixed == -1); lives.push_back((flame.dies - flame.born) / kTickMs); }
            world.fight.pending.clear();
            return lives;
        };
        const auto& meteor_row = *data->skills.get(meteor);
        const auto meteor_fires = burst_fires("meteorcenter", "Meteor", 3);
        assert(!meteor_fires.empty() && std::ranges::all_of(meteor_fires, [&](auto life) { return life == std::uint32_t(meteor_row.par[2] + 2 * meteor_row.par[3]); }));
        const auto immolation_fires = burst_fires("immolationarrow", "Immolation Arrow", 1);
        assert(!immolation_fires.empty() && std::ranges::all_of(immolation_fires, [](auto life) { return life == 100u; }));
        std::printf("OK: Meteor's and Immolation Arrow's fire last their own frames, burning their row\n");
    }

    // A chest's trap fires at its event 4, 35 frames after it opened
    // (FUN_00582510 -> FUN_005817a0); in act 1 a lightning trap (1) is a
    // firebolt (2).
    const auto missiles_named = [&](std::string_view name) {
        return std::ranges::count_if(world.fight.missiles, [&](const auto& missile) { return missile.info && missile.info->name == name; });
    };
    world.fight.missiles.clear();
    world.arm_trap(1, world.actor->player.x + 1, world.actor->player.y, 1, 4600 * kTickMs);
    world.tick({}, (4600 + 34) * kTickMs, (4600 + 33) * kTickMs);
    assert(world.fight.missiles.empty() && world.traps.size() == 1);
    world.tick({}, (4600 + 35) * kTickMs, (4600 + 34) * kTickMs);
    assert(world.traps.empty() && missiles_named("trapfirebolt") == 1 && missiles_named("chainlightning") == 0);
    std::printf("OK: a chest's trap springs 35 frames on, lightning a firebolt in act 1\n");

    // The Moldy Tome (FUN_00594e70): its message 127 for the player
    // (S->C 0x27); heard back (cmd::QuestMessage), the Forgotten Tower starts.
    const auto* tower_level = data->level(d2d::rules::level_ids::kStonyField);
    d2d::game::populate_level(*data, world.fight.spawning, *tower_level);
    const auto tome = std::ranges::find(tower_level->npcs, d2d::rules::operate_fn::kMoldyTome, &Npc::operate_fn);
    assert(tome != tower_level->npcs.end());
    const int tome_index = int(tome - tower_level->npcs.begin());
    world.level = tower_level;
    world.swap_npcs(moor);
    world.actor->events.clear();
    world.operate(tome_index, 4700 * kTickMs);
    assert(world.tower.state == 0 && world.actor->events.size() == 1);
    const auto* told = std::get_if<d2d::game::ev::Speech>(&world.actor->events[0]);
    assert(told && told->npc == tome_index && told->string == d2d::rules::TowerQuest::kTome);
    world.apply(d2d::game::cmd::QuestMessage{ tome_index, d2d::rules::TowerQuest::kTome }, 4701 * kTickMs);
    assert(world.tower.state == 2);
    std::printf("OK: the Moldy Tome's message 127, heard, starts the Forgotten Tower\n");

    // Charsi's imbue (FUN_00579d60): every rare it can make survives its
    // save form, which the View carries too (replication.hpp wire::items).
    if (data->item_tables) {
        int checked = 0;
        for (std::uint32_t seed = 0; seed < 500; ++seed) {
            d2d::rules::Rng imbue_rng{ seed };
            d2d::d2s::Item axe;
            axe.code = "hax"; axe.quality = 2; axe.identified = true; axe.ilvl = 8;
            const auto imbued = d2d::rules::imbue_item(data->rules, axe, 8, imbue_rng);
            d2d::d2s::detail::BitWriter writer;
            d2d::d2s::detail::write_item(writer, imbued, *data->item_tables);
            d2d::d2s::detail::Bits bits{ std::span<const std::byte>(writer.out), 0 };
            try {
                const auto back = d2d::d2s::detail::item(bits, *data->item_tables);
                assert(back.code == "hax" && back.quality == 6);
                std::map<std::pair<int, int>, std::int64_t> sent, read;   // what's saved, summed by (stat, param)
                for (const auto& prop : imbued.props)
                    if (prop.stat >= 0 && std::size_t(prop.stat) < data->item_tables->stats.size() && data->item_tables->stats[std::size_t(prop.stat)].save_bits)
                        sent[{ prop.stat, prop.param }] += prop.value;
                for (const auto& prop : back.props) read[{ prop.stat, prop.param }] += prop.value;
                assert(read == sent);
            } catch (const std::exception& error) {
                std::printf("imbue seed %u: %s; props:", seed, error.what());
                for (const auto& prop : imbued.props) std::printf(" %d:%d:%d", prop.stat, prop.param, prop.value);
                std::printf("\n");
                assert(false);
            }
            ++checked;
        }
        std::printf("OK: %d imbued rares round-trip their save form\n", checked);
    }
    // Sets.txt (FUN_00660120): Hsarus' Defense (row 1, 3 pieces) — two worn
    // give its partial thorns 5, all three its full bonuses (lightning 25).
    if (data->rules.set_bonuses.size() > 1 && data->rules.set_bonuses[1].pieces == 3) {
        std::vector<d2d::d2s::Item> worn;
        for (std::size_t row = 0; row < data->rules.sets.size() && worn.size() < 3; ++row)
            if (data->rules.sets[row].set == 1) {
                d2d::d2s::Item piece;
                piece.code = data->rules.sets[row].code; piece.quality = 5; piece.set_id = int(row);
                piece.location = d2d::d2s::item_location::kEquipped; piece.slot = std::uint8_t(d2d::d2s::body_location::kHead + int(worn.size()));
                worn.push_back(std::move(piece));
            }
        auto total = [&](int stat) {
            std::int64_t sum = 0;
            for (const auto& prop : set_props(*data, worn)) if (prop.stat == stat) sum += prop.value;
            return sum;
        };
        assert(total(41) == 25 && total(78) == 5);
        worn.pop_back();
        assert(total(41) == 0 && total(78) == 5);
        worn.pop_back();
        assert(set_props(*data, worn).empty());
        std::printf("OK: Sets.txt partial and full bonuses\n");
    }
    // Local co-op (docs/design/local-coop.md): a second player joins beside
    // the first and moves on their own orders; each one's View shows the
    // other; a level change takes the party; the Blood Moor's monsters go at
    // whoever is near; the second leaves.
    {
        World coop(&*data, -1, -1);
        coop.enter(character);
        auto second_character = character;
        second_character.name = "Second";
        auto& second = coop.join(second_character);
        assert(coop.players.size() == 2 && second.id == 2 && coop.actor == &coop.lead() && coop.fight.heroes.size() == 2);
        assert(std::hypot(second.player.x - coop.lead().player.x, second.player.y - coop.lead().player.y) < 3);
        {
            ViewEncoder encoder;
            View received;
            assert(apply_view(*data, encode_view(*data, coop.view(), encoder), received));
            assert(received.others.size() == 1 && received.others[0].name == "Second" && received.others[0].id == 2);
        }
        const float lead_x = coop.lead().player.x, lead_y = coop.lead().player.y;
        const auto [goal_x, goal_y] = coop.level->nearest_free(second.player.x + 4, second.player.y);
        const float start_gap = std::hypot(goal_x - second.player.x, goal_y - second.player.y);
        const std::vector<Order> walk{ { second.id, cmd::Move{ goal_x, goal_y, true } } };
        for (std::uint32_t tick = 1; tick <= 50; ++tick) coop.tick(tick == 1 ? std::span<const Order>(walk) : std::span<const Order>{}, tick * kTickMs, (tick - 1) * kTickMs);
        assert(coop.lead().player.x == lead_x && coop.lead().player.y == lead_y);
        assert(std::hypot(goal_x - second.player.x, goal_y - second.player.y) < start_gap / 2);

        const auto* blood_moor = data->level(d2d::rules::level_ids::kBloodMoor);
        coop.arrive(blood_moor, float(blood_moor->ds1.width()) / 2, float(blood_moor->ds1.height()) / 2, "a test");
        assert(coop.level == blood_moor && !blood_moor->unit_blocked(second.player.x, second.player.y));
        assert(std::hypot(second.player.x - coop.lead().player.x, second.player.y - coop.lead().player.y) < 3);
        // The second by a monster, the first 20 cells off: it's the second who's hit.
        std::uint32_t now_ms = 60 * kTickMs;
        for (int warm = 0; warm < 5; ++warm, now_ms += kTickMs) coop.tick({}, now_ms + kTickMs, now_ms);
        const auto prey = std::ranges::find_if(coop.fight.monsters, [&](const Monster& monster) {
            return monster.alive() && !blood_moor->unit_blocked(monster.unit.x + 1, monster.unit.y); });
        assert(prey != coop.fight.monsters.end());
        const int prey_id = prey->id, prey_type = prey->type;
        std::tie(second.player.x, second.player.y) = blood_moor->nearest_free(prey->unit.x + 1, prey->unit.y);
        second.target_x = second.player.x; second.target_y = second.player.y;
        std::tie(coop.lead().player.x, coop.lead().player.y) = blood_moor->nearest_free(prey->unit.x + 20, prey->unit.y);
        coop.lead().target_x = coop.lead().player.x; coop.lead().target_y = coop.lead().player.y;
        const auto full = second.character.stats.get(d2d::d2s::kMaxLife);   // `prey` may move as monsters are born: by id from here
        for (int frame = 0; frame < 250 && second.character.stats.get(d2d::d2s::kLife) >= full; ++frame, now_ms += kTickMs) coop.tick({}, now_ms + kTickMs, now_ms);
        assert(second.character.stats.get(d2d::d2s::kLife) < full);
        {   // As it first shows (FUN_004cc380) a live monster plays its Init; a
            // corpse (a joined game's monster already dead) plays none.
            const auto with_init = std::ranges::find_if(data->monsters.types, [&](const auto& type) {
                const auto found = data->mon_sounds.find(d2d::rules::boss_sound(type, d2d::rules::Boss::none, {}));
                return found != data->mon_sounds.end() && found->second.init > 0; });
            assert(with_init != data->monsters.types.end());
            const int init = data->mon_sounds.at(d2d::rules::boss_sound(*with_init, d2d::rules::Boss::none, {})).init;
            const auto shows = [&](int hit_points) {
                Monster shown;
                shown.type = int(with_init - data->monsters.types.begin());
                shown.hit_points = hit_points;
                coop.cues.due.clear();
                coop.fight.monster_sounds(shown, now_ms);
                return std::ranges::count(coop.cues.due, init, &Cues::Cue::sound);
            };
            assert(shows(1) == 1 && shows(0) == 0);
            coop.cues.due.clear();
        }

        // Player-count rules (net-host.md section 2). Life and experience +50 %
        // a player past the first (FUN_00573cb0, tables 0x6e1590 / 0x6e15b4).
        {
            d2d::rules::Rng alone{ 99 }, pair{ 99 };
            const auto one = d2d::rules::monster_stats(data->monsters, prey_type, 0, alone);
            const auto two = d2d::rules::monster_stats(data->monsters, prey_type, 0, pair, 0, 2);
            assert(two.hit_points == one.hit_points + one.hit_points / 2 && two.exp == one.exp + one.exp / 2);
            assert(d2d::rules::player_life_pct(8) == 350 && d2d::rules::player_life_pct(10) == 400 && d2d::rules::player_exp_pct(10) == 360);
        }
        // The party's share of a kill (FUN_0057e6c0): 100 + 100 x 89 / 256 =
        // 134 over levels 10 + 20, 4.4666665 a level.
        assert((d2d::rules::party_shares(100, std::vector{ 10, 20 }) == std::vector<std::int64_t>{ 44, 89 }));
        assert((d2d::rules::party_shares(100, std::vector{ 7 }) == std::vector<std::int64_t>{ 100 }));
        // Both near a kill: both gain.
        std::tie(coop.lead().player.x, coop.lead().player.y) = blood_moor->nearest_free(second.player.x + 1, second.player.y);
        second.character.stats.values[d2d::d2s::kLife] = second.character.stats.values[d2d::d2s::kMaxLife];
        const auto lead_exp = coop.lead().character.stats.get(d2d::d2s::kExp), second_exp = second.character.stats.get(d2d::d2s::kExp);
        assert(coop.fight.monster_index(prey_id) >= 0);
        const auto prey_index = std::size_t(coop.fight.monster_index(prey_id));
        // A MonProp thorns monster's flat value back on a pet that hits it
        // (FUN_005bf670), through the pet's physical resist; none on a miss.
        {
            d2d::rules::Rng pet_rng{ 5 };
            Fight::Pet pet{ .monster = make_monster(*data, prey_type, second.player.x, second.player.y, pet_rng, 0) };
            const int life = pet.monster.hit_points, resist = pet.monster.target(*data).res[0];
            coop.fight.monsters[prey_index].props.thorns = 40;
            coop.fight.pet_strike(pet, prey_index, { .hit = false }, now_ms);
            assert(pet.monster.hit_points == life);
            coop.fight.pet_strike(pet, prey_index, { .hit = true }, now_ms);
            assert(pet.monster.hit_points == std::max(life - d2d::rules::resisted(40, resist), 0));   // dead is 0 (0057c86c)
            // Life is kept in 256ths (0057c852): two half points are one, and
            // a life under one point after a hit is dead (0057c865).
            {
                Monster fine = pet.monster;
                fine.hit_points = 3;
                fine.life_frac = 0;
                assert(!hurt(*data, fine, 0, now_ms, 128) && fine.hit_points == 3 && fine.life_frac == 128);
                assert(!hurt(*data, fine, 0, now_ms, 128) && fine.hit_points == 2 && fine.life_frac == 0);
                assert(!hurt(*data, fine, 0, now_ms, 1) && fine.hit_points == 2 && fine.life_frac == 1);
                assert(hurt(*data, fine, 1, now_ms, 0) && !fine.alive() && fine.hit_points == 0 && fine.life_frac == 0);   // dead is 0 (0057c86c)
                // A heal (FUN_0057a980) adds whole points under the cap and keeps the
                // 256ths taken off; one the cap cuts is full life.
                fine = pet.monster;
                fine.stats.hit_points = 5;
                fine.hit_points = 5;
                hurt(*data, fine, 1, now_ms, 128);
                heal(fine, 1);
                assert(fine.hit_points == 5 && fine.life_frac == 128);
                heal(fine, 1);
                assert(fine.hit_points == 5 && fine.life_frac == 0);
            }
            // None from a converted monster: no blow lands on it (FUN_00554200).
            const int align = std::exchange(coop.fight.monsters[prey_index].align, 2);
            coop.fight.pet_strike(pet, prey_index, { .hit = true }, now_ms);
            assert(pet.monster.hit_points == std::max(life - d2d::rules::resisted(40, resist), 0));
            assert(!coop.fight.land(prey_index, { .hit = true }, true, now_ms));
            coop.fight.monsters[prey_index].align = align;
            // And on the player's melee blow (the strike's and Dragon
            // Flight's kick), through damage reduction %, then flat: the
            // whole hero's, a weapon's DR too, even for a kick (pf_kick has
            // the weapon's stats off; FUN_0057c1e0 reads the unit's).
            const auto combat = coop.fight.actor->player_combat, kick = coop.fight.actor->pf_kick;
            coop.fight.actor->player_combat.dr_pct = 20; coop.fight.actor->player_combat.dr_flat = 3;
            coop.fight.actor->pf_kick.dr_pct = 0; coop.fight.actor->pf_kick.dr_flat = 0;
            const auto hurt = coop.fight.actor->self_hurt;
            coop.fight.player_thorns(prey_index, { .hit = false });
            assert(coop.fight.actor->self_hurt == hurt);
            coop.fight.player_thorns(prey_index, { .hit = true });
            assert(coop.fight.actor->self_hurt == hurt + (std::int64_t(40 * 80 / 100 - 3) << 8));
            coop.fight.actor->self_hurt = hurt;
            // Dragon Flight through Fight::spot: beside the prey, the kick, and on
            // a landed kick the thorns through the whole hero's DR. The kick rolls
            // to hit, so a few casts: each one's thorns are none or exactly these.
            const auto* flight = data->skills.get(data->skills.by_name.at("Dragon Flight"));
            assert(flight && flight->srvdofunc == d2d::rules::ServerDoFunction::kDragonFlight);
            const auto at_x = coop.fight.actor->player.x, at_y = coop.fight.actor->player.y;
            int landed = 0;
            for (int cast = 0; cast < 40; ++cast) {
                auto& prey_now = coop.fight.monsters[prey_index];
                prey_now.hit_points = prey_now.stats.hit_points * 100;
                std::tie(coop.fight.actor->player.x, coop.fight.actor->player.y) = blood_moor->nearest_free(prey_now.unit.x + 2, prey_now.unit.y);
                coop.fight.actor->attack_mon = int(prey_index);
                const auto before = coop.fight.actor->self_hurt;
                coop.fight.spot(*flight, now_ms, { .level = 1, .target = int(prey_index) });
                const auto taken = coop.fight.actor->self_hurt - before;
                assert(taken == 0 || taken == (std::int64_t(40 * 80 / 100 - 3) << 8));
                landed += taken != 0;
            }
            assert(landed > 0);
            coop.fight.actor->self_hurt = hurt;
            coop.fight.actor->player.x = at_x; coop.fight.actor->player.y = at_y;
            coop.fight.actor->player_combat = combat; coop.fight.actor->pf_kick = kick;
            coop.fight.monsters[prey_index].props.thorns = 0;
        }
        coop.fight.monsters[prey_index].hit_points = 0;
        coop.fight.killed(prey_index, now_ms);
        assert(coop.lead().character.stats.get(d2d::d2s::kExp) > lead_exp && second.character.stats.get(d2d::d2s::kExp) > second_exp);
        // Gold picked up is shared (FUN_00540900): 100 between two, 50 each.
        const auto lead_gold = coop.lead().character.stats.get(d2d::d2s::kGold), second_gold = second.character.stats.get(d2d::d2s::kGold);
        coop.loot.put({ .code = "gld", .gold = 100 }, coop.lead().player.x, coop.lead().player.y, 1, coop.fight.spawning.game, now_ms);
        coop.loot.ground.back().gold = 100;
        coop.loot.take(coop.loot.ground.size() - 1);
        assert(coop.lead().character.stats.get(d2d::d2s::kGold) == lead_gold + 50 && second.character.stats.get(d2d::d2s::kGold) == second_gold + 50);
        // A quest step's bits reach the partner who hasn't done that quest.
        {
            namespace quest = d2d::rules::quest;
            auto& theirs = second.character.header.quests[std::size_t(second.character.header.active_difficulty())];
            quest::set(theirs, 6, 0);                                     // done Sisters to the Slaughter already
            coop.credit_party([&] {
                auto& mine = coop.quests();
                quest::set(mine, 7, 13); quest::set(mine, 7, 1);
                quest::set(mine, 6, 13);
                quest::set(mine, 5, 13);                                  // the Tower credits each player itself (World::kOwnCredit)
            });
            assert(quest::bit(theirs, 7, 13) && quest::bit(theirs, 7, 1) && !quest::bit(theirs, 6, 13) && !quest::bit(theirs, 5, 13));
        }

        // Death in co-op (local-coop.md): a ghost with its body where it fell,
        // the gear kept; it walks, monsters pass it by; a partner's Resurrect
        // channel raises it there in 10 s (two in 5); a move ends one's.
        {
            auto& lead = coop.lead();
            const auto die = [&](World::Player& who) {
                coop.act_as(who);
                who.character.stats.values[d2d::d2s::kLife] = 0;
                coop.fight.set_pmode(kModeDT, now_ms);
                coop.act_as(coop.lead());
                for (int frame = 0; frame < 100 && !who.ghost; ++frame, now_ms += kTickMs) coop.tick({}, now_ms + kTickMs, now_ms);
            };
            std::tie(second.player.x, second.player.y) = blood_moor->nearest_free(lead.player.x + 1, lead.player.y);
            const auto items = second.character.items.size();
            die(second);
            assert(second.ghost && second.body && second.character.items.size() == items && second.corpses.empty());
            const float body_x = second.body->x, body_y = second.body->y;
            const auto [step_x, step_y] = blood_moor->nearest_free(body_x + 2, body_y);
            const std::vector<Order> float_on{ { second.id, cmd::Move{ step_x, step_y, true } } };
            for (int frame = 0; frame < 25; ++frame, now_ms += kTickMs) coop.tick(frame == 0 ? std::span<const Order>(float_on) : std::span<const Order>{}, now_ms + kTickMs, now_ms);
            assert(std::hypot(second.player.x - body_x, second.player.y - body_y) > 0.5f && second.body->x == body_x);   // the ghost walked, the body stayed
            const std::vector<Order> raise{ { lead.id, cmd::UseSkill{ kResurrectSkill, body_x, body_y, second.id } } };
            int frames = 0;
            for (; frames < 400 && second.ghost; ++frames, now_ms += kTickMs) coop.tick(frames == 0 ? std::span<const Order>(raise) : std::span<const Order>{}, now_ms + kTickMs, now_ms);
            assert(!second.ghost && !second.body && second.pmode < 0 && second.character.stats.get(d2d::d2s::kLife) > 0);
            assert(frames >= 250 && frames < 330);              // 10 s of channel (250 frames), after the walk up
            assert(std::hypot(second.player.x - body_x, second.player.y - body_y) < 0.01f);   // up where it fell
            // A move ends the channel: the share done is lost.
            die(second);
            const std::vector<Order> start{ { lead.id, cmd::UseSkill{ kResurrectSkill, second.body->x, second.body->y, second.id } } };
            for (int frame = 0; frame < 120; ++frame, now_ms += kTickMs) coop.tick(frame == 0 ? std::span<const Order>(start) : std::span<const Order>{}, now_ms + kTickMs, now_ms);
            assert(second.body && second.body->raised > 0.1f);
            const std::vector<Order> walk_off{ { lead.id, cmd::Move{ lead.player.x + 3, lead.player.y, true } } };
            coop.tick(walk_off, now_ms + kTickMs, now_ms); now_ms += kTickMs;
            coop.tick({}, now_ms + kTickMs, now_ms); now_ms += kTickMs;
            assert(second.body && second.body->raised == 0 && lead.raising < 0);
            // Two channelling: twice as fast.
            auto third_character = std::move(character);
            third_character.name = "Third";
            auto& third = coop.join(third_character);
            std::tie(third.player.x, third.player.y) = blood_moor->nearest_free(second.body->x - 1, second.body->y);
            third.target_x = third.player.x; third.target_y = third.player.y;
            const std::vector<Order> both{ { lead.id, cmd::UseSkill{ kResurrectSkill, second.body->x, second.body->y, second.id } },
                                           { third.id, cmd::UseSkill{ kResurrectSkill, second.body->x, second.body->y, second.id } } };
            frames = 0;
            for (; frames < 400 && second.ghost; ++frames, now_ms += kTickMs) coop.tick(frames == 0 ? std::span<const Order>(both) : std::span<const Order>{}, now_ms + kTickMs, now_ms);
            assert(!second.ghost && frames >= 125 && frames < 200);
            assert(coop.leave(third.id) == "no character store");
            die(second);
            // In a town the ghosts are up again.
            coop.arrive(&data->town, data->town.start.first, data->town.start.second, "a test");
            coop.tick({}, now_ms + kTickMs, now_ms); now_ms += kTickMs;
            assert(!second.ghost && !second.body);
        }

        assert(coop.leave(second.id) == "no character store" && coop.players.size() == 1 && coop.fight.heroes.size() == 1);
        assert(coop.leave(coop.lead().id) == "the last player stays");
        std::printf("OK: two players: their own orders, the other in the View, the party arrives together, monsters go at the near one; scaling, party experience, gold, quest credit\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
