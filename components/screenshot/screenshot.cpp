// SPDX-License-Identifier: GPL-3.0-or-later
#include "screenshot.hpp"

#include <zconf.h>
#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <vector>

namespace d2d::screenshot {

namespace {

void write_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(std::uint8_t(value >> 24));
    out.push_back(std::uint8_t(value >> 16));
    out.push_back(std::uint8_t(value >>  8));
    out.push_back(std::uint8_t(value      ));
}

void write_chunk(std::vector<std::uint8_t>& out,
                 const char type[4],
                 const std::uint8_t* data,
                 std::size_t len) {
    write_u32_be(out, std::uint32_t(len));
    const auto crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data, data + len);
    const auto crc = ::crc32(0L, out.data() + crc_start,
                             uInt(out.size() - crc_start));
    write_u32_be(out, std::uint32_t(crc));
}

}  // namespace

std::size_t save_png(const std::filesystem::path& path,
                     std::span<const std::uint8_t> pixels,
                     std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::runtime_error("screenshot: zero dimension");
    if (pixels.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("screenshot: pixel span size mismatch");

    // Build raw filtered scanlines: 1 filter byte per row, then RGBA pixels.
    const std::size_t rowStride = std::size_t(width) * 4;
    std::vector<std::uint8_t> raw(height * (rowStride + 1));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw[y * (rowStride + 1)] = 0;   // filter = None
        std::memcpy(raw.data() + y * (rowStride + 1) + 1,
                    pixels.data() + std::size_t(y) * rowStride,
                    rowStride);
    }

    // zlib-compress the filtered stream.
    uLongf compBound = ::compressBound(uLong(raw.size()));
    std::vector<std::uint8_t> comp(compBound);
    int result = ::compress2(comp.data(), &compBound,
                         raw.data(), uLong(raw.size()),
                         Z_BEST_SPEED);   // fast path — dev-only screenshots
    if (result != Z_OK)
        throw std::runtime_error("screenshot: zlib compress failed");
    comp.resize(compBound);

    // Assemble the PNG.
    std::vector<std::uint8_t> out;
    static constexpr std::uint8_t kSig[8] =
        {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), kSig, kSig + 8);

    std::uint8_t ihdr[13];
    ihdr[ 0] = std::uint8_t(width >> 24); ihdr[ 1] = std::uint8_t(width >> 16);
    ihdr[ 2] = std::uint8_t(width >>  8); ihdr[ 3] = std::uint8_t(width      );
    ihdr[ 4] = std::uint8_t(height >> 24); ihdr[ 5] = std::uint8_t(height >> 16);
    ihdr[ 6] = std::uint8_t(height >>  8); ihdr[ 7] = std::uint8_t(height      );
    ihdr[ 8] = 8;   // bit depth
    ihdr[ 9] = 6;   // color type = RGBA
    ihdr[10] = 0;   // compression = deflate
    ihdr[11] = 0;   // filter = adaptive (per-scanline byte)
    ihdr[12] = 0;   // interlace = none
    write_chunk(out, "IHDR", ihdr, sizeof(ihdr));
    write_chunk(out, "IDAT", comp.data(), comp.size());
    write_chunk(out, "IEND", nullptr, 0);

    // Write the whole file at once — dev-only, doesn't warrant streaming.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("screenshot: open failed: " + path.string());
    file.write(reinterpret_cast<const char*>(out.data()),
             std::streamsize(out.size()));
    if (!file) throw std::runtime_error("screenshot: write failed: " + path.string());
    return out.size();
}

}  // namespace d2d::screenshot
