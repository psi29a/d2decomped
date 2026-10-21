// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for controls.hpp.
#include "controls.hpp"

#include "common.hpp"
#include "gamepad_map.hpp"
#include "keyconfig.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kWidth = int(kScreenWidth), kHeight = int(kScreenHeight);
constexpr int kLeft = (kWidth - 0x26c) / 2, kTop = (kHeight - 0x1a4) / 2;   // the panel's corner (90, 90)
constexpr int kColumn = (kWidth - 2 * kLeft - 0x31) / 3;                       // 190
constexpr int kShown = 15;                                                     // rows on screen
constexpr int kBarX = kWidth - kLeft - 0x1f;                                   // the scroll bar's left (679)
constexpr std::array<std::array<std::uint8_t, 3>, 6> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 }, { 105, 105, 105 } } };

// The pad page (d2d): its settings, two presets, then the actions (the
// game menu's Start is fixed, as Esc is in game.exe's list).
enum PadRowKind { kSetting, kPreset, kPadAction, kGap };
struct PadRow { PadRowKind kind; int index; std::string_view name; };
constexpr PadRow kPadRows[] = {
    { kSetting, 0, "Left-handed sticks" }, { kSetting, 1, "Swap confirm and back" }, { kSetting, 2, "Dead zone" },
    { kSetting, 3, "Cursor speed" }, { kPreset, 0, "Right-handed layout" }, { kPreset, 1, "Left-handed layout" }, { kGap, 0, "" },
    { kPadAction, int(pad::Action::Primary), "Attack / Use" }, { kPadAction, int(pad::Action::Secondary), "Right-hand Skill" },
    { kPadAction, int(pad::Action::Back), "Back (store, dialog)" }, { kPadAction, int(pad::Action::Inventory), "Inventory Screen" },
    { kPadAction, int(pad::Action::Character), "Character Screen" }, { kPadAction, int(pad::Action::SkillTree), "Skill Tree" },
    { kPadAction, int(pad::Action::QuestLog), "Quest Log" }, { kPadAction, int(pad::Action::Automap), "Automap" },
    { kPadAction, int(pad::Action::ToggleRun), "Toggle Run/Walk" }, { kPadAction, int(pad::Action::SwapWeapons), "Swap Weapons" },
    { kPadAction, int(pad::Action::SkillPicker), "Skill Picker" }, { kPadAction, int(pad::Action::Recentre), "Centre Cursor" },
    { kPadAction, int(pad::Action::Belt1), "Use Belt 1" }, { kPadAction, int(pad::Action::Belt2), "Use Belt 2" },
    { kPadAction, int(pad::Action::Belt3), "Use Belt 3" }, { kPadAction, int(pad::Action::Belt4), "Use Belt 4" },
    { kPadAction, int(pad::Action::Trade), "Buy / Sell (store)" }, { kPadAction, int(pad::Action::TabPrev), "Store Tab Before" },
    { kPadAction, int(pad::Action::TabNext), "Store Tab After" },
};
constexpr int kPadCount = int(std::size(kPadRows));

std::string pad_action_name(pad::Action action) {
    for (const auto& row : kPadRows) if (row.kind == kPadAction && row.index == int(action)) return std::string(row.name);
    return std::string(pad::kActionNames[std::size_t(action)]);
}

const d2d::palette::Palette& palette(const Scene& scene) { return scene.act_pal.entries().empty() ? scene.pal : scene.act_pal; }
const d2d::font::Font& font(const Scene& scene) { return scene.font_chat.line_height() > 0 ? scene.font_chat : scene.font; }

}  // namespace

