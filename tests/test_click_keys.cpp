// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// d2d's click keys (apps/d2d/keyconfig.hpp Clicks, deviations.md
// improvement 15): Attack / Use and Right-hand Skill on Mouse 3..5 or a
// key, outside game.exe's table, one function per key across both, and
// d2d.cfg's form.
#include "keyconfig.hpp"

#include <array>
#include <cassert>
#include <cstdint>

using namespace d2d::client::keys;

int main() {
    Table table = defaults();
    Clicks clicks = kNoClicks;
    std::uint16_t message = 0;

    // Mouse 5 (Toggle Run's second key by default) to Attack / Use: it leaves
    // the table, and the table stays one game.exe takes (.key, 0x476 bytes).
    assert(key_of(table, kToggleRun, 0) == kMouse5);
    assert(assign_click(table, clicks, kClickAttack, 1, kMouse5, message) && message == 0);
    assert(clicks[kClickAttack][1] == kMouse5);
    assert(key_of(table, kToggleRun, 0) == kNone && key_of(table, kToggleRun, 1) == 'R');
    assert(click_of(clicks, kMouse5) == kClickAttack && click_of(clicks, kMouse4) == -1 && click_of(clicks, kNone) == -1);
    assert(read_character(write_character(table)).has_value());

    // The same key to Right-hand Skill moves it there.
    assert(assign_click(table, clicks, kClickSkill, 0, kMouse5, message));
    assert(clicks[kClickAttack][1] == kNone && clicks[kClickSkill][0] == kMouse5);
    assert(click_of(clicks, kMouse5) == kClickSkill);

    // A key the table takes back leaves the clicks (ControlsUI::assign_key).
    assert(assign(table, kToggleRun, 0, kMouse5, message));
    release_click_key(clicks, kMouse5);
    assert(click_of(clicks, kMouse5) == -1);

    // Refused: the wheel (nothing to hold), the left / right buttons, Esc,
    // Print Screen; nothing moves.
    const Table before = table;
    assert(!assign_click(table, clicks, kClickAttack, 1, kWheelUp, message) && message == kCannotWheel);
    assert(!assign_click(table, clicks, kClickAttack, 1, 1, message) && message == kCannotKey);
    assert(!assign_click(table, clicks, kClickAttack, 1, 0x1b, message) && message == kCannotKey);
    assert(!assign_click(table, clicks, kClickSkill, 1, kPrintScreen, message) && message == kCannotKey);
    assert(!assign_click(table, clicks, 2, 1, 'E', message));
    assert(table == before && clicks == kNoClicks);

    // A letter: off its function in the table (Q: Quest Log), on the click.
    assert(assign_click(table, clicks, kClickSkill, 1, 'Q', message));
    assert(key_of(table, kQuestLog, 1) == kNone && clicks[kClickSkill][1] == 'Q');

    // d2d.cfg: decimal virtual keys, "none"; nonsense and refused keys read as none.
    assert(click_text({ kMouse4, kNone }) == "257,none");
    assert(click_pair("257,none") == (std::array<std::uint16_t, 2>{ kMouse4, kNone }));
    assert(click_pair("81,258") == (std::array<std::uint16_t, 2>{ 'Q', kMouse5 }));
    assert(click_pair("") == (std::array<std::uint16_t, 2>{ kNone, kNone }));
    assert(click_pair("x,259") == (std::array<std::uint16_t, 2>{ kNone, kNone }));   // 259: the wheel
    assert(click_pair("1,27") == (std::array<std::uint16_t, 2>{ kNone, kNone }));     // the left button, Esc
    assert(click_pair("258,258") == (std::array<std::uint16_t, 2>{ kMouse5, kNone }));
    assert(click_pair("99999999,258") == (std::array<std::uint16_t, 2>{ kNone, kMouse5 }));
    // Both pairs from d2d.cfg: a key in both stays on Attack / Use only.
    const Clicks loaded = click_load("258,257", "257,81");
    assert(loaded[kClickAttack] == (std::array<std::uint16_t, 2>{ kMouse5, kMouse4 }));
    assert(loaded[kClickSkill] == (std::array<std::uint16_t, 2>{ kNone, 'Q' }));
    return 0;
}
