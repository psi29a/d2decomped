// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// `--blizz SEED N`: Blizzard through the REAL Fight:
// N casts of Blizzard (a real Fight::fire at a point, the centre stepped 100
// frames by Fight::missile_tick) on an empty open level; one line per shard
// `S<case> <frame> <dx> <dy>`: the subtile offset of the shard from the
// centre's subtile. game.exe's are -6..5 at radius 7 (FUN_005a9820, run by
// tools/emu/sk_ground_blizz.py game; `d2d` compares).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace d2d::game;

int main(int argc, char** argv) try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode != "--blizz") { std::printf("SKIP: an oracle mode, run `test_sk_ground --blizz SEED N` (see tools/emu/sk_ground_blizz.py)\n"); return D2D_TEST_SKIPPED; }
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
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    int at_x = -1, at_y = -1;
    for (int y = 20; y < 400 && at_x < 0; ++y)
        for (int x = 20; x < 400 && at_x < 0; ++x)
            if (std::ranges::all_of(std::array<std::pair<int, int>, 5>{ { { 0, 0 }, { 8, 0 }, { -8, 0 }, { 0, 8 }, { 0, -8 } } },
                                    [&](auto off) { return !missile_rooms(*data, open, float(x + off.first) + 0.5f, float(y + off.second) + 0.5f).empty(); }))
                at_x = x * 5 + 2, at_y = y * 5 + 2;
    assert(at_x >= 0);
    world.actor->player.x = (float(at_x) + 0.5f) / 5;
    world.actor->player.y = (float(at_y) + 0.5f) / 5;
    const Crowd crowd;
    std::uint32_t now = 1000 * kTickMs;
    const int blizzard = data->skills.by_name.at("Blizzard");
    const auto& skill = *data->skills.get(blizzard);

    {
        std::uint32_t state = std::uint32_t(argc > 2 ? std::atoi(argv[2]) : 1);
        if (state == 0) state = 1;
        const auto next = [&state] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; };
        const int cases = argc > 3 ? std::atoi(argv[3]) : 100;
        for (std::uint32_t skip = (state * 2654435761u) % 9973; skip > 0; --skip) world.fight.rng(2);   // the fight's own generator: another stream per SEED
        long shards = 0;
        for (int i = 0; i < cases; ++i) {
            world.fight.monsters.clear(); world.fight.missiles.clear(); world.fight.pending.clear();
            world.fight.missile_frame = -1;
            const int cell_x = at_x + int(next() % 41) - 20, cell_y = at_y + int(next() % 41) - 20;   // the centre's subtile
            Fight::Caster caster;
            caster.x = world.actor->player.x; caster.y = world.actor->player.y; caster.level = 1 + int(next() % 20);
            caster.to_x = (float(cell_x) + 0.5f) / 5; caster.to_y = (float(cell_y) + 0.5f) / 5;
            world.fight.fire(skill, now, caster);
            for (int frame = 1; frame <= 110; ++frame) {
                now += kTickMs;
                world.fight.world(true, now, 0.04f, crowd);
                for (const auto& missile : world.fight.missiles)
                    if (missile.info && missile.info->name == "blizzard1" && missile.born == now) {
                        ++shards;
                        std::printf("S%d %d %d %d\n", i, frame, int(std::floor(missile.x * 5)) - cell_x, int(std::floor(missile.y * 5)) - cell_y);
                    }
            }
            now += 100 * kTickMs;
        }
        std::fprintf(stderr, "casts %d shards %ld\n", cases, shards);
        return 0;
    }
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
