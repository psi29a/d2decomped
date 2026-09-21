// Load font16 (metrics + glyph sheet) and verify glyph lookup + measure.
#include <font.hpp>
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

    // font16 is one of the mid-size UI fonts.
    auto tbl_bytes = mpq.read(R"(data\local\FONT\LATIN\font16.tbl)");
    auto dc6_bytes = mpq.read(R"(data\local\FONT\LATIN\font16.dc6)");
    d2d::dc6::Sprite sheet(dc6_bytes);

    d2d::font::Font f(tbl_bytes, std::move(sheet));
    std::printf("font16: glyphs=%zu line_height=%d sheet_frames=%zu\n",
                f.glyph_count(), f.line_height(),
                f.sheet().total_frames());

    assert(f.glyph_count() == 256);      // one record per Latin-1 byte

    // Every printable ASCII char should have a glyph with non-zero width.
    for (int c = 'A'; c <= 'Z'; ++c) {
        const auto* g = f.find(std::uint16_t(c));
        assert(g);
        assert(g->width > 0);
        assert(g->height > 0);
        assert(g->frame < f.sheet().total_frames());
    }

    // "Hello" should have a positive measured width equal to the sum of
    // per-char advance widths.
    const auto w = f.measure("Hello");
    int by_hand = 0;
    for (char c : std::string_view("Hello")) by_hand += f.find(c)->width;
    assert(w == by_hand);
    assert(w > 0);

    std::printf("measure(\"Hello\") = %d px\n", w);

    // A missing code returns nullptr.
    assert(f.find(0xFFFF) == nullptr);

    std::printf("OK\n");
    return 0;
}
