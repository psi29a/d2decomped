// SPDX-License-Identifier: GPL-3.0-or-later
// The automap's files beside a save (UI\automap.cpp; docs/research/re/
// automap.md "Saved maps"): Name.map lists the last 4 map seeds, Name.ma0..3
// hold each one's cells, per Levels.txt Layer.
//
// Name.map, 24 bytes (FUN_00457f40): u32 12, u32 next slot, u32 seed[4].
// A seed found is its slot's file; a new one takes `next` (next = (next + 1)
// % 4) and that slot's file is deleted. A short or other-version file starts
// over: 12, 1, the seed in slot 0, all four .ma files deleted.
//
// Name.maN: u32 head[100], by layer, each the file offset of the layer's
// first record (0: none). A record (FUN_00458200 appends one, FUN_00458750
// reads the chain) is u32 next, layer, cel file, object seed, then four byte
// counts, then the four lists: floors, walls, units (kept only while the
// object seed matches), the town miniatures (in the cel file's cels). A cell
// is u16 cel, x, y in automap pixels; each list in (y, x, cel) order. A
// save appends only the cells added since the layer was loaded.
// Little-endian throughout.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

namespace d2d::d2s {

struct AutomapCell { std::int16_t cel = 0, x = 0, y = 0; };
struct AutomapLayer {
    std::uint32_t cel_file = 0;      // 0 the act's own cels; 1..3 a town miniature's
    std::uint32_t object_seed = 0;   // game +0x80, FUN_00546c60's draw
    std::array<std::vector<AutomapCell>, 4> lists;   // floors, walls, units, miniatures
};

inline constexpr std::uint32_t kAutomapIndexVersion = 12;
inline constexpr int kAutomapLayers = 100;

namespace detail {
inline std::uint32_t automap_get32(const unsigned char* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 | std::uint32_t(bytes[3]) << 24;
}
inline void automap_put32(unsigned char* bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(value >> (8 * i));
}
inline void automap_write32(std::fstream& file, std::uint32_t value) {
    unsigned char bytes[4];
    automap_put32(bytes, value);
    file.write(reinterpret_cast<const char*>(bytes), 4);
}
// CreateFile's OPEN_ALWAYS: read and write, made empty when missing.
inline bool automap_open(std::fstream& file, const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) std::ofstream(path, std::ios::binary);
    file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    return bool(file);
}
inline std::array<std::uint32_t, kAutomapLayers> automap_heads(std::fstream& file, bool& whole) {
    std::array<unsigned char, 4 * kAutomapLayers> raw{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()));
    whole = file.gcount() == std::streamsize(raw.size());
    file.clear();
    std::array<std::uint32_t, kAutomapLayers> heads{};
    if (whole) for (std::size_t i = 0; i < heads.size(); ++i) heads[i] = automap_get32(raw.data() + 4 * i);
    return heads;
}
inline void automap_put_heads(std::fstream& file, const std::array<std::uint32_t, kAutomapLayers>& heads) {
    file.seekp(0);
    for (const auto head : heads) automap_write32(file, head);
}
}   // namespace detail

// FUN_00457f40: the .ma file for `map_seed`, Name.map brought up to date on
// the way. "" when Name.map can't be made.
// game.exe first tries the save dir's subdirectory DAT_007a0500, which
// FUN_0044d900 copies from the launch info's +0xd5 (32 chars, after the
// character's name at +0xbd): by its place a Battle.net account, not
// confirmed. d2d never logs into Battle.net, so only the save dir.
inline std::filesystem::path automap_file(const std::filesystem::path& dir, const std::string& name, std::uint32_t map_seed) {
    namespace fs = std::filesystem;
    const auto base = (dir / name).string();
    const auto slot_file = [&](std::uint32_t which) { return fs::path(base + ".ma" + std::to_string(which)); };
    std::fstream index;
    if (!detail::automap_open(index, base + ".map")) return {};
    unsigned char head[24] = {};
    index.read(reinterpret_cast<char*>(head), 24);
    const bool whole = index.gcount() == 24;
    index.clear();
    std::error_code error;
    std::uint32_t slot = detail::automap_get32(head + 4);
    if (whole && detail::automap_get32(head) == kAutomapIndexVersion && slot < 4) {
        for (std::uint32_t i = 0; i < 4; ++i)
            if (detail::automap_get32(head + 8 + 4 * i) == map_seed) return slot_file(i);
        detail::automap_put32(head + 4, (slot + 1) % 4);
        detail::automap_put32(head + 8 + 4 * slot, map_seed);
        fs::remove(slot_file(slot), error);
    } else {
        // A `next` past 3 starts over here; game.exe writes past seed[3] (bugs.md #40).
        detail::automap_put32(head, kAutomapIndexVersion);
        detail::automap_put32(head + 4, 1);
        detail::automap_put32(head + 8, map_seed);
        for (std::uint32_t i = 0; i < 4; ++i) fs::remove(slot_file(i), error);
        slot = 0;
    }
    index.seekp(0);
    index.write(reinterpret_cast<const char*>(head), 24);
    return slot_file(slot);
}

