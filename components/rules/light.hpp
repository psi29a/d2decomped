// Time of day and light — game.exe 1.14d. The day is ENVIRONMENT\Env.cpp,
// the light grid the client's (0x4744b0..0x475b20).
// docs/research/re/lighting.md.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace d2d::rules {

// The day's six phases (0x7443f0): the degree each starts at and its type
// (0 day, 1 dusk, 2 night, 3 dawn). 128 frames a degree (0x7443e4).
struct DayPhase { int start, type; };
inline constexpr std::array<DayPhase, 6> kDay{ { { 320, 3 }, { 340, 3 }, { 0, 0 }, { 160, 1 }, { 180, 1 }, { 200, 2 } } };
inline constexpr int kDayScale = 128;

// A game's clock; a new game starts at sunrise (FUN_0061be40).
// ponytail: Acts 1, 2 and 5; Act 3's faster nights, Act 4's clock and
// the eclipse aren't here. The phases' colours aren't used: the software
// renderer draws intensity only.
struct Day {
    int phase = 2, time = 0;                   // time: frames into the day
    // A client frame (FUN_0061bee0): one frame, two at night. Past the
    // next phase's start the phase moves on and the clock snaps to that
    // start. game.exe bug (bugs.md #9): phase 2 starts at 0°, so phase 1
    // moves on at its first frame and the day is 340° long.
    void step() {
        time += kDay[std::size_t(phase)].type == 2 ? 2 : 1;
        if (time >= 360 * kDayScale) time = 0;
        const auto& next = kDay[std::size_t((phase + 1) % 6)];
        if (next.start * kDayScale < time) { phase = (phase + 1) % 6; time = next.start * kDayScale; }
    }
    // FUN_0061bb80: sin(angle) · 128 + 128, the sine halved past 180°;
    // 0..255. Sunrise 128, noon 255, midnight 64.
    [[nodiscard]] int intensity() const {
        auto s = float(std::sin(double(time) / kDayScale / 180.0 * 3.1415927410125732));
        if (time >= 180 * kDayScale) s = float(s * 0.5);
        return std::clamp(int(double(s) * 128.0 + 128.0 + 0.5), 0, 255);
    }
};

