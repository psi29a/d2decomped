// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A clicked warp: how is the walk to it planned, and is the warp taken? The hero starts 2.5 cells off the warp's tile unit
// in each of 8 directions (other distances and directions: --dists, --dirs), a click at the centre of LvlWarp's Select box
// (World::warp_clicked) goes through the real World::apply and tick; the warp is taken when the level changes within 600 ticks
// (no monsters: cleared every tick).
//   no argument: the starts of kCases, each against game.exe's own answer (tools/emu/warp_walk_path.py run on that start): the path
//   World::tick plans for the click and whether the warp is taken. Fails when a clicked warp is planned as a walk to a spot.
//   `--sweep SEEDS [options]`: for every level with warps and every warp, one start per direction: one line each
//   `W seed level warp dest dir result ticks unit_blocked` (result: 1 taken, 0 not taken, -1 start blocked or off the map), then `S taken total blocked`.
//   `--dump SEEDS FILE [options]`: the sweep plus the oracle's inputs and d2d's plans (tools/emu/warp_walk_path.py)
//   options: `--levels MIN MAX` (default 1 136), `--dists C1,C2,..` (start distances in cells, default 2.5),
//   `--dirs N` (default 8: the eight neighbours; any other N: N angles round the unit)
// Needs the data (SKIPs without D2_MPQ_DIR and the 1.14d patch installer).

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

struct Options {
    int seeds = 1;
    std::FILE* out = stdout;
    bool dump = false;
    int level_min = 1, level_max = 136, dirs = 8;
    std::vector<float> dists{ 2.5f };
};

constexpr int kSorceress = 1;
constexpr int kWindow = 96;   // the dump's blocked bits: a square of 2 * kWindow + 1 subtiles round the tile unit
using Points = std::vector<std::pair<int, int>>;

Points subtiles(const std::vector<std::pair<float, float>>& path) {
    Points points;
    for (const auto& point : path) points.emplace_back(int(std::floor(point.first * 5)), int(std::floor(point.second * 5)));
    return points;
}

void print_points(std::FILE* out, const Points& points) {
    std::fprintf(out, " %zu", points.size());
    for (const auto& point : points) std::fprintf(out, " %d,%d", point.first, point.second);
}

// Starts whose answer game.exe gave (FUN_00649970's first path, then the walk's frames and the unit distance: tools/emu/warp_walk_path.py
// game, on d2d's bits at each start; data seed 1, 2.5 cells off the tile unit, direction `dir` of the table in main). `plan` is that first
// path in subtiles; `taken` is whether the warp is taken after the walk and the server's one more walk.
// The first six are starts where the plan made as a walk to a spot differs from the unit walk (3 or 4 points where game.exe has 1, or a
// different route); in the first four d2d took the warp where game.exe's hero stops short of it.
struct Case { int level, warp, dir; bool taken; const char* plan; };
constexpr Case kCases[] = {
    { 17, 0, 3, false, "102,182 102,182 103,182" },
    { 76, 0, 3, false, "233,716" },
    { 113, 0, 7, false, "298,276" },
    { 118, 2, 7, false, "58,436" },
    { 46, 2, 1, true, "31,128" },
    { 52, 0, 2, true, "132,139" },
    { 2, 0, 0, true, "117,139 112,144 110,144 106,140" },        // not a case the plan decides: a longer route and a warp that is taken
    { 6, 1, 6, false, "131,252 129,252 126,255 126,260" },       // the same, and a warp that is not taken
};

Points parse_plan(const char* text) {
    Points points;
    for (const char* at = text; *at;) { int x = 0, y = 0, used = 0; if (std::sscanf(at, "%d,%d%n", &x, &y, &used) != 2) break; points.emplace_back(x, y); at += used; while (*at == ' ') ++at; }
    return points;
}

// The tick's path after its first tick must be game.exe's path: the walker passes no point in one tick (every start of both sweeps), and an
// empty path is nothing to compare, not a match.
bool plan_matches(const Points& got, const Points& expected) { return !got.empty() && got == expected; }

struct Start {
    int result = 0;       // 1 taken, 0 not taken, -1 start blocked
    int ticks = 0;
    Points plan;          // the path World::tick made for the click, as it stands after the first tick (the walker may have passed its first point)
    bool unit_blocked = false;
};

