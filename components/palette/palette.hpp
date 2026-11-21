// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp palette (.dat / .pl2) reader.
//
// D2 stores palettes in two forms:
//   * pal.dat  — 768 bytes: 256 packed RGB triples (b, g, r) per DAT layout,
//                no header.
//   * Pal.PL2  — 443,175 bytes: 256 RGBA entries at offset 0, then a
//                mountain of colormaps (light levels, tints, alpha blends,
//                additive/multiplicative, hue variations, …). See the Pl2
//                class below for the subset we parse.
//
// D2 convention: palette index 0 is transparent for sprite blits. All other
// entries are fully opaque. Callers who want a different transparency
// scheme can override alpha post-construction.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace d2d::palette {

// DirectDraw's Video Options range (FUN_0047d230, FUN_0047d2f0). 155 and 100
// are the shipped defaults (FUN_004f8ea0). Gamma 100 is the flat ramp.
inline constexpr int kGammaSteps = 21, kGammaMin = 55, kGammaMax = 255, kDefaultGamma = 155;
inline constexpr int kContrastSteps = 100, kContrastMin = 0, kContrastMax = 100, kDefaultContrast = 100;

// FUN_0047cc90 / FUN_0047cd00, truncated. Contrast 100 is position 99 and
// saves back as 99; gamma 155 is position 10 of 0..20.
constexpr int slider_position(int steps, int lowest, int highest, int value) noexcept {
    return (steps - 1) * (value - lowest + 1) / (highest - lowest);
}
constexpr int slider_value(int steps, int lowest, int highest, int position) noexcept {
    return lowest + (highest - lowest) / (steps - 1) * position;
}

// FUN_006be190: 256 byte entries, top 255. At contrast 100 the power term
// is zero and the converter's truncation is min(255, i * gamma / 100).
// Below that it blends the line with pow(i / 255, 100 / gamma) * 255.
inline std::array<std::uint8_t, 256> make_ramp(int gamma, int contrast) noexcept {
    std::array<std::uint8_t, 256> level{};
    const double gamma_scale = gamma / 100.0;
    const double exponent = 100.0 / gamma;
    for (int index = 0; index < 256; ++index) {
        int out;
        if (contrast == kContrastMax) {
            out = std::min(255, index * gamma / 100);
        } else {
            const double linear = std::min(index * gamma_scale, 255.0);
            const double powered = std::pow(index / 255.0, exponent) * 255.0;
            const double mixed = (linear * contrast + powered * (kContrastMax - contrast)) / 100.0;
            out = mixed > 0.0 ? static_cast<int>(mixed) : 0;
            out = std::min(out, 255);
        }
        level[std::size_t(index)] = std::uint8_t(out);
    }
    return level;
}

struct VideoRamp {
    std::array<std::uint8_t, 256> level{};
    int gamma = kDefaultGamma;
    int contrast = kDefaultContrast;
};

inline VideoRamp& video_ramp() {
    static VideoRamp ramp = [] {
        VideoRamp built;
        built.level = make_ramp(kDefaultGamma, kDefaultContrast);
        return built;
    }();
    return ramp;
}

struct Rgba {
    std::uint8_t r{}, g{}, b{}, a{};
    constexpr bool operator==(const Rgba&) const = default;
};

class Palette {
public:
    Palette() = default;

    // Accepts a 768-byte pal.dat (BGR triples: game.exe 1.14d
    // FUN_004fb1e0 / FUN_00605210; docs/research/re/palette.md) or a .PL2
    // (RGBA at offset 0). Auto-detects layout from total size: exactly 768 →
    // BGR-packed DAT; 4-byte stride → RGBA PL2. Larger DAT-style buffers are
    // truncated; short buffers throw.
    explicit Palette(std::span<const std::byte> bytes) {
        // PL2 files start with 256 * 4-byte RGBA (unused alpha byte). Any
        // buffer at least that big AND longer than the plain 768-byte DAT is
        // parsed as PL2. Everything else is BGR-packed DAT.
        if (bytes.size() >= 256u * 4u && bytes.size() > 256u * 3u) {
            for (std::size_t i = 0; i < 256; ++i) {
                entries_[i] = Rgba{
                    std::uint8_t(bytes[i * 4 + 0]),   // R
                    std::uint8_t(bytes[i * 4 + 1]),   // G
                    std::uint8_t(bytes[i * 4 + 2]),   // B
                    std::uint8_t(i == 0 ? 0 : 0xFF),
                };
            }
            return;
        }
        constexpr std::size_t kNeed = 256 * 3;
        if (bytes.size() < kNeed)
            throw std::runtime_error("palette: need 768 bytes, got fewer");
        for (std::size_t i = 0; i < 256; ++i) {
            // DAT layout is (B, G, R) per index (game.exe evidence above).
            entries_[i] = Rgba{
                std::uint8_t(bytes[i * 3 + 2]),   // R
                std::uint8_t(bytes[i * 3 + 1]),   // G
                std::uint8_t(bytes[i * 3 + 0]),   // B
                std::uint8_t(i == 0 ? 0 : 0xFF),
            };
        }
    }

