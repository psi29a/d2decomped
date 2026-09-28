// D2Decomp DCC (Diablo Cel Compressed) sprite parser.
//
// DCC is the animated-sprite format used for characters, monsters, missiles,
// overlays — anything that walks or wiggles. It's much denser than DC6:
// pixel data is bit-packed with variable-width per-pixel encoding, cells
// diff against the previous frame, and a 4-way palette mask packs common
// pixel values.
//
// This is a port of the format spec; we do not copy any implementation code.
// unverified (source: OpenDiablo2's reader, Necrolis' notes): the format as a
// whole; the data agrees — every character, monster and missile DCC in the
// MPQs decodes and draws as game.exe shows it. game.exe's decoder not traced.
//
// Format outline (all little-endian, bit-packed after the file header):
//
//   File header (7 bytes fixed + 8*dirs bytes):
//     u8   signature (= 0x74)
//     u8   version
//     u8   numberOfDirections
//     i32  framesPerDirection
//     i32  = 1 (magic)
//     i32  totalSizeCoded (unused by us)
//     numberOfDirections × i32 directionOffsets (byte offsets into the file)
//
//   Direction (bit-stream, starting at directionOffsets[dir]*8 bits):
//     u32   outSizeCoded
//     2 bits  compressionFlags
//     4 bits  variable0Bits     (via crazyBitTable)
//     4 bits  widthBits         (via crazyBitTable)
//     4 bits  heightBits
//     4 bits  xOffsetBits
//     4 bits  yOffsetBits
//     4 bits  optionalDataBits
//     4 bits  codedBytesBits
//     For each frame (framesPerDirection):
//       variable0Bits           variable0 (unused)
//       widthBits               width
//       heightBits              height
//       xOffsetBits             xOffset  (signed)
//       yOffsetBits             yOffset  (signed)
//       optionalDataBits        numOptionalBytes  (only 0 supported)
//       codedBytesBits          numCodedBytes
//       1 bit                   frameIsBottomUp   (only 0 supported)
//     If compressionFlags & 2:  20 bits  equalCellsBitstreamSize
//     20 bits                   pixelMaskBitstreamSize
//     If compressionFlags & 1:
//       20 bits                 encodingTypeBitstreamSize
//       20 bits                 rawPixelCodesBitstreamSize
//     256 bits                  paletteMask (1 bit per palette index)
//     Then 5 back-to-back bitstreams, each with the size counted above:
//       equalCells, pixelMask, encodingType, rawPixelCodes,
//       pixelCodesAndDisplacement (rest of direction).
//
// Cells are 4×4 aligned (last row/column may be smaller). Frames are
// reconstructed as: for each cell in each frame, either reuse the previous
// frame's same cell (if EqualCells bit is set), or fill from a pixel-buffer
// entry that carries up to 4 palette values (bits 0..3 of the pixelMask).
//
// Output: each Frame owns a width×height row-major buffer of palette
// indices; index 0 is transparent.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace d2d::dcc {

struct Frame {
    std::int32_t width{}, height{};
    std::int32_t x_offset{}, y_offset{};
    // Bounding box of this frame within its parent direction's canvas.
    std::int32_t box_left{}, box_top{}, box_right{}, box_bottom{};
    std::vector<std::uint8_t> pixels;   // width * height palette indices
};

class Sprite {
public:
    Sprite() = default;
    explicit Sprite(std::span<const std::byte> bytes) { parse(bytes); }

    [[nodiscard]] std::uint8_t directions() const noexcept { return dirs_; }
    [[nodiscard]] std::int32_t frames_per_direction() const noexcept { return frames_per_dir_; }

    [[nodiscard]] const Frame& frame(std::uint8_t dir, std::int32_t idx) const {
        if (dir >= dirs_ || idx < 0 || idx >= frames_per_dir_)
            throw std::out_of_range("DCC: frame OOB");
        return frames_[std::size_t(dir) * frames_per_dir_ + idx];
    }
    [[nodiscard]] std::span<const Frame> frames() const noexcept { return frames_; }
    // Every pixel through a 256-entry map (an item colormap); 0 stays clear.
    void remap(const std::uint8_t* map) {
        for (auto& frame : frames_) for (auto& pixel : frame.pixels) if (pixel) pixel = map[pixel];
    }

private:
    // Per-cell 4x4 (or smaller edge) block within a direction canvas.
    struct Cell {
        std::int32_t x{}, y{};
        std::int32_t width{}, height{};
        std::int32_t last_x{-1}, last_y{-1}, last_w{-1}, last_h{-1};
    };
    struct FrameCell {
        std::int32_t x{}, y{};
        std::int32_t width{}, height{};
    };
    struct PBEntry {
        std::array<std::uint8_t, 4> value{};
        std::int32_t frame{-1};
        std::int32_t frame_cell_index{-1};
    };

