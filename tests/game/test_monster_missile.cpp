// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--monster-missile CASES`: what a monster's MissA / MissS1 / MissC shot
// carries, the cases of tools/emu/mm_diff.py (game.exe's FUN_0059fa30 ->
// FUN_0059f900 -> FUN_0064b860 on a real shooter): the REAL Ai::tick
// functions monster_shot_phys (256ths) and monster_shot_stats (the elements
// the missile's src carries), one line each, to diff.
//
// A case line: ID ROW FIRST CAST S1 LVL A1MIN A1MAX A2MIN A2MAX S1MIN S1MAX
// Output: ID PHYS_LO PHYS_HI, then per element of mode A2: ETYPE MIN MAX DUR,
// then `P MIN MAX LEN` when the shot carries poison (MonStats::poison_*:
// per frame in 256ths, frames).
//
// `--missile-block-skill CASES`: tab-separated lines `ID LEVEL ROW SKILL` of the skill-launched rows (andariel_missile at LEVEL with the monster skill SKILL: the Fallen
// Shaman's fire, the Death Mauler's maul, ...), the same `K` output.
// `--missile-block CASES`: the same shots through the real monster_blow against a defender with Block 75 and nothing else:
// `K ID BLOCKED 400`, as tools/emu/missile_block.py compares them with game.exe's FUN_005ad730 (can-block of FUN_0057dfb0).
//
// With no arguments: the poison of a row's own element through the REAL
// monster_blow, no game data (rows typed from Missiles.txt 1.14d).

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

// What a hit of the shot applies: the poison the record holds (per frame in
// 256ths for ELen frames, FUN_0064b860 slots +0x34 / +0x38 / +0x3c), through the
// real monster_blow against a defender with no resistance.
d2d::rules::Taken hit_by(const GameData::MissileInfo& info, int lvl, const d2d::rules::MonStats& owner = {}) {
    d2d::rules::MonStats shot = monster_shot_stats(info, owner, false, false, false, lvl);
    shot.level = 99; shot.to_hit = 1000000;
    d2d::rules::Rng rng(7);
    d2d::rules::Fighter defender;
    d2d::rules::Taken taken;
    for (int i = 0; i < 50 && !taken.hit; ++i) taken = d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, shot, true, rng, true);
    assert(taken.hit);
    return taken;
}

GameData::MissileInfo poison_row(int emin, int emax, int bracket_min, int bracket_max, int elen) {
    GameData::MissileInfo info;
    info.etype = 3; info.hitshift = 1; info.emin = emin; info.emax = emax; info.elen = elen;
    info.emin_lev.fill(bracket_min); info.emax_lev.fill(bracket_max);
    return info;
}

