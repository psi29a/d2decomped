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
// What we skip (add when needed):
//   • substitution groups (used for dynamic tile swap — quest state)
//   • NPCs + NPC action paths (version > 14)
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

struct Object {
    std::int32_t type{};
    std::int32_t id{};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t flags{};
};

class Map {
public:
    Map() = default;
    explicit Map(std::span<const std::byte> bytes) { parse(bytes); }

    [[nodiscard]] int version()          const noexcept { return version_; }
    [[nodiscard]] int width()            const noexcept { return width_; }
    [[nodiscard]] int height()           const noexcept { return height_; }
    [[nodiscard]] std::int32_t act()     const noexcept { return act_; }
    [[nodiscard]] const std::vector<std::string>& files()   const noexcept { return files_; }
    [[nodiscard]] const std::vector<Layer>& walls()         const noexcept { return walls_; }
    [[nodiscard]] const std::vector<Layer>& floors()        const noexcept { return floors_; }
    [[nodiscard]] const std::vector<Layer>& shadows()       const noexcept { return shadows_; }
    [[nodiscard]] const std::vector<Object>& objects()      const noexcept { return objects_; }

private:
    struct Cursor {
        const std::byte* p;
        const std::byte* end;
        std::int32_t rd_i32() {
            if (p + 4 > end) throw std::runtime_error("DS1: read past end");
            std::int32_t v; std::memcpy(&v, p, 4); p += 4; return v;
        }
        std::uint32_t rd_u32() {
            if (p + 4 > end) throw std::runtime_error("DS1: read past end");
            std::uint32_t v; std::memcpy(&v, p, 4); p += 4; return v;
        }
        std::uint8_t rd_u8() {
            if (p >= end) throw std::runtime_error("DS1: read past end");
            return std::uint8_t(*p++);
        }
        void skip(std::size_t n) {
            if (p + n > end) throw std::runtime_error("DS1: skip past end");
            p += n;
        }
    };

    static void decode_tile_dword(std::uint32_t dw, Tile& t) {
        t.prop1     = std::uint8_t( dw        & 0xFF);
        t.sequence  = std::uint8_t((dw >>  8) & 0x3F);
        t.unknown1  = std::uint8_t((dw >> 14) & 0x3F);
        t.style     = std::uint8_t((dw >> 20) & 0x3F);
        t.unknown2  = std::uint8_t((dw >> 26) & 0x1F);
        t.hidden    = (dw & 0x80000000u) != 0;
    }

    void parse(std::span<const std::byte> b) {
        Cursor c{b.data(), b.data() + b.size()};

        version_ = c.rd_i32();
        // Sanity — real D2 DS1s span v3..v18 in the wild.
        if (version_ < 0 || version_ > 40)
            throw std::runtime_error("DS1: bogus version");

        width_  = c.rd_i32() + 1;
        height_ = c.rd_i32() + 1;
        if (width_  <= 0 || width_  > 1024) throw std::runtime_error("DS1: bad width");
        if (height_ <= 0 || height_ > 1024) throw std::runtime_error("DS1: bad height");

        if (version_ >= 8) act_ = c.rd_i32() + 1;
        std::int32_t sub_type = 0;
        if (version_ >= 10) sub_type = c.rd_i32();

        // File list (v3+).
        if (version_ >= 3) {
            const auto n = c.rd_i32();
            if (n < 0 || n > 64) throw std::runtime_error("DS1: bogus file count");
            files_.resize(n);
            for (auto& s : files_) {
                while (true) {
                    const auto ch = c.rd_u8();
                    if (ch == 0) break;
                    s.push_back(static_cast<char>(ch));
                }
            }
        }

        // Unknown 8-byte block (v9..v13 only).
        if (version_ >= 9 && version_ <= 13) c.skip(8);

        // Layer counts.
        std::int32_t num_walls = 0;
        std::int32_t num_floors = 1;                // default when not specified
        std::int32_t num_shadows = 1;               // always exactly 1 unless zero
        if (version_ >= 4) {
            num_walls = c.rd_i32();
            if (version_ >= 16) num_floors = c.rd_i32();
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
        for (auto& l : walls_)   l.cells.assign(cells, Tile{});
        for (auto& l : floors_)  l.cells.assign(cells, Tile{});
        for (auto& l : shadows_) l.cells.assign(cells, Tile{});

        // Layer stream: interleaved per Blizzard's order.
        // For versions <4 the schema is one wall + one floor + orientation +
        // one substitute + one shadow, back-to-back. We don't hit that in
        // practice (D2 LoD uses v17+), so throw fast if encountered.
        if (version_ < 4)
            throw std::runtime_error("DS1: v<4 layer schema not supported");

        std::vector<std::uint32_t> sub_stream;
        if (num_subs) sub_stream.assign(cells, 0);

        for (std::int32_t w = 0; w < num_walls; ++w) {
            // wall dword
            for (std::size_t i = 0; i < cells; ++i) {
                decode_tile_dword(c.rd_u32(), walls_[w].cells[i]);
            }
            // orientation dword (paired with each wall)
            for (std::size_t i = 0; i < cells; ++i) {
                const auto dw = c.rd_u32();
                auto& t = walls_[w].cells[i];
                t.wall_type = std::uint8_t(dw & 0xFF);
                t.wall_zero = (dw >> 8) & 0x00FFFFFFu;
            }
        }
        for (std::int32_t f = 0; f < num_floors; ++f) {
            for (std::size_t i = 0; i < cells; ++i) {
                decode_tile_dword(c.rd_u32(), floors_[f].cells[i]);
            }
        }
        if (!shadows_.empty()) {
            for (std::size_t i = 0; i < cells; ++i) {
                decode_tile_dword(c.rd_u32(), shadows_[0].cells[i]);
            }
        }
        if (num_subs) {
            for (std::size_t i = 0; i < cells; ++i) {
                sub_stream[i] = c.rd_u32();
            }
        }

        // Objects (v3+).
        if (version_ >= 3) {
            const auto n = c.rd_i32();
            if (n < 0 || n > 100000) throw std::runtime_error("DS1: bogus object count");
            objects_.resize(n);
            for (auto& o : objects_) {
                o.type  = c.rd_i32();
                o.id    = c.rd_i32();
                o.x     = c.rd_i32();
                o.y     = c.rd_i32();
                o.flags = c.rd_i32();
            }
        }

        // Substitutions, NPCs, and NPC action paths follow. Not parsed — the
        // caller can layer another pass on the remaining bytes when needed.
        // ponytail: skip trailing tail. add when a subsystem needs it.
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
};

}  // namespace d2d::ds1