    // LSB-first bit reader over a byte buffer. `bit_pos` is a global bit
    // offset from the start of `data`; we don't wrap or reset.
    struct Bits {
        const std::byte* data;
        std::size_t      end_bits;   // total addressable bits
        std::size_t      pos = 0;    // current bit position
        std::size_t      read_since_copy = 0;

        std::uint32_t get_bit() {
            if (pos >= end_bits) throw std::runtime_error("DCC: bitstream OOB");
            const auto bit = (std::uint8_t(data[pos >> 3]) >> (pos & 7)) & 1u;
            ++pos;
            ++read_since_copy;
            return bit;
        }
        std::uint32_t get_bits(int count) {
            if (count <= 0) return 0;
            std::uint32_t value = 0;
            for (int i = 0; i < count; ++i) value |= get_bit() << i;
            return value;
        }
        std::int32_t get_signed(int count) {
            if (count == 0) return 0;
            const auto value = get_bits(count);
            if (count == 1) return -std::int32_t(value);         // 1-bit: 1 → -1
            const std::uint32_t sign = 1u << (count - 1);
            if ((value & sign) == 0) return std::int32_t(value);
            // Sign-extend: for negative, top bit is set; fill upper bits with 1.
            const std::uint32_t mask = ~((sign << 1) - 1);
            return std::int32_t(value | mask);
        }
        void skip(int count) {
            if (count < 0 || pos + std::size_t(count) > end_bits)
                throw std::runtime_error("DCC: bitstream skip OOB");
            pos += count;
            read_since_copy += count;
        }
        Bits fork() const {                              // clone; independent read counter
            Bits copy = *this;
            copy.read_since_copy = 0;
            return copy;
        }
    };

    static std::uint32_t rd_u32(const std::byte* source) {
        std::uint32_t value; std::memcpy(&value, source, 4); return value;
    }

    void parse(std::span<const std::byte> bytes) {
        if (bytes.size() < 15) throw std::runtime_error("DCC: truncated header");
        if (std::uint8_t(bytes[0]) != 0x74)
            throw std::runtime_error("DCC: bad signature");
        // b[1] version — accept anything.
        dirs_          = std::uint8_t(bytes[2]);
        frames_per_dir_ = std::int32_t(rd_u32(bytes.data() + 3));
        // rd_u32(b + 7) must == 1 in valid files; we don't enforce it.
        // rd_u32(b + 11) = totalSizeCoded (skipped).
        if (dirs_ == 0 || frames_per_dir_ <= 0)
            throw std::runtime_error("DCC: empty");

        const std::size_t dirOffsetsBase = 15;
        if (bytes.size() < dirOffsetsBase + std::size_t(dirs_) * 4)
            throw std::runtime_error("DCC: truncated direction offsets");

        std::vector<std::uint32_t> dir_offsets(dirs_);
        for (std::uint8_t direction = 0; direction < dirs_; ++direction) {
            dir_offsets[direction] = rd_u32(bytes.data() + dirOffsetsBase + std::size_t(direction) * 4);
        }

        frames_.assign(std::size_t(dirs_) * frames_per_dir_, Frame{});
        for (std::uint8_t direction = 0; direction < dirs_; ++direction) {
            decode_direction(bytes, dir_offsets[direction], direction);
        }
    }

