// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Where a saved corpse lies (FUN_005352c0 -> FUN_0064e7e0, step 4, the plus
// against 0x9c19): corpse_spot round the town start with the player on it.
// `--corpse-spot CASES G` diffs tools/emu/corpse_spot.py's cases and
// game.exe's answers (`id found x y`).

#include <gamedata.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

using namespace d2d::game;

namespace {

constexpr int kWorld = 200;                      // corpse_spot.py's world: 5 x 5 rooms of 40 subtiles
using Words = std::map<std::pair<int, int>, int>;

std::pair<int, int> spot(const Words& words, int start_x, int start_y) {
    return corpse_spot(start_x, start_y, [&](int at_x, int at_y) {
        if (at_x < 0 || at_y < 0 || at_x >= kWorld || at_y >= kWorld) return 0x27;
        const auto found = words.find({ at_x, at_y });
        return found == words.end() ? 0 : found->second;
    });
}

int diff(const char* cases_path, const char* game_path) {
    std::ifstream cases(cases_path), game(game_path);
    int count = 0, bad = 0;
    for (std::string line, answer; std::getline(cases, line) && std::getline(game, answer); ++count) {
        std::istringstream in(line), expect(answer);
        int number = 0, start_x = 0, start_y = 0, words_count = 0, found = 0, want_x = 0, want_y = 0;
        in >> number >> start_x >> start_y >> words_count;
        Words words;
        for (int k = 0, at_x = 0, at_y = 0, bits = 0; k < words_count && in >> at_x >> at_y >> bits; ++k) words[{ at_x, at_y }] = bits;
        words[{ start_x, start_y }] |= 0x1000;                                      // the player on the start
        expect >> number >> found >> want_x >> want_y;
        const auto [at_x, at_y] = spot(words, start_x, start_y);
        if ((at_x != want_x || at_y != want_y) && bad++ < 5) std::fprintf(stderr, "case %d: game.exe %d %d, d2d %d %d\n", number, want_x, want_y, at_x, at_y);
    }
    std::printf("%d cases, %d differ\n", count, bad);
    return bad == 0 && count > 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--corpse-spot") return diff(argv[2], argv[3]);
    constexpr int kX = 100, kY = 100;
    Words words{ { { kX, kY }, 0x1000 } };
    assert(spot(words, kX, kY) == std::pair(kX - 1, kY - 1));                 // the player on the start: up-left of them
    words[{ kX - 1, kY - 1 }] = 0x1;
    assert(spot(words, kX, kY) == std::pair(kX + 1, kY - 1));
    words[{ kX + 1, kY - 1 }] = 0x8000;
    assert(spot(words, kX, kY) == std::pair(kX, kY - 5));                       // ring 2 is 5 out, not 2 (step 4)
    words[{ kX, kY - 5 }] = 0x2000;                                              // 0x2000 isn't in 0x9c19
    assert(spot(words, kX, kY) == std::pair(kX, kY - 5));
    assert(spot({}, kX, kY) == std::pair(kX, kY));                              // nothing there: the start itself
    std::printf("OK: corpse_spot\n");
    return 0;
}
