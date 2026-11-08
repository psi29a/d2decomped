// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Cyclone Armor (FUN_005c87a0, absorb event 0xb, raised by FUN_0057c1e0 at 0x57c2fc before its reduction loop) soaks the
// unresisted fire, cold, then lightning fields in 256ths (FUN_005c8780, each only above 0); FUN_0057bf80 then takes the flat
// MDR off each field and the resistance off what is left. cyclone_armor_cases.inc is game.exe's own answer on 3000 records
// (tools/emu/cyclone_armor.py; normal difficulty, max resistance 75).

#include <combat.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <iterator>
#include <span>

namespace {

struct Case { int pool, mdr, fire_res, cold_res, light_res, fire, cold, light, magic, fire_after, cold_after, light_after, magic_after, total, pool_after, on; };
constexpr Case kCases[] = {
#include "cyclone_armor_cases.inc"
};

}   // namespace

int main() {
    namespace rules = d2d::rules;
    int bad = 0;
    for (const auto& one : kCases) {
        int pool = one.pool;
        std::array fields{ one.fire, one.cold, one.light };
        const std::array res{ one.fire_res, one.cold_res, one.light_res };
        for (auto& field : fields) field -= rules::cyclone_take(pool, field);
        for (std::size_t index = 0; index < fields.size(); ++index) fields[index] = rules::elemental_fine(fields[index], one.mdr * 256, std::min(res[index], 75), true);
        const int magic = rules::elemental_fine(one.magic, one.mdr * 256, 0, false);
        const int total = fields[0] + fields[1] + fields[2] + magic;
        if (fields != std::array{ one.fire_after, one.cold_after, one.light_after } || magic != one.magic_after || total != one.total
            || pool != one.pool_after || (pool != 0) != (one.on != 0)) {
            if (++bad <= 5) std::printf("FAIL: pool %d mdr %d fields %d %d %d %d: d2d %d %d %d %d %d pool %d, game.exe %d %d %d %d %d pool %d\n", one.pool, one.mdr,
                                        one.fire, one.cold, one.light, one.magic, fields[0], fields[1], fields[2], magic, total, pool, one.fire_after,
                                        one.cold_after, one.light_after, one.magic_after, one.total, one.pool_after);
        }
    }
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }

    // absorb_blows on a blow monster_blow built: fire 10 and cold 5 points unresisted, fire at 50 %, 16 points of pool.
    rules::StruckBlow blow{ .damage = 7, .marked = true, .elemental = 7 };
    blow.parts[0] = { .raw = 10, .dealt = 5, .res = 50, .fine = 5 * 256 };
    blow.parts[2] = { .raw = 5, .dealt = 2, .res = 50, .fine = 2 * 256 + 128 };
    blow.sum = 7 * 256 + 128;
    int pool = 16 * 256;
    const int taken = rules::absorb_blows(std::span(&blow, 1), pool, true);
    assert(taken == 15 * 256 && pool == 256 && blow.elemental == 0 && !blow.marked && blow.damage == 0 && blow.frac == 0);
    std::printf("OK: %zu records as FUN_0057c1e0 / FUN_005c87a0 leave them\n", std::size(kCases));
    return 0;
}
