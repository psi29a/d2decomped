// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// When a clicked warp is taken: FUN_00548b00 asks the unit distance when the click comes and when the walk ends (FUN_00580c20,
// LAB_00580e73), not on the way (collision.md "The server's pending interact"). From 3 cells off each of Blood Moor's warps in 8
// directions, the hero passes into reach (World::warp_close) still walking and stays on the level till the walk ends; from in
// reach the click takes the warp in its own tick (the client's 0x13 with no walk, LAB_00462010). Needs the data (SKIPs without
// D2_MPQ_DIR and the 1.14d patch installer).

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <stdexcept>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

constexpr int kSorceress = 1;

struct Walk {
    bool taken = false;
    int ticks = 0;         // the ticks run, the one that took the warp too
    int reach_ticks = 0;   // ticks that ended in reach of the warp with the hero still walking (and still on the level)
    bool early = false;    // the level changed in a tick that began with the walk still going and its path not at its end
};

// The hero `dist` cells off the warp's tile unit along (dx, dy), a click on the middle of its Select box, then up to 600 ticks.
Walk walk(World& world, std::uint32_t& now, const Level& level, std::size_t index, float dx, float dy, float dist) {
    const auto& warp = level.warps[index];
    const int unit_x = int(std::floor(warp.unit_x * 5)), unit_y = int(std::floor(warp.unit_y * 5));
    const float sel_x = float(warp.select[0]) + float(warp.select[2]) / 2, sel_y = float(warp.select[1]) + float(warp.select[3]) / 2;
    const float diff = (sel_x + float((unit_x - unit_y) * 16)) / 80, sum = (sel_y + float((unit_x + unit_y) * 8)) / 40;
    const float start_x = warp.unit_x + dist * dx, start_y = warp.unit_y + dist * dy;
    Walk result;
    if (level.unit_blocked(start_x, start_y)) return result;
    world.arrive(&level, start_x, start_y, "test");
    for (int warm = 0; warm < 3; ++warm) { world.tick({}, now + kTickMs, now); now += kTickMs; }
    world.fight.monsters.clear();
    world.actor->player.x = start_x; world.actor->player.y = start_y;
    world.actor->player.walking = false; world.actor->player.path.clear();
    world.apply(cmd::Move{ (sum + diff) / 2, (sum - diff) / 2, true }, now);
    for (int tick = 0; tick < 600 && world.level == &level; ++tick) {
        const bool far_from_end = world.actor->player.walking && world.actor->player.path.size() > 1;
        ++result.ticks;
        world.tick({}, now + kTickMs, now); now += kTickMs; world.fight.monsters.clear();
        if (world.level != &level) { result.taken = true; result.early = far_from_end; break; }
        if (world.actor->player.walking && World::warp_close(warp, world.actor->player.x, world.actor->player.y)) ++result.reach_ticks;
    }
    return result;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 1);
    if (!data || !data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    for (int id = 1; id <= 136; ++id) (void)data->level(id);
    d2d::rules::Rng rng(7);
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
    std::uint32_t now = 1000 * kTickMs;
    const Level* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    if (!moor || moor->warps.empty()) throw std::runtime_error("no Blood Moor warps");
    static const int dx8[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dy8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    int starts = 0, taken = 0, walked_in_reach = 0, at_once = 0, in_reach = 0;
    for (std::size_t index = 0; index < moor->warps.size(); ++index) {
        for (int dir = 0; dir < 8; ++dir) {
            const auto away = walk(world, now, *moor, index, float(dx8[dir]), float(dy8[dir]), 3.f);
            if (!away.taken && away.reach_ticks == 0) continue;   // blocked start, or a walk that never came near
            ++starts; taken += away.taken ? 1 : 0;
            if (away.reach_ticks > 0) ++walked_in_reach;
            if (away.early) std::printf("FAIL warp %zu dir %d: taken with its walk still going\n", index, dir);
            if (away.early) return 1;
            // Within reach before the click (0.4 cells, unit distance under 5): taken in the click's tick.
            const float near_x = moor->warps[index].unit_x + 0.4f * float(dx8[dir]), near_y = moor->warps[index].unit_y + 0.4f * float(dy8[dir]);
            if (moor->unit_blocked(near_x, near_y) || !World::warp_close(moor->warps[index], near_x, near_y)) continue;
            ++in_reach;
            const auto close_by = walk(world, now, *moor, index, float(dx8[dir]), float(dy8[dir]), 0.4f);
            if (close_by.taken && close_by.ticks == 1) ++at_once;
            else std::printf("FAIL warp %zu dir %d: in reach, the click did not take the warp in its tick\n", index, dir);
        }
    }
    std::printf("%d starts 3 cells off: %d taken, %d walked on in reach; %d of %d in reach taken in the click's tick\n", starts, taken, walked_in_reach, at_once, in_reach);
    const bool ok = starts > 0 && walked_in_reach > 0 && at_once == in_reach;
    std::printf("%s\n", ok ? "OK: a warp is taken when the walk ends, or at once in reach" : "FAIL");
    return ok ? 0 : 1;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_warp_walk_end: %s\n", error.what());
    return 1;
}
