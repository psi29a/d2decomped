// D2Decomp screenshot writer — dump a raw RGBA framebuffer to PNG.
//
// Minimal PNG encoder: signature + IHDR + one IDAT (zlib-compressed via
// libz) + IEND. Every scanline is prefixed with a filter byte (0 = None) —
// we skip more elaborate filters so the encode side stays trivial; zlib
// still gets useful compression from the pixel repetition.
//
// Byte layout of `pixels`: row-major, top-to-bottom, RGBA8 (r, g, b, a).
// Callers whose framebuffer is bottom-up should flip before calling.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace d2d::screenshot {

// Writes `pixels` (w*h*4 bytes) as a PNG to `path`. Throws std::runtime_error
// on any I/O or compression failure. Returns the number of bytes written.
std::size_t save_png(const std::filesystem::path& path,
                     std::span<const std::uint8_t> pixels,
                     std::uint32_t width, std::uint32_t height);

}  // namespace d2d::screenshot
