// SPDX-License-Identifier: GPL-3.0-or-later
// How long a WAV plays: its `data` chunk over the `fmt ` chunk's average
// bytes a second (PCM and IMA ADPCM alike), in ms. What a sound instance
// lasts while it plays (FUN_004b9c20 asks the playing list), for the World,
// which plays nothing itself.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace d2d::rules {

[[nodiscard]] inline std::optional<std::uint32_t> wav_ms(std::span<const std::byte> file) {
    const auto u32 = [&](std::size_t pos) {
        return std::uint32_t(file[pos]) | std::uint32_t(file[pos + 1]) << 8 | std::uint32_t(file[pos + 2]) << 16 | std::uint32_t(file[pos + 3]) << 24;
    };
    const auto tag = [&](std::size_t pos, const char* name) {
        return char(file[pos]) == name[0] && char(file[pos + 1]) == name[1] && char(file[pos + 2]) == name[2] && char(file[pos + 3]) == name[3];
    };
    if (file.size() < 12 || !tag(0, "RIFF") || !tag(8, "WAVE")) return std::nullopt;
    std::uint32_t byte_rate = 0;
    for (std::size_t pos = 12; pos + 8 <= file.size();) {
        const std::uint32_t size = u32(pos + 4);
        if (tag(pos, "fmt ") && size >= 12 && pos + 16 <= file.size()) byte_rate = u32(pos + 16);
        if (tag(pos, "data")) {
            if (byte_rate == 0) return std::nullopt;
            return std::uint32_t(std::uint64_t(size) * 1000 / byte_rate);
        }
        pos += 8 + size + (size & 1);
    }
    return std::nullopt;
}

}  // namespace d2d::rules
