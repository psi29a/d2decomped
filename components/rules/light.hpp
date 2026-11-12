// SPDX-License-Identifier: GPL-3.0-or-later
// Time of day and light — game.exe 1.14d. The day is ENVIRONMENT\Env.cpp,
// the light grid the client's (0x4744b0..0x475b20).
// docs/research/re/lighting.md.
#pragma once

#include "level_ids.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace d2d::rules {

// The day's six phases (0x7443f0): the degree each starts at and its type
// (0 day, 1 dusk, 2 night, 3 dawn). 128 frames a degree (0x7443e4).
struct DayPhase { int start, type; };
inline constexpr std::array<DayPhase, 6> kDay{ { { 320, 3 }, { 340, 3 }, { 0, 0 }, { 160, 1 }, { 180, 1 }, { 200, 2 } } };
inline constexpr int kDayScale = 128;
// The eclipse's phases (0x744480: Tainted Sun's dark sky), and the starts
// Act 4's eclipse steps past instead (0x744438).
inline constexpr std::array<DayPhase, 6> kEclipse{ { { 300, 3 }, { 0, 0 }, { 60, 1 }, { 120, 2 }, { 180, 2 }, { 240, 2 } } };
inline constexpr std::array<DayPhase, 6> kAct4Eclipse{ { { 340, 3 }, { 350, 3 }, { 0, 0 }, { 180, 1 }, { 190, 1 }, { 200, 2 } } };

// Act 4's light has no sun (FUN_0061bb80 with act 3): each level's target
// from the table at 0x61bcc8, any other level act + 13 = 16.
constexpr int act4_light(int level_id) {
    using namespace level_ids;
    switch (level_id) {
    case kPandemoniumFortress: return 0x80;
    case kOuterSteppes: return 0x40;
    case kPlainsOfDespair: return 0x38;
    case kCityOfTheDamned: return 0x30;
    default: return 16;
    }
}
// ... which the light moves toward one a frame (FUN_0061bb80).
constexpr int light_toward(int light, int target) { return light < target ? light + 1 : light > target ? light - 1 : light; }

// A game's clock; a new game starts at sunrise (FUN_0061be40).
// The eclipse (+0x30) as the client has it from S->C 0x53 (FUN_0061c240):
// its own phases, no night's extra frame, and the light falling 8 a frame
// to 32 (FUN_0061bb80), whatever the hour.
// The phases' colours aren't used: the software renderer draws intensity
// only.
struct Day {
    int phase = 2, time = 0;                   // time: frames into the day
    bool eclipse = false;
    int dimmed = 255;                          // the eclipse's intensity (+0xc), falling
    // The environment's intensity (+0xc, FUN_0061c0e0), what raised roofs
    // take: intensity() but in Act 4, where it fades to act4_light from
    // what the last act left. A new game's is sunrise's 128 (FUN_0061be40).
    int environment = 128;
    // A client frame (FUN_0061bee0, `act` 0-based): one frame; out of the
    // eclipse 16 in Act 4, else two at night (ten in Act 3). Past the next
    // phase's start the phase moves on and the clock snaps to that start
    // (the eclipse's in Act 4 are 0x744438's, but it snaps to 0x744480's).
    // game.exe bug (bugs.md #9): phase 2 starts at 0°, so phase 1 moves on
    // at its first frame and the day is 340° long. Then the environment's
    // intensity in `level_id` (FUN_0061bb80).
    void step(int act = 0, int level_id = 0) {
        ++time;
        if (!eclipse) time += act == 3 ? 15 : kDay[std::size_t(phase)].type != 2 ? 0 : act == 2 ? 9 : 1;
        if (time >= 360 * kDayScale) time = 0;
        const auto& starts = !eclipse ? kDay : act == 3 ? kAct4Eclipse : kEclipse;
        const int next = (phase + 1) % 6;
        if (starts[std::size_t(next)].start * kDayScale < time) { phase = next; time = (eclipse ? kEclipse : kDay)[std::size_t(next)].start * kDayScale; }
        if (eclipse) dim();
        environment = act == 3 ? light_toward(environment, act4_light(level_id)) : intensity();
    }
    // S->C 0x53 (FUN_0061c240): the phase, the time (past a day: 0) and the
    // eclipse; going dark starts from the sun's light and dims a step.
    void set(int new_phase, int new_time, bool dark) {
        phase = std::clamp(new_phase, 0, 5);
        time = new_time > 360 * kDayScale ? 0 : std::max(new_time, 0);
        dimmed = sun();
        eclipse = dark;
        if (eclipse) dim();
    }
    [[nodiscard]] int intensity() const { return eclipse ? dimmed : sun(); }
    // FUN_0061bb80: sin(angle) · 128 + 128, the sine halved past 180°;
    // 0..255. Sunrise 128, noon 255, midnight 64.
    [[nodiscard]] int sun() const {
        auto sine = float(std::sin(double(time) / kDayScale / 180.0 * 3.1415927410125732));
        if (time >= 180 * kDayScale) sine = float(sine * 0.5);
        return std::clamp(int(double(sine) * 128.0 + 128.0 + 0.5), 0, 255);
    }

private:
    void dim() {
        if (dimmed > 32) dimmed -= 8;
        if (dimmed < 32) dimmed = 32;
    }
};

