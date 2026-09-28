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

    d2d::mpq::Archive archive(d2data);

    // Small UI icon: 141 bytes on disk, 1 direction × 1 frame.
    {
        const auto raw = archive.read(R"(data\global\ui\MENU\helpwhitebullet.dc6)");
        d2d::dc6::Sprite sprite(raw);
        assert(sprite.directions() == 1);
        assert(sprite.frames_per_direction() == 1);
        assert(sprite.total_frames() == 1);

        const auto& frame = sprite.frame(0, 0);
        assert(frame.width  > 0 && frame.width  <= 32);
        assert(frame.height > 0 && frame.height <= 32);
        assert(frame.pixels.size() == std::size_t(frame.width) * frame.height);
        // Bullet icon has a coloured centre — at least one non-transparent pixel.
        bool any = false;
        for (auto pixel : frame.pixels) if (pixel) { any = true; break; }
        assert(any);
        std::printf("bullet: %ux%u, %zu non-zero pixels\n",
                    frame.width, frame.height,
                    (std::size_t)std::count_if(frame.pixels.begin(), frame.pixels.end(),
                                               [](auto pixel){ return pixel != 0; }));
    }

    // A directional sprite (monster/inventory) — exercises multi-frame decoding.
    {
        const auto raw = archive.read(R"(data\global\items\invgsba.dc6)");
        d2d::dc6::Sprite sprite(raw);
        assert(sprite.directions() >= 1);
        assert(sprite.frames_per_direction() >= 1);
        for (const auto& frame : sprite.frames()) {
            assert(frame.pixels.size() == std::size_t(frame.width) * frame.height);
        }
    }

    std::printf("OK\n");
    return 0;
}
