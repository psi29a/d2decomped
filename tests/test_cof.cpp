// Parse two real COF files (already used as byte-length canary in test_mpq).
#include <cof.hpp>
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
    const auto d2char = dir / "d2char.mpq";
    if (!fs::exists(d2char)) {
        std::printf("SKIP: %s not found\n", d2char.string().c_str());
        return 0;
    }

    d2d::mpq::Archive a(d2char);

    // Necromancer walk-left animation, hand-to-hand weapon class.
    {
        const auto raw = a.read(R"(data\global\CHARS\NE\COF\NEWL1HT.COF)");
        d2d::cof::Cof c(raw);
        std::printf("NEWL1HT: layers=%u frames=%u dirs=%u speed=%u\n",
                    unsigned(c.layers()), unsigned(c.frames_per_direction()),
                    unsigned(c.directions()), unsigned(c.speed()));
        assert(c.layers() > 0);
        assert(c.frames_per_direction() > 0);
        assert(c.directions() > 0);
        assert(c.layer_defs().size() == c.layers());
        assert(c.events().size() == c.frames_per_direction());

        // Every layer defines a weapon class (usually "hth" for walk).
        for (const auto& L : c.layer_defs()) {
            assert(!L.weapon_class.empty());
            assert(L.weapon_class.size() <= 3);
        }

        // Priority lookup for (dir 0, frame 0) returns `layers` bytes.
        const auto pri = c.priority(0, 0);
        assert(pri.size() == c.layers());
    }

    // Barbarian throw with crossbow — different weapon class.
    {
        const auto raw = a.read(R"(data\global\CHARS\BA\COF\BATNXBW.COF)");
        d2d::cof::Cof c(raw);
        assert(c.directions() == 8 || c.directions() == 16);
        assert(c.layers() > 0);
    }

    std::printf("OK\n");
    return 0;
}
