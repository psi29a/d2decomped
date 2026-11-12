// SPDX-License-Identifier: GPL-3.0-or-later
// Which skills FUN_004aa3a0 puts on the picker.
// The left list is the first stack word 1 (EDX 0x50 at 0x4aa820); the right
// list is 0 (EDX screen width - 0x80 at 0x4aa847). A SkillDesc index
// (+0x194) below 0 is skipped. Rows 0..3 take a non-item node whose ListRow
// (SkillDesc +5, sign-extended at 0x4aa513) equals the row, so ListRow -1
// is absent. Row 4 takes an item node. On the right list a base node whose
// count (FUN_006450f0, +0x30) is above 0 and whose record +8 has the mask
// at 0x6ce278 (0x10: scroll, bit 0x24) becomes an item node. In 1.14d those
// rows are ListRow 4: the identify and town-portal scrolls and tomes
// (books.txt +8 / +0xc: isc/ibk, tsc/tbk). usable() already keeps a scroll
// off the left list (leftskill is clear). The count is their quantity
// (stat 0x46, FUN_0055c110) while carried. A scroll whose quantity stat
// reads 0 adds nothing. A book (Book of Identify, Book of Townportal)
// whose quantity stat reads 0 adds 1.
// ponytail: FUN_006446a0 (+0xc) bit 8 skips the node. FUN_004c6140 sets it
// when the client grants a skill the local player didn't have. d2d keeps
// no skill-node flags, so that grant still lists.
#pragma once

#include <d2s_items.hpp>
#include <skills.hpp>

#include <vector>

namespace d2d::client {

[[nodiscard]] inline int scroll_count(const d2d::rules::Skill& skill, const std::vector<d2d::d2s::Item>& items) {
    const bool book = skill.name == "Book of Townportal" || skill.name == "Book of Identify";
    const char* code = skill.name == "Scroll of Townportal" ? "tsc" : skill.name == "Book of Townportal" ? "tbk"
        : skill.name == "Scroll of Identify" ? "isc" : skill.name == "Book of Identify" ? "ibk" : nullptr;
    if (!code) return 0;
    int count = 0;
    for (const auto& item : items) {
        if (item.code != code) continue;
        int quantity = item.quantity;
        if (quantity == 0 && book) quantity = 1;
        if (quantity <= 0) continue;
        const int where = item.location;
        if (where == d2d::d2s::item_location::kStored || where == d2d::d2s::item_location::kBelt
            || where == d2d::d2s::item_location::kCursor || where == d2d::d2s::item_location::kEquipped)
            count += quantity;
    }
    return count;
}

// True when FUN_004aa3a0 will draw the skill. The left list's leftskill
// test and the level live in SkillBar::usable.
[[nodiscard]] inline bool picker_lists(const d2d::rules::Skill& skill, const std::vector<d2d::d2s::Item>& items) {
    if (skill.desc.empty() || skill.list_row < 0) return false;
    if (skill.list_row == 4) return scroll_count(skill, items) > 0;
    return true;
}

}  // namespace d2d::client
