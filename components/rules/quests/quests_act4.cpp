// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of quests_act4.hpp's functions, with the research behind each.
#include "quests_act4.hpp"

#include "quest_clock.hpp"
#include "quests.hpp"

#include <cstddef>
#include <vector>

namespace d2d::rules {
namespace act4_detail {

bool told(const QuestBits& quest_bits, int quest) {
    return !(quest::bit(quest_bits, quest, 0) || quest::bit(quest_bits, quest, 15)) || quest::bit(quest_bits, quest, 13) || quest::bit(quest_bits, quest, 14);
}

std::vector<QuestMsg> block_for(const std::vector<Entry>& block, int npc) {
    std::vector<QuestMsg> out;
    for (const auto& entry : block) if (entry.npc == npc) out.push_back({ entry.string, entry.greet });
    return out;
}

}


// The player joins (LAB_005b4220).
auto IzualQuest::join(const QuestBits& quest_bits) -> void {
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) return;
    if (quest::bit(quest_bits, kQuest, 4)) { ghost_told = true; log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

// The fortress entered (LAB_005b4140): state 2 → 3.
auto IzualQuest::enter(QuestBits& quest_bits, int into) -> void {
    if (into != act4_detail::kFortress || state != 2 || quest::bit(quest_bits, kQuest, 0)) return;
    state = 3;
    if (log == 0) log = 1;
    mark(quest_bits);
}

// Izual died (FUN_005b4020): state 4; the player's bits 13 and 1 (reward
// due) unless done or due; his ghost due 3 ticks on (FUN_005b3e60).
auto IzualQuest::killed(QuestBits& quest_bits, float x, float y) -> void {
    state = 4;
    if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) {
        quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
        act4_detail::clear_steps(quest_bits, kQuest);
    }
    ghost_due = true; spot_x = x; spot_y = y;
}

auto IzualQuest::said(QuestBits& quest_bits, int npc, int string, int player) -> Heard {
    Heard out;
    if (npc == kTyrael && string == kTold) { state = 2; heard = true; mark(quest_bits); }
    if (npc == kTyrael && string == kReward && quest::bit(quest_bits, kQuest, 1)) {
        if (quest::bit(quest_bits, kQuest, 13) && state != 5) { state = 5; log = 13; out.chain = true; }
        quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false);
        act4_detail::clear_steps(quest_bits, kQuest);
        out.skill_points = kSkillPoints;
        rewarded.add(player);
    }
    if (npc == kGhost && string == kGhostSpeech) {
        quest::set(quest_bits, kQuest, 5);
        if (log != 4) { log = 4; news = true; }
    }
    return out;
}

auto IzualQuest::talk(const QuestBits& quest_bits, int npc, int player) const -> std::vector<QuestMsg> {
    if (npc == kGhost && !quest::bit(quest_bits, kQuest, 5)) return act4_detail::block_for(kBlocks[3], npc);
    int block = -1;
    if (quest::bit(quest_bits, kQuest, 1)) block = 3;
    else if (rewarded.has(player)) block = 4;
    else if (quest::bit(quest_bits, kQuest, 0) || (state > 3 && !quest::bit(quest_bits, kQuest, 13)) || !open) block = -1;
    else if (state >= 1 && state <= 5) block = state - 1;
    return block >= 0 ? act4_detail::block_for(kBlocks[block], npc) : std::vector<QuestMsg>{};
}

// The "!" (FUN_005b37f0).
auto IzualQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    if (!open || quest::bit(quest_bits, kQuest, 0)) return false;
    if (npc == kTyrael) return quest::bit(quest_bits, kQuest, 1) || state == 1;
    return npc == kGhost && !quest::bit(quest_bits, kQuest, 5);
}

// A later player's +0xd8 (LAB_005b64c0) counts the hammers about (data
// +0x10), which d2d doesn't keep (the forge asks the hand).
// The player joins (FUN_005b64e0).
auto HellforgeQuest::join(const QuestBits& quest_bits, bool stone) -> void {
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) return;
    if (!stone) { state = 1; return; }
    if (quest::bit(quest_bits, kQuest, 5)) { stone_given = true; log = 4; state = 2; }
    else if (quest::bit(quest_bits, kQuest, 3)) { log = 1; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 2)) { log = 1; state = 2; }
}

