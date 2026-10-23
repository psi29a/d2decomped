// SPDX-License-Identifier: GPL-3.0-or-later
// missile-dir-dump — d2d's missile frame directions in the text form
// tools/emu/missile_dirs.py prints game.exe's. Reads "dx<TAB>dy" a line (the
// target less the shooter, in subtiles); prints "dx dy: d8 d16 d32", the
// frame of an 8-, 16- and 32-direction sprite (missile_facing, then
// missile_direction).
// missile-dir-dump table — every 64-step facing's frame for 1 to 32
// directions, as missile_dirs.py's `table`.
#include <ai.hpp>
#include <gamedata.hpp>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    using d2d::game::missile_direction;
    if (argc > 1 && std::strcmp(argv[1], "table") == 0) {
        for (const int dirs : { 1, 2, 4, 8, 16, 32 }) {
            std::printf("%d:", dirs);
            for (int dir64 = 0; dir64 < 64; ++dir64) std::printf(" %d", missile_direction(dir64, dirs));
            std::printf("\n");
        }
        return 0;
    }
    int dx = 0, dy = 0;
    while (std::scanf("%d %d", &dx, &dy) == 2) {
        const int facing = d2d::game::missile_facing(0.1f, 0.1f, float(dx) / 5, float(dy) / 5);   // from subtile (0, 0)'s centre
        std::printf("%d %d: %d %d %d\n", dx, dy, missile_direction(facing, 8), missile_direction(facing, 16), missile_direction(facing, 32));
    }
    return 0;
}
