// SPDX-License-Identifier: GPL-3.0-or-later
// Shared display and release validation for predicted Teleport destinations.
#pragma once

#include "touch/input.hpp"

#include <utility>


namespace d2d::client {

std::pair<float, float> teleport_stick_drag(float x, float y, float magnitude);

struct TeleportAim {
    int button = -1, item = -1;
    touch::Target preview{}, displayed{};
    float dx = 0, dy = 0;
    bool manual = false, shown = false;

    void clear() { *this = {}; }
    void display() { displayed = preview; shown = true; }
    bool matches(const touch::Target& current) const;
};

} // namespace d2d::client