// The client's light grid (0x7b0e68): subtiles around the player
// (FUN_00475800), an intensity each. Positions are in eighths of a
// subtile, the way lights hold them (unit position >> 13, + 4).
// game.exe's is 48 x 48 (±24 subtiles): a light further off adds nothing
// and the view's corners at 800 x 600 read the clamped edge (bugs.md #11).
// d2d sizes it to the view plus the widest light instead (kGame for
// game.exe's).
struct LightGrid {
    static constexpr int kGame = 48;
    int n = kGame;                             // entries a side
    int x0 = 0, y0 = 0;                        // the subtile of entry (0, 0)
    std::vector<std::uint8_t> v = std::vector<std::uint8_t>(std::size_t(kGame * kGame));
    std::vector<std::uint8_t> blocked = std::vector<std::uint8_t>(std::size_t(kGame * kGame));   // subtiles whose collision has 0x22 (FUN_004756d0)
    [[nodiscard]] bool is_blocked(int sx, int sy) const {   // FUN_00474a30: off the grid counts as blocked
        const int cx = sx - x0, cy = sy - y0;
        return cx < 0 || cy < 0 || cx >= n || cy >= n || blocked[std::size_t(cy * n + cx)];
    }
    // Centred on the player's subtile, all at the ambient (FUN_004744b0).
    void reset(int px, int py, int ambient, int size = kGame) {
        n = size;
        x0 = px - n / 2; y0 = py - n / 2;
        v.assign(std::size_t(n * n), std::uint8_t(std::clamp(ambient, 0, 255)));
        blocked.assign(std::size_t(n * n), 0);
    }
    // FUN_00475aa0: the entry for subtile (sx, sy), clamped to the edge.
    [[nodiscard]] int at(int sx, int sy) const {
        return v[std::size_t(std::clamp(sy - y0, 0, n - 1) * n + std::clamp(sx - x0, 0, n - 1))];
    }
    // FUN_004747c0: a light's share into the entry at (x, y), capped at 255.
    void add(int x, int y, int amount) {
        const int cx = (x >> 3) - x0, cy = (y >> 3) - y0;
        if (cx < 0 || cy < 0 || cx >= n || cy >= n) return;
        auto& e = v[std::size_t(cy * n + cx)];
        e = std::uint8_t(std::min(255, e + amount));
    }
    // A light at (x, y), radius r, intensity i (FUN_004748d0): each entry
    // of its square takes (r − d) · i / r, d the distance (FUN_004740d0:
    // 0.96 · the longer side + 0.4 · the shorter, in 1/1024ths).
    void stamp(int x, int y, int r, int i) {
        if (r < 1 || r > 255) return;
        const int step = (i << 16) / r, side = r * 2 >> 3;   // the light's square
        const int sx = (x - (x & 7)) - r, sy = (y - (y & 7)) - r;
        for (int row = 0; row <= side; ++row)
            for (int col = 0; col <= side; ++col) {
                const int cx = sx + col * 8, cy = sy + row * 8;
                const int dx = std::abs(x - cx), dy = std::abs(y - cy);
                const int d = (std::max(dx, dy) * 0x3d7 + std::min(dx, dy) * 0x197) >> 10;
                if (const int s = (r - d) * step >> 16; s > 0) add(cx, cy, s);
            }
    }
    // A light that walls shadow (types 0: the player, objects; FUN_00474d70
    // at light quality 2). Round the light's subtile, 64 x 64: each blocked
    // subtile 16 (FUN_00474a70); then ring by ring outward from ring 2, each
    // subtile takes the shade of its neighbour toward the light, or a blend
    // of the two by the angle (FUN_00474b50 / FUN_00474c00) — a blocked
    // neighbour passes its 16 on. The light is then scaled by
    // (8 − shade / 2) / 8 and nothing lands where the shade is 16.
    void stamp_shadowed(int x, int y, int r, int i) {
        if (r < 1 || r > 255) return;
        constexpr int kT = 64, kC = 32;
        std::vector<int> B(kT * kT, 0), A(kT * kT, 0);
        const int lx = x >> 3, ly = y >> 3, rc = r >> 3, side = r * 2 >> 3;   // the light's square
        for (int row = 0; row <= side; ++row)
            for (int col = 0; col <= side; ++col)
                B[std::size_t((kC - rc + row) * kT + kC - rc + col)] = is_blocked(lx - rc + col, ly - rc + row) ? 16 : 0;
        auto cell = [&](int idx) { return B[std::size_t(idx)] ? B[std::size_t(idx)] : A[std::size_t(idx)]; };
        auto shade = [&](int px, int py, int row, int col) {
            int dx = x - px, dy = y - py;
            if (dx == 0 && dy == 0) return;
            const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
            dx = std::abs(dx); dy = std::abs(dy);
            const int at = row * kT + col;
            int v = 0;
            if (dx == 0) v = cell((row + sy) * kT + col);
            else if (dy == 0) v = cell(at + sx);
            else if (dy <= dx) { const int f = (dy << 8) / dx; v = ((256 - f) * cell(at + sx) + f * cell((row + sy) * kT + col + sx)) >> 8; }
            else { const int f = (dx << 8) / dy; v = ((256 - f) * cell((row + sy) * kT + col) + f * cell((row + sy) * kT + col + sx)) >> 8; }
            A[std::size_t(at)] = v;
        };
        if (rc >= 2)
            for (int d = 2; d <= rc; ++d) {
                const int top = ly * 8 + 4 - d * 8, bottom = ly * 8 + 4 + d * 8, left = lx * 8 + 4 - d * 8, right = lx * 8 + 4 + d * 8;
                for (int j = 0; j <= d; ++j) {
                    const int xl = lx * 8 + 4 - j * 8, xr = lx * 8 + 4 + j * 8, yu = ly * 8 + 4 - j * 8, yd = ly * 8 + 4 + j * 8;
                    shade(xl, top, kC - d, kC - j);    shade(xr, top, kC - d, kC + j);
                    shade(xl, bottom, kC + d, kC - j); shade(xr, bottom, kC + d, kC + j);
                    shade(right, yu, kC - j, kC + d);  shade(right, yd, kC + j, kC + d);
                    shade(left, yu, kC - j, kC - d);   shade(left, yd, kC + j, kC - d);
                }
            }
        const int step = (i << 16) / r;
        const int sx0 = (x - (x & 7)) - r, sy0 = (y - (y & 7)) - r;
        for (int row = 0; row <= side; ++row)
            for (int col = 0; col <= side; ++col) {
                const int a = A[std::size_t((kC - rc + row) * kT + kC - rc + col)];
                if (a >= 16) continue;
                const int cx = sx0 + col * 8, cy = sy0 + row * 8;
                const int dx = std::abs(x - cx), dy = std::abs(y - cy);
                const int d = (std::max(dx, dy) * 0x3d7 + std::min(dx, dy) * 0x197) >> 10;
                if (const int s = ((r - d) * step >> 16) * (8 - (a >> 1)) >> 3; s > 0) add(cx, cy, s);
            }
    }
};

}  // namespace d2d::rules
