// drlg-dump <mpq dir> <map seed> [level] — our generator's level in the
// same text form as tools/emu/drlg.py prints game.exe's, for diffing.
// drlg-dump <mpq dir> <first>-<last> <level> <out dir> writes <seed>.txt each.
// A trailing `tiles` adds every room's tiles (as drlg.py <seed> <level> tiles).
#include <maze.hpp>
#include <outdoor_data.hpp>
#include <mpq.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

namespace fs = std::filesystem;
using namespace d2d::drlg;

static bool g_units = false;                            // print rooms' units instead of their tiles

static std::string dump(const OutdoorAssets& a, std::uint32_t seed, int id, const RoomDt1s* dt1s = nullptr) {
    std::ostringstream os;
    char b[256];
#define pf(...) (std::snprintf(b, sizeof b, __VA_ARGS__), os << b)   // a literal format each time (-Wformat-security)
    if (const auto row = level_row(a.levels, id); row && to_int(a.levels.get(*row, "DrlgType")) == 1) {
        const int w = to_int(a.levels.get(*row, "SizeX")), h = to_int(a.levels.get(*row, "SizeY"));
        pf("level %d at %d,%d size %dx%d\n", id, to_int(a.levels.get(*row, "OffsetX")), to_int(a.levels.get(*row, "OffsetY")), w, h);
        MazeDef m;
        for (std::size_t r = 0; r < a.lvl_maze.size(); ++r)
            if (to_int(a.lvl_maze.get(r, "Level"), -1) == id) {
                // ponytail: LvlMaze.txt has one Rooms column; game.exe's .bin has one per difficulty
                m.rooms.fill(to_int(a.lvl_maze.get(r, "Rooms")));
                m.w = to_int(a.lvl_maze.get(r, "SizeX"));
                m.h = to_int(a.lvl_maze.get(r, "SizeY"));
                m.merge = to_int(a.lvl_maze.get(r, "Merge"));
            }
        std::vector<std::string> notes;
        auto rooms = generate_maze(a.data, m, id, w, h, level_seed(seed, id), 0, notes);
        auto sorted = rooms;
        std::ranges::sort(sorted, {}, [](const auto& r) { return std::tuple(r.y, r.x); });
        pf("rooms %zu\n", sorted.size());
        for (const auto& r : sorted)
            pf("%d,%d %dx%d kind %d seed %08x def %d file %d at %d,%d\n", r.x, r.y, r.w, r.h, r.kind, r.seed, r.def, r.file, r.px, r.py);
        if (dt1s) {
            auto built = level_room_tiles(rooms, {}, a.data, *dt1s, id, warp_slots(a, id), notes);
            std::ranges::sort(built, {}, [](const auto& r) { return std::tuple(r.y, r.x); });
            if (g_units) {
                for (const auto& r : built) {
                    if (r.units.empty()) continue;
                    pf("units %d,%d:", r.x, r.y);
                    for (const auto& u : r.units) pf(" %d:%d m%d %d,%d f%x", u.type, u.id, u.mode, u.x, u.y, u.flags);
                    pf("\n");
                }
                built.clear();
            }
            for (const auto& r : built) {
                const auto it = a.data.presets.find(r.seed->def);
                const auto mask = it != a.data.presets.end() ? it->second.dt1_mask : 0u;
                pf("room %d,%d mask %x dt1s", r.x, r.y, mask);
                for (const auto* f : room_dt1_list(mask, *dt1s)) pf(" %s", f->name.c_str());
                pf("\n");
                for (int layer : { 0, 1, 2 })
                    for (const auto& e : r.tiles)
                        if (e.layer == layer)
                            pf(" %s %d,%d o%d %s:%d\n", layer == 0 ? "wall" : layer == 1 ? "floor" : "shadow", e.x - r.x, e.y - r.y,
                               e.orient, e.file ? e.file->name.c_str() : "?", e.index);
            }
        }
        for (const auto& n : notes) std::fprintf(stderr, "not implemented: %s\n", n.c_str());
        return os.str();
    }
    const auto layout = act1_from_map_seed(level_defs(a.levels), seed);
    if (std::getenv("DRLG_LAYOUT"))
        for (const auto& p : layout) std::fprintf(stderr, "layout %d at %d,%d %dx%d dir %d flip %d flags %x\n", p.level, p.x, p.y, p.w, p.h, p.dir, p.flip, p.flags);
    const auto L = outdoor_level(a.levels, layout, id);
    const auto o = generate_outdoor(a.data, L, level_seed(seed, id));
    pf("level %d at %d,%d size %dx%d\n", id, L.rect.x, L.rect.y, L.rect.w, L.rect.h);
    pf("flags %x\n", o.flags);
    for (auto [name, g] : { std::pair{ "g04", &o.g04 }, { "g18", &o.g18 }, { "g2c", &o.g2c } }) {
        pf("%s %dx%d\n", name, o.cw, o.ch);
        for (int y = 0; y < o.ch; ++y)
            for (int x = 0; x < o.cw; ++x) pf("%x%c", (*g)[std::size_t(y * o.cw + x)], x + 1 < o.cw ? ' ' : '\n');
    }
    auto rooms = o.rooms;
    std::ranges::sort(rooms, {}, [](const auto& r) { return std::tuple(r.y, r.x); });
    pf("rooms %zu\n", rooms.size());
    for (const auto& r : rooms) pf("%d,%d %dx%d kind %d seed %08x\n", r.x, r.y, r.w, r.h, r.kind, r.seed);
    auto notes = o.notes;
    if (dt1s) {                                         // plain rooms' tiles, as tools/emu/drlg.py tiles
        auto built = level_room_tiles(o.rooms, o.plain, a.data, *dt1s, id, warp_slots(a, id), notes);
        std::ranges::sort(built, {}, [](const auto& r) { return std::tuple(r.y, r.x); });
        if (g_units) {
            for (const auto& r : built) {
                if (r.units.empty()) continue;
                pf("units %d,%d:", r.x, r.y);
                for (const auto& u : r.units) pf(" %d:%d m%d %d,%d f%x", u.type, u.id, u.mode, u.x, u.y, u.flags);
                pf("\n");
            }
            built.clear();
        }
        for (const auto& r : built) {
            const auto it = a.data.presets.find(r.seed->def);
            const auto mask = r.plain ? r.plain->dt1_mask : it != a.data.presets.end() ? it->second.dt1_mask : 0u;
            pf("room %d,%d mask %x dt1s", r.x, r.y, mask);
            for (const auto* f : room_dt1_list(mask, *dt1s)) pf(" %s", f->name.c_str());
            pf("\n");
            for (int layer : { 0, 1, 2 })
                for (const auto& e : r.tiles)
                    if (e.layer == layer)
                        pf(" %s %d,%d o%d %s:%d\n", layer == 0 ? "wall" : layer == 1 ? "floor" : "shadow", e.x - r.x, e.y - r.y,
                           e.orient, e.file ? e.file->name.c_str() : "?", e.index);
        }
    }
    for (const auto& n : notes) std::fprintf(stderr, "not implemented: %s\n", n.c_str());
    return os.str();
#undef pf
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: drlg-dump <mpq dir> <map seed> [level]\n"); return 2; }
    const fs::path dir = argv[1];
    const std::string range = argv[2];
    const int id = argc > 3 ? std::atoi(argv[3]) : 2;
    d2d::mpq::Stack mpqs;
    if (const char* p = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(p);
    for (const char* n : { "d2exp.mpq", "d2data.mpq" }) if (fs::exists(dir / n)) mpqs.push(dir / n);
    OutdoorAssets a;                                    // not const: load_room_dt1s adds to it
    load_outdoor_assets(a, [&](const std::string& p) { return mpqs.try_read(p); });
    g_units = argc > 4 && std::string(argv[argc - 1]) == "units";
    const bool tiles = argc > 4 && (std::string(argv[argc - 1]) == "tiles" || g_units);
    RoomDt1s dt1s;
    if (tiles) {
        const auto row = level_row(a.levels, id);
        dt1s = load_room_dt1s(a, [&](const std::string& p) { return mpqs.try_read(p); }, row ? to_int(a.levels.get(*row, "LevelType")) : 0);
        a.data.dt1s = &dt1s;
    }
    if (const auto dash = range.find('-'); dash != std::string::npos && argc > 4) {
        const auto first = std::uint32_t(std::stoul(range.substr(0, dash), nullptr, 0));
        const auto last = std::uint32_t(std::stoul(range.substr(dash + 1), nullptr, 0));
        for (auto seed = first;; ++seed) {
            std::ofstream(fs::path(argv[4]) / (std::to_string(seed) + ".txt")) << dump(a, seed, id, tiles ? &dt1s : nullptr);
            if (seed == last) break;
        }
    } else {
        std::fputs(dump(a, std::uint32_t(std::stoul(range, nullptr, 0)), id, tiles ? &dt1s : nullptr).c_str(), stdout);
    }
}