    explicit Palette(const std::array<Rgba, 256>& entries) : entries_(entries) {}

    // File colour through the DirectDraw ramp (FUN_006be190). Index 0 stays
    // transparent: alpha is the file's. entries() is the file palette.
    // The entry as drawn into the frame: the file's colour. The display ramp (gamma, contrast) is
    // DirectDraw's on the whole picture (FUN_006be190), put on as the frame is shown (ramp_rgba).
    [[nodiscard]] Rgba operator[](std::uint8_t idx) const noexcept { return entries_[idx]; }
    // A colour through the display ramp.
    [[nodiscard]] static Rgba ramp(Rgba colour) noexcept {
        const auto& level = video_ramp().level;
        colour.r = level[colour.r];
        colour.g = level[colour.g];
        colour.b = level[colour.b];
        return colour;
    }
    // An RGBA picture through the display ramp, in place (the frame as it goes to the screen).
    static void ramp_rgba(std::span<std::uint8_t> rgba) noexcept {
        const auto& level = video_ramp().level;
        for (std::size_t at = 0; at + 3 < rgba.size(); at += 4) {
            rgba[at] = level[rgba[at]];
            rgba[at + 1] = level[rgba[at + 1]];
            rgba[at + 2] = level[rgba[at + 2]];
        }
    }
    [[nodiscard]] std::span<const Rgba> entries() const noexcept { return entries_; }
    // Rebuild FUN_006be190. The next frame shown takes it.
    static void set_ramp(int gamma, int contrast) noexcept {
        gamma = std::clamp(gamma, kGammaMin, kGammaMax);
        contrast = std::clamp(contrast, kContrastMin, kContrastMax);
        auto& ramp = video_ramp();
        ramp.level = make_ramp(gamma, contrast);
        ramp.gamma = gamma;
        ramp.contrast = contrast;
    }
    [[nodiscard]] static int gamma() noexcept { return video_ramp().gamma; }
    [[nodiscard]] static int contrast() noexcept { return video_ramp().contrast; }
    // The index whose colour is nearest (r, g, b) (FUN_004fb180 ->
    // FUN_00605210): the least squared distance, the first on a tie.
    [[nodiscard]] std::uint8_t nearest(int red, int green, int blue) const noexcept {
        std::uint8_t found = 0;
        unsigned best = ~0u;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& colour = entries_[i];
            const auto off_r = unsigned(colour.r - red), off_g = unsigned(colour.g - green), off_b = unsigned(colour.b - blue);
            if (const unsigned distance = off_b * off_b + off_g * off_g + off_r * off_r; distance < best) { best = distance; found = std::uint8_t(i); }
        }
        return found;
    }

    // Overwrite the alpha for one entry — handy when a specific tileset
    // treats a non-zero index as chroma-key.
    void set_alpha(std::uint8_t idx, std::uint8_t alpha) noexcept {
        entries_[idx].a = alpha;
    }

private:
    std::array<Rgba, 256> entries_{};
};

