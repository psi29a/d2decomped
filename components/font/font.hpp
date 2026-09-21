// D2Decomp bitmap-font parser + text blitter helpers.
//
// D2 stores fonts as a pair:
//   * font<size>.tbl — glyph metrics table, "Woo!\x01" magic + 7 header
//     bytes + 256 * 14-byte glyph records.
//   * font<size>.dc6 — 1 direction × N frames DC6, one frame per glyph
//     indexed by the glyph record's `frame` field.
//
// Glyph record (14 bytes each):
//   u16 code            character code (byte 0/1)
//   u8  unknown1        (usually 0)
//   u8  width           advance width in pixels
//   u8  height          glyph height in pixels
//   3   unknown2        (usually 1, 0, 0)
//   u16 frame           DC6 frame index
//   4   unknown3        (usually 1, 0, 0, char_code_repeated | 0)
//
// D2 encodes each char code as itself (ASCII/Latin-1), except NUL and a few
// control codes which show up as zero-width placeholders. Non-Latin fonts
// (JPN/KOR/CYR) use the same layout with different codepoints.
#pragma once

#include <dc6.hpp>
#include <palette.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::font {

struct Glyph {
    std::uint16_t code{};
    std::uint8_t  width{};    // advance width (blit + cursor advance)
    std::uint8_t  height{};
    std::uint16_t frame{};    // index into the DC6 sheet
};

class Font {
public:
    Font() = default;

    // Consumes both the .tbl bytes and the already-parsed DC6 sheet. Takes
    // the sheet by value — Font owns it thereafter.
    Font(std::span<const std::byte> tbl_bytes, dc6::Sprite sheet)
        : sheet_(std::move(sheet)) {
        parse_tbl(tbl_bytes);
    }

    [[nodiscard]] const dc6::Sprite& sheet() const noexcept { return sheet_; }
    [[nodiscard]] int line_height() const noexcept { return line_height_; }
    [[nodiscard]] std::size_t glyph_count() const noexcept { return glyphs_.size(); }

    // Nullptr when the char isn't in the font — caller decides whether to
    // skip, substitute, or throw.
    [[nodiscard]] const Glyph* find(std::uint16_t code) const {
        auto it = glyphs_.find(code);
        return it == glyphs_.end() ? nullptr : &it->second;
    }

    // Total advance width of `text` (single-line — no newline handling yet).
    [[nodiscard]] int measure(std::string_view text) const {
        int w = 0;
        for (unsigned char c : text) if (auto* g = find(c)) w += g->width;
        return w;
    }

    // Blit `text` at (x, y) into `fb` (row-major RGBA, size fbW*fbH*4).
    // Uses `pal` to map DC6 palette indices; glyph background (index 0) is
    // transparent. y is the TOP of the glyph row. Returns the cursor's
    // final x position.
    int draw(std::vector<std::uint8_t>& fb,
             std::uint32_t fbW, std::uint32_t fbH,
             const palette::Palette& pal,
             int x, int y, std::string_view text) const {
        return draw_tinted(fb, fbW, fbH, pal, x, y, text, 255, 255, 255);
    }

    // Same as draw(), but multiplies each palette-lookup RGB by (tr, tg, tb)
    // / 255. Handy for coloured text without a per-colour font DC6 — pass
    // (255, 200, 60) for a rough gold, (255, 96, 96) for red, etc. Not the
    // same as D2's PL2 hue-shift (which does index remapping), but visually
    // close enough for section headers and highlight rows.
    int draw_tinted(std::vector<std::uint8_t>& fb,
                    std::uint32_t fbW, std::uint32_t fbH,
                    const palette::Palette& pal,
                    int x, int y, std::string_view text,
                    std::uint8_t tr, std::uint8_t tg, std::uint8_t tb) const {
        for (unsigned char c : text) {
            const auto* g = find(c);
            if (!g) continue;
            const auto& fr = sheet_.frame(0, g->frame);
            blit_glyph_tinted(fb, fbW, fbH, pal, fr, x, y, tr, tg, tb);
            x += g->width;
        }
        return x;
    }

private:
    static void blit_glyph_tinted(std::vector<std::uint8_t>& fb,
                                  std::uint32_t fbW, std::uint32_t fbH,
                                  const palette::Palette& pal,
                                  const dc6::Frame& fr,
                                  int dst_x, int dst_y,
                                  std::uint8_t tr, std::uint8_t tg, std::uint8_t tb) {
        for (std::uint32_t gy = 0; gy < fr.height; ++gy) {
            const int py = dst_y + int(gy);
            if (py < 0 || std::uint32_t(py) >= fbH) continue;
            for (std::uint32_t gx = 0; gx < fr.width; ++gx) {
                const int px = dst_x + int(gx);
                if (px < 0 || std::uint32_t(px) >= fbW) continue;
                const auto idx = fr.pixels[gy * fr.width + gx];
                if (idx == 0) continue;
                const auto c = pal[idx];
                auto* p = &fb[(std::size_t(py) * fbW + std::uint32_t(px)) * 4];
                p[0] = std::uint8_t(int(c.r) * tr / 255);
                p[1] = std::uint8_t(int(c.g) * tg / 255);
                p[2] = std::uint8_t(int(c.b) * tb / 255);
                p[3] = c.a;
            }
        }
    }

    void parse_tbl(std::span<const std::byte> b) {
        constexpr std::size_t kHdr = 12;
        constexpr std::size_t kRec = 14;
        if (b.size() < kHdr) throw std::runtime_error("font: truncated header");
        // "Woo!\x01" magic — first 5 bytes.
        if (b[0] != std::byte{0x57} || b[1] != std::byte{0x6f}
            || b[2] != std::byte{0x6f} || b[3] != std::byte{0x21}
            || b[4] != std::byte{0x01})
            throw std::runtime_error("font: bad magic");
        // 7 unknown header bytes follow, then 256 * 14 records.
        std::size_t p = kHdr;
        while (p + kRec <= b.size()) {
            Glyph g;
            g.code   = std::uint16_t(std::uint8_t(b[p + 0])
                                    | (std::uint8_t(b[p + 1]) << 8));
            // p + 2: unknown1
            g.width  = std::uint8_t(b[p + 3]);
            g.height = std::uint8_t(b[p + 4]);
            // p + 5..7: unknown2
            g.frame  = std::uint16_t(std::uint8_t(b[p + 8])
                                    | (std::uint8_t(b[p + 9]) << 8));
            // p + 10..13: unknown3
            glyphs_.emplace(g.code, g);
            if (g.height > line_height_) line_height_ = g.height;
            p += kRec;
        }
    }

    std::unordered_map<std::uint16_t, Glyph> glyphs_;
    dc6::Sprite sheet_;
    int         line_height_ = 0;
};

}  // namespace d2d::font
