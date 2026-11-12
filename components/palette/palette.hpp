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
    [[nodiscard]] Rgba operator[](std::uint8_t idx) const noexcept {
        Rgba colour = entries_[idx];
        const auto& level = video_ramp().level;
        colour.r = level[colour.r];
        colour.g = level[colour.g];
        colour.b = level[colour.b];
        return colour;
    }
    [[nodiscard]] std::span<const Rgba> entries() const noexcept { return entries_; }
    // Rebuild FUN_006be190. Every palette lookup sees it.
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
// blend50 is game.exe's installed 0x7fe468 (FUN_004fb1e0): tools/emu pl2.py
// diffs it (docs/research/formats/pl2.md). The other tables (the 75 / 25 %
// alphas, multiply, max-component at 0x5b500) have no caller here yet.
class Pl2 {
public:
    static constexpr std::size_t kFileSize        = 443175;
    static constexpr std::size_t kBaseOff         = 0;
    static constexpr std::size_t kAdditiveOff     = 0x33500;
    static constexpr std::size_t kBlend50Off      = 0x13500;
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

private:
    std::array<Rgba, 256>            base_{};
    std::array<std::uint8_t, 65536>  additive_{};
    std::array<std::uint8_t, 65536>  blend50_{};
    std::array<std::uint8_t, 13 * 256> text_shift_{};
};

}  // namespace d2d::palette
