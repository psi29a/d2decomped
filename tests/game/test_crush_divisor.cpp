// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The life divisor of a Crushing Blow on a monster: the divisor the real Monster::target hands player_blow (Monster::crush_divisor)
// for a MonStats row, how the monster was made and the game's player count.
//   `--crush CASES` reads tools/emu/crush_div.py's case lines (`id row kind players`) and prints `id crush`: the life
//   (262,144 points, in 256ths) over the divisor, whole; `crush_div.py compare` checks it against game.exe's FUN_005bffc0 on
//   a type-1 unit. Needs the MPQs and 1.14d's tables (D2_MPQ_DIR, D2_PATCH_INSTALLER); it skips without.
//   no argument: game.exe's divisors for pinned rows and kinds.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <montypes.hpp>
#include <uniques.hpp>

#include <array>
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

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kLife = std::int64_t(1) << 26;
constexpr std::array<d2d::rules::Boss, 5> kKinds{ d2d::rules::Boss::none, d2d::rules::Boss::champion, d2d::rules::Boss::unique,
                                                  d2d::rules::Boss::superunique, d2d::rules::Boss::minion };

// The crush of `kLife` on a monster of MonStats `row` made as `kind` (0 plain, 1 champion, 2 unique, 3 superunique, 4 minion).
std::int64_t crush_of(const GameData& data, int row, int kind, int players) {
    Monster monster;
    monster.type = row;
    monster.boss = kKinds[std::size_t(kind)];
    monster.players = players;
    return kLife / monster.target(data).crush_divisor;   // what player_blow divides the life by
}

// game.exe's crushes of a 262,144-point life (crush_div.py game on these rows: FUN_005bffc0 on a type-1 unit; V run), 256ths: a
// plain monster, a champion and a UNIQUE take a quarter (4); a superunique and a MonStats boss row an eighth (8); a hireling a tenth
// whatever the kind, a superunique too, and without the player share at any count (4 players would give 10 to a plain unique
// too); each player after the first adds a share of the divisor from FUN_005738f0's table (2 players +50 percent, 3 +100, 8 +350).
struct Pin { std::string_view id; int kind, players; std::int64_t crush; };
constexpr std::array<Pin, 14> kPins{ {
    { "zombie1", 0, 1, 16777216 },
    { "zombie1", 1, 1, 16777216 },
    { "zombie1", 2, 1, 16777216 },
    { "zombie1", 3, 1, 8388608 },
    { "zombie1", 3, 3, 4194304 },
    { "zombie1", 2, 2, 11184810 },
    { "andariel", 0, 1, 8388608 },
    { "andariel", 2, 1, 8388608 },
    { "roguehire", 2, 1, 6710886 },
    { "zombie1", 4, 1, 16777216 },
    { "diablo", 3, 8, 1864135 },
    { "quillrat1", 2, 3, 8388608 },
    { "roguehire", 0, 4, 6710886 },
    { "roguehire", 3, 1, 6710886 },
} };

}   // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    const bool oracle = mode == "--crush" && argc > 2;
    if (!oracle && argc > 1) {
        std::printf("SKIP: an oracle mode, `test_crush_divisor --crush CASES`\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return oracle ? D2D_TEST_SKIPPED : 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return oracle ? D2D_TEST_SKIPPED : 0; }
    if (oracle) {
        std::ifstream cases(argv[2]);
        if (!cases) { std::fprintf(stderr, "FAIL: cannot open %s\n", argv[2]); return 1; }
        std::string line;
        while (std::getline(cases, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream words(line);
            long id = 0, row = 0, kind = 0, players = 0;
            if (!(words >> id >> row >> kind >> players) || kind < 0 || kind > 4 || row < 0 || row >= long(data->monsters.types.size())) {
                std::fprintf(stderr, "FAIL: bad case line: %s\n", line.c_str());
                return 1;
            }
            std::printf("%ld %lld\n", id, static_cast<long long>(crush_of(*data, int(row), int(kind), int(players))));
        }
        return 0;
    }
    for (const auto& pin : kPins) {
        const auto found = data->monsters.by_id.find(std::string(pin.id));
        assert(found != data->monsters.by_id.end());
        assert(crush_of(*data, int(found->second), pin.kind, pin.players) == pin.crush);
    }
    std::printf("ok\n");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
