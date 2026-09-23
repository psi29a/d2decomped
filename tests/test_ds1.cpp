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
        d2d::ds1::Map m(raw);
        std::printf("townStrans: v%d %dx%d act=%d files=%zu walls=%zu floors=%zu objs=%zu\n",
                    m.version(), m.width(), m.height(), m.act(),
                    m.files().size(), m.walls().size(), m.floors().size(),
                    m.objects().size());
        assert(m.width() > 0 && m.height() > 0);
        assert(m.version() >= 3);
        assert(m.act() == 1);
        // Rogue camp stamps reference at least one .dt1 tileset.
        assert(!m.files().empty());
        for (const auto& f : m.files()) {
            assert(!f.empty());
        }

        // Every layer should have exactly width*height cells.
        const auto cells = std::size_t(m.width()) * m.height();
        for (const auto& l : m.floors()) assert(l.cells.size() == cells);
        for (const auto& l : m.walls())  assert(l.cells.size() == cells);

        // At least one floor cell has a non-zero style (i.e. some tile).
        bool any_floor = false;
        for (const auto& l : m.floors())
            for (const auto& t : l.cells)
                if (t.style != 0 || t.sequence != 0 || t.prop1 != 0) {
                    any_floor = true; break;
                }
        assert(any_floor);
    }

    // The full Rogue Encampment: v18, with NPC patrol paths (Akara, Kashya,
    // Warriv, Charsi, Gheed) after the substitution groups.
    {
        const auto raw = mpq.read(R"(data\global\tiles\ACT1\TOWN\townE1.ds1)");
        d2d::ds1::Map m(raw);
        int walkers = 0;
        for (const auto& o : m.objects()) {
            if (o.path.empty()) continue;
            ++walkers;
            assert(o.type == 1);                       // only NPCs patrol
            for (const auto& pt : o.path) {            // points stay on the map
                assert(pt.x >= 0 && pt.x < m.width() * 5);
                assert(pt.y >= 0 && pt.y < m.height() * 5);
            }
        }
        std::printf("townE1: v%d, %d NPCs with paths\n", m.version(), walkers);
        assert(walkers == 5);
    }

    std::printf("OK\n");
    return 0;
}
