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
#include <font.hpp>
#include <palette.hpp>
#include <screenshot.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// D2 LoD 800×600 mode dimensions — matches TitleScreen.DC6, which ships
// pre-sliced into a 4×3 grid of sub-frames adding up to exactly 800×600
// (columns 256/256/256/32, rows 256/256/88).
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
// through the supplied palette to real RGBA. Signed dest so negative offsets
// clip cleanly (logo frames have ox down to -180).
void blit_sprite(std::vector<std::uint8_t>& fb,
                 const d2d::dc6::Frame& f,
                 const d2d::palette::Palette& pal,
                 int dst_x, int dst_y) {
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const int dy = dst_y + int(y);
        if (dy < 0 || dy >= int(kH)) continue;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const int dx = dst_x + int(x);
            if (dx < 0 || dx >= int(kW)) continue;
            const auto idx = f.pixels[y * f.width + x];
            if (idx == 0) continue;
            const auto c = pal[idx];
            auto* p = &fb[(std::size_t(dy) * kW + std::size_t(dx)) * 4];
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
        }
    }
}

// Blit a frame at anchor+(frame.offset_x, frame.offset_y - height + 1).
// D2 convention is BOTTOM-LEFT origin — matches DCC's frame-box math (see
// OpenDiablo2/dcc_direction_frame.go: `box.top = y_offset - height + 1`).
// The fire animation confirms this: its oy=132 is constant across frames of
// varying height, so anchoring the BOTTOM keeps the fire base planted while
// the flame top flickers up and down.
void blit_at_anchor(std::vector<std::uint8_t>& fb,
                    const d2d::dc6::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y) {
    const int x = anchor_x + f.offset_x;
    const int y = anchor_y + f.offset_y - int(f.height) + 1;
    blit_sprite(fb, f, pal, x, y);
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

// A DC6 background is often stored as a grid of sub-frames arranged
// left-to-right, top-to-bottom (D2 splits large images because DC6 frames
// each cap at 256×256 in practice). This lays them out side by side.
void blit_dc6_grid(std::vector<std::uint8_t>& fb,
                   const d2d::dc6::Sprite& spr,
                   const d2d::palette::Palette& pal,
                   int origin_x, int origin_y,
                   int tiles_across) {
    const auto per_dir = spr.frames_per_direction();
    int cy = origin_y;
    int cx = origin_x;
    int row_h = 0;
    for (int i = 0; i < int(per_dir); ++i) {
        const auto& f = spr.frame(0, i);
        blit_sprite(fb, f, pal, cx, cy);
        cx += int(f.width);
        if (int(f.height) > row_h) row_h = int(f.height);
        if ((i + 1) % tiles_across == 0) {
            cx  = origin_x;
            cy += row_h;
            row_h = 0;
        }
    }
}

// All assets the main-menu scene needs. Loaded once at startup; render()
// paints from these each tick without touching the MPQ again.
struct Scene {
    d2d::palette::Palette pal;
    d2d::dc6::Sprite      bg;
    d2d::dc6::Sprite      logo_bl, logo_br;   // "DIABLO II" silhouettes
    d2d::dc6::Sprite      logo_fl, logo_fr;   // fire filling for the logo
    d2d::dc6::Sprite      fire;               // campfire in the scene
    d2d::font::Font       font;
    int                   bg_tiles_across{4};
};

std::optional<Scene> load_scene(const fs::path& data_dir) {
    const auto d2data = data_dir / "d2data.mpq";
    if (!fs::exists(d2data)) return std::nullopt;
    try {
        d2d::mpq::Stack mpqs;
        const auto d2exp = data_dir / "d2exp.mpq";
        if (fs::exists(d2exp)) mpqs.push(d2exp);
        mpqs.push(d2data);

        // Prefer the LoD title asset (fenced rogue camp at night). Classic
        // TitleScreen is only 4×3 sub-frames; LoD is the same layout.
        auto title = mpqs.try_read(R"(data\global\ui\FrontEnd\gameselectscreenEXP.dc6)");
        if (!title) title = mpqs.try_read(R"(data\global\ui\FrontEnd\TitleScreen.DC6)");
        if (!title) throw std::runtime_error("no title screen asset");

        return Scene{
            .pal      = d2d::palette::Palette(mpqs.read(
                          R"(data\global\palette\Sky\pal.dat)")),
            .bg       = d2d::dc6::Sprite(*title),
            .logo_bl  = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackLeft.DC6)")),
            .logo_br  = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackRight.DC6)")),
            .logo_fl  = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireLeft.DC6)")),
            .logo_fr  = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireRight.DC6)")),
            .fire     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\fire.DC6)")),
            .font     = d2d::font::Font(
                          mpqs.read(R"(data\local\FONT\LATIN\font16.tbl)"),
                          d2d::dc6::Sprite(mpqs.read(R"(data\local\FONT\LATIN\font16.dc6)"))),
        };
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] load_scene: %s\n", e.what());
        return std::nullopt;
    }
}

