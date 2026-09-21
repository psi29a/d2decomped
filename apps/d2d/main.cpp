// d2d — the game binary. Phase-5 in progress.
//
// Now opens an SDL3 window and presents an in-memory framebuffer as a
// streaming texture. The dev control channel + screenshot pipeline still
// see that same framebuffer, so `screenshot /tmp/x.png` captures exactly
// what's on-screen. --headless skips the window and paints once (useful
// for CI / A/B PNG diffing without a display).
//
// CLI:
//   --devctl <path>   bind AF_UNIX control socket
//   --data <dir>      MPQ directory (default: ~/Workspace/private/diablo2)
//   --headless        no window; paint once, then serve devctl until quit
//                     (or exit immediately if --devctl also missing)

#include <mpq.hpp>
#include <dc6.hpp>
#include <devctl.hpp>
#include <palette.hpp>
#include <screenshot.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kW = 800;
constexpr std::uint32_t kH = 600;

fs::path default_data_dir() {
    // ponytail: match the launcher's default install path on this box. Wire a
    // proper QSettings/config lookup when a second contributor shows up.
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return fs::path(home) / "Workspace" / "private" / "diablo2";
}

// Palette-lookup blit: index 0 is transparent (skip), all other indices map
// through the supplied palette to real RGBA.
void blit_sprite(std::vector<std::uint8_t>& fb,
                 const d2d::dc6::Frame& f,
                 const d2d::palette::Palette& pal,
                 std::uint32_t dst_x, std::uint32_t dst_y) {
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const auto dy = dst_y + y;
        if (dy >= kH) break;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const auto dx = dst_x + x;
            if (dx >= kW) break;
            const auto idx = f.pixels[y * f.width + x];
            if (idx == 0) continue;
            const auto c = pal[idx];
            auto* p = &fb[(dy * kW + dx) * 4];
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
        }
    }
}

void paint_test_pattern(std::vector<std::uint8_t>& fb) {
    // Diagonal gradient — a visually distinctive canary when no MPQ loads.
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            auto* p = &fb[(y * kW + x) * 4];
            p[0] = std::uint8_t(x);
            p[1] = std::uint8_t(y);
            p[2] = std::uint8_t((x + y) / 2);
            p[3] = 0xFF;
        }
    }
}

void paint(std::vector<std::uint8_t>& fb, const fs::path& data_dir) {
    paint_test_pattern(fb);
    const auto d2data = data_dir / "d2data.mpq";
    if (!fs::exists(d2data)) return;
    try {
        d2d::mpq::Archive a(d2data);
        d2d::palette::Palette pal(a.read(
            R"(data\global\palette\ACT1\pal.dat)"));

        auto raw = a.read(R"(data\global\ui\MENU\helpwhitebullet.dc6)");
        d2d::dc6::Sprite spr(raw);
        for (int j = 0; j < 12; ++j)
            for (int i = 0; i < 16; ++i)
                blit_sprite(fb, spr.frame(0, 0), pal,
                            20 + i * 24, 20 + j * 24);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] paint: %s\n", e.what());
    }
}

// --- SDL3 render loop ------------------------------------------------------

// RAII holders — SDL_Init failure is the only thing we treat as fatal;
// everything else logs and returns nullptr so the caller can fall back to
// headless mode.
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

    bool open(int w_, int h_) {
        w = SDL_CreateWindow("d2d", w_, h_, 0);
        if (!w) { std::fprintf(stderr, "[d2d] SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
        r = SDL_CreateRenderer(w, nullptr);
        if (!r) { std::fprintf(stderr, "[d2d] SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
        // RGBA32 is defined as ABGR8888 on LE / RGBA8888 on BE — memory order
        // is always (r, g, b, a), matching our framebuffer.
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, w_, h_);
        if (!t) { std::fprintf(stderr, "[d2d] SDL_CreateTexture: %s\n", SDL_GetError()); return false; }
        return true;
    }
};

int run_windowed(const std::vector<std::uint8_t>& fb,
                 d2d::devctl::Channel& ch,
                 std::atomic<std::uint64_t>& frame_count,
                 std::atomic<bool>& quit) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[d2d] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Window win;
    if (!win.open(int(kW), int(kH))) {
        SDL_Quit();
        return 1;
    }

    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quit = true;
            else if (ev.type == SDL_EVENT_KEY_DOWN &&
                     ev.key.key == SDLK_ESCAPE)     quit = true;
        }

        if (ch.active()) ch.pump();

        SDL_UpdateTexture(win.t, nullptr, fb.data(), int(kW * 4));
        SDL_RenderClear(win.r);
        SDL_RenderTexture(win.r, win.t, nullptr, nullptr);
        SDL_RenderPresent(win.r);

        ++frame_count;
    }

    SDL_Quit();
    return 0;
}

int run_headless(d2d::devctl::Channel& ch,
                 std::atomic<std::uint64_t>& frame_count,
                 std::atomic<bool>& quit) {
    if (!ch.active()) {
        std::printf("d2d: --headless with no --devctl, single-shot paint. exiting.\n");
        return 0;
    }
    while (!quit) {
        ch.pump();
        ++frame_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string devctl_path;
    fs::path    data_dir = default_data_dir();
    bool        headless = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--devctl" && i + 1 < argc)      devctl_path = argv[++i];
        else if (a == "--data" && i + 1 < argc)   data_dir    = argv[++i];
        else if (a == "--headless")               headless    = true;
        else if (a == "--help" || a == "-h") {
            std::printf("usage: d2d [--devctl <path>] [--data <dir>] [--headless]\n");
            return 0;
        } else {
            std::fprintf(stderr, "d2d: unknown arg '%.*s'\n",
                         int(a.size()), a.data());
            return 2;
        }
    }

    std::vector<std::uint8_t> fb(std::size_t(kW) * kH * 4, 0);
    paint(fb, data_dir);

    std::atomic<std::uint64_t> frame_count{0};
    std::atomic<bool>          quit{false};

    d2d::devctl::Channel ch;
    ch.on("info", [&](const std::vector<std::string>&) {
        return "w=" + std::to_string(kW) + " h=" + std::to_string(kH)
             + " frame=" + std::to_string(frame_count.load()) + "\nok\n";
    });
    ch.on("screenshot", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err screenshot <path>\n");
        const auto n = d2d::screenshot::save_png(args[1], fb, kW, kH);
        return "ok " + std::to_string(n) + "\n";
    });
    ch.on("quit", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("ok\n");
    });
    ch.listen(devctl_path);

    return headless
        ? run_headless(ch, frame_count, quit)
        : run_windowed(fb, ch, frame_count, quit);
}
