// SPDX-License-Identifier: GPL-3.0-or-later
// D2Decomp DT1 (Diablo Tile) parser.
//
// DT1 = an atlas of tiles that compose a Diablo II level. Each tile is a
// collection of 32×32 pixel blocks positioned within a tile-local bounding
// box. Blocks are stored in one of two encodings (isometric diamond or
// RLE) and decoded into a single palette-indexed pixel buffer per tile.
//
// File layout (little-endian throughout):
//
//   +0x000  i32  majorVersion  (= 7)
//   +0x004  i32  minorVersion  (= 6)
//   +0x008  260 bytes  padding
//   +0x10C  i32  numberOfTiles
//   +0x110  i32  tileHeadersOffset  (usually 0x114 — right after)
//
// Per tile header (96 bytes):
//   i32  direction
//   i16  roofHeight
//   u16  materialFlags
//   i32  height          (can be NEGATIVE — floor tiles extend up from y=0)
//   i32  width
//   4    padding
//   i32  type             (orientation: floor / left-wall / right-wall / …)
//   i32  style
//   i32  sequence
//   i32  rarityFrameIndex
//   4    padding
//   25   subTileFlags     (5×5 grid, walkability + LoS per subtile)
//   7    padding
//   i32  blockHeaderPointer  (file offset)
//   i32  blockHeaderSize
//   i32  numBlocks
//   12   padding
//
// Per block header (20 bytes, at blockHeaderPointer):
//   i16  x, y             (position within tile, in pixels; can be negative)
//   2    padding
//   u8   gridX, gridY
//   i16  format           (0 = RLE, 1 = 3D isometric)
//   i32  length           (encoded pixel data length in bytes)
//   2    padding
//   i32  fileOffset       (RELATIVE to blockHeaderPointer)
//
// Block encoded data:
//   * Isometric (256 bytes exactly):
//     Diamond fill with row widths {4,8,12,16,20,24,28,32,28,24,20,16,12,8,4}
//     and x-offsets  {14,12,10,8,6,4,2,0,2,4,6,8,10,12,14}.
//   * RLE:
//     Pairs (skipX, runLen); skipX advances x by that many transparent
//     pixels, then runLen palette-index bytes follow. When both bytes of the
//     pair are 0, advance to next scanline (x=0, y++).
//
// Block x counts from the cell's left corner in a 160-wide frame, whatever
// the tile's width: draw a tile at the cell's left corner, not centred
// (1677 of the tiles are narrower; every block of every DT1 lies in
// [0, width)).
//
// Output: each tile gets a width × abs(height) row-major byte buffer with
// (none with Pixels::skip: headers and subtile flags only)
// palette indices; index 0 is transparent. Blocks with negative y are
// shifted by |height| so all row indices are non-negative.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace d2d::dt1 {

struct Tile {
    std::int32_t  direction{};
    std::int16_t  roof_height{};
    std::uint16_t material_flags{};
    std::int32_t  height{};     // may be negative (floor tiles)
    std::int32_t  width{};
    std::int32_t  type{};       // orientation code
    std::int32_t  style{};
    std::int32_t  sequence{};
    std::int32_t  rarity_frame_index{};
    std::array<std::uint8_t, 25> subtile_flags{};
    std::int32_t y_shift{};             // added to block y to land in pixels[]
    std::vector<std::uint8_t> pixels;   // width * abs(height), 0=transparent
};

// Whether an archive decodes its tiles' pixels. A headless game server
// needs only the headers and subtile flags (walkability).
enum class Pixels { decode, skip };

class Archive {
public:
    Archive() = default;
    explicit Archive(std::span<const std::byte> bytes, Pixels pixels = Pixels::decode) { parse(bytes, pixels); }

