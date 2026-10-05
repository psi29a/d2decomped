// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for window.hpp: SDL events.
#include "window.hpp"

#include "platform.hpp"
#include "ui.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace d2d::client {

void handle_sdl_events(SDL_Event& event, Mouse& mouse, Screen& current_screen,
                       std::string& text_input, bool& text_backspace,
                       std::vector<SDL_Keycode>& keys, std::atomic<bool>& quit) {
    // SDL_EVENT_QUIT fires on app-level termination (Cmd-Q, all windows
    // closed). WINDOW_CLOSE_REQUESTED fires when a specific window's ✕
    // is clicked — SDL3 does NOT auto-promote it to QUIT. Both mean
    // "user wants out" for us since we're single-window.
    if (event.type == SDL_EVENT_QUIT ||
        event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        quit = true; return;
    }
    // Cmd-Q backup — some window managers eat the SDL_EVENT_QUIT.
    if (event.type == SDL_EVENT_KEY_DOWN &&
        event.key.key == SDLK_Q && (event.key.mod & SDL_KMOD_GUI)) {
        quit = true; return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_ESCAPE) {
            // Esc pops one layer up:
            //   Title       -> quit
            //   CharCreate  -> CharSelect  (the flow you came from)
            //   InGame      -> closes an open panel, else CharSelect (the
            //                  roster; matches D2) — handled in-game
            //   everything else -> Title
            switch (current_screen) {
                case Screen::Title:      quit = true; break;
                case Screen::CharCreate: current_screen = Screen::CharSelect; break;
                case Screen::InGame:     keys.push_back(event.key.key); break;
                default:                 current_screen = Screen::Title; break;
            }
        } else if (event.key.key == SDLK_BACKSPACE) {
            text_backspace = true;
            if (current_screen == Screen::InGame) keys.push_back(event.key.key);   // typing chat or a trade's gold
        } else {
            keys.push_back(event.key.key);   // per-screen key handling
        }
    } else if (event.type == SDL_EVENT_TEXT_INPUT) {
        // ev.text.text is UTF-8; keep the printable Latin-1 subset.
        for (const char* cursor = event.text.text; *cursor; ++cursor) {
            const auto letter = static_cast<unsigned char>(*cursor);
            if (letter >= 0x20 && letter <= 0x7e) text_input.push_back(char(letter));
        }
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mouse.x = int(event.motion.x);
        mouse.y = int(event.motion.y);
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        mouse.x = int(event.button.x);
        mouse.y = int(event.button.y);
        if (event.button.button == SDL_BUTTON_LEFT) {
            mouse.down = true;
            mouse.press_this_frame = true;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            mouse.rpress_this_frame = true;
        }
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        mouse.wheel += event.wheel.integer_y;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        mouse.x = int(event.button.x);
        mouse.y = int(event.button.y);
        if (event.button.button == SDL_BUTTON_LEFT) {
            mouse.down = false;
            mouse.release_this_frame = true;
        }
    }
}

}  // namespace d2d::client
