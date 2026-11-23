// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "skills/pad.hpp"
#include "touch/input.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2d::client::touch {

inline constexpr Box kBindingPrevious{ 250, 396, 64, 64 };
inline constexpr Box kBindingNext{ 330, 396, 64, 64 };

// Touch pages are fixed groups, independent of the controller wheel's sliding window.
struct BindingPicker {
    int page = 0, selected = -1;
    std::int64_t finger = -1;
    bool hit = false, from_hub = false;

    static int pages(int count) {
        return std::max(1, (std::max(0, count) + skillpad::kWheelShown - 1) / skillpad::kWheelShown);
    }
    skillpad::WheelPage visible(int count) const {
        const int start = std::clamp(page, 0, pages(count) - 1) * skillpad::kWheelShown;
        return { start, std::min(skillpad::kWheelShown, std::max(0, count - start)) };
    }
    void clear_press() { selected = -1; finger = -1; hit = from_hub = false; }
    void turn(int count, int direction) {
        const int total = pages(count);
        page = (std::clamp(page, 0, total - 1) + (direction < 0 ? total - 1 : 1)) % total;
        clear_press();
    }
    void begin(std::int64_t owner, float dx, float dy) {
        clear_press();
        finger = owner;
        from_hub = std::hypot(dx, dy) <= skillpad::kBindWheelHub;
    }
    void track(float dx, float dy, int count) {
        const auto range = visible(count);
        int closest = -1;
        float distance = skillpad::kBindWheelIconR;
        for (int slot = 0; slot < range.shown; ++slot) {
            const float angle = skillpad::wedge_center(slot, range.shown);
            const float next = std::hypot(dx - std::cos(angle) * skillpad::kBindWheelRx,
                                          dy - std::sin(angle) * skillpad::kBindWheelRy);
            if (next < distance) { distance = next; closest = slot; }
        }
        if (closest < 0 && from_hub)
            closest = skillpad::wedge(dx, dy, range.shown, skillpad::kBindWheelHub);
        if (closest >= 0) { selected = range.start + closest; hit = true; }
    }
};

} // namespace d2d::client::touch