std::uint16_t virtual_key(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) return std::uint16_t('A' + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9) return std::uint16_t(key);
    if (key >= SDLK_F1 && key <= SDLK_F12) return std::uint16_t(0x70 + (key - SDLK_F1));
    if (key >= SDLK_F13 && key <= SDLK_F24) return std::uint16_t(0x7c + (key - SDLK_F13));
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return std::uint16_t(0x61 + (key - SDLK_KP_1));
    if (key >= kMouseKey && key <= kMouseKey + 2) return std::uint16_t(keys::kMouse3 + (key - kMouseKey));
    switch (key) {
        case SDLK_BACKSPACE: return 0x08;     case SDLK_TAB: return 0x09;        case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;  case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
        case SDLK_LALT: case SDLK_RALT: return 0x12;      case SDLK_PAUSE: return 0x13;  case SDLK_CAPSLOCK: return 0x14;
        case SDLK_ESCAPE: return 0x1b;        case SDLK_SPACE: return 0x20;      case SDLK_PAGEUP: return 0x21;
        case SDLK_PAGEDOWN: return 0x22;      case SDLK_END: return 0x23;        case SDLK_HOME: return 0x24;
        case SDLK_LEFT: return 0x25;          case SDLK_UP: return 0x26;         case SDLK_RIGHT: return 0x27;
        case SDLK_DOWN: return 0x28;          case SDLK_PRINTSCREEN: return 0x2c; case SDLK_INSERT: return 0x2d;
        case SDLK_DELETE: return 0x2e;        case SDLK_LGUI: return 0x5b;       case SDLK_RGUI: return 0x5c;
        case SDLK_APPLICATION: return 0x5d;   case SDLK_KP_0: return 0x60;       case SDLK_KP_MULTIPLY: return 0x6a;
        case SDLK_KP_PLUS: return 0x6b;       case SDLK_KP_MINUS: return 0x6d;   case SDLK_KP_PERIOD: return 0x6e;
        case SDLK_KP_DIVIDE: return 0x6f;     case SDLK_NUMLOCKCLEAR: return 0x90; case SDLK_SCROLLLOCK: return 0x91;
        case SDLK_SEMICOLON: return 0xba;     case SDLK_EQUALS: return 0xbb;     case SDLK_COMMA: return 0xbc;
        case SDLK_MINUS: return 0xbd;         case SDLK_PERIOD: return 0xbe;     case SDLK_SLASH: return 0xbf;
        case SDLK_GRAVE: return 0xc0;         case SDLK_LEFTBRACKET: return 0xdb; case SDLK_BACKSLASH: return 0xdc;
        case SDLK_RIGHTBRACKET: return 0xdd;  case SDLK_APOSTROPHE: return 0xde;
        default: return 0;
    }
}

bool key_held(std::uint16_t key) {
    if (key == keys::kNone) return false;
    if (key >= keys::kMouse3 && key <= keys::kMouse5) {
        static constexpr SDL_MouseButtonFlags kMasks[] = { SDL_BUTTON_MMASK, SDL_BUTTON_X1MASK, SDL_BUTTON_X2MASK };
        return (SDL_GetMouseState(nullptr, nullptr) & kMasks[key - keys::kMouse3]) != 0;
    }
    const SDL_Keymod mods = SDL_GetModState();
    if (key == 0x10) return (mods & SDL_KMOD_SHIFT) != 0;
    if (key == 0x11) return (mods & SDL_KMOD_CTRL) != 0;
    if (key == 0x12) return (mods & SDL_KMOD_ALT) != 0;
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    // ponytail: a scan over SDL's keys for the one with this virtual key; fine for a few held functions a frame
    for (int scancode = 0; scancode < count; ++scancode)
        if (state[scancode] && virtual_key(SDL_GetKeyFromScancode(SDL_Scancode(scancode), SDL_KMOD_NONE, false)) == key) return true;
    return false;
}

void ControlsUI::show(const keys::Table& keys_now, const pad::Config& pad_now, bool lod) {
    open = true;
    expansion = lod;
    table = backup = keys_now;
    pad = pad_backup = pad_now;
    editing = false;
    pad_page = false;
    sel = scroll = 0;
    column = 1;
    latch = true;
    message = 0;
    pad_message.clear();
    press = kNone;
}

int ControlsUI::count() const { return pad_page ? kPadCount : int(keys::rows(expansion).size()); }

