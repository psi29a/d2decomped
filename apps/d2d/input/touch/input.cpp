// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/input.hpp"
#include <cstdint>

namespace d2d::client::touch {
    bool StaminaPress::event(std::int64_t finger, Phase phase, float x, float y, const Box& box) {
        if (phase == Phase::Down && owner_ == -1) {
            if (!box.contains(x, y)) return false;
            owner_ = finger;
        }
        if (finger != owner_) return false;
        if (phase == Phase::Cancel) { clear(); return false; }
        if (phase == Phase::Up) {
            const bool activate = inside_ && box.contains(x, y);
            clear();
            return activate;
        }
        if (phase == Phase::Move && !box.contains(x, y)) inside_ = false;
        return false;
    }
} // namespace d2d::client::touch
