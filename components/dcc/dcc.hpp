// D2Decomp DCC (Diablo Cel Compressed) sprite parser.
//
// DCC is the animated-sprite format used for characters, monsters, missiles,
// overlays — anything that walks or wiggles. It's much denser than DC6:
// pixel data is bit-packed with variable-width per-pixel encoding, cells
// diff against the previous frame, and a 4-way palette mask packs common
// pixel values.
//
// This is a port of the format spec (see references in OpenDiablo2 and
// Necrolis' notes); we do not copy any implementation code.
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
        for (auto& f : frames_) for (auto& p : f.pixels) if (p) p = map[p];
    }

private:
    // Per-cell 4x4 (or smaller edge) block within a direction canvas.
    struct Cell {
        std::int32_t x{}, y{};
        std::int32_t w{}, h{};
        std::int32_t last_x{-1}, last_y{-1}, last_w{-1}, last_h{-1};
    };
    struct FrameCell {
        std::int32_t x{}, y{};
        std::int32_t w{}, h{};
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
            const auto v = (std::uint8_t(data[pos >> 3]) >> (pos & 7)) & 1u;
            ++pos;
            ++read_since_copy;
            return v;
        }
        std::uint32_t get_bits(int n) {
            if (n <= 0) return 0;
            std::uint32_t v = 0;
            for (int i = 0; i < n; ++i) v |= get_bit() << i;
            return v;
        }
        std::int32_t get_signed(int n) {
            if (n == 0) return 0;
            const auto v = get_bits(n);
            if (n == 1) return -std::int32_t(v);         // 1-bit: 1 → -1
            const std::uint32_t sign = 1u << (n - 1);
            if ((v & sign) == 0) return std::int32_t(v);
            // Sign-extend: for negative, top bit is set; fill upper bits with 1.
            const std::uint32_t mask = ~((sign << 1) - 1);
            return std::int32_t(v | mask);
        }
        void skip(int n) {
            if (n < 0 || pos + std::size_t(n) > end_bits)
                throw std::runtime_error("DCC: bitstream skip OOB");
            pos += n;
            read_since_copy += n;
        }
        Bits fork() const {                              // clone; independent read counter
            Bits b = *this;
            b.read_since_copy = 0;
            return b;
        }
    };

    static std::uint32_t rd_u32(const std::byte* p) {
        std::uint32_t v; std::memcpy(&v, p, 4); return v;
    }

    void parse(std::span<const std::byte> b) {
        if (b.size() < 15) throw std::runtime_error("DCC: truncated header");
        if (std::uint8_t(b[0]) != 0x74)
            throw std::runtime_error("DCC: bad signature");
        // b[1] version — accept anything.
        dirs_          = std::uint8_t(b[2]);
        frames_per_dir_ = std::int32_t(rd_u32(b.data() + 3));
        // rd_u32(b + 7) must == 1 in valid files; we don't enforce it.
        // rd_u32(b + 11) = totalSizeCoded (skipped).
        if (dirs_ == 0 || frames_per_dir_ <= 0)
            throw std::runtime_error("DCC: empty");

        const std::size_t dirOffsetsBase = 15;
        if (b.size() < dirOffsetsBase + std::size_t(dirs_) * 4)
            throw std::runtime_error("DCC: truncated direction offsets");

        std::vector<std::uint32_t> dir_offsets(dirs_);
        for (std::uint8_t d = 0; d < dirs_; ++d) {
            dir_offsets[d] = rd_u32(b.data() + dirOffsetsBase + std::size_t(d) * 4);
        }

        frames_.assign(std::size_t(dirs_) * frames_per_dir_, Frame{});
        for (std::uint8_t d = 0; d < dirs_; ++d) {
            decode_direction(b, dir_offsets[d], d);
        }
    }

    void decode_direction(std::span<const std::byte> b, std::uint32_t byte_off,
                          std::uint8_t dir_idx) {
        Bits bm{b.data(), b.size() * 8, std::size_t(byte_off) * 8, 0};

        static constexpr int crazy[16] =
            {0,1,2,4,6,8,10,12,14,16,20,24,26,28,30,32};

        [[maybe_unused]] const auto outSizeCoded = bm.get_bits(32);
        const auto compFlags = bm.get_bits(2);
        const int variable0Bits    = crazy[bm.get_bits(4)];
        const int widthBits        = crazy[bm.get_bits(4)];
        const int heightBits       = crazy[bm.get_bits(4)];
        const int xOffsetBits      = crazy[bm.get_bits(4)];
        const int yOffsetBits      = crazy[bm.get_bits(4)];
        const int optionalBits     = crazy[bm.get_bits(4)];
        const int codedBytesBits   = crazy[bm.get_bits(4)];

        // Frame headers.
        std::vector<Frame> frames(frames_per_dir_);
        std::int32_t minx =  INT32_MAX, miny =  INT32_MAX;
        std::int32_t maxx = INT32_MIN,  maxy = INT32_MIN;
        for (std::int32_t f = 0; f < frames_per_dir_; ++f) {
            auto& fr = frames[f];
            bm.skip(variable0Bits);                     // variable0 (ignored)
            fr.width    = std::int32_t(bm.get_bits(widthBits));
            fr.height   = std::int32_t(bm.get_bits(heightBits));
            fr.x_offset = bm.get_signed(xOffsetBits);
            fr.y_offset = bm.get_signed(yOffsetBits);
            const auto optBytes = bm.get_bits(optionalBits);
            const auto codedBytes = bm.get_bits(codedBytesBits);
            (void)codedBytes;
            const auto bottomUp = bm.get_bit();
            if (optBytes != 0)
                throw std::runtime_error("DCC: optional data not supported");
            if (bottomUp != 0)
                throw std::runtime_error("DCC: bottom-up frames not supported");

            fr.box_left   = fr.x_offset;
            fr.box_top    = fr.y_offset - fr.height + 1;
            fr.box_right  = fr.box_left + fr.width;
            fr.box_bottom = fr.box_top  + fr.height;
            minx = std::min(minx, fr.box_left);
            miny = std::min(miny, fr.box_top);
            maxx = std::max(maxx, fr.box_right);
            maxy = std::max(maxy, fr.box_bottom);
        }

        const std::int32_t dir_left = minx, dir_top = miny;
        const std::int32_t dir_w = maxx - minx, dir_h = maxy - miny;
        if (dir_w <= 0 || dir_h <= 0)
            throw std::runtime_error("DCC: degenerate direction box");

        std::int32_t equalCellsSize = 0, pixelMaskSize = 0;
        std::int32_t encodingSize   = 0, rawPixelSize  = 0;
        if (compFlags & 0x2) equalCellsSize = std::int32_t(bm.get_bits(20));
        pixelMaskSize = std::int32_t(bm.get_bits(20));
        if (compFlags & 0x1) {
            encodingSize = std::int32_t(bm.get_bits(20));
            rawPixelSize = std::int32_t(bm.get_bits(20));
        }

        // 256-bit palette-entry-valid mask.
        std::array<std::uint8_t, 256> palette{};
        int paletteCount = 0;
        for (int i = 0; i < 256; ++i) {
            if (bm.get_bit()) palette[paletteCount++] = std::uint8_t(i);
        }

        // Fork bitstreams — each starts at the current position and advances
        // its own read counter independently. We then skip the parent by the
        // declared size to reach the next stream.
        Bits equalCellsBs = bm.fork();  bm.skip(equalCellsSize);
        Bits pixelMaskBs  = bm.fork();  bm.skip(pixelMaskSize);
        Bits encTypeBs    = bm.fork();  bm.skip(encodingSize);
        Bits rawPixelBs   = bm.fork();  bm.skip(rawPixelSize);
        Bits pcdBs        = bm.fork();  // pixel codes + displacement (rest)

        // Direction-level cells (4×4 grid).
        constexpr int cells_per_row = 4;
        const int dir_hcells = 1 + (dir_w - 1) / cells_per_row;
        const int dir_vcells = 1 + (dir_h - 1) / cells_per_row;
        std::vector<Cell> dir_cells(std::size_t(dir_hcells) * dir_vcells);
        for (int cy = 0; cy < dir_vcells; ++cy) {
            for (int cx = 0; cx < dir_hcells; ++cx) {
                auto& c = dir_cells[cy * dir_hcells + cx];
                c.x = cx * 4;
                c.y = cy * 4;
                c.w = (cx == dir_hcells - 1) ? dir_w - (cx * 4) : 4;
                c.h = (cy == dir_vcells - 1) ? dir_h - (cy * 4) : 4;
            }
        }

        // Per-frame cells (aligned within the direction canvas).
        std::vector<std::vector<FrameCell>> frame_cells(frames_per_dir_);
        std::vector<int> frame_hcells(frames_per_dir_), frame_vcells(frames_per_dir_);
        for (std::int32_t f = 0; f < frames_per_dir_; ++f) {
            const auto& fr = frames[f];
            int w0 = 4 - ((fr.box_left - dir_left) & 3);
            int h0 = 4 - ((fr.box_top  - dir_top)  & 3);
            int hcnt, vcnt;
            if (fr.width - w0 <= 1) hcnt = 1;
            else { int t = fr.width - w0 - 1; hcnt = 2 + t / 4; if (t % 4 == 0) --hcnt; }
            if (fr.height - h0 <= 1) vcnt = 1;
            else { int t = fr.height - h0 - 1; vcnt = 2 + t / 4; if (t % 4 == 0) --vcnt; }
            frame_hcells[f] = hcnt;
            frame_vcells[f] = vcnt;

            std::vector<int> cw(hcnt), ch(vcnt);
            if (hcnt == 1) cw[0] = fr.width;
            else {
                cw[0] = w0;
                for (int i = 1; i < hcnt - 1; ++i) cw[i] = 4;
                cw[hcnt - 1] = fr.width - w0 - 4 * (hcnt - 2);
            }
            if (vcnt == 1) ch[0] = fr.height;
            else {
                ch[0] = h0;
                for (int i = 1; i < vcnt - 1; ++i) ch[i] = 4;
                ch[vcnt - 1] = fr.height - h0 - 4 * (vcnt - 2);
            }

            frame_cells[f].resize(std::size_t(hcnt) * vcnt);
            int off_y = fr.box_top - dir_top;
            for (int y = 0; y < vcnt; ++y) {
                int off_x = fr.box_left - dir_left;
                for (int x = 0; x < hcnt; ++x) {
                    auto& fc = frame_cells[f][y * hcnt + x];
                    fc.x = off_x; fc.y = off_y;
                    fc.w = cw[x]; fc.h = ch[y];
                    off_x += cw[x];
                }
                off_y += ch[y];
            }
        }

        // === Fill pixel buffer ===
        static constexpr int pixel_mask_lookup[16] =
            {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};

        int max_pbe = 0;
        for (std::int32_t f = 0; f < frames_per_dir_; ++f)
            max_pbe += frame_hcells[f] * frame_vcells[f];
        std::vector<PBEntry> pixel_buffer(max_pbe);

        std::vector<int> cell_buffer(std::size_t(dir_hcells) * dir_vcells, -1);
        int pb_index = -1;
        std::uint32_t last_pixel = 0;

        for (std::int32_t f = 0; f < frames_per_dir_; ++f) {
            const auto& fr = frames[f];
            const int origin_cx = (fr.box_left - dir_left) / cells_per_row;
            const int origin_cy = (fr.box_top  - dir_top)  / cells_per_row;
            const int fhc = frame_hcells[f];
            const int fvc = frame_vcells[f];

            for (int cy = 0; cy < fvc; ++cy) {
                for (int cx = 0; cx < fhc; ++cx) {
                    const int cur = origin_cx + cx +
                                    (cy + origin_cy) * dir_hcells;
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
                        std::uint32_t v;
                        if (encoding_type != 0) {
                            v = rawPixelBs.get_bits(8);
                        } else {
                            v = last_pixel;
                            std::uint32_t disp = pcdBs.get_bits(4);
                            v += disp;
                            while (disp == 15) {
                                disp = pcdBs.get_bits(4);
                                v += disp;
                            }
                        }
                        if (v == last_pixel) {
                            pixel_stack[i] = 0;
                            break;
                        }
                        pixel_stack[i] = v;
                        last_pixel = v;
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
                    pixel_buffer[pb_index].frame = f;
                    pixel_buffer[pb_index].frame_cell_index = cx + cy * fhc;
                }
            }
        }
        // Palette-map values.
        for (int i = 0; i <= pb_index; ++i)
            for (int k = 0; k < 4; ++k)
                pixel_buffer[i].value[k] = palette[pixel_buffer[i].value[k]];

        // === Reconstruct frames ===
        std::vector<std::uint8_t> canvas(std::size_t(dir_w) * dir_h, 0);
        int pb = 0;
        for (std::int32_t f = 0; f < frames_per_dir_; ++f) {
            auto& out = frames_[std::size_t(dir_idx) * frames_per_dir_ + f];
            const auto& fr = frames[f];
            out.width    = fr.width;
            out.height   = fr.height;
            out.x_offset = fr.x_offset;
            out.y_offset = fr.y_offset;
            out.box_left = fr.box_left;   out.box_top    = fr.box_top;
            out.box_right= fr.box_right;  out.box_bottom = fr.box_bottom;
            out.pixels.assign(std::size_t(fr.width) * fr.height, 0);

            for (std::size_t ci = 0; ci < frame_cells[f].size(); ++ci) {
                const auto& fc = frame_cells[f][ci];
                const int dir_cell_x = fc.x / cells_per_row;
                const int dir_cell_y = fc.y / cells_per_row;
                const int dir_cell = dir_cell_x + dir_cell_y * dir_hcells;
                auto& bcell = dir_cells[dir_cell];
                auto& pbe = pixel_buffer[pb];

                if (pbe.frame != f || pbe.frame_cell_index != int(ci)) {
                    // EqualCells: copy previous cell or clear.
                    if (fc.w != bcell.last_w || fc.h != bcell.last_h) {
                        for (int y = 0; y < fc.h; ++y)
                            for (int x = 0; x < fc.w; ++x)
                                canvas[(fc.y + y) * dir_w + fc.x + x] = 0;
                    } else {
                        for (int y = 0; y < fc.h; ++y)
                            for (int x = 0; x < fc.w; ++x)
                                canvas[(fc.y + y) * dir_w + fc.x + x] =
                                    canvas[(bcell.last_y + y) * dir_w
                                           + bcell.last_x + x];
                    }
                } else {
                    if (pbe.value[0] == pbe.value[1]) {
                        // Solid fill.
                        for (int y = 0; y < fc.h; ++y)
                            for (int x = 0; x < fc.w; ++x)
                                canvas[(fc.y + y) * dir_w + fc.x + x] = pbe.value[0];
                    } else {
                        const int bits = (pbe.value[1] != pbe.value[2]) ? 2 : 1;
                        for (int y = 0; y < fc.h; ++y) {
                            for (int x = 0; x < fc.w; ++x) {
                                const auto idx = pcdBs.get_bits(bits);
                                canvas[(fc.y + y) * dir_w + fc.x + x] = pbe.value[idx];
                            }
                        }
                    }
                    ++pb;
                }

                bcell.last_w = fc.w; bcell.last_h = fc.h;
                bcell.last_x = fc.x; bcell.last_y = fc.y;
            }

            // Blit the frame's region from the shared canvas — every cell in
            // this frame's box was either painted this pass or filled by an
            // EqualCells copy, so the canvas rect is complete.
            const int shift_x = fr.box_left - dir_left;
            const int shift_y = fr.box_top  - dir_top;
            for (int y = 0; y < fr.height; ++y) {
                for (int x = 0; x < fr.width; ++x) {
                    out.pixels[y * fr.width + x] =
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
