// SPDX-License-Identifier: GPL-3.0-or-later
// The game menu Esc opens (UI 9) and the HUD's mini-panel (UI 0x15) with
// its button (docs/research/re/menu.md).
#pragma once

#include "audio.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace d2d::client {

// The text images the menus name (UI\ENG\<name>.dc6), for the loader.
std::vector<std::string_view> game_menu_images();

// The menus (FUN_0047e3d0 draws, the input table at 0x6d6034 drives):
// 0 main, 1 options, 2 sound, 3 video, 4 automap. Opening one selects its
// last item (FUN_0047e090).
// ponytail: video and automap
// options are kept, not applied; 3D sound, EAX, 3D bias and perspective
// are off as on a machine without a 3D provider or Glide.
struct GameMenu {
    enum Action { kNone, kClose, kExit, kControls };
    bool open = false;
    bool expansion = true;                 // the video / automap menus' LoD rows (FUN_00408f20)
    int menu = 0, sel = 0;                 // 0x7bc93c's menu, 0x7bc938
    bool held = false, drag = false;       // the left button / a slider (0x7bc948 / 0x7bc94c)
    int last_x = -1, last_y = -1;          // where the mouse was (0x7bc980 / 0x7bc984)
    int pent = 0;                          // pentspin's frame (0x7bc944)
    std::uint32_t pent_ms = 0;
    std::array<std::array<int, 8>, 5> value{};   // each row's value, by its LoD row
    bool restore_automap = false, restore_mini = false;   // reopened after it (0x713058 b = 1)
    bool volume_changed = false;           // the sound sliders moved: the caller keeps them

    GameMenu();
    void show(int which);
    // One frame's keys (not Esc) and mouse; what the menu asks for.
    Action input(const Scene& scene, Audio& audio, const Mouse& mouse, const std::vector<SDL_Keycode>& keys, std::uint32_t now_ms);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene) const;

    // The rows on screen (classic skips the LoD ones) and their tops.
    [[nodiscard]] std::vector<int> rows() const;
    [[nodiscard]] int top() const;
    [[nodiscard]] int hit(int mouse_y) const;      // FUN_0047d520
    [[nodiscard]] bool enabled(int row) const;
private:
    Action activate(const Scene& scene, Audio& audio);                  // FUN_0047d5c0
    void slide(const Scene& scene, Audio& audio, int mouse_x, int mouse_y);   // FUN_0047d670
    void changed(Audio& audio, int row);           // a row's select function
};

// The mini-panel and the HUD's menu button (FUN_0047f710 / FUN_004977c0).
// Its buttons' actions, as FUN_0047ec50 numbers them.
// ponytail: no tooltips (FUN_0047f490); messages (5) does nothing.
struct MiniPanel {
    enum Button { kCharacter = 0, kInventory = 1, kSkills = 2, kAutomap = 4, kMessages = 5, kQuests = 6, kMenu = 7 };
    bool open = true;                      // FUN_004567f0 opens it with the game
    std::array<bool, 7> down{};            // a button held
    bool button_down = false;              // the HUD's menu button held (0x7befd0)
    // A press / release on it (left / right: panels open on that side);
    // true if it took the click; `action` the button let go on, else -1.
    bool input(const Scene& scene, Audio& audio, const Mouse& mouse, bool left, bool right, int& action);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, bool left, bool right, int mouse_x, int mouse_y) const;
};

}  // namespace d2d::client
