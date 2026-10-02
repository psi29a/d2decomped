// SPDX-License-Identifier: GPL-3.0-or-later
// Parse a real Rogue-Encampment DS1 and sanity-check the grid + file refs.
#include <ds1.hpp>
#include <mpq.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

int main() {
    const char* env = std::getenv("D2_MPQ_DIR");
    const fs::path dir = env ? fs::path(env)
        : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "")
            / "Workspace/private/diablo2";
    const auto d2data = dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        std::printf("SKIP: %s not found\n", d2data.string().c_str());
        return 0;
    }

    d2d::mpq::Archive mpq(d2data);

    // Small chunk of the Rogue Encampment — a boundary/transition stamp.
    // Exercises header, file list, walls, floors, objects on a real file.
    {
        const auto raw = mpq.read(R"(data\global\tiles\ACT1\TOWN\townStrans.ds1)");
        d2d::ds1::Map map(raw);
        std::printf("townStrans: v%d %dx%d act=%d files=%zu walls=%zu floors=%zu objs=%zu\n",
                    map.version(), map.width(), map.height(), map.act(),
                    map.files().size(), map.walls().size(), map.floors().size(),
                    map.objects().size());
        assert(map.width() > 0 && map.height() > 0);
        assert(map.version() >= 3);
        assert(map.act() == 1);
        // Rogue camp stamps reference at least one .dt1 tileset.
        assert(!map.files().empty());
        for (const auto& file : map.files()) {
            assert(!file.empty());
        }

        // Every layer should have exactly width*height cells.
        const auto cells = std::size_t(map.width()) * map.height();
        for (const auto& layer : map.floors()) assert(layer.cells.size() == cells);
        for (const auto& layer : map.walls())  assert(layer.cells.size() == cells);

        // At least one floor cell has a non-zero style (i.e. some tile).
        bool any_floor = false;
        for (const auto& layer : map.floors())
            for (const auto& tile : layer.cells)
                if (tile.style != 0 || tile.sequence != 0 || tile.prop1 != 0) {
                    any_floor = true; break;
                }
        assert(any_floor);
    }

    // The full Rogue Encampment: v18, with NPC patrol paths (Akara, Kashya,
    // Warriv, Charsi, Gheed) after the substitution groups.
    {
        const auto raw = mpq.read(R"(data\global\tiles\ACT1\TOWN\townE1.ds1)");
        d2d::ds1::Map map(raw);
        int walkers = 0;
        for (const auto& object : map.objects()) {
            if (object.path.empty()) continue;
            ++walkers;
            assert(object.type == 1);                       // only NPCs patrol
            for (const auto& point : object.path) {            // points stay on the map
                assert(point.x >= 0 && point.x < map.width() * 5);
                assert(point.y >= 0 && point.y < map.height() * 5);
            }
        }
        std::printf("townE1: v%d, %d NPCs with paths\n", map.version(), walkers);
        assert(walkers == 5);
    }

    std::printf("OK\n");
    return 0;
}