    [[nodiscard]] std::span<const Tile> tiles() const noexcept { return tiles_; }
    [[nodiscard]] std::size_t size() const noexcept { return tiles_.size(); }

private:
    static std::int16_t rd_i16(const std::byte* source) {
        std::int16_t value; std::memcpy(&value, source, 2); return value;
    }
    static std::uint16_t rd_u16(const std::byte* source) {
        std::uint16_t value; std::memcpy(&value, source, 2); return value;
    }
    static std::int32_t rd_i32(const std::byte* source) {
        std::int32_t value; std::memcpy(&value, source, 4); return value;
    }

    void parse(std::span<const std::byte> bytes, Pixels pixels) {
        if (bytes.size() < 0x114) throw std::runtime_error("DT1: truncated header");

        const auto major = rd_i32(bytes.data());
        const auto minor = rd_i32(bytes.data() + 4);
        if (major != 7 || minor != 6)
            throw std::runtime_error("DT1: unsupported version");

        const auto numTiles = rd_i32(bytes.data() + 0x10c);
        const auto tileHdrOff = rd_i32(bytes.data() + 0x110);
        if (numTiles < 0 || tileHdrOff < 0)
            throw std::runtime_error("DT1: negative counts");

        constexpr std::size_t kTileHdr = 96;
        if (bytes.size() < std::size_t(tileHdrOff)
                     + std::size_t(numTiles) * kTileHdr)
            throw std::runtime_error("DT1: tile header table OOB");

        tiles_.resize(numTiles);
        // Pass 1: tile headers (block metadata lives elsewhere in the file).
        std::vector<std::pair<std::int32_t, std::int32_t>> block_ptrs(numTiles);
        for (std::int32_t i = 0; i < numTiles; ++i) {
            const std::byte* header = bytes.data() + tileHdrOff + i * kTileHdr;
            auto& tile = tiles_[i];
            tile.direction          = rd_i32(header + 0);
            tile.roof_height        = rd_i16(header + 4);
            tile.material_flags     = rd_u16(header + 6);
            tile.height             = rd_i32(header + 8);
            tile.width              = rd_i32(header + 12);
            // h + 16: 4 bytes padding
            tile.type               = rd_i32(header + 20);
            tile.style              = rd_i32(header + 24);
            tile.sequence           = rd_i32(header + 28);
            tile.rarity_frame_index = rd_i32(header + 32);
            // h + 36: 4 bytes padding
            for (int k = 0; k < 25; ++k)
                tile.subtile_flags[k] = std::uint8_t(header[40 + k]);
            // h + 65: 7 bytes padding
            const auto blkHdrPtr   = rd_i32(header + 72);
            // h + 76: blockHeaderSize (unused)
            const auto numBlocks   = rd_i32(header + 80);
            // h + 84: 12 bytes padding
            block_ptrs[i]          = {blkHdrPtr, numBlocks};

            if (pixels == Pixels::decode) allocate_pixels(tile);
        }
        if (pixels == Pixels::skip) return;

        // Pass 2: block headers + decode into each tile's pixel buffer.
        // Y-shift is computed from actual block Y positions:
        // tileYOffset = max(0, -min(block.y)).
        // unverified (source: OpenDiablo2's renderer): the y-shift. Blocks whose
        // shifted rows still fall outside the buffer get silently clipped by
        // put_pixel — that matches D2's behaviour on odd tiles.
        constexpr std::size_t kBlockHdr = 20;
        struct BlockDecode {
            std::int16_t x, y;
            std::int16_t format;
            std::span<const std::byte> enc;
        };
        std::vector<BlockDecode> decoded;
        for (std::int32_t i = 0; i < numTiles; ++i) {
            const auto [blkHdrPtr, numBlocks] = block_ptrs[i];
            if (numBlocks == 0) continue;
            if (blkHdrPtr < 0
                || std::size_t(blkHdrPtr) + std::size_t(numBlocks) * kBlockHdr
                   > bytes.size())
                throw std::runtime_error("DT1: block header table OOB");

            decoded.clear();
            decoded.reserve(numBlocks);
            std::int32_t min_y = 0;
            for (std::int32_t j = 0; j < numBlocks; ++j) {
                const std::byte* block_header = bytes.data() + blkHdrPtr + j * kBlockHdr;
                BlockDecode block;
                block.x      = rd_i16(block_header + 0);
                block.y      = rd_i16(block_header + 2);
                // bh + 4: padding, bh + 6/7: gridX/Y (unused)
                block.format = rd_i16(block_header + 8);
                const auto length = rd_i32(block_header + 10);
                // bh + 14: padding
                const auto blkOff = rd_i32(block_header + 16);   // relative to blkHdrPtr

                const std::size_t dataOff =
                    std::size_t(blkHdrPtr) + std::size_t(blkOff);
                if (length < 0 || dataOff + std::size_t(length) > bytes.size())
                    throw std::runtime_error("DT1: block data OOB");

                block.enc = std::span(bytes.data() + dataOff, length);
                if (block.y < min_y) min_y = block.y;
                decoded.push_back(block);
            }

            tiles_[i].y_shift = -min_y;   // >= 0
            for (const auto& block : decoded) {
                if (block.format == 1) {
                    decode_isometric(tiles_[i], block.x, block.y, block.enc);
                } else {
                    decode_rle(tiles_[i], block.x, block.y, block.enc);
                }
            }
        }
    }

