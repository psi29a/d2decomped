// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "input/touch/input.hpp"

#include <world.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2d::client::touch {
Box effect_box(const Layout& layout, std::size_t index);
Box effect_confirm();
Box effect_cancel();
std::size_t effect_capacity(const Layout& layout);
std::size_t effects_per_page(std::size_t count, std::size_t capacity);
std::size_t effect_page_start(std::size_t count, std::size_t capacity, std::size_t page);

// Captures an effect instance, never a row index; replacement invalidates a pending action.
struct EffectGesture {
    struct Dismiss { int kind; std::uint64_t instance; };
    struct Result { bool consumed = false; std::optional<Dismiss> dismiss; };
    std::size_t page = 0;
    std::int64_t finger = -1;
    int kind = -1, last_kind = -1;
    std::uint64_t instance = 0, last_instance = 0;
    std::uint32_t down_ms = 0, last_ms = 0;
    bool modal = false, inside = true;
    std::int64_t modal_finger = -1;
    int modal_button = -1;
    const game::View::Effect* current(const std::vector<game::View::Effect>& effects) const;
    bool inspecting(std::uint32_t now) const;
    Result event(std::int64_t owner, Phase phase, float x, float y, const Layout& layout,
                 const std::vector<game::View::Effect>& effects, std::uint32_t now);
    void cancel();
};
} // namespace d2d::client::touch
