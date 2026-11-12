// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Where a level warp puts the player: FUN_005550b0 finds the free spot nearest the
// other side's warp unit (FUN_0064e7b0 -> FUN_0064dea0, size 2, mask 0x1c09; no spot
// in rings 1 to 49 and FUN_00554ea0's second search fails, so the warp is not taken).
//   `--warp-arrival CASES`: World::warp_arrival on a 200 x 200 subtile Level, the cases
//   of tools/emu/sk_tele_spot.py (`gen --warp N SEED`): `id 1 x y x y n {x y bits}...`,
//   one line each `id did x y` (`- -` when the warp is refused), as
//   `sk_tele_spot.py warp CASES` prints for game.exe. The units' bit 0x1000 is not in
//   the cases (d2d keeps units in the Crowd).
//   no argument: a free unit subtile is the arrival.

#include <ds1.hpp>
#include <gamedata.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

constexpr int kTiles = 40, kSide = kTiles * 5;
using Words = std::vector<std::array<int, 3>>;

int subtile(float cells) { return int(std::floor(cells * 5)); }

float centre(int sub) { return (float(sub) + 0.5f) / 5; }

Level window(const Words& words) {
    Level level;
    level.ds1 = d2d::ds1::Map(kTiles, kTiles, 1, 1);
    level.walk.assign(std::size_t(kSide) * std::size_t(kSide), 0);
    for (const auto& word : words)
        if (word[0] >= 0 && word[1] >= 0 && word[0] < kSide && word[1] < kSide) level.walk[std::size_t(word[1]) * std::size_t(kSide) + std::size_t(word[0])] |= std::uint16_t(word[2]);
    return level;
}

std::optional<std::pair<int, int>> arrival(const Level& level, int x, int y) {
    const auto landing = level.nearest_free(centre(x), centre(y));      // World::arrive's search
    return std::pair{ subtile(landing.first), subtile(landing.second) };
}

using Spot = std::optional<std::pair<int, int>>;

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "--warp-arrival") {
        if (argc < 3) { std::printf("FAIL: --warp-arrival needs a CASES file\n"); return 1; }
        std::ifstream cases(argv[2]);
        if (!cases) { std::printf("FAIL: cannot open %s\n", argv[2]); return 1; }
        std::string line;
        while (std::getline(cases, line)) {
            std::istringstream fields(line);
            int id = 0, tele = 0, unit_x = 0, unit_y = 0, other_x = 0, other_y = 0, count = 0;
            fields >> id >> tele >> unit_x >> unit_y >> other_x >> other_y >> count;
            Words words(std::size_t(std::max(count, 0)));
            for (auto& word : words) fields >> word[0] >> word[1] >> word[2];
            const auto landing = arrival(window(words), unit_x, unit_y);
            if (landing) std::printf("%d 1 %d %d\n", id, landing->first, landing->second);
            else std::printf("%d 0 - -\n", id);
        }
        return 0;
    }
    if (!mode.empty()) { std::printf("FAIL: modes are none and `--warp-arrival CASES`\n"); return 1; }

    assert(arrival(window({}), 100, 100) == Spot(std::pair(100, 100)));   // a free unit subtile: the unit's own
    std::printf("OK: --warp-arrival CASES prints where d2d puts a warp's arrival (the oracle: tools/emu/sk_tele_spot.py warp)\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_warp_arrival: %s\n", error.what());
    return 1;
}
