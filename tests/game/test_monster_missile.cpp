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
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

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
        for (const auto [src_damage, rate] : { std::pair{128, 48}, std::pair{64, 24}, std::pair{255, 95}, std::pair{-1, 95} }) {
            const auto shot = with_src(src_damage);
            assert(shot.poison_min == rate && shot.poison_max == rate && shot.poison_len == 500);
        }
    }
    return 0;
}

int main(int argc, char** argv) try {
    if (argc < 3 || std::string_view(argv[1]) != "--monster-missile") return unit_test();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->patched);

    std::ifstream cases(argv[2]);
    std::string line;
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
        const auto stats = monster_shot_stats(info, owner, first != 0, cast != 0, s1_shot != 0, lvl);
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