// One start: the hero `dist` cells from the warp's tile unit along (dx, dy), a click on the warp's Select box.
// `dump` adds the oracle's inputs (`G`), d2d's own plans (`D`, `F`, `E`) and the tick's plan (`T`).
Start run_start(World& world, int walk_velocity, std::uint32_t& now, const Level& level, std::size_t index, int seed, int id, int key, float dx, float dy, float dist, std::FILE* dump) {
    const auto& warp = level.warps[index];
    const int unit_x = int(std::floor(warp.unit_x * 5)), unit_y = int(std::floor(warp.unit_y * 5));
    const float sel_x = float(warp.select[0]) + float(warp.select[2]) / 2, sel_y = float(warp.select[1]) + float(warp.select[3]) / 2;
    const float diff = (sel_x + float((unit_x - unit_y) * 16)) / 80, sum = (sel_y + float((unit_x + unit_y) * 8)) / 40;
    const float click_x = (sum + diff) / 2, click_y = (sum - diff) / 2;
    Start start;
    start.unit_blocked = level.unit_blocked(warp.unit_x, warp.unit_y);
    const float start_x = warp.unit_x + dist * dx, start_y = warp.unit_y + dist * dy;
    const bool outside = start_x < 0 || start_y < 0 || start_x >= float(level.ds1.width()) || start_y >= float(level.ds1.height());   // a warp near the map's edge: the start is off the map
    if (outside || level.unit_blocked(start_x, start_y)) { start.result = -1; return start; }
    world.arrive(&level, start_x, start_y, "test");
    for (int warm = 0; warm < 3; ++warm) { world.tick({}, now + kTickMs, now); now += kTickMs; }   // a level's first tick fills its rooms' walk flags
    world.fight.monsters.clear();
    world.actor->player.x = start_x; world.actor->player.y = start_y;
    world.actor->player.walking = false; world.actor->player.path.clear();
    if (dump) {
        {   // `G seed level warp dir unit_sub_x unit_sub_y walk_velocity hex`: blocked bits of the 193 x 193 subtiles round the unit (row per y, 4 per hex digit),
            // taken as this start's world sees them (a level's walk flags fill room by room), as a player's walk asks them
            const auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
            std::fprintf(dump, "G %d %d %zu %d %d %d %d ", seed, id, index, key, unit_x, unit_y, walk_velocity);
            int nibble = 0, count = 0;
            for (int gy = unit_y - kWindow; gy <= unit_y + kWindow; ++gy)
                for (int gx = unit_x - kWindow; gx <= unit_x + kWindow; ++gx) {
                    nibble = nibble * 2 + (level.unit_blocked(centre(gx), centre(gy), world.actor->player.shape) ? 1 : 0);
                    if (++count % 4 == 0) { std::fputc("0123456789abcdef"[nibble], dump); nibble = 0; }
                }
            if (count % 4) std::fputc("0123456789abcdef"[nibble << (4 - count % 4)], dump);
            std::fputc('\n', dump);
        }
        // `D|F seed level warp dir start_x16.16 start_y16.16 npoints x,y...`: a player_walk to the tile unit before anything ticks, from where the tick
        // reads the hero (its 16.16 place, as a float): D as a walk to a unit, F as a walk to a spot
        const Crowd none;
        const float here_x = world.actor->player.x, here_y = world.actor->player.y;
        const auto plan = [&](const char* tag, bool to_unit) {
            std::fprintf(dump, "%s %d %d %zu %d %lld %lld", tag, seed, id, index, key, static_cast<long long>(world.actor->player.x.raw), static_cast<long long>(world.actor->player.y.raw));
            print_points(dump, subtiles(player_walk(level, here_x, here_y, warp.unit_x, warp.unit_y, to_unit, none, &world.actor->player, nullptr)));
            std::fputc('\n', dump);
        };
        plan("D", true);
        plan("F", false);
        {   // `E seed level warp dir T n pts S n pts P n pts`: rules::toward_path, search_path and player_path of the same bits, no walk-back (to tell a pather deviation from the wrapper's)
            const auto sub = [](float value) { return int(std::floor(value * 5)); };
            const auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
            const auto blocked = [&](int at_x, int at_y) { return level.unit_blocked(centre(at_x), centre(at_y), world.actor->player.shape); };
            const int from_x = sub(here_x), from_y = sub(here_y), to_x = sub(warp.unit_x), to_y = sub(warp.unit_y);
            std::fprintf(dump, "E %d %d %zu %d", seed, id, index, key);
            const auto print = [&](const char* name, const Points& points) { std::fprintf(dump, " %s", name); print_points(dump, points); };
            print("T", d2d::rules::toward_path(from_x, from_y, to_x, to_y, 0x49, 1, blocked));
            print("S", d2d::rules::search_path(from_x, from_y, to_x, to_y, true, blocked));
            print("P", d2d::rules::player_path(from_x, from_y, to_x, to_y, 1, true, blocked));
            std::fputc('\n', dump);
        }
    }
    world.apply(cmd::Move{ click_x, click_y, true }, now);
    for (; start.ticks < 600 && world.level == &level; ++start.ticks) {
        world.tick({}, now + kTickMs, now); now += kTickMs; world.fight.monsters.clear();
        if (start.ticks == 0) start.plan = subtiles(world.actor->player.path);
    }
    start.result = world.level != &level ? 1 : 0;
    if (dump) {   // `T seed level warp dir n x,y...`: the path the real tick planned for the click (World::tick, not a call of player_walk)
        std::fprintf(dump, "T %d %d %zu %d", seed, id, index, key);
        print_points(dump, start.plan);
        std::fputc('\n', dump);
    }
    return start;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    Options options;
    const bool sweep = argc > 1 && (std::strcmp(argv[1], "--sweep") == 0 || std::strcmp(argv[1], "--dump") == 0);
    options.dump = sweep && std::strcmp(argv[1], "--dump") == 0;
    int next = 2;
    if (sweep) {
        if (argc < 3 + (options.dump ? 1 : 0)) { std::fprintf(stderr, "test_warp_walk: %s SEEDS%s [--levels MIN MAX] [--dists C1,C2] [--dirs N]\n", argv[1], options.dump ? " FILE" : ""); return 1; }
        options.seeds = std::clamp(std::atoi(argv[next++]), 0, 10000);   // a sweep of at most 10000 maps
    }
    if (options.dump) {
        // The data loader logs on stdout, so a dump goes to its own file (W, G, D, F, E, T and S lines all go there).
        options.out = std::fopen(argv[next++], "w");
        if (!options.out) { std::fprintf(stderr, "test_warp_walk: cannot open %s\n", argv[next - 1]); return 1; }
    }
    for (; sweep && next < argc; ++next) {
        const std::string option = argv[next];
        if (option == "--levels" && next + 2 < argc) { options.level_min = std::atoi(argv[next + 1]); options.level_max = std::atoi(argv[next + 2]); next += 2; }
        else if (option == "--dirs" && next + 1 < argc) options.dirs = std::max(std::atoi(argv[++next]), 1);
        else if (option == "--dists" && next + 1 < argc) {
            options.dists.clear();
            for (const char* at = argv[++next]; *at;) { char* end = nullptr; options.dists.push_back(std::strtof(at, &end)); at = *end ? end + 1 : end; }
        } else { std::fprintf(stderr, "test_warp_walk: unknown option %s\n", option.c_str()); return 1; }
    }
    std::FILE* out = options.out;
    int total = 0, taken = 0, blocked_starts = 0, failed = 0;
    for (int seed = 1; seed <= options.seeds; ++seed) {
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, seed);
        if (!data || !data->patched) { std::fprintf(out, "SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
        // Level builds run on threads and link their neighbours as they finish; build and link every level before the first
        // start so a start near a map edge sees the same bits on every run.
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
        for (int id = options.level_min; id <= options.level_max; ++id) {
            if (!sweep && std::none_of(std::begin(kCases), std::end(kCases), [id](const Case& one) { return one.level == id; })) continue;
            const Level* level = data->level(id);
            if (!level || level->ds1.width() <= 0) continue;
            for (std::size_t index = 0; index < level->warps.size(); ++index) {
                const auto& warp = level->warps[index];
                const int per_dist = options.dirs;
                for (int key = 0; key < int(options.dists.size()) * per_dist; ++key) {
                    static const int dx8[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dy8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                    const int sub_dir = key % per_dist;
                    const Case* wanted = nullptr;
                    for (const auto& one : kCases) if (!sweep && seed == 1 && one.level == id && one.warp == int(index) && one.dir == key) wanted = &one;
                    if (!sweep && !wanted) continue;
                    const float dist = options.dists[std::size_t(key / per_dist)];
                    const float dx = per_dist == 8 ? float(dx8[sub_dir]) : std::cos(6.2831853f * float(sub_dir) / float(per_dist));
                    const float dy = per_dist == 8 ? float(dy8[sub_dir]) : std::sin(6.2831853f * float(sub_dir) / float(per_dist));
                    const auto start = run_start(world, data->walk_velocity[kSorceress], now, *level, index, seed, id, key, dx, dy, dist, options.dump ? out : nullptr);
                    if (start.result < 0) { std::fprintf(out, "W %d %d %zu %d %d -1 0 %d\n", seed, id, index, warp.destination, key, int(start.unit_blocked)); ++blocked_starts; continue; }
                    ++total; taken += start.result;
                    if (wanted) {
                        const bool plan_ok = plan_matches(start.plan, parse_plan(wanted->plan)), taken_ok = (start.result == 1) == wanted->taken;
                        if (!plan_ok || !taken_ok) {
                            std::printf("FAIL level %d warp %zu direction %d: the plan is", id, index, key);
                            for (const auto& point : start.plan) std::printf(" %d,%d", point.first, point.second);
                            std::printf(" (game.exe: %s), the warp is %s (game.exe: %s)\n", wanted->plan, start.result ? "taken" : "not taken", wanted->taken ? "taken" : "not taken");
                            ++failed;
                        }
                    }
                    if (sweep) std::fprintf(out, "W %d %d %zu %d %d %d %d %d\n", seed, id, index, warp.destination, key, start.result, start.ticks, int(start.unit_blocked));
                }
            }
        }
    }
    if (sweep) std::fprintf(out, "S %d %d %d\n", taken, total, blocked_starts);
    if (out != stdout) std::fclose(out);
    if (!sweep) {
        const bool ok = failed == 0 && total == int(std::size(kCases));
        std::printf("%s: %d of %zu starts answer as game.exe does (the plan the tick makes, the warp taken or not)\n", ok ? "OK" : "FAIL", total - failed, std::size(kCases));
        return ok ? 0 : 1;
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_warp_walk: %s\n", error.what());
    return 1;
}
