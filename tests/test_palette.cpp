// Load the Rogue Encampment (ACT1) palette and sanity-check the shape.
#include <mpq.hpp>
#include <palette.hpp>

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

    // ACT1 palette is 768 bytes: 256 RGB triples.
    {
        const auto raw = mpq.read(R"(data\global\palette\ACT1\pal.dat)");
        assert(raw.size() == 256 * 3);
        d2d::palette::Palette p(raw);

        assert(p.entries().size() == 256);
        // Index 0 is always transparent by convention.
        assert(p[0].a == 0);
        // Every other entry is opaque.
        for (int i = 1; i < 256; ++i) assert(p[i].a == 0xFF);

        // Print a couple entries so a regression jumps out visually.
        std::printf("ACT1 pal[0] = %02x %02x %02x %02x\n",
                    p[0].r, p[0].g, p[0].b, p[0].a);
        std::printf("ACT1 pal[255] = %02x %02x %02x %02x\n",
                    p[255].r, p[255].g, p[255].b, p[255].a);
    }

    // PL2 shares the first 768 bytes with pal.dat — same parser works.
    {
        const auto raw = mpq.read(R"(data\global\palette\menu1\Pal.PL2)");
        assert(raw.size() > 256 * 3);
        d2d::palette::Palette p(raw);
        assert(p.entries().size() == 256);
        assert(p[0].a == 0);
    }

    // set_alpha override.
    {
        std::array<std::byte, 256*3> bytes{};
        bytes[0] = std::byte{100};
        d2d::palette::Palette p(bytes);
        assert(p[0].a == 0);
        p.set_alpha(0, 0xFF);
        assert(p[0].a == 0xFF);
    }

    std::printf("OK\n");
    return 0;
}
