// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00463740 (World::near_room): a spot in the room's near list holds,
// and that list (FUN_0066c370) takes the next-door level's close rooms too:
// a spot across the Blood Moor's edge, in the camp, counts; a close level
// neither's Vis names (the Black Marsh by the Monastery Gate) doesn't.
// `--near SEED LEVEL...`: each room's near list as tools/emu/near_rooms.py
// prints game.exe's (level id and rect off the level's origin, sorted), to diff.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

std::string hex(unsigned value) { char text[16]; std::snprintf(text, sizeof text, "%x", value); return text; }

// Print the selected levels' room flags and sorted near lists in the oracle's text format.
int dump_near(const fs::path& data_dir, const char* patch, std::uint32_t seed, const std::vector<int>& wanted, bool show_vis) {
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, seed);
    if (!data || !data->patched) { std::printf("SKIP: the oracle is 1.14d's: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    for (const int level_id : wanted) {
        // Its act's levels built and linked first (play builds them as the player nears: want_nearby).
        const int act = d2d::rules::level_ids::act_of(level_id);
        for (int other = 1; other < 137; ++other)
            if (d2d::rules::level_ids::act_of(other) == act) (void)data->level(other);
        data->poll_levels();
        const Level* level = data->level(level_id);
        if (!level) { std::printf("level %d: none\n", level_id); continue; }
        std::vector<std::tuple<int, int, int, int, unsigned>> rooms;   // y, x, width, height, vis flags
        for (std::size_t index = 0;; ++index) {
            const auto rect = level_room(*level, index);
            if (!rect) break;
            rooms.emplace_back(rect->y, rect->x, rect->width, rect->height, index < level->room_flags.size() ? level->room_flags[index] & 0xff0 : 0);
        }
        std::ranges::sort(rooms);
        std::printf("level %d rooms %zu\n", level_id, rooms.size());
        for (const auto& [y_pos, x_pos, width, height, vis] : rooms) {
            std::vector<std::tuple<int, int, int, int, int>> listed;
            for (const auto& [other, index] : near_rooms(*data, *level, float(x_pos) + 0.5f, float(y_pos) + 0.5f))
                if (const auto rect = level_room(*other, std::size_t(index)))
                    listed.emplace_back(other->id, other->world_x + rect->x - level->world_x, other->world_y + rect->y - level->world_y, rect->width, rect->height);
            std::ranges::sort(listed);
            std::string line = "room " + std::to_string(x_pos) + "," + std::to_string(y_pos) + " " + std::to_string(width) + "x" + std::to_string(height) + (show_vis ? " vis " + hex(vis) : std::string{}) + ":";
            for (const auto& [other_id, other_x, other_y, other_w, other_h] : listed)
                line += " " + std::to_string(other_id) + "@" + std::to_string(other_x) + "," + std::to_string(other_y) + "," + std::to_string(other_w) + "x" + std::to_string(other_h);
            std::printf("%s\n", line.c_str());
        }
    }
    return 0;
}

}  // namespace

// Check the integrated camp flags and cross-level near-list behavior.
int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (argc >= 4 && std::string_view(argv[1]) == "--near") {
        std::vector<int> wanted;
        for (int arg = 3; arg < argc; ++arg) wanted.push_back(std::atoi(argv[arg]));
        return dump_near(data_dir, patch, std::uint32_t(std::stoul(argv[2], nullptr, 0)), wanted, std::getenv("NEAR_VIS") != nullptr);
    }
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    // FUN_00667ed0 -> FUN_00667970 keeps one room-flag word for every 8x8
    // room of a preset level, including the Rogue Encampment.
    std::size_t town_rooms = 0;
    while (level_room(data->town, town_rooms)) ++town_rooms;
    assert(data->town.room_flags.size() == town_rooms);
    assert(std::ranges::all_of(data->town.room_flags, [](std::uint32_t flags) { return (flags & 0x10) != 0; }));
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    if (!moor || moor->nearby.empty()) { std::printf("SKIP: no Blood Moor linked to the camp\n"); return D2D_TEST_SKIPPED; }

    World world(&*data, -1, -1);
    world.level = moor;
    bool across = false;
    for (std::size_t index = 0; index < moor->rooms.size() && !across; ++index) {
        const auto& room = moor->rooms[index];
        const float from_x = float(room.x) + float(room.width) / 2, from_y = float(room.y) + float(room.height) / 2;
        for (const auto& rect : missile_rooms(*data, *moor, from_x, from_y)) {
            // The rect's centre in the Blood Moor's own cells.
            const float x = (float(rect[0] + rect[2] / 2) + 0.5f) / 5 - float(moor->world_x), y = (float(rect[1] + rect[3] / 2) + 0.5f) / 5 - float(moor->world_y);
            if (moor->inside(x, y)) continue;
            assert(world.near_room(from_x, from_y, x, y));
            across = true;
            break;
        }
    }
    assert(across);
    // Close isn't enough: the Black Marsh's east edge lies by the Monastery Gate, but neither's Vis names the
    // other (FUN_0066c220 walks the slots), so none of its rooms takes one of the Gate's.
    const auto* marsh = data->level(d2d::rules::level_ids::kBlackMarsh);
    const auto* gate = data->level(d2d::rules::level_ids::kMonasteryGate);
    data->poll_levels();
    if (marsh && gate && !marsh->rooms.empty()) {
        for (std::size_t index = 0; index < marsh->rooms.size(); ++index) {
            const auto& room = marsh->rooms[index];
            for (const auto& [other, other_room] : near_rooms(*data, *marsh, float(room.x) + 0.5f, float(room.y) + 0.5f))
                assert(other->id != gate->id);
        }
    }
    std::printf("OK: the camp has room flags and a next-door level's close room is in the near list\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
