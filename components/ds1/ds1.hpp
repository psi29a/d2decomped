// D2Decomp DS1 (Diablo Stamp 1) level parser.
//
// DS1 = a grid of tile references + object placements that make up one map
// stamp. Blizzard reused this format across a decade of builds, so the
// header is version-gated with many optional fields; we cover versions
// 3..18 which spans classic → 1.14d.
//
// What we parse:
//   • header: version, grid dimensions, act, substitution type
//   • referenced file list (relative .dt1 / .dcc paths — the tileset this
//     stamp draws from)
//   • layer counts (walls / floors / shadows)
//   • per-cell tile references (packed dwords, one per layer per cell)
//   • object placements (type / id / x / y / flags)
//
//   • substitution groups: rectangles LvlSub stamps copy from (DRLG)
//
// What we skip (add when needed):
//   • orientation dword decoding beyond the type byte (the rest is
//     "zero" padding — kept as `wall_zero` if the caller cares)
//
// Tile record dword layout (all layers except substitution):
//   bits  0..7   prop1
//   bits  8..13  sequence
//   bits 14..19  unknown1
//   bits 20..25  style
//   bits 26..30  unknown2
//   bit    31    hidden
// The (style, sequence) pair (plus the wall type from the orientation
// stream) is what the renderer feeds into a DT1 tile lookup.
//
// Layer stream ORDER (per Blizzard's writer, per OpenDiablo2's reader):
//   for each wall i:   wall_i dword, then orientation_i dword
//   for each floor i:  floor_i dword
//   if shadows > 0:    shadow dword
//   if subs > 0:       substitution dword
// Each stream is width*height dwords, row-major, top-left origin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2d::ds1 {

struct Tile {
    std::uint8_t prop1{};
    std::uint8_t sequence{};
    std::uint8_t unknown1{};
    std::uint8_t style{};
    std::uint8_t unknown2{};
    bool         hidden{};
    // Wall layers only — from the paired orientation dword:
    std::uint8_t wall_type{};   // low byte of orientation dword
    std::uint32_t wall_zero{};  // upper 24 bits (usually zero)
    // Substitution layers only — raw dword.
    std::uint32_t substitution{};
};

struct Layer {
    std::vector<Tile> cells;   // width * height, row-major
};

struct PathPoint {
    std::int32_t x{}, y{};      // subtiles, like Object::x/y
    std::int32_t action{};      // v15+: what the NPC does on arrival
};

// A substitution group (DS1 substitution type 1 / 2): a rectangle of
// tiles; `variants` (v13+) counts the alternatives laid out to its right.
struct Group { std::int32_t x{}, y{}, width{}, height{}, variants{}; };

struct Object {
    std::int32_t type{};
    std::int32_t id{};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t flags{};
    std::vector<PathPoint> path;   // NPC patrol path (v14+), empty if none
};

class Map {
public:
    Map() = default;
    explicit Map(std::span<const std::byte> bytes) { parse(bytes); }
    // An empty map to build into (the DRLG's generated levels).
    Map(int width, int height, int walls, int floors) : version_(18), width_(width), height_(height), act_(1) {
        const auto cells = std::size_t(width) * std::size_t(height);
        walls_.assign(std::size_t(walls), Layer{ std::vector<Tile>(cells) });
        floors_.assign(std::size_t(floors), Layer{ std::vector<Tile>(cells) });
        shadows_.assign(1, Layer{ std::vector<Tile>(cells) });
    }

    // A tile dword and back (lossless; walls keep their orientation apart).
    [[nodiscard]] static Tile tile(std::uint32_t packed) { Tile decoded; decode_tile_dword(packed, decoded); return decoded; }
    [[nodiscard]] static std::uint32_t word(const Tile& tile) {
        return std::uint32_t(tile.prop1) | std::uint32_t(tile.sequence) << 8 | std::uint32_t(tile.unknown1) << 14
             | std::uint32_t(tile.style) << 20 | std::uint32_t(tile.unknown2) << 26 | (tile.hidden ? 0x80000000u : 0u);
    }

