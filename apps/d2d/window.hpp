// SDL window/renderer and SDL event -> Mouse/text translation.
#pragma once

#include "audio.hpp"

namespace d2d::client {


// RAII holders — SDL_Init failure is the only thing we treat as fatal;
// everything else logs and returns false so the caller can bail out.

struct Window {
    SDL_Window*   w = nullptr;
    SDL_Renderer* r = nullptr;
    SDL_Texture*  t = nullptr;

    Window() = default;
    ~Window() {
        if (t) SDL_DestroyTexture(t);
        if (r) SDL_DestroyRenderer(r);
        if (w) SDL_DestroyWindow(w);
    }
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // Game renders at the fixed w_ x h_ (D2 LoD's 800x600); the window is
    // `scale` times that and SDL's logical presentation does the zoom,
    // letterboxing any other window size. Same setup as thirdeye's
    // graphics.cpp — pair with SDL_ConvertEventToRenderCoordinates so
    // mouse events arrive in game pixels.
    bool open(int w_, int h_, int scale) {
        // Hints have to be set BEFORE SDL_CreateWindow to take effect.
        // Disable the CGWindowServer "wants full-screen space" nag on
        // macOS — that dialog is what triggers user reports of the
        // window appearing to freeze right after launch. Also request
        // high-DPI so the renderer picks up the true screen scale.
        SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
        w = SDL_CreateWindow("d2d", w_ * scale, h_ * scale, SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!w) { d2d::log::error("SDL_CreateWindow: {}", SDL_GetError()); return false; }
        r = SDL_CreateRenderer(w, nullptr);
        if (!r) { d2d::log::error("SDL_CreateRenderer: {}", SDL_GetError()); return false; }
        // VSync avoids tearing AND caps our frame rate at the monitor
        // refresh — the primary yield mechanism. Failure is not fatal;
        // pace_frame() delays anyway as a floor.
        SDL_SetRenderVSync(r, 1);
        SDL_HideCursor();   // d2d draws D2's gauntlet into the frame
        SDL_SetRenderLogicalPresentation(r, w_, h_, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        // RGBA32 is defined as ABGR8888 on LE / RGBA8888 on BE — memory order
        // is always (r, g, b, a), matching our framebuffer.
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, w_, h_);
        if (!t) { d2d::log::error("SDL_CreateTexture: {}", SDL_GetError()); return false; }
        // Linear filter on upscale, as thirdeye does.
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return true;
    }
};

// Turn SDL mouse + text events into a per-tick snapshot. Rising/falling
// edges are recomputed each tick from the raw button state. When the
// active screen has a text field, the caller flips SDL text input on/off.
void handle_sdl_events(SDL_Event& ev, Mouse& m, Screen& current_screen,
                       std::string& text_input, bool& text_backspace,
                       std::vector<SDL_Keycode>& keys, std::atomic<bool>& quit);

}  // namespace d2d::client
