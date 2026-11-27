// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/ui_sound.hpp"
#include <cstddef>
#include <cstdint>

namespace d2d::client::touch {
    bool UiClickThrottle::accept(UiClick action, int destination, std::uint32_t now_ms) {
        auto& last = clicks_[std::size_t(action)];
        if (last.has_time && last.destination == destination && now_ms - last.time < kRepeatMs) return false;
        last = { destination, now_ms, true };
        return true;
    }

} // namespace d2d::client::touch
