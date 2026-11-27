// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// test-sources: apps/d2d/input/touch/ui_sound.cpp
#include "input/touch/ui_sound.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client::touch;
    UiClickThrottle clicks;

    // Faster menu transitions still sound when they lead somewhere different.
    assert(clicks.accept(UiClick::Back, 1, 0));
    assert(!clicks.accept(UiClick::Back, 1, 20));
    assert(clicks.accept(UiClick::Back, 2, 30));
    assert(!clicks.accept(UiClick::Back, 2, 35));
    assert(clicks.accept(UiClick::Inventory, 1, 36));
    assert(clicks.accept(UiClick::Inventory, 0, 40));

    // Destination selection, paging, saving and leaving the editor can all
    // happen quickly. A new step must not be throttled by the preceding one.
    assert(clicks.accept(UiClick::Bind, 1, 200));
    assert(clicks.accept(UiClick::Destination, 0, 205));
    assert(!clicks.accept(UiClick::Destination, 0, 210));
    assert(clicks.accept(UiClick::Destination, 2, 215));
    assert(clicks.accept(UiClick::WheelPage, 1, 220));
    assert(clicks.accept(UiClick::Assignment, 2, 225));
    assert(!clicks.accept(UiClick::Assignment, 2, 230));
    assert(clicks.accept(UiClick::Back, 0, 235));
    assert(clicks.accept(UiClick::Assignment, 2, 325));

    UiClickThrottle early;
    assert(early.accept(UiClick::Destination, -1, 0));
    assert(!early.accept(UiClick::Destination, -1, UiClickThrottle::kRepeatMs - 1));
    assert(early.accept(UiClick::Destination, -1, UiClickThrottle::kRepeatMs));

    UiClickThrottle wrap;
    constexpr std::uint32_t first = UINT32_MAX - 49;
    assert(wrap.accept(UiClick::BindSet, 1, first));
    assert(!wrap.accept(UiClick::BindSet, 1, 49));
    assert(wrap.accept(UiClick::BindSet, 1, 50));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