// FUN_00458750: every record of `layer`, in file order. A record of another
// layer cuts the chain there (FUN_004586e0) and ends it. A cel at or past its
// cel file's count (`cel_counts`: [0] for lists 0..2, [cel_file] for list 3;
// < 0 unknown, not checked) cuts the chain at that record too and ends that
// list, but the record's other lists and the rest of the chain still load.
inline AutomapLayer read_automap(const std::filesystem::path& path, int layer, std::uint32_t object_seed,
                                 const std::array<int, 4>& cel_counts = { -1, -1, -1, -1 }) {
    AutomapLayer out;
    out.object_seed = object_seed;
    if (layer < 0 || layer >= kAutomapLayers) return out;
    std::fstream file;
    if (!detail::automap_open(file, path)) return out;
    bool whole = false;
    auto heads = detail::automap_heads(file, whole);
    if (!whole) return out;
    std::uint32_t offset = heads[std::size_t(layer)], previous = 0;
    while (offset != 0) {
        unsigned char record[32];
        file.seekg(std::streamoff(offset));
        file.read(reinterpret_cast<char*>(record), 32);
        if (file.gcount() != 32) break;
        const auto cut = [&] {   // FUN_004586e0: the previous record's next, or the head, to 0
            const auto read_at = file.tellg();
            if (previous != 0) {
                file.seekp(std::streamoff(previous));
                detail::automap_write32(file, 0);
            } else {
                heads[std::size_t(layer)] = 0;
                detail::automap_put_heads(file, heads);
            }
            file.seekg(read_at);
        };
        if (int(detail::automap_get32(record + 4)) != layer) {
            cut();
            break;
        }
        out.cel_file = detail::automap_get32(record + 8);
        for (std::size_t list = 0; list < 4; ++list) {
            const std::uint32_t size = detail::automap_get32(record + 16 + 4 * list);
            if (size == 0) continue;
            if (list == 2 && detail::automap_get32(record + 12) != object_seed) {
                file.seekg(std::streamoff(size), std::ios::cur);   // other objects: not theirs
                continue;
            }
            std::vector<unsigned char> raw(size);
            file.read(reinterpret_cast<char*>(raw.data()), std::streamsize(size));
            if (file.gcount() != std::streamsize(size)) return out;
            const int count = list == 3 ? (out.cel_file < 4 ? cel_counts[out.cel_file] : -1) : cel_counts[0];
            for (std::size_t at = 0; at + 6 <= raw.size(); at += 6) {
                const auto cel = std::int16_t(raw[at] | raw[at + 1] << 8);
                if (count >= 0 && cel >= count) { cut(); break; }
                out.lists[list].push_back({ cel, std::int16_t(raw[at + 2] | raw[at + 3] << 8), std::int16_t(raw[at + 4] | raw[at + 5] << 8) });
            }
        }
        previous = offset;
        offset = detail::automap_get32(record);
    }
    return out;
}

// FUN_00458200: `cells` as a new record at the end of the file, linked from
// the layer's last record (or its head). Nothing written when all four are
// empty (FUN_004584c0 doesn't open the file then).
inline bool append_automap(const std::filesystem::path& path, int layer, const AutomapLayer& cells) {
    if (layer < 0 || layer >= kAutomapLayers) return false;
    if (std::all_of(cells.lists.begin(), cells.lists.end(), [](const auto& list) { return list.empty(); })) return true;
    std::fstream file;
    if (!detail::automap_open(file, path)) return false;
    bool whole = false;
    auto heads = detail::automap_heads(file, whole);
    if (!whole) detail::automap_put_heads(file, heads);
    file.seekg(0, std::ios::end);
    const auto end = std::uint32_t(file.tellg());
    if (std::uint32_t last = heads[std::size_t(layer)]; last != 0) {
        for (;;) {
            unsigned char next[4] = {};
            file.seekg(std::streamoff(last));
            file.read(reinterpret_cast<char*>(next), 4);
            file.clear();
            if (detail::automap_get32(next) == 0) break;
            last = detail::automap_get32(next);
        }
        file.seekp(std::streamoff(last));
        detail::automap_write32(file, end);
    } else {
        heads[std::size_t(layer)] = end;
    }
    detail::automap_put_heads(file, heads);
    file.seekp(std::streamoff(end));
    detail::automap_write32(file, 0);
    detail::automap_write32(file, std::uint32_t(layer));
    detail::automap_write32(file, cells.cel_file);
    detail::automap_write32(file, cells.object_seed);
    for (const auto& list : cells.lists) detail::automap_write32(file, std::uint32_t(list.size() * 6));
    // The tree's in-order walk (FUN_00458470): y, then x, then the cel.
    for (auto list : cells.lists) {
        std::sort(list.begin(), list.end(), [](const AutomapCell& lhs, const AutomapCell& rhs) {
            return std::tie(lhs.y, lhs.x, lhs.cel) < std::tie(rhs.y, rhs.x, rhs.cel);
        });
        for (const auto& cell : list)
            for (const std::int16_t value : { cell.cel, cell.x, cell.y }) {
                const unsigned char bytes[2] = { static_cast<unsigned char>(value & 0xff), static_cast<unsigned char>((value >> 8) & 0xff) };
                file.write(reinterpret_cast<const char*>(bytes), 2);
            }
    }
    return bool(file);
}

}   // namespace d2d::d2s