// The row's function, or kSeparator for a gap (the pad page: its index, or kSeparator).
int ControlsUI::row_action(int row) const {
    if (row < 0 || row >= count()) return keys::kSeparator;
    if (pad_page) return kPadRows[row].kind == kGap ? int(keys::kSeparator) : row;
    return keys::rows(expansion)[std::size_t(row)].action;
}

// The list's last first row (FUN_004a44c0).
static int max_scroll(int rows) { return std::max(rows - kShown, 1); }

int ControlsUI::thumb_y() const {
    const int thumb = expansion && !pad_page ? 5 : 6;
    return (0xf6 - thumb * 12) * scroll / max_scroll(count()) + 0x2f + kTop;
}

void ControlsUI::scroll_by(int step) { scroll = std::clamp(scroll + step, 0, max_scroll(count())); }

// FUN_004a45c0: the selection one row on, the list scrolled to keep it shown.
void ControlsUI::move(int step) {
    sel = std::clamp(sel + step, 0, count() - 1);
    if (sel < scroll) scroll = sel;
    while (sel >= scroll + kShown) ++scroll;
}

std::pair<int, int> ControlsUI::focus() const {
    const int on_screen = std::clamp(sel - scroll, 0, kShown - 1);
    return { kLeft + 0x1e + (column == 1 ? kColumn : 2 * kColumn) + 20, kTop + 0x34 + 18 * on_screen - 6 };
}

std::vector<std::string> ControlsUI::button_texts(const Scene& scene) const {
    std::vector<std::string> out = { string_id(scene, 0xf86), string_id(scene, 0xf84), string_id(scene, 0xf85) };   // Cancel, Default, Accept
    if (pad_extra) out.emplace_back(pad_page ? "Keyboard" : "Gamepad");
    return out;
}

// FUN_004a5060: the panel's width in equal parts, each button its text
// and 10 either side, 0x149..0x16f down. (d2d's fourth, the pad page's,
// takes a quarter.)
std::vector<std::array<int, 4>> ControlsUI::button_boxes(const Scene& scene) const {
    const auto texts = button_texts(scene);
    const int part = (kWidth - 2 * kLeft) / int(texts.size());
    std::vector<std::array<int, 4>> out;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        const int centre = part * int(i) + kLeft + part / 2, half = font(scene).measure(texts[i]) / 2;
        out.push_back({ centre - half - 10, kTop + 0x149, centre + half + 10, kTop + 0x16f });
    }
    return out;
}

std::vector<std::pair<int, int>> ControlsUI::buttons(const Scene& scene) const {
    std::vector<std::pair<int, int>> out;
    for (const auto& box : button_boxes(scene)) out.emplace_back((box[0] + box[2]) / 2, (box[1] + box[3]) / 2);
    return out;
}

// The buttons' handlers (0x7246da): Cancel (0x4a5040) the keys back and to
// the menu, Default (0x4a5010, FUN_00469a60) the defaults in, Accept
// (0x4a5020) kept and back to the game.
void ControlsUI::button(int which, Result& result) {
    if (which == 0) { table = backup; pad = pad_backup; result = kCancel; }
    else if (which == 1) { if (pad_page) pad = pad::Config{}; else table = keys::defaults(); }
    else if (which == 2) {
        if (!pad::clashes(pad.bindings).empty()) { pad_message = "Two actions share a button"; message = 0; message_ms = now_; return; }
        result = kAccept;
    } else if (which == 3) {
        pad_page = !pad_page;
        editing = false;
        sel = scroll = 0;
        column = 1;
    }
}

