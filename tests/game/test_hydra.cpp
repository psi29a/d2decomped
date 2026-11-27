// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Hydra's three heads (do 144), as tools/emu/hydra.py runs game.exe's
// FUN_005ca910: `--hydra` reads its cases (`px py town spot nearby`, then three
// `dx dy`: the point in subtiles, whether it's in a town, 3-bit masks of the
// heads free on their subtile and with room within 4, and where each of those
// lands off its spot) and prints each with every head's `row,x,y` (its row less
// the summon row) or `-` (rules::hydra). Without arguments: the rule on a
// point (no game data), then with the MPQs (D2_MPQ_DIR) a cast in the open:
// hydra1, hydra2 and hydra3 at their spots (Fight::summon).

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int hydra_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int point_x = 0, point_y = 0, town = 0, spot = 0, nearby = 0;
        std::array<int, 6> offsets{};
        fields >> point_x >> point_y >> town >> spot >> nearby;
        for (int& offset : offsets) fields >> offset;
        if (!fields) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        std::array<int, 3> rows{};
        const auto heads = d2d::rules::hydra(point_x, point_y, town != 0, [&](int head, int x, int y) -> std::optional<std::pair<int, int>> {
            rows[std::size_t(head)] = head;
            if (spot >> head & 1) return std::pair{ x, y };
            if (nearby >> head & 1) return std::pair{ x + offsets[std::size_t(2 * head)], y + offsets[std::size_t(2 * head + 1)] };
            return std::nullopt;
        });
        std::string out;
        for (std::size_t k = 0; k < heads.size(); ++k)
            out += (k ? " " : "") + (heads[k].made ? std::to_string(rows[k]) + "," + std::to_string(heads[k].x) + "," + std::to_string(heads[k].y) : std::string("-"));
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--hydra") return hydra_cases();
    // Round (1000, 1000): (999, 999), (1000, 1000), (1001, 999); none in a town; a head with no room is left out.
    const auto here = [](int, int x, int y) -> std::optional<std::pair<int, int>> { return std::pair{ x, y }; };
    const auto heads = d2d::rules::hydra(1000, 1000, false, here);
    assert(heads[0].made && heads[0].x == 999 && heads[0].y == 999 && heads[1].x == 1000 && heads[1].y == 1000 && heads[2].x == 1001 && heads[2].y == 999);
    for (const auto& head : d2d::rules::hydra(1000, 1000, true, here)) assert(!head.made);
    const auto two = d2d::rules::hydra(1000, 1000, false, [](int head, int x, int y) -> std::optional<std::pair<int, int>> {
        if (head == 1) return std::nullopt;
        return std::pair{ x, y };
    });
    assert(two[0].made && !two[1].made && two[2].made);
    std::puts("OK: Hydra's three heads a subtile apart round the point, none in a town (FUN_005ca910)");

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: the cast in a game: no d2data.mpq (D2_MPQ_DIR)"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: the cast in a game: needs the 1.14d data (D2_PATCH_INSTALLER)"); return 0; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kSorceress, "Heads", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kSorceress;
    character.name = "Heads";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    const auto& hydra = *data->skills.get(data->skills.by_name.at("Hydra"));
    world.fight.skill_level = [&](int id) { return id == hydra.id ? 1 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    const float point_x = std::floor(actor.player.x) + 6.5f, point_y = std::floor(actor.player.y) + 0.5f;
    world.fight.summon(hydra, 1, point_x, point_y, now);
    assert(actor.pets.size() == 3);
    const int centre_x = int(std::floor(point_x * 5)), centre_y = int(std::floor(point_y * 5));
    const auto expect = d2d::rules::hydra(centre_x, centre_y, false, here);
    for (std::size_t k = 0; k < 3; ++k) {
        const auto& head = actor.pets[k].monster;
        assert(head.type == int(data->monsters.by_id.at("hydra" + std::to_string(k + 1))));
        assert(int(std::floor(float(head.unit.x) * 5)) == expect[k].x && int(std::floor(float(head.unit.y) * 5)) == expect[k].y);
    }
    std::puts("OK: a cast makes hydra1, hydra2 and hydra3 at their spots (Fight::summon)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_hydra: %s\n", error.what());
    return 1;
}
