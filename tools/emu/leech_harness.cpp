// SPDX-License-Identifier: GPL-3.0-or-later
// d2d's life / mana leech of a player's melee blow on tools/emu/leech.py's cases: the REAL d2d::rules::player_blow
// (no copy of its lines). The weapon's physical is the case's `phys` (256ths, a fixed roll), the target has no
// resistance, defense 0, no block; the case's seed is advanced until the blow hits. Prints `id life mana physical`
// in 256ths: what Fight::land credits, against a target holding `life` (256ths).
//
//   g++ -O2 -std=c++23 -I<d2d>/components/rules -I<d2d>/components -I<d2d>/components/d2s -I<d2d>/components/txt \
//       -o leech_harness leech_harness.cpp
//   ./leech_harness < cases.tsv > d2d.tsv
#include "combat.hpp"
#include <cstdint>
#include <cstdio>

int main() {
    char line[512];
    while (std::fgets(line, sizeof line, stdin)) {
        long id, diff, sl, sm, drain, phys, life;
        if (line[0] == '#' || std::sscanf(line, "%ld %ld %ld %ld %ld %ld %ld", &id, &diff, &sl, &sm, &drain, &phys, &life) != 7) continue;
        d2d::rules::Fighter fighter;
        fighter.phys_lo = fighter.phys_hi = int(phys);
        fighter.ar_base = 1 << 20;
        fighter.life_steal = int(sl);
        fighter.mana_steal = int(sm);
        d2d::rules::Target target;
        target.hit_points = int((life + 255) >> 8);
        target.max_hp = int((life + 255) >> 8);
        target.drain = int(drain);
        // The difficulty's divisors and the life in 256ths exist from the claim on; the parent has neither.
        [&](auto& tgt) {
            if constexpr (requires { tgt.life_fine; }) {
                static constexpr int kDivisor[3] = { 1, 2, 3 };
                tgt.life_fine = int(life);
                tgt.life_steal_divisor = tgt.mana_steal_divisor = kDivisor[diff];
            }
        }(target);
        d2d::rules::Blow blow;
        for (std::uint32_t seed = std::uint32_t(id) * 7919u + 1; !blow.hit; ++seed) {
            d2d::rules::Rng rng{ seed };
            blow = d2d::rules::player_blow(fighter, target, 30, rng);
        }
        // Fight::land credits `blow.life << 8` on the parent and `blow.life` (256ths) from the claim on.
        const int scale = [](auto& tgt) { if constexpr (requires { tgt.life_fine; }) return 1; else return 256; }(target);
        std::printf("%ld %d %d %d\n", id, blow.life * scale, blow.mana * scale, blow.phys_fine);
    }
}
