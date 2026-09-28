// Load the Rogue Encampment (ACT1) palette and sanity-check the shape.
#include <mpq.hpp>
#include <palette.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

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
        d2d::palette::Palette palette(raw);

        assert(palette.entries().size() == 256);
        // Index 0 is always transparent by convention.
        assert(palette[0].a == 0);
        // Every other entry is opaque.
        for (int i = 1; i < 256; ++i) assert(palette[std::uint8_t(i)].a == 0xFF);

        // Print a couple entries so a regression jumps out visually.
        std::printf("ACT1 pal[0] = %02x %02x %02x %02x\n",
                    palette[0].r, palette[0].g, palette[0].b, palette[0].a);
        std::printf("ACT1 pal[255] = %02x %02x %02x %02x\n",
                    palette[255].r, palette[255].g, palette[255].b, palette[255].a);
    }

    // PL2 shares the first 768 bytes with pal.dat — same parser works.
    {
        const auto raw = mpq.read(R"(data\global\palette\menu1\Pal.PL2)");
        assert(raw.size() > 256 * 3);
        d2d::palette::Palette palette(raw);
        assert(palette.entries().size() == 256);
        assert(palette[0].a == 0);
    }

    // set_alpha override.
    {
        std::array<std::byte, 256*3> bytes{};
        bytes[0] = std::byte{100};
        d2d::palette::Palette palette(bytes);
        assert(palette[0].a == 0);
        palette.set_alpha(0, 0xFF);
        assert(palette[0].a == 0xFF);
    }

    // BGR ordering — pal.dat stores (b, g, r) per DAT format. Feed a synthetic
    // buffer where index 1 is BGR (10, 20, 30) and check we read R=30 B=10.
    {
        std::array<std::byte, 256*3> bytes{};
        bytes[1*3 + 0] = std::byte{10};   // B
        bytes[1*3 + 1] = std::byte{20};   // G
        bytes[1*3 + 2] = std::byte{30};   // R
        d2d::palette::Palette palette(bytes);
        assert(palette[1].r == 30);
        assert(palette[1].g == 20);
        assert(palette[1].b == 10);
    }

    // Pl2 colormap — real PL2 file. Check base + additive + blend50 LUTs
    // hold their invariants (additive is commutative; identity axes hit;
    // blend50 diagonal is identity).
    {
        const auto raw = mpq.read(R"(data\global\palette\menu1\Pal.PL2)");
        assert(raw.size() == d2d::palette::Pl2::kFileSize);
        d2d::palette::Pl2 pl2(raw);
        assert(pl2.base_palette()[0].a == 0);
        // additive(i, 0) == i and additive(0, j) == j for every index.
        for (int i = 0; i < 256; ++i) {
            assert(pl2.additive(std::uint8_t(i), 0) == i);
            assert(pl2.additive(0, std::uint8_t(i)) == i);
        }
        // blend50 diagonal identity: blending an index with itself is itself.
        for (int i = 0; i < 256; ++i) {
            assert(pl2.blend50(std::uint8_t(i), std::uint8_t(i)) == i);
        }
        // Commutativity of additive.
        for (int i = 0; i < 256; i += 17) {
            for (int j = 0; j < 256; j += 13) {
                assert(pl2.additive(std::uint8_t(i), std::uint8_t(j))
                    == pl2.additive(std::uint8_t(j), std::uint8_t(i)));
            }
        }
        std::printf("PL2 base[94]  = %02x %02x %02x\n",
                    pl2.base_palette()[94].r, pl2.base_palette()[94].g,
                    pl2.base_palette()[94].b);
        std::printf("PL2 additive(94, 200) = %u\n", pl2.additive(94, 200));
    }

    // Pl2 short buffer throws.
    {
        std::vector<std::byte> tiny(1024);
        bool threw = false;
        try { d2d::palette::Pl2 pl2(tiny); }
        catch (const std::runtime_error&) { threw = true; }
        assert(threw);
    }

    std::printf("OK\n");
    return 0;
}
