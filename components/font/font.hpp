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
        auto found = glyphs_.find(code);
        return found == glyphs_.end() ? nullptr : &found->second;
    }

    // Total advance width of `text` (single-line — no newline handling yet).
    [[nodiscard]] int measure(std::string_view text) const {
        int width = 0;
        for (unsigned char letter : text) if (auto* glyph = find(letter)) width += glyph->width;
        return width;
    }

    // Blit `text` at (x, y) into `fb` (row-major RGBA, size fbW*fbH*4).
    // Uses `pal` to map DC6 palette indices; glyph background (index 0) is
    // transparent. y is the TOP of the glyph row. Returns the cursor's
    // final x position.
    int draw(std::vector<std::uint8_t>& framebuffer,
             std::uint32_t fbW, std::uint32_t fbH,
             const palette::Palette& pal,
             int x, int y, std::string_view text) const {
        return draw_tinted(framebuffer, fbW, fbH, pal, x, y, text, 255, 255, 255);
    }

    // Same as draw(), but multiplies each palette-lookup RGB by (tr, tg, tb)
    // / 255. Handy for coloured text without a per-colour font DC6 — pass
    // (255, 200, 60) for a rough gold, (255, 96, 96) for red, etc. Not the
    // same as D2's PL2 hue-shift (which does index remapping), but visually
    // close enough for section headers and highlight rows.
    // Rows outside [clip_y0, clip_y1) are skipped (a scrolling text box).
    int draw_tinted(std::vector<std::uint8_t>& framebuffer,
                    std::uint32_t fbW, std::uint32_t fbH,
                    const palette::Palette& pal,
                    int x, int y, std::string_view text,
                    std::uint8_t tint_red, std::uint8_t tint_green, std::uint8_t tint_blue,
                    int clip_y0 = 0, int clip_y1 = 1 << 30) const {
        for (unsigned char letter : text) {
            const auto* glyph = find(letter);
            if (!glyph) continue;
            const auto& frame = sheet_.frame(0, glyph->frame);
            blit_glyph_tinted(framebuffer, fbW, fbH, pal, frame, x, y, tint_red, tint_green, tint_blue, clip_y0, clip_y1);
            x += glyph->width;
        }
        return x;
    }

private:
    static void blit_glyph_tinted(std::vector<std::uint8_t>& framebuffer,
                                  std::uint32_t fbW, std::uint32_t fbH,
                                  const palette::Palette& pal,
                                  const dc6::Frame& frame,
                                  int dst_x, int dst_y,
                                  std::uint8_t tint_red, std::uint8_t tint_green, std::uint8_t tint_blue,
                                  int clip_y0, int clip_y1) {
        for (std::uint32_t glyph_y = 0; glyph_y < frame.height; ++glyph_y) {
            const int pixel_y = dst_y + int(glyph_y);
            if (pixel_y < 0 || std::uint32_t(pixel_y) >= fbH || pixel_y < clip_y0 || pixel_y >= clip_y1) continue;
            for (std::uint32_t glyph_x = 0; glyph_x < frame.width; ++glyph_x) {
                const int pixel_x = dst_x + int(glyph_x);
                if (pixel_x < 0 || std::uint32_t(pixel_x) >= fbW) continue;
                const auto idx = frame.pixels[glyph_y * frame.width + glyph_x];
                if (idx == 0) continue;
                const auto colour = pal[idx];
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * fbW + std::uint32_t(pixel_x)) * 4];
                pixel[0] = std::uint8_t(int(colour.r) * tint_red / 255);
                pixel[1] = std::uint8_t(int(colour.g) * tint_green / 255);
                pixel[2] = std::uint8_t(int(colour.b) * tint_blue / 255);
                pixel[3] = colour.a;
            }
        }
    }

    void parse_tbl(std::span<const std::byte> bytes) {
        constexpr std::size_t kHdr = 12;
        constexpr std::size_t kRec = 14;
        if (bytes.size() < kHdr) throw std::runtime_error("font: truncated header");
        // "Woo!\x01" magic — first 5 bytes.
        if (bytes[0] != std::byte{0x57} || bytes[1] != std::byte{0x6f}
            || bytes[2] != std::byte{0x6f} || bytes[3] != std::byte{0x21}
            || bytes[4] != std::byte{0x01})
            throw std::runtime_error("font: bad magic");
        // 7 unknown header bytes follow, then 256 * 14 records.
        std::size_t offset = kHdr;
        while (offset + kRec <= bytes.size()) {
            Glyph glyph;
            glyph.code   = std::uint16_t(std::uint8_t(bytes[offset + 0])
                                    | (std::uint8_t(bytes[offset + 1]) << 8));
            // p + 2: unknown1
            glyph.width  = std::uint8_t(bytes[offset + 3]);
            glyph.height = std::uint8_t(bytes[offset + 4]);
            // p + 5..7: unknown2
            glyph.frame  = std::uint16_t(std::uint8_t(bytes[offset + 8])
                                    | (std::uint8_t(bytes[offset + 9]) << 8));
            // p + 10..13: unknown3
            glyphs_.emplace(glyph.code, glyph);
            if (glyph.height > line_height_) line_height_ = glyph.height;
            offset += kRec;
        }
    }

    std::unordered_map<std::uint16_t, Glyph> glyphs_;
    dc6::Sprite sheet_;
    int         line_height_ = 0;
};

}  // namespace d2d::font
