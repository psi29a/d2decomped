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

    d2d::mpq::Archive archive(d2char);

    // Small light overlay — a good bit-decoder canary (~2 KB compressed).
    {
        const auto raw = archive.read(R"(data\global\CHARS\SO\S1\SOS1LITBLHTH.dcc)");
        d2d::dcc::Sprite sprite(raw);
        std::printf("SOS1LITBL: dirs=%u frames=%d\n",
                    unsigned(sprite.directions()), sprite.frames_per_direction());
        assert(sprite.directions() > 0);
        assert(sprite.frames_per_direction() > 0);
        for (const auto& frame : sprite.frames()) {
            assert(frame.width > 0 && frame.height > 0);
            assert(frame.pixels.size() == std::size_t(frame.width) * frame.height);
        }
        // At least one frame has non-transparent pixels.
        bool any = false;
        for (const auto& frame : sprite.frames()) {
            if (std::any_of(frame.pixels.begin(), frame.pixels.end(),
                            [](auto pixel){ return pixel != 0; })) { any = true; break; }
        }
        assert(any);
    }

    // Bigger sprite — a Barbarian head equipped with a helm.
    // Exercises equal-cells + raw-pixel paths across all directions.
    {
        const auto raw = archive.read(R"(data\global\CHARS\BA\HD\BAHDHLMTWHTH.dcc)");
        d2d::dcc::Sprite sprite(raw);
        std::printf("BAHDHLMTW: dirs=%u frames=%d\n",
                    unsigned(sprite.directions()), sprite.frames_per_direction());
        assert(sprite.directions() > 0 && sprite.frames_per_direction() > 0);
        std::size_t total_painted = 0;
        for (const auto& frame : sprite.frames()) {
            assert(frame.pixels.size() == std::size_t(frame.width) * frame.height);
            total_painted += std::size_t(std::count_if(
                frame.pixels.begin(), frame.pixels.end(),
                [](auto pixel){ return pixel != 0; }));
        }
        std::printf("BAHDHLMTW: total painted pixels = %zu\n", total_painted);
        assert(total_painted > 100);
    }

    std::printf("OK\n");
    return 0;
}
