// SPDX-License-Identifier: GPL-3.0-or-later
// The automap's files beside a save (d2s_automap.hpp): Name.map's seed
// slots, Name.maN's records written and read back, in a temp dir.
#include <d2s_automap.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

static std::vector<unsigned char> bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> out;
    for (std::istreambuf_iterator<char> it(in), end; it != end; ++it) out.push_back(static_cast<unsigned char>(*it));
    return out;
}
static std::uint32_t u32(const std::vector<unsigned char>& bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 | std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24;
}

int main() {
    using namespace d2d::d2s;
    const auto dir = fs::temp_directory_path() / "d2d_test_automap";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // A first seed takes slot 0: 12, next 1, the seed.
    const auto file = automap_file(dir, "Hero", 0xabcd);
    assert(file == dir / "Hero.ma0");
    auto index = bytes_of(dir / "Hero.map");
    assert(index.size() == 24 && u32(index, 0) == 12 && u32(index, 4) == 1 && u32(index, 8) == 0xabcd && u32(index, 12) == 0);

    // Two saves of layer 1 chain; layer 0's sits between them.
    AutomapLayer first;
    first.object_seed = 77;
    first.lists[0] = { { 5, 10, 20 }, { 3, -8, 4 } };
    first.lists[2] = { { 9, 1, 1 } };
    assert(append_automap(file, 1, first));
    AutomapLayer other;
    other.lists[1] = { { 6, 0, 0 } };
    assert(append_automap(file, 0, other));
    AutomapLayer second;
    second.object_seed = 78;
    second.lists[1] = { { 7, 2, 2 } };
    second.lists[2] = { { 4, 3, 3 } };
    assert(append_automap(file, 1, second));
    assert(append_automap(file, 2, AutomapLayer{}));   // nothing new: nothing written

    const auto raw = bytes_of(file);
    assert(raw.size() == 400 + (32 + 18) + (32 + 6) + (32 + 12));
    assert(u32(raw, 0) == 450 && u32(raw, 4) == 400 && u32(raw, 8) == 0);   // heads by layer
    assert(u32(raw, 400) == 488);                                    // layer 1's first record links to its second
    assert(u32(raw, 404) == 1 && u32(raw, 412) == 77 && u32(raw, 416) == 12 && u32(raw, 424) == 6);
    // Floors in (y, x, cel) order: (3, -8, 4) first.
    assert(raw[432] == 3 && raw[434] == 0xf8 && raw[435] == 0xff && raw[436] == 4);

    // Read back: the units of the other object seed are left out.
    const auto back = read_automap(file, 1, 78);
    assert(back.lists[0].size() == 2 && back.lists[0][0].cel == 3 && back.lists[0][0].x == -8 && back.lists[0][1].y == 20);
    assert(back.lists[1].size() == 1 && back.lists[1][0].cel == 7);
    assert(back.lists[2].size() == 1 && back.lists[2][0].cel == 4);
    assert(read_automap(file, 0, 0).lists[1].size() == 1 && read_automap(file, 3, 0).lists[0].empty());

    // Other seeds take the next slots; the first is found again; a fifth
    // wraps to slot 0 and drops its old file.
    assert(automap_file(dir, "Hero", 2) == dir / "Hero.ma1");
    assert(automap_file(dir, "Hero", 0xabcd) == dir / "Hero.ma0" && fs::exists(dir / "Hero.ma0"));
    assert(automap_file(dir, "Hero", 3) == dir / "Hero.ma2");
    assert(automap_file(dir, "Hero", 4) == dir / "Hero.ma3");
    assert(automap_file(dir, "Hero", 5) == dir / "Hero.ma0" && !fs::exists(dir / "Hero.ma0"));
    index = bytes_of(dir / "Hero.map");
    assert(u32(index, 4) == 1 && u32(index, 8) == 5 && u32(index, 12) == 2);

    fs::remove_all(dir);
    std::printf("OK\n");
    return 0;
}
