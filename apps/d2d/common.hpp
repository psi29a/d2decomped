// Shared includes, screen/iso constants, data-dir lookup, blit helpers.
#pragma once


#include <mpq.hpp>
#include <cof.hpp>
#include <compcode.hpp>
#include <dc6.hpp>
#include <dcc.hpp>
#include <devctl.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <outdoor_data.hpp>
#include <dt1.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <screenshot.hpp>
#include <tbl.hpp>
#include <txt.hpp>
#include <rules.hpp>
#include <monsters.hpp>
#include <combat.hpp>
#include <drops.hpp>
#include <userdir.hpp>

#include "log.hpp"

#include "npc_menu.hpp"
#include "npc_talk.hpp"
#include "speech_sound.hpp"
#include "video.hpp"
#include "obj_preset.hpp"

#include <SDL3/SDL.h>

#if __has_include(<AL/al.h>)
#  include <AL/al.h>
#  include <AL/alc.h>
#else
#  include <OpenAL/al.h>
#  include <OpenAL/alc.h>
#endif
#include <CLI/CLI.hpp>
#include <csignal>
#include <unordered_map>
#include <format>
#include <unordered_set>
#include <variant>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <fstream>
#include <functional>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

// Set to 1 by d2d_sigint_handler on SIGINT/SIGTERM; polled each frame.
// Declared at global scope because std::signal handlers must have C
// linkage. Defined further below.
extern volatile std::sig_atomic_t g_sigint_quit;

namespace {

// D2 LoD 800×600 mode dimensions — matches TitleScreen.DC6, which ships
// pre-sliced into a 4×3 grid of sub-frames adding up to exactly 800×600
// (columns 256/256/256/32, rows 256/256/88).
constexpr std::uint32_t kW = 800;
constexpr std::uint32_t kH = 600;

// D2 iso-diamond tile dimensions. Each cell footprint = 160x80; each
// step in x moves (+80, +40) on screen, each step in y moves (-80, +40).
// See OpenDiablo2's mapengine for the same convention.
constexpr int kIsoW = 160;
constexpr int kIsoH = 80;

// Dev overlay toggled by devctl `debug collision`: blocked subtiles in red.
static bool g_debug_collision = false;

fs::path default_data_dir(std::string_view cfg_data) {
    // Resolution order (first hit wins):
    //   1. --data CLI arg (handled in main; not here)
    //   2. $D2_MPQ_DIR env var — portable, matches the test-suite convention
    //   3. `data = …` in d2d.cfg (user / ./ / global dir, see main)
    //   4. macOS: the launcher's QSettings-persisted path
    //      (~/Library/Preferences/com.d2decomp.D2 Launcher.plist, key
    //      game.dataPath) — same lookup tools/ghidra/import.sh uses
    //   5. Eyeballed default: ~/Workspace/private/diablo2
    if (const char* env = std::getenv("D2_MPQ_DIR"); env && *env)
        return fs::path(env);
    if (!cfg_data.empty()) return fs::path(cfg_data);

#if defined(__APPLE__)
    if (FILE* p = ::popen(
            "defaults read 'com.d2decomp.D2 Launcher' game.dataPath 2>/dev/null",
            "r"); p) {
        char buf[1024];
        std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, p);
        ::pclose(p);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) --n;
        if (n > 0) {
            buf[n] = '\0';
            return fs::path(buf);
        }
    }
#endif

    const char* home = std::getenv("HOME");
    if (!home) return {};
    return fs::path(home) / "Workspace" / "private" / "diablo2";
}

// Palette-lookup blit: index 0 is transparent (skip), all other indices map
// through the supplied palette to real RGBA. Signed dest so negative offsets
// clip cleanly (logo frames have ox down to -180).
// `shade` scales the colour, 256 = as is (skill tree: greyed-out icons).
void blit_sprite(std::vector<std::uint8_t>& fb,
                 const d2d::dc6::Frame& f,
                 const d2d::palette::Palette& pal,
                 int dst_x, int dst_y, int shade = 256) {
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
            if (shade == 256) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
            else {
                p[0] = std::uint8_t(std::min(255, c.r * shade / 256));
                p[1] = std::uint8_t(std::min(255, c.g * shade / 256));
                p[2] = std::uint8_t(std::min(255, c.b * shade / 256));
            }
            p[3] = c.a;
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

// Additive blit — D2's fire assets are drawn with TRANS_ADDITIVE. When a
// PL2 is available we resolve through Blizzard's authored
// MaxComponentBlend[fg][bg] table, which returns the palette index
// Blizzard picked for that combination (palette-preserving, exact match
// against the original renderer). Falls back to plain RGB-clamp additive
// when the caller has only a plain Palette — used by the credits path
// which doesn't need PL2 fidelity.
//
// Reading back the framebuffer to a palette index would require an RGB->
// index reverse lookup we don't have; instead the PL2 path samples the
// UNMODIFIED-so-far fg color for the src and treats the current fb pixel
// as bg by finding its closest palette index. To keep it fast and avoid a
// KD-tree, we short-circuit the common case: when bg is black (index 0,
// i.e. the fb has not been written since the last clear), additive(fg, 0)
// == fg — so we just plot pal[fg]. Otherwise fall back to RGB-clamp add.
// The fire is drawn early in the frame over the char-create bg (which is
// dark/near-black in the campfire pit), so this covers > 95% of pixels.
void blit_additive(std::vector<std::uint8_t>& fb,
                   const d2d::dc6::Frame& f,
                   const d2d::palette::Palette& pal,
                   const d2d::palette::Pl2* pl2,
                   int anchor_x, int anchor_y) {
    const int dst_x = anchor_x + f.offset_x;
    const int dst_y = anchor_y + f.offset_y - int(f.height) + 1;
    for (std::uint32_t y = 0; y < f.height; ++y) {
        const int py = dst_y + int(y);
        if (py < 0 || py >= int(kH)) continue;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const int px = dst_x + int(x);
            if (px < 0 || px >= int(kW)) continue;
            const auto idx = f.pixels[y * f.width + x];
            if (idx == 0) continue;
            auto* p = &fb[(std::size_t(py) * kW + std::size_t(px)) * 4];
            if (pl2 && p[0] == 0 && p[1] == 0 && p[2] == 0) {
                // Fast path: black bg => additive result == fg palette entry.
                const auto& out = pl2->base_palette()[idx];
                p[0] = out.r; p[1] = out.g; p[2] = out.b;
            } else {
                const auto c = pal[idx];
                p[0] = std::uint8_t(std::min(255, int(p[0]) + int(c.r)));
                p[1] = std::uint8_t(std::min(255, int(p[1]) + int(c.g)));
                p[2] = std::uint8_t(std::min(255, int(p[2]) + int(c.b)));
            }
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

// D2's base game/anim tick is 25 Hz — every animation rate in AnimData.d2
// is `25 * animRate / 256`. The frontend menu runs at that base rate;
// tying our advance to wall-clock ms keeps playback correct regardless of
// how fast we happen to be rendering (60 Hz, 120 Hz, headless, whatever).
constexpr std::uint32_t kBaseFrameMs = 40;   // 1000 / 25

}  // namespace
