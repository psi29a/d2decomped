// Round-trip test: build a small procedural RGBA image, save as PNG, then
// re-open via `file(1)`-style header sniff and re-decode via libpng — no,
// simpler: just verify the PNG signature + IHDR-declared dimensions, and
// zlib-decode the IDAT stream back to the same pixel bytes.
#include <screenshot.hpp>

#include <zlib.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

static std::uint32_t rd_u32_be(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16)
         | (std::uint32_t(p[2]) <<  8) |  std::uint32_t(p[3]);
}

int main() {
    // Procedural 32×24 gradient — every pixel unique, so a filter or endian
    // slip would be immediately visible.
    constexpr std::uint32_t W = 32, H = 24;
    std::vector<std::uint8_t> src(W * H * 4);
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            auto* p = src.data() + (y * W + x) * 4;
            p[0] = std::uint8_t(x * 8);
            p[1] = std::uint8_t(y * 10);
            p[2] = std::uint8_t((x + y) * 4);
            p[3] = 0xFF;
        }
    }

    const auto path = fs::temp_directory_path()
        / ("d2d-screenshot-test-" + std::to_string(::getpid()) + ".png");

    const auto wrote = d2d::screenshot::save_png(path, src, W, H);
    assert(wrote > 8);
    std::printf("wrote %zu bytes to %s\n", wrote, path.string().c_str());

    // Slurp file back.
    std::ifstream is(path, std::ios::binary);
    std::vector<std::uint8_t> file(std::istreambuf_iterator<char>(is), {});
    assert(file.size() == wrote);

    // PNG signature.
    static constexpr std::uint8_t kSig[8] =
        {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    assert(std::memcmp(file.data(), kSig, 8) == 0);

    // First chunk: IHDR.
    assert(rd_u32_be(file.data() + 8) == 13);
    assert(std::memcmp(file.data() + 12, "IHDR", 4) == 0);
    assert(rd_u32_be(file.data() + 16) == W);
    assert(rd_u32_be(file.data() + 20) == H);
    assert(file[24] == 8);   // bit depth
    assert(file[25] == 6);   // colour type = RGBA

    // Scan for IDAT and IEND.
    std::size_t p = 8 + 4 + 4 + 13 + 4;   // sig + IHDR (len+type+data+crc)
    std::vector<std::uint8_t> idat;
    bool saw_iend = false;
    while (p < file.size()) {
        const auto len = rd_u32_be(file.data() + p);
        const auto* type = file.data() + p + 4;
        const auto* data = file.data() + p + 8;
        if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            saw_iend = true;
            break;
        }
        p += 4 + 4 + len + 4;
    }
    assert(!idat.empty());
    assert(saw_iend);

    // Zlib-inflate IDAT and split off the per-row filter bytes (all 0).
    const std::size_t rowStride = W * 4;
    std::vector<std::uint8_t> raw(H * (rowStride + 1));
    uLongf raw_size = uLongf(raw.size());
    int rc = ::uncompress(raw.data(), &raw_size, idat.data(), uLong(idat.size()));
    assert(rc == Z_OK);
    assert(raw_size == raw.size());
    for (std::uint32_t y = 0; y < H; ++y) {
        assert(raw[y * (rowStride + 1)] == 0);   // filter type = None
        assert(std::memcmp(raw.data() + y * (rowStride + 1) + 1,
                           src.data() + y * rowStride, rowStride) == 0);
    }

    // Clean up.
    std::error_code ec;
    fs::remove(path, ec);

    std::printf("OK\n");
    return 0;
}
