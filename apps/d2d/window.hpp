// SPDX-License-Identifier: GPL-3.0-or-later
// SDL window/renderer and SDL event -> Mouse/text translation.
#pragma once

#include "common.hpp"
#include "hw_cursor.hpp"
#include "platform.hpp"
#include "ui.hpp"

#include <log.hpp>

#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !SDL_VERSION_ATLEAST(3, 4, 0)
#define SDL_SCALEMODE_PIXELART SDL_SCALEMODE_NEAREST   // 3.2 has no pixel-art mode: plain nearest is closest
#endif

namespace d2d::client {

// The upscale filter by name: d2d.cfg's `scalemode` (the launcher's
// dropdown) and --toggle scalemode=. Empty for any other name.
inline std::optional<SDL_ScaleMode> scale_mode_named(std::string_view name) {
    if (name == "pixelart") return SDL_SCALEMODE_PIXELART;
    if (name == "nearest") return SDL_SCALEMODE_NEAREST;
    if (name == "linear") return SDL_SCALEMODE_LINEAR;
    return std::nullopt;
}

// RAII holders — SDL_Init failure is the only thing we treat as fatal;
// everything else logs and returns false so the caller can bail out.

void set_icon(SDL_Window* window);   // assets/icon/d2d.bmp: title bar, taskbar, dock

struct Window {
    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture = nullptr;
    SDL_ScaleMode scale_mode = SDL_SCALEMODE_PIXELART;   // set before open()

    Window() = default;
    ~Window() {
        if (cursor) SDL_DestroyCursor(cursor);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // Game renders at the fixed w_ x h_ (D2 LoD's 800x600); the window is
    // `scale` times that and SDL's logical presentation does the zoom,
    // letterboxing any other window size. Same setup as thirdeye's
    // graphics.cpp — pair with SDL_ConvertEventToRenderCoordinates so
    // mouse events arrive in game pixels.
    // `fullscreen` is borderless at the desktop's own mode (SDL3's default
    // fullscreen; no display mode switch), the game letterboxed inside it.
    bool open(int width_, int height_, int scale, bool fullscreen, bool resizable = false, int window_width = 0) {
        // Hints have to be set BEFORE SDL_CreateWindow to take effect.
        // Disable the CGWindowServer "wants full-screen space" nag on
        // macOS — that dialog is what triggers user reports of the
        // window appearing to freeze right after launch. Also request
        // high-DPI so the renderer picks up the true screen scale.
        SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
        view_w = width_;
        window = SDL_CreateWindow("d2d", (window_width > 0 ? window_width : width_) * scale, height_ * scale,
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | (resizable ? SDL_WINDOW_RESIZABLE : 0));
        if (!window) { d2d::log::error("SDL_CreateWindow: {}", SDL_GetError()); return false; }
        set_icon(window);
        // The launcher quits as it starts us: take the foreground it leaves
        // (Windows' focus-stealing guard otherwise leaves us behind).
        SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
        SDL_RaiseWindow(window);
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) { d2d::log::error("SDL_CreateRenderer: {}", SDL_GetError()); return false; }
        // VSync avoids tearing AND caps our frame rate at the monitor
        // refresh — the primary yield mechanism. Failure is not fatal;
        // pace_frame() delays anyway as a floor. --fps-cap turns it off again:
        // that rate is the sleep's.
        SDL_SetRenderVSync(renderer, 1);
        SDL_HideCursor();   // d2d draws D2's gauntlet into the frame
        SDL_SetRenderLogicalPresentation(renderer, width_, height_, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        // RGBA32 is defined as ABGR8888 on LE / RGBA8888 on BE — memory order
        // is always (r, g, b, a), matching our framebuffer.
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, width_, height_);
        if (!texture) { d2d::log::error("SDL_CreateTexture: {}", SDL_GetError()); return false; }
        SDL_SetTextureScaleMode(texture, scale_mode);
        return true;
    }