// The PL2 blend tables' colours (docs/research/formats/pl2.md). game.exe
// stores the nearest palette entry (FUN_00605210) of each; blend.hpp draws
// that entry when a PL2 is installed, these colours when none is. Each
// channel is truncated.
//
// Alpha (FUN_00605880): (screen * (255 - w) + sprite * w) / 255. The three
// tables' sprite weights, +0x3500 / +0x13500 / +0x23500.
inline constexpr int kAlphaThreeQuarters = 0xbf, kAlphaHalf = 0x7f, kAlphaQuarter = 0x3f;
[[nodiscard]] constexpr Rgba alpha_blend(const Rgba& screen, const Rgba& sprite, int weight) noexcept {
    const auto mix = [weight](int under, int over) { return std::uint8_t((under * (255 - weight) + over * weight) / 255); };
    return Rgba{ mix(screen.r, sprite.r), mix(screen.g, sprite.g), mix(screen.b, sprite.b), 0xFF };
}
// Additive, +0x33500 (FUN_006059b0): the sum, 255 at most.
[[nodiscard]] constexpr Rgba additive_blend(const Rgba& screen, const Rgba& sprite) noexcept {
    const auto mix = [](int under, int over) { return std::uint8_t(std::min(255, under + over)); };
    return Rgba{ mix(screen.r, sprite.r), mix(screen.g, sprite.g), mix(screen.b, sprite.b), 0xFF };
}
// Multiply, +0x43500 (FUN_00605aa0): screen * sprite / 255.
[[nodiscard]] constexpr Rgba multiply_blend(const Rgba& screen, const Rgba& sprite) noexcept {
    const auto mix = [](int under, int over) { return std::uint8_t(under * over / 255); };
    return Rgba{ mix(screen.r, sprite.r), mix(screen.g, sprite.g), mix(screen.b, sprite.b), 0xFF };
}
// COF draw effect 6's blend (PL2 +0x5b500, FUN_00605b70): alpha with the
// sprite's greatest channel as its weight, so black leaves the screen and a
// full channel covers it.
[[nodiscard]] constexpr int max_component_weight(const Rgba& sprite) noexcept {
    return std::max({ int(sprite.r), int(sprite.g), int(sprite.b) });
}
[[nodiscard]] constexpr Rgba max_component_blend(const Rgba& screen, const Rgba& sprite, int weight) noexcept {
    return alpha_blend(screen, sprite, weight);
}

// The five 256 x 256 blend tables a draw can go through (pl2.md): the three
// alphas (the sprite's share 75 / 50 / 25 %), additive, multiply, and the
// max-component one (COF draw effect 6).
enum class Blend : std::uint8_t { kAlpha75, kAlpha50, kAlpha25, kAdditive, kMultiply, kMaxComponent };

// PL2 colormap file. We currently parse:
//   * base 256 RGBA at offset 0                    → base_palette()
//   * additive[256][256] at offset 0x33500         → additive(fg, bg)
//   * 50/50 alpha at offset 0x13500 (weight 0x7f)  → blend50(fg, bg)
//
// The additive table is D2's TRANS_ADDITIVE mode — the fire, spell VFX,
// and shrine glows all blend through it. Given (foreground_idx, bg_idx)
// it returns the palette index whose colour has the clamped per-channel sum
// of the two — palette-preserving, unlike direct RGB addition which drifts
// off the palette and looks off-key.
//
// blend50 is game.exe's installed 0x7fe468 (FUN_004fb1e0) and max_blend its
// 0x831b68: tools/emu pl2.py diffs both (docs/research/formats/pl2.md); blend()
// reads all five, as blend.hpp's Blender draws them.
class Pl2 {
public:
    static constexpr std::size_t kFileSize        = 443175;
    static constexpr std::size_t kBaseOff         = 0;
    static constexpr std::size_t kAdditiveOff     = 0x33500;
    static constexpr std::size_t kBlend50Off      = 0x13500;
    static constexpr std::size_t kMaxBlendOff     = 0x5b500;   // max_component_blend's table
    static constexpr std::size_t kAlpha75Off      = 0x3500;
    static constexpr std::size_t kAlpha25Off      = 0x23500;
    static constexpr std::size_t kMultiplyOff     = 0x43500;
    // The 13 text colours' index shifts (FUN_004fb1e0 copies PL2 +0x6b627
    // to 0x7d5668 and hands it to the driver: a glyph's index i draws as
    // shift[colour][i]); their RGBs at +0x6b600 (0x7d563c).
    static constexpr std::size_t kTextShiftOff    = 0x6b627;

    Pl2() = default;

