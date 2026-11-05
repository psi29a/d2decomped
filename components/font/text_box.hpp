// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::font {

struct TextBoxRow {
    std::string text;
    int x{};
    int baseline{};
};

// Latin kind-4 text boxes: FUN_004fc9b0 / FUN_004fcda0 add and wrap
// rows; FUN_004fbf30 places them. Baselines use the original bottom anchor.
template<class Measure>
auto text_box_rows(std::string_view text, int x, int bottom, int width, int height,
                   int font_height, Measure measure, int margin_x = 0, int margin_y = 0,
                   int spacing = 4) -> std::vector<TextBoxRow> {
    std::vector<std::string_view> pending{ text };
    std::vector<std::string_view> lines;
    std::vector<TextBoxRow> rows;
    const int available_width = width - 2 * margin_x;
    const int pitch = font_height + spacing;
    int remaining_height = height - 2 * margin_y;
    int baseline = bottom - height + margin_y + font_height;
    while (!pending.empty()) {
        auto line = pending.back();
        pending.pop_back();
        const auto first = line.find_first_not_of(" \t\n\r\v\f");
        line = first == std::string_view::npos ? std::string_view{} : line.substr(first);
        if (measure(line) >= available_width && !line.empty()) {
            std::size_t last_space = std::string_view::npos;
            std::size_t cut = line.size();
            for (std::size_t index = 0; index < line.size(); ++index) {
                if (line[index] == ' ') last_space = index;
                if (measure(line.substr(0, index + 1)) >= available_width) {
                    cut = last_space == std::string_view::npos ? index : last_space;
                    break;
                }
            }
            // The original recurses without progress if even the first glyph
            // cannot fit. Keep one glyph in that case instead of hanging.
            cut = std::max(std::size_t{1}, cut);
            if (cut < line.size()) {
                pending.push_back(line.substr(cut));
                pending.push_back(line.substr(0, cut));
                continue;
            }
        }
        lines.push_back(line);
    }
    // FUN_004fbab0 / FUN_004fc9b0 keep the newest rows visible when
    // appending overflows a static box with no scrollbar.
    const int capacity = pitch > 0 ? std::max(1, (remaining_height - pitch) / pitch + 1) : 1;
    const auto first_row = lines.size() > std::size_t(capacity) ? lines.size() - std::size_t(capacity) : 0;
    for (auto index = first_row; index < lines.size(); ++index) {
        const auto line = lines[index];
        rows.push_back({ std::string(line), x + std::max(0, (width - measure(line)) / 2), baseline });
        remaining_height -= pitch;
        if (pitch <= 0 || pitch > remaining_height) break;
        baseline += pitch;
    }
    return rows;
}

} // namespace d2d::font
