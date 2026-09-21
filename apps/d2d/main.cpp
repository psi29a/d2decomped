// d2d — the game binary. Skeleton stage: no window yet, no engine loop —
// just an in-memory framebuffer that the devctl channel can dump to PNG.
// This exists so the harness (control channel + screenshot pipeline) is
// wired end-to-end before phase 5's real renderer lands. When SDL3 arrives,
// the framebuffer becomes an SDL_Texture and the main loop grows an event
// pump; the devctl surface stays the same.
//
// CLI:
//   --devctl <path>   bind AF_UNIX control socket
//   --data <dir>      MPQ directory (default: ~/Workspace/private/diablo2)
//   --headless        do not sleep between pumps; exit on `quit` or SIGINT
//
// Without --devctl, d2d runs one paint pass and exits. That mode is only
// useful once there's a real window; today it just proves the framebuffer
// build path compiles.

#include <mpq.hpp>
#include <dc6.hpp>
#include <devctl.hpp>
#include <screenshot.hpp>

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

// Draw a very rough grayscale-palette blit — every non-zero index becomes a
// gray value. Placeholder until we load a real PL2 palette.
void blit_sprite(std::vector<std::uint8_t>& fb,
                 const d2d::dc6::Frame& f,
                 std::uint32_t dst_x, std::uint32_t dst_y) {
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const auto dy = dst_y + y;
        if (dy >= kH) break;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const auto dx = dst_x + x;
            if (dx >= kW) break;
            const auto idx = f.pixels[y * f.width + x];
            if (idx == 0) continue;
            auto* p = &fb[(dy * kW + dx) * 4];
            p[0] = idx; p[1] = idx; p[2] = idx; p[3] = 0xFF;
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
    if (!fs::exists(d2data)) return;   // pattern-only mode
    try {
        d2d::mpq::Archive a(d2data);
        auto raw = a.read(R"(data\global\ui\MENU\helpwhitebullet.dc6)");
        d2d::dc6::Sprite spr(raw);
        // Tile the bullet across the top-left corner to prove decode.
        for (int j = 0; j < 12; ++j)
            for (int i = 0; i < 16; ++i)
                blit_sprite(fb, spr.frame(0, 0),
                            20 + i * 24, 20 + j * 24);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] paint: %s\n", e.what());
    }
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

    // Frame counter shared with the devctl handler so `info` can report it.
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

    if (!ch.active()) {
        // No socket → single-shot paint. Useful for `d2d --data ... ; open …`
        // once a real window is in the loop; today it's mostly a smoke test.
        std::printf("d2d: no control channel, single-shot paint. exiting.\n");
        return 0;
    }

    // Cooperative loop. ~30 Hz when interactive; tight when headless (still
    // sleeps 1ms to keep the box comfy — devctl is line-based and cheap).
    const auto tick_ms = headless ? 1 : 33;
    while (!quit) {
        ch.pump();
        ++frame_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(tick_ms));
    }
    ch.close();
    return 0;
}
