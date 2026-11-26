// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/loot_chooser.hpp"
#include "input/touch/input.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace d2d::client {

void LootChooser::clear() {
    const bool closed = controller_closed || (open && !touch);
    *this = {};
    controller_closed = closed;
}

bool LootChooser::take_controller_close() {
    return std::exchange(controller_closed, false);
}

void LootChooser::start(std::vector<LootChoice> items, bool use_touch) {
    clear(); choices = std::move(items); touch = use_touch; open = true;
    std::vector<int> ids; ids.reserve(choices.size());
    for (const auto& choice : choices) ids.push_back(choice.id);
    radial.reset(ids);
}

touch::Box LootChooser::list_box() {
    const float width = std::min(360.f, 800.f - touch::safe_inset.left - touch::safe_inset.right - 24.f);
    return { std::clamp(800.f - touch::safe_inset.right - width - 18.f, touch::safe_inset.left + 6.f,
                        800.f - touch::safe_inset.right - width - 6.f),
             touch::safe_inset.top + 150.f, width, 288.f };
}

int LootChooser::touch_event(std::int64_t id, touch::Phase phase, float x, float y) {
    const auto box = list_box();
    if (phase == touch::Phase::Down && finger == -1 && box.contains(x, y)) {
        finger = id; start_x = x; start_y = y; moved = false;
        row = box.contains(x, y) ? int((y - box.y) / 48.f) : -1;
    }
    if (id != finger) return -1;
    if (phase == touch::Phase::Move && (!box.contains(x, y) || std::abs(y - start_y) > 12.f || std::abs(x - start_x) > 12.f)) moved = true;
    if (phase == touch::Phase::Cancel) { finger = -1; return -1; }
    if (phase != touch::Phase::Up) return -1;
    const int hit = box.contains(x, y) ? int((y - box.y) / 48.f) : -1;
    int result = -1;
    if (moved || std::abs(y - start_y) > 12.f || std::abs(x - start_x) > 12.f) {
        const int max_scroll = std::max(0, int(choices.size()) - 6);
        const int delta = int(std::round((start_y - y) / 48.f));
        scroll = std::size_t(std::clamp(int(scroll) + delta, 0, max_scroll));
    } else if (row >= 0 && hit == row && scroll + std::size_t(row) < choices.size()) result = choices[scroll + std::size_t(row)].id;
    finger = -1;
    return result;
}

} // namespace d2d::client