    [[nodiscard]] int version()          const noexcept { return version_; }
    [[nodiscard]] int width()            const noexcept { return width_; }
    [[nodiscard]] int height()           const noexcept { return height_; }
    [[nodiscard]] std::int32_t act()     const noexcept { return act_; }
    [[nodiscard]] const std::vector<std::string>& files()   const noexcept { return files_; }
    [[nodiscard]] const std::vector<Layer>& walls()         const noexcept { return walls_; }
    [[nodiscard]] const std::vector<Layer>& floors()        const noexcept { return floors_; }
    [[nodiscard]] const std::vector<Layer>& shadows()       const noexcept { return shadows_; }
    [[nodiscard]] const std::vector<Object>& objects()      const noexcept { return objects_; }
    [[nodiscard]] int sub_type()                              const noexcept { return sub_type_; }
    [[nodiscard]] const std::vector<Group>& groups()        const noexcept { return groups_; }
    std::vector<Layer>& walls()   noexcept { return walls_; }
    std::vector<Layer>& floors()  noexcept { return floors_; }
    std::vector<Layer>& shadows() noexcept { return shadows_; }

private:
    struct Cursor {
        const std::byte* at;
        const std::byte* end;
        std::int32_t rd_i32() {
            if (at + 4 > end) throw std::runtime_error("DS1: read past end");
            std::int32_t value; std::memcpy(&value, at, 4); at += 4; return value;
        }
        std::uint32_t rd_u32() {
            if (at + 4 > end) throw std::runtime_error("DS1: read past end");
            std::uint32_t value; std::memcpy(&value, at, 4); at += 4; return value;
        }
        std::uint8_t rd_u8() {
            if (at >= end) throw std::runtime_error("DS1: read past end");
            return std::uint8_t(*at++);
        }
        void skip(std::size_t count) {
            if (at + count > end) throw std::runtime_error("DS1: skip past end");
            at += count;
        }
    };

    static void decode_tile_dword(std::uint32_t packed, Tile& tile) {
        tile.prop1     = std::uint8_t( packed        & 0xFF);
        tile.sequence  = std::uint8_t((packed >>  8) & 0x3F);
        tile.unknown1  = std::uint8_t((packed >> 14) & 0x3F);
        tile.style     = std::uint8_t((packed >> 20) & 0x3F);
        tile.unknown2  = std::uint8_t((packed >> 26) & 0x1F);
        tile.hidden    = (packed & 0x80000000u) != 0;
    }