    void decode_direction(std::span<const std::byte> bytes, std::uint32_t byte_off,
                          std::uint8_t dir_idx) {
        Bits bitmap{bytes.data(), bytes.size() * 8, std::size_t(byte_off) * 8, 0};

        static constexpr int crazy[16] =
            {0,1,2,4,6,8,10,12,14,16,20,24,26,28,30,32};

        [[maybe_unused]] const auto outSizeCoded = bitmap.get_bits(32);
        const auto compFlags = bitmap.get_bits(2);
        const int variable0Bits    = crazy[bitmap.get_bits(4)];
        const int widthBits        = crazy[bitmap.get_bits(4)];
        const int heightBits       = crazy[bitmap.get_bits(4)];
        const int xOffsetBits      = crazy[bitmap.get_bits(4)];
        const int yOffsetBits      = crazy[bitmap.get_bits(4)];
        const int optionalBits     = crazy[bitmap.get_bits(4)];
        const int codedBytesBits   = crazy[bitmap.get_bits(4)];

        // Frame headers.
        std::vector<Frame> frames(frames_per_dir_);
        std::int32_t minx =  INT32_MAX, miny =  INT32_MAX;
        std::int32_t maxx = INT32_MIN,  maxy = INT32_MIN;
        for (std::int32_t frame_index = 0; frame_index < frames_per_dir_; ++frame_index) {
            auto& frame = frames[frame_index];
            bitmap.skip(variable0Bits);                     // variable0 (ignored)
            frame.width    = std::int32_t(bitmap.get_bits(widthBits));
            frame.height   = std::int32_t(bitmap.get_bits(heightBits));
            frame.x_offset = bitmap.get_signed(xOffsetBits);
            frame.y_offset = bitmap.get_signed(yOffsetBits);
            const auto optBytes = bitmap.get_bits(optionalBits);
            const auto codedBytes = bitmap.get_bits(codedBytesBits);
            (void)codedBytes;
            const auto bottomUp = bitmap.get_bit();
            if (optBytes != 0)
                throw std::runtime_error("DCC: optional data not supported");
            if (bottomUp != 0)
                throw std::runtime_error("DCC: bottom-up frames not supported");

            frame.box_left   = frame.x_offset;
            frame.box_top    = frame.y_offset - frame.height + 1;
            frame.box_right  = frame.box_left + frame.width;
            frame.box_bottom = frame.box_top  + frame.height;
            minx = std::min(minx, frame.box_left);
            miny = std::min(miny, frame.box_top);
            maxx = std::max(maxx, frame.box_right);
            maxy = std::max(maxy, frame.box_bottom);
        }

        const std::int32_t dir_left = minx, dir_top = miny;
        const std::int32_t dir_w = maxx - minx, dir_h = maxy - miny;
        if (dir_w <= 0 || dir_h <= 0)
            throw std::runtime_error("DCC: degenerate direction box");

        std::int32_t equalCellsSize = 0, pixelMaskSize = 0;
        std::int32_t encodingSize   = 0, rawPixelSize  = 0;
        if (compFlags & 0x2) equalCellsSize = std::int32_t(bitmap.get_bits(20));
        pixelMaskSize = std::int32_t(bitmap.get_bits(20));
        if (compFlags & 0x1) {
            encodingSize = std::int32_t(bitmap.get_bits(20));
            rawPixelSize = std::int32_t(bitmap.get_bits(20));
        }

        // 256-bit palette-entry-valid mask.
        std::array<std::uint8_t, 256> palette{};
        int paletteCount = 0;
        for (int i = 0; i < 256; ++i) {
            if (bitmap.get_bit()) palette[paletteCount++] = std::uint8_t(i);
        }

        // Fork bitstreams — each starts at the current position and advances
        // its own read counter independently. We then skip the parent by the
        // declared size to reach the next stream.
        Bits equalCellsBs = bitmap.fork();  bitmap.skip(equalCellsSize);
        Bits pixelMaskBs  = bitmap.fork();  bitmap.skip(pixelMaskSize);
        Bits encTypeBs    = bitmap.fork();  bitmap.skip(encodingSize);
        Bits rawPixelBs   = bitmap.fork();  bitmap.skip(rawPixelSize);
        Bits pcdBs        = bitmap.fork();  // pixel codes + displacement (rest)

        // Direction-level cells (4×4 grid).
        constexpr int cells_per_row = 4;
        const int dir_hcells = 1 + (dir_w - 1) / cells_per_row;
        const int dir_vcells = 1 + (dir_h - 1) / cells_per_row;
        std::vector<Cell> dir_cells(std::size_t(dir_hcells) * dir_vcells);
        for (int cell_y = 0; cell_y < dir_vcells; ++cell_y) {
            for (int cell_x = 0; cell_x < dir_hcells; ++cell_x) {
                auto& cell = dir_cells[cell_y * dir_hcells + cell_x];
                cell.x = cell_x * 4;
                cell.y = cell_y * 4;
                cell.width = (cell_x == dir_hcells - 1) ? dir_w - (cell_x * 4) : 4;
                cell.height = (cell_y == dir_vcells - 1) ? dir_h - (cell_y * 4) : 4;
            }
        }

        // Per-frame cells (aligned within the direction canvas).
        std::vector<std::vector<FrameCell>> frame_cells(frames_per_dir_);
        std::vector<int> frame_hcells(frames_per_dir_), frame_vcells(frames_per_dir_);
        for (std::int32_t frame_index = 0; frame_index < frames_per_dir_; ++frame_index) {
            const auto& frame = frames[frame_index];
            int first_width = 4 - ((frame.box_left - dir_left) & 3);
            int first_height = 4 - ((frame.box_top  - dir_top)  & 3);
            int hcnt, vcnt;
            if (frame.width - first_width <= 1) hcnt = 1;
            else { int rest = frame.width - first_width - 1; hcnt = 2 + rest / 4; if (rest % 4 == 0) --hcnt; }
            if (frame.height - first_height <= 1) vcnt = 1;
            else { int rest = frame.height - first_height - 1; vcnt = 2 + rest / 4; if (rest % 4 == 0) --vcnt; }
            frame_hcells[frame_index] = hcnt;
            frame_vcells[frame_index] = vcnt;

            std::vector<int> cell_widths(hcnt), cell_heights(vcnt);
            if (hcnt == 1) cell_widths[0] = frame.width;
            else {
                cell_widths[0] = first_width;
                for (int i = 1; i < hcnt - 1; ++i) cell_widths[i] = 4;
                cell_widths[hcnt - 1] = frame.width - first_width - 4 * (hcnt - 2);
            }
            if (vcnt == 1) cell_heights[0] = frame.height;
            else {
                cell_heights[0] = first_height;
                for (int i = 1; i < vcnt - 1; ++i) cell_heights[i] = 4;
                cell_heights[vcnt - 1] = frame.height - first_height - 4 * (vcnt - 2);
            }

            frame_cells[frame_index].resize(std::size_t(hcnt) * vcnt);
            int off_y = frame.box_top - dir_top;
            for (int y = 0; y < vcnt; ++y) {
                int off_x = frame.box_left - dir_left;
                for (int x = 0; x < hcnt; ++x) {
                    auto& frame_cell = frame_cells[frame_index][y * hcnt + x];
                    frame_cell.x = off_x; frame_cell.y = off_y;
                    frame_cell.width = cell_widths[x]; frame_cell.height = cell_heights[y];
                    off_x += cell_widths[x];
                }
                off_y += cell_heights[y];
            }
        }

        // === Fill pixel buffer ===
        static constexpr int pixel_mask_lookup[16] =
            {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};

        int max_pbe = 0;
        for (std::int32_t frame_index = 0; frame_index < frames_per_dir_; ++frame_index)
            max_pbe += frame_hcells[frame_index] * frame_vcells[frame_index];
        std::vector<PBEntry> pixel_buffer(max_pbe);

        std::vector<int> cell_buffer(std::size_t(dir_hcells) * dir_vcells, -1);
        int pb_index = -1;
        std::uint32_t last_pixel = 0;

        for (std::int32_t frame_index = 0; frame_index < frames_per_dir_; ++frame_index) {
            const auto& frame = frames[frame_index];
            const int origin_cx = (frame.box_left - dir_left) / cells_per_row;
            const int origin_cy = (frame.box_top  - dir_top)  / cells_per_row;
            const int fhc = frame_hcells[frame_index];
            const int fvc = frame_vcells[frame_index];

            for (int cell_y = 0; cell_y < fvc; ++cell_y) {
                for (int cell_x = 0; cell_x < fhc; ++cell_x) {
                    const int cur = origin_cx + cell_x +
                                    (cell_y + origin_cy) * dir_hcells;
                    std::uint32_t pixel_mask = 0x0F;
                    bool next_cell = false;

                    if (cell_buffer[cur] != -1) {
                        std::uint32_t tmp = 0;
                        if (equalCellsSize > 0) tmp = equalCellsBs.get_bit();
                        if (tmp == 0) pixel_mask = pixelMaskBs.get_bits(4);
                        else next_cell = true;
                    }
                    if (next_cell) continue;

                    std::array<std::uint32_t, 4> pixel_stack{};
                    last_pixel = 0;
                    const int n_pix_bits = pixel_mask_lookup[pixel_mask];
                    int encoding_type = 0;
                    if (n_pix_bits != 0 && encodingSize > 0)
                        encoding_type = int(encTypeBs.get_bit());

                    int decoded = 0;
                    for (int i = 0; i < n_pix_bits; ++i) {
                        std::uint32_t value;
                        if (encoding_type != 0) {
                            value = rawPixelBs.get_bits(8);
                        } else {
                            value = last_pixel;
                            std::uint32_t disp = pcdBs.get_bits(4);
                            value += disp;
                            while (disp == 15) {
                                disp = pcdBs.get_bits(4);
                                value += disp;
                            }
                        }
                        if (value == last_pixel) {
                            pixel_stack[i] = 0;
                            break;
                        }
                        pixel_stack[i] = value;
                        last_pixel = value;
                        ++decoded;
                    }

                    const int old = cell_buffer[cur];
                    ++pb_index;
                    int cur_idx = decoded - 1;
                    for (int i = 0; i < 4; ++i) {
                        if (pixel_mask & (1u << i)) {
                            if (cur_idx >= 0) {
                                pixel_buffer[pb_index].value[i] =
                                    std::uint8_t(pixel_stack[cur_idx]);
                                --cur_idx;
                            } else {
                                pixel_buffer[pb_index].value[i] = 0;
                            }
                        } else {
                            pixel_buffer[pb_index].value[i] =
                                (old >= 0) ? pixel_buffer[old].value[i]
                                           : std::uint8_t(0);
                        }
                    }
                    cell_buffer[cur] = pb_index;
                    pixel_buffer[pb_index].frame = frame_index;
                    pixel_buffer[pb_index].frame_cell_index = cell_x + cell_y * fhc;
                }
            }
        }
        // Palette-map values.
        for (int i = 0; i <= pb_index; ++i)
            for (int k = 0; k < 4; ++k)
                pixel_buffer[i].value[k] = palette[pixel_buffer[i].value[k]];

        // === Reconstruct frames ===
        std::vector<std::uint8_t> canvas(std::size_t(dir_w) * dir_h, 0);
        int buffer_at = 0;
        for (std::int32_t frame_index = 0; frame_index < frames_per_dir_; ++frame_index) {
            auto& out = frames_[std::size_t(dir_idx) * frames_per_dir_ + frame_index];
            const auto& frame = frames[frame_index];
            out.width    = frame.width;
            out.height   = frame.height;
            out.x_offset = frame.x_offset;
            out.y_offset = frame.y_offset;
            out.box_left = frame.box_left;   out.box_top    = frame.box_top;
            out.box_right= frame.box_right;  out.box_bottom = frame.box_bottom;
            out.pixels.assign(std::size_t(frame.width) * frame.height, 0);

            for (std::size_t cell_index = 0; cell_index < frame_cells[frame_index].size(); ++cell_index) {
                const auto& frame_cell = frame_cells[frame_index][cell_index];
                const int dir_cell_x = frame_cell.x / cells_per_row;
                const int dir_cell_y = frame_cell.y / cells_per_row;
                const int dir_cell = dir_cell_x + dir_cell_y * dir_hcells;
                auto& bcell = dir_cells[dir_cell];
                auto& pbe = pixel_buffer[buffer_at];

                if (pbe.frame != frame_index || pbe.frame_cell_index != int(cell_index)) {
                    // EqualCells: copy previous cell or clear.
                    if (frame_cell.width != bcell.last_w || frame_cell.height != bcell.last_h) {
                        for (int y = 0; y < frame_cell.height; ++y)
                            for (int x = 0; x < frame_cell.width; ++x)
                                canvas[(frame_cell.y + y) * dir_w + frame_cell.x + x] = 0;
                    } else {
                        for (int y = 0; y < frame_cell.height; ++y)
                            for (int x = 0; x < frame_cell.width; ++x)
                                canvas[(frame_cell.y + y) * dir_w + frame_cell.x + x] =
                                    canvas[(bcell.last_y + y) * dir_w
                                           + bcell.last_x + x];
                    }
                } else {
                    if (pbe.value[0] == pbe.value[1]) {
                        // Solid fill.
                        for (int y = 0; y < frame_cell.height; ++y)
                            for (int x = 0; x < frame_cell.width; ++x)
                                canvas[(frame_cell.y + y) * dir_w + frame_cell.x + x] = pbe.value[0];
                    } else {
                        const int bits = (pbe.value[1] != pbe.value[2]) ? 2 : 1;
                        for (int y = 0; y < frame_cell.height; ++y) {
                            for (int x = 0; x < frame_cell.width; ++x) {
                                const auto idx = pcdBs.get_bits(bits);
                                canvas[(frame_cell.y + y) * dir_w + frame_cell.x + x] = pbe.value[idx];
                            }
                        }
                    }
                    ++buffer_at;
                }

                bcell.last_w = frame_cell.width; bcell.last_h = frame_cell.height;
                bcell.last_x = frame_cell.x; bcell.last_y = frame_cell.y;
            }

            // Blit the frame's region from the shared canvas — every cell in
            // this frame's box was either painted this pass or filled by an
            // EqualCells copy, so the canvas rect is complete.
            const int shift_x = frame.box_left - dir_left;
            const int shift_y = frame.box_top  - dir_top;
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    out.pixels[y * frame.width + x] =
                        canvas[(y + shift_y) * dir_w + x + shift_x];
                }
            }
        }
    }

    std::uint8_t  dirs_{};
    std::int32_t  frames_per_dir_{};
    std::vector<Frame> frames_;
};

}  // namespace d2d::dcc