// The pad page's edit: a setting steps, a preset applies, an action takes
// the button (which leaves an action it clashed with, as a key does).
void ControlsUI::pad_edit(pad::Button pressed, std::uint32_t now_ms) {
    const auto& row = kPadRows[std::size_t(std::clamp(sel, 0, kPadCount - 1))];
    if (row.kind == kSetting) {
        if (row.index == 0) pad.swap_sticks = !pad.swap_sticks;
        else if (row.index == 1) pad.swap_confirm = !pad.swap_confirm;
        else if (row.index == 2) pad.deadzone = pad.deadzone >= 0.395f ? 0.05f : pad.deadzone + 0.05f;
        else pad.cursor_speed = pad.cursor_speed >= 1495.f ? 350.f : pad.cursor_speed + 150.f;
        editing = false;
        return;
    }
    if (row.kind == kPreset) {
        const pad::Config keep = pad;
        pad = pad::Config{};
        pad.deadzone = keep.deadzone;
        pad.cursor_speed = keep.cursor_speed;
        pad.swap_sticks = row.index == 1;
        editing = false;
        return;
    }
    if (pressed == pad::Button::Count) return;
    const auto action = pad::Action(row.index);
    if (pad::is_dpad(pressed) && (pad::kWhere[std::size_t(action)] & pad::kDpadHops) != 0) {
        pad_message = "The d-pad moves the cursor in panels";
        message = 0;
        message_ms = now_ms;
        editing = false;
        return;
    }
    const std::size_t slot = column == 1 ? 0 : 1;
    pad_message.clear();
    for (std::size_t other = 0; other < std::size_t(pad::Action::Count); ++other) {
        if (other == std::size_t(action) || (pad::kWhere[other] & pad::kWhere[std::size_t(action)]) == 0) continue;
        for (auto& button : pad.bindings.buttons[other])
            if (button == pressed) {
                button = pad::Bindings::kNone;
                pad_message = std::string(pad::label(family, pressed)) + " was " + pad_action_name(pad::Action(other)) + "'s";
                message = 0;
                message_ms = now_ms;
            }
    }
    auto& pair = pad.bindings[action];
    if (pair[1 - slot] == pressed) pair[1 - slot] = pad::Bindings::kNone;
    pair[slot] = pressed;
    editing = false;
}

// A key (FUN_004a5a40..FUN_004a63b0): editing, it goes in (Esc stops);
// else the arrows move, Delete clears, Enter edits, Esc cancels.
void ControlsUI::key(std::uint16_t key, std::uint32_t now_ms, Result& result) {
    if (editing) {
        if (key == 0x1b) { editing = false; latch = false; return; }
        if (pad_page) return;                       // the pad page takes a pad's button
        std::uint16_t refused = 0;
        if (keys::assign(table, row_action(sel), column, key, refused)) {
            editing = false;
            latch = !(key == 0x0d || key == 0x2e || key == 0x08 || key == 0x20);
        } else {
            latch = true;
        }
        if (refused) refuse(refused, now_ms);
        return;
    }
    if (key == 0x26 || key == 0x28) {
        const int was = sel;
        do move(key == 0x26 ? -1 : 1);
        while (row_action(sel) == keys::kSeparator && sel > 0 && sel < count() - 1);
        if (row_action(sel) == keys::kSeparator) sel = was;
        return;
    }
    if (key == 0x25) { column = 1; return; }
    if (key == 0x27) { column = 0; return; }
    if (key == 0x2e || key == 0x0d || key == 0x1b) {
        if (!latch) { latch = true; return; }
        if (key == 0x1b) { button(0, result); return; }
        if (row_action(sel) == keys::kSeparator) return;
        if (key == 0x2e) {
            if (pad_page) { if (kPadRows[std::size_t(sel)].kind == kPadAction) pad.bindings[pad::Action(kPadRows[std::size_t(sel)].index)][column == 1 ? 0 : 1] = pad::Bindings::kNone; }
            else keys::clear(table, row_action(sel), column);
            return;
        }
        editing = true;
        latch = false;
        if (pad_page) pad_edit(pad::Button::Count, now_ms);
    }
}

