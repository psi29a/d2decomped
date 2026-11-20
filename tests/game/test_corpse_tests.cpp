// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Necromancer's corpse tests, as tools/emu/corpse_tests.py runs game.exe's
// start functions 15, 17 and 21: `--corpse-tests` reads its cases (`pick mode
// flags c4 c6 hidden s36 s8f town`: c4 the unit flag
// +0xc4 & 4, c6 byte +0xc6 & 1, the row `pick` modulo the MonStats rows)
// and prints each with `| raise blast revive`: rules::raise_takes,
// blast_takes and revive_takes.
// Without arguments, with the game data: Fight::corpse_near through Revive's
// start skips a champion's corpse and takes a plain zombie's; swaps_ai
// refuses a live champion.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
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

// Two zombie corpses by the player, a champion's nearer: Revive's start
// passes it over for the plain one; Raise's takes the nearer.
int pick_check(const GameData& data) {
    d2d::rules::Rng rng{ 7 };
    Character character;
    auto made = new_character(data, 2, "Gravedigger", false, true, rng);
    character.name = "Gravedigger";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(data, character.header, character.items, character.stats);
    auto world = std::make_unique<World>(&data, -1, -1);
    world->enter(character);
    Level open = *data.level(d2d::rules::level_ids::kBloodMoor);
    world->level = &open;
    world->fight.enter(&open);
    const int zombie = data.monsters.row("zombie1");
    assert(data.monsters.types[std::size_t(zombie)].revive && data.monsters.types[std::size_t(zombie)].corpse_sel);
    const float spot_x = world->actor->player.x + 3, spot_y = world->actor->player.y;
    for (const auto& [boss, off] : { std::pair{ d2d::rules::Boss::champion, 0.1f }, std::pair{ d2d::rules::Boss::none, 0.5f } }) {
        auto& corpse = world->fight.monsters.emplace_back();
        corpse.type = zombie;
        corpse.boss = boss;
        corpse.mode = "DD";
        corpse.hit_points = 0;
        corpse.unit.x = spot_x + off; corpse.unit.y = spot_y;
    }
    const int champion = int(world->fight.monsters.size()) - 2, plain = champion + 1;
    using Start = d2d::rules::ServerStartFunction;
    assert(world->fight.corpse_near(spot_x, spot_y, Start::kRevive) == plain);
    assert(world->fight.corpse_near(spot_x, spot_y, Start::kRaiseSkeleton) == champion);
    // The same FUN_005dd480 test on the living (Confuse, Attract, Howl): a champion's think stays its own.
    auto live_champion = world->fight.monsters[std::size_t(champion)], live_plain = world->fight.monsters[std::size_t(plain)];
    for (auto* live : { &live_champion, &live_plain }) { live->hit_points = live->stats.hit_points = 10; live->mode = "NU"; }
    assert(!swaps_ai(data, live_champion) && swaps_ai(data, live_plain));
    std::puts("OK: Revive passes a champion's corpse over for a plain zombie's; Raise takes the champion's; a live champion keeps its think");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--corpse-tests";
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--corpse-tests needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)");
        return D2D_TEST_SKIPPED;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) {
        if (cases) { std::fputs("--corpse-tests needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)");
        return D2D_TEST_SKIPPED;
    }
    return cases ? corpse_cases(*data) : pick_check(*data);
} catch (const std::exception& error) {
    std::printf("test_corpse_tests: %s\n", error.what());
    return 1;
}
