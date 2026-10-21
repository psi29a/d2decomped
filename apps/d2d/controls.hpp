// SPDX-License-Identifier: GPL-3.0-or-later
// Configure Controls (UI 0xb, .\UI\KeyConfig: FUN_004a5270 draws,
// FUN_004a4ad0 the mouse; docs/research/re/controls.md): the functions in
// a scrolling list, each with two keys, and Cancel / Default / Accept.
// d2d adds a page for the gamepad's buttons (the "pad_controls" toggle).
#pragma once

#include "gamepad_map.hpp"
#include "keyconfig.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

// SDL's key as the Windows virtual key game.exe binds (0: none).
[[nodiscard]] std::uint16_t virtual_key(SDL_Keycode key);
// Whether a bound key is held now (Show Items, Run, Stand Still).
[[nodiscard]] bool key_held(std::uint16_t key);
// The mouse's third / fourth / fifth button, as keys_this_frame carries them.
inline constexpr SDL_Keycode kMouseKey = 0x7f000000;

struct ControlsUI {
    enum Result { kStay, kCancel, kAccept };
    bool open = false;
    bool expansion = true;
    bool pad_page = false;                 // d2d: the gamepad's page
    bool pad_extra = true;                 // the "pad_controls" toggle: the page's button shows
    keys::Table table, backup;             // the keys being edited (0x7a6f90) and those to go back to (0x7a6b18)
    pad::Config pad, pad_backup;           // the same for the pad's page
    pad::Family family = pad::Family::Xbox;
    int sel = 0, column = 1, scroll = 0;   // 0x7c02b0, 0x7246d4 (slot 1: the first column), 0x7c02ac
    bool editing = false;                  // 0x7c02b8: the next key goes in
    bool latch = true;                     // 0x724728: Enter / Delete / Esc act (else the next press only sets it)
    std::uint16_t message = 0;             // 0x7c02c4: a refusal's string id, shown 2 s
    std::uint32_t message_ms = 0;
    std::string pad_message;               // the pad page's (d2d's own text)
    static constexpr int kNoButton = -1;
    int hover_button = kNoButton;          // 0x7c02b4: the button under the mouse (game.exe's none is 3)
    unsigned frame = 0;                    // 0x7c02d0: the edited key blinks by it

    // Opening (FUN_004a5200): the keys now kept to go back to.
    void show(const keys::Table& keys_now, const pad::Config& pad_now, bool lod);
    // One frame: keys as virtual keys, the mouse, a pad button just pressed
    // (Button::Count: none) for the pad page's edit.
    Result input(const Scene& scene, const std::vector<std::uint16_t>& keys, const Mouse& mouse, pad::Button pad_press, std::uint32_t now_ms);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, std::uint32_t now_ms) const;
    // Where a pad's cursor goes: the selected row's edited column.
    [[nodiscard]] std::pair<int, int> focus() const;
    // The buttons' centres (a pad's shoulders step them).
    [[nodiscard]] std::vector<std::pair<int, int>> buttons(const Scene& scene) const;
    // The buttons' boxes (FUN_004a5060: by their text's width): {x0, y0, x1, y1}.
    [[nodiscard]] std::vector<std::array<int, 4>> button_boxes(const Scene& scene) const;
    [[nodiscard]] std::vector<std::string> button_texts(const Scene& scene) const;

private:
    enum Press { kNone, kList, kUp, kDown, kAbove, kThumb, kBelow, kButtonPress };
    Press press = kNone;
    int press_button = kNoButton, drag_from = 0, drag_thumb = 0;
    std::uint32_t repeat_ms = 0, now_ = 0;
    [[nodiscard]] int count() const;
    [[nodiscard]] int row_action(int row) const;
    void move(int step);                   // FUN_004a45c0 (the selection)
    void scroll_by(int step);              // FUN_004a4500
    [[nodiscard]] int thumb_y() const;
    void mouse_frame(const Scene& scene, const Mouse& mouse, std::uint32_t now_ms, Result& result);
    void key(std::uint16_t key, std::uint32_t now_ms, Result& result);
    void button(int which, Result& result);
    void refuse(std::uint16_t id, std::uint32_t now_ms) { message = id; message_ms = now_ms; pad_message.clear(); }
    // The pad page's rows: its settings, then each pad::Action.
    void pad_edit(pad::Button button, std::uint32_t now_ms);
};

}  // namespace d2d::client
