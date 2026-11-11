// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// An NPC service's item panel (apps/d2d/use_item.hpp, game.exe's
// .\UI\UseItem.cpp): the slot takes only an accepted cursor item
// (FUN_004bfc50), presses closer than 400 ms are dropped, the first button
// sends only with an item placed (FUN_004bfaf0) and the wait holds against
// cancel and Esc (FUN_004c02f0, FUN_004c0150), S->C 0x58 6 / 7 shut it
// (FUN_004c0550).
#include "use_item.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::client::use_item;

int main() try {
    // The table at 0x727900 and the slot's box.
    assert(kButtons[0].label == 0xfb1 && kButtons[0].frame == 16 && kButtons[1].label == 0x102f && kButtons[1].frame == 10);
    assert(button_at(0x7a, 0xe0) == 0 && button_at(0x9a, 0x100) == 0 && button_at(0x9b, 0xf0) == -1 && button_at(0xb1, 0xe0) == 1);
    assert(in_slot(0x7b, 0x6a) && in_slot(0xd3, 0xdc) && !in_slot(0x7a, 0x80) && !in_slot(0xa0, 0xdd));
    assert(service_label(kCharsi) == 0xfb1 && service_label(kLarzuk) == 0x58dc && service_label(kAnya) == 0x58dd);
    assert(instructions(kCharsi) == 0x275c && instructions(kLarzuk) == 0x58de && instructions(kAnya) == 0x58db);

    Panel panel;
    open(panel, 5, kCharsi, 1000);
    assert(panel.state == State::kOpen && panel.npc == 5);
    constexpr int kSlotX = 0xa7, kSlotY = 0xa0, kImbueX = 0x8a, kCloseX = 0xc0, kButtonY = 0xf0;
    // Within 400 ms of the open: dropped.
    assert(press(panel, kSlotX, kSlotY, 1200, false, false, true, true).act == Act::kNone && panel.state == State::kOpen);
    // State 1: the inventory takes its own clicks.
    assert(press(panel, 600, 300, 1400, true, false, false, false).act == Act::kInventory);
    // Unidentified or not imbuable: cursor_error, not placed.
    auto out = press(panel, kSlotX, kSlotY, 1800, false, false, true, false);
    assert(out.act == Act::kNone && out.sounds[1] == kError && panel.state == State::kOpen);
    // Imbue with nothing placed: the button clicks, nothing goes.
    (void)press(panel, kImbueX, kButtonY, 2200, false, false, false, false);
    assert(panel.down[0]);
    out = release(panel, kImbueX, kButtonY, false);
    assert(out.act == Act::kNone && out.sounds[0] == kPass && !panel.down[0]);
    // Placed; a press on the slot takes it back; placed again.
    assert(press(panel, kSlotX, kSlotY, 2600, false, false, true, true).act == Act::kPlace && panel.state == State::kPlaced);
    out = press(panel, kSlotX, kSlotY, 3000, false, false, true, true);
    assert(out.act == Act::kNone && out.sounds[1] == kPass && panel.state == State::kOpen);
    assert(press(panel, kSlotX, kSlotY, 3400, false, false, true, true).act == Act::kPlace);
    // State 2: the inventory's clicks are the panel's.
    assert(press(panel, 600, 300, 3800, true, false, true, true).act == Act::kNone);
    // Imbue let go off the button: nothing; on it: sent, waiting.
    (void)press(panel, kImbueX, kButtonY, 4200, false, false, true, true);
    assert(release(panel, 10, 10, false).act == Act::kNone && panel.state == State::kPlaced);
    (void)press(panel, kImbueX, kButtonY, 4600, false, false, true, true);
    out = release(panel, kImbueX, kButtonY, false);
    assert(out.act == Act::kSend && out.sounds[0] == kPass && out.sounds[1] == kSelect && panel.state == State::kWaiting);
    // Waiting: close, Esc and the slot do nothing; 0x58 4 puts it back to placed.
    assert(press(panel, kCloseX, kButtonY, 5000, false, false, true, true).act == Act::kNone && !panel.down[1]);
    assert(key(panel).act == Act::kNone);
    assert(!reply(panel, 4) && panel.state == State::kPlaced);
    panel.state = State::kWaiting;
    assert(reply(panel, 6));
    // Close (the second button), Esc and the inventory's close box shut it.
    open(panel, 5, kCharsi, 0);
    (void)press(panel, kCloseX, kButtonY, 1000, false, false, false, false);
    out = release(panel, kCloseX, kButtonY, false);
    assert(out.act == Act::kClose && out.sounds[1] == kPass);
    open(panel, 5, kCharsi, 0);
    assert(key(panel).act == Act::kClose);
    (void)press(panel, 600, 500, 1000, true, true, false, false);
    assert(panel.close_down && release(panel, 600, 500, true).act == Act::kClose);
    // State 2: let go on the close box shuts it unheld (FUN_00489190 on release).
    open(panel, 5, kCharsi, 0);
    (void)press(panel, kSlotX, kSlotY, 1000, false, false, true, true);
    assert(release(panel, 600, 500, true).act == Act::kClose);
    // 0x58 7 shuts it too; 1 shuts it; a shut panel takes nothing.
    open(panel, 5, kCharsi, 0);
    assert(reply(panel, 7) && reply(panel, 1));
    panel = {};
    assert(!reply(panel, 6) && press(panel, kSlotX, kSlotY, 9000, false, false, true, true).act == Act::kNone);
    std::printf("OK: the item panel's slot, buttons, wait and answers\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_use_item: %s\n", error.what());
    return 1;
}
