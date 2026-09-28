// Definitions for items.hpp: the hover text of items.
#include "items.hpp"
#include "common.hpp"
#include "scene.hpp"

namespace d2d::client {

void draw_hover_text(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<TextLine>& lines,
                     int x0, int x1, int top, int bottom) {
    if (lines.empty()) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int lh = 16;                                   // font16 cell height
    int w = 0;
    for (const auto& l : lines) w = std::max(w, s.font.measure(l.text));
    const int h = lh * int(lines.size());
    int bx = std::clamp((x0 + x1) / 2 - w / 2 - 2, 0, std::max(0, int(kW) - w - 4));
    int by = bottom - h - 2;
    if (by < 0) by = std::min(top, int(kH) - h - 4);
    for (int y = std::max(0, by); y < std::min(int(kH), by + h + 4); ++y)
        for (int x = bx; x < std::min(int(kW), bx + w + 4); ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 4); p[1] = std::uint8_t(p[1] / 4); p[2] = std::uint8_t(p[2] / 4);
        }
    int y = by + 2;
    for (const auto& l : lines) {
        const int lw = s.font.measure(l.text);
        s.font.draw_tinted(fb, kW, kH, pal, bx + 2 + (w - lw) / 2, y, l.text, l.rgb[0], l.rgb[1], l.rgb[2]);
        y += lh;
    }
}

}  // namespace d2d::client
