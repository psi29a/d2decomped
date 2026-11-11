// SPDX-License-Identifier: GPL-3.0-or-later
// The item panel an NPC's service opens (game.exe's .\UI\UseItem.cpp, UI
// 0xe, kind 1: 0x4c0620): Charsi's Imbue, Larzuk's sockets, Anya's
// personalize. One slot and two buttons on MENU\upgrade; the item on the
// cursor goes in the slot, the first button sends C->S 0x38 kind 0 and a
// wait box stays up till S->C 0x58 answers (docs/research/re/act1-end.md
// "Imbue"). The rules are here; use_item.cpp draws it, Town drives it.
// ponytail: kind 0, the panel S->C 0x58 kind 0 opens on MENU\horadricback
// (the Horadric Staff's orifice, `hst` only), isn't drawn by d2d.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2d::d2s { struct Item; }
namespace d2d::client { struct Scene; }

namespace d2d::client::use_item {

// 0x7c5474: shut, open, an item placed, waiting for the server.
enum class State : std::uint8_t { kClosed, kOpen, kPlaced, kWaiting };

// Where game.exe draws and tests it (FUN_004bfdd0, FUN_004bfc50): fixed
// screen pixels, no left-panel offset. MENU\upgrade frame 0 bottom-left at
// (0x69, 0x10e); the slot x 0x7b..0xd3, y 0x6a..0xdc; the item in it
// centred on x 0xa7 from y 0x6a (FUN_004c01e0).
inline constexpr int kPanelX = 0x69, kPanelBottom = 0x10e;
inline constexpr int kSlotX0 = 0x7b, kSlotX1 = 0xd3, kSlotY0 = 0x6a, kSlotY1 = 0xdc;
inline constexpr int kItemX = 0xa7;
// The instructions (FUN_004bfdd0 kind 1): font16, gold (4), wrapped to 0x12c,
// each line centred on x 0xa0, the first's bottom at y 0x1e, then 0x14 apart.
inline constexpr int kTextX = 0xa0, kTextBottom = 0x1e, kTextStep = 0x14, kTextWidth = 0x12c;
// Presses closer than this are dropped (FUN_004bfc50: timeGetTime against 0x7c5488).
inline constexpr std::uint32_t kPressGapMs = 0x190;

// The buttons (0x7278fc: 2 records of 0x28 bytes from 0x727900): label,
// buysellbtn's bottom-left and frame (+1 pressed), the box they answer in.
struct Button { std::uint16_t label; int x, bottom, frame, x0, x1, y0, y1; };
inline constexpr std::array<Button, 2> kButtons = { {
    { 0xfb1, 0x7a, 0x100, 16, 0x7a, 0x9a, 0xe0, 0x100 },     // the service: imbue
    { 0x102f, 0xb1, 0x100, 10, 0xb1, 0xd1, 0xe0, 0x100 },    // close
} };

// The NPCs whose menu opens it (hcIdx), and what they check (FUN_004bfc50).
inline constexpr int kCharsi = 0x9a, kLarzuk = 0x1ff, kAnya = 0x200;
// The first button's label as its hover draws it (FUN_004bfdd0): 0xfb1
// "imbue", 0x58dc "add sockets", 0x58dd "personalize".
[[nodiscard]] inline std::uint16_t service_label(int hc_idx) {
    return hc_idx == kLarzuk ? 0x58dc : hc_idx == kAnya ? 0x58dd : 0xfb1;
}
// The instructions: 0x275c upgraderestrictEx, 0x58de Addsocketsui2, 0x58db Rename Instruct.
[[nodiscard]] inline std::uint16_t instructions(int hc_idx) {
    return hc_idx == kLarzuk ? 0x58de : hc_idx == kAnya ? 0x58db : 0x275c;
}

// Sounds.txt rows the panel plays (FUN_004b9a00): cursor_pass,
// cursor_select, cursor_error. Placing plays the item's dropsound
// (FUN_004c1d60), which Town looks up.
inline constexpr int kPass = 1, kSelect = 2, kError = 3;

struct Panel {
    State state = State::kClosed;
    int npc = -1;                    // the NPC's index (0x7c0d25: its unit)
    int hc_idx = -1;                 // its class (FUN_004b1670)
    int item = -1;                   // the placed item's id (0x7c5480)
    std::uint32_t press_ms = 0;      // the last press taken (0x7c5488)
    std::array<bool, 2> down{};      // each button held (its record's +0x26)
    bool close_down = false;         // the inventory's close box held (0x7bce90)
    [[nodiscard]] bool open() const { return state != State::kClosed; }
    [[nodiscard]] bool placed() const { return state == State::kPlaced || state == State::kWaiting; }
};

// What a press, release or key asks Town to do.
enum class Act : std::uint8_t {
    kNone,
    kInventory,      // the inventory's own click (state 1: FUN_004922a0)
    kPlace,          // the cursor item into the slot: play its dropsound
    kSend,           // C->S 0x38 kind 0 {npc, item}
    kClose,          // shut, and the NPC's talk ends (FUN_004c0090, FUN_004b3fe0)
};
struct Outcome {
    Act act = Act::kNone;
    std::array<int, 2> sounds{};     // Sounds.txt rows in order, 0 none
};

[[nodiscard]] inline bool in_slot(int mouse_x, int mouse_y) {
    return mouse_x >= kSlotX0 && mouse_x <= kSlotX1 && mouse_y >= kSlotY0 && mouse_y <= kSlotY1;
}
[[nodiscard]] inline int button_at(int mouse_x, int mouse_y) {
    for (std::size_t index = 0; index < kButtons.size(); ++index) {
        const auto& button = kButtons[index];
        if (mouse_x >= button.x0 && mouse_x <= button.x1 && mouse_y >= button.y0 && mouse_y <= button.y1) return int(index);
    }
    return -1;
}

// Opened from the NPC's menu (0x4b35b0 -> 0x4c0620 -> FUN_004c0380): state
// 1, the press clock from now.
inline void open(Panel& panel, int npc, int hc_idx, std::uint32_t now_ms) {
    panel = { .state = State::kOpen, .npc = npc, .hc_idx = hc_idx, .press_ms = now_ms };
}

// A press (FUN_004bfc50; the right button's too, 0x204). `in_inventory`:
// on the inventory panel; `on_close_box`: on its close box; `cursor_item`:
// an item on the cursor; `accepted`: it's identified (flag 0x10,
// FUN_006280a0) and the NPC's check takes it (Charsi FUN_0062c590, Larzuk
// FUN_0062c770, Anya FUN_0062c6a0).
// - within 400 ms of the last press taken: nothing;
// - state 1 on the inventory: the inventory's (its close box held, as
//   FUN_004912a0 holds 0x7bce90); state 2 on the close box: held
//   (FUN_00489190); else a button under it is held (cursor_pass, FUN_004bfbc0);
// - an item on the cursor in the slot: refused (cursor_error), or placed
//   (state 2, FUN_004bfa70), or in state 2 taken back (state 1, cursor_pass).
[[nodiscard]] inline Outcome press(Panel& panel, int mouse_x, int mouse_y, std::uint32_t now_ms, bool in_inventory, bool on_close_box,
                                   bool cursor_item, bool accepted) {
    Outcome out;
    if (!panel.open() || now_ms - panel.press_ms < kPressGapMs) return out;
    panel.press_ms = now_ms;
    if (panel.state == State::kOpen || panel.state == State::kPlaced) {
        if (on_close_box) { panel.close_down = true; return out; }
        if (panel.state == State::kOpen && in_inventory) { out.act = Act::kInventory; return out; }
        panel.down = {};
        if (const int button = button_at(mouse_x, mouse_y); button >= 0) {
            panel.down[std::size_t(button)] = true;
            out.sounds[0] = kPass;
        }
    }
    if (!cursor_item || !in_slot(mouse_x, mouse_y)) return out;
    if (!accepted) { out.sounds[1] = kError; return out; }
    if (panel.state == State::kOpen) {
        panel.state = State::kPlaced;
        out.act = Act::kPlace;
    } else if (panel.state == State::kPlaced) {
        panel.state = State::kOpen;
        panel.item = -1;
        out.sounds[1] = kPass;
    }
    return out;
}

// Cancel (FUN_004c02f0): nothing while waiting; else cursor_pass and shut.
inline void cancel(Panel& panel, Outcome& out) {
    if (panel.state == State::kWaiting) return;
    out.act = Act::kClose;
    out.sounds[1] = kPass;
}

// A release (FUN_004c04e0). State 2: a held button let go over itself
// (FUN_004c0450: cursor_pass; the first sends, the second cancels), else
// over the inventory's close box cancels (FUN_00489190). Other states: the
// close box let go over itself while held cancels (FUN_004890b0 ->
// 0x4c0440), else the buttons as in state 2. Send (FUN_004bfaf0) works in
// state 2 only: cursor_select, state 3, the wait box up.
[[nodiscard]] inline Outcome release(Panel& panel, int mouse_x, int mouse_y, bool on_close_box) {
    Outcome out;
    if (!panel.open()) return out;
    const bool close_held = std::exchange(panel.close_down, false);
    if (panel.state != State::kPlaced && close_held && on_close_box) { cancel(panel, out); return out; }
    const int button = button_at(mouse_x, mouse_y);
    if (button >= 0 && panel.down[std::size_t(button)]) {
        out.sounds[0] = kPass;
        if (button == 1) cancel(panel, out);
        else if (panel.state == State::kPlaced) {
            panel.state = State::kWaiting;
            out.act = Act::kSend;
            out.sounds[1] = kSelect;
        }
    } else if (panel.state == State::kPlaced && on_close_box) {
        cancel(panel, out);
    }
    panel.down = {};
    return out;
}

// Esc or Space (FUN_004c0150): shut, no sound, unless waiting.
[[nodiscard]] inline Outcome key(const Panel& panel) {
    Outcome out;
    if (panel.open() && panel.state != State::kWaiting) out.act = Act::kClose;
    return out;
}

// S->C 0x58's kind (FUN_004c0550): 1 shuts it; 4 takes the wait box down,
// the item still placed; 6 (done) and 7 (refused) shut it and end the NPC's
// talk (FUN_004b3fb0). Returns whether to shut it.
[[nodiscard]] inline bool reply(Panel& panel, int kind) {
    if (!panel.open()) return false;
    if (kind == 4 && panel.state == State::kWaiting) panel.state = State::kPlaced;
    return kind == 1 || kind == 6 || kind == 7;
}

// The panel (FUN_004c01e0), `held` the cursor item, drawn in the slot while placed (use_item.cpp).
void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Panel& panel, const d2d::d2s::Item* held,
          int mouse_x, int mouse_y, std::uint32_t now_ms);

}  // namespace d2d::client::use_item