// The Hellforge Hammer into an inventory (LAB_005b5a90, event 4 on an
// item bound to this record: hfh's Items.txt quest 25; the soulstone's
// 23 is a4q1's): 0 → 1 unless done, in this game (13), due, or the log
// at 13. tools/emu/quest_item_events.py.
auto HellforgeQuest::hammer_picked(const QuestBits& quest_bits) -> void {
    if (!quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 13) && !quest::bit(quest_bits, kQuest, 1) && open && log != 13 && state == 0) state = 1;
}

// The fortress entered (FUN_005b6080).
auto HellforgeQuest::enter(const QuestBits& quest_bits, int into) -> void {
    if (into == act4_detail::kFortress && state == 2 && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1)) state = 3;
}

// Heard (FUN_005b6100).
auto HellforgeQuest::said(QuestBits& quest_bits, int npc, int string, int player) -> Heard {
    Heard out;
    if (npc != kCain) return out;
    if (string == kTold) { state = 2; heard = true; }
    if (string == kGivesStone) { out.gives_stone = true; stone_given = heard = true; if (state < 2) state = 2; }
    if (string == kReward && quest::bit(quest_bits, kQuest, 1)) {
        if (quest::bit(quest_bits, kQuest, 13) && state != 5) { state = 5; log = 13; out.chain = true; }
        quest::set(quest_bits, kQuest, 0); quest::set(quest_bits, kQuest, 1, false);
        rewarded.add(player);
    }
    return out;
}

auto HellforgeQuest::operate(QuestBits& quest_bits, bool stone, bool hammer_in_hand) -> Struck {
    Struck out;
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return out;
    if (forge_mode == 0 && stone) { forge_mode = 2; log = 3; news = true; out.takes_stone = true; return out; }
    if (forge_mode != 2 || !hammer_in_hand || ++blows < kBlows) return out;
    forge_mode = 3; state = 4; log = 13; news = true;
    quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 1);
    act4_detail::clear_steps(quest_bits, kQuest);
    out.takes_hammer = out.done = true;
    return out;
}

// Blocks (0x73e140; FUN_005b62d0).
auto HellforgeQuest::talk(const QuestBits& quest_bits, int npc, bool stone, int player) const -> std::vector<QuestMsg> {
    if (npc != kCain) return {};
    if (quest::bit(quest_bits, kQuest, 1)) return { { kReward, true } };
    if (rewarded.has(player)) return { { kReward, false } };
    if (state == 0 || quest::bit(quest_bits, kQuest, 0) || (state > 3 && !quest::bit(quest_bits, kQuest, 13)) || !open) return {};
    if (state == 1) {
        if (stone) return { { kTold, true } };
        if (stone_given || log > 2) return {};
        return { { kGivesStone, true } };
    }
    if (stone || log > 2 || stone_given) return {};
    return { { kGivesStone, true } };
}

// The "!" (FUN_005b5ee0).
auto HellforgeQuest::alert(const QuestBits& quest_bits, int npc) const -> bool {
    return npc == kCain && !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 1) && open && state == 1 && !stone_given;
}

// The player joins (FUN_005b5080).
auto TerrorQuest::join(QuestBits& quest_bits, bool expansion) -> void {
    join_more(quest_bits, expansion);
    open = !quest::bit(quest_bits, kQuest, 0) && !quest::bit(quest_bits, kQuest, 15);
    if (!open) return;
    if (quest::bit(quest_bits, kQuest, 4)) { log = 2; state = 3; }
    else if (quest::bit(quest_bits, kQuest, 3)) { state = 3; log = 1; }
    else if (quest::bit(quest_bits, kQuest, 2)) { state = 2; log = 1; }
}

auto TerrorQuest::enter(QuestBits& quest_bits, int into) -> void {           // LAB_005b4ea0
    if (into == act4_detail::kFortress && state == 2 && !quest::bit(quest_bits, kQuest, 0)) {
        state = 3;
        if (log < 1) log = 1;
        mark(quest_bits);
    }
    if (into == act4_detail::kChaosSanctuary && open) {
        if (state < 3) state = 3;
        if (log < 1) { log = 1; news = true; }
        mark(quest_bits);
    }
}

// A seal opened (FUN_005b5630; 392..396): whether all five are open
// and the three bosses dead (the release, FUN_005b5230, once).
auto TerrorQuest::seal(int object_id) -> bool {
    if (object_id >= 392 && object_id <= 396) seals[std::size_t(object_id - 392)] = true;
    return ready();
}

