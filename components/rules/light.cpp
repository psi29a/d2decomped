// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of light.hpp's functions, with the research behind each.
#include "light.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace d2d::rules {

// A light's radius in eighths at quality 0 (FUN_004748d0's head): another
// player's (type 0, not the player's own unit) over 15 is 16, a type 1 one
// (monsters', missiles') 8, a type 3 one 0 (not stamped). Other qualities
// and types keep it.
int light_radius8(int quality, int type, bool own, int radius8) {
    if (quality != 0 || radius8 <= 0) return radius8;              // 0: not stamped at all (radius - 1 over 0xfe)
    if (type == 0 && !own && radius8 > 15) return 16;
    if (type == 1) return 8;
    return type == 3 ? 0 : radius8;
}



// S->C 0x53 (FUN_0061c240): the phase, the time (past a day: 0) and the
// eclipse; going dark starts from the sun's light and dims a step.
auto Day::set(int new_phase, int new_time, bool dark) -> void {
    phase = std::clamp(new_phase, 0, 5);
    time = new_time > 360 * kDayScale ? 0 : std::max(new_time, 0);
    dimmed = sun();
    eclipse = dark;
    if (eclipse) dim();
}

// A frame drawn at `now_ms`: the quality to light it with.
auto LightQuality::frame(int option, std::uint32_t now_ms) -> int {
    if (!counting) { counting = true; since_ms = now_ms; }
    if (now_ms - since_ms > 3000) { fps = frames / 3; since_ms = now_ms; frames = 0; }
    ++frames;
    int wanted = option > 0 ? 2 : 0;
    if (option > 0) {
        if (fps <= 9) wanted = 0;
        else if (fps <= 12) wanted = 1;
        else if (fps <= 15) { if (level >= 1) return level; wanted = 1; }
    }
    if (wanted != level) {
        if (changed && now_ms - changed_ms <= 2000) return level;
        changed = true; changed_ms = now_ms; level = wanted;
    }
    return wanted;
}

// Cells under each room take that room's light (FUN_00474610, then each
// other room of FUN_00619730). A tile is 5 subtiles. The grid starts as
// the player's room, so same-level rooms are not passed in.
auto LightGrid::paint_border_rooms(int world_x, int world_y, std::span<const BorderRoom> rooms) -> void {
    auto act_tile = [](int subtile) {
        const int tile = subtile / 5;
        const int remainder = subtile % 5;
        return remainder != 0 && subtile < 0 ? tile - 1 : tile;
    };
    for (const BorderRoom& room : rooms) {
        if (room.width <= 0 || room.height <= 0) continue;
        const auto room_light = std::uint8_t(std::clamp(room.light, 0, 255));
        for (int row = 0; row < grid_size; ++row) {
            for (int column = 0; column < grid_size; ++column) {
                const int tile_x = world_x + act_tile(origin_x + column);
                const int tile_y = world_y + act_tile(origin_y + row);
                if (tile_x < room.x || tile_y < room.y || tile_x >= room.x + room.width || tile_y >= room.y + room.height) continue;
                values[std::size_t(row * grid_size + column)] = room_light;
            }
        }
    }
}

// FUN_00475aa0: the entry for subtile (sx, sy), clamped to the edge.
auto LightGrid::at(int subtile_x, int subtile_y) const -> int {
    return values[std::size_t(std::clamp(subtile_y - origin_y, 0, grid_size - 1) * grid_size + std::clamp(subtile_x - origin_x, 0, grid_size - 1))];
}

// A light at (x, y), radius r, intensity i (FUN_004748d0): each entry
// of its square takes (r − d) · i / r, d the distance (FUN_004740d0:
// 0.96 · the longer side + 0.4 · the shorter, in 1/1024ths).
auto LightGrid::stamp(int x, int y, int radius, int intensity) -> void {
    if (radius < 1 || radius > 255) return;
    const int step = (intensity << 16) / radius, side = radius * 2 >> 3;   // the light's square
    const int start_x = (x - (x & 7)) - radius, start_y = (y - (y & 7)) - radius;
    for (int row = 0; row <= side; ++row)
        for (int col = 0; col <= side; ++col) {
            const int cell_x = start_x + col * 8, cell_y = start_y + row * 8;
            const int dx = std::abs(x - cell_x), dy = std::abs(y - cell_y);
            const int distance = (std::max(dx, dy) * 0x3d7 + std::min(dx, dy) * 0x197) >> 10;
            if (const int strength = (radius - distance) * step >> 16; strength > 0) add(cell_x, cell_y, strength);
        }
}

// A light that walls shadow (types 0: the player, objects; FUN_00474d70
// at light quality 2): shade_square round it, then stamp_shaded.
auto LightGrid::stamp_shadowed(int x, int y, int radius, int intensity) -> void {
    stamp_shaded(x, y, radius, intensity, shade_square(x, y, radius, [&](int subtile_x, int subtile_y) { return is_blocked(subtile_x, subtile_y); }));
}

}