    // The logical view's width (the height stays kScreenHeight): the
    // widescreen game view is wider than the 800x600 menus, so the texture
    // and the letterbox follow the screen shown.
    int view_w = int(kScreenWidth);
    bool set_view(int width_) {
        if (width_ == view_w) return true;
        SDL_Texture* wider = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width_, int(kScreenHeight));
        if (!wider) { d2d::log::error("SDL_CreateTexture: {}", SDL_GetError()); return false; }
        SDL_SetTextureScaleMode(wider, scale_mode);
        SDL_DestroyTexture(texture);
        texture = wider;
        SDL_SetRenderLogicalPresentation(renderer, width_, int(kScreenHeight), SDL_LOGICAL_PRESENTATION_LETTERBOX);
        view_w = width_;
        return true;
    }

    // The gauntlet as the OS cursor (hw_cursor.hpp). SDL takes the image in
    // window points; on a high-density display it also gets one in pixels.
    // False when SDL can't (the dummy driver, a compositor without colour
    // cursors): the caller draws it into the frame instead.
    SDL_Cursor* cursor = nullptr;
    CursorImage cursor_points;
    bool cursor_shown = false;
    bool show_cursor(const d2d::dc6::Frame& frame, const d2d::palette::Palette& pal) {
        SDL_FRect drawn{};
        if (!SDL_GetRenderLogicalPresentationRect(renderer, &drawn)) return false;
        const float density = SDL_GetWindowPixelDensity(window);
        const float pixels = drawn.h / float(kScreenHeight);
        if (!(pixels > 0) || !(density > 0)) return false;
        // Pixel art and nearest both take one source pixel. Linear filters.
        const auto cursor_scale = scale_mode == SDL_SCALEMODE_LINEAR ? CursorScaleMode::linear
                                : scale_mode == SDL_SCALEMODE_NEAREST ? CursorScaleMode::nearest
                                                                      : CursorScaleMode::pixelart;
        auto image = cursor_image(frame, pal, pixels / density, cursor_scale);
        if (!cursor || image != cursor_points) {
            const auto surface_of = [](CursorImage& made) {
                return SDL_CreateSurfaceFrom(made.width, made.height, SDL_PIXELFORMAT_RGBA32, made.rgba.data(), made.width * 4);
            };
            SDL_Surface* base = surface_of(image);
            if (!base) return false;
            auto dense = density > 1 ? cursor_image(frame, pal, pixels, cursor_scale) : CursorImage{};
            if (SDL_Surface* sharp = dense.width > 0 ? surface_of(dense) : nullptr) {
                SDL_AddSurfaceAlternateImage(base, sharp);
                SDL_DestroySurface(sharp);   // base holds its own reference
            }
            SDL_Cursor* made = SDL_CreateColorCursor(base, image.hot_x, image.hot_y);
            SDL_DestroySurface(base);
            if (!made) return false;
            SDL_SetCursor(made);
            if (cursor) SDL_DestroyCursor(cursor);
            cursor = made;
            cursor_points = std::move(image);
        }
        if (!cursor_shown) SDL_ShowCursor();
        cursor_shown = true;
        return true;
    }
    void hide_cursor() {
        if (cursor_shown) SDL_HideCursor();
        cursor_shown = false;
    }
    // Whether the OS pointer is at (x, y) in game pixels (from the centre
    // 800 x 600): false once the pad or devctl has moved d2d's mouse, which
    // leaves the OS pointer behind, so the hand is drawn where d2d has it.
    [[nodiscard]] bool pointer_at(int x, int y) const {
        float window_x = 0, window_y = 0, game_x = 0, game_y = 0;
        SDL_GetMouseState(&window_x, &window_y);
        if (!SDL_RenderCoordinatesFromWindow(renderer, window_x, window_y, &game_x, &game_y)) return false;
        game_x -= float(view_w - int(kScreenWidth)) / 2;
        return std::abs(game_x - float(x)) <= 1 && std::abs(game_y - float(y)) <= 1;
    }
};

// Turn SDL mouse + text events into a per-tick snapshot. Rising/falling
// edges are recomputed each tick from the raw button state. When the
// active screen has a text field, the caller flips SDL text input on/off.
void handle_sdl_events(SDL_Event& event, Mouse& mouse, Screen& current_screen,
                       std::string& text_input, bool& text_backspace,
                       std::vector<SDL_Keycode>& keys, std::atomic<bool>& quit);

}  // namespace d2d::client