int unit_test() {
    // goospit1 (Sand Maggot), mummy4 (Radament, unraveler4 / 5), pantherpotgreen (Night Slinger):
    // game.exe's record, FUN_0064b100 / 0064b1d0 / 0064b2a0 run in the emulator (tools/emu/mm_diff.py),
    // which the verifier's oracle reproduces: per frame (256ths) x frames / 256 = total points.
    const auto goo = poison_row(4, 4, 3, 3, 200), mummy = poison_row(9, 9, 5, 5, 400), pot = poison_row(9, 12, 6, 6, 400);
    {
        const auto taken = hit_by(goo, 1);
        assert(taken.poison == 8 && taken.poison_ticks == 200);        // 6.25 points over 200 frames
        const auto hell = hit_by(goo, 8);
        assert(hell.poison == 50 && hell.poison_ticks == 200);         // 39.1 points, not 609
    }
    {
        assert(hit_by(mummy, 1).poison == 18 && hit_by(mummy, 1).poison_ticks == 400);
        const auto hell = hit_by(mummy, 8);
        assert(hell.poison == 88 && hell.poison_ticks == 400);         // 137.5 points, not 4,281
    }
    {
        const auto taken = hit_by(pot, 8);
        assert(taken.poison >= 102 && taken.poison <= 108 && taken.poison_ticks == 400);   // 159 to 169 points in all, not 5,250
        assert(taken.poison * taken.poison_ticks / 256 <= 170);
    }
    {                                                                  // FUN_005ad730: a shot with no physical record is never blocked (can-block 0 skips the roll)
        const auto blocks = [&](int min, int max) {
            GameData::MissileInfo row;
            row.min = min << 8; row.max = max << 8;
            auto shot = monster_shot_stats(row, {}, false, false, false, 1);
            shot.no_hit_roll = true;
            d2d::rules::Fighter defender;
            defender.block = 75;
            d2d::rules::Rng rng(11);
            int blocked = 0;
            for (int i = 0; i < 400; ++i) blocked += d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, shot, true, rng, true).blocked ? 1 : 0;
            return blocked;
        };
        assert(blocks(0, 0) == 0 && blocks(0, 5) == 0 && blocks(1, 5) > 0 && blocks(3, 3) > 0);
        GameData::MissileInfo fire;                                    // a skill's missile with no physical (shafire): missile_blow, the Fallen Shaman's path
        fire.min = 0; fire.max = 0;
        GameData::MissileInfo spray;
        spray.min = 7 << 8; spray.max = 9 << 8;
        for (const auto* row : { &fire, &spray }) {
            auto shot = missile_blow(*row, d2d::rules::MissileDamage{}, 30);
            shot.no_hit_roll = true;
            d2d::rules::Fighter defender;
            defender.block = 75;
            d2d::rules::Rng rng(11);
            int blocked = 0;
            for (int i = 0; i < 400; ++i) blocked += d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, shot, true, rng, true).blocked ? 1 : 0;
            assert((blocked == 0) == (row == &fire));
        }
    }
    {                                                                  // the row's poison rides on the missile's poison stats, not an element
        const auto shot = monster_shot_stats(goo, {}, false, false, false, 1);
        assert(shot.poison_min == 8 && shot.poison_max == 8 && shot.poison_len == 200);
        assert(std::ranges::none_of(shot.elements, [](const auto& element) { return element.type == 3; }));
        d2d::rules::MonStats champion;                                // an owner's own poison hit is dropped on a SrcDamage-0 row (all poison rows)
        champion.poison_min = champion.poison_max = 40; champion.poison_len = 300;
        const auto both = monster_shot_stats(goo, champion, false, false, false, 1);
        assert(both.poison_min == 8 && both.poison_max == 8 && both.poison_len == 200);
        // SrcDamage -1 is the byte 0xff = 255 in game.exe (movzx at 0x64b8ed): 95, not -1 / 128 of the pair (0)
        // where SrcDamage != 0 (no real poison row; patched as the verifier's oracle did): (row + owner) x SrcDamage / 128, lengths summed
        const auto with_src = [&](int src_damage) { auto row = goo; row.src_damage = src_damage; return monster_shot_stats(row, champion, false, false, false, 1); };
        for (const auto& [src_damage, rate] : { std::pair{128, 48}, std::pair{64, 24}, std::pair{255, 95}, std::pair{-1, 95} }) {
            const auto scaled = with_src(src_damage);
            assert(scaled.poison_min == rate && scaled.poison_max == rate && scaled.poison_len == 500);
        }
    }
    {                                                                  // a boss's enchants (owner stats 0x30..0x38): only with SrcDamage, x SrcDamage / 128 (FUN_0064a930 / FUN_0064aa60)
        d2d::rules::MonStats boss;
        boss.enchants[0] = std::pair{ 10, 20 }; boss.enchants[2] = std::pair{ 4, 8 }; boss.enchant_len = 100;
        GameData::MissileInfo row;
        assert(std::ranges::none_of(monster_shot_stats(row, boss, true, false, false, 1).enchants, [](const auto& enchant) { return enchant.has_value(); }));
        row.src_damage = 128;
        for (const bool s1_shot : { false, true }) {
            const auto shot = monster_shot_stats(row, boss, !s1_shot, false, s1_shot, 1);
            assert(shot.enchants[0] == std::pair(10, 20) && shot.enchants[2] == std::pair(4, 8) && shot.enchant_len == 100);
        }
        row.src_damage = 96;
        const auto scaled = monster_shot_stats(row, boss, false, false, false, 1);
        assert(scaled.enchants[0] == std::pair(7, 15) && scaled.enchants[2] == std::pair(3, 6) && scaled.enchant_len == 75);
        assert(!monster_shot_stats(row, boss, false, true, false, 1).enchants[0]);
    }
    return 0;
}