    static std::size_t buf_height(const Tile& tile) {
        return std::size_t(tile.height < 0 ? -tile.height : tile.height);
    }

    static void allocate_pixels(Tile& tile) {
        const auto height = buf_height(tile);
        if (height == 0 || tile.width <= 0) return;
        tile.pixels.assign(std::size_t(tile.width) * height, 0);
    }

    static void put_pixel(Tile& tile, std::int32_t pixel_x, std::int32_t pixel_y,
                          std::uint8_t value) {
        const auto y = pixel_y + tile.y_shift;
        if (pixel_x < 0 || pixel_x >= tile.width) return;
        if (y < 0 || std::size_t(y) >= buf_height(tile)) return;
        tile.pixels[std::size_t(y) * tile.width + pixel_x] = value;
    }

    static void decode_isometric(Tile& tile, std::int16_t block_x, std::int16_t block_y,
                                 std::span<const std::byte> enc) {
        // Fixed 256-byte diamond fill. If the block is short, silently clip.
        static constexpr int xjump[15] = {14,12,10,8,6,4,2,0,2,4,6,8,10,12,14};
        static constexpr int nbpix[15] = { 4, 8,12,16,20,24,28,32,28,24,20,16,12, 8, 4};
        std::size_t idx = 0;
        for (int row = 0; row < 15 && idx < enc.size(); ++row) {
            const int count = nbpix[row];
            const int left = xjump[row];
            for (int k = 0; k < count; ++k, ++idx) {
                if (idx >= enc.size()) return;
                put_pixel(tile, block_x + left + k, block_y + row, std::uint8_t(enc[idx]));
            }
        }
    }

    static void decode_rle(Tile& tile, std::int16_t block_x, std::int16_t block_y,
                           std::span<const std::byte> enc) {
        std::int32_t x = 0, y = 0;
        std::size_t idx = 0;
        std::int32_t remain = static_cast<std::int32_t>(enc.size());
        while (remain > 0) {
            const auto first_byte = std::uint8_t(enc[idx]);
            const auto second_byte = std::uint8_t(enc[idx + 1]);
            idx    += 2;
            remain -= 2;
            if ((first_byte | second_byte) == 0) {
                x = 0;
                ++y;
                continue;
            }
            x += first_byte;
            if (idx + second_byte > enc.size()) throw std::runtime_error("DT1: RLE OOB");
            for (int k = 0; k < second_byte; ++k) {
                put_pixel(tile, block_x + x, block_y + y, std::uint8_t(enc[idx + k]));
                ++x;
            }
            idx    += second_byte;
            remain -= second_byte;
        }
    }

    std::vector<Tile> tiles_;
};

}  // namespace d2d::dt1