void render(std::vector<std::uint8_t>& fb,
            const Scene& s,
            std::uint64_t tick) {
    // Full-screen background — no need to clear; the 4×3 grid tiles fill
    // exactly 800×600 with no gaps.
    blit_dc6_grid(fb, s.bg, s.pal, 0, 0, s.bg_tiles_across);

    // "DIABLO II" logo — anchor picks where the logo's BOTTOM sits (see
    // blit_at_anchor). With BlackLeft's oy=47 and height=122, anchor_y=170
    // puts the logo top at ~95px, matching reference. Black silhouettes
    // first, then the fire fills on top. Same 30-frame flicker cycle
    // across all four pieces — advance one frame every 3 ticks (~20 fps).
    const auto n_logo = s.logo_bl.frames_per_direction();
    const auto fi = (n_logo == 0) ? 0u : std::uint32_t((tick / 3) % n_logo);
    constexpr int kLogoAnchorX = 400;
    constexpr int kLogoAnchorY = 170;
    blit_at_anchor(fb, s.logo_bl.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_at_anchor(fb, s.logo_br.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_at_anchor(fb, s.logo_fl.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_at_anchor(fb, s.logo_fr.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);

    // Campfire animation. Fire's oy=132 is constant, height flickers 89..176,
    // so anchoring the base keeps the fire planted on the pit while flames
    // dance upward. Anchor at (400, 460) puts the base near screen-bottom-
    // centre where the pit is in the LoD scene. Different tick divisor
    // desyncs the fire flicker from the logo flicker.
    const auto n_fire = s.fire.frames_per_direction();
    const auto ff = (n_fire == 0) ? 0u : std::uint32_t((tick / 4) % n_fire);
    constexpr int kFireAnchorX = 400;
    constexpr int kFireAnchorY = 460;
    blit_at_anchor(fb, s.fire.frame(0, ff), s.pal, kFireAnchorX, kFireAnchorY);

    // Menu labels — no button chrome yet, just text over the background.
    struct MenuItem { const char* text; int y; };
    constexpr MenuItem items[] = {
        {"SINGLE PLAYER",     380},
        {"OTHER MULTIPLAYER", 425},
        {"EXIT DIABLO II",    475},
    };
    for (const auto& mi : items) {
        const int w = s.font.measure(mi.text);
        s.font.draw(fb, kW, kH, s.pal, int(kW) / 2 - w / 2, mi.y, mi.text);
    }
    s.font.draw(fb, kW, kH, s.pal, 8, int(kH) - 14, "d2d dev build");
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

int run_windowed(std::vector<std::uint8_t>& fb,
                 const std::optional<Scene>& scene,
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

        const auto tick = frame_count.load();
        if (scene) render(fb, *scene, tick);
        else       paint_test_pattern(fb);

        SDL_UpdateTexture(win.t, nullptr, fb.data(), int(kW * 4));
        SDL_RenderClear(win.r);
        SDL_RenderTexture(win.r, win.t, nullptr, nullptr);
        SDL_RenderPresent(win.r);

        ++frame_count;
    }

    SDL_Quit();
    return 0;
}

int run_headless(std::vector<std::uint8_t>& fb,
                 const std::optional<Scene>& scene,
                 d2d::devctl::Channel& ch,
                 std::atomic<std::uint64_t>& frame_count,
                 std::atomic<bool>& quit) {
    if (!ch.active()) {
        std::printf("d2d: --headless with no --devctl, single-shot paint. exiting.\n");
        return 0;
    }
    while (!quit) {
        ch.pump();
        const auto tick = frame_count.load();
        if (scene) render(fb, *scene, tick);
        else       paint_test_pattern(fb);
        ++frame_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 Hz
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
    for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
    auto scene = load_scene(data_dir);   // nullopt if MPQ dir is missing

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
        ? run_headless(fb, scene, ch, frame_count, quit)
        : run_windowed(fb, scene, ch, frame_count, quit);
}
