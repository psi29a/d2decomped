// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of quests.hpp's functions; their notes are in the header.
#include "quests.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2d::rules {
namespace quest {

bool bit(const QuestBits& quest_bits, int quest, int bit) {
    const int bit_index = quest * 16 + bit;
    return quest_bits[std::size_t(bit_index >> 3)] >> (bit_index & 7) & 1;
}

void set(QuestBits& quest_bits, int quest, int bit, bool set) {
    const int bit_index = quest * 16 + bit;
    const auto mask = std::uint8_t(1u << (bit_index & 7));
    auto& byte = quest_bits[std::size_t(bit_index >> 3)];
    byte = std::uint8_t(set ? byte | mask : byte & ~mask);
}

}
}

namespace d2d::rules {

int quest_log(const QuestBits& quest_bits, int quest, int state, int log, int done) {
    const bool b13 = quest::bit(quest_bits, quest, 13), b14 = quest::bit(quest_bits, quest, 14);
    if (state < done) return b14 || (quest == 4 && log == 6 && !b13) ? 12 : log;
    if (b13) return log;
    if (quest == 4 && b14) return state == 6 ? 12 : log;
    return quest == 10 ? (log != 4 ? 12 : 4) : 12;
}

PalaceSpots palace_spots(const QuestBits& game_bits, bool tombs_open, int tombs_state) {
    const bool tombs_early = tombs_open && tombs_state < 2;
    PalaceSpots spots;
    spots.outside = !quest::bit(game_bits, 8, 13) && !quest::bit(game_bits, 12, 13) && tombs_early;
    spots.guard = !quest::bit(game_bits, 14, 13);
    spots.palace = !(!quest::bit(game_bits, 8, 13) && !quest::bit(game_bits, 9, 13) && tombs_early);
    spots.palace_west = tombs_early;
    return spots;
}

// The first join runs it from quest 1 (FUN_00546270).
void chain(int quest, DenQuest& den, BurialQuest& burial, CainQuest& cain, TowerQuest& tower, ToolsQuest& tools, AndyQuest& andy) {
    for (;;) {
        switch (quest) {
        case 1: if (!den.chain()) return; quest = 2; break;
        case 2: if (!burial.chain()) return; quest = 4; break;
        case 4: if (!cain.open()) return; quest = 3; break;
        case 5: if (!tower.chain()) return; quest = 3; break;
        case 3: if (!tools.open()) return; quest = 6; break;
        default: andy.chain(); return;
        }
    }
}

QuestText quest_text(const d2d::rules::QuestBits& quest_bits, int quest, const QuestState& quest_state) {
    if (quest < 1 || quest > 22 || quest == 7 || quest == 8 || quest == 15 || quest == 16) return {};
    const auto& rec = kQuestLogRecords[std::size_t(quest - (quest < 7 ? 1 : quest < 15 ? 3 : 5))];
    auto flag = [&](int bit) { return d2d::rules::quest::bit(quest_bits, quest, bit); };
    auto game = [&](int bit) { return (quest_state.game[std::size_t(quest)] >> bit & 1) != 0; };
    auto entry = [&](int string, int speech, int shown, int state, bool marks) {
        return QuestText{ string == 3725 ? 0 : string, -1, speech == 3725 ? 0 : speech, shown, state, marks };
    };
    const bool bit0 = flag(0), bit1 = flag(1), b13 = flag(13);
    int log_state = quest_state.log[std::size_t(quest)];
    if (bit0) {                                               // done: 13 in this game, 11 before
        log_state = b13 ? 13 : 11;
        return entry(rec[std::size_t(2 * log_state + 1)], rec[1], flag(12) ? 1 : 0, log_state, false);
    }
    if (quest == 14 && !b13) {                                // the Seven Tombs' own case (record 0x723a64)
        const bool summoner = d2d::rules::quest::bit(quest_bits, 12, 0) || d2d::rules::quest::bit(quest_bits, 12, 13);
        const int tomb = summoner && log_state == 1 ? 7 : flag(3) ? 5 : flag(4) ? 6 : 0;
        if (tomb) return entry(rec[std::size_t(2 * tomb + 1)], rec[std::size_t(2 * tomb + 2)], 3, tomb, false);
    }
    if (b13 && bit1 && rec[2] != 0xffff) {                    // the reward's due
        log_state = rec[2] + 1;
        return entry(rec[std::size_t(2 * log_state + 1)], rec[std::size_t(2 * log_state + 2)], 3, log_state, true);
    }
    if (bit1 && flag(15)) return rec[21] == 3725 ? QuestText{} : entry(rec[21], rec[22], 3, 10, false);
    if (log_state == 0)
        return (game(13) || game(15)) && !b13 && !bit1 ? entry(3729 + quest_state.multiplayer, 3725, 3, -1, false) : QuestText{};
    if (log_state > 13) return {};
    if (quest == 1) {                                       // the Den (name 3714): its count
        QuestText text = entry(rec[std::size_t(2 * log_state + 1)], rec[std::size_t(2 * log_state + 2)], log_state == 13 ? (flag(12) ? 1 : 0) : 3, log_state, true);
        if (log_state == 3 || log_state == 4) { if (quest_state.den_left >= 2) text.count = quest_state.den_left; else text.string = 3739; }
        return text;
    }
    int line = rec[std::size_t(2 * log_state + 1)];
    if ((!b13 && !bit1 && game(13)) || flag(14)) line = 3729 + quest_state.multiplayer;   // "done by another player"
    else if (line == 3727 && !quest_state.multiplayer) line = 3729;
    return entry(line, rec[std::size_t(2 * log_state + 2)], log_state == 13 ? (flag(12) ? 1 : 0) : 3, log_state, true);
}

bool quest_act_open(const QuestBits& quest_bits, int act, bool expansion) {
    static constexpr std::array<int, 5> kActDone = { 0, 7, 15, 23, 26 };
    if (act <= 0) return act == 0;
    return act < 5 && (act < 4 || expansion) && quest::bit(quest_bits, kActDone[std::size_t(act)], 0);
}

int quest_log_act(const QuestBits& quest_bits, int act) {
    act = act > 4 ? 0 : act;
    while (act > 0 && !quest_act_open(quest_bits, act, true)) --act;
    return act;
}

int quest_log_pick(const std::vector<QuestLogPick>& entries, int pending, int remembered) {
    if (pending >= 0) return pending;
    int listed = 0, under_way = -1;
    for (const auto& entry : entries) {
        if (entry.fresh || entry.shown == 0) return entry.slot;
        if (entry.shown == 3 && under_way < 0) under_way = entry.slot;
        ++listed;
    }
    if (!listed) return -1;
    return remembered >= 0 ? remembered : under_way;
}

// A message below row 100's (164) is looked for in rows 0..99, any other in
// rows 100 on, so 164 is found at row 100 (bugs.md 46: the old reading,
// 0xe8c, was wrong; tools/emu/quest_name.py).
int quest_name(int message) {
    struct Run { int first, last, name; };
    static constexpr std::array<Run, 32> kRuns{ {
        { 64, 80, 3714 }, { 81, 96, 3715 }, { 97, 126, 3717 }, { 127, 145, 3718 }, { 146, 165, 3716 }, { 166, 184, 3719 },
        { 304, 334, 923 }, { 335, 339, 924 }, { 340, 372, 925 }, { 373, 395, 926 }, { 397, 407, 926 }, { 408, 429, 927 }, { 430, 452, 928 },
        { 527, 542, 933 }, { 543, 548, 931 }, { 549, 570, 930 }, { 571, 593, 932 }, { 594, 627, 934 }, { 628, 635, 935 }, { 637, 663, 935 },
        { 670, 677, 937 }, { 678, 680, 939 }, { 681, 685, 938 }, { 20000, 20001, 938 }, { 20002, 20002, 22641 }, { 20077, 20095, 22618 },
        { 20096, 20115, 22622 }, { 20116, 20136, 22627 }, { 20137, 20137, 22633 }, { 20139, 20152, 22633 }, { 20153, 20168, 22637 }, { 20169, 20180, 22641 } } };
    const auto run = std::ranges::find_if(kRuns, [&](const Run& entry) { return message >= entry.first && message <= entry.last; });
    return run != kRuns.end() ? run->name : 0xe8c;
}

const FindLine* find_line(int level) {
    for (const auto& row : kFindLines)
        if (std::ranges::find(row.levels, level) != row.levels.end() && level != 0) return &row;
    return nullptr;
}

}
