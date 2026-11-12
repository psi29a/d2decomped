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
//   no argument: hand cases (answers from the same oracle) and, with the game data, the
//   whole use_warp on seed 5 (SKIPs without D2_MPQ_DIR and the 1.14d patch installer).

#include <character.hpp>
#include <character_store.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;

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
    const auto landing = World::warp_arrival(level, centre(x), centre(y));
    if (!landing) return std::nullopt;
    return std::pair{ subtile(landing->first), subtile(landing->second) };
}

Words square(int x, int y, int half) {
    Words words;
    for (int row = y - half; row <= y + half; ++row)
        for (int column = x - half; column <= x + half; ++column) words.push_back({ column, row, 1 });
    return words;
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

    // Hand cases; the answers are `sk_tele_spot.py warp` (game.exe's FUN_0064e7b0 / FUN_00554ea0) on the same words.
    assert(arrival(window({}), 100, 100) == Spot(std::pair(100, 100)));                         // a free unit subtile: the unit's own
    assert(arrival(window({ { 100, 100, 1 } }), 100, 100) == Spot(std::pair(99, 99)));         // the plus of every ring-1 edge subtile is hit: the first corner
    assert(arrival(window({ { 100, 100, 1 }, { 99, 99, 1 } }), 100, 100) == Spot(std::pair(101, 99)));   // nearest_free scans (99, 101) first
    assert(arrival(window({ { 100, 100, 1 }, { 99, 99, 1 }, { 101, 99, 1 } }), 100, 100) == Spot(std::pair(99, 101)));
    assert(arrival(window({ { 100, 100, 1 }, { 100, 99, 1 }, { 100, 101, 1 }, { 99, 100, 1 }, { 101, 100, 1 } }), 100, 100) == Spot(std::pair(98, 99)));
    assert(arrival(window({ { 100, 100, 0x400 } }), 100, 100) == Spot(std::pair(99, 99)));      // an object's bit blocks too
    assert(arrival(window({ { 102, 100, 1 }, { 100, 100, 1 }, { 101, 100, 1 }, { 99, 100, 1 } }), 100, 100) == Spot(std::pair(100, 98)));
    // Rings 1 to 49 only (FUN_0064dea0 goes on while ring + 1 < 50): out to ring 49 blocked, the warp is not taken; to ring 48, the first corner of ring 49.
    assert(!arrival(window(square(100, 100, 49)), 100, 100));
    assert(arrival(window(square(100, 100, 48)), 100, 100) == Spot(std::pair(51, 51)));
    assert(arrival(window(square(100, 100, 47)), 100, 100) == Spot(std::pair(52, 52)));

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 5);
    if (!data || !data->patched) { std::printf("SKIP: the data part needs 1.14d's (D2_PATCH_INSTALLER)\n"); return 0; }
    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Warp", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Warp";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);

    // The whole use_warp on seed 5: the hero stands on the warp unit of `from`, takes the warp to `to`, and lands on the other
    // side's unit subtile + (dx, dy). The unit's subtile is blocked in all three, so nearest_free's ring order puts the hero
    // one subtile off (FUN_0064dea0's answers: the oracle on the destination's collision, tools/emu/sk_tele_spot.py, S).
    const auto take = [&](int origin, int target) {
        const Level* source = data->level(origin);
        assert(source && source->ds1.width() > 0);
        const auto warp_at = std::ranges::find_if(source->warps, [&](const Level::Warp& warp) { return warp.destination == target; });
        assert(warp_at != source->warps.end());
        const Level* destination = data->level(target);
        assert(destination && destination->ds1.width() > 0);
        const auto back = std::ranges::find_if(destination->warps, [&](const Level::Warp& other) { return other.destination == origin && other.pair == warp_at->pair; });
        assert(back != destination->warps.end());
        if (world.level != source) world.arrive(source, warp_at->unit_x, warp_at->unit_y, "test");
        world.actor->player.x = warp_at->unit_x; world.actor->player.y = warp_at->unit_y;
        world.actor->player.walking = false;
        world.actor->take_warp = int(warp_at - source->warps.begin());
        world.use_warp();
        return std::tuple{ world.level == destination, subtile(world.actor->player.x) - subtile(back->unit_x), subtile(world.actor->player.y) - subtile(back->unit_y) };
    };
    for (const auto& [origin, target, want_x, want_y] : { std::array{ 10, 5, 1, 0 }, std::array{ 15, 11, 0, 1 }, std::array{ 17, 18, 0, 1 } }) {
        const auto [reached, dx, dy] = take(origin, target);
        std::printf("warp %d to %d: hero %+d %+d from the unit\n", origin, target, dx, dy);
        assert(reached && dx == want_x && dy == want_y);
    }

    // No free spot in rings 1 to 49 round the unit: game.exe does not take the warp (FUN_00554ea0 returns 0 at 0x5551c3);
    // the hero stays on the level he is on, standing where he stood.
    {
        const Level* source = data->level(10);
        const Level* destination = data->level(5);
        std::fill(destination->walk.begin(), destination->walk.end(), std::uint16_t(1));
        if (world.level != source) world.arrive(source, source->warps.front().unit_x, source->warps.front().unit_y, "test");
        const auto warp_at = std::ranges::find_if(source->warps, [&](const Level::Warp& warp) { return warp.destination == 5; });
        assert(warp_at != source->warps.end());
        world.actor->player.x = warp_at->unit_x; world.actor->player.y = warp_at->unit_y;
        world.actor->take_warp = int(warp_at - source->warps.begin());
        world.use_warp();
        assert(world.level == source && world.actor->take_warp < 0);
        assert(world.actor->player.x == warp_at->unit_x && world.actor->player.y == warp_at->unit_y);
    }
    std::printf("OK: a warp lands on game.exe's spot and is refused when there is none\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_warp_arrival: %s\n", error.what());
    return 1;
}
