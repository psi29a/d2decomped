// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A plain monster's unit seed after its stat init, its life and its look,
// through the REAL make_monster against stock 1.14d's FUN_00573cb0 run by
// tools/emu/unit_seed_init.py (unit_seed_init_cases.inc): every MonStats row
// and difficulty it is made in, then random ones. What a monster's first
// think draws is the seed as all that left it. `--unit-seed CASES` prints
// d2d's answers to a cases file instead (`id | seed_low seed_high life256
// look`), to diff with the script's. Needs the game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace d2d::game;

namespace {

struct Case { const char* id; int row, difficulty; std::uint32_t seed, low, high; int life_fine; const char* look; };

constexpr Case kCases[] = {
#include "unit_seed_init_cases.inc"
};

struct Made { std::uint32_t low, high; int life_fine; std::string look; };

// make_monster on that unit seed, no region (FUN_005739d0 rolls each layer), one player, no area. `look`: the
// layers' picks, game.exe's unit data +4.. (the first draws on the seed, rules::monster_look); `parts_ok`: each
// component make_monster dressed it in is that pick's part.
Made made(const GameData& data, int row, int difficulty, std::uint32_t seed, bool* parts_ok = nullptr) {
    d2d::rules::Rng rng{ 1 };
    const Monster monster = make_monster(data, row, 0, 0, rng, difficulty, true, 1, -1, d2d::rules::Rng{ seed });
    const auto& type_info = data.monsters.types[std::size_t(row)];
    d2d::rules::Rng first{ seed };
    auto picks = d2d::rules::monster_look(nullptr, type_info.choices, first);
    if (monster.variant >= 0) picks[10] = std::uint8_t(monster.variant);   // the knights' rand(4) over S3 (+0xe), 0x138's S4 (+0xf)
    if (monster.variant >= 0 && row == 0x138) picks[11] = std::uint8_t(monster.variant);
    constexpr char kHex[] = "0123456789abcdef";
    std::string look;
    for (std::size_t layer = 0; layer < 16; ++layer) {
        look += kHex[picks[layer] >> 4 & 0xf];
        look += kHex[picks[layer] & 0xf];
        if (parts_ok && picks[layer] < type_info.parts[layer].size() && monster.npc.comp[layer] != type_info.parts[layer][picks[layer]]) *parts_ok = false;
    }
    return { monster.seed.low, monster.seed.high, monster.lifeless ? 0 : monster.stats.hit_points * 256, look };
}

}   // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: the oracle is 1.14d's game.exe: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto known = [&](int row, std::string_view name) {
        return row >= 0 && std::size_t(row) < data->monsters.types.size() && data->monsters.types[std::size_t(row)].id == name;
    };
    if (argc >= 3 && std::string_view(argv[1]) == "--unit-seed") {
        std::ifstream case_file(argv[2]);
        std::string line;
        while (std::getline(case_file, line)) {
            std::istringstream fields(line);
            std::string id, name;
            int row = 0, difficulty = 0;
            std::uint64_t seed = 0;
            if (!(fields >> id >> name >> row >> difficulty >> seed)) continue;
            if (!known(row, name)) { std::printf("%s | no-type\n", id.c_str()); continue; }
            const auto out = made(*data, row, difficulty, std::uint32_t(seed));
            std::printf("%s | %u %u %d %s\n", id.c_str(), out.low, out.high, out.life_fine, out.look.c_str());
        }
        return 0;
    }
    int checked = 0, bad = 0, skipped = 0;
    for (const auto& one : kCases) {
        // d2d keeps one row of a duplicated Id: a case of the other is not d2d's.
        if (!known(one.row, one.id)) { ++skipped; continue; }
        bool parts_ok = true;
        const auto out = made(*data, one.row, one.difficulty, one.seed, &parts_ok);
        ++checked;
        if (out.low != one.low || out.high != one.high || out.life_fine != one.life_fine || out.look != one.look || !parts_ok) {
            if (++bad <= 20)
                std::printf("FAIL %s row %d diff %d seed %u: d2d %u %u %d %s%s, game.exe %u %u %d %s\n", one.id, one.row, one.difficulty, one.seed,
                            out.low, out.high, out.life_fine, out.look.c_str(), parts_ok ? "" : " (parts)", one.low, one.high, one.life_fine, one.look);
        }
    }
    std::printf("%d cases, %d wrong, %d rows d2d doesn't keep\n", checked, bad, skipped);
    return bad == 0 && checked > 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
