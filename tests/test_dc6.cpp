// Parse a real DC6 out of d2data.mpq and verify structure + pixel decoding.
#include <dc6.hpp>
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
    const auto d2data = dir / "d2data.mpq";
    if (!fs::exists(d2data)) {
        std::printf("SKIP: %s not found\n", d2data.string().c_str());
        return 0;
    }

    d2d::mpq::Archive a(d2data);

    // Small UI icon: 141 bytes on disk, 1 direction × 1 frame.
    {
        const auto raw = a.read(R"(data\global\ui\MENU\helpwhitebullet.dc6)");
        d2d::dc6::Sprite s(raw);
        assert(s.directions() == 1);
        assert(s.frames_per_direction() == 1);
        assert(s.total_frames() == 1);

        const auto& f = s.frame(0, 0);
        assert(f.width  > 0 && f.width  <= 32);
        assert(f.height > 0 && f.height <= 32);
        assert(f.pixels.size() == std::size_t(f.width) * f.height);
        // Bullet icon has a coloured centre — at least one non-transparent pixel.
        bool any = false;
        for (auto p : f.pixels) if (p) { any = true; break; }
        assert(any);
        std::printf("bullet: %ux%u, %zu non-zero pixels\n",
                    f.width, f.height,
                    (std::size_t)std::count_if(f.pixels.begin(), f.pixels.end(),
                                               [](auto p){ return p != 0; }));
    }

    // A directional sprite (monster/inventory) — exercises multi-frame decoding.
    {
        const auto raw = a.read(R"(data\global\items\invgsba.dc6)");
        d2d::dc6::Sprite s(raw);
        assert(s.directions() >= 1);
        assert(s.frames_per_direction() >= 1);
        for (const auto& f : s.frames()) {
            assert(f.pixels.size() == std::size_t(f.width) * f.height);
        }
    }

    std::printf("OK\n");
    return 0;
}
