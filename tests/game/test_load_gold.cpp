// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The purse and stash gold a character enters a game with: a saved value over its cap is 0
// (FUN_0056b180 -> FUN_0056af80 at 0x56afb4..0x56b00f through FUN_0053ff00; caps FUN_00622e70
// `clvl x 10000`, FUN_00623460 2,500,000). Needs the MPQs (D2_MPQ_DIR, as test_game); it skips without.
// `--load-gold CASES` prints, per case `id level gold bank`, `id gold bank`: the cases of
// tools/emu/fl_gold.py, to diff against game.exe's.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());

    d2d::rules::Rng made_rng(7);
    const auto made = new_character(*data, 4, "Headless", false, true, made_rng);
    // The purse and stash gold of a save with these three values, after entering a game.
    const auto entered = [&](std::int64_t level, std::int64_t gold, std::int64_t bank) {
        using namespace d2d::d2s;
        Character saved;
        saved.character_class = 4;
        saved.name = "Headless";
        saved.header = made.header;
        saved.stats = made.stats;
        saved.items = made.items;
        saved.panel = panel_stats(*data, saved.header, saved.items, saved.stats);
        saved.stats.values[kLevel] = level; saved.stats.values[kGold] = gold; saved.stats.values[kGoldBank] = bank;
        World world(&*data, -1, -1);
        world.enter(saved);
        const auto& got = world.actor->character.stats.values;
        return std::pair<std::int64_t, std::int64_t>{ got[kGold], got[kGoldBank] };
    };

    if (argc > 2 && std::string_view(argv[1]) == "--load-gold") {
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "load-gold: cannot open %s\n", argv[2]); return 1; }
        std::string id;
        std::int64_t level = 0, gold = 0, bank = 0;
        while (cases >> id >> level >> gold >> bank) {
            const auto [got_gold, got_bank] = entered(level, gold, bank);
            std::printf("%s %lld %lld\n", id.c_str(), static_cast<long long>(got_gold), static_cast<long long>(got_bank));
        }
        return 0;
    }

    // At the cap is kept, one over is 0, each of the two on its own.
    assert((entered(4, 40000, 2500000) == std::pair<std::int64_t, std::int64_t>{ 40000, 2500000 }));
    assert((entered(4, 40001, 2500001) == std::pair<std::int64_t, std::int64_t>{ 0, 0 }));
    assert((entered(4, 40001, 2500000) == std::pair<std::int64_t, std::int64_t>{ 0, 2500000 }));
    assert((entered(4, 40000, 2500001) == std::pair<std::int64_t, std::int64_t>{ 40000, 0 }));
    std::printf("OK: a save's gold over its cap is 0 on entering a game\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
