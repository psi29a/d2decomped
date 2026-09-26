// The Blood Moor from real game data over many map seeds: a closed
// border, one Den of Evil, roads, grass everywhere else, and the same
// level from the same seed. Prints one level's cells.
#include <maze.hpp>
#include <outdoor_data.hpp>
#include <mpq.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace d2d::drlg;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env) : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / "Workspace/private/diablo2";
    if (!fs::exists(dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", dir.string().c_str()); return 0; }
    d2d::mpq::Stack mpqs;
    if (const char* p = std::getenv("D2_PATCH_INSTALLER")) mpqs.push_installer(p);
    for (const char* n : { "d2exp.mpq", "d2data.mpq" }) if (fs::exists(dir / n)) mpqs.push(dir / n);

    OutdoorAssets a;
    load_outdoor_assets(a, [&](const std::string& p) { return mpqs.try_read(p); });
    assert(a.levels.size() > 100 && a.data.presets.count(52) && a.data.subs.size() > 10);
    const auto defs = level_defs(a.levels);

    std::vector<std::string> notes;
    std::array<std::uint32_t, 4> first{};                       // first map seed per town file
    for (std::uint32_t seed = 1; seed < 200; ++seed)
        if (const int f = town_file(act1_from_map_seed(defs, seed)); f >= 0 && !first[std::size_t(f)]) first[std::size_t(f)] = seed;
    std::printf("first seeds for townN1/E1/S1/W1: %u %u %u %u\n", first[0], first[1], first[2], first[3]);
    for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        const auto layout = act1_from_map_seed(defs, seed);
        const auto L = outdoor_level(a.levels, layout, 2);
        assert(L.rect.level == 2 && L.rect.w + L.rect.h == 152);
        assert(L.neighbours.size() == 2);                       // Cold Plains and the town
        const auto o = generate_outdoor(a.data, L, level_seed(seed, 2));
        for (const auto& n : o.notes) if (std::ranges::find(notes, n) == notes.end()) notes.push_back(n);
        auto cell = [&](const std::vector<std::uint32_t>& g, int x, int y) { return g[std::size_t(y * o.cw + x)]; };
        auto print = [&] {
            std::printf("seed %u: Blood Moor %dx%d at (%d,%d), flags 0x%x, town at (%d,%d)\n", seed, L.rect.w, L.rect.h,
                        L.rect.x, L.rect.y, L.rect.flags, L.town.x, L.town.y);
            for (int y = 0; y < o.ch; ++y) {
                for (int x = 0; x < o.cw; ++x) {
                    const auto def = cell(o.g04, x, y), f = cell(o.g2c, x, y);
                    if (def) std::printf("%3u", def);
                    else std::printf("  %c", f & 0x200 ? '+' : f & 0x100 ? ' ' : f & 0x80 ? '=' : '.');
                }
                std::puts("");
            }
        };
        if (seed == 1 || seed == 3) {
            print();
            int road = 0;
            for (const auto& t : o.tiles.floors()[0].cells) road += (t.prop1 & 0x80) && t.style == 0 && t.sequence;
            std::printf("road tiles %d; roads:", road);
            for (const auto& r : o.roads) { std::printf(" ["); for (auto [x, y] : r) std::printf(" %d,%d", x - L.rect.x, y - L.rect.y); std::printf(" ]"); }
            std::puts("");
        }
        int dens = 0;
        for (int y = 0; y < o.ch; ++y)
            for (int x = 0; x < o.cw; ++x) {
                const auto f = cell(o.g2c, x, y);
                if (x == 0 || y == 0 || x == o.cw - 1 || y == o.ch - 1) { if (!(f & 0x301)) { std::printf("seed %u open at (%d,%d)\n", seed, x, y); print(); } assert(f & 0x301); }   // closed but for the town side
                dens += cell(o.g04, x, y) == 52;
            }
        assert(dens == 1);
        assert(!o.roads.empty());
        // Every tile has a floor or a preset's.
        int bare = 0;
        for (const auto& t : o.tiles.floors()[0].cells) bare += (t.prop1 == 0);
        assert(bare < o.tiles.width() * o.tiles.height() / 4);
        const auto again = generate_outdoor(a.data, L, level_seed(seed, 2));
        assert(again.g04 == o.g04 && again.g2c == o.g2c && again.roads == o.roads);
    }
    for (const auto& n : notes) std::printf("not implemented: %s\n", n.c_str());

    // Values game.exe itself produces for map seed 3, read out under the
    // emulator (tools/emu/drlg.py 3 2 tiles, drlg.py 3 8); diff_drlg.py
    // checks thousands of seeds this way.
    {
        const auto read = [&](const std::string& p) { return mpqs.try_read(p); };
        const auto dt1s = load_room_dt1s(a, read, 2);
        a.data.dt1s = &dt1s;
        const auto L = outdoor_level(a.levels, act1_from_map_seed(defs, 3), 2);
        const auto o = generate_outdoor(a.data, L, level_seed(3, 2));
        assert(o.rooms.size() == 83 && o.rooms.front().x == 0 && o.rooms.front().seed == 0x32de6615);
        std::vector<std::string> dnotes;
        const auto built = level_room_tiles(o.rooms, o.plain, a.data, dt1s, 2, warp_slots(a, 2), dnotes);
        auto has = [&](int rx, int ry, int layer, int x, int y, const std::string& want) {    // any tile of the layer there
            for (const auto& r : built)
                if (r.x == rx && r.y == ry)
                    for (const auto& t : r.tiles)
                        if (t.layer == layer && t.x == rx + x && t.y == ry + y && t.file
                            && t.file->name + ":" + std::to_string(t.index) == want) return true;
            return false;
        };
        assert(has(24, 8, 1, 0, 0, "floor.dt1:39"));                             // a plain room's grass
        assert(has(32, 8, 0, 6, 2, "trees.dt1:63"));                             // a stamped tree
        assert(has(32, 8, 2, 5, 1, "trees.dt1:67"));                             // its shadow, picked at stamp time
        assert(has(64, 24, 1, 2, 3, "cavedr.dt1:27"));                           // the Den entrance's lit floor
        a.data.dt1s = nullptr;

        MazeDef m;
        m.rooms.fill(1); m.w = m.h = 24; m.merge = 500;                         // LvlMaze "Act 1 - Cave 1"
        auto den = generate_maze(a.data, m, 8, 200, 200, level_seed(3, 8), 0, dnotes);
        assert(den.size() == 27);
        auto at = [&](int x, int y) { for (const auto& r : den) if (r.x == x && r.y == y) return r; return Outdoor::RoomSeed{}; };
        assert(at(24, 0).def == 97 && at(24, 0).file == 0 && at(24, 0).seed == 0x8d8dcf44);    // the Den's own room
        assert(at(0, 24).def == 84 && at(0, 24).file == 1);                                    // the entrance
        assert(at(24, 24).def == 61 && at(24, 24).seed == 0x775aabb3);                         // Cave NW
        // Units (drlg.py 3 8 units): Corpsefire (superunique 40 = MonStats rows + 40) in room (40, 8).
        const auto den_dt1s = load_room_dt1s(a, read, 3);
        const auto den_rooms = level_room_tiles(den, {}, a.data, den_dt1s, 8, warp_slots(a, 8), dnotes);
        bool corpsefire = false;
        for (const auto& r : den_rooms)
            for (const auto& u : r.units)
                corpsefire |= r.x == 40 && r.y == 8 && u.type == 1 && u.id == a.data.ids.monstats + 40 && u.x == 20 && u.y == 5;
        assert(a.data.ids.monstats == 734 && a.data.ids.superuniques == 66 && corpsefire);
        bool flavie = false;                                                    // the Blood Moor's way in
        for (const auto& r : built) for (const auto& u : r.units) flavie |= u.type == 1 && u.id == 266;
        assert(flavie);
    }
    std::puts("test_outdoor: ok");
}