    explicit Pl2(std::span<const std::byte> bytes) {
        if (bytes.size() < kAdditiveOff + 256u * 256u)   // the later of the two tables
            throw std::runtime_error("PL2: buffer too small");
        for (std::size_t i = 0; i < 256; ++i) {
            base_[i] = Rgba{
                std::uint8_t(bytes[i * 4 + 0]),
                std::uint8_t(bytes[i * 4 + 1]),
                std::uint8_t(bytes[i * 4 + 2]),
                std::uint8_t(i == 0 ? 0 : 0xFF),
            };
        }
        for (std::size_t i = 0; i < 256 * 256; ++i) {
            additive_[i] = std::uint8_t(bytes[kAdditiveOff + i]);
            blend50_[i]  = std::uint8_t(bytes[kBlend50Off  + i]);
        }
        if (bytes.size() >= kMaxBlendOff + 256u * 256u)
            for (std::size_t i = 0; i < 256u * 256u; ++i) {
                max_blend_[i] = std::uint8_t(bytes[kMaxBlendOff + i]);
                alpha75_[i] = std::uint8_t(bytes[kAlpha75Off + i]);
                alpha25_[i] = std::uint8_t(bytes[kAlpha25Off + i]);
                multiply_[i] = std::uint8_t(bytes[kMultiplyOff + i]);
            }
        if (bytes.size() >= kTextShiftOff + 13u * 256u)
            for (std::size_t i = 0; i < 13u * 256u; ++i) text_shift_[i] = std::uint8_t(bytes[kTextShiftOff + i]);
    }
    // `pal` as text colour `colour` (0..12) draws it: each index through its shift.
    [[nodiscard]] Palette text_palette(const Palette& pal, int colour) const {
        std::array<Rgba, 256> entries{};
        for (std::size_t i = 0; i < 256; ++i) entries[i] = pal.entries()[text_shift_[std::size_t(colour % 13) * 256 + i]];
        entries[0].a = 0;
        return Palette(entries);
    }

    [[nodiscard]] const std::array<Rgba, 256>& base_palette() const noexcept {
        return base_;
    }

    // additive(fg, bg) -> palette index that most closely represents
    // fg + bg per Blizzard's authored LUT. Symmetric.
    [[nodiscard]] std::uint8_t additive(std::uint8_t foreground, std::uint8_t background) const noexcept {
        return additive_[std::size_t(foreground) * 256 + background];
    }
    [[nodiscard]] std::uint8_t blend50(std::uint8_t foreground, std::uint8_t background) const noexcept {
        return blend50_[std::size_t(foreground) * 256 + background];
    }
    // The renderer reads T[screen * 256 + sprite] (FUN_00608110).
    [[nodiscard]] std::uint8_t max_blend(std::uint8_t screen, std::uint8_t sprite) const noexcept {
        return max_blend_[std::size_t(screen) * 256 + sprite];
    }
    // Any of the blend tables, T[screen * 256 + sprite] (each built with the
    // sprite as the column, pl2.md).
    [[nodiscard]] std::uint8_t blend(Blend mode, std::uint8_t screen, std::uint8_t sprite) const noexcept {
        const std::size_t cell = std::size_t(screen) * 256 + sprite;
        switch (mode) {
            case Blend::kAlpha75: return alpha75_[cell];
            case Blend::kAlpha50: return blend50_[cell];
            case Blend::kAlpha25: return alpha25_[cell];
            case Blend::kAdditive: return additive_[cell];
            case Blend::kMultiply: return multiply_[cell];
            case Blend::kMaxComponent: return max_blend_[cell];
        }
        return sprite;
    }

private:
    // The 64 KB tables live on the heap: a Scene holds two PL2s, and inline
    // they overflowed Windows' 1 MB main-thread stack in load_scene.
    std::array<Rgba, 256>      base_{};
    std::vector<std::uint8_t>  additive_  = std::vector<std::uint8_t>(65536);
    std::vector<std::uint8_t>  blend50_   = std::vector<std::uint8_t>(65536);
    std::vector<std::uint8_t>  max_blend_ = std::vector<std::uint8_t>(65536);
    std::vector<std::uint8_t>  alpha75_   = std::vector<std::uint8_t>(65536);
    std::vector<std::uint8_t>  alpha25_   = std::vector<std::uint8_t>(65536);
    std::vector<std::uint8_t>  multiply_  = std::vector<std::uint8_t>(65536);
    std::array<std::uint8_t, 13 * 256> text_shift_{};
};

}  // namespace d2d::palette
