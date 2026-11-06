// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00483f80 (0x483f80): the grid tints a quest consumable red while its
// quest holds it back, on the active difficulty's quest words: the Book of
// Skill (ass) without Radament's (9) bit 5, the Potion of Life (xyz)
// without the Golden Bird's (0x14) bit 5, the Scroll of Resistance (tr2)
// unless the Prison of Ice's (0x25) has bit 8 and not bit 7.

#include <d2s.hpp>
#include <item_text.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

int main() {
    d2d::d2s::Header header;
    header.difficulty[1] = 0x80;                     // Nightmare
    const auto set = [&](int quest, int bit, bool on) {
        const int index = quest * 16 + bit;
        auto& byte = header.quests[1][std::size_t(index >> 3)];
        byte = std::uint8_t(on ? byte | (1 << (index & 7)) : byte & ~(1 << (index & 7)));
    };
    using d2d::game::quest_item_held_back;
    assert(quest_item_held_back("ass", header) && quest_item_held_back("xyz", header) && quest_item_held_back("tr2", header));
    assert(!quest_item_held_back("hp1", header));
    set(9, 5, true);
    assert(!quest_item_held_back("ass", header));
    set(0x14, 5, true);
    assert(!quest_item_held_back("xyz", header));
    set(0x25, 8, true);
    assert(!quest_item_held_back("tr2", header));    // bit 8, not bit 7
    set(0x25, 7, true);
    assert(quest_item_held_back("tr2", header));     // both
    set(0x25, 8, false);
    assert(quest_item_held_back("tr2", header));     // bit 7 alone
    header.quests[1] = {};
    header.quests[0][9 * 2] = 0xff; header.quests[0][9 * 2 + 1] = 0xff;
    assert(quest_item_held_back("ass", header));     // Normal's word doesn't count in Nightmare
    std::printf("quest_item_held_back: ok\n");
    return 0;
}
