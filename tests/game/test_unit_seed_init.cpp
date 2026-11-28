// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A plain monster's unit seed after its stat init, its life and its look,
// through the REAL make_monster against stock 1.14d's FUN_00573cb0 run by
// tools/emu/unit_seed_init.py (unit_seed_init_cases.inc): every MonStats row
// and difficulty it is made in, then random ones; and in a level region,
// FUN_00547bc0 first (unit_seed_init_region_cases.inc). What a monster's first
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

// With a level region: `others` rows from 0x40 on (not `row`), then `row` with its sets rolled on
// {sets_seed, 666} when `present`; the region's count and the row's sets after FUN_00547bc0.
struct RegionCase { const char* id; int row, difficulty; std::uint32_t seed; int others, present; std::uint32_t sets_seed; int count, sets;
                    std::uint32_t low, high; int life_fine; const char* look; };

constexpr RegionCase kRegionCases[] = {
#include "unit_seed_init_region_cases.inc"
};

struct Made { int count = 0, sets = 0; std::uint32_t low, high; int life_fine; std::string look; };

// make_monster on that unit seed, one player, no area, in `region` (none: FUN_005739d0 rolls each layer).
// `look`: game.exe's unit data +4.. (Monster::look); `parts_ok`: each component it was dressed in is its pick's.
Made made(const GameData& data, int row, int difficulty, std::uint32_t seed, d2d::rules::Region* region = nullptr, bool* parts_ok = nullptr) {
    d2d::rules::Rng rng{ 1 };
    const Monster monster = make_monster(data, row, 0, 0, rng, difficulty, true, 1, -1, d2d::rules::Rng{ seed }, region);
    const auto& type_info = data.monsters.types[std::size_t(row)];
    constexpr char kHex[] = "0123456789abcdef";
    std::string look;
    for (std::size_t layer = 0; layer < 16; ++layer) {
        const auto pick = monster.look[layer];
        look += kHex[pick >> 4 & 0xf];
        look += kHex[pick & 0xf];
        if (parts_ok && pick < type_info.parts[layer].size() && monster.npc.comp[layer] != type_info.parts[layer][pick]) *parts_ok = false;
    }
    Made out{ 0, 0, monster.seed.low, monster.seed.high, monster.lifeless ? 0 : monster.stats.hit_points * 256, look };
    if (region) {
        out.count = int(region->types.size());
        for (std::size_t i = 0; i < region->types.size(); ++i)
            if (region->types[i].first == row) out.sets = int(region->components[i].size());
    }
    return out;
}

// The case's region, as unit_seed_init.py lays it out.
d2d::rules::Region region_of(const GameData& data, int row, int others, bool present, std::uint32_t sets_seed) {
    d2d::rules::Region region;
    for (int type = 0x40; int(region.types.size()) < others; ++type)
        if (type != row) { region.types.emplace_back(type, 0); region.components.emplace_back(); }
    if (present) {
        d2d::rules::Rng sets_rng{ sets_seed };
        region.types.emplace_back(row, 0);
        region.components.push_back(d2d::rules::roll_components(data.monsters.types[std::size_t(row)].choices, sets_rng));
    }
    return region;
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
        if (!case_file) { std::fprintf(stderr, "test_unit_seed_init: can't open %s\n", argv[2]); return 1; }
        std::string line;
        while (std::getline(case_file, line)) {
            std::istringstream fields(line);
            std::string id, name;
            int row = 0, difficulty = 0;
            std::uint64_t seed = 0, sets_seed = 0;
            int others = -1, present = 0;
            if (!(fields >> id >> name >> row >> difficulty >> seed)) continue;
            if (!known(row, name)) { std::printf("%s | no-type\n", id.c_str()); continue; }
            if (fields >> others >> present >> sets_seed) {
                auto region = region_of(*data, row, others, present != 0, std::uint32_t(sets_seed));
                const auto out = made(*data, row, difficulty, std::uint32_t(seed), &region);
                std::printf("%s | %d %d %u %u %d %s\n", id.c_str(), out.count, out.sets, out.low, out.high, out.life_fine, out.look.c_str());
                continue;
            }
            const auto out = made(*data, row, difficulty, std::uint32_t(seed));
            std::printf("%s | %u %u %d %s\n", id.c_str(), out.low, out.high, out.life_fine, out.look.c_str());
        }
        if (case_file.bad()) { std::fprintf(stderr, "test_unit_seed_init: a read error in %s\n", argv[2]); return 1; }
        return 0;
    }
    int checked = 0, bad = 0, skipped = 0;
    for (const auto& one : kCases) {
        // d2d keeps one row of a duplicated Id: a case of the other is not d2d's.
        if (!known(one.row, one.id)) { ++skipped; continue; }
        bool parts_ok = true;
        const auto out = made(*data, one.row, one.difficulty, one.seed, nullptr, &parts_ok);
        ++checked;
        if (out.low != one.low || out.high != one.high || out.life_fine != one.life_fine || out.look != one.look || !parts_ok) {
            if (++bad <= 20)
                std::printf("FAIL %s row %d diff %d seed %u: d2d %u %u %d %s%s, game.exe %u %u %d %s\n", one.id, one.row, one.difficulty, one.seed,
                            out.low, out.high, out.life_fine, out.look.c_str(), parts_ok ? "" : " (parts)", one.low, one.high, one.life_fine, one.look);
        }
    }
    for (const auto& one : kRegionCases) {
        if (!known(one.row, one.id)) { ++skipped; continue; }
        auto region = region_of(*data, one.row, one.others, one.present != 0, one.sets_seed);
        bool parts_ok = true;
        const auto out = made(*data, one.row, one.difficulty, one.seed, &region, &parts_ok);
        ++checked;
        if (out.count != one.count || out.sets != one.sets || out.low != one.low || out.high != one.high || out.life_fine != one.life_fine
            || out.look != one.look || !parts_ok) {
            if (++bad <= 20)
                std::printf("FAIL %s row %d diff %d seed %u region %d/%d: d2d %d %d %u %u %d %s%s, game.exe %d %d %u %u %d %s\n", one.id, one.row,
                            one.difficulty, one.seed, one.others, one.present, out.count, out.sets, out.low, out.high, out.life_fine, out.look.c_str(),
                            parts_ok ? "" : " (parts)", one.count, one.sets, one.low, one.high, one.life_fine, one.look);
        }
    }
    std::printf("%d cases, %d wrong, %d rows d2d doesn't keep\n", checked, bad, skipped);
    return bad == 0 && checked > 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
