// SPDX-License-Identifier: GPL-3.0-or-later
// A screen blend through the installed PL2 tables. game.exe's renderer draws a
// blended pixel as T[screen * 256 + sprite] (FUN_00608110; FUN_004fb1e0 installs
// the act palette's tables, pl2.md): a palette entry. d2d's screen is RGB, so
// the Blender finds each colour's index back: an entry of the palette exactly
// (the lowest index of that colour; a table row depends only on the colour), any
// other the nearest (FUN_00605210, Palette::nearest), cached. With none
// installed, the RGB formulas the tables were built from (palette.hpp).
#pragma once

#include "palette.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace d2d::palette {

class Blender {
public:
    // The screen holds the palette's colours through the display ramp (Palette::operator[], the gamma
    // and contrast), so the lookup is by the ramped colours and the table's entry comes back ramped:
    // remade whenever the ramp changes. Two entries the ramp makes one colour take the lower's row.
    explicit Blender(const Pl2& pl2) : pl2_(&pl2), base_(pl2.base_palette()) {}
    // `screen` under `sprite` as the table has it.
    [[nodiscard]] Rgba mix(Blend mode, Rgba screen, Rgba sprite) {
        ramp_up();
        auto colour = ramped_.entries()[pl2_->blend(mode, index_of(screen), index_of(sprite))];
        colour.a = 0xFF;
        return colour;
    }
    [[nodiscard]] std::uint8_t index_of(Rgba colour) {
        ramp_up();
        const auto [found, made] = indices_.try_emplace(key(colour), std::uint8_t(0));
        if (made) found->second = ramped_.nearest(colour.r, colour.g, colour.b);
        return found->second;
    }

private:
    static std::uint32_t key(Rgba colour) { return std::uint32_t(colour.r) << 16 | std::uint32_t(colour.g) << 8 | colour.b; }
    void ramp_up() {
        if (built_ && gamma_ == Palette::gamma() && contrast_ == Palette::contrast()) return;
        built_ = true; gamma_ = Palette::gamma(); contrast_ = Palette::contrast();
        std::array<Rgba, 256> ramped{};
        for (std::size_t index = 0; index < 256; ++index) ramped[index] = base_[std::uint8_t(index)];
        ramped_ = Palette(ramped);
        indices_.clear();
        for (int index = 255; index >= 0; --index) indices_[key(ramped[std::size_t(index)])] = std::uint8_t(index);
    }
    const Pl2* pl2_;
    Palette base_, ramped_;
    bool built_ = false;
    int gamma_ = 0, contrast_ = 0;
    std::unordered_map<std::uint32_t, std::uint8_t> indices_;
};

// The blender in use (the act palette's), null for none.
inline Blender*& installed_blender() noexcept {
    static Blender* blender = nullptr;
    return blender;
}

// `screen` under `sprite` in `mode`: through the installed tables, else in RGB.
inline Rgba blend(Blend mode, Rgba screen, Rgba sprite) {
    if (auto* blender = installed_blender()) return blender->mix(mode, screen, sprite);
    switch (mode) {
        case Blend::kAlpha75: return alpha_blend(screen, sprite, kAlphaThreeQuarters);
        case Blend::kAlpha50: return alpha_blend(screen, sprite, kAlphaHalf);
        case Blend::kAlpha25: return alpha_blend(screen, sprite, kAlphaQuarter);
        case Blend::kAdditive: return additive_blend(screen, sprite);
        case Blend::kMultiply: return multiply_blend(screen, sprite);
        case Blend::kMaxComponent: return max_component_blend(screen, sprite, max_component_weight(sprite));
    }
    return sprite;
}

}  // namespace d2d::palette