// The mouse (FUN_004a4ad0): a press in the list selects (and its release
// on the same row and column edits it), the arrows and the bar scroll,
// a button acts on its release over it.
void ControlsUI::mouse_frame(const Scene& scene, const Mouse& mouse, std::uint32_t now_ms, Result& result) {
    const int mouse_x = mouse.x, mouse_y = mouse.y;
    const auto boxes = button_boxes(scene);
    hover_button = kNoButton;
    for (std::size_t i = 0; i < boxes.size(); ++i)
        if (mouse_x >= boxes[i][0] && mouse_x <= boxes[i][2] && mouse_y >= boxes[i][1] && mouse_y <= boxes[i][3]) hover_button = int(i);
    const bool on_bar = mouse_x >= kBarX && mouse_x <= kWidth - kLeft - 0x12;
    const int thumb = expansion && !pad_page ? 5 : 6, top_of_thumb = thumb_y();
    const bool in_list = mouse_x >= kLeft + 0x12 && mouse_x <= kLeft + (kColumn + 6) * 3 && mouse_y >= kTop + 0x28 && mouse_y <= kTop + 0x136;
    const auto row_at = [&] {                                   // FUN_004a4720
        for (int i = 0; i < kShown && scroll + i < count(); ++i)
            if (mouse_y <= kTop + 0x3a + 18 * i) return scroll + i;
        return std::min(scroll + kShown, count() - 1);
    };
    const int column_at = mouse_x < kLeft + 0x12 + 2 * kColumn ? 1 : 0;   // FUN_004a4780
    const bool repeat = now_ms > repeat_ms + 50;
    if (mouse.press_this_frame) {
        if (editing && !pad_page) { editing = false; }        // 0x4a6000: a click ends the edit
        if (in_list) press = kList;
        else if (on_bar && mouse_y >= kTop + 0x23 && mouse_y <= kTop + 0x2f) press = kUp;
        else if (on_bar && mouse_y >= kTop + 0x125 && mouse_y <= kTop + 0x131) press = kDown;
        else if (on_bar && mouse_y > kTop + 0x2f && mouse_y < top_of_thumb) press = kAbove;
        else if (on_bar && mouse_y >= top_of_thumb && mouse_y <= top_of_thumb + thumb * 12) { press = kThumb; drag_from = mouse_y; drag_thumb = top_of_thumb; }
        else if (on_bar && mouse_y > top_of_thumb + thumb * 12 && mouse_y < kTop + 0x125) press = kBelow;
        else if (hover_button != kNoButton) press = kButtonPress;
        press_button = hover_button;
        repeat_ms = 0;
    }
    if (mouse.down) {
        if (press == kList) {
            if (mouse_y < kTop + 0x28 && repeat) { move(-1); repeat_ms = now_ms; }
            if (mouse_y > kTop + 0x136 && repeat) { move(1); repeat_ms = now_ms; }
            if (in_list) {
                const int was = sel, was_column = column;
                sel = row_at();
                column = column_at;
                if (row_action(sel) == keys::kSeparator) { sel = was; column = was_column; }
            }
        } else if (press == kUp && repeat) { scroll_by(-1); repeat_ms = now_ms; }
        else if (press == kDown && repeat) { scroll_by(1); repeat_ms = now_ms; }
        else if (press == kAbove && repeat) { scroll_by(-14); repeat_ms = now_ms; }
        else if (press == kBelow && repeat) { scroll_by(14); repeat_ms = now_ms; }
        else if (press == kThumb) {
            const int target = (drag_thumb - kTop + mouse_y - drag_from - 0x2f) * (max_scroll(count()) + 1) / (0xf6 - thumb * 12);
            scroll_by(target - scroll);
        }
        if (press == kButtonPress && hover_button != press_button) hover_button = kNoButton;
    }
    if (mouse.release_this_frame) {
        if (press == kList && in_list && row_at() == sel && column_at == column && row_action(sel) != keys::kSeparator) {   // FUN_004a5e60
            editing = true;
            if (pad_page) pad_edit(pad::Button::Count, now_ms);
        }
        if (press == kButtonPress && hover_button == press_button && press_button != kNoButton) button(press_button, result);
        press = kNone;
    }
    if (mouse.wheel != 0) {                                    // FUN_004a59d0 / FUN_004a63b0
        if (editing && !pad_page) {
            std::uint16_t refused = 0;
            if (keys::assign(table, row_action(sel), column, mouse.wheel > 0 ? keys::kWheelUp : keys::kWheelDown, refused)) editing = false;
            if (refused) refuse(refused, now_ms);
        } else if (!editing) {
            scroll_by(mouse.wheel > 0 ? -1 : 1);
        }
    }
}

