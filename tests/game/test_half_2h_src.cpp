// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A missile row's Half2HSrc (FUN_0064b860 0x64bafa..0x64bb1d): SrcDamage halved
// toward zero when the owner's weapon is a two-hander (FUN_006289c0). The cases
// are tools/emu/half_2h_src.py's answers from game.exe, one per weapons.txt code;
// with the MPQs (D2_MPQ_DIR) each weapon goes through make_fighter and
// missile_physical, whose weapon share must be the halved SrcDamage's.

#include <combat.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>

namespace fs = std::filesystem;

namespace {

struct Case { const char* code; int flag, srcdam, after; };
constexpr Case kCases[] = {
#include "half_2h_src_cases.inc"
};

}  // namespace

// Each oracle case through make_fighter and missile_physical (FUN_0064b860 0x64bafa..0x64bb1d).
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = d2d::game::load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    const auto& tables = data->rules;
    d2d::rules::StatSum sum{}, weapon_sum{};
    d2d::d2s::Stats stats;
    int bad = 0;
    for (const auto& one : kCases) {
        d2d::d2s::Item weapon;
        weapon.code = one.code;
        weapon.identified = true;
        auto fighter = d2d::rules::make_fighter(tables, &weapon, nullptr, sum, weapon_sum, stats, {}, 0, {});
        fighter.phys_lo = fighter.phys_hi = 128 << 8;                   // the share is then the SrcDamage in 256ths
        d2d::rules::Swing swing;
        swing.missile = true;
        swing.srcdam = one.srcdam;
        swing.half_2h_src = one.flag != 0;
        const auto physical = d2d::rules::missile_physical(fighter, {}, swing);
        const std::int64_t want = std::int64_t(one.after) << 8;
        if (physical.low != want && ++bad <= 5)
            std::printf("DIFF %s flag %d srcdam %d: game %d, d2d %lld\n", one.code, one.flag, one.srcdam, one.after, (long long)(physical.low >> 8));
    }
    std::printf("%s: %zu FUN_0064b860 Half2HSrc cases, %d differ\n", bad ? "FAIL" : "OK", std::size(kCases), bad);
    return bad ? 1 : 0;
} catch (const std::exception& error) {
    std::printf("test_half_2h_src: %s\n", error.what());
    return 1;
}
