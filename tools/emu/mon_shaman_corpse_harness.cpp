// SPDX-License-Identifier: GPL-3.0-or-later
// d2d side of tools/emu/mon_shaman_corpse.py: reads `class flags corpse_bit mode dx dy aip4 game` lines, prints 1 / 0 for whether
// d2d's rule takes the candidate as a unique shaman's corpse. --old: the rule ai.cpp had before shaman_corpse_reach.
#include <monsters.hpp>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    const bool old_rule = argc > 1 && std::strcmp(argv[1], "--old") == 0;
    int base = 0, flags = 0, corpse = 0, mode = 0, dx = 0, dy = 0, aip4 = 0, game = 0;
    while (std::scanf("%d %d %d %d %d %d %d %d", &base, &flags, &corpse, &mode, &dx, &dy, &aip4, &game) == 8) {
        const bool dead = mode == 0xc && corpse != 0;                              // ai.cpp's loop: not alive, mode DD, a corpse
        const bool champion = (flags & 4) != 0, unique = (flags & 8) != 0;
        const bool took = old_rule ? dead && (base == 19 || base == 58) && !unique && d2d::rules::ai_distance(dx, dy) < 80
                                   : dead && d2d::rules::shaman_corpse_reach(base, champion, unique, dx, dy, aip4);
        std::printf("%d\n", took ? 1 : 0);
    }
    return 0;
}