// Diablo's release (lighting.md "What fills it"): S->C 0x89 event 12
// (FUN_005b5230 as the sanctuary's cleared; the client's 0x46b290 sets
// 0x7a7460 in any level) fills River of Flame and the Chaos Sanctuary at
// 160, (255, 64, 48), ahead of their own light (FUN_0046bdd0, asked first
// by FUN_00474550). Event 13 (classic only, at Diablo's death) ends it at
// once; in LoD it holds until the next game. -1: the level's own light.
inline constexpr int kReleaseLight = 0xa0;
constexpr int release_light(int level_id, bool released) {
    return released && (level_id == level_ids::kRiverOfFlame || level_id == level_ids::kChaosSanctuary) ? kReleaseLight : -1;
}

// The light a near room of another level paints (FUN_00474610, after
// FUN_0046bdd0). Release wins. A dip (`dip_light` >= 0) covers every level
// but the Den, River of Flame and the Chaos Sanctuary. Otherwise the
// level's Intensity, or the day's when it has none (`level_light` < 0).
constexpr int border_room_light(int level_id, int level_light, int day_light, int dip_light, bool diablo_red) {
    if (const int released = release_light(level_id, diablo_red); released >= 0) return released;
    if (dip_light >= 0 && level_id != level_ids::kDenOfEvil && level_id != level_ids::kRiverOfFlame && level_id != level_ids::kChaosSanctuary)
        return dip_light;
    if (level_light >= 0) return level_light;
    return day_light;
}

// One near room, in act tiles, and the light its subtiles take.
struct BorderRoom { int x = 0, y = 0, width = 0, height = 0, light = 0; };

