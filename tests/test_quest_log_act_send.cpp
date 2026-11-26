// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_00544190 sends a quest-log update only when the player's level act
// equals the quest record's +8 act byte.
#include <quest_log_send.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::rules;

// The oracle's cases, then every record's act against every player act.
int main() try {
    struct OracleCase { int record_act, player_act; bool sent; };
    static constexpr OracleCase oracle_cases[]{
#include "quest_log_act_cases.inc"
    };
    for (const auto& sample : oracle_cases)
        assert(quest_log_act_matches(sample.record_act, sample.player_act) == sample.sent);

    constexpr std::array ranges{
        std::array{ 1, 6, 0 }, std::array{ 9, 14, 1 }, std::array{ 17, 22, 2 },
        std::array{ 25, 27, 3 }, std::array{ 34, 34, 0 }, std::array{ 35, 40, 4 },
    };
    for (const auto& range : ranges)
        for (int quest = range[0]; quest <= range[1]; ++quest)
            for (int act = 0; act < 5; ++act)
                assert(quest_log_send_in_act(quest, act) == (act == range[2]));

    for (const int gap : { 0, 7, 8, 15, 16, 23, 24, 28, 29, 30, 31, 32, 33, 41 }) {   // 0x723f30's rows with record 37
        assert(quest_log_record_act(gap) == -1);
        for (int act = 0; act < 5; ++act) assert(!quest_log_send_in_act(gap, act));
    }
    std::puts("OK: FUN_00544190's quest-record act gate is ported");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_log_act_send: %s\n", error.what());
    return 1;
}
