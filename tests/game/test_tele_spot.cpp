// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--tele-spot CASES`: the player's Teleport destination (srvdofunc 27,
// FUN_005ca360) through the REAL Fight::spot on a Level, the cases of
// tools/emu/sk_tele_spot.py: `id tele caster_x caster_y target_x target_y n {x y bits}...`, subtiles of
// a 5 x 5 block of 8 x 8 tile rooms (the caster in the centre room), the
// level's Teleport column, the non-zero collision words. One line each,
// `id did x y`: whether the player was put somewhere and the subtile (`- -`
// when it stays). game.exe's: `python sk_tele_spot.py game CASES`, with the
// cases made by `gen N SEED --no-units` (d2d keeps units in the Crowd, so the
// units' bit 0x1000 is left out).
// With no argument: a target outside the caster's room and its near list is
// refused, one inside lands, a blocked one lands on the nearest free spot.

#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <outdoor.hpp>
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
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

constexpr int kRoom = 40, kGrid = 5, kSide = kRoom * kGrid;
constexpr int kOffset = 40;                                // the block's subtile in the level: whole 8-tile rooms from the corner

}  // namespace

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (!mode.empty() && mode != "--tele-spot") {
        std::printf("SKIP: modes are none and `--tele-spot CASES` (see tools/emu/sk_tele_spot.py)\n");
        return D2D_TEST_SKIPPED;
    }
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1 && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    // The field: Blood Moor's size with 25 rooms of 8 x 8 tiles in a block, walled all round it.
    Level field = *data->level(d2d::rules::level_ids::kBloodMoor);
    field.nearby.clear();
    field.warps.clear();
    field.rooms.clear();
    for (int row = 0; row < kGrid; ++row)
        for (int column = 0; column < kGrid; ++column) {
            d2d::drlg::Outdoor::RoomSeed room;
            room.x = kOffset / 5 + column * 8;
            room.y = kOffset / 5 + row * 8;
            field.rooms.push_back(room);
        }
    const int width = field.ds1.width() * 5, height = field.ds1.height() * 5;
    assert(width >= kOffset + kSide && height >= kOffset + kSide);
    world.level = &field;
    world.fight.enter(&field);
    world.fight.skill_level = [](int) { return 1; };
    const auto& teleport = *data->skills.get(data->skills.by_name.at("Teleport"));
    std::uint32_t now = 1000 * kTickMs;

    const auto place = [&](int tele, int caster_x, int caster_y, int target_x, int target_y, const std::vector<std::array<int, 3>>& words) {
        field.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(1));
        for (int y = 0; y < kSide; ++y)
            for (int x = 0; x < kSide; ++x) field.walk[std::size_t(kOffset + y) * std::size_t(width) + std::size_t(kOffset + x)] = 0;
        for (const auto& word : words)
            if (word[0] >= 0 && word[1] >= 0 && word[0] < kSide && word[1] < kSide)
                field.walk[std::size_t(kOffset + word[1]) * std::size_t(width) + std::size_t(kOffset + word[0])] |= std::uint16_t(word[2]);
        field.teleport = tele;
        auto* actor = world.actor;
        actor->player.x = (float(kOffset + caster_x) + 0.5f) / 5;
        actor->player.y = (float(kOffset + caster_y) + 0.5f) / 5;
        actor->cast_x = (float(kOffset + target_x) + 0.5f) / 5;
        actor->cast_y = (float(kOffset + target_y) + 0.5f) / 5;
        actor->attack_mon = -1;
        actor->reassigned = false;
        now += 100 * kTickMs;
        world.fight.spot(teleport, now, world.fight.player_caster(teleport.id));
        return actor->reassigned;
    };

    if (mode.empty()) {
        assert(place(1, 100, 100, 130, 100, {}));          // 30 subtiles east: the next room, near
        assert(world.actor->player.x * 5 > float(kOffset + 129));
        assert(!place(1, 100, 100, 160, 100, {}));         // the room after it: not near
        assert(!place(1, 100, 100, 100, 199, {}));
        assert(!place(0, 100, 100, 110, 100, {}));         // the level's Teleport 0
        // A blocked target lands on the nearest free spot (FUN_0064e7b0): the first ring's corner, then the least |dx| + |dy| of a ring.
        const auto landed = [&] { return std::pair{ int(std::floor(world.actor->player.x * 5)) - kOffset, int(std::floor(world.actor->player.y * 5)) - kOffset }; };
        assert(place(1, 100, 100, 130, 100, { { 130, 100, 1 } }) && landed() == std::pair(129, 99));
        assert(place(1, 100, 100, 130, 100, { { 130, 101, 1 } }) && landed() == std::pair(129, 100));       // the plus of (130, 100) is hit: left column first
        assert(place(1, 100, 100, 130, 100, { { 129, 100, 1 }, { 131, 100, 1 }, { 130, 99, 1 }, { 130, 101, 1 } }) && landed() == std::pair(128, 99));
        // The search stops at ring 49 (FUN_0064dea0: on only while ring + 1 < 50): a square of blocked subtiles out to ring 49 leaves only ring 50, refused; out to ring 48, the first corner of ring 49 is taken.
        const auto square = [](int half) {
            std::vector<std::array<int, 3>> words;
            for (int y = 100 - half; y <= 100 + half; ++y)
                for (int x = 130 - half; x <= 130 + half; ++x) words.push_back({ x, y, 1 });
            return words;
        };
        assert(!place(1, 100, 100, 130, 100, square(49)));
        assert(place(1, 100, 100, 130, 100, square(48)) && landed() == std::pair(81, 51));
        std::printf("test_tele_spot: ok\n");
        return 0;
    }

    if (argc < 3) { std::printf("FAIL: --tele-spot needs a CASES file\n"); return 1; }
    std::ifstream cases(argv[2]);
    if (!cases) { std::printf("FAIL: cannot open %s\n", argv[2]); return 1; }
    std::string line;
    while (std::getline(cases, line)) {
        std::istringstream fields(line);
        int id = 0, tele = 0, caster_x = 0, caster_y = 0, target_x = 0, target_y = 0, count = 0;
        fields >> id >> tele >> caster_x >> caster_y >> target_x >> target_y >> count;
        std::vector<std::array<int, 3>> words(std::size_t(std::max(count, 0)));
        for (auto& word : words) fields >> word[0] >> word[1] >> word[2];
        if (place(tele, caster_x, caster_y, target_x, target_y, words))
            std::printf("%d 1 %d %d\n", id, int(std::floor(world.actor->player.x * 5)) - kOffset, int(std::floor(world.actor->player.y * 5)) - kOffset);
        else std::printf("%d 0 - -\n", id);
    }
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