ControlsUI::Result ControlsUI::input(const Scene& scene, const std::vector<std::uint16_t>& keys_now, const Mouse& mouse, pad::Button pad_press,
                                     std::uint32_t now_ms) {
    Result result = kStay;
    now_ = now_ms;
    ++frame;
    if (editing && pad_page && pad_press != pad::Button::Count && pad_press != pad::Button::Start) { pad_edit(pad_press, now_ms); return result; }
    if (editing && pad_page && pad_press == pad::Button::Start) { editing = false; return result; }
    for (const auto key_now : keys_now) key(key_now, now_ms, result);
    if (result == kStay) mouse_frame(scene, mouse, now_ms, result);
    if (result != kStay) { open = false; editing = false; }
    return result;
}

void ControlsUI::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, std::uint32_t now_ms) const {
    const auto& pal = palette(scene);
    const auto& text_font = font(scene);
    const auto text = [&](const std::string& words, int left, int baseline, int colour) {
        const auto& rgb = kRgb[std::size_t(std::clamp(colour, 0, 5))];
        text_font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, left, glyph_top(text_font, baseline), words, rgb[0], rgb[1], rgb[2]);
    };
    const auto darken = [&](int left, int top, int width, int height) {   // FUN_0046efd0 colour 0, mode 1: half
        for (int py = std::max(0, top); py < std::min(kHeight, top + height); ++py)
            for (int px = std::max(0, left); px < std::min(kWidth, left + width); ++px)
                for (int channel = 0; channel < 3; ++channel) {
                    auto& value = framebuffer[(std::size_t(py) * kScreenWidth + std::size_t(px)) * 4 + std::size_t(channel)];
                    value = std::uint8_t(value / 2);
                }
    };
    const auto cel = [&](const d2d::dc6::Sprite& sprite, int frame_index, int left, int bottom) {   // FUN_004f6480 draw mode 5
        if (frame_index < 0 || std::uint32_t(frame_index) >= sprite.frames_per_direction()) return;
        const auto& image = sprite.frame(0, std::uint32_t(frame_index));
        blit_sprite(framebuffer, image, pal, left, bottom - int(image.height));
    };
    // FUN_00452e50: a box of boxpieces round {x0, y0, x1, y1}.
    const auto box = [&](int left, int top, int last_x, int last_y) {
        const auto& pieces = scene.boxpieces;
        const int right = last_x + 1, bottom = last_y + 1;
        int piece = 0;
        for (int along = left + 12; along < right - 13; along += 12) cel(pieces, ++piece % 6 + 2, along, top + 12);
        for (int along = left + 13; along < right - 13; along += 12) cel(pieces, ++piece % 6 + 0x10, along, bottom + 10);
        for (int down = top + 0x16; down < bottom - 1; down += 12) cel(pieces, ++piece % 3 + 10, left - 4, down);
        for (int down = top + 24; down < bottom - 1; down += 12) cel(pieces, ++piece % 3 + 0xd, right - 7, down);
        cel(pieces, 0, left, top + 12);
        cel(pieces, 1, right - 12, top + 12);
        cel(pieces, 8, left, bottom + 1);
        cel(pieces, 9, right - 12, bottom + 1);
    };
    darken(kLeft, kTop, kWidth - 2 * kLeft, 0x171);
    box(kLeft, kTop + 3, kWidth - kLeft - 3, kTop + 0x170);
    box(kLeft, kTop + 0x143, kWidth - kLeft - 3, kTop + 0x170);
    for (int i = 0; i < 3; ++i) text(string_id(scene, std::uint16_t(0xf51 + i)), kLeft + 0x12 + kColumn * i, kTop + 0x1e, 4);
    const auto key_text = [&](std::uint16_t key) -> std::string {
        const std::uint16_t id = keys::name_id(key);
        return id ? string_id(scene, id) : std::string(1, char(key & 0xff));
    };
    const auto pad_text = [&](int row, int slot) -> std::string {
        const auto& pad_row = kPadRows[std::size_t(row)];
        if (pad_row.kind == kSetting) {
            if (slot == 0) return "";
            if (pad_row.index == 0) return pad.swap_sticks ? "On" : "Off";
            if (pad_row.index == 1) return pad.swap_confirm ? "On" : "Off";
            if (pad_row.index == 2) return std::to_string(int(std::lround(pad.deadzone * 100))) + "%";
            return std::to_string(int(std::lround(pad.cursor_speed)));
        }
        if (pad_row.kind != kPadAction) return "";
        const auto button = pad.bindings[pad::Action(pad_row.index)][slot == 1 ? 0 : 1];
        return button == pad::Bindings::kNone ? string_id(scene, 0xeb2) : std::string(pad::label(family, button));
    };
    const bool blink_off = editing && (frame & 0xf) <= 4;
    for (int i = 0; i < kShown && scroll + i < count(); ++i) {
        const int row = scroll + i, baseline = kTop + 0x34 + 18 * i;
        const int action = row_action(row);
        if (action == keys::kSeparator) continue;
        int name_colour = 0;
        if (row == sel) {
            darken(kLeft + 0x12, baseline - 0x11, kColumn * 3, 0x12);
            name_colour = editing ? 3 : 0;
        }
        text(pad_page ? std::string(kPadRows[std::size_t(row)].name) : string_id(scene, keys::rows(expansion)[std::size_t(row)].name), kLeft + 0x1e, baseline, name_colour);
        for (const int slot : { 1, 0 }) {
            if (row == sel && blink_off && column == slot) continue;
            int colour;                                                     // FUN_004a4fa0
            std::string words;
            if (pad_page) {
                words = pad_text(row, slot);
                colour = row == sel && (editing || slot == column) ? 3 : 4;
            } else {
                const std::uint16_t key = keys::key_of(table, action, slot);
                words = key_text(key);
                colour = row == sel && (editing || slot == column) ? 3
                       : keys::key_of(table, action, 0) == keys::kNone && keys::key_of(table, action, 1) == keys::kNone ? 1
                       : key != keys::kNone ? 4 : 5;
            }
            text(words, kLeft + 0x1e + (slot == 1 ? kColumn : 2 * kColumn), baseline, colour);
        }
    }
    // The scroll bar (textslid): its trough, thumb and arrows.
    const auto& slider = scene.textslid;
    for (int k = 0; k < 0x15; ++k) cel(slider, 0xd, kBarX, kTop + 0x3b + 12 * k);
    const int thumb = expansion && !pad_page ? 5 : 6, top_of_thumb = thumb_y();
    for (int k = 0; k < thumb; ++k) cel(slider, 0xe, kBarX - 1, top_of_thumb + 12 + 12 * k);
    cel(slider, press == kUp ? 9 : 0xb, kBarX, kTop + 0x2f);
    cel(slider, press == kDown ? 8 : 0xa, kBarX, kTop + 0x131);
    // The buttons: gold, or their own colour under the mouse (Cancel red, Default blue, Accept green).
    static constexpr int kButtonColour[] = { 1, 3, 2, 3 };
    const auto texts = button_texts(scene);
    const auto boxes = button_boxes(scene);
    for (std::size_t i = 0; i < texts.size(); ++i) text(texts[i], boxes[i][0] + 10, kTop + 0x15f, int(i) == hover_button ? kButtonColour[i] : 4);
    // A refusal, 2 s, red, centred under the panel.
    if (now_ms < message_ms + 2000 && (message || !pad_message.empty())) {
        const std::string words = message ? string_id(scene, message) : pad_message;
        text(words, kWidth / 2 - text_font.measure(words) / 2, kTop + 399, 1);
    }
}

}  // namespace d2d::client
