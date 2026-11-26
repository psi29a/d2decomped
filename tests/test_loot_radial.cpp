// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// test-sources: apps/d2d/input/loot_radial.cpp
// Pure controller input decisions; no game parity claim.
#include "input/loot_radial.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>

int main() try {
    using d2d::client::loot_radial::Model;
    constexpr float angle_pi = d2d::client::loot_radial::kPi;
    const std::array<int, 10> ids{101, 102, 103, 104, 105, 106, 107, 108, 201, 202};
    Model model(ids);
    assert(model.selected_id() == -1); // no default item highlight
    assert(model.step(0.f, -1.f) == -1);
    assert(model.step(std::numeric_limits<float>::quiet_NaN(), 0.f) == -1);
    assert(model.step(0.f, 0.f) == -1); // malformed sample disarms the flick
    assert(model.step(0.2f, 0.0f) == -1);
    assert(model.step(0.8f, 0.0f) == -1);
    assert(model.selected_sector() == 2); // right side, eight-sector first page
    assert(model.selected_id() == 103);
    assert(model.step(0.1f, 0.0f) == 103); // neutral return commits once
    assert(model.step(0.0f, 0.0f) == -1);

    // Small angular movement around a sector boundary retains the current highlight.
    assert(model.step(std::cos(-angle_pi / 4.0f), std::sin(-angle_pi / 4.0f)) == -1);
    const int stable_sector = model.selected_sector();
    assert(model.step(std::cos(-angle_pi / 4.0f + 0.04f), std::sin(-angle_pi / 4.0f + 0.04f)) == -1);
    assert(model.selected_sector() == stable_sector);
    model.cancel();
    assert(model.step(0.8f, 0.0f) == -1);
    assert(model.step(0.0f, 0.0f) == -1); // canceled hold cannot later commit

    // Explicit paging never commits the old or newly highlighted item on release.
    assert(model.step(0.8f, 0.0f) == -1);
    model.change_page(1);
    assert(model.page() == 1 && model.page_size() == 2);
    assert(model.selected_id() == -1);
    assert(model.step(0.1f, 0.0f) == -1);
    assert(model.step(0.8f, 0.0f) == -1);
    assert(model.selected_id() == 202);
    assert(model.step(0.1f, 0.0f) == 202);

    // Snapshot owns stable IDs; reset swaps it and clears page and selection.
    const std::array<int, 1> replacement{777};
    model.reset(replacement);
    assert(model.page() == 0 && model.page_count() == 1);
    assert(model.selected_id() == -1);
    assert(model.step(0.0f, -0.8f) == -1);
    assert(model.selected_id() == 777);
    assert(model.step(0.0f, 0.0f) == 777);
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_loot_radial: %s\n", error.what());
    return 1;
}
