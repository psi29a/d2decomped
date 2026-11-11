// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A monster's MonEquip gear walk (FUN_005d6b60): which rows, which item of
// each, the unit seed after. mon_equip_cases.inc is game.exe's own walk on
// 2000 calls over synthetic tables (tools/emu/mon_equip.py).

#include <mon_equip.hpp>
#include <rules.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Row { int table, monster, level, oninit; const char* item[3]; int loc[3], quality[3]; };
constexpr Row kRows[] = {
#define MON_EQUIP_ROWS
#include "mon_equip_cases.inc"
#undef MON_EQUIP_ROWS
};
struct Firsts { int table; int first[3]; };
constexpr Firsts kFirsts[] = {
#define MON_EQUIP_FIRSTS
#include "mon_equip_cases.inc"
#undef MON_EQUIP_FIRSTS
};
struct Case { int table, monster, level, oninit, owner, filled; std::uint32_t low, high, low_after, high_after; const char* calls; };
constexpr Case kCases[] = {
#include "mon_equip_cases.inc"
};

std::string trimmed(const std::string& code) {
    const auto end = code.find_last_not_of(' ');
    return end == std::string::npos ? "____" : code.substr(0, end + 1);
}

int walk_cases() {
    int bad = 0;
    for (const auto& one : kCases) {
        std::vector<d2d::rules::MonEquipRow> rows;
        for (const auto& row : kRows)
            if (row.table == one.table)
                rows.push_back({ row.monster, row.level, row.oninit != 0, { row.item[0], row.item[1], row.item[2] }, { row.loc[0], row.loc[1], row.loc[2] },
                                 { row.quality[0], row.quality[1], row.quality[2] } });
        int first = -1;
        for (const auto& firsts : kFirsts)
            if (firsts.table == one.table) first = firsts.first[one.monster];
        d2d::rules::Rng own;
        own.low = one.low; own.high = one.high;
        const auto owner = one.owner >= 0 ? std::optional<std::uint32_t>(std::uint32_t(one.owner)) : std::nullopt;
        const auto gear = d2d::rules::monster_gear(rows, first, one.monster, one.level, one.oninit != 0, std::uint32_t(one.filled), owner, own);
        std::string calls;
        for (const auto& made : gear) {
            if (!calls.empty()) calls += ",";
            calls += (made.owners ? std::string("own") : trimmed(rows[made.row].item[made.slot])) + "/" + std::to_string(made.loc) + "/" + std::to_string(made.quality);
        }
        if (calls.empty()) calls = "-";
        if ((calls != one.calls || own.low != one.low_after || own.high != one.high_after) && ++bad <= 5)
            std::printf("FAIL: table %d monster %d level %d oninit %d owner %d filled %d: d2d %s %u %u, game.exe %s %u %u\n", one.table, one.monster, one.level,
                        one.oninit, one.owner, one.filled, calls.c_str(), own.low, own.high, one.calls, one.low_after, one.high_after);
    }
    if (bad) std::printf("%d of %zu walks differ\n", bad, std::size(kCases));
    return bad;
}

}   // namespace

int main() try {
    const int bad = walk_cases();
    if (bad) return 1;
    std::printf("OK: %zu MonEquip walks as FUN_005d6b60 makes them\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
