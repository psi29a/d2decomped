// Parse a real DT1 from d2data.mpq and verify structure + decoded pixels.
#include <dt1.hpp>
#include <mpq.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

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

    // A minimal DT1 — 1×1 tile, exercises the header-and-decode path with
    // very little data (blank.dt1 is 14 KB, one of the smallest real DT1s).
    // Notable Blizzard quirk: some files named ".dt1" (e.g.
    // ACT1/BARRACKS/gargtrap.dt1) are actually mislabeled DC6s — the version
    // check fails on those with "DT1: unsupported version", which is the
    // desired behaviour (the caller should route by content, not extension).
    {
        const auto raw = mpq.read(R"(data\global\tiles\ACT1\OUTDOORS\blank.dt1)");
        d2d::dt1::Archive archive(raw);
        std::printf("blank.dt1: %zu tiles\n", archive.size());
        assert(archive.size() > 0);
        for (const auto& tile : archive.tiles()) {
            assert(tile.width > 0);
            const auto height = std::size_t(tile.height < 0 ? -tile.height : tile.height);
            assert(tile.pixels.size() == std::size_t(tile.width) * height
                || height == 0);   // some rare "shadow" tiles carry no pixels
        }
    }

    // Rogue Encampment floor — the milestone tileset for phase 5.
    {
        const auto raw = mpq.read(R"(data\global\tiles\ACT1\TOWN\floor.dt1)");
        d2d::dt1::Archive archive(raw);
        std::printf("rogue floor.dt1: %zu tiles\n", archive.size());
        assert(archive.size() > 20);
        // A floor tile: at least one tile has non-transparent decoded pixels.
        bool any_painted = false;
        for (const auto& tile : archive.tiles()) {
            for (auto pixel : tile.pixels) if (pixel) { any_painted = true; break; }
            if (any_painted) break;
        }
        assert(any_painted);
    }

    std::printf("OK\n");
    return 0;
}
