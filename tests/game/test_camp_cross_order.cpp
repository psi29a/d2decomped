// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0061b190 builds a room against every room1 already in its near list,
// including FUN_0066c370's rooms from the level next door.  Bring every
// Blood Moor room up before the Rogue Encampment and check that the camp's
// FUN_006422a0 room1 seeds include those cross-level FUN_0066e940 shares.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <spawning.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

struct Answer { int x, y; std::uint32_t seed; };

// The camp's room1 seeds after every Blood Moor room came up first (FUN_0061b190, FUN_0066e940), by
// room position; nullopt without the 1.14d data.
std::optional<std::vector<Answer>> camp_after_moor(const fs::path& data_dir, const fs::path& patch, std::uint32_t map_seed) {
    auto data = load_game_data(data_dir, patch, map_seed);
    if (!data || !data->patched) return std::nullopt;
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor);
    Spawning spawning;
    (void)populate_level(*data, spawning, *moor);
    (void)populate_level(*data, spawning, data->town);
    const auto found = spawning.levels.find(&data->town);
    assert(found != spawning.levels.end());
    const auto& state = found->second;
    std::vector<Answer> answers;
    for (std::size_t index = 0; index < state.room_seeds.size(); ++index)
        if (const auto room = level_room(data->town, index)) answers.push_back({ room->x, room->y, state.room_seeds[index].low });
    std::ranges::sort(answers, {}, [](const Answer& answer) { return std::tuple{ answer.y, answer.x }; });
    return answers;
}

}  // namespace

// Map seed 3 against game.exe's answers, or with --oracle FIRST LAST the seeds' lines for
// tools/emu/camp_cross_order.py.
int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch_path = std::getenv("D2_PATCH_INSTALLER");
    const fs::path patch = patch_path ? fs::path(patch_path) : fs::path{};
    const bool oracle = argc == 4 && std::string_view(argv[1]) == "--oracle";
    const int first = oracle ? std::atoi(argv[2]) : 3;
    const int last = oracle ? std::atoi(argv[3]) : first;
    for (int seed = first; seed <= last; ++seed) {
        const auto result = camp_after_moor(data_dir, patch, std::uint32_t(seed));
        if (!result) { std::puts("SKIP: these checks need 1.14d (set D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
        const auto& answers = *result;
        for (const auto& answer : answers) {
            if (oracle) std::printf("%d\troom1 %d,%d seed %08x\n", seed, answer.x, answer.y, answer.seed);
        }
        if (!oracle) {
            // tools/emu/camp_cross_order.py, map seed 3, Blood Moor list
            // order followed by the camp's list order.
            constexpr std::array<std::uint32_t, 35> expected = {
                0xba3a6a69, 0x7459545f, 0x16d83540, 0xd80a135b, 0x8747b48f, 0x5ddc72aa, 0x15e550de,
                0x482bf339, 0xe0b8e50d, 0x7b732daf, 0x1f572b53, 0xc1a6d5d5, 0xff44408a, 0xf3833941,
                0xa8648459, 0x8e270d24, 0xa3f1d98a, 0x6c43e411, 0xd1a0c886, 0x3497c65a, 0x9686fa89,
                0x1d4a29b0, 0x0cda794e, 0xa2f851bb, 0x743b88c9, 0x48210e89, 0x4724033c, 0xb8eb9083,
                0x70b282f1, 0x83c435e7, 0x3a3af04a, 0x03a33760, 0x71b27916, 0x7eebfea3, 0xac3a5ca4,
            };
            assert(answers.size() == expected.size());
            for (std::size_t index = 0; index < answers.size(); ++index) assert(answers[index].seed == expected[index]);
            std::printf("OK: %zu camp room1 seeds include Blood Moor rooms brought up first\n", answers.size());
        }
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_camp_cross_order: %s\n", error.what());
    return 1;
}
