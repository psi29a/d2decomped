// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/effects.hpp"
#include "input/touch/input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2d::client::touch {
Box effect_box(const Layout& layout, std::size_t index) {
    return { layout.quest_track.x + layout.quest_track.width + 6.f, std::min(layout.quest_track.y, layout.move.y - 88.f) + float(index) * 44.f, 40.f, 40.f };
}
Box effect_confirm() { return { to_viewport_x(310.f), 300.f, 100.f, 44.f }; }
Box effect_cancel() { return { to_viewport_x(420.f), 300.f, 100.f, 44.f }; }
std::size_t effect_capacity(const Layout& layout) {
    return std::size_t(std::max(0.f, (layout.move.y - effect_box(layout, 0).y) / 44.f));
}
std::size_t effects_per_page(std::size_t count, std::size_t capacity) {
    return count > capacity && capacity > 1 ? capacity - 1 : capacity;
}
std::size_t effect_page_start(std::size_t count, std::size_t capacity, std::size_t page) {
    const auto size = effects_per_page(count, capacity);
    if (size == 0 || count == 0) return 0;
    return page % ((count + size - 1) / size) * size;
}
const game::View::Effect* EffectGesture::current(const std::vector<game::View::Effect>& effects) const {
    const auto found = std::ranges::find_if(effects, [&](const auto& effect) { return effect.kind == kind && effect.instance == instance; });
    return found == effects.end() ? nullptr : &*found;
}
bool EffectGesture::inspecting(std::uint32_t now) const { return finger != -1 && inside && now - down_ms >= 500; }
void EffectGesture::cancel() { *this = {}; }
EffectGesture::Result EffectGesture::event(std::int64_t owner, Phase phase, float x, float y, const Layout& layout,
                                          const std::vector<game::View::Effect>& effects, std::uint32_t now) {
    if (modal) {
        const auto* effect = current(effects);
        if (!effect || !effect->dismissible) { cancel(); return { true, {} }; }
        if (phase == Phase::Down && modal_finger == -1) {
            modal_finger = owner;
            modal_button = effect_confirm().contains(x, y) ? 0 : effect_cancel().contains(x, y) ? 1 : -1;
        }
        if (owner != modal_finger) return { true, {} };
        const bool in_button = modal_button == 0 ? effect_confirm().contains(x, y) : effect_cancel().contains(x, y);
        if (phase == Phase::Move && !in_button) modal_button = -1;
        if (phase == Phase::Cancel) { cancel(); return { true, {} }; }
        if (phase == Phase::Up) {
            const auto dismiss = modal_button == 0 && in_button ? std::optional<Dismiss>{ { effect->kind, effect->instance } } : std::nullopt;
            const bool close = in_button && modal_button >= 0;
            modal_finger = -1;
            if (close) cancel();
            return { true, dismiss };
        }
        return { true, {} };
    }
    const auto capacity = effect_capacity(layout);
    const auto start = effect_page_start(effects.size(), capacity, page);
    const auto count = std::min(effects.size() - start, effects_per_page(effects.size(), capacity));
    if (phase == Phase::Down && finger == -1) {
        if (effects.size() > capacity && capacity > 1 && effect_box(layout, capacity - 1).contains(x, y)) {
            ++page; last_kind = -1; return { true, {} };
        }
        for (std::size_t index = 0; index < count; ++index) if (effect_box(layout, index).contains(x, y)) {
            const auto& effect = effects[start + index];
            finger = owner; kind = effect.kind; instance = effect.instance; down_ms = now; inside = true;
            return { true, {} };
        }
    }
    if (finger == -1 || owner != finger) return {};
    bool in_box = false;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& effect = effects[start + index];
        if (effect.kind == kind && effect.instance == instance) in_box = effect_box(layout, index).contains(x, y);
    }
    inside = inside && in_box;
    if (phase == Phase::Up || phase == Phase::Cancel) {
        const auto* effect = current(effects);
        if (phase == Phase::Up && inside && effect && now - down_ms < 500) {
            if (effect->dismissible && last_kind == kind && last_instance == instance && now - last_ms <= 350) {
                modal = true; last_kind = -1;
            } else { last_kind = kind; last_instance = instance; last_ms = now; }
        } else last_kind = -1;
        finger = -1;
    }
    return { true, {} };
}
} // namespace d2d::client::touch