int main(int argc, char** argv) try {
    const bool block_mode = argc >= 3 && std::string_view(argv[1]) == "--missile-block";
    const bool skill_mode = argc >= 3 && std::string_view(argv[1]) == "--missile-block-skill";
    if (argc < 3 || (std::string_view(argv[1]) != "--monster-missile" && !block_mode && !skill_mode)) return unit_test();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    std::ifstream cases(argv[2]);
    std::string line;
    if (skill_mode) {
        // A monster skill's missile (andariel_missile: Fallen Shaman fire, Radament's bolt, ...) through missile_blow and the real monster_blow, as above.
        while (std::getline(cases, line)) {
            std::istringstream fields(line);                         // ID TAB LEVEL TAB ROW TAB SKILL: a row name may hold a space (death mauler)
            int case_id = 0, lvl = 1;
            std::string row, skill;
            fields >> case_id >> lvl;
            fields.ignore(1);
            std::getline(fields, row, '\t');
            std::getline(fields, skill);
            const auto found = data->missiles.find(row);
            if (found == data->missiles.end()) { std::printf("N %d\n", case_id); continue; }      // d2d does not load the row (only the rows its monsters and skills name)
            // The real andariel_missile at the level d2d launches it, with the monster skill it carries when the row names a Skill (the Death Mauler's maul).
            const auto skill_found = data->skills.by_name.find(skill);
            const auto* carried = skill_found != data->skills.by_name.end() ? data->skills.get(skill_found->second) : nullptr;
            std::vector<Missile> made;
            andariel_missile(*data, Monster{}, row, 1, 0, 0, made, lvl, carried);
            if (made.empty()) { std::printf("N %d\n", case_id); continue; }
            auto stats = made.back().src;
            stats.no_hit_roll = true;
            d2d::rules::Fighter defender;
            defender.block = 75;
            d2d::rules::Rng rng(std::uint32_t(case_id) * 2654435761u + 7);
            int blocked = 0;
            for (int trial = 0; trial < 400; ++trial)
                blocked += d2d::rules::monster_blow(defender, 30, d2d::rules::Motion::standing, stats, true, rng, true).blocked ? 1 : 0;
            std::printf("K %d %d 400\n", case_id, blocked);
        }
        return 0;
    }
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int case_id = 0, first = 0, cast = 0, s1_shot = 0, lvl = 0;
        std::string row;
        d2d::rules::MonStats owner;
        fields >> case_id >> row >> first >> cast >> s1_shot >> lvl >> owner.a1_min >> owner.a1_max >> owner.a2_min >> owner.a2_max >> owner.s1_min >> owner.s1_max;
        const auto found = data->missiles.find(row);
        if (found == data->missiles.end()) { std::fprintf(stderr, "no row %s\n", row.c_str()); return 1; }
        const auto& info = found->second;
        const auto phys = monster_shot_phys(info, first ? owner.a1_min : s1_shot ? owner.s1_min : owner.a2_min, first ? owner.a1_max : s1_shot ? owner.s1_max : owner.a2_max, cast != 0, lvl);
        auto stats = monster_shot_stats(info, owner, first != 0, cast != 0, s1_shot != 0, lvl);
        if (block_mode) {
            // The shot through the real monster_blow against a defender whose only roll is Block 75: how many of 400 are blocked.
            stats.no_hit_roll = true;
            d2d::rules::Fighter defender;
            defender.block = 75;
            d2d::rules::Rng rng(std::uint32_t(case_id) * 2654435761u + 7);
            int blocked = 0;
            for (int trial = 0; trial < 400; ++trial)
                blocked += d2d::rules::monster_blow(defender, 30, d2d::rules::Motion::standing, stats, true, rng, true).blocked ? 1 : 0;
            std::printf("K %d %d 400\n", case_id, blocked);
            continue;
        }
        std::printf("%d %d %d", case_id, phys.low, phys.high);
        for (const auto& element : stats.elements)
            if (element.type >= 0 && element.mode == "A2") std::printf(" %d %d %d %d", element.type, element.min, element.max, element.dur);
        if (stats.poison_max > 0) std::printf(" P %d %d %d", stats.poison_min, stats.poison_max, stats.poison_len);
        std::printf("\n");
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_monster_missile: %s\n", error.what());
    return 1;
}
