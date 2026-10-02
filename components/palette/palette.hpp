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

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace d2d::palette {

struct Rgba {
    std::uint8_t r{}, g{}, b{}, a{};
    constexpr bool operator==(const Rgba&) const = default;
};

class Palette {
public:
    Palette() = default;

    // Accepts a 768-byte pal.dat (BGR triples; unverified (source:
    // OpenDiablo2's d2dat.Load); the data agrees — the colours match
    // game.exe's screens) or a .PL2 (RGBA at offset 0). Auto-detects layout from
    // total size: exactly 768 → BGR-packed DAT; 4-byte stride → RGBA PL2.
    // Larger DAT-style buffers are truncated; short buffers throw.
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
            // DAT layout is (B, G, R) per index (unverified, see above).
            entries_[i] = Rgba{
                std::uint8_t(bytes[i * 3 + 2]),   // R
                std::uint8_t(bytes[i * 3 + 1]),   // G
                std::uint8_t(bytes[i * 3 + 0]),   // B
                std::uint8_t(i == 0 ? 0 : 0xFF),
            };
        }
    }

    explicit Palette(const std::array<Rgba, 256>& entries) : entries_(entries) {}

    [[nodiscard]] Rgba operator[](std::uint8_t idx) const noexcept {
        return entries_[idx];
    }
    [[nodiscard]] std::span<const Rgba> entries() const noexcept { return entries_; }

    // Overwrite the alpha for one entry — handy when a specific tileset
    // treats a non-zero index as chroma-key.
    void set_alpha(std::uint8_t idx, std::uint8_t alpha) noexcept {
        entries_[idx].a = alpha;
    }

private:
    std::array<Rgba, 256> entries_{};
};

// PL2 colormap file. We currently parse:
//   * base 256 RGBA at offset 0                      → base_palette()
//   * MaxComponentBlend[256][256] at offset 210176   → additive(fg, bg)
//   * Transparency50[256][256]     at offset 374016  → blend50(fg, bg)
//
// The additive table is D2's TRANS_ADDITIVE mode — the fire, spell VFX,
// and shrine glows all blend through it. Given (foreground_idx, bg_idx)
// it returns the palette index whose colour has the per-channel max of
// the two — palette-preserving, unlike RGB-clamp additive which drifts
// off the palette and looks off-key.
//
// Both offsets validated empirically against menu1/Pal.PL2:
//   * 210176: T[i][0]=i AND T[0][j]=j on every index; result brightness
//     always >= max(fg, bg) — additive semantics.
//   * 374016: T[i][i]=i (blending with self is self) — 50% alpha blend.
//
// Other transforms (LightLevelVariations, HueVariations, TextColorShifts,
// Transparency25/75, per-channel tone shifts) are in the file but not
// exposed here — add when a subsystem asks for one. See
// docs/research/re/pl2-layout.md for the layout notes.
class Pl2 {
public:
    static constexpr std::size_t kFileSize        = 443175;
    static constexpr std::size_t kBaseOff         = 0;
    static constexpr std::size_t kAdditiveOff     = 210176;
    static constexpr std::size_t kBlend50Off      = 374016;

    Pl2() = default;

    explicit Pl2(std::span<const std::byte> bytes) {
        if (bytes.size() < kBlend50Off + 256u * 256u)
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
};

}  // namespace d2d::palette
