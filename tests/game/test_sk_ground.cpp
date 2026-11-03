// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// With no argument: a standing Blizzard shard (Size 2) and Fire Wall flame
// (Size 1) hit a monster of size 1 / 2 / 3 on 5 / 13 / 21 and 1 / 5 / 9
// subtiles (FUN_005ae1f0 -> FUN_00641cb0, run natively by sk_ground_hit.py),
// and a Fire Ball or Glacial Spike burst rolls once for every monster in it
// (FUN_005a9a70 / FUN_005aa8b0, sk_ground_roll.py). The oracle modes:
// `--blizz SEED N`: Blizzard through the REAL Fight:
// N casts of Blizzard (a real Fight::fire at a point, the centre stepped 100
// frames by Fight::missile_tick) on an empty open level; one line per shard
// `S<case> <frame> <dx> <dy>`: the subtile offset of the shard from the
// centre's subtile. game.exe's are -6..5 at radius 7 (FUN_005a9820, run by
// tools/emu/sk_ground_blizz.py game; `d2d` compares).
// `--hit ROW FRAMES PER`: one standing missile (ROW: blizzard1, a Size 2
// shard, or firewall, Size 1) and one hostile monster of a class of each size
// 1..3 (the first PER MonStats rows of each size) at every offset -7..7
// subtiles; the monster has a million life and the missile deals a fixed 10
// points, so the life lost / 10 over FRAMES frames is its hits. One line
// `H <size> <class> <dx> <dy> <hits> <moved>` (moved 1: the monster walked off
// its subtile; game.exe's: tools/emu/sk_ground_hit.py).
// `--burst-roll fireball|glacialspike TRIALS`: three hostile monsters of one
// class at (0,0), (2,1), (-1,2) subtiles in a burst's radius, the skill's own
// damage at level 5; per trial `R <trial> <life lost in 256ths>` x3 (game.exe:
// tools/emu/sk_ground_roll.py, one record for the burst).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
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
#include <cstddef>
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
    if (!mode.empty() && mode != "--blizz" && mode != "--hit" && mode != "--burst-roll") {
        std::printf("SKIP: modes are none, `--blizz SEED N`, `--hit ROW FRAMES PER` and `--burst-roll NAME TRIALS` (see tools/emu/sk_ground_*.py)\n");
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
    const auto reset = [&] {
        world.fight.monsters.clear(); world.fight.missiles.clear(); world.fight.pending.clear();
        world.fight.missile_frame = -1;
    };
    // A million-life monster of MonStats row `row` on a subtile, hurt by nothing but the missile's fixed damage.
    const auto place = [&](std::size_t row, int sub_x, int sub_y) {
        d2d::rules::Rng spawn_rng{ 5 };
        Monster monster = make_monster(*data, int(row), (float(sub_x) + 0.5f) / 5, (float(sub_y) + 0.5f) / 5, spawn_rng, 0);
        monster.stats.hit_points = monster.hit_points = 1000000;
        monster.life_frac = 0;
        monster.next_act = ~0u;
        world.fight.add_monster(std::move(monster));
        auto& placed = world.fight.monsters.back();
        placed.boss_res = {};
        const auto res = world.fight.target_of(world.fight.monsters.size() - 1).res;
        for (std::size_t k = 0; k < placed.boss_res.size() && k < res.size(); ++k) placed.boss_res[k] = -res[k];
    };

    // The hits a standing missile of ROW lands in FRAMES frames on a monster of MonStats row `row` standing (dx, dy) subtiles from it,
    // and whether the monster left its subtile meanwhile (some classes walk on their own whatever next_act says: no standing target).
    struct Hit { int hits; bool moved; };
    const auto hits_at = [&](const std::string& row_name, std::size_t row, int dx, int dy, int frames) {
        const auto& missile_row = data->missiles.at(row_name);
        const auto& hit_skill = *data->skills.get(data->skills.by_name.at(row_name == "firewall" ? "Fire Wall" : "Blizzard"));
        reset();
        place(row, at_x + dx, at_y + dy);
        world.fight.launch(missile_row, hit_skill, 1, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0, 0, std::max(frames, 1), now);
        for (auto& missile : world.fight.pending) missile.fixed = 10 << 8;
        int hits = 0;
        bool moved = false;
        const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
        for (int frame = 1; frame <= frames; ++frame) {
            now += kTickMs;
            const int before = world.fight.monsters[0].hit_points;
            world.fight.monsters[0].next_act = ~0u;
            world.fight.world(true, now, 0.04f, crowd);
            hits += (before - world.fight.monsters[0].hit_points) / 10;
            moved = moved || subtile(world.fight.monsters[0].unit.x) != at_x + dx || subtile(world.fight.monsters[0].unit.y) != at_y + dy;
        }
        now += 20 * kTickMs;
        return Hit{ hits, moved };
    };
    // What each of three monsters of one size-2 class lost (256ths) to one Fire Ball (or Glacial Spike) bursting over them.
    const auto burst_losses = [&](const std::string& name, std::size_t row) {
        const auto& burst_skill = *data->skills.get(data->skills.by_name.at(name == "fireball" ? "Fire Ball" : "Glacial Spike"));
        const auto& missile_row = data->missiles.at(name);
        const auto life = [&](std::size_t index) { return world.fight.monsters[index].hit_points * 256 - world.fight.monsters[index].life_frac; };
        reset();
        for (const auto& off : std::array<std::pair<int, int>, 3>{ { { 0, 0 }, { 2, 1 }, { -1, 2 } } }) place(row, at_x + off.first, at_y + off.second);
        const std::array<int, 3> before{ life(0), life(1), life(2) };
        world.fight.launch(missile_row, burst_skill, 5, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0, 0, 1, now);
        for (int frame = 1; frame <= 3; ++frame) {
            now += kTickMs;
            for (auto& monster : world.fight.monsters) monster.next_act = ~0u;
            world.fight.world(true, now, 0.04f, crowd);
        }
        now += 20 * kTickMs;
        return std::array<int, 3>{ before[0] - life(0), before[1] - life(1), before[2] - life(2) };
    };
    // One class of each size (MonStats maggotegg1, fallen1, bighead1: the ones sk_ground_hit.py ran first).
    std::array<std::size_t, 4> first_of{};
    for (const auto& [size, id] : { std::pair{ 1, "maggotegg1" }, std::pair{ 2, "fallen1" }, std::pair{ 3, "bighead1" } }) {
        first_of[std::size_t(size)] = std::size_t(data->monsters.by_id.at(id));
        assert(data->monsters.types[first_of[std::size_t(size)]].size == size);
    }

    if (mode.empty()) {
        for (const std::string row_name : { "blizzard1", "firewall" })
            for (int size = 1; size <= 3; ++size) {
                int area = 0;
                for (int dx = -7; dx <= 7; ++dx)
                    for (int dy = -7; dy <= 7; ++dy) area += hits_at(row_name, first_of[std::size_t(size)], dx, dy, 3).hits > 0;
                const int want = row_name == "blizzard1" ? std::array{ 5, 13, 21 }[std::size_t(size - 1)] : std::array{ 1, 5, 9 }[std::size_t(size - 1)];
                std::printf("%s hits a size %d monster on %d subtiles (game.exe %d)\n", row_name.c_str(), size, area, want);
                assert(area == want);
            }
        std::printf("OK: a standing missile hits by the footprint of its Size against the monster's\n");
        for (const std::string name : { "fireball", "glacialspike" })
            for (int trial = 0; trial < 50; ++trial) {
                const auto lost = burst_losses(name, first_of[2]);
                assert(lost[0] > 0 && lost[0] == lost[1] && lost[1] == lost[2]);
            }
        std::printf("OK: a Fire Ball and a Glacial Spike burst roll once for every monster in it\n");
        return 0;
    }

    if (mode == "--hit") {
        const std::string row_name = argc > 2 ? argv[2] : "blizzard1";
        const int frames = argc > 3 ? std::atoi(argv[3]) : 9;
        const int per = argc > 4 ? std::atoi(argv[4]) : 2;
        std::array<int, 4> taken{};
        for (std::size_t row = 0; row < data->monsters.types.size(); ++row) {
            const int size = data->monsters.types[row].size;
            if (size < 1 || size > 3 || taken[std::size_t(size)] >= per) continue;
            ++taken[std::size_t(size)];
            for (int dx = -7; dx <= 7; ++dx)
                for (int dy = -7; dy <= 7; ++dy) {
                    const auto [hits, moved] = hits_at(row_name, row, dx, dy, frames);
                    std::printf("H %d %zu %d %d %d %d\n", size, row, dx, dy, hits, moved ? 1 : 0);
                }
        }
        return 0;
    }

    if (mode == "--burst-roll") {
        const std::string name = argc > 2 ? argv[2] : "fireball";
        const int trials = argc > 3 ? std::atoi(argv[3]) : 100;
        for (int trial = 0; trial < trials; ++trial) {
            const auto lost = burst_losses(name, first_of[2]);
            assert(lost[0] > 0 && lost[1] > 0 && lost[2] > 0);   // a zero loss would pass for an equal roll in cmp
            std::printf("R %d %d %d %d\n", trial, lost[0], lost[1], lost[2]);
        }
        return 0;
    }

    if (mode == "--blizz") {
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
