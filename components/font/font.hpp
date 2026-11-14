// SPDX-License-Identifier: GPL-3.0-or-later
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
#include <string>
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
    // Byte 10 of the .tbl, the height game.exe centres a button label by
    // (FUN_00501a40).
    [[nodiscard]] int tbl_height() const noexcept { return tbl_height_; }
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

    // FUN_00501a80 hands glyphs to FUN_004f64b0 at a bottom-left anchor.
    // Unlike draw_tinted, each frame's own height determines its top.
    int draw_tinted_baseline(std::vector<std::uint8_t>& framebuffer,
                             std::uint32_t framebuffer_width, std::uint32_t framebuffer_height,
                             const palette::Palette& palette, int x, int baseline,
                             std::string_view text, std::uint8_t tint_red,
                             std::uint8_t tint_green, std::uint8_t tint_blue) const {
        for (unsigned char letter : text) {
            const auto* glyph = find(letter);
            if (!glyph) continue;
            const auto& frame = sheet_.frame(0, glyph->frame);
            blit_glyph_tinted(framebuffer, framebuffer_width, framebuffer_height, palette, frame,
                             x + frame.offset_x, baseline + frame.offset_y - int(frame.height) + 1,
                             tint_red, tint_green, tint_blue, 0, 1 << 30);
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
        // 7 header bytes follow (byte 10 the font's height), then 256 * 14 records.
        tbl_height_ = std::uint8_t(bytes[10]);
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
    int         tbl_height_ = 0;
};

// The edit box's wrap (FUN_00502970, Src/D2WinFont.cpp). One byte is one
// code unit, which is the Latin wchar the box copies. `glyph_width` is
// the font record's byte +3 (FUN_00501730 via DAT_00841da0). A line feed
// adds no width. 0xff, 'c', and '0'..'6' add no width when three count
// slots remain (the measure's EBX starts at 3). A prefix fits when its
// width is less than or equal to `box_width`. The walk back stops where
// FUN_00526d30 returns 1. `language_id` is FUN_00525150 (0 when the caller
// leaves it unset). Ids other than 6, 8 and 9, including 7: the unit at
// the index is not _isspace (00687006; DAT_0096dfe4 is 0, ctype bit 8,
// codes 9..13 and 32) and the previous unit is. That space stays on the
// line. Ids 6, 8 and 9 read only the unit at the index: return 0 for an
// ASCII letter or digit, and 1 for any other byte. FUN_005028e0 with
// flag 1 then drops one leading space. No such break: the line is the
// longest prefix that fits, and one glyph if even that is over.
// ponytail: those ids also return 0 for the 16-bit units 0x2026, 0x3001,
// 0x3002, 0xff01, 0xff0c, 0xff0e, 0xff1f, 0xff61, 0xff64, and for
// 0xff10..0xff19, 0xff21..0xff3a and 0xff41..0xff5a. A byte unit never
// holds them.
template <class GlyphWidth>
auto wrap_edit_box(std::string_view text, int box_width, GlyphWidth glyph_width, int language_id = 0) -> std::vector<std::string> {
    auto space_unit = [](unsigned char code) {
        return code == ' ' || (code >= 9 && code <= 13);
    };
    auto unit_at = [&](int pos) -> unsigned char {
        if (pos < 0 || pos >= int(text.size())) return 0;
        return static_cast<unsigned char>(text[std::size_t(pos)]);
    };
    auto measured = [&](int origin, int count) {
        if (origin >= int(text.size()) || unit_at(origin) == 0) return 0;
        int total = 0;
        int index = 0;
        int budget = 3;
        int pos = origin;
        while (index < count && pos < int(text.size())) {
            const auto code = unit_at(pos);
            if (code == 0) break;
            if (code == 10) {
                ++pos;
                ++index;
                ++budget;
                continue;
            }
            const bool colour = budget < count && code == 0xff && unit_at(pos + 1) == 'c'
                && unit_at(pos + 2) >= '0' && unit_at(pos + 2) <= '6';
            if (colour) {
                pos += 3;
                index += 3;
                budget += 3;
                continue;
            }
            total += glyph_width(code);
            ++pos;
            ++index;
            ++budget;
        }
        return total;
    };
    const bool other_break = language_id == 6 || language_id == 8 || language_id == 9;
    auto kept_break = [&](int origin, int index) {
        if (index == 0) return true;
        const auto unit = unit_at(origin + index);
        if (other_break) {
            const bool letter = (unit >= 'A' && unit <= 'Z') || (unit >= 'a' && unit <= 'z');
            const bool digit = unit >= '0' && unit <= '9';
            return !letter && !digit;
        }
        if (space_unit(unit)) return false;
        return space_unit(unit_at(origin + index - 1));
    };
    auto copy_line = [&](int origin, int count) {
        if (origin < int(text.size()) && count > 0 && space_unit(unit_at(origin))) {
            ++origin;
            --count;
        }
        if (origin >= int(text.size()) || count <= 0) return std::string{};
        const auto available = int(text.size()) - origin;
        const int take = count < available ? count : available;
        auto slice = text.substr(std::size_t(origin), std::size_t(take));
        const auto end = slice.find('\0');
        if (end != std::string_view::npos) slice = slice.substr(0, end);
        return std::string(slice);
    };
    if (measured(0, int(text.size())) <= box_width) return { std::string(text) };

    std::vector<std::string> lines;
    const int length = int(text.size());
    int start = 0;
    while (start <= length) {
        int end = length;
        int measured_width = 0;
        if (length > start) {
            const int base = 1 - start;
            while (true) {
                measured_width = measured(start, base + end);
                if (measured_width <= box_width) break;
                --end;
                if (end <= start) break;
            }
            if (measured_width <= box_width && end > start) {
                int mark = end - start + 1;
                while (end > start) {
                    if (end == length || kept_break(start, mark)) break;
                    --end;
                    --mark;
                }
            }
        }
        if (end == start && length > start) {
            const int base = 1 - start;
            end = length;
            while (end > start) {
                if (measured(start, base + end) <= box_width) break;
                --end;
            }
        }
        lines.push_back(copy_line(start, end - start + 1));
        start = end + 1;
    }
    return lines;
}

}  // namespace d2d::font
