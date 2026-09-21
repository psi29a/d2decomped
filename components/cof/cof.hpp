// D2Decomp COF (Component Object File) parser.
//
// COF describes how animated body parts (head, torso, weapons, ...) compose
// into one character animation. It doesn't hold pixels — only the recipe:
// which layers, how many directions/frames, per-frame draw priority, and
// which frames carry animation-event markers (footstep, attack impact, ...).
//
// File layout (little-endian, tightly packed):
//
//   Header (25 bytes):
//     u8  numLayers
//     u8  framesPerDirection
//     u8  numDirections
//     21  bytes unknown (probably i32 xMin/xMax/yMin/yMax + i32 animRate
//         + 1 pad — kept as raw bytes; nothing in-tree needs them)
//     u8  speed
//
//   Body:
//     3   bytes unknown
//     For each layer: 9 bytes
//       u8  type          (composite slot: HD/TR/LG/RA/LA/RH/LH/SH/S1..S8)
//       u8  shadow        (nonzero = casts shadow)
//       u8  selectable    (0/1)
//       u8  transparent   (0/1)
//       u8  drawEffect    (blend mode enum)
//       4   bytes weaponClass ASCII code, NUL-padded to length 4
//                            ("hth", "1hs", "bow", …)
//     framesPerDirection bytes: per-frame animation-event markers
//                            (0 = none, 1 = attack, 2 = missile, 3 = sound,
//                             4 = skill).
//     numDirections * framesPerDirection * numLayers bytes:
//       per (dir, frame) draw order — an ordered list of composite-type
//       enum values telling the compositor which layer to draw first.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2d::cof {

struct Layer {
    std::uint8_t type{};
    std::uint8_t shadow{};
    bool         selectable{};
    bool         transparent{};
    std::uint8_t draw_effect{};
    std::string  weapon_class;   // 1..3 chars, e.g. "hth", "1hs"
};

class Cof {
public:
    Cof() = default;
    explicit Cof(std::span<const std::byte> bytes) { parse(bytes); }

    [[nodiscard]] std::uint8_t layers()               const noexcept { return num_layers_; }
    [[nodiscard]] std::uint8_t frames_per_direction() const noexcept { return frames_per_dir_; }
    [[nodiscard]] std::uint8_t directions()           const noexcept { return num_dirs_; }
    [[nodiscard]] std::uint8_t speed()                const noexcept { return speed_; }

    [[nodiscard]] std::span<const Layer>              layer_defs() const noexcept { return layer_defs_; }
    [[nodiscard]] std::span<const std::uint8_t>       events()     const noexcept { return events_; }

    // Draw priority for (direction, frame): a run of `layers()` bytes,
    // each byte a composite type enum. Row-major (direction-major, frame).
    [[nodiscard]] std::span<const std::uint8_t>
    priority(std::size_t dir, std::size_t frame) const {
        if (dir >= num_dirs_ || frame >= frames_per_dir_)
            throw std::out_of_range("COF: bad (dir, frame)");
        const auto stride = std::size_t(num_layers_);
        const auto off = (dir * frames_per_dir_ + frame) * stride;
        return {priorities_.data() + off, stride};
    }

private:
    void parse(std::span<const std::byte> b) {
        constexpr std::size_t kHdr = 25;
        if (b.size() < kHdr + 3) throw std::runtime_error("COF: truncated header");
        num_layers_       = std::uint8_t(b[0]);
        frames_per_dir_   = std::uint8_t(b[1]);
        num_dirs_         = std::uint8_t(b[2]);
        speed_            = std::uint8_t(b[24]);

        std::size_t p = kHdr + 3;   // skip 3-byte body unknown

        constexpr std::size_t kLayer = 9;
        if (b.size() < p + std::size_t(num_layers_) * kLayer)
            throw std::runtime_error("COF: truncated layer table");
        layer_defs_.resize(num_layers_);
        for (auto& L : layer_defs_) {
            L.type        = std::uint8_t(b[p + 0]);
            L.shadow      = std::uint8_t(b[p + 1]);
            L.selectable  = std::uint8_t(b[p + 2]) != 0;
            L.transparent = std::uint8_t(b[p + 3]) != 0;
            L.draw_effect = std::uint8_t(b[p + 4]);
            // 4-byte weapon class code, NUL-padded (D2 codes are 1..3 chars).
            for (std::size_t k = 0; k < 4; ++k) {
                const auto ch = char(b[p + 5 + k]);
                if (ch == '\0') break;
                L.weapon_class.push_back(ch);
            }
            p += kLayer;
        }

        if (b.size() < p + frames_per_dir_)
            throw std::runtime_error("COF: truncated events");
        events_.assign(frames_per_dir_, 0);
        for (std::size_t k = 0; k < frames_per_dir_; ++k) {
            events_[k] = std::uint8_t(b[p + k]);
        }
        p += frames_per_dir_;

        const std::size_t priBytes =
            std::size_t(num_dirs_) * frames_per_dir_ * num_layers_;
        if (b.size() < p + priBytes)
            throw std::runtime_error("COF: truncated priority table");
        priorities_.assign(priBytes, 0);
        for (std::size_t k = 0; k < priBytes; ++k) {
            priorities_[k] = std::uint8_t(b[p + k]);
        }
    }

    std::uint8_t num_layers_{};
    std::uint8_t frames_per_dir_{};
    std::uint8_t num_dirs_{};
    std::uint8_t speed_{};
    std::vector<Layer>        layer_defs_;
    std::vector<std::uint8_t> events_;
    std::vector<std::uint8_t> priorities_;
};

}  // namespace d2d::cof