    void parse(std::span<const std::byte> bytes) {
        Cursor cursor{bytes.data(), bytes.data() + bytes.size()};

        version_ = cursor.rd_i32();
        // Sanity — real D2 DS1s span v3..v18 in the wild.
        if (version_ < 0 || version_ > 40)
            throw std::runtime_error("DS1: bogus version");

        width_  = cursor.rd_i32() + 1;
        height_ = cursor.rd_i32() + 1;
        if (width_  <= 0 || width_  > 1024) throw std::runtime_error("DS1: bad width");
        if (height_ <= 0 || height_ > 1024) throw std::runtime_error("DS1: bad height");

        if (version_ >= 8) act_ = cursor.rd_i32() + 1;
        std::int32_t sub_type = 0;
        if (version_ >= 10) sub_type = cursor.rd_i32();
        sub_type_ = sub_type;

        // File list (v3+).
        if (version_ >= 3) {
            const auto file_count = cursor.rd_i32();
            if (file_count < 0 || file_count > 64) throw std::runtime_error("DS1: bogus file count");
            files_.resize(file_count);
            for (auto& file : files_) {
                while (true) {
                    const auto letter = cursor.rd_u8();
                    if (letter == 0) break;
                    file.push_back(static_cast<char>(letter));
                }
            }
        }

        // Unknown 8-byte block (v9..v13 only).
        if (version_ >= 9 && version_ <= 13) cursor.skip(8);

        // Layer counts.
        std::int32_t num_walls = 0;
        std::int32_t num_floors = 1;                // default when not specified
        std::int32_t num_shadows = 1;               // always exactly 1 unless zero
        if (version_ >= 4) {
            num_walls = cursor.rd_i32();
            if (version_ >= 16) num_floors = cursor.rd_i32();
        }
        // We don't implement substitutions; count is inferred from sub_type
        // (0 = none, 1/2 = one layer). Keep the number for read-skip below.
        const std::int32_t num_subs =
            (version_ >= 12 && (sub_type == 1 || sub_type == 2)) ? 1 : 0;

        if (num_walls   < 0 || num_walls   > 4) throw std::runtime_error("DS1: bad walls");
        if (num_floors  < 0 || num_floors  > 2) throw std::runtime_error("DS1: bad floors");

        walls_.assign(num_walls,  Layer{});
        floors_.assign(num_floors, Layer{});
        shadows_.assign(num_shadows, Layer{});
        const auto cells = std::size_t(width_) * height_;
        for (auto& layer : walls_)   layer.cells.assign(cells, Tile{});
        for (auto& layer : floors_)  layer.cells.assign(cells, Tile{});
        for (auto& layer : shadows_) layer.cells.assign(cells, Tile{});

        // Layer stream: interleaved per Blizzard's order.
        // For versions <4 the schema is one wall + one floor + orientation +
        // one substitute + one shadow, back-to-back. We don't hit that in
        // practice (D2 LoD uses v17+), so throw fast if encountered.
        if (version_ < 4)
            throw std::runtime_error("DS1: v<4 layer schema not supported");

        std::vector<std::uint32_t> sub_stream;
        if (num_subs) sub_stream.assign(cells, 0);

        for (std::int32_t wall = 0; wall < num_walls; ++wall) {
            // wall dword
            for (std::size_t i = 0; i < cells; ++i) {
                decode_tile_dword(cursor.rd_u32(), walls_[wall].cells[i]);
            }
            // orientation dword (paired with each wall)
            for (std::size_t i = 0; i < cells; ++i) {
                const auto packed = cursor.rd_u32();
                auto& tile = walls_[wall].cells[i];
                tile.wall_type = std::uint8_t(packed & 0xFF);
                tile.wall_zero = (packed >> 8) & 0x00FFFFFFu;
            }
        }
        for (std::int32_t floor = 0; floor < num_floors; ++floor) {
            for (std::size_t i = 0; i < cells; ++i) {
                decode_tile_dword(cursor.rd_u32(), floors_[floor].cells[i]);
            }
        }
        if (!shadows_.empty()) {
            for (std::size_t i = 0; i < cells; ++i) {
                decode_tile_dword(cursor.rd_u32(), shadows_[0].cells[i]);
            }
        }
        if (num_subs) {
            for (std::size_t i = 0; i < cells; ++i) {
                sub_stream[i] = cursor.rd_u32();
            }
        }

        // Objects (v3+).
        if (version_ >= 3) {
            const auto object_count = cursor.rd_i32();
            if (object_count < 0 || object_count > 100000) throw std::runtime_error("DS1: bogus object count");
            objects_.resize(object_count);
            for (auto& object : objects_) {
                object.type  = cursor.rd_i32();
                object.id    = cursor.rd_i32();
                object.x     = cursor.rd_i32();
                object.y     = cursor.rd_i32();
                object.flags = cursor.rd_i32();
            }
        }

        // Substitution groups (v12+, sub_type 1/2).
        if (version_ >= 12 && (sub_type == 1 || sub_type == 2) && cursor.at < cursor.end) {
            if (version_ >= 18) cursor.skip(4);
            const auto group_count = cursor.rd_i32();
            if (group_count < 0 || group_count > 100000) throw std::runtime_error("DS1: bogus group count");
            groups_.resize(std::size_t(group_count));
            // Act1/Outdoors/Trees.ds1 (v12) says 14 groups and ends 12 bytes
            // into the 14th; game.exe (FUN_00665950) reads on past its buffer.
            // Past the end reads as 0 here (a 0x0 group that stamps nothing).
            auto read_i32 = [&] { return cursor.at + 4 <= cursor.end ? cursor.rd_i32() : (cursor.at = cursor.end, 0); };
            for (auto& group : groups_) {
                group.x = read_i32(); group.y = read_i32(); group.width = read_i32(); group.height = read_i32();
                if (version_ >= 13) group.variants = read_i32();
            }
        }

        // NPC paths (v14+): {count, x, y} then `count` points. The path
        // belongs to the object standing at (x, y).
        if (version_ >= 14 && cursor.at < cursor.end) {
            const auto point_count = cursor.rd_i32();
            if (point_count < 0 || point_count > 100000) throw std::runtime_error("DS1: bogus path count");
            for (std::int32_t k = 0; k < point_count; ++k) {
                const auto count = cursor.rd_i32();
                const auto x = cursor.rd_i32(), y = cursor.rd_i32();
                if (count < 0 || count > 10000) throw std::runtime_error("DS1: bogus path length");
                std::vector<PathPoint> path(static_cast<std::size_t>(count));
                for (auto& point : path) {
                    point.x = cursor.rd_i32();
                    point.y = cursor.rd_i32();
                    if (version_ >= 15) point.action = cursor.rd_i32();
                }
                for (auto& object : objects_)
                    if (object.x == x && object.y == y) { object.path = path; break; }
            }
        }
    }

    int                        version_{};
    int                        width_{};
    int                        height_{};
    std::int32_t               act_{};
    std::vector<std::string>   files_;
    std::vector<Layer>         walls_;
    std::vector<Layer>         floors_;
    std::vector<Layer>         shadows_;
    std::vector<Object>        objects_;
    int                        sub_type_{};
    std::vector<Group>         groups_;
};

}  // namespace d2d::ds1
