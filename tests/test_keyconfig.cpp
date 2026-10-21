// SPDX-License-Identifier: GPL-3.0-or-later
// game.exe's key bindings (apps/d2d/keyconfig.hpp): the defaults, the
// assignment rules, the .key files and the key names. `--dump` prints
// every assignment from the defaults and every key's name, as
// tools/emu/keys.py prints them from game.exe's own code, for a diff.
#include "keyconfig.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace d2d::client::keys;

namespace {
int dump() {
    for (int action = 0; action <= kGameMenu; ++action)
        for (int slot = 0; slot < 2; ++slot)
            for (int key = 0; key <= kWheelDown; ++key) {
                auto table = defaults();
                std::uint16_t message = 0;
                const bool assigned = assign(table, action, slot, std::uint16_t(key), message);
                unsigned sum = 0;
                for (const auto byte : write_table(table)) sum = sum * 31 + byte;
                std::printf("%x %d %x %d %x %08x\n", action, slot, key, int(assigned), message, sum);
            }
    for (int key = 0; key <= kWheelDown; ++key) std::printf("name %x %x\n", key, name_id(std::uint16_t(key)));
    std::printf("name ffff %x\n", name_id(kNone));
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--dump") return dump();
    const Table start = defaults();
    // The defaults: whole, Inventory on I then B, the wheel on the skill steps.
    assert(valid(start));
    assert(key_of(start, kInventory, 1) == 'I' && key_of(start, kInventory, 0) == 'B');
    assert(key_of(start, kPreviousSkill, 1) == kWheelUp && key_of(start, kAutomap, 0) == kMouse3);
    assert(actions_of(start, 'C') == std::vector<int>{ kCharacter });
    // A key moves: K to the Character screen's first slot, then to the Party screen's.
    {
        Table table = start;
        std::uint16_t message = 1;
        assert(assign(table, kCharacter, 1, 'K', message) && message == 0 && key_of(table, kCharacter, 1) == 'K');
        assert(assign(table, kParty, 1, 'K', message) && key_of(table, kCharacter, 1) == kNone && key_of(table, kParty, 1) == 'K');
        assert(valid(table));
        clear(table, kParty, 1);
        assert(key_of(table, kParty, 1) == kNone);
    }
    // Refusals: Escape, an arrow, the wheel on a held function, Print Screen
    // but on Screen Shot, the game menu's row; the table stays.
    {
        Table table = start;
        std::uint16_t message = 0;
        assert(!assign(table, kCharacter, 1, 0x1b, message) && message == kCannotKey);
        assert(!assign(table, kCharacter, 1, 0x26, message) && message == kCannotKey);
        assert(!assign(table, kShowItems, 1, kWheelUp, message) && message == kCannotWheel);
        assert(!assign(table, kScreenShot, 0, kWheelDown, message) && message == kCannotWheel);
        assert(!assign(table, kCharacter, 0, kPrintScreen, message) && message == kCannotKey);
        assert(!assign(table, kGameMenu, 1, 'K', message));
        assert(table == start);
    }
    // The files: a character's (0x25 + the table) and default.key ("WS"),
    // a broken one refused (a key on two functions).
    {
        const auto character_file = write_character(start);
        assert(character_file.size() == 0x476 && read_character(character_file) == start);
        const auto shared = write_default(start);
        assert(shared.size() == 6 + 0x474 && read_default(shared) == start);
        auto broken = start;
        broken[1].key = 'A';
        assert(!valid(broken) && !read_character(write_character(broken)));
    }
    // Names: a letter is itself, the rest strings (None, Enter, F1, Mouse 3, Wheel Down).
    assert(name_id('K') == 0 && name_id(kNone) == 0xeb2 && name_id(0x0d) == 0xed1 && name_id(0x70) == 0xef1);
    assert(name_id(kMouse3) == 0xeb6 && name_id(kWheelDown) == 0xeba);
    // The screen's rows: 0x33 classic, 0x3e expansion.
    assert(rows(false).size() == 0x33 && rows(true).size() == 0x3e);
    return 0;
}
