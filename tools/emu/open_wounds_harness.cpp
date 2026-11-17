// SPDX-License-Identifier: GPL-3.0-or-later
// d2d's Open Wounds on tools/emu/open_wounds.py's cases: the life a hit that rolled it takes from a struck monster over the
// whole wound, in 256ths. Prints `id applied life` (a player struck: `id -`, d2d has no monster that wounds a player).
// The rule's own `open_wounds_fine(level, special)`, a frame, for `kOpenWoundsFrames`. (The parent took
// `open_wounds_per_sec(level)` whole points a second for 8 seconds, whatever the monster.)
//
//   g++ -O2 -std=c++23 -I<d2d>/components/rules -I<d2d>/components -I<d2d>/components/d2s -I<d2d>/components/txt \
//       -o open_wounds_harness open_wounds_harness.cpp
//   ./open_wounds_harness < cases.tsv > d2d.tsv
#include "combat.hpp"
#include <cstdio>

int main() {
    char line[512];
    while (std::fgets(line, sizeof line, stdin)) {
        long id, level, kind, event, word;
        if (line[0] == '#' || std::sscanf(line, "%ld %ld %ld %ld %ld", &id, &level, &kind, &event, &word) != 5) continue;
        if (kind == 0) { std::printf("%ld -\n", id); continue; }
        const bool special = (word & 12) != 0;     // d2d's Boss kinds champion, unique, superunique set Target::special; this word is theirs
        const long long life = static_cast<long long>(d2d::rules::open_wounds_fine(int(level), special)) * d2d::rules::kOpenWoundsFrames;
        std::printf("%ld 1 %lld\n", id, life);
    }
}
