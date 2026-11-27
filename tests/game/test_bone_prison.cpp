// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Bone Prison's twelve pieces (do 62), as tools/emu/bone_prison.py runs
// game.exe's FUN_005c5d00: `--bone-prison` reads its cases (`px py town spot
// nearby`, then twelve `dx dy`: the point in subtiles, 12-bit masks of the spots in
// a town room, free on their subtile, with room within 4, and where each of those
// lands off its spot) and prints each with every piece's `x,y,dir` or
// `-` (rules::bone_prison). Without arguments: the rule on a few points (no
// game data), then with the MPQs (D2_MPQ_DIR) a cast in the open: twelve pieces
// facing the point; a later piece broken takes the prison down, the first falls
// alone (MonUMod 15 partydead, FUN_005a2d10).

#include <ai.hpp>
#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
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

int prison_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int point_x = 0, point_y = 0, town = 0, spot = 0, nearby = 0;
        std::array<int, 24> offsets{};
        fields >> point_x >> point_y >> town >> spot >> nearby;
        for (int& offset : offsets) fields >> offset;
        if (!fields) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        int index = -1;                                  // a town test a piece, before its spawn
        const auto pieces = d2d::rules::bone_prison(point_x, point_y, [&](int, int) { return (town >> ++index & 1) != 0; },
                                                    [&](int x, int y) -> std::optional<std::pair<int, int>> {
                                                        if (spot >> index & 1) return std::pair{ x, y };
                                                        if (nearby >> index & 1) return std::pair{ x + offsets[std::size_t(2 * index)], y + offsets[std::size_t(2 * index + 1)] };
                                                        return std::nullopt;
                                                    });
        std::string out;
        for (const auto& piece : pieces)
            out += (out.empty() ? "" : " ") + (piece.made ? std::to_string(piece.x) + "," + std::to_string(piece.y) + "," + std::to_string(piece.dir) : std::string("-"));
        std::printf("%s | %s\n", line.c_str(), out.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--bone-prison") return prison_cases();
    // Twelve round (1000, 1000), each facing it: the first at (999, 996) faces 5, the last at (997, 997) faces 0.
    const auto here = [](int x, int y) -> std::optional<std::pair<int, int>> { return std::pair{ x, y }; };
    const auto every = d2d::rules::bone_prison(1000, 1000, [](int, int) { return false; }, here);
    assert(every[0].made && every[0].x == 999 && every[0].y == 996 && every[0].dir == 5);
    assert(every[11].made && every[11].x == 997 && every[11].y == 997 && every[11].dir == 0 && every[6].dir == 42);
    // None in a town; a spot the spawn refuses gets none and the rest still come.
    for (const auto& piece : d2d::rules::bone_prison(1000, 1000, [](int, int) { return true; }, here)) assert(!piece.made);
    const auto gap = d2d::rules::bone_prison(1000, 1000, [](int, int) { return false; }, [](int x, int y) -> std::optional<std::pair<int, int>> {
        if (x == 1003 && y == 997) return std::nullopt;                          // no room for the third
        if (x == 1004 && y == 999) return std::pair{ 1002, 1000 };              // the fourth lands nearer, facing from there
        return std::pair{ x, y };
    });
    assert(!gap[2].made && gap[1].made && gap[3].made && gap[3].x == 1002 && gap[3].dir == d2d::rules::direction64(1002, 1000, 1000, 1000));
    std::puts("OK: Bone Prison's twelve spots, none in a town or with no room, each facing the point from where it got (FUN_005c5d00)");

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: the cast in a game: no d2data.mpq (D2_MPQ_DIR)"); return 0; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: the cast in a game: needs the 1.14d data (D2_PATCH_INSTALLER)"); return 0; }
    d2d::rules::Rng rng{ 7 };
    auto made = new_character(*data, d2d::d2s::kNecromancer, "Bones", false, true, rng);
    Character character;
    character.character_class = d2d::d2s::kNecromancer;
    character.name = "Bones";
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
    const auto& prison = *data->skills.get(data->skills.by_name.at("Bone Prison"));
    world.fight.skill_level = [&](int id) { return id == prison.id ? 1 : 0; };
    world.fight.skill_base = world.fight.skill_level;
    std::uint32_t now = 40000;
    world.tick({}, now, now - 40);
    world.fight.monsters.clear();
    auto& actor = *world.actor;
    const auto cast = [&] {
        actor.pets.clear();
        const float point_x = std::floor(actor.player.x) + 6.5f, point_y = std::floor(actor.player.y) + 0.5f;
        world.fight.summon(prison, 1, point_x, point_y, now);
        assert(actor.pets.size() == 12);
        const int centre_x = int(std::floor(point_x * 5)), centre_y = int(std::floor(point_y * 5));
        for (const auto& pet : actor.pets) {
            const int x = int(std::floor(float(pet.monster.unit.x) * 5)), y = int(std::floor(float(pet.monster.unit.y) * 5));
            assert(pet.monster.unit.facing.now == d2d::rules::direction64(x, y, centre_x, centre_y));
        }
    };
    const auto standing = [&] { return std::ranges::count_if(actor.pets, [](const Fight::Pet& pet) { return pet.monster.alive(); }); };
    cast();
    world.fight.pet_hurt(actor.pets[5], 100000, now);                 // a later piece
    now += 40;
    world.tick({}, now, now - 40);
    assert(standing() == 0);
    cast();
    world.fight.pet_hurt(actor.pets[0], 100000, now);                 // the first
    now += 40;
    world.tick({}, now, now - 40);
    assert(standing() == 11);
    cast();
    actor.pets[5].until = now + 40;                                    // a later piece's time up on the next tick
    now += 40;
    world.tick({}, now, now - 40);
    assert(standing() == 0);                                           // the rest fall the same tick, not the next
    std::puts("OK: a cast's twelve pieces face the point; a later one broken or ended takes them all, the first falls alone (FUN_005a2d10)");

    // A zombie on the first spot: that piece lands within 4 of it instead (FUN_0056d940's radius 4), facing the point.
    const float point_x = std::floor(actor.player.x) + 6.5f, point_y = std::floor(actor.player.y) + 0.5f;
    const int first_x = int(std::floor(point_x * 5)) - 1, first_y = int(std::floor(point_y * 5)) - 4;
    d2d::rules::Rng make_rng{ 3 };
    world.fight.monsters.push_back(make_monster(*data, int(data->monsters.by_id.at("zombie1")), (float(first_x) + 0.5f) / 5, (float(first_y) + 0.5f) / 5, make_rng, 0));
    cast();
    const int moved_x = int(std::floor(float(actor.pets[0].monster.unit.x) * 5)), moved_y = int(std::floor(float(actor.pets[0].monster.unit.y) * 5));
    assert((moved_x != first_x || moved_y != first_y) && std::abs(moved_x - first_x) <= 4 && std::abs(moved_y - first_y) <= 4);
    std::puts("OK: a piece whose spot is taken lands within 4 of it, facing the point from there (FUN_0056d940)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_bone_prison: %s\n", error.what());
    return 1;
}