// A quest-clock step. True: the timer runs (every kReleaseTicks + 1
// steps) with its count past 9, so Diablo comes now if he can; if he
// can't, it runs again (returns 0) and tries on its next run.
auto TerrorQuest::release_step() -> bool {
    if (!armed || ++release_steps < quest_timer_steps(kReleaseTicks)) return false;
    release_steps = 0;
    return ++release_runs >= kReleaseRuns;
}

// Diablo died (FUN_005b52e0 class 0xf3 -> FUN_005b4d20 for the player):
// whether the classic ending (bits 6 / 7 and the next difficulty) applies.
auto TerrorQuest::diablo_died(QuestBits& quest_bits, bool expansion, int player) -> void {
    dead = true;
    if (open) { log = 13; news = true; }
    if (quest::bit(quest_bits, kQuest, 0) || quest::bit(quest_bits, kQuest, 1)) return;
    quest::set(quest_bits, kQuest, 13); quest::set(quest_bits, kQuest, 0);
    act4_detail::clear_steps(quest_bits, kQuest);
    if (!expansion) { quest::set(quest_bits, kQuest, 6); quest::set(quest_bits, kQuest, 7); }
    rewarded.add(player);
}

auto TerrorQuest::said(QuestBits& quest_bits, int npc, int string, bool expansion) -> Heard {   // FUN_005b4670
    Heard out;
    if (npc == kTyrael && string == kTold) { state = 2; heard = true; mark(quest_bits); }
    if (npc == kTyrael && string == kTyraelLast) quest::set(quest_bits, kQuest, 7, false);
    if (npc == kTyrael && string == kTyraelPortal && expansion) {
        quest::set(quest_bits, kQuest, 9);
        if (!portal_made) { portal_made = true; out.portal = true; }
    }
    if (npc == kCain && string == kCainLast) quest::set(quest_bits, kQuest, 6, false);
    if (npc == kCain && string == kCainPortal) quest::set(quest_bits, kQuest, 8);
    return out;
}

auto TerrorQuest::talk(const QuestBits& quest_bits, int npc, bool expansion) const -> std::vector<QuestMsg> {
    using act4_detail::block_for;
    const bool tyrael_last = quest::bit(quest_bits, kQuest, 7), cain_last = quest::bit(quest_bits, kQuest, 6);
    if (!expansion) {
        if (tyrael_last && npc == kTyrael) return block_for(kBlocks[2], npc);
        if (cain_last && npc == kCain) return block_for(kBlocks[2], npc);
        if (tyrael_last) return npc == kCain ? block_for(kBlocks[3], npc) : std::vector<QuestMsg>{};
        if (cain_last) return npc == kTyrael ? block_for(kBlocks[3], npc) : std::vector<QuestMsg>{};
    }
    if (!quest::bit(quest_bits, kQuest, 0)) {
        if ((state > 3 && !quest::bit(quest_bits, kQuest, 13)) || !open || state == 0) return {};
        return state <= 2 ? block_for(kBlocks[state - 1], npc) : std::vector<QuestMsg>{};
    }
    std::vector<QuestMsg> out;
    if (!expansion) return out;
    if (!quest::bit(quest_bits, kQuest, 9)) {
        if (npc == kTyrael) out = block_for(kBlocks[4], npc);
        else if (npc == kCain) out = block_for(kBlocks[5], npc);
    }
    if (!quest::bit(quest_bits, kQuest, 8)) {
        auto more = npc == kCain ? block_for(kBlocks[4], npc) : npc == kTyrael ? block_for(kBlocks[5], npc) : std::vector<QuestMsg>{};
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

// The "!" (FUN_005b4450).
auto TerrorQuest::alert(const QuestBits& quest_bits, int npc, bool expansion) const -> bool {
    const bool done = quest::bit(quest_bits, kQuest, 0);
    if (npc == kTyrael) {
        if (!done && !quest::bit(quest_bits, kQuest, 15) && state == 1) return true;
        if (!expansion) return done && quest::bit(quest_bits, kQuest, 7);
        return done && !quest::bit(quest_bits, kQuest, 9);
    }
    if (npc == kCain) {
        if (!expansion) return done && quest::bit(quest_bits, kQuest, 6);
        return done && !quest::bit(quest_bits, kQuest, 8);
    }
    return false;
}

}
