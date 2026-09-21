// Parse real DCC sprites and verify structure + decoded pixels.
#include <dcc.hpp>
#include <mpq.hpp>

#include <algorithm>
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

    // Small light overlay — a good bit-decoder canary (~2 KB compressed).
    {
        const auto raw = a.read(R"(data\global\CHARS\SO\S1\SOS1LITBLHTH.dcc)");
        d2d::dcc::Sprite s(raw);
        std::printf("SOS1LITBL: dirs=%u frames=%d\n",
                    unsigned(s.directions()), s.frames_per_direction());
        assert(s.directions() > 0);
        assert(s.frames_per_direction() > 0);
        for (const auto& f : s.frames()) {
            assert(f.width > 0 && f.height > 0);
            assert(f.pixels.size() == std::size_t(f.width) * f.height);
        }
        // At least one frame has non-transparent pixels.
        bool any = false;
        for (const auto& f : s.frames()) {
            if (std::any_of(f.pixels.begin(), f.pixels.end(),
                            [](auto p){ return p != 0; })) { any = true; break; }
        }
        assert(any);
    }

    // Bigger sprite — a Barbarian head equipped with a helm.
    // Exercises equal-cells + raw-pixel paths across all directions.
    {
        const auto raw = a.read(R"(data\global\CHARS\BA\HD\BAHDHLMTWHTH.dcc)");
        d2d::dcc::Sprite s(raw);
        std::printf("BAHDHLMTW: dirs=%u frames=%d\n",
                    unsigned(s.directions()), s.frames_per_direction());
        assert(s.directions() > 0 && s.frames_per_direction() > 0);
        std::size_t total_painted = 0;
        for (const auto& f : s.frames()) {
            assert(f.pixels.size() == std::size_t(f.width) * f.height);
            total_painted += std::size_t(std::count_if(
                f.pixels.begin(), f.pixels.end(),
                [](auto p){ return p != 0; }));
        }
        std::printf("BAHDHLMTW: total painted pixels = %zu\n", total_painted);
        assert(total_painted > 100);
    }

    std::printf("OK\n");
    return 0;
}
