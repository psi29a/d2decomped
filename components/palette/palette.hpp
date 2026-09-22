// D2Decomp palette (.dat / .pl2 base) reader.
//
// D2 stores palettes in two forms:
//   * pal.dat  — 768 bytes: 256 packed RGB triples (r, g, b), no header.
//   * Pal.PL2  — 443,175 bytes: the same 256 entries at offset 0, then a
//                mountain of colormaps (light levels, tints, alpha blends,
//                additive/multiplicative, hue variations, …). We only care
//                about the base 256 entries for now — colormaps get their
//                own parser when a subsystem (lighting, unit-select tint,
//                item hue) actually needs them.
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

    // Accepts a 768-byte pal.dat (BGR triples — confirmed via OpenDiablo2's
    // d2dat.Load) or a .PL2 (RGBA at offset 0). Auto-detects layout from
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
            // DAT layout is (B, G, R) per index — see OpenDiablo2 dat.go.
            entries_[i] = Rgba{
                std::uint8_t(bytes[i * 3 + 2]),   // R
                std::uint8_t(bytes[i * 3 + 1]),   // G
                std::uint8_t(bytes[i * 3 + 0]),   // B
                std::uint8_t(i == 0 ? 0 : 0xFF),
            };
        }
    }

    [[nodiscard]] Rgba operator[](std::uint8_t idx) const noexcept {
        return entries_[idx];
    }
    [[nodiscard]] std::span<const Rgba> entries() const noexcept { return entries_; }

    // Overwrite the alpha for one entry — handy when a specific tileset
    // treats a non-zero index as chroma-key.
    void set_alpha(std::uint8_t idx, std::uint8_t a) noexcept {
        entries_[idx].a = a;
    }

private:
    std::array<Rgba, 256> entries_{};
};

}  // namespace d2d::palette
