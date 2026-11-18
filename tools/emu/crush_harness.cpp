// SPDX-License-Identifier: GPL-3.0-or-later
// d2d's Crushing Blow of a player's blow on tools/emu/crush.py's cases: the REAL d2d::rules::player_blow (no copy of its lines).
// A case is `id life phys res div`: the target's life in 256ths (a whole number of points), the weapon's physical in 256ths
// (a fixed roll, before the target's physical resistance `res`), the target's Crushing Blow divisor (4, 8, 10). The weapon
// has Crushing Blow 100 %; the seed is advanced until the blow hits and crushes. Prints `id crush` in 256ths: the blow's
// damage in 256ths less its physical after resistance (what the crush added).
// With --pair a case is `id diff sl sm life phys div` (no resistance, Drain 100, the difficulty's divisors 1 / 2 / 3) and
// the line is `id crush life mana`, 256ths: the crush and the leech of the same blow.
//
//   g++ -O2 -std=c++23 -I<d2d>/components/rules -I<d2d>/components -I<d2d>/components/d2s -I<d2d>/components/txt \
//       -o crush_harness crush_harness.cpp
//   ./crush_harness < cases.tsv > d2d.tsv
#include "combat.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    const bool pair = argc > 1 && std::strcmp(argv[1], "--pair") == 0;
    char line[512];
    while (std::fgets(line, sizeof line, stdin)) {
        long id = 0, life = 0, phys = 0, res = 0, div = 0, diff = 0, sl = 0, sm = 0;
        if (line[0] == '#') continue;
        if (pair ? std::sscanf(line, "%ld %ld %ld %ld %ld %ld %ld", &id, &diff, &sl, &sm, &life, &phys, &div) != 7
                 : std::sscanf(line, "%ld %ld %ld %ld %ld", &id, &life, &phys, &res, &div) != 5) continue;
        d2d::rules::Fighter fighter;
        fighter.phys_lo = fighter.phys_hi = int(phys);
        fighter.ar_base = 1 << 20;
        fighter.crushing = 100;
        fighter.life_steal = int(sl);
        fighter.mana_steal = int(sm);
        d2d::rules::Target target;
        target.hit_points = int((life + 255) >> 8);
        target.max_hp = int((life + 255) >> 8);
        target.res[0] = int(res);
        target.crush_divisor = int(div);
        target.drain = 100;
        static constexpr int kDivisor[3] = { 1, 2, 3 };
        target.life_fine = int(life);
        target.life_steal_divisor = target.mana_steal_divisor = kDivisor[diff];
        d2d::rules::Blow blow;
        for (std::uint32_t seed = std::uint32_t(id) * 7919u + 1; !blow.hit; ++seed) {
            d2d::rules::Rng rng{ seed };
            blow = d2d::rules::player_blow(fighter, target, 30, rng);
        }
        const int crush = blow.damage * 256 + blow.frac - blow.phys_fine;
        if (pair) std::printf("%ld %d %d %d\n", id, crush, blow.life, blow.mana);
        else std::printf("%ld %d\n", id, crush);
    }
}
