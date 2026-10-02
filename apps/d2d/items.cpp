// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for items.hpp: the hover text of items.
#include "items.hpp"

#include "common.hpp"
#include "scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

void draw_hover_text(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<TextLine>& lines,
                     int left, int right, int top, int bottom) {
    if (lines.empty()) return;
    const auto& pal = scene.act1_pal.entries().empty() ? scene.pal : scene.act1_pal;
    const int line_height = 16;                                   // font16 cell height
    int width = 0;
    for (const auto& line : lines) width = std::max(width, scene.font.measure(line.text));
    const int height = line_height * int(lines.size());
    int box_x = std::clamp((left + right) / 2 - width / 2 - 2, 0, std::max(0, int(kScreenWidth) - width - 4));
    int box_y = bottom - height - 2;
    if (box_y < 0) box_y = std::min(top, int(kScreenHeight) - height - 4);
    for (int y = std::max(0, box_y); y < std::min(int(kScreenHeight), box_y + height + 4); ++y)
        for (int x = box_x; x < std::min(int(kScreenWidth), box_x + width + 4); ++x) {
            auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
            pixel[0] = std::uint8_t(pixel[0] / 4); pixel[1] = std::uint8_t(pixel[1] / 4); pixel[2] = std::uint8_t(pixel[2] / 4);
        }
    int y = box_y + 2;
    for (const auto& line : lines) {
        const int line_width = scene.font.measure(line.text);
        const int x = box_x + 2 + (width - line_width) / 2;
        const std::string head = line.text.substr(0, line.split);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, y, head, line.rgb[0], line.rgb[1], line.rgb[2]);
        if (line.split < line.text.size())
            scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + scene.font.measure(head), y, line.text.substr(line.split),
                                   line.tail[0], line.tail[1], line.tail[2]);
        y += line_height;
    }
}

}  // namespace d2d::client
