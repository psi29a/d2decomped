// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of quests_act4.hpp's functions; their notes are in the header.
#include "quests_act4.hpp"

#include "quests.hpp"

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
}