// The client's light grid (0x7b0e68): subtiles around the player
// (FUN_00475800), an intensity each. Positions are in eighths of a
// subtile, the way lights hold them (unit position >> 13, + 4).
// game.exe's is 48 x 48 (±24 subtiles): a light further off adds nothing
// and the view's corners at 800 x 600 read the clamped edge (bugs.md #11).
// d2d sizes it to the view plus the widest light instead (kGame for
// game.exe's).
struct LightGrid {
    static constexpr int kGame = 48;
    int grid_size = kGame;                             // entries a side
    int origin_x = 0, origin_y = 0;                        // the subtile of entry (0, 0)
    std::vector<std::uint8_t> values = std::vector<std::uint8_t>(std::size_t(kGame * kGame));
    std::vector<std::uint8_t> blocked = std::vector<std::uint8_t>(std::size_t(kGame * kGame));   // subtiles whose collision has 0x22 (FUN_004756d0)
    [[nodiscard]] bool is_blocked(int subtile_x, int subtile_y) const {   // FUN_00474a30: off the grid counts as blocked
        const int grid_x = subtile_x - origin_x, grid_y = subtile_y - origin_y;
        return grid_x < 0 || grid_y < 0 || grid_x >= grid_size || grid_y >= grid_size || blocked[std::size_t(grid_y * grid_size + grid_x)];
    }
    // Centred on the player's subtile, all at the ambient (FUN_004744b0).
    void reset(int player_x, int player_y, int ambient, int size = kGame) {
        grid_size = size;
        origin_x = player_x - grid_size / 2; origin_y = player_y - grid_size / 2;
        values.assign(std::size_t(grid_size * grid_size), std::uint8_t(std::clamp(ambient, 0, 255)));
        blocked.assign(std::size_t(grid_size * grid_size), 0);
    }
    // Cells under each room take that room's light (FUN_00474610, then each
    // other room of FUN_00619730). A tile is 5 subtiles. The grid starts as
    // the player's room, so same-level rooms are not passed in.
    void paint_border_rooms(int world_x, int world_y, std::span<const BorderRoom> rooms) {
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
    [[nodiscard]] int at(int subtile_x, int subtile_y) const {
        return values[std::size_t(std::clamp(subtile_y - origin_y, 0, grid_size - 1) * grid_size + std::clamp(subtile_x - origin_x, 0, grid_size - 1))];
    }
    // FUN_004747c0: a light's share into the entry at (x, y), capped at 255.
    void add(int x, int y, int amount) {
        const int grid_x = (x >> 3) - origin_x, grid_y = (y >> 3) - origin_y;
        if (grid_x < 0 || grid_y < 0 || grid_x >= grid_size || grid_y >= grid_size) return;
        auto& entry = values[std::size_t(grid_y * grid_size + grid_x)];
        entry = std::uint8_t(std::min(255, entry + amount));
    }
    // A light at (x, y), radius r, intensity i (FUN_004748d0): each entry
    // of its square takes (r − d) · i / r, d the distance (FUN_004740d0:
    // 0.96 · the longer side + 0.4 · the shorter, in 1/1024ths).
    void stamp(int x, int y, int radius, int intensity) {
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
    // at light quality 2). Round the light's subtile, 64 x 64: each blocked
    // subtile 16 (FUN_00474a70); then ring by ring outward from ring 2, each
    // subtile takes the shade of its neighbour toward the light, or a blend
    // of the two by the angle (FUN_00474b50 / FUN_00474c00) — a blocked
    // neighbour passes its 16 on. The light is then scaled by
    // (8 − shade / 2) / 8 and nothing lands where the shade is 16.
    void stamp_shadowed(int x, int y, int radius, int intensity) {
        if (radius < 1 || radius > 255) return;
        constexpr int kGrid = 64, kCenter = 32;
        std::vector<int> blocking(kGrid * kGrid, 0), shaded(kGrid * kGrid, 0);
        const int light_x = x >> 3, light_y = y >> 3, radius_cells = radius >> 3, side = radius * 2 >> 3;   // the light's square
        for (int row = 0; row <= side; ++row)
            for (int col = 0; col <= side; ++col)
                blocking[std::size_t((kCenter - radius_cells + row) * kGrid + kCenter - radius_cells + col)] = is_blocked(light_x - radius_cells + col, light_y - radius_cells + row) ? 16 : 0;
        auto cell = [&](int idx) { return blocking[std::size_t(idx)] ? blocking[std::size_t(idx)] : shaded[std::size_t(idx)]; };
        auto shade = [&](int from_x, int from_y, int row, int col) {
            int dx = x - from_x, dy = y - from_y;
            if (dx == 0 && dy == 0) return;
            const int step_x = dx < 0 ? -1 : 1, step_y = dy < 0 ? -1 : 1;
            dx = std::abs(dx); dy = std::abs(dy);
            const int cell_index = row * kGrid + col;
            int value = 0;
            if (dx == 0) value = cell((row + step_y) * kGrid + col);
            else if (dy == 0) value = cell(cell_index + step_x);
            else if (dy <= dx) { const int fraction = (dy << 8) / dx; value = ((256 - fraction) * cell(cell_index + step_x) + fraction * cell((row + step_y) * kGrid + col + step_x)) >> 8; }
            else { const int fraction = (dx << 8) / dy; value = ((256 - fraction) * cell((row + step_y) * kGrid + col) + fraction * cell((row + step_y) * kGrid + col + step_x)) >> 8; }
            shaded[std::size_t(cell_index)] = value;
        };
        if (radius_cells >= 2)
            for (int ring = 2; ring <= radius_cells; ++ring) {
                const int top = light_y * 8 + 4 - ring * 8, bottom = light_y * 8 + 4 + ring * 8, left = light_x * 8 + 4 - ring * 8, right = light_x * 8 + 4 + ring * 8;
                for (int j = 0; j <= ring; ++j) {
                    const int ring_left = light_x * 8 + 4 - j * 8, ring_right = light_x * 8 + 4 + j * 8, ring_up = light_y * 8 + 4 - j * 8, ring_down = light_y * 8 + 4 + j * 8;
                    shade(ring_left, top, kCenter - ring, kCenter - j);    shade(ring_right, top, kCenter - ring, kCenter + j);
                    shade(ring_left, bottom, kCenter + ring, kCenter - j); shade(ring_right, bottom, kCenter + ring, kCenter + j);
                    shade(right, ring_up, kCenter - j, kCenter + ring);  shade(right, ring_down, kCenter + j, kCenter + ring);
                    shade(left, ring_up, kCenter - j, kCenter - ring);   shade(left, ring_down, kCenter + j, kCenter - ring);
                }
            }
        const int step = (intensity << 16) / radius;
        const int sx0 = (x - (x & 7)) - radius, sy0 = (y - (y & 7)) - radius;
        for (int row = 0; row <= side; ++row)
            for (int col = 0; col <= side; ++col) {
                const int shading = shaded[std::size_t((kCenter - radius_cells + row) * kGrid + kCenter - radius_cells + col)];
                if (shading >= 16) continue;
                const int cell_x = sx0 + col * 8, cell_y = sy0 + row * 8;
                const int dx = std::abs(x - cell_x), dy = std::abs(y - cell_y);
                const int distance = (std::max(dx, dy) * 0x3d7 + std::min(dx, dy) * 0x197) >> 10;
                if (const int strength = ((radius - distance) * step >> 16) * (8 - (shading >> 1)) >> 3; strength > 0) add(cell_x, cell_y, strength);
            }
    }
};

}  // namespace d2d::rules
