// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Necromancer's corpse tests, as tools/emu/corpse_tests.py runs game.exe's
// start functions 15, 17 and 21: `--corpse-tests` reads its cases (`pick mode
// flags c4 c6 hidden s36 s8f town`: c4 the unit flag
// +0xc4 & 4, c6 byte +0xc6 & 1, the row `pick` modulo the MonStats rows)
// and prints each with `| raise blast revive`: rules::raise_takes,
// blast_takes and revive_takes.
// Without arguments, on synthetic rows: Fight::corpse_near by each start
// (corpseSel, the champion, a shatter) and swaps_ai on a live champion.

#include <ai.hpp>
#include <cues.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <loot.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>
#include <uniques.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int corpse_cases(const GameData& data) {
    const auto& types = data.monsters.types;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        long long pick = 0, mode = 0, flags = 0, unit_flag = 0, dying_byte = 0, hidden = 0, s36 = 0, s8f = 0, town = 0;
        if (!(fields >> pick >> mode >> flags >> unit_flag >> dying_byte >> hidden >> s36 >> s8f >> town)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto& type = types[std::size_t(pick) % types.size()];
        const d2d::rules::CorpseState corpse{ .mode = int(mode), .flags = int(flags & 0xffff), .targetable = unit_flag != 0, .dying = dying_byte != 0, .hidden = hidden != 0,
                                              .uninterruptable = s36 != 0, .attached = s8f != 0, .town = town != 0 };
        std::printf("%s | %d %d %d\n", line.c_str(), d2d::rules::raise_takes(type, corpse) ? 1 : 0, d2d::rules::blast_takes(type, corpse) ? 1 : 0,
                    d2d::rules::revive_takes(type, corpse) ? 1 : 0);
    }
    return 0;
}

// Synthetic rows, no game data: a zombie that passes every test and a
// copy with corpseSel off. Two zombie corpses by the player, a champion's
// nearer: Revive's start passes it over for the plain one, Raise's takes the
// nearer; the corpseSel-less one is no one's; a shattered one isn't either;
// swaps_ai refuses a live champion.
int pick_check() {
    d2d::test::TestData fixture;
    for (const auto& [row, id] : { std::pair{ 0, "zombie" }, std::pair{ 1, "nocorpse" } }) {
        auto& type = fixture.monster(row, id);
        type.corpse_sel = row == 0; type.revive = true; type.velocity = 3; type.can_walk = true; type.switch_ai = true;
    }
    auto& data = fixture.data;
    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    arena.walk.resize(150 * 150);
    const Level* level = &arena;
    d2d::rules::Rng random{ 7 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, random, cues };
    loot.ground_level = level;
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    hero.player.x = hero.player.y = 10;
    const float spot_x = 13, spot_y = 10;
    const auto corpse = [&](int type, d2d::rules::Boss boss, float off) {
        auto& made = fight.monsters.emplace_back();
        made.type = type; made.boss = boss; made.mode = "DD"; made.hit_points = 0;
        made.unit.x = spot_x + off; made.unit.y = spot_y;
        return int(fight.monsters.size()) - 1;
    };
    using Start = d2d::rules::ServerStartFunction;
    // Corpse Explosion's start refuses the cast itself with no corpse its test passes (LAB_005c31c0), before any cost.
    auto& blast = fixture.skill(74, "Corpse Explosion");
    blast.srvstfunc = Start::kCorpseBlast; blast.srvdofunc = d2d::rules::ServerDoFunction::kCorpseExplosion;
    fight.skill_level = [](int) { return 1; };
    fight.skill_base = fight.skill_level;
    assert(!fight.cast_missile(74, spot_x, spot_y, 1000));
    const int no_corpse = corpse(1, d2d::rules::Boss::none, 0.f);
    assert(!fight.cast_missile(74, spot_x, spot_y, 1000));
    assert(fight.corpse_near(spot_x, spot_y, Start::kRaiseSkeleton) < 0 && fight.corpse_near(spot_x, spot_y, Start::kCorpseBlast) < 0
           && fight.corpse_near(spot_x, spot_y) == no_corpse);
    const int champion = corpse(0, d2d::rules::Boss::champion, 0.1f), plain = corpse(0, d2d::rules::Boss::none, 0.5f);
    assert(fight.corpse_near(spot_x, spot_y, Start::kRevive) == plain);
    assert(fight.cast_missile(74, spot_x, spot_y, 1000));
    assert(fight.corpse_near(spot_x, spot_y, Start::kRaiseSkeleton) == champion);
    fight.monsters[std::size_t(champion)].shatter = true;
    assert(fight.corpse_near(spot_x, spot_y, Start::kRaiseSkeleton) == plain);
    // The same FUN_005dd480 test on the living (Confuse, Attract, Howl): a champion's think stays its own.
    auto live_champion = fight.monsters[std::size_t(champion)], live_plain = fight.monsters[std::size_t(plain)];
    for (auto* live : { &live_champion, &live_plain }) { live->hit_points = live->stats.hit_points = 10; live->mode = "NU"; live->shatter = false; }
    assert(!swaps_ai(data, live_champion) && swaps_ai(data, live_plain));
    std::puts("OK: Revive passes a champion's corpse over; Raise takes it; no corpseSel or a shatter is no one's; a live champion keeps its think");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--corpse-tests";
    if (!cases) return pick_check();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        std::fputs("--corpse-tests needs D2_MPQ_DIR\n", stderr);
        return 1;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        std::fputs("--corpse-tests needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr);
        return 1;
    }
    return corpse_cases(*data);
} catch (const std::exception& error) {
    std::printf("test_corpse_tests: %s\n", error.what());
    return 1;
}
