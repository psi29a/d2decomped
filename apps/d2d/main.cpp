// SPDX-License-Identifier: GPL-3.0-or-later
// d2d — the game binary. Phase-5 in progress.
//
// Now opens an SDL3 window and presents an in-memory framebuffer as a
// streaming texture. The dev control channel + screenshot pipeline still
// see that same framebuffer, so `screenshot /tmp/x.png` captures exactly
// what's on-screen. --headless runs the same loop on SDL's dummy video
// driver (no window) so CI / scripts can drive it over devctl.
//
// CLI:
//   --devctl <path>   bind AF_UNIX control socket
//   --data <dir>      MPQ directory (else $D2_MPQ_DIR, a d2data.mpq beside
//                     d2d or in ./, then `data =` in d2d.cfg; else it stops)
//   --headless        no window (SDL dummy driver); needs --devctl
//   --scale <n>       window = 800x600 * n, SDL zooms (also `scale` in d2d.cfg)
//   --seed <n>        map seed: act 1's layout and the Blood Moor

#include "audio/audio.hpp"
#include "build_info.hpp"
#include "common.hpp"
#include "devctl/verbs.hpp"
#include "frontend/frontend.hpp"
#include "frontend/lobby.hpp"
#include "frontend/roster.hpp"
#include "frontend/title_rules.hpp"
#include "frontend/video.hpp"
#include "input/cursor_anim.hpp"
#include "input/gamepad.hpp"
#include "input/gamepad_map.hpp"
#include "input/hw_cursor.hpp"
#include "input/keyconfig.hpp"
#include "input/pointer_events.hpp"
#include "input/touch/input.hpp"
#include "items/cursor.hpp"
#include "load.hpp"
#include "panels/controls.hpp"
#include "platform.hpp"
#include "render/world_view.hpp"
#include "scene.hpp"
#include "session.hpp"
#include "ui.hpp"
#include "watchdog.hpp"
#include "window.hpp"

#include <character_name.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <d2s_write.hpp>
#include <devctl.hpp>
#include <install.hpp>
#include <join.hpp>
#include <lan.hpp>
#include <log.hpp>
#include <log_file.hpp>
#include <mpq.hpp>
#include <palette.hpp>
#include <screenshot.hpp>
#ifdef D2D_WITH_SENTRY
#include <sentry.h>
#endif
#include <sound_ids.hpp>
#include <switches.hpp>
#include <tcp.hpp>
#include <userdir.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#ifdef D2D_WITH_SENTRY
#include <condition_variable>
#endif
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <map>
#ifdef D2D_WITH_SENTRY
#include <mutex>
#endif
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __vita__
#include <SDL3/SDL_main.h>   // SDL's main runs ours on a 512 KB stack
#endif
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>   // SDL's activity calls ours as SDL_main
#include <pthread.h>
#endif

using namespace d2d::client;

#ifdef __vita__
// newlib's heap: most of the extended-memory app mode (ATTRIBUTE2=12,
// apps/d2d/CMakeLists.txt), where D2Vita sees ~355 MiB free at boot.
// ponytail: a guess, not measured on a Vita; lower it if SDL's GXM runs short.
extern "C" {
unsigned int _newlib_heap_size_user = 300 * 1024 * 1024;
}
#endif

namespace {

// The frame as it goes to the screen (and a screenshot): through the display ramp, DirectDraw's on the
// whole picture (FUN_006be190, Palette::ramp_rgba). The frame itself keeps the palette's colours, so
// a blend through the PL2 tables finds them (palette::Blender).
const std::vector<std::uint8_t>& shown_frame(const std::vector<std::uint8_t>& framebuffer) {
    static std::vector<std::uint8_t> shown;
    shown = framebuffer;
    d2d::palette::Palette::ramp_rgba(shown);
    return shown;
}

#ifdef D2D_WITH_SENTRY
std::string cpu_model() {
#if defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))
    int registers[4]{};
    __cpuid(registers, 0x80000000);
    if (static_cast<unsigned>(registers[0]) < 0x80000004) return {};
    std::array<int, 12> brand{};
    for (int i = 0; i < 3; ++i) __cpuid(brand.data() + i * 4, 0x80000002 + i);
    std::string model(reinterpret_cast<const char*>(brand.data()), sizeof(brand));
    if (const auto nul = model.find('\0'); nul != std::string::npos) model.resize(nul);
    while (!model.empty() && model.back() == ' ') model.pop_back();
    return model;
#elif defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        const auto split = line.find(':');
        if (split == std::string::npos) continue;
        const auto key = line.substr(0, split);
        if (key != "model name" && key != "Hardware") continue;
        const auto first = line.find_first_not_of(" \t", split + 1);
        return first == std::string::npos ? std::string{} : line.substr(first);
    }
    return {};
#elif defined(__APPLE__)
    std::size_t size = 0;
    if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0) == 0 && size > 1) {
        std::string model(size, '\0');
        if (sysctlbyname("machdep.cpu.brand_string", model.data(), &size, nullptr, 0) == 0) {
            model.resize(std::strlen(model.c_str()));
            return model;
        }
    }
    return {};
#else
    return {};
#endif
}

constexpr const char* cpu_architecture() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "aarch64";
#elif defined(_M_ARM) || defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
}
#endif   // D2D_WITH_SENTRY

std::string crash_notes_from_environment() {
#ifdef _WIN32
    const wchar_t* notes = _wgetenv(L"D2D_CRASH_NOTES");
    if (!notes || !*notes) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, notes, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::vector<char> buffer(static_cast<std::size_t>(length));
    if (WideCharToMultiByte(CP_UTF8, 0, notes, -1, buffer.data(), length, nullptr, nullptr) != length)
        return {};
    return std::string(buffer.data(), static_cast<std::size_t>(length - 1));
#else
    const char* notes = std::getenv("D2D_CRASH_NOTES");
    return notes ? notes : "";
#endif
}

#ifdef D2D_WITH_SENTRY
void sentry_log_sink(d2d::log::Level level, std::string_view line) {
    const std::string message(line);
    auto breadcrumb = sentry_value_new_breadcrumb("default", message.c_str());
    sentry_value_set_by_key(
        breadcrumb, "level",
        sentry_value_new_string(level == d2d::log::Level::error ? "error" : "warning"));
    sentry_add_breadcrumb(breadcrumb);
}

bool start_sentry(const fs::path& user_dir, std::string_view release,
                  std::string_view notes) {
    const char* dsn = std::getenv("SENTRY_DSN");
    if (!dsn || !*dsn) {
        d2d::log::warn("Crash reporting was requested, but SENTRY_DSN is not configured.");
        return false;
    }
    auto* options = sentry_options_new();
    if (!options) {
        d2d::log::warn("Could not allocate Sentry options; crash reporting is unavailable.");
        return false;
    }
    sentry_options_set_dsn(options, dsn);
    sentry_options_set_release(options, std::string(release).c_str());
    sentry_options_set_auto_session_tracking(options, 0);
    const auto database = user_dir / "sentry";
    const char* raw_base_path = SDL_GetBasePath();
    const fs::path base_path = raw_base_path ? fs::path(raw_base_path) : fs::path{};   // SDL owns it (SDL3): not ours to free
#ifdef _WIN32
    const auto database_path = database.wstring();
    sentry_options_set_database_pathw(options, database_path.c_str());
    const auto handler_path = (base_path / "crashpad_handler.exe").wstring();
    sentry_options_set_handler_pathw(options, handler_path.c_str());
#else
    const auto database_path = database.string();
    sentry_options_set_database_path(options, database_path.c_str());
    const auto handler_path = (base_path / "crashpad_handler").string();
    sentry_options_set_handler_path(options, handler_path.c_str());
#endif
    sentry_options_set_debug(options, 0);
    if (sentry_init(options) != 0) {
        d2d::log::warn("Sentry initialization failed; crash reporting is unavailable.");
        return false;
    }

    auto device = sentry_value_new_object();
    const auto model = cpu_model();
    if (!model.empty())
        sentry_value_set_by_key(device, "cpu_model", sentry_value_new_string(model.c_str()));
    sentry_value_set_by_key(device, "cpu_architecture", sentry_value_new_string(cpu_architecture()));
    sentry_value_set_by_key(device, "logical_cpu_count", sentry_value_new_int32(SDL_GetNumLogicalCPUCores()));
    sentry_value_set_by_key(device, "memory_mb", sentry_value_new_int32(SDL_GetSystemRAM()));
    sentry_value_set_by_key(device, "operating_system", sentry_value_new_string(SDL_GetPlatform()));
    sentry_set_context("machine", device);
    if (!notes.empty())
        sentry_set_extra("user_notes", sentry_value_new_string_n(notes.data(), notes.size()));
    d2d::log::set_sink(sentry_log_sink);
    d2d::log::info("Crash reporting enabled.");
    return true;
}

class SentrySession {
public:
    SentrySession(bool requested, const fs::path& user_dir, std::string_view release,
                  std::string_view notes)
        : active_(requested && start_sentry(user_dir, release, notes)) {}

    ~SentrySession() {
        if (active_) {
            d2d::log::set_sink(nullptr);
            sentry_close();
        }
    }

    [[nodiscard]] bool enabled() const { return active_; }

private:
    bool active_;
};

class SentryFrameAttachment {
public:
    SentryFrameAttachment()
        : active_(sentry_is_enabled()),
          available_frame_(active_ ? std::size_t(kScreenWidth) * kScreenHeight * 4 : 0) {
        if (active_)
            worker_ = std::thread([this] { process_frames(); });
    }

    ~SentryFrameAttachment() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_one();
        if (worker_.joinable())
            worker_.join();
        if (!sentry_uuid_is_nil(&attachment_))
            sentry_remove_attachment(attachment_);
    }

    void update(std::span<const std::uint8_t> framebuffer) {
        if (!active_)
            return;
        if (framebuffer.empty() || framebuffer.size() % (std::size_t(kScreenHeight) * 4) != 0) {
            d2d::log::warn("Skipping crash-frame capture due to unexpected framebuffer size.");
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_capture_ < std::chrono::seconds(1))
            return;

        std::lock_guard lock(mutex_);
        if (stopping_ || !frame_available_)
            return;
        last_capture_ = now;
        available_frame_.assign(framebuffer.begin(), framebuffer.end());
        frame_available_ = false;
        frame_pending_ = true;
        ready_.notify_one();
    }

private:
    void process_frames() {
        for (;;) {
            std::vector<std::uint8_t> frame;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || frame_pending_; });
                if (stopping_ && !frame_pending_)
                    return;
                frame.swap(available_frame_);
                frame_pending_ = false;
            }

            try {
                const auto png = d2d::screenshot::encode_png(
                    frame, std::uint32_t(frame.size() / 4 / kScreenHeight), kScreenHeight);
                auto attachment = sentry_attachment_from_bytes(
                    reinterpret_cast<const char*>(png.data()), png.size(),
                    "last-game-frame.png");
                if (sentry_value_is_null(attachment)) {
                    d2d::log::warn("Could not prepare the latest game frame for Sentry.");
                } else {
                    sentry_attachment_set_content_type(attachment, "image/png");
                    const auto new_attachment = sentry_add_attachment(attachment);
                    if (sentry_uuid_is_nil(&new_attachment)) {
                        d2d::log::warn("Could not attach the latest game frame to Sentry.");
                    } else {
                        if (!sentry_uuid_is_nil(&attachment_))
                            sentry_remove_attachment(attachment_);
                        attachment_ = new_attachment;
                    }
                }
            } catch (const std::exception& exception) {
                d2d::log::warn("Could not encode crash-frame attachment: {}", exception.what());
            }

            {
                std::lock_guard lock(mutex_);
                available_frame_.swap(frame);
                frame_available_ = true;
            }
        }
    }

    bool active_{};
    std::vector<std::uint8_t> available_frame_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool frame_available_{true};
    bool frame_pending_{};
    bool stopping_{};
    std::chrono::steady_clock::time_point last_capture_{};
    sentry_uuid_t attachment_{};
    std::thread worker_;
};
#else   // -DD2D_GAME_ONLY=ON builds without Sentry (apps/d2d/CMakeLists.txt)
class SentrySession {
public:
    SentrySession(bool requested, const fs::path&, std::string_view, std::string_view) {
        if (requested) d2d::log::warn("This d2d was built without crash reporting.");
    }
    [[nodiscard]] bool enabled() const { return false; }
};

struct SentryFrameAttachment {
    void update(std::span<const std::uint8_t>) {}
};
#endif   // D2D_WITH_SENTRY

// Set by main() before entering the loop — a lazy way to plumb --start-*
// through without threading extra parameters everywhere.
static std::string g_start_screen;
static bool        g_video = true;          // startup cinematics (--no-video / cfg video = 0)
static fs::path    g_user_dir;
static pad::Config g_pad_config;          // d2d.cfg pad_* (gamepad_map.hpp load_config)
static std::array<int, 6> g_automap_options{ 0, 0, 1, 1, 1, 0 };   // d2d.cfg automap_mode/fade/center/party/names/left (game.exe's registry)
static bool        g_pad_controls = true;
static bool        g_dismiss_effects = false;   // host opt-in for effect dismissal
static bool        g_click_keys = true;   // Configure Controls' Attack / Use and Right-hand Skill keys (deviations.md); off: --toggle click_keys=off
static keys::Clicks g_click_bindings = keys::kNoClicks;   // d2d.cfg click_attack / click_skill
static bool        g_coop_lobby = true;   // the title's "SINGLE PLAYER / COOP" and character select's co-op lobby; off: --toggle coop_lobby=off // Configure Controls' gamepad page (deviations.md); off: --toggle pad_controls=off
static int         g_master_volume = 100, g_music_volume = 50;   // d2d.cfg master_volume / music_volume (the game menu sets them)
static int         g_gamma = 155, g_contrast = 100;   // d2d.cfg gamma / contrast (DirectDraw FUN_006be190; 100 is the flat ramp)
static int         g_start_class = -1;   // none: char-create opens with no class picked
static std::string g_start_name;
static bool        g_start_hardcore = false;
static bool        g_start_classic = false;
static bool        g_no_save = false;         // --no-save: nothing is written (scripted tests)
static int         g_start_cam_x = -1;   // -1 = "use map center"
static int         g_start_cam_y = -1;
// A game's map seed (FUN_0052c280): a fixed one if given, else the save's
// own when it was last played on this difficulty (single player,
// FUN_0056a090), else a new one — the random one this run started with.
// A saved 0 means none here: d2d's early saves hold 0 (deviations.md
// improvement 2; game.exe takes it as the seed).
static std::string   g_join_host;           // the game.exe TCP/IP host char-select joins: --join, or TCP/IP's JOIN GAME
static std::string   g_cli_join;            // --join's own, what SINGLE PLAYER goes back to
static std::string   g_lan_connect;        // --connect: char-select's OK joins this d2d host (lan-host.md)
static int           g_lan_host_port = 0;   // --host: d2d players join this game over the LAN (lan-host.md); 0 off
static std::string   g_cli_lan_connect;    // --connect's and --host's own, what SINGLE PLAYER goes back to
static int           g_cli_lan_host_port = 0;
static std::string   g_last_tcp_ip;         // d2d.cfg last_tcp_ip: the join box's start (game.exe's LastTcpIp)
static fs::path      g_game_exe;            // --game-exe: its tables (the codec's, read at runtime)
static bool          g_seed_fixed = false;  // --seed
static std::uint32_t g_map_seed = 0;
static std::uint32_t game_seed(const d2d::d2s::Header& header) {
    if (!g_seed_fixed && header.difficulty[std::size_t(header.active_difficulty())] & 0x80 && header.map_id) return header.map_id;
    return g_map_seed;
}
static int         g_scale = 1;          // window = game res * g_scale
static SDL_ScaleMode g_scale_mode = SDL_SCALEMODE_PIXELART;   // d2d.cfg scalemode, --toggle scalemode=
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
static bool        g_touch_controls = true;   // phones and tablets start on touch; d2d.cfg touch_controls = 0 turns it off
#else
static bool        g_touch_controls = false;
#endif
static bool        g_fullscreen = false; // d2d.cfg fullscreen = 1 (Alt+Enter flips and saves it)
static bool        g_widescreen = false; // d2d.cfg widescreen = 1 / --widescreen: the game view widens to the window
static int         g_forced_view_w = 0;  // --view-width: the widescreen width, else the window's aspect

// What a failed join shows (tcpip-menu.md, "Join errors"): game.exe's
// "Cannot Connect to Server" (0x145b) when the host doesn't answer, its
// message for the host's B4 reason (table 0x70f384, FUN_0044cb60; an
// expansion character's 0x14 / 0x15 name Baal), else d2d's own words.
static std::string join_error_text(const Scene& scene, const NetGame::JoinError& error, bool expansion) {
    static constexpr std::array<std::uint16_t, 29> kRefused = {
        0x14f5, 0x14f6, 0x14f7, 0x14f8, 0x14f9, 0x14fb, 0x14fa, 0x14ed, 0x14ee, 0x14fc, 0x14fd, 0x14fe, 0x14ff, 0x1500, 0x1501,
        0x1502, 0x1503, 0x1504, 0x1505, 0x14f0, 0x14f4, 0x14f3, 0x14f2, 0x14f1, 0x14ef, 0x2775, 0x2776, 0x14fa, 0x14fb };
    std::uint16_t id = 0;
    if (error.no_connect) id = 0x145b;
    else if (error.refused >= 0) {
        const auto reason = std::size_t(error.refused > 0x1c ? 9 : error.refused);
        id = expansion && reason == 0x14 ? 0x5522 : expansion && reason == 0x15 ? 0x5521 : kRefused[reason];
    }
    const auto found = id ? lookup_string(scene, id) : std::nullopt;
    return found ? u16_to_latin1(*found) : error.what;
}

static Screen parse_screen(std::string_view text) {
    if (text == "credits")    return Screen::Credits;
    if (text == "charselect") return Screen::CharSelect;
    if (text == "charcreate") return Screen::CharCreate;
    if (text == "ingame")     return Screen::InGame;
    if (text == "video")      return Screen::Video;
    if (text == "cinematics") return Screen::Cinematics;
    if (text == "othermultiplayer") return Screen::OtherMultiplayer;
    if (text == "tcpip")      return Screen::TcpIp;
    if (text == "endgame")    return Screen::Endgame;
    return Screen::Title;
}

// Frame pacer — sleeps whatever's left of the frame's budget after render.
// Matches D2's own pattern (FUN_004f6190 in game.exe): compute time budget
// remaining, sleep if we're ahead. Without this, an unlucky vsync miss or a
// windowed compositor that skips vsync sends us into a 100% CPU spin.
// The budget follows the window's display, a little under its refresh so
// vsync, not the sleep, holds each frame to the display; 60 frames a second
// where no refresh is known (headless). --fps-cap (d2d.cfg fps_cap) sets an
// exact rate instead, slept precisely. The World still ticks at 25 Hz; the
// frames between draw the units sliding (unit_slide.hpp).
static int g_fps_cap = 0;   // frames a second, 0 = the display's refresh
struct FrameBudget { std::uint64_t nanos; bool exact; };
inline FrameBudget frame_budget(SDL_Window* window) {
    if (g_fps_cap > 0) return { 1'000'000'000ull / std::uint64_t(g_fps_cap), true };
    const SDL_DisplayMode* mode = window ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)) : nullptr;
    if (!mode || mode->refresh_rate < 30.f) return { 1'000'000'000ull / 60, false };
    return { std::uint64_t(1e9 / (double(mode->refresh_rate) * 1.05)), false };
}
inline void pace_frame(std::uint64_t frame_start_ns, FrameBudget budget) {
    const std::uint64_t elapsed = SDL_GetTicksNS() - frame_start_ns;
    if (elapsed < budget.nanos) {
        // SDL_DelayPrecise spins its last millisecond: only for a set cap.
        if (budget.exact) SDL_DelayPrecise(budget.nanos - elapsed);
        else SDL_DelayNS(budget.nanos - elapsed);
    } else {
        // Even when we blew the budget, yield a little so we don't
        // monopolise the scheduler.
        SDL_DelayNS(250'000);
    }
}

// Map the window safe area into the centre 800-wide touch band (logical px).
// Widescreen spare columns live outside that band; touch input and draw_touch
// stay 800-wide, so insets are only within [0, 800].
void apply_safe_inset(const Window& win) {
    touch::viewport_width = float(std::max(win.view_w, int(kScreenWidth)));
#if SDL_VERSION_ATLEAST(3, 2, 0)   // SDL_GetWindowSafeArea and SDL_RenderCoordinatesFromWindow: since 3.2.0
    SDL_Rect area{};
    float left = 0, top = 0, right = 0, bottom = 0;
    // Safe area is window points. RenderCoordinatesFromWindow lands in the
    // logical view, same as the touch path. Pairing those points with
    // SizeInPixels clamped a false inset and slid corner HUD inward.
    if (!win.window || !win.renderer || !SDL_GetWindowSafeArea(win.window, &area)
        || !SDL_RenderCoordinatesFromWindow(win.renderer, float(area.x), float(area.y), &left, &top)
        || !SDL_RenderCoordinatesFromWindow(win.renderer, float(area.x + area.w), float(area.y + area.h), &right, &bottom)
        || win.view_w <= 0) {
        touch::safe_inset = {};
        return;
    }
    const float max_horizontal = std::max(0.f, (touch::viewport_width - 400.f) * .5f);
    touch::safe_inset.left = std::clamp(left, 0.f, max_horizontal);
    touch::safe_inset.right = std::clamp(touch::viewport_width - right, 0.f, max_horizontal);
    const auto clamp_v = [](float value) { return std::clamp(value, 0.f, 100.f); };
    touch::safe_inset.top = clamp_v(std::max(0.f, top));
    touch::safe_inset.bottom = clamp_v(std::max(0.f, float(kScreenHeight) - bottom));
#else
    (void)win;   // the 3.1 previews: no safe area, the touch layout stays put
#endif
}

/**
 * Run the interactive window loop, routing SDL and controller input to the current screen. Touch
 * gestures and character-selection cursor ownership are resolved before gameplay and UI updates.
 */
int run_windowed(std::vector<std::uint8_t>& framebuffer,
                 std::optional<Scene>& scene,
                 std::future<void>& loading,
                 const fs::path& save_dir,
                 d2d::devctl::Channel& channel,
                 std::atomic<std::uint64_t>& frame_count,
                 std::atomic<bool>& quit) {
    // A Steam Deck has no keyboard: SDL_StartTextInput opens Steam's own
    // (steam://open/keyboard), which SDL does on X11 only, and only once told
    // to: SDL 3.2 by "SteamDeck", 3.4 by SDL_ENABLE_STEAM_SCREEN_KEYBOARD (named
    // by string: 3.2's headers lack it). The Deck's buttons count as a keyboard,
    // so "auto" wouldn't show it. The same variables in the env still win.
    if (std::getenv("SteamDeck") || std::getenv("SteamGamepadUI")) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
        SDL_SetHint("SteamDeck", "1");
        SDL_SetHint("SDL_ENABLE_STEAM_SCREEN_KEYBOARD", "1");
        SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        d2d::log::error("SDL_Init: {}", SDL_GetError());
        return 1;
    }
    // A pad is optional: without gamepad support d2d plays with keyboard and mouse as before.
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) d2d::log::warn("gamepad support unavailable: {}", SDL_GetError());
    const int sdl_v = SDL_GetVersion();
    d2d::log::info("Initializing SDL... done! SDL {}.{}.{} ({})", SDL_VERSIONNUM_MAJOR(sdl_v),
                   SDL_VERSIONNUM_MINOR(sdl_v), SDL_VERSIONNUM_MICRO(sdl_v), SDL_GetCurrentVideoDriver());
    Window win;
    win.scale_mode = g_scale_mode;
    if (!win.open(int(kScreenWidth), int(kScreenHeight), g_scale, g_fullscreen, g_widescreen, g_widescreen ? 1066 : 0)) { SDL_Quit(); return 1; }
    apply_safe_inset(win);
    // A set cap is the sleep's alone: a present held to the next refresh after it would miss the rate.
    if (g_fps_cap > 0) SDL_SetRenderVSync(win.renderer, 0);
    SentryFrameAttachment crash_frame;
#ifdef D2D_WITH_SENTRY
    if (sentry_is_enabled()) {
        auto graphics = sentry_value_new_object();
        sentry_value_set_by_key(graphics, "renderer",
                                sentry_value_new_string(SDL_GetRendererName(win.renderer)));
        const auto properties = SDL_GetRendererProperties(win.renderer);
        if (auto* gpu = static_cast<SDL_GPUDevice*>(
                SDL_GetPointerProperty(properties, SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr))) {
            if (const char* driver = SDL_GetGPUDeviceDriver(gpu))
                sentry_value_set_by_key(graphics, "gpu_driver",
                                        sentry_value_new_string(driver));
#if SDL_VERSION_ATLEAST(3, 4, 0)
            const auto gpu_properties = SDL_GetGPUDeviceProperties(gpu);
            if (const char* name = SDL_GetStringProperty(
                    gpu_properties, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
                name && *name)
                sentry_value_set_by_key(graphics, "gpu_model",
                                        sentry_value_new_string(name));
#endif
        }
        sentry_set_context("graphics", graphics);
    }
#endif
    d2d::log::info("  Window: {}x{} (scale {}), renderer {}", kScreenWidth * g_scale, kScreenHeight * g_scale, g_scale,
                   SDL_GetRendererName(win.renderer));
    // The title shows while finish_scene loads the rest (only the menus'
    // part of the scene is read here); then the menus take input. Only a quit
    // is taken off SDL's queue meanwhile: a click waits there for the menus.
    // (Black when the startup videos come first, or another screen was asked for.)
    const bool title_first = (g_start_screen.empty() && !g_video) || g_start_screen == "title";
    const auto boot_title = scene && title_first ? std::optional<TitleUI>(title_ui(*scene, g_coop_lobby)) : std::nullopt;
    while (loading.valid() && loading.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        SDL_PumpEvents();
        SDL_Event event;
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0) quit = true;
        if (boot_title) render_title(framebuffer, *scene, boot_title->buttons, std::uint32_t(SDL_GetTicks()), boot_title->version);
        SDL_UpdateTexture(win.texture, nullptr, shown_frame(framebuffer).data(), int(kScreenWidth * 4));
        SDL_RenderClear(win.renderer);
        SDL_RenderTexture(win.renderer, win.texture, nullptr, nullptr);
        SDL_RenderPresent(win.renderer);
        crash_frame.update(framebuffer);
        if (++frame_count == 1) d2d::log::info("First frame presented at {} ms after launch.", d2d::log::ms());
        SDL_Delay(10);
    }
    if (loading.valid()) loading.get();
    Audio audio;
    audio.init();
    audio.master_volume = g_master_volume;
    audio.music_volume = g_music_volume;
    audio.apply_volume();
    if (scene)
        g_on_button_press = [&] { audio.play_file(audio.ui_sounds, *scene, R"(data\global\sfx\cursor\button.wav)"); };
    struct ClearHook { ~ClearHook() { g_on_button_press = nullptr; } } clear_hook;   // audio dies with this scope

    Screen screen = g_start_screen.empty() ? Screen::Title
                                            : parse_screen(g_start_screen);
    // Startup cinematics, as FUN_00435230 plays them: the Blizzard and
    // Blizzard North logos, then the D2 intro if it hasn't been seen, else
    // (LoD) the expansion intro if that hasn't. game.exe keeps "seen" in
    // the registry; we keep it in <user dir>/cinematics_seen. The files are
    // 640x480 (logos) and 640x292 (intros, letterboxed); shown at 800x600
    // like D2 does in its 640x480 video mode, scaled up.
    d2d::mpq::Stack video_mpqs;
    std::vector<std::string> video_queue;
    if (scene) {
        // One that won't open (cut short, unreadable) goes without its videos.
        for (const char* name : { "d2xvideo.mpq", "d2video.mpq" })
            if (const auto path = d2d::install::find_file(scene->data_dir, name)) {
                try { video_mpqs.push(*path); }
                catch (const std::exception& error) { d2d::log::warn("{}; its videos are skipped", error.what()); }
            }
    }
    if (scene && !video_mpqs.empty() && g_video && (g_start_screen.empty() || g_start_screen == "video")) {
        video_queue = { R"(Data\Local\Video\New_BLIZ640x480.bik)", R"(Data\Local\Video\BlizNorth640x480.bik)" };
        std::string seen;
        if (std::ifstream file(g_user_dir / "cinematics_seen"); file) std::getline(file, seen, '\0');
        const char* intro = R"(data\local\video\ENG\d2intro640x292.bik)";
        const char* xintro = R"(data\local\video\ENG\D2x_Intro_640x292.bik)";
        std::string mark;
        if (seen.find("d2intro") == std::string::npos && video_mpqs.contains(intro)) {
            video_queue.push_back(intro); mark = "d2intro";
        } else if (seen.find("d2xintro") == std::string::npos && video_mpqs.contains(xintro)) {
            video_queue.push_back(xintro); mark = "d2xintro";
        }
        if (!mark.empty()) {
            std::error_code error;
            fs::create_directories(g_user_dir, error);
            std::ofstream(g_user_dir / "cinematics_seen", std::ios::app) << mark << '\n';
        }
        screen = Screen::Video;
    }
    if (scene)
        d2d::log::info("Initializing Video... {} video MPQ(s), {} startup cinematic(s)",
                       video_mpqs.size(), video_queue.size());
    d2d::video::Player video;
    bool video_playing = false;
    std::uint32_t video_start = 0;
    Screen video_return = Screen::Title;        // where the video screen goes when done
    EndgameUI endgame;
    auto read_seen = [&] {
        std::string seen;
        if (std::ifstream file(g_user_dir / "cinematics_seen"); file) std::getline(file, seen, '\0');
        return seen;
    };
    CinematicsUI cin_ui = scene ? cinematics_ui(*scene, cinematics_unlocked(read_seen())) : CinematicsUI{};
    Screen last_screen = screen;
    Mouse  mouse;
    CursorAnim cursor_anim;                       // the hand's animation (FUN_00468310)
    std::pair<int, int> cursor_seen{ -1, -1 };     // the mouse at the last draw: a move there is FUN_00468840's
    Gamepads gamepads;
    pad::Mapper pad_mapper{ g_pad_config };
    std::array<bool, std::size_t(pad::Button::Count)> pad_capture_held{};   // last frame's buttons, for Configure Controls' pad edit
    gamepads.open_connected();
    // Local co-op (docs/design/local-coop.md): a game begun with a pad other
    // than the Deck's own controls in is co-op. Pads take seats; seat 0's
    // plays the first player, each other one a player of its own.
    pad::Seats seats;
    bool coop_game = false, was_in_game = false;
    int last_ui_seat = 0;
    std::map<SDL_JoystickID, bool> start_held;    // each pad's Start last frame, for an unseated pad's rejoin
    std::uint32_t pad_last_ms = std::uint32_t(SDL_GetTicks());
    int pad_focus_key = 0;
    bool roster_navigation = false;
    bool pad_in_use = false, pad_was_field = false, pad_cursor_hidden = false;   // the pad was the last to act; its field mode
    TitleUI title = scene ? title_ui(*scene, g_coop_lobby) : TitleUI{};
    Lobby lobby;                                  // local co-op's lobby on character select
    TitleUI other_multiplayer = scene ? other_multiplayer_ui(*scene) : TitleUI{};
    TcpIpUI tcpip;

    // Char-create UI. Positions from RE'd master-table records; labels
    // from string.tbl by ID (0x13ed = EXIT, 0x13ee = OK per record +0x18).
    // OK/EXIT bottom-row buttons are RE'd as records 0x70ade0 and 0x70ae10
    // — the last two entries of the char-select master table, shared
    // with char-create by convention (see char-create-table.md).
    CharCreateUI character;
    if (scene) {
        auto tbl_label = [&](std::uint16_t id, const char* fallback) {
            if (auto found = lookup_string(*scene, id)) return u16_to_latin1(*found);
            return std::string(fallback);
        };
        character.cancel_label   = tbl_label(0x13ed, "EXIT");
        character.ok_label       = tbl_label(0x13ee, "OK");
        character.hardcore_label = tbl_label(0x1406, "Hardcore");
        character.expansion_label = tbl_label(0x58cb, "Expansion Character");
        character.cancel_btn = Button{ 33, rec_top(572, 35), 128, 35, character.cancel_label.c_str(),
                                &scene->medium_sel_button,
                                Screen::CharSelect, /*do_switch=*/true };
        // OK's target is InGame; do_switch flips true per tick once a class
        // is picked AND a name is entered (see the per-frame gate below).
        character.ok_btn     = Button{ 627, rec_top(572, 35), 128, 35, character.ok_label.c_str(),
                                &scene->medium_sel_button,
                                Screen::InGame, /*do_switch=*/false };
        // Preload class/name if --start-screen ingame was given.
        if (g_start_class >= 0 && g_start_class < 7) { character.selected = g_start_class; character.character_class = kUiToSaveClass[g_start_class]; }
        if (!g_start_name.empty()) character.name = g_start_name;
        character.hardcore = g_start_hardcore;
        if (g_start_classic) character.expansion = false;
    }

    // Char-select UI, from the LoD init (FUN_0043ae30): records 0xa4..0xa6
    // are the tall buttons CREATE NEW / CONVERT TO / DELETE at x 33/233/433,
    // each with a second line set by FUN_00500bf0 (0x5524 "CHARACTER",
    // 0x58ca "EXPANSION"); 0xa2/0xa3 are OK/EXIT (0x13ee / 0x13ed).
    CharSelectUI csu;
    // LoD init (FUN_0043ae30) starts the selection at 0: first character
    // preselected, OK live straight away.
    if (scene && !scene->saves.empty()) csu.selected = 0;
    // --start-name picks that save (scripts join as the right character; the list is last-played first).
    if (scene && !g_start_name.empty())
        for (std::size_t index = 0; index < scene->saves.size(); ++index)
            if (scene->saves[index].name == g_start_name) {
                csu.selected = int(index);
                csu.scroll = std::max(0, int(index) / 2 * 2 - (kSlots - 2));
            }
    if (scene) {
        auto tbl_label = [&](std::uint16_t id, const char* fallback) {
            if (auto found = lookup_string(*scene, id)) return u16_to_latin1(*found);
            return std::string(fallback);
        };
        csu.create_label   = tbl_label(0x2a50, "CREATE NEW");
        csu.create_label2  = tbl_label(0x5524, "CHARACTER");
        csu.convert_label  = tbl_label(0x58cc, "CONVERT TO");
        csu.convert_label2 = tbl_label(0x58ca, "EXPANSION");
        csu.delete_label   = tbl_label(0x1498, "DELETE");
        csu.delete_label2  = tbl_label(0x5524, "CHARACTER");
        csu.cancel_label = tbl_label(0x13ed, "EXIT");
        csu.ok_label     = tbl_label(0x13ee, "OK");
        csu.create_btn = Button{ 33, rec_top(528, 60), 168, 60, csu.create_label.c_str(),
                                 &scene->tall_button,
                                 Screen::CharCreate, /*do_switch=*/true };
        csu.create_btn.label2 = csu.create_label2.c_str();
        csu.convert_btn = Button{ 233, rec_top(528, 60), 168, 60, csu.convert_label.c_str(),
                                  &scene->tall_button,
                                  Screen::CharSelect, /*do_switch=*/false };
        csu.convert_btn.label2 = csu.convert_label2.c_str();
        csu.delete_btn = Button{ 433, rec_top(528, 60), 168, 60, csu.delete_label.c_str(),
                                 &scene->tall_button,
                                 Screen::CharSelect, /*do_switch=*/false };
        csu.delete_btn.label2 = csu.delete_label2.c_str();
        csu.cancel_btn = Button{ 33, rec_top(572, 35), 128, 35, csu.cancel_label.c_str(),
                                 &scene->medium_button,
                                 Screen::Title, /*do_switch=*/true };
        csu.ok_btn     = Button{ 627, rec_top(572, 35), 128, 35, csu.ok_label.c_str(),
                                 &scene->medium_button,
                                 Screen::InGame, /*do_switch=*/false };
        csu.popup_ok   = Button{ 351, rec_top(337, 32), 96, 32, nullptr, &scene->cancel_button };
        csu.delete_text = tbl_label(0x142b, "Are you sure that you want to delete this character?");
        csu.no_label    = tbl_label(0x142f, "NO");
        csu.yes_label   = tbl_label(0x142e, "YES");
        csu.popup_no    = Button{ 281, rec_top(337, 32), 96, 32, nullptr, &scene->cancel_button };
        csu.popup_yes   = Button{ 421, rec_top(337, 32), 96, 32, nullptr, &scene->cancel_button };
        csu.convert_text = { tbl_label(0x58ce, "Warning:  Once you convert a character to expansion, you cannot play it in original Diablo II games."), tbl_label(0x58cf, ""),
                             tbl_label(0x58d0, "Are you sure you wish to continue?") };
        csu.difficulty_heading = tbl_label(0x2723, "SELECT DIFFICULTY");
        for (int index = 0; index < 3; ++index) {
            static constexpr int kBottom[3] = { 297, 340, 383 };
            csu.difficulty_label[std::size_t(index)] = tbl_label(std::uint16_t(0x2722 - index), std::array{ "NORMAL", "NIGHTMARE", "HELL" }[std::size_t(index)]);
            csu.difficulty_btn[std::size_t(index)] = Button{ 264, rec_top(kBottom[index], 35), 272, 35, nullptr, &scene->btn_wide };
        }
    }

    const auto start_ticks = SDL_GetTicks();
    // SDL text input only while CharCreate's name field is up. While it's
    // on, macOS routes every key through the input method (IMK); leaving
    // it on everywhere cost ~100 ms inside SDL_PollEvent on Esc in InGame
    // (logged with "error messaging the mach port for
    // IMKCFRunLoopWakeUpReliable"). It was left on permanently as a
    // beachball suspect; that beachball was the pan-left float loop.
    bool text_active = false;
    ClientSession session(scene ? &*scene : nullptr, character, g_start_cam_x, g_start_cam_y);
    session.set_pad_config(g_pad_config);
    session.touch_enabled = g_touch_controls;
    if (g_touch_controls) session.last_device = touch::LastDevice::Touch;
    if (g_lan_host_port > 0) {
        if (auto listening = d2d::net::lan::Host::listen(std::uint16_t(g_lan_host_port))) {
            session.lan_host.emplace(std::move(*listening));
            d2d::log::info("lan: hosting on port {} ({})", g_lan_host_port, d2d::net::own_address());
        } else d2d::log::warn("lan: can't host on port {}: {}", g_lan_host_port, listening.error());
    }
    // The left button is down while a source holds it: the physical button or an Attack / Use key.
    const auto attack_key_held = [&] { return std::ranges::any_of(session.click_keys[keys::kClickAttack], key_held); };
    // A pad's seat; a new seat's player picks their character (ClientSession::coop_seat's roster).
    const auto take_seat = [&](const Gamepads::Pad& pad) {
        if (const int seat = seats.connect(pad.key, int(pad.id)); seat > 0) session.coop_seat(seat, seats.colour(seat), gamepads.sample(pad.id));
    };
    // The characters' saves (character_store.hpp): the World writes through
    // it when the player leaves the game or quits; the roster is read again.
    const CharacterStore characters{ save_dir, scene && scene->item_tables ? &*scene->item_tables : nullptr };
    session.world.characters = g_no_save ? nullptr : &characters;
    session.world.autoloot_gold = g_autoloot;
    session.world.dismiss_effects = g_dismiss_effects;
    if (!g_no_save) {
        session.cfg_file = g_user_dir / "d2d.cfg";
        d2d::userdir::Config cfg;
        d2d::userdir::load_cfg(session.cfg_file, cfg);
        session.hireling_hint = cfg["popup_hireling"] != "1";   // FUN_004939b0 reads the registry's PopupHireling each game
    }
    if (g_click_keys) session.click_keys = g_click_bindings;
    if (!g_no_save) session.key_dir = save_dir;                 // <name>.key and default.key beside the saves, as game.exe keeps them
    session.pad_controls_page = g_pad_controls;
    session.click_keys_on = g_click_keys;
    session.shot_dir = g_user_dir / "screenshots";
    for (int option = 0; option < 5; ++option) session.game_menu.value[4][std::size_t(option) + 1] = g_automap_options[std::size_t(option)];
    session.automap.left = g_automap_options[5] != 0;
    // A joined game: the host keeps the character and sends its save back
    // (B3) now and then and on leaving; each whole one goes to disk
    // (FUN_0045c520 writes <save dir><name>.d2s).
    auto write_save_back = [&] {
        if (!session.net_game) return;
        auto bytes = session.net_game->session.take_save_back();
        if (!bytes || g_no_save) return;
        const auto& name = session.net_game->session.name();
        if (auto error = characters.save_bytes(name, std::as_bytes(std::span(*bytes))); !error.empty())
            d2d::log::warn("net: {}'s save from the host not written: {}", name, error);
        else d2d::log::info("net: wrote {}'s save from the host ({} bytes)", name, bytes->size());
    };
    if (screen == Screen::InGame && scene) {
        // --start-screen ingame: a new character of the class and name given,
        // made as char-create's OK makes one (unsaved until the game saves).
        static_cast<Character&>(character) = made_character(*scene, kUiToSaveClass[std::size_t(std::max(character.selected, 0))],
                                                            character.name.empty() ? std::string("Tester") : character.name, character.hardcore, character.expansion, session.rng);
        set_map_seed(*scene, game_seed(character.header), character.header.active_difficulty());
        session.enter();
    }
    std::array<bool, 8> frontend_played{};   // title-screen ambience picks
    bool frontend_restart = true;             // the playlist starts at track 0 (frontend_track)
    std::uint32_t last_ms = 0;

    // Watchdog — writes to stderr the second the main thread stops
    // updating its heartbeat. Atomic reads only; no SDL calls (would
    // crash — SDL is main-thread only on macOS).
    std::atomic<std::uint32_t> heartbeat_ms{std::uint32_t(SDL_GetTicks())};
    std::atomic<std::uint32_t> current_phase{std::uint32_t(MainPhase::Idle)};
    g_current_phase_ptr = &current_phase;
    std::atomic<bool> watchdog_stop{false};
    std::thread watchdog([&] {
        std::uint32_t last_reported = 0;
        while (!watchdog_stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const auto now = std::uint32_t(SDL_GetTicks());
            const auto beat = heartbeat_ms.load(std::memory_order_relaxed);
            const auto since = now - beat;
            if (since >= 1000 && (now - last_reported) >= 1000) {
                const auto phase = current_phase.load(std::memory_order_relaxed);
                d2d::log::error("MAIN STUCK: {} ms in phase='{}' (frame not advancing)",
                                since, main_phase_name(phase));
                last_reported = now;
            }
        }
    });

    // Input, debug and state verbs for scripted tests (devctl/verbs.hpp).
    register_game_verbs(channel, win, screen, csu, character, session, scene, audio);

    while (!quit) {
        // Signal-driven quit — Ctrl-C / SIGTERM. The atomic write from
        // d2d_sigint_handler is polled here; SDL_EVENT_QUIT and window
        // close still work through handle_sdl_events.
        if (g_sigint_quit) { quit = true; break; }
        const std::uint64_t frame_start_ns = SDL_GetTicksNS();
        const std::uint32_t frame_start_ms = std::uint32_t(frame_start_ns / 1'000'000);
        // Toggled inside the `input` timing window, so any IME cost of the
        // switch itself shows up there.
        if (const bool want = screen == Screen::CharCreate || (screen == Screen::InGame && (session.chat_typing || session.coop_typing() || session.gold_box))
                               || (screen == Screen::CharSelect && lobby.typing())
                           || (screen == Screen::TcpIp && tcpip.box_open); want != text_active) {
            if (want) {
                // The field being typed in, so Steam's keyboard opens clear of it.
                const int side = screen == Screen::InGame ? (win.view_w - int(kScreenWidth)) / 2 : 0;
                const SDL_Rect field = screen == Screen::CharCreate ? SDL_Rect{ 319, rec_top(519, 26), 169, 26 }
                                     : screen == Screen::TcpIp      ? SDL_Rect{ 300, rec_top(268, 20), 200, 20 }
                                     : session.gold_box                 ? SDL_Rect{ 258 + side, 212, 100, 16 }        // the gold box's amount
                                     : SDL_Rect{ 0x7f + side, int(kScreenHeight) - 0x67, int(kScreenWidth) - 0xff, 0x2f };   // the chat line
                float left = 0, top = 0, right = 0, bottom = 0;
                SDL_RenderCoordinatesToWindow(win.renderer, float(field.x), float(field.y), &left, &top);
                SDL_RenderCoordinatesToWindow(win.renderer, float(field.x + field.w), float(field.y + field.h), &right, &bottom);
                const SDL_Rect area{ int(left), int(top), int(right - left), int(bottom - top) };
                SDL_SetTextInputArea(win.window, &area, 0);
                SDL_StartTextInput(win.window);
            }
            else      SDL_StopTextInput(win.window);
            text_active = want;
        }
        heartbeat_ms.store(frame_start_ms, std::memory_order_relaxed);

        mouse.press_this_frame = false;
        mouse.release_this_frame = false;
        mouse.rpress_this_frame = false;
        mouse.rrelease_this_frame = false;
        mouse.wheel = 0;
        std::string text_this_frame;
        bool        backspace_this_frame = false;
        std::vector<SDL_Keycode> keys_this_frame;
        current_phase.store(std::uint32_t(MainPhase::PollEvents),
                            std::memory_order_relaxed);
        bool mouse_took_over = false;   // the mouse moved or clicked this frame: it ends the pad's field mode
        session.touch_input.frame();
        SDL_Event event;
        // A press ends the frame's poll before the next pointer event (pointer_events.cpp, #545).
        const auto press_waits = [&mouse] {
            SDL_Event next;
            return SDL_PeepEvents(&next, 1, SDL_PEEKEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1 && waits_for_next_frame(mouse, next.type);
        };
        while (!press_waits() && SDL_PollEvent(&event)) {
            // SDL3 doesn't rescale event coords under logical presentation;
            // convert so the mouse lands in 800x600 game pixels.
            SDL_ConvertEventToRenderCoordinates(win.renderer, &event);
            // Widescreen: mouse x counts from the centre 800x600, where the UI is.
            const float view_off = float((win.view_w - int(kScreenWidth)) / 2);
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                event.motion.x -= view_off;
                if (event.motion.xrel != 0 || event.motion.yrel != 0) roster_navigation = false;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) event.button.x -= view_off;
            else if (event.type == SDL_EVENT_MOUSE_WHEEL) event.wheel.mouse_x -= view_off;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && screen == Screen::InGame) { session.pad.teleport.clear(); session.loot_chooser.clear(); session.touch_effect.cancel(); session.touch_get_hold.clear(); session.cancel_touch_item(); }
            if (session.touch_enabled && screen == Screen::InGame) {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    session.touch_cancel_skill_chase(); session.touch_stop_skill_chase(false);
                    session.touch_teleport.clear(); session.touch_skill_hold.clear();
                    session.touch_input.clear(); session.touch_item_finger = -1; session.touch_fingers = 0;
                    session.touch_pointer = {}; session.touch_sheet = {}; session.touch_item_drag = false;
                }
                if ((event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
                    ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.which == SDL_TOUCH_MOUSEID)) continue;
                // Sidecar/Pencil may arrive as a single pointer rather than SDL fingers.
                // While touch is last, keep those packets as owner -2 so a leftover mouse
                // packet cannot hand the seat to keyboard/mouse.
                const bool touch_last = session.last_device == touch::LastDevice::Touch;
                const bool pointer_held = session.touch_input.owns(-2);
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if ((down && touch_last) || (!down && (touch_last || pointer_held))) {
                        const auto phase = down ? touch::Phase::Down : touch::Phase::Up;
                        if (session.touch_event(-2, phase, touch::to_viewport_x(event.button.x), event.button.y)) continue;
                    }
                }
                if ((touch_last || pointer_held) && event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_LMASK))
                    if (session.touch_event(-2, touch::Phase::Move, touch::to_viewport_x(event.motion.x), event.motion.y)) continue;
                if (event.type == SDL_EVENT_FINGER_DOWN || event.type == SDL_EVENT_FINGER_UP ||
                    event.type == SDL_EVENT_FINGER_MOTION || event.type == SDL_EVENT_FINGER_CANCELED) {
                    session.last_device = touch::LastDevice::Touch;
                    const auto phase = event.type == SDL_EVENT_FINGER_DOWN ? touch::Phase::Down : event.type == SDL_EVENT_FINGER_UP ? touch::Phase::Up
                                     : event.type == SDL_EVENT_FINGER_MOTION ? touch::Phase::Move : touch::Phase::Cancel;
                    const float touch_x = event.tfinger.x, touch_y = event.tfinger.y;
                    if (session.touch_event(std::int64_t(event.tfinger.fingerID), phase, touch_x, touch_y)) continue;
                    mouse.x = int(touch::to_legacy_x(touch_x)); mouse.y = int(touch_y);
                    if (phase == touch::Phase::Down) { mouse.down = true; mouse.press_this_frame = true; }
                    if (phase == touch::Phase::Up) { mouse.down = false; mouse.release_this_frame = true; }
                    if (phase == touch::Phase::Cancel) mouse.down = false;
                    continue;
                }
            }
            gamepads.handle_event(event);
            const bool touch_mouse = (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID)
                || ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.which == SDL_TOUCH_MOUSEID);
            const bool touch_busy = session.touch_enabled && (session.touch_fingers > 0 || session.touch_pointer.active || session.touch_sheet.open);
            if ((event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                && !touch_mouse && !touch_busy) {
                pad_in_use = false; mouse_took_over = true;
                session.last_device = touch::LastDevice::MouseKeyboard;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                pad_in_use = false;
                session.last_device = touch::LastDevice::MouseKeyboard;
            }
            // The click keys (deviations.md improvement 15): a key or mouse button bound to
            // Attack / Use clicks the left button (held while it's held), Right-hand Skill
            // the right; not while typing or in Configure Controls, where it's a key to bind.
            if (bool down = false; screen == Screen::InGame && session.click_keys_on) {
                if (const int click = click_event(event, session.click_keys, down); click >= 0) {
                    const bool live = !text_active && !session.controls.open;
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) { mouse.x = int(event.button.x); mouse.y = int(event.button.y); }
                    if (click == keys::kClickAttack && !down && mouse.down && !attack_key_held()
                        && !(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) { mouse.down = false; mouse.release_this_frame = true; }
                    if (live && down && !(event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)) {
                        if (click == keys::kClickAttack) { mouse.down = true; mouse.press_this_frame = true; }
                        else mouse.rpress_this_frame = mouse.rdown = true;
                    }
                    if (live || !down) continue;
                }
            }
            const bool keep_down = screen == Screen::InGame && session.click_keys_on && event.type == SDL_EVENT_MOUSE_BUTTON_UP
                                && event.button.button == SDL_BUTTON_LEFT && attack_key_held();
            handle_sdl_events(event, mouse, screen, text_this_frame,
                              backspace_this_frame, keys_this_frame, quit);
            if (keep_down) { mouse.down = true; mouse.release_this_frame = false; }   // an Attack / Use key still holds it
        }
        // The right button is held while a source holds it: the physical button or a Right-hand Skill key.
        if (mouse.rdown && !(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK) && !(session.click_keys_on && std::ranges::any_of(session.click_keys[keys::kClickSkill], key_held)))
            mouse.rdown = false;
        // A partner's roster name and the keyboard's Enter (Steam's keyboard
        // on a Deck): their character is made, and the Enter is theirs, not
        // player 1's (character select's Enter plays, the game's opens chat).
        if (const bool partner_typing = scene && ((screen == Screen::CharSelect && lobby.typing()) || (screen == Screen::InGame && session.coop_typing()));
            partner_typing && std::erase_if(keys_this_frame, [](SDL_Keycode key) { return key == SDLK_RETURN || key == SDLK_KP_ENTER; }) > 0) {
            if (screen == Screen::CharSelect) lobby.enter(*scene);
            else session.coop_enter();
            // A name refused (taken, or not one D2 takes) is still being typed: Steam's keyboard, closed by its Enter, opens again.
            if (screen == Screen::CharSelect ? lobby.typing() : session.coop_typing()) { SDL_StopTextInput(win.window); text_active = false; }
        }
        {
            // The pad plays as a cursor, clicks and keys through the same paths as the mouse and keyboard.
            const std::uint32_t pad_now = std::uint32_t(SDL_GetTicks());
            const float pad_dt = std::min(float(pad_now - pad_last_ms) / 1000.f, 0.1f);
            pad_last_ms = pad_now;
            const pad::Context pad_context = screen == Screen::InGame && session.controls.open ? pad::Context::Menu   // Configure Controls: a menu's layout
                                           : screen == Screen::InGame ? (session.store.npc >= 0 ? pad::Context::Shop : session.pad_dialog() ? pad::Context::Dialog
                                                                         : session.pad_field() ? pad::Context::Game : pad::Context::Panel)
                                           : screen == Screen::Title  ? pad::Context::Title : pad::Context::Menu;
            const auto pad_changes = gamepads.changes();
            if (!pad_changes.went.empty()) { session.pad.teleport.clear(); session.loot_chooser.clear(); session.touch_effect.cancel(); session.touch_get_hold.clear(); session.cancel_touch_item(); }
            if (screen == Screen::InGame) {                      // hotplug, shown: a pad in or out mid-game (not the Deck's own controls: docked, they play no one)
                for (const auto& pad : pad_changes.came) if (!pad.built_in) session.add_chat(pad.name + " connected");
                for (const auto& pad : pad_changes.went) if (!pad.built_in) session.add_chat(pad.name + " disconnected");
            }
            const bool in_game = screen == Screen::InGame;
            if (in_game && !was_in_game) {                       // a game begins: co-op or not, for its length
                coop_game = gamepads.external() && !session.net_game;
                seats.clear();
                if (coop_game) {
                    d2d::log::info("co-op: a game with {} pads", gamepads.pads().size());
                    for (const auto& pad : gamepads.pads()) if (!pad.built_in) take_seat(pad);
                    // The lobby's choices join at once; a seat still choosing gets its roster in the game.
                    for (const auto& lobby_seat : lobby.seats)
                        if (lobby_seat.choice && !session.coop_choose(lobby_seat.seat, *lobby_seat.choice)) d2d::log::warn("co-op: seat {}'s character couldn't join", lobby_seat.seat);
                }
                lobby.seats.clear();
            } else if (in_game && coop_game) {
                // Went first: a pad Steam put someone else behind lets its seat go before its new self takes one.
                for (const auto& gone : pad_changes.went) {
                    const int seat = seats.disconnect(int(gone.id), pad_now);
                    if (seat < 0) continue;
                    // Steam's unnamed virtual pad (no real ids yet) can't come back by its key: a seat it
                    // took (the Deck's own controls, found out late) goes now, not after the hold.
                    if (seat > 0 && gone.key.starts_with("28de:11ff:")) { d2d::log::info("co-op: seat {} freed (an unnamed pad)", seat); seats.release(seat); session.coop_leave(seat); continue; }
                    d2d::log::info("co-op: seat {} held", seat);
                }
                for (const auto& pad : pad_changes.came) if (!pad.built_in) take_seat(pad);
                for (const int seat : seats.expire(pad_now)) {
                    if (seat > 0) { session.coop_leave(seat); continue; }
                    // The first player's pad gone for good: the next player leads (or they play on alone).
                    if (const int next = session.lead_leaves(); next > 0) { seats.swap(0, next); seats.release(next); }
                }
            }
            if (session.handed_over > 0) {                          // Save and Exit handed the game on: the next player's pad to seat 0, the leaver's logged out
                seats.swap(0, session.handed_over);
                seats.release(session.handed_over);
                session.handed_over = -1;
            }
            if (session.partner_left > 0) {                         // a partner's Save and Exit: their pad logged out
                seats.release(session.partner_left);
                session.partner_left = -1;
            }
            // A pad logged out (or past the four seats) comes back with Start: a seat and its roster.
            if (coop_game)
                for (const auto& pad : gamepads.pads()) {
                    const bool start = gamepads.sample(pad.id).held[std::size_t(pad::Button::Start)];
                    const bool was = std::exchange(start_held[pad.id], start);
                    if (!pad.built_in && start && !was && seats.seat_of(int(pad.id)) < 0) take_seat(pad);
                }
            if (!in_game) coop_game = false;
            if (!in_game && was_in_game) session.game_ended();
            was_in_game = in_game;
            if (coop_game)
                for (const auto& coop_player : session.coop)
                    session.coop_pad(coop_player.seat, gamepads.sample(SDL_JoystickID(std::max(seats.pad(coop_player.seat), 0))), pad_dt);
            // The screen's pad (a partner's while they have it) plays the UI; the first player's plays their field meanwhile.
            const int ui_seat = coop_game ? session.focus_seat() : 0;
            if (coop_game && ui_seat != 0) session.lead_pad(gamepads.sample(SDL_JoystickID(std::max(seats.pad(0), 0))), pad_dt);
            // Character select with two or more external pads: the lobby. The
            // first is player 1's (it alone plays the screen and starts the
            // game); the others choose their characters (Lobby).
            std::vector<int> external_pads;
            for (const auto& pad : gamepads.pads()) if (!pad.built_in) external_pads.push_back(int(pad.id));
            const bool lobby_on = g_coop_lobby && screen == Screen::CharSelect && scene && g_join_host.empty() && external_pads.size() >= 2;
            if (lobby_on) {
                lobby.sync({ external_pads.begin() + 1, external_pads.end() }, csu.selected, g_pad_config);
                for (auto& lobby_seat : lobby.seats) lobby.input(lobby_seat, gamepads.sample(SDL_JoystickID(lobby_seat.pad)), pad_dt, *scene);
            } else if (screen != Screen::InGame && screen != Screen::CharSelect) {
                lobby.seats.clear();
            }
            const pad::Sample first = coop_game ? gamepads.sample(SDL_JoystickID(std::max(seats.pad(ui_seat), 0)))
                                    : lobby_on  ? gamepads.sample(SDL_JoystickID(external_pads.front())) : gamepads.sample();
            if (ui_seat != last_ui_seat) {                       // another pad: what it holds now isn't pressed now
                pad_mapper = pad::Mapper{ g_pad_config };
                (void)pad_mapper.step(first, pad_context, 0);
                last_ui_seat = ui_seat;
            }
            if (session.pad_config_changed) {                       // Configure Controls' Accept: its pad settings
                g_pad_config = session.pad_config;
                pad_mapper = pad::Mapper{ g_pad_config };
                session.pad_config_changed = false;
            }
            // The pad page's edit takes the next button pressed, not what it does.
            const bool pad_capture = screen == Screen::InGame && session.controls.open && session.controls.editing && session.controls.pad_page;
            if (pad_capture)
                for (std::size_t button = 0; button < std::size_t(pad::Button::Count); ++button) {
                    const bool now_down = button == std::size_t(pad::Button::LeftTrigger) ? first.left_trigger >= 0.5f
                                        : button == std::size_t(pad::Button::RightTrigger) ? first.right_trigger >= 0.5f : first.held[button];
                    if (now_down && !pad_capture_held[button]) session.controls_pad_press = pad::Button(button);
                }
            for (std::size_t button = 0; button < std::size_t(pad::Button::Count); ++button)
                pad_capture_held[button] = button == std::size_t(pad::Button::LeftTrigger) ? first.left_trigger >= 0.5f
                                         : button == std::size_t(pad::Button::RightTrigger) ? first.right_trigger >= 0.5f : first.held[button];
            const bool controls_pad = screen == Screen::InGame && session.controls.open;
            const pad::SkillUi skill_ui = controls_pad || !session.pad_config.skill_buttons ? pad::SkillUi::None
                                         : session.skillbar.picking == 3 ? pad::SkillUi::Wheel
                                         : session.skill_strip >= 0 ? pad::SkillUi::Strip : pad::SkillUi::None;
            const bool chooser_closed = session.loot_chooser.take_controller_close();
            if (chooser_closed) pad_mapper.consume_until_neutral();
            pad::Actions pad_actions = pad_capture ? (void(pad_mapper.step(first, pad_context, pad_dt, skill_ui)), pad::Actions{})
                                                         : pad_mapper.step(first, pad_context, pad_dt, skill_ui);
            const int pad_side = (win.view_w - int(kScreenWidth)) / 2;   // widescreen's world beyond the 800x600 UI
            const bool pad_active = pad_actions.move_active || pad_actions.click_press || pad_actions.click_down || pad_actions.rclick_press
                                    || pad_actions.cursor_dx != 0 || pad_actions.cursor_dy != 0 || !pad_actions.keys.empty()
                                    || pad_actions.snap_x != 0 || pad_actions.snap_y != 0 || pad_actions.trade_press || pad_actions.tab_step != 0
                                    || pad_actions.skill_picker || pad_actions.recentre_cursor || pad_actions.focus_step != 0 || pad_actions.skill_busy
                                    || pad_actions.loot_cancel || pad_actions.aim_active;
            if (!chooser_closed && session.loot_chooser.open && !session.loot_chooser.touch) {
                session.loot_pad_event(pad_actions);
                if (session.loot_chooser.take_controller_close()) pad_mapper.consume_until_neutral();
                pad_actions = {};
            } else if (session.loot_chooser.open && pad_active) {
                session.loot_chooser.clear();
                pad_mapper.consume_until_neutral();
                pad_actions = {};
            }
            if (pad_active && !pad_in_use) d2d::log::info("gamepad: in use (screen {})", int(screen));
            if (pad_active) {
                pad_in_use = true;
                session.last_device = touch::LastDevice::Gamepad;
            }
            session.skill_actions = pad_actions;
            if (session.drive_skill_ui(pad_actions, mouse.x, mouse.y)) {
                pad_actions.click_press = false;
                pad_actions.click_down = false;
                pad_actions.click_release = false;
                pad_actions.rclick_press = false;
                pad_actions.keys.clear();
                pad_actions.snap_x = 0;
                pad_actions.snap_y = 0;
                pad_actions.skill_picker = false;
                pad_actions.trade_press = false;
                pad_actions.tab_step = 0;
            } else if (pad_actions.skill_picker) session.pad_open_picker();
            if (session.menu_handoff.drop(pad_capture_held[std::size_t(pad::Button::South)])) {
                pad_actions.click_press = false;
                pad_actions.click_down = false;
                pad_actions.click_release = false;
            }
            // In the field a pad plays without a cursor: its buttons hit what is in reach (ClientSession::pad_act).
            if (session.touch_pointer.active || session.touch_pointer.press || session.touch_pointer.release) {
                mouse.x = int(session.touch_pointer.x);
                mouse.y = int(session.touch_pointer.y);
                if (session.touch_pointer.press) { mouse.down = true; mouse.press_this_frame = true; }
                if (session.touch_pointer.release) { mouse.down = false; mouse.release_this_frame = true; }
                session.touch_pointer.press = session.touch_pointer.release = false;
            }
            const bool pad_field = screen == Screen::InGame && pad_in_use && session.pad_field()
                && (!session.touch_enabled || session.last_device == touch::LastDevice::Gamepad);
            const bool touch_owns_pointer = session.touch_enabled && session.last_device == touch::LastDevice::Touch
                && (session.touch_fingers > 0 || session.touch_pointer.active || session.touch_sheet.open);
            if (pad_field != pad_was_field && !mouse_took_over && !touch_owns_pointer && session.last_device != touch::LastDevice::Touch)
                { mouse.x = int(kScreenWidth) / 2; mouse.y = int(kScreenHeight) / 2; }   // the cursor comes back centred
            pad_was_field = pad_field;
            // A menu opening puts the pad's cursor on the choice to start from.
            const auto pad_focus = screen == Screen::InGame && pad_in_use && session.last_device == touch::LastDevice::Gamepad
                && !mouse_took_over && !touch_owns_pointer ? session.pad_focus() : std::nullopt;
            if (pad_focus && pad_focus->key != pad_focus_key) { mouse.x = pad_focus->x; mouse.y = pad_focus->y; }
            pad_focus_key = pad_focus ? pad_focus->key : 0;
            if (screen != Screen::CharSelect || pad_actions.cursor_dx != 0 || pad_actions.cursor_dy != 0 || pad_actions.focus_step != 0) roster_navigation = false;
            if (screen == Screen::CharSelect && !csu.confirm_delete && !csu.confirm_convert && !csu.pick_difficulty)
                for (const auto key : pad_actions.keys)
                    if (key == pad::Key::Up || key == pad::Key::Down || key == pad::Key::Left || key == pad::Key::Right) roster_navigation = true;
            pad_cursor_hidden = pad_field || roster_navigation
                || (session.touch_enabled && session.last_device == touch::LastDevice::Touch && !session.held);
            session.pad_playing = pad_in_use && session.last_device == touch::LastDevice::Gamepad;
            session.pad_cue = pad_field && !session.pad_config.skill_buttons && session.last_device == touch::LastDevice::Gamepad;
            // The button labels of the pad playing the UI: a co-op partner's while they have the screen.
            const int label_pad = seats.label_pad(coop_game, ui_seat);
            session.pad_family = label_pad >= 0 ? gamepads.family(SDL_JoystickID(label_pad))
                            : gamepads.primary() ? gamepads.primary()->family : pad::Family::Xbox;
            if (pad_actions.recentre_cursor) { mouse.x = int(kScreenWidth) / 2; mouse.y = int(kScreenHeight) / 2; }
            if (!pad_field) {
                mouse.x = std::clamp(mouse.x + int(std::lround(pad_actions.cursor_dx)), -pad_side, int(kScreenWidth) + pad_side - 1);
                mouse.y = std::clamp(mouse.y + int(std::lround(pad_actions.cursor_dy)), 0, int(kScreenHeight) - 1);
            }
            // The menus' buttons, for the d-pad's hops and the shoulders' steps.
            const auto menu_buttons = [&] {
                std::vector<pad::Point> points;
                const auto buttons = [&](std::initializer_list<const Button*> shown) {
                    for (const Button* button : shown) points.push_back({ button->x + button->width / 2, button->y + button->height / 2 });
                };
                if (screen == Screen::Title)                 for (const auto& button : title.buttons) buttons({ &button });
                else if (screen == Screen::OtherMultiplayer) for (const auto& button : other_multiplayer.buttons) buttons({ &button });
                else if (screen == Screen::TcpIp) {
                    if (tcpip.box_open) buttons({ &tcpip.box_ok, &tcpip.box_cancel });
                    else buttons({ &tcpip.join, &tcpip.host, &tcpip.cancel });
                } else if (screen == Screen::CharCreate) {
                    // Each figure's idle frame 0 box, its centre: where a click picks it (class_under, FUN_005003a0).
                    for (std::size_t index = 0; index < std::size(kClassPos); ++index) {
                        const auto& position = kClassPos[index];
                        const auto* idle = scene ? &scene->class_anims[index][0] : nullptr;
                        if (!idle || idle->frames_per_direction() == 0) { points.push_back({ position.x, position.y - 50 }); continue; }
                        const auto& frame = idle->frame(0, 0);
                        points.push_back({ position.x + frame.offset_x + int(frame.width) / 2,
                                           position.y + frame.offset_y - int(frame.height) / 2 });
                    }
                    buttons({ &character.cancel_btn, &character.ok_btn });
                } else if (screen == Screen::CharSelect) {
                    if (csu.confirm_delete || csu.confirm_convert) buttons({ &csu.popup_no, &csu.popup_yes });
                    else if (csu.pick_difficulty) buttons({ &csu.difficulty_btn[0], &csu.difficulty_btn[1], &csu.difficulty_btn[2] });
                    else buttons({ &csu.create_btn, &csu.convert_btn, &csu.delete_btn, &csu.cancel_btn, &csu.ok_btn });
                } else if (screen == Screen::InGame && session.controls.open && scene) {
                    for (const auto& [point_x, point_y] : session.controls.buttons(*scene)) points.push_back({ point_x, point_y });
                }
                return points;
            };
            if (pad_actions.snap_x != 0 || pad_actions.snap_y != 0) {
                // Char select's d-pad steps its roster by key instead.
                // Char select's d-pad steps its roster by key instead, as Configure Controls' steps its rows.
                const bool by_key = (screen == Screen::CharSelect && !csu.confirm_delete && !csu.confirm_convert && !csu.pick_difficulty) || (screen == Screen::InGame && session.controls.open);
                const auto points = by_key ? std::vector<pad::Point>{} : screen == Screen::InGame ? session.pad_snap_points() : menu_buttons();
                if (const int pick = (session.tree_open && screen == Screen::InGame ? pad::tree_pick : pad::snap_pick)(mouse.x, mouse.y, pad_actions.snap_x, pad_actions.snap_y, points); pick >= 0) {
                    mouse.x = points[std::size_t(pick)].x; mouse.y = points[std::size_t(pick)].y;
                }
            }
            if (pad_actions.focus_step != 0) {
                // The shoulders in a menu: the cursor to the screen's next / last button, round.
                if (const auto points = menu_buttons(); !points.empty()) {
                    const auto current = std::ranges::find_if(points, [&](const pad::Point& point) { return point.x == mouse.x && point.y == mouse.y; });
                    const int count = int(points.size());
                    const int next = current == points.end() ? (pad_actions.focus_step > 0 ? 0 : count - 1)
                                                        : (int(current - points.begin()) + pad_actions.focus_step + count) % count;
                    mouse.x = points[std::size_t(next)].x; mouse.y = points[std::size_t(next)].y;
                }
            }
            session.pad_a_edge = session.pad_a_down = session.pad_a_up = false;
            session.pad_south_down = session.pad_south_edge = session.pad_rt_down = session.pad_rt_edge = false;
            if (!mouse_took_over && !pad_field) {
                session.pad_a_edge = pad_actions.click_press;
                session.pad_a_down = pad_actions.click_down;
                session.pad_a_up = pad_actions.click_release;
                session.pad_south_down = pad_actions.south_down;
                session.pad_south_edge = pad_actions.south_press;
                session.pad_rt_down = pad_actions.right_trigger_down;
                session.pad_rt_edge = pad_actions.right_trigger_press;
            }
            if (pad_actions.trade_press) session.pad_trade(mouse.x, mouse.y);
            if (pad_actions.drop_press) session.pad_drop(mouse.x, mouse.y);
            if (pad_actions.tab_step != 0) session.pad_tab(pad_actions.tab_step);
            if (pad_actions.click_press && !pad_field)   { mouse.down = true;  mouse.press_this_frame = true; }
            if (pad_actions.click_release && mouse.down) { mouse.down = false; mouse.release_this_frame = true; }
            if (pad_actions.rclick_press && screen == Screen::InGame && !pad_field && session.pad_back(mouse.x, mouse.y)) {
                pad_actions.rclick_press = false;
                pad_actions.keys.push_back(pad::Key::Escape);
            }
            if (pad_actions.rclick_press && !pad_field)  mouse.rpress_this_frame = true;
            for (const pad::Key pad_key : pad_actions.keys) {
                // Escape quits from the title screen; B must not be able to close the app there.
                if (screen == Screen::Title && pad_key == pad::Key::Escape) continue;
                SDL_Event key_event{};
                key_event.type = SDL_EVENT_KEY_DOWN;
                key_event.key.down = true;
                key_event.key.key = pad_keycode(pad_key);
                handle_sdl_events(key_event, mouse, screen, text_this_frame, backspace_this_frame, keys_this_frame, quit);
            }
            session.pad.move = screen == Screen::InGame
                ? ClientSession::PadMove{ pad_actions.move_x, pad_actions.move_y, pad_actions.move_active,
                                pad_field && pad_actions.click_press, pad_field && pad_actions.click_down, pad_field && pad_actions.rclick_press,
                                pad_actions.aim_active, pad_actions.aim_x, pad_actions.aim_y, pad_actions.aim_magnitude }
                : ClientSession::PadMove{};
            if (session.touch_enabled && session.last_device == touch::LastDevice::Touch && session.pad_field()) session.pad.move = {};
        }
        current_phase.store(std::uint32_t(MainPhase::Devctl),
                            std::memory_order_relaxed);
        if (channel.active()) channel.pump();

        // The in-game view widens to the window's aspect (--widescreen); the
        // menus, cinematics and endgame stay 800x600.
        {
            int want = int(kScreenWidth);
            if (g_widescreen && screen == Screen::InGame) {
                int pixels_w = 0, pixels_h = 0;
                SDL_GetWindowSizeInPixels(win.window, &pixels_w, &pixels_h);
                want = g_forced_view_w > 0 ? g_forced_view_w
                     : pixels_h > 0 ? std::clamp(int(kScreenHeight) * pixels_w / pixels_h & ~1, int(kScreenWidth), kMaxViewWidth) : int(kScreenWidth);
            }
            if (want != win.view_w && win.set_view(want)) {
                framebuffer.assign(std::size_t(want) * kScreenHeight * 4, 0);
                for (std::size_t i = 3; i < framebuffer.size(); i += 4) framebuffer[i] = 0xFF;
                g_view_width = want;
            }
            apply_safe_inset(win);
        }
        const std::uint32_t t_after_input = std::uint32_t(SDL_GetTicks());
        current_phase.store(std::uint32_t(MainPhase::Render),
                            std::memory_order_relaxed);
        const auto now_ms = std::uint32_t(SDL_GetTicks() - start_ticks);
        // Level music and ambience belong to the game screen.
        if (screen != Screen::InGame && (audio.music.sound > 0 || audio.music_old.src)) {
            audio.stop(audio.music);
            audio.stop(audio.music_old);
            audio.stop(audio.ambience);
            audio.stop(audio.voice);
        }
        if (screen == Screen::InGame && audio.music.sound < 0) audio.stop(audio.music);
        audio.update();
        const Screen prev_screen = last_screen;          // the screen as of the last frame
        last_screen = screen;
        // Out of the game (to the menus): its world sounds, ambience and rain
        // stop. Under a cinematic they hold (the game goes on after it; the
        // ClientSession's frame picks them up again).
        const bool game_screen = screen == Screen::InGame || screen == Screen::Video;
        if ((prev_screen == Screen::InGame || prev_screen == Screen::Video) && !game_screen) audio.stop_world();
        if (screen == Screen::Video && prev_screen == Screen::InGame) audio.pause_world(true);
        // Frontend music (FUN_00516250's music thread + FUN_00514990): on
        // the menus, whenever nothing plays, the next track of the 8-track
        // list (the LoD one at 0x72f8b8; classic uses 0x72f878's), played
        // by path at full volume (frontend_track picks it). Entering a game
        // stops it (FUN_00514930); back on the menus it starts over.
        if (screen == Screen::InGame) frontend_restart = true;
        if (scene && screen != Screen::InGame && screen != Screen::Video && audio.ok && audio.music.sound == 0) {
            static constexpr const char* kFrontendMusic[8] = {
                R"(data\global\music\introedit.wav)", R"(data\global\music\act5\icecaves.wav)",
                R"(data\global\music\act5\xtemple.wav)", R"(data\global\music\act2\desert.wav)",
                R"(data\global\music\act2\sewer.wav)", R"(data\global\music\act3\kurast.wav)",
                R"(data\global\music\act3\kurastsewer.wav)", R"(data\global\music\act4\diablo.wav)" };
            const int pick = frontend_track(frontend_played, frontend_restart, session.rng(8));
            audio.play_music_path(*scene, kFrontendMusic[pick], -(pick + 1), 1.f, false);
        }
        if (scene) {
            switch (screen) {
            case Screen::Video: {
                // Next video when none plays or the viewer skips one.
                const bool skip = mouse.press_this_frame || !keys_this_frame.empty();
                if (video_playing && skip) { video_playing = false; audio.video_stop(); }
                while (!video_playing && !video_queue.empty()) {
                    const std::string path = video_queue.front();
                    video_queue.erase(video_queue.begin());
                    auto file = video_mpqs.open(path);
                    const bool letterbox = path.find("x292") != std::string::npos;
                    if (file && video.open(std::move(*file), int(kScreenWidth), letterbox ? int(kScreenHeight) * 292 / 480 : int(kScreenHeight))) {
                        video_playing = true;
                        video_start = now_ms;
                        audio.video_start(video.sample_rate());
                    }
                }
                if (!video_playing) { screen = video_return; video_return = Screen::Title; break; }
                if (!video.advance(double(now_ms - video_start) / 1000.0)) {
                    video_playing = false;
                    audio.video_stop();
                }
                audio.video_feed(video.audio());
                std::fill(framebuffer.begin(), framebuffer.end(), std::uint8_t{0});
                for (std::size_t i = 3; i < framebuffer.size(); i += 4) framebuffer[i] = 0xFF;
                const int top = (int(kScreenHeight) - video.height()) / 2;
                const auto& pixels = video.rgba();
                for (int y = 0; y < video.height(); ++y)
                    std::memcpy(&framebuffer[(std::size_t(top + y) * kScreenWidth) * 4], &pixels[std::size_t(y) * kScreenWidth * 4], std::size_t(kScreenWidth) * 4);
                break;
            }
            case Screen::Cinematics: {
                if (prev_screen != Screen::Cinematics)             // entering: refresh what's unlocked
                    cin_ui = cinematics_ui(*scene, cinematics_unlocked(read_seen()));
                for (int i = 0; i < 7; ++i) {
                    auto& entry = cin_ui.entry[std::size_t(i)];
                    if (i >= cin_ui.unlocked) { entry.hovered = entry.pressed = false; continue; }
                    Screen dummy = screen;
                    update_button(entry, mouse, dummy, quit);
                    if (entry.clicked && video_mpqs.contains(kCinematicVideo[std::size_t(i)])) {
                        video_queue = { kCinematicVideo[std::size_t(i)] };
                        video_return = Screen::Cinematics;
                        audio.stop(audio.music);
                        screen = Screen::Video;
                    }
                }
                if (update_button(cin_ui.cancel, mouse, screen, quit)) { quit = false; screen = Screen::Title; }
                render_cinematics(framebuffer, *scene, cin_ui);
                break;
            }
            case Screen::Endgame:
                // FUN_0044e770: OK pressed and released on it (FUN_0044e460 /
                // FUN_0044e4f0) -> the main menu.
                if (prev_screen != Screen::Endgame || !endgame.background.frames_per_direction()) endgame = endgame_ui(*scene, session.hero.character.header);
                if (mouse.press_this_frame && endgame_on_ok(mouse.x, mouse.y)) endgame.pressed = true;
                if (mouse.release_this_frame) {
                    if (endgame.pressed && endgame_on_ok(mouse.x, mouse.y)) screen = Screen::Title;
                    endgame.pressed = false;
                }
                render_endgame(framebuffer, *scene, endgame);
                break;
            case Screen::Title:
                if (prev_screen == Screen::Video) {                 // Esc skipped the videos
                    audio.video_stop();
                    video_playing = false;
                    video_queue.clear();
                    // ACT04END skipped: the endgame screen still comes.
                    if (std::exchange(video_return, Screen::Title) == Screen::Endgame) { screen = Screen::Endgame; break; }
                }
                for (auto& button : title.buttons) update_button(button, mouse, screen, quit);
                if (screen == Screen::CharSelect) {           // SINGLE PLAYER
                    g_join_host = g_cli_join;
                    g_lan_connect = g_cli_lan_connect;
                    g_lan_host_port = g_cli_lan_host_port;
                }
                render_title(framebuffer, *scene, title.buttons, now_ms, title.version);
                break;
            case Screen::OtherMultiplayer:
                for (auto& button : other_multiplayer.buttons) update_button(button, mouse, screen, quit);
                render_title(framebuffer, *scene, other_multiplayer.buttons, now_ms);
                break;
            case Screen::TcpIp: {
                // docs/research/re/tcpip-menu.md. Esc is the screen's CANCEL,
                // or the join box's (FUN_004fa450 names each).
                if (prev_screen != Screen::TcpIp) tcpip = tcpip_ui(*scene, d2d::net::own_address());
                auto clicked = [&](Button& button) {
                    Screen stay = screen;
                    update_button(button, mouse, stay, quit);
                    return button.clicked;
                };
                const bool escape = std::ranges::find(keys_this_frame, SDLK_ESCAPE) != keys_this_frame.end();
                if (!tcpip.popup.empty()) {                  // the failed test's popup: OK back to these options (FUN_0042ffe0)
                    Screen stay = screen;
                    update_button(tcpip.popup_ok, mouse, stay, quit);
                    if (tcpip.popup_ok.clicked || escape) tcpip.popup.clear();
                } else if (tcpip.box_open) {
                    // The edit line (record 0x10d) takes up to 255 characters.
                    for (const char letter : text_this_frame) if (tcpip.address.size() < 255) tcpip.address.push_back(letter);
                    if (backspace_this_frame && !tcpip.address.empty()) tcpip.address.pop_back();
                    const bool enter = std::ranges::any_of(keys_this_frame, [](SDL_Keycode key) { return key == SDLK_RETURN || key == SDLK_KP_ENTER; });
                    const bool ok = clicked(tcpip.box_ok) || enter;
                    if (clicked(tcpip.box_cancel) || escape) tcpip.box_open = false;
                    else if (ok && !tcpip.address.empty()) {
                        // FUN_00434790: the address is kept (registry LastTcpIp), then one
                        // test connect (FUN_0052a750, port 4000): failing, "Cannot Connect
                        // to Server" (0x145b); else char-select joins it (game type 9).
                        g_last_tcp_ip = tcpip.address;
                        if (!d2d::userdir::save_cfg(g_user_dir / "d2d.cfg", { { "last_tcp_ip", g_last_tcp_ip } }))
                            d2d::log::warn("d2d.cfg: last_tcp_ip not saved");
                        tcpip.box_open = false;
                        g_lan_host_port = 0;
                        g_lan_connect.clear();
                        g_join_host.clear();
                        // FUN_0042f6e0 replaces the edit box with its PLEASE WAIT popup.
                        // Present it now: both connects below are synchronous, so the main
                        // loop cannot reach its usual presentation until they return.
                        render_tcpip(framebuffer, *scene, tcpip, now_ms);
                        render_connecting(framebuffer, *scene);
                        SDL_UpdateTexture(win.texture, nullptr, shown_frame(framebuffer).data(), fb_width(framebuffer) * 4);
                        SDL_RenderClear(win.renderer);
                        SDL_RenderTexture(win.renderer, win.texture, nullptr, nullptr);
                        SDL_RenderPresent(win.renderer);
                        crash_frame.update(framebuffer);
                        if (auto lan_test = d2d::net::TcpConnection::connect(tcpip.address, d2d::net::lan::kDefaultPort, 1500)) {   // a d2d host (lan-host.md)
                            g_lan_connect = tcpip.address;
                            screen = Screen::CharSelect;
                        } else if (auto test = d2d::net::TcpConnection::connect(tcpip.address, 4000, 5000); !test) {
                            d2d::log::warn("join {}: {}", tcpip.address, test.error());
                            tcpip.popup = tcpip.no_connect;
                        } else {
                            g_join_host = tcpip.address;
                            screen = Screen::CharSelect;
                        }
                    }
                } else {
                    const bool join = clicked(tcpip.join) && tcpip.has_ip;
                    if (clicked(tcpip.host) && tcpip.has_ip) {   // d2d hosts d2d players (lan-host.md)
                        g_lan_host_port = d2d::net::lan::kDefaultPort;
                        g_lan_connect.clear();
                        g_join_host.clear();
                        screen = Screen::CharSelect;
                    }
                    update_button(tcpip.cancel, mouse, screen, quit);
                    if (escape) screen = Screen::OtherMultiplayer;
                    if (join) { tcpip.box_open = true; tcpip.address = g_last_tcp_ip; }
                }
                render_tcpip(framebuffer, *scene, tcpip, now_ms);
                render_tcpip_popup(framebuffer, *scene, tcpip);
                break;
            }
            case Screen::Credits:
                if (mouse.release_this_frame) screen = Screen::Title;
                render_credits(framebuffer, *scene, now_ms);
                break;
            case Screen::CharSelect: {
                if (!csu.popup.empty()) {   // a failed join's popup takes the input until OK
                    Screen stay = screen;
                    update_button(csu.popup_ok, mouse, stay, quit);
                    if (csu.popup_ok.clicked
                        || std::ranges::any_of(keys_this_frame, [](SDL_Keycode key) { return key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE; })) {
                        csu.popup.clear();
                        screen = csu.popup_return;
                    }
                    render_charselect(framebuffer, *scene, csu, now_ms);
                    break;
                }
                if (csu.confirm_convert) {   // CONVERT's YES / NO takes the input
                    Screen stay = screen;
                    update_button(csu.popup_no, mouse, stay, quit);
                    update_button(csu.popup_yes, mouse, stay, quit);
                    const auto key = [&](auto... wanted) { return std::ranges::any_of(keys_this_frame, [&](SDL_Keycode got) { return ((got == wanted) || ...); }); };
                    const bool yes = csu.popup_yes.clicked || key(SDLK_RETURN, SDLK_KP_ENTER);
                    if (yes || csu.popup_no.clicked || key(SDLK_ESCAPE)) csu.confirm_convert = false;
                    if (yes && g_no_save) d2d::log::info("--no-save: {} not converted", scene->saves[std::size_t(std::clamp(csu.selected, 0, int(scene->saves.size()) - 1))].name);
                    if (yes && !g_no_save && csu.selected >= 0 && csu.selected < int(scene->saves.size()) && !scene->saves[std::size_t(csu.selected)].expansion()) {
                        // FUN_0043b520 -> FUN_0043caa0 on <save dir><name>.d2s; a failure says 0x5570 (FUN_00435180).
                        const auto path = save_dir / (scene->saves[std::size_t(csu.selected)].name + ".d2s");
                        std::ifstream save_in(path, std::ios::binary);
                        std::vector<char> raw{ std::istreambuf_iterator<char>(save_in), {} };
                        save_in.close();
                        std::vector<std::byte> bytes(raw.size());
                        std::memcpy(bytes.data(), raw.data(), raw.size());
                        // Written as any save is (CharacterStore::save_bytes: a .bak, a temp file, a rename), so a
                        // failed write leaves the old save.
                        bool done = d2d::d2s::convert_to_expansion(bytes);
                        if (done) {
                            const auto why = characters.save_bytes(scene->saves[std::size_t(csu.selected)].name, bytes);
                            if (!why.empty()) d2d::log::warn("CONVERT TO EXPANSION: {}", why);
                            done = why.empty();
                        }
                        if (done) {
                            const int kept = csu.selected;
                            load_saves(*scene, save_dir);
                            csu.selected = std::min(kept, int(scene->saves.size()) - 1);
                        } else {
                            const auto text = lookup_string(*scene, 0x5570);
                            csu.popup = text ? u16_to_latin1(*text) : "Unable to access file. Cannot convert character.";
                            csu.popup_return = Screen::CharSelect;
                        }
                        d2d::log::info("charselect: convert {} to expansion: {}", path.filename().string(), done ? "done" : "failed");
                    }
                    render_charselect(framebuffer, *scene, csu, now_ms);
                    break;
                }
                if (csu.confirm_delete) {   // DELETE's YES / NO takes the input
                    Screen stay = screen;
                    update_button(csu.popup_no, mouse, stay, quit);
                    update_button(csu.popup_yes, mouse, stay, quit);
                    const auto key = [&](auto... wanted) { return std::ranges::any_of(keys_this_frame, [&](SDL_Keycode got) { return ((got == wanted) || ...); }); };
                    const bool yes = csu.popup_yes.clicked || key(SDLK_RETURN, SDLK_KP_ENTER);
                    if (yes || csu.popup_no.clicked || key(SDLK_ESCAPE)) csu.confirm_delete = false;
                    if (yes && g_no_save) d2d::log::info("--no-save: {} not deleted", scene->saves[std::size_t(std::clamp(csu.selected, 0, int(scene->saves.size()) - 1))].name);
                    if (yes && !g_no_save && csu.selected >= 0 && csu.selected < int(scene->saves.size())) {
                        // FUN_0043c750: every file <save dir><name>.* (the .d2s, .key, .ma*, ...).
                        const std::string prefix = scene->saves[std::size_t(csu.selected)].name + ".";
                        std::error_code error;
                        for (const auto& entry : fs::directory_iterator(save_dir, error))
                            if (entry.is_regular_file(error) && entry.path().filename().string().starts_with(prefix)) fs::remove(entry.path(), error);
                        load_saves(*scene, save_dir);
                        csu.selected = scene->saves.empty() ? -1 : 0;
                        csu.scroll = 0;
                    }
                    render_charselect(framebuffer, *scene, csu, now_ms);
                    break;
                }
                int chosen_difficulty = -1;
                if (csu.pick_difficulty) {   // the difficulty box takes the input until a pick or Esc
                    Screen stay = screen;
                    for (auto& button : csu.difficulty_btn) update_button(button, mouse, stay, quit);
                    static constexpr SDL_Keycode kDifficultyKey[3] = { SDLK_R, SDLK_N, SDLK_H };   // records 0xa8..0xaa
                    for (int index = 0; index < 3; ++index)
                        if ((index < 2 || csu.hell_open)
                            && (csu.difficulty_btn[std::size_t(index)].clicked
                                || std::ranges::find(keys_this_frame, kDifficultyKey[index]) != keys_this_frame.end()))
                            chosen_difficulty = index;
                    if (chosen_difficulty < 0) {
                        if (std::ranges::find(keys_this_frame, SDLK_ESCAPE) != keys_this_frame.end()) csu.pick_difficulty = false;
                        render_charselect(framebuffer, *scene, csu, now_ms);
                        break;
                    }
                    csu.pick_difficulty = false;
                    keys_this_frame.clear();
                }
                if (std::ranges::find(keys_this_frame, SDLK_ESCAPE) != keys_this_frame.end()) screen = Screen::Title;
                const int count = int(scene->saves.size());
                const int max_scroll = charselect_max_scroll(count);
                int rows = -mouse.wheel;   // wheel up = scroll toward the top
                bool play = false;
                if (mouse.press_this_frame && chosen_difficulty < 0 && !roster_navigation) {   // a pick's click stays in the box, off the slots under it
                    const int slot = charselect_slot_at(mouse.x, mouse.y);
                    if (slot >= 0 && csu.scroll + slot < count) {
                        // A second press on the same character within
                        // 500 ms plays it, like OK (FUN_0043a9d0).
                        play = csu.selected == csu.scroll + slot && now_ms - csu.last_click_ms < 500;
                        csu.selected = csu.scroll + slot;
                        csu.last_click_ms = now_ms;
                    }
                    // Scrollbar arrows (only live while the bar is shown).
                    if (max_scroll > 0 && mouse.x >= kScrollX
                        && mouse.x < kScrollX + 12) {
                        if (mouse.y >= kScrollUpTop && mouse.y < kScrollUpTop + kScrollArrow)
                            rows = -1;
                        if (mouse.y >= kScrollDownTop && mouse.y < kScrollDownTop + kScrollArrow)
                            rows = 1;
                    }
                }
                csu.scroll = std::clamp(csu.scroll + 2 * rows, 0, max_scroll);
                const bool pad_start = session.pad_a_edge && csu.popup.empty() && !csu.confirm_delete && !csu.confirm_convert && !csu.pick_difficulty;
                if (pad_start) {
                    const auto over = [&](const Button& button) {
                        return mouse.x >= button.x && mouse.x < button.x + button.width && mouse.y >= button.y && mouse.y < button.y + button.height;
                    };
                    const bool on_button = over(csu.create_btn) || over(csu.convert_btn) || over(csu.delete_btn) || over(csu.cancel_btn) || over(csu.ok_btn);
                    const bool on_scroll = max_scroll > 0 && mouse.x >= kScrollX && mouse.x < kScrollX + 12
                        && ((mouse.y >= kScrollUpTop && mouse.y < kScrollUpTop + kScrollArrow)
                            || (mouse.y >= kScrollDownTop && mouse.y < kScrollDownTop + kScrollArrow));
                    const int slot = charselect_slot_at(mouse.x, mouse.y);
                    const int under = !roster_navigation && slot >= 0 && csu.scroll + slot < count ? csu.scroll + slot : -1;
                    csu.selected = pad::roster_target(roster_navigation, under, csu.selected);
                    if (pad::starts_character(true, !roster_navigation && (on_button || on_scroll), under, csu.selected, count)) play = true;
                }
                // Keyboard, as LoD's FUN_00439e90: Home/End jump to the
                // ends, Left/Right only move within the row (2 columns),
                // Up/Down a whole row; the list scrolls to keep the pick
                // on screen. Enter plays it.
                for (const auto key : keys_this_frame) {
                    if (count == 0) break;
                    int& sel = csu.selected;
                    if (sel < 0) sel = 0;
                    else if (key == SDLK_HOME)                       sel = 0;
                    else if (key == SDLK_END)                        sel = count - 1;
                    else if (key == SDLK_LEFT  && sel % 2 == 1)      sel -= 1;
                    else if (key == SDLK_RIGHT && sel % 2 == 0 && sel + 1 < count) sel += 1;
                    else if (key == SDLK_UP    && sel >= 2)          sel -= 2;
                    else if (key == SDLK_DOWN  && sel + 2 < count)       sel += 2;
                    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) play = true;
                    const int row0 = sel / 2 * 2;
                    if (row0 < csu.scroll) csu.scroll = row0;
                    if (row0 > csu.scroll + kSlots - 2) csu.scroll = row0 - (kSlots - 2);
                }
                // OK only enters the game with a save picked.
                csu.ok_btn.do_switch = csu.selected >= 0;
                for (Button* button : {&csu.create_btn, &csu.convert_btn, &csu.delete_btn,
                                  &csu.cancel_btn, &csu.ok_btn})
                    update_button(*button, mouse, screen, quit);
                if (play && csu.selected >= 0 && csu.selected < count) screen = Screen::InGame;
                if (chosen_difficulty >= 0) screen = Screen::InGame;
                // A dead hardcore character (FUN_00439840: status 4 and 8 at 00439a36, single player or
                // hosting): game.exe's popup 0x14b8 and no game. A joiner's goes on; the host refuses it.
                if (screen == Screen::InGame && g_join_host.empty() && csu.selected >= 0 && csu.selected < count
                    && scene->saves[std::size_t(csu.selected)].hardcore() && scene->saves[std::size_t(csu.selected)].died()) {
                    screen = Screen::CharSelect;
                    chosen_difficulty = -1;
                    const auto text = lookup_string(*scene, 0x14b8);
                    csu.popup = text ? u16_to_latin1(*text) : "You cannot create or join games with a dead hardcore character.";
                    csu.popup_return = Screen::CharSelect;
                    d2d::log::info("charselect: {}", csu.popup);
                }
                // Past Normal, game.exe asks first (FUN_00439840); joining, the host's game sets it.
                if (screen == Screen::InGame && chosen_difficulty < 0 && g_join_host.empty() && g_lan_connect.empty() && csu.selected >= 0 && csu.selected < count
                    && asks_difficulty(scene->saves[std::size_t(csu.selected)])) {
                    screen = Screen::CharSelect;
                    csu.pick_difficulty = true;
                    for (auto& button : csu.difficulty_btn) disarm(button);
                    csu.hell_open = top_difficulty(scene->saves[std::size_t(csu.selected)]) >= 2;
                }
                if (csu.convert_btn.clicked && csu.selected >= 0 && csu.selected < count && !scene->saves[std::size_t(csu.selected)].expansion()) {
                    csu.confirm_convert = true;
                    roster_navigation = false;                 // the pad's cursor shows on YES / NO
                    disarm(csu.popup_yes);
                    disarm(csu.popup_no);
                }
                if (csu.delete_btn.clicked && csu.selected >= 0 && csu.selected < count) {
                    csu.confirm_delete = true;
                    roster_navigation = false;
                    disarm(csu.popup_yes);
                    disarm(csu.popup_no);
                }
                if (screen == Screen::InGame) {
                    // Load the picked save into the in-game character.
                    const auto& header = scene->saves[std::size_t(csu.selected)];
                    static_cast<Character&>(character) = character_of_save(*scene, std::size_t(csu.selected));
                    character.selected   = kSaveClassToUi[header.cls];
                    character.hardcore   = header.hardcore();
                    if (chosen_difficulty >= 0)   // the pick is now the save's current difficulty, its act kept
                        for (int index = 0; index < 3; ++index)
                            character.header.difficulty[std::size_t(index)] = std::uint8_t((character.header.difficulty[std::size_t(index)] & 0x7f) | (index == chosen_difficulty ? 0x80 : 0));
                    if (!g_lan_connect.empty()) {
                        // --connect: a d2d host's game. The host plays the character and keeps it;
                        // this World only draws what the host's Views say (lan-host.md).
                        std::ifstream save_file(save_dir / (header.name + ".d2s"), std::ios::binary);
                        std::vector<std::uint8_t> save{ std::istreambuf_iterator<char>(save_file), {} };
                        std::expected<d2d::net::lan::Client, std::string> joined = std::unexpected(std::string("the character's save didn't read"));
                        if (save_file) {
                            d2d::log::info("lan: joining {} as {}", g_lan_connect, header.name);
                            joined = d2d::net::lan::Client::join(g_lan_connect, { .name = header.name, .save = std::move(save) });
                        }
                        std::optional<d2d::net::lan::Client::Event> welcome;
                        std::string refused = joined ? "" : joined.error();
                        for (int wait = 0; joined && !welcome && refused.empty() && wait < 500; ++wait) {
                            for (auto& reply : joined->poll()) {
                                if (reply.type == d2d::net::lan::Client::Event::Type::Welcome) welcome = std::move(reply);
                                else if (reply.type != d2d::net::lan::Client::Event::Type::View) refused = reply.reason;
                            }
                            if (!welcome) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        if (!welcome || !refused.empty() || welcome->info.size() < 6) {   // a Refuse can come with the Welcome (a hardcore check, a full game)
                            d2d::log::error("lan: join failed: {}", refused.empty() ? "no welcome from the host" : refused);
                            screen = Screen::CharSelect;
                            csu.popup = join_error_text(*scene, { .what = refused.empty() ? "Cannot Connect to Server" : refused, .no_connect = refused.empty() || !joined }, header.expansion());
                            csu.popup_return = g_cli_lan_connect.empty() ? Screen::TcpIp : Screen::CharSelect;
                        } else {
                            std::uint32_t seed = 0;
                            for (int shift = 0; shift < 32; shift += 8) seed |= std::uint32_t(welcome->info[std::size_t(shift / 8)]) << shift;
                            const int difficulty = std::clamp(int(welcome->info[4]), 0, 2);
                            d2d::log::info("lan: in {}'s game as player {}: act {}, map seed {:#x}, difficulty {}", g_lan_connect, welcome->player, welcome->info[5] + 1, seed, difficulty);
                            set_map_seed(*scene, seed, difficulty);
                            character.header.difficulty = {};
                            character.header.difficulty[std::size_t(difficulty)] = 0x80;
                            session.lan_store = session.world.characters;  // the host's Saves of the character go here
                            session.world.characters = nullptr;          // the host writes the save
                            session.lan_client = std::move(*joined);
                            session.lan_client->send_resync();   // Views that came while we joined were dropped: a whole one, once we're in
                            session.enter();
                        }
                    } else if (g_join_host.empty()) {
                        set_map_seed(*scene, game_seed(character.header), character.header.active_difficulty());
                        if (g_lan_host_port > 0 && !session.lan_host) {   // TCP/IP's HOST GAME
                            if (auto listening = d2d::net::lan::Host::listen(std::uint16_t(g_lan_host_port))) {
                                session.lan_host.emplace(std::move(*listening));
                                d2d::log::info("lan: hosting on port {} ({})", g_lan_host_port, d2d::net::own_address());
                            } else d2d::log::warn("lan: can't host on port {}: {}", g_lan_host_port, listening.error());
                        }
                        session.enter();                            // the World takes the character
                    } else {
                        // --join: the host's game, at its map seed. d2d's World saves
                        // nothing from it: the host keeps the character and its
                        // save-back is what's written. A failed join stays here.
                        std::ifstream save_file(save_dir / (header.name + ".d2s"), std::ios::binary);
                        std::vector<std::uint8_t> save{ std::istreambuf_iterator<char>(save_file), {} };
                        const auto log_path = g_user_dir / std::format("net-{:%Y%m%dT%H%M%SZ}.log", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
                        d2d::log::info("Joining {} as {} (net log {})", g_join_host, header.name, log_path.string());
                        std::vector<std::pair<int, int>> send_bits;
                        for (const auto& desc : scene->stat_desc) send_bits.emplace_back(desc.send_bits, desc.send_param);
                        std::vector<std::array<std::uint8_t, 16>> monster_parts;
                        for (const auto& type_info : scene->monsters.types) monster_parts.push_back(type_info.choices);
                        auto joined = NetGame::join(g_join_host, g_game_exe, std::move(save), log_path, 10000,
                                                    scene->item_tables ? &*scene->item_tables : nullptr, g_autoparty, std::move(send_bits), std::move(monster_parts));
                        if (!joined) {
                            d2d::log::error("join failed: {}", joined.error().what);
                            csu.popup = join_error_text(*scene, joined.error(), header.expansion());
                            // A host that didn't answer goes back to TCP/IP, as
                            // game.exe's test connect does (FUN_00434790, OK -> FUN_0042ffe0).
                            csu.popup_return = joined.error().no_connect && g_cli_join.empty() ? Screen::TcpIp : Screen::CharSelect;
                            screen = Screen::CharSelect;
                        } else {
                            auto& net = **joined;
                            d2d::log::info("  in {}'s game: act {}, map seed {:#x}, difficulty {}", g_join_host, net.act + 1, net.map_seed, net.difficulty);
                            set_map_seed(*scene, net.map_seed, net.difficulty);
                            character.header.difficulty = {};              // the host's game's difficulty, Act 1
                            character.header.difficulty[std::size_t(std::clamp(net.difficulty, 0, 2))] = 0x80;
                            session.world.characters = nullptr;
                            session.world.fight.remote_monsters = true;
                            for (const auto& type_info : scene->monsters.types) net.monster_velocity.push_back(type_info.velocity);
                            session.net_game = std::move(*joined);
                            session.net_chat_npc = 0;
                            session.world.hosted_quests = true;
                            session.enter();
                            if (const auto* here = session.world.level; here && net.self_x > 0) {
                                session.hero.player.x = (net.self_x + 0.5f) / 5.f - float(here->world_x);
                                session.hero.player.y = (net.self_y + 0.5f) / 5.f - float(here->world_y);
                                session.publish();
                            }
                            session.net_game->set_running(session.view.running);   // the host starts us walking
                        }
                    }
                }
                if (lobby.typing()) lobby.text(text_this_frame, backspace_this_frame);   // a partner's new character's name
                render_charselect(framebuffer, *scene, csu, now_ms);
                if (!lobby.seats.empty()) lobby.draw(framebuffer, *scene);
                break;
            }
            case Screen::InGame: {
                if (session.coop_typing()) {                        // a joining pad's new character's name
                    session.coop_text(text_this_frame, backspace_this_frame);
                    text_this_frame.clear();
                }
                session.typed = text_this_frame;
                // The act palette follows the player's level: its Levels.txt Pal (Level::palette_act).
                if (const int act = session.view.level ? session.view.level->palette_act() : session.view_act(); act != scene->palette_act)
                    load_act_palettes(*scene, scene->mpqs, act);
                session.update(framebuffer, mouse, keys_this_frame, screen, audio, now_ms, last_ms);
                // A panel key moved the cursor with the view (FUN_00468770's SetCursorPos):
                // the game's copy, and the OS cursor unless a pad drives it.
                if (const auto moved = std::exchange(session.cursor_move, std::nullopt)) {
                    mouse.x = moved->first; mouse.y = moved->second;
                    if (!pad_in_use) {
                        float window_x = 0, window_y = 0;
                        SDL_RenderCoordinatesToWindow(win.renderer, float(mouse.x) + float(win.view_w - int(kScreenWidth)) / 2, float(mouse.y), &window_x, &window_y);
                        SDL_WarpMouseInWindow(win.window, window_x, window_y);
                    }
                }
                write_save_back();
                // S->C 0x61 (FUN_00482ef0): a cinematic now (its table 0x721d9c: 1..7 the
                // Cinematics menu's videos), seen from then on; back into the game after.
                if (const int movie = std::exchange(session.world.movie, -1); movie >= 1 && movie <= int(kCinematicVideo.size()) && g_video
                    && video_mpqs.contains(kCinematicVideo[std::size_t(movie - 1)])) {
                    static constexpr std::array<const char*, 7> kSeen{ "d2intro", "act2start", "act3start", "act4start", "act4end", "d2xintro", "d2xout" };
                    if (read_seen().find(kSeen[std::size_t(movie - 1)]) == std::string::npos) {
                        std::error_code error;
                        fs::create_directories(g_user_dir, error);
                        std::ofstream(g_user_dir / "cinematics_seen", std::ios::app) << kSeen[std::size_t(movie - 1)] << '\n';
                    }
                    video_queue = { kCinematicVideo[std::size_t(movie - 1)] };
                    // LoD's 5 (DAT_007bc9d8): ACT04END, then D2X_INTRO.
                    if (movie == 5 && session.hero.character.header.expansion() && video_mpqs.contains(kCinematicVideo[5])) {
                        video_queue.emplace_back(kCinematicVideo[5]);
                        if (read_seen().find(kSeen[5]) == std::string::npos) std::ofstream(g_user_dir / "cinematics_seen", std::ios::app) << kSeen[5] << '\n';
                    }
                    video_return = Screen::InGame;
                    screen = Screen::Video;
                }
                if (session.net_game && session.net_game->closed()) {        // the host's gone: back to the characters, as game.exe's drop
                    d2d::log::warn("net: lost the host; leaving the game");
                    screen = Screen::CharSelect;
                }
                if (screen == Screen::CharSelect && session.net_game) {   // a joined game: leave the host, saving on again
                    session.net_game->leave();
                    write_save_back();
                    session.net_game.reset();
                    session.net_monsters.clear();
                    session.world.fight.remote_monsters = false;
                    session.world.hosted_quests = false;
                    session.world.host_warps = false;
                    session.world.characters = g_no_save ? nullptr : &characters;
                }
                if (session.lan_client && !session.lan_lost.empty()) {        // the d2d host's gone: back to the characters
                    d2d::log::warn("lan: {}; leaving the game", session.lan_lost);
                    csu.popup = join_error_text(*scene, { .what = session.lan_lost, .no_connect = true }, character.expansion);
                    csu.popup_return = Screen::CharSelect;
                    session.lan_client.reset();
                    screen = Screen::CharSelect;
                }
                if (screen == Screen::CharSelect && (session.lan_client || session.lan_store || session.lan_host)) {   // a LAN game left
                    session.lan_client.reset();
                    session.lan_lost.clear();
                    session.lan_resyncing = false;
                    session.lan_views = 0;
                    if (session.lan_store) session.world.characters = g_no_save ? nullptr : &characters;
                    session.lan_store = nullptr;
                    session.lan_host.reset();                         // the joiners get Lost (their saves went at ClientSession::save)
                    session.lan_players.clear();
                }
                if (screen == Screen::CharSelect && scene) {         // left the game (saved): the roster again,
                    load_saves(*scene, save_dir);                      // the character just played first
                    csu.selected = scene->saves.empty() ? -1 : 0;
                    csu.scroll = 0;
                }
                // Classic, Diablo dead with bit 13 (DAT_007a0604): leaving plays
                // ACT04END, then the endgame screen (FUN_0044f360 -> FUN_00482ef0);
                // after the leave and the roster reload above, so neither is skipped.
                if (screen != Screen::InGame && std::exchange(session.world.end_movie, false)) {
                    video_queue = { kCinematicVideo[4] };
                    video_return = Screen::Endgame;
                    screen = Screen::Video;
                }
                break;
            }
            case Screen::CharCreate: {
                character.appearance.reset();   // a new character wears starting gear
                character.items.clear();
                character.stats = {};
                // Text input into the 15-character name buffer, then the
                // create screen's own OK-button gating (FUN_00430620).
                for (const char letter : text_this_frame) {
                    if (d2d::d2s::character_name_allows(character.name, letter) && character.name.size() < 15)
                        character.name.push_back(letter);
                }
                if (backspace_this_frame && !character.name.empty())
                    character.name.pop_back();

                character.ok_btn.do_switch = character.selected >= 0 && d2d::d2s::valid_character_name(character.name);
                update_button(character.cancel_btn, mouse, screen, quit);
                update_button(character.ok_btn,     mouse, screen, quit);
                if (!character.cancel_btn.hovered && !character.ok_btn.hovered && scene) {
                    const int previous = character.selected;
                    handle_charcreate_click(character, *scene, mouse, now_ms);
                    if (character.selected != previous) {
                        if (previous >= 0) {
                            const int cls = kUiToSaveClass[std::size_t(previous)];
                            audio.play_sfx(*scene, d2d::rules::sound_ids::kClassDeselect[std::size_t(cls)], 1.f, 0);
                        }
                        if (character.selected >= 0) {
                            const int cls = kUiToSaveClass[std::size_t(character.selected)];
                            audio.play_sfx(*scene, d2d::rules::sound_ids::kClassSelect[std::size_t(cls)], 1.f, 0);
                        }
                    }
                }
                // OK: a new character (CharStats.txt's start), saved at once so
                // it's on the roster. A name that has a save already is refused.
                if (screen == Screen::InGame && scene) {
                    if (fs::exists(characters.path(character.name))) {
                        d2d::log::info("a character named {} exists already", character.name);
                        screen = Screen::CharCreate;
                    } else {
                        static_cast<Character&>(character) = made_character(*scene, kUiToSaveClass[std::size_t(std::max(character.selected, 0))],
                                                                            character.name, character.hardcore, character.expansion, session.rng);
                        set_map_seed(*scene, game_seed(character.header), character.header.active_difficulty());
                        session.enter();
                        session.save();
                        load_saves(*scene, save_dir);
                    }
                }
                advance_char_states(character, *scene, now_ms);
                render_charcreate(framebuffer, *scene, character, now_ms);
                break;
            }
            }
            // D2's own cursor, drawn into the frame so it scales with the
            // game (the OS pointer is hidden). DC6 frames anchor bottom-
            // left, which puts the fingertip on the hotspot. Palette of
            // the screen underneath.
            // The hand plays CursorAnim's row and frame (FUN_004683c0 draws
            // row 0x7a6adc at frame 0x7a6ae0 >> 8, then FUN_00468310 steps it).
            // The mouse's press, release and moves reach the hand whether it's drawn or not (a held
            // item, an identify pick, a pad's field mode), so a release then can't leave it pressed.
            if (mouse.press_this_frame) cursor_anim.press(now_ms);
            if (mouse.release_this_frame) cursor_anim.release(now_ms);
            if (mouse.x != cursor_seen.first || mouse.y != cursor_seen.second) cursor_anim.move(now_ms);
            cursor_seen = { mouse.x, mouse.y };
            const int cursor_dx = (fb_width(framebuffer) - int(kScreenWidth)) / 2;
            bool os_cursor = false;   // the OS shows the gauntlet this frame (hw_cursor.hpp)
            if (screen == Screen::InGame && session.held && !session.use_item.placed()) {   // placed in an item panel: the hand (FUN_00468070(0))
                draw_held(framebuffer, *scene, *session.held, mouse.x + cursor_dx, mouse.y);
            } else if (screen == Screen::InGame && session.identify_with >= 0 && scene->cursor_buysell.frames_per_direction() > 0) {
                // An identify pick: cursor state 6, buysell's frame = Books.txt
                // SpellIcon (0; S->C 0x3f, FUN_00468010), bottom 0x21 below the
                // hotspot (FUN_00468460).
                const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
                const auto& frame = scene->cursor_buysell.frame(0, 0);
                blit_sprite(framebuffer, frame, pal, mouse.x + cursor_dx + frame.offset_x,
                            mouse.y + 0x21 + frame.offset_y - int(frame.height) + 1);
            } else if (screen != Screen::Video && !pad_cursor_hidden && scene->cursor.frames_per_direction() >= 8) {   // hidden over cinematics
                const auto& pal = screen == Screen::InGame
                                      ? (scene->act_pal.entries().empty() ? scene->pal : scene->act_pal)
                                  : screen == Screen::CharCreate ? scene->charselect_pal : scene->pal;
                const auto& row_sprite = scene->cursor_rows[std::size_t(std::clamp(cursor_anim.row, 0, 5))];
                const auto& sheet = row_sprite.frames_per_direction() > std::uint32_t(cursor_anim.frame()) ? row_sprite : scene->cursor;
                const auto& frame = sheet.frame(0, std::uint32_t(&sheet == &row_sprite ? cursor_anim.frame() : 0));
                cursor_anim.tick(now_ms);
                // Co-op with two pads or more: the first player's cursor in their seat's colour.
                const bool seat_colour = coop_game && std::ranges::count_if(gamepads.pads(), [](const Gamepads::Pad& pad) { return !pad.built_in; }) >= 2;
                const auto& lead = kSeatColours[std::size_t(std::clamp(session.focus_colour(), 0, 3))];
                const auto& hand_pal = seat_colour ? tinted(pal, lead[0], lead[1], lead[2]) : pal;
                // The OS cursor carries the plain hand only; a held item or identify pick is
                // drawn into the frame, as game.exe draws every cursor (deviations.md row 19).
                os_cursor = g_hw_cursor && win.pointer_at(mouse.x, mouse.y) && win.show_cursor(frame, hand_pal);
                if (!os_cursor)
                    blit_sprite(framebuffer, frame, hand_pal, mouse.x + cursor_dx + frame.offset_x,
                                mouse.y + frame.offset_y - int(frame.height) + 1);
            }
            if (!os_cursor) win.hide_cursor();
        } else {
            paint_test_pattern(framebuffer);
        }

        const std::uint32_t t_after_render = std::uint32_t(SDL_GetTicks());
        // Skip GPU work when the window is minimized — Metal's swapchain
        // stalls if we keep pushing frames to a hidden drawable, which
        // is the classic macOS beachball trigger for SDL apps that
        // don't gate render on window visibility.
        const auto wflags = SDL_GetWindowFlags(win.window);
        if (!(wflags & SDL_WINDOW_MINIMIZED)) {
            current_phase.store(std::uint32_t(MainPhase::Upload),
                                std::memory_order_relaxed);
            SDL_UpdateTexture(win.texture, nullptr, shown_frame(framebuffer).data(), fb_width(framebuffer) * 4);
            SDL_RenderClear(win.renderer);
            SDL_RenderTexture(win.renderer, win.texture, nullptr, nullptr);
            current_phase.store(std::uint32_t(MainPhase::Present),
                                std::memory_order_relaxed);
            SDL_RenderPresent(win.renderer);
            crash_frame.update(framebuffer);
        }
        const std::uint32_t t_after_present = std::uint32_t(SDL_GetTicks());
        if (++frame_count == 1) d2d::log::info("First frame presented at {} ms after launch.", d2d::log::ms());
        last_ms = now_ms;
        // Per-frame diagnostics — break the frame into `input` (SDL event
        // pump + devctl; macOS blocks in here during window drags / focus
        // changes), `render` (our CPU blits into the framebuffer) and
        // `present` (SDL upload + present, where Metal can stall). `render`
        // used to start at frame_start and silently include `input`, which
        // is how event-loop stalls showed up as 143-541 ms "render" spikes
        // that never reproduce as render work. The 5s alive line prints
        // input max plus render/present avg + max and the camera position.
        const std::uint32_t dt_input   = t_after_input   - frame_start_ms;
        const std::uint32_t dt_render  = t_after_render  - t_after_input;
        const std::uint32_t dt_present = t_after_present - t_after_render;
        static std::uint32_t stat_input_max = 0;
        static std::uint32_t stat_frames = 0;
        static std::uint32_t stat_render_sum = 0, stat_render_max = 0;
        static std::uint32_t stat_present_sum = 0, stat_present_max = 0;
        static std::uint32_t stat_last_report_ms = 0;
        ++stat_frames;
        stat_render_sum  += dt_render;
        stat_present_sum += dt_present;
        if (dt_input   > stat_input_max)   stat_input_max   = dt_input;
        if (dt_render  > stat_render_max)  stat_render_max  = dt_render;
        if (dt_present > stat_present_max) stat_present_max = dt_present;
        // Any single phase > 100ms is a stall candidate — log it with
        // whichever phase spiked so we can tell CPU-side from GPU-side.
        if (dt_input > 100 || dt_render > 100 || dt_present > 100) {
            d2d::log::warn(
                "slow frame: input={} ms render={} ms present={} ms screen={} cam=({},{})",
                dt_input, dt_render, dt_present, int(screen), int(session.player.x), int(session.player.y));
        }
        if (now_ms - stat_last_report_ms >= 5000) {
            const std::uint32_t avg_r = stat_frames ? stat_render_sum  / stat_frames : 0;
            const std::uint32_t avg_p = stat_frames ? stat_present_sum / stat_frames : 0;
            d2d::log::info(
                "alive: {} frames/5s | input max={} | render avg={} max={} | present avg={} max={} | screen={} cam=({},{})",
                stat_frames, stat_input_max, avg_r, stat_render_max, avg_p, stat_present_max,
                int(screen), int(session.player.x), int(session.player.y));
            stat_frames = 0;      stat_input_max = 0;
            stat_render_sum = 0;  stat_render_max = 0;
            stat_present_sum = 0; stat_present_max = 0;
            stat_last_report_ms = now_ms;
        }
        current_phase.store(std::uint32_t(MainPhase::PaceDelay),
                            std::memory_order_relaxed);
        pace_frame(frame_start_ns, frame_budget(win.window));
    }
    if (screen == Screen::InGame) session.save();   // quitting from the game saves it
    if (session.net_game) { session.net_game->leave(); write_save_back(); }   // a joined game: leave the host (0x69), don't just drop it
    // Shut the watchdog down cleanly so it doesn't outlive SDL_Quit()
    // and touch stale pointers.
    watchdog_stop.store(true, std::memory_order_relaxed);
    watchdog.join();
    g_current_phase_ptr = nullptr;
    gamepads.close();   // before SDL goes away, not at scope exit
    SDL_Quit();
    return 0;
}

}  // namespace

// Ctrl-C / kill (TERM) plumbing. std::signal handlers need C linkage
// and can only touch objects with `sig_atomic_t` semantics — hence
// the raw volatile int rather than a std::atomic<bool>. Every main
// loop polls this each iteration and treats it as a `quit` request
// identical to SDL_EVENT_QUIT.
//
// Double-tap escape hatch: a SECOND SIGINT/SIGTERM before the loop
// notices the first calls _exit() unconditionally. This exists for
// the exact scenario Bret hit — the main thread is beach-balled in
// SDL_RenderPresent, our polled quit flag never gets checked, but
// hammering Ctrl-C still gets you out without needing `kill -9`.
volatile std::sig_atomic_t g_sigint_quit  = 0;
volatile std::sig_atomic_t g_sigint_count = 0;
extern "C" void d2d_sigint_handler(int) {
    g_sigint_quit = 1;
    g_sigint_count = g_sigint_count + 1;
    if (g_sigint_count >= 2) _exit(130);
}

/**
 * Parse launcher/config options and start the game with the selected local data and input settings.
 * Touch controls are opt-in through the flag or saved configuration, on by default on Android and iOS.
 */
int main(int argc, char** argv) {
#ifdef __ANDROID__
    // SDL calls this on its Java thread, with Android's 1 MB stack; this
    // frame and load_scene's take 1.1 MB. Run again on a thread of 16 MB.
    static thread_local bool on_own_stack = false;
    if (!on_own_stack) {
        struct Call { int argc; char** argv; int result; } call{argc, argv, 1};
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, 16u << 20);
        pthread_t thread;
        const auto body = [](void* raw) -> void* {
            auto* const pending = static_cast<Call*>(raw);
            on_own_stack = true;
            pending->result = SDL_main(pending->argc, pending->argv);
            return nullptr;
        };
        if (pthread_create(&thread, &attributes, body, &call) != 0) return 1;
        pthread_join(thread, nullptr);
        return call.result;
    }
#endif
    // Ctrl-C and SIGTERM set the loop-quit flag instead of terminating
    // mid-frame. SIGPIPE gets ignored so a closed devctl client doesn't
    // kill the game.
    std::signal(SIGINT,  d2d_sigint_handler);
    std::signal(SIGTERM, d2d_sigint_handler);
#ifdef SIGPIPE                                        // POSIX only; Windows has no SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
#ifdef _WIN32
    // Warnings in colour: escape codes only work once VT processing is on.
    if (const HANDLE console = GetStdHandle(STD_ERROR_HANDLE); console != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        d2d::log::set_windows_vt(GetConsoleMode(console, &mode) && SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING));
    }
#endif

#ifdef __ANDROID__
    // No HOME there: the app's own external folder
    // (Android/data/<package>/files), which needs no permission and which
    // adb can fill (docs/android.md).
    if (const char* files = SDL_GetAndroidExternalStoragePath(); files && *files) setenv("D2D_USER_DIR", files, 0);
#endif
    // Per-user dir, thirdeye layout (components/userdir): d2d.cfg, save/,
    // screenshots/. Config loads global -> ./ -> user, later wins.
    const fs::path user_dir = d2d::userdir::user_dir("d2d");
    const fs::path save_dir = user_dir / "save";
    const fs::path shot_dir = user_dir / "screenshots";
    std::error_code mk_ec;
    fs::create_directories(save_dir, mk_ec);
    fs::create_directories(shot_dir, mk_ec);
    d2d::log::open(user_dir / "d2d.log");
    d2d::log::info("d2d {} — Diablo II re-implementation", d2d::build::kVersion);
    d2d::log::info("  Git hash: {}", d2d::build::kGitHash);
    d2d::log::info("  Branch: {}", d2d::build::kGitBranch);
    d2d::log::info("  Tag: {}", d2d::build::kGitTag);
    d2d::log::info("  Built: {}", d2d::build::kDate);
    d2d::log::info("  User dir: {}", user_dir.string());
    d2d::userdir::Config cfg;
    for (const auto& dir : { d2d::userdir::global_dir("d2d"), fs::path("."), user_dir })
        if (fs::exists(dir / "d2d.cfg")) {
            d2d::userdir::load_cfg(dir / "d2d.cfg", cfg);
            d2d::log::info("  Config: {}", (dir / "d2d.cfg").string());
        }

    std::string devctl_path;
    fs::path    data_dir;
    bool        headless = false;
    bool        send_crash_reports = false;
    bool        enable_crash_test = false;
    std::string start_screen;   // "title" | "credits" | "charcreate" | "ingame"
    int         start_class = -1;
    std::string start_name;
    bool        start_hardcore = false;
    bool        start_classic = false;
    bool        no_save = false;
    std::uint32_t map_seed = std::random_device{}();   // --seed fixes it (3 puts the Blood Moor east, townE1)

    CLI::App app{"d2d — Diablo II re-implementation"};
    app.add_option("--seed", map_seed, "Map seed (act 1 layout and the Blood Moor)");
    app.add_option("--devctl", devctl_path,
                   "Unix-socket dev-control channel path");
    std::string data_dir_str;
    int scale = cfg.contains("scale") ? std::atoi(cfg["scale"].c_str()) : 1;
    int view_width = 0;
    bool cli_widescreen = false;
    app.add_option("--scale", scale, "Window scale (game renders at 800x600)")
        ->check(CLI::Range(1, 8));
    app.add_option("--view-width", view_width,
                   "Widescreen game view's width, 800.." + std::to_string(kMaxViewWidth) + " (else it follows the window's aspect; also `widescreen = 1` in d2d.cfg)")
        ->check(CLI::Range(int(kScreenWidth), kMaxViewWidth));
    app.add_flag  ("--widescreen", cli_widescreen,
                   "Widen the in-game view to the window's aspect (menus stay 800x600)");
    app.add_option("--data", data_dir_str,
                   "D2 MPQ directory (else $D2_MPQ_DIR, d2data.mpq beside d2d or in ./, d2d.cfg data =)");
    auto* touch_flag = app.add_flag("--touch-controls", g_touch_controls, "Show the touch control prototype");
    app.add_flag  ("--headless", headless,
                   "Run without opening a window");
    app.add_flag  ("--send-crash-reports", send_crash_reports,
                   "Enable crash reporting (launcher opt-in)");
    app.add_flag  ("--enable-crash-test", enable_crash_test,
                   "Enable the devctl crash-test command (requires crash reporting)");
    app.add_option("--start-screen", start_screen,
                   "Jump directly to a screen at startup")
        ->check(CLI::IsMember({"title", "credits", "charselect", "charcreate", "ingame", "video", "othermultiplayer", "tcpip", "endgame"}));
    app.add_option("--start-class", start_class,
                   "Preselect a class index (0..6)")
        ->check(CLI::Range(0, 6));
    app.add_option("--start-name", start_name,
                   "Preload character name; on charselect, pick that save");
    app.add_flag  ("--start-hardcore", start_hardcore,
                   "Preload the Hardcore checkbox");
    app.add_flag  ("--start-classic", start_classic,
                   "Preload a classic (not Expansion) character");
    app.add_flag  ("--no-save", no_save,
                   "Never write character saves (scripted tests)");
    bool no_video = false;
    // d2d's own changes to game.exe (deviations.md), each on by default:
    // --toggle trans_roof=off,... Names in kToggles.
    std::string toggles;
    app.add_option("--toggle", toggles, "Turn d2d's deviations on/off: name=on|off[,...] (trans_roof, autoloot, autoparty, pad_controls, click_keys, coop_lobby, light_grid, splash_centre, unit_slide, hwcursor, dismiss_effects, touch_classic_hud); scalemode=pixelart|nearest|linear");
    app.add_flag  ("--no-video", no_video, "Skip the startup cinematics");
    int fps_cap = cfg.contains("fps_cap") ? std::atoi(cfg["fps_cap"].c_str()) : 0;
    app.add_option("--fps-cap", fps_cap, "Frames a second at most, 0 = the display's refresh (also `fps_cap =` in d2d.cfg)")
        ->check(CLI::Range(0, 1000));
    std::string join_host, game_exe, log_level = "info";
    app.add_option("--log-level", log_level, "error, warning, info, debug or trace");
    app.add_option("--join", join_host, "Join a game.exe TCP/IP game at this address (your own network only)");
    int lan_host_port = 0;
    std::string lan_connect;
    app.add_option("--connect", lan_connect, "Join a d2d game hosted with --host at this address (your LAN)");
    app.add_option("--host", lan_host_port, "Host this game for d2d players on your LAN, on this port (6113 is the usual)")->check(CLI::Range(1, 65535));
    app.add_option("--game-exe", game_exe, "Your 1.14d game.exe, for --join (else $D2_GAME_EXE, game.exe or bin/game.exe beside the MPQs)");
    int start_cam_x = -1, start_cam_y = -1;
    app.add_option("--start-cam-x", start_cam_x,
                   "InGame camera x (grid cell)");
    app.add_option("--start-cam-y", start_cam_y,
                   "InGame camera y (grid cell)");
    // game.exe's one-dash switches, before CLI11. Two-dash options pass through.
    const std::vector<std::string_view> arguments(argv, argv + argc);
    d2d::switches::Translated switched = d2d::switches::translate(arguments, app);
    std::vector<char*> switched_argv;
    switched_argv.reserve(switched.argv.size());
    for (std::string& argument : switched.argv) switched_argv.push_back(argument.data());
    try {
        app.parse(static_cast<int>(switched_argv.size()), switched_argv.data());
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }
    if (enable_crash_test && (!send_crash_reports || devctl_path.empty())) {
        d2d::log::error("--enable-crash-test requires --send-crash-reports and --devctl <path>");
        return 2;
    }
    if (const auto level = d2d::log::parse_level(log_level)) d2d::log::set_level(*level);
    else { d2d::log::error("--log-level {}: one of error, warning, info, debug, trace", log_level); return 2; }
    for (const std::string& line : switched.ignored) d2d::log::warn("{}", line);
    const auto crash_notes = crash_notes_from_environment();
    SentrySession sentry(send_crash_reports, user_dir, d2d::build::kVersion, crash_notes);
    if (enable_crash_test && !sentry.enabled()) {
        d2d::log::error("--enable-crash-test requires a successful Sentry initialization");
        return 2;
    }
    g_seed_fixed = app.count("--seed") > 0;
    g_map_seed = map_seed;
    // The MPQs: first hit wins, each a hard stop (install-detect.md,
    // decision 5). No guessing: the launcher writes `data =` into d2d.cfg.
    {
        const char* env = std::getenv("D2_MPQ_DIR");
        const char* base = SDL_GetBasePath();
        std::error_code cwd_ec;
#if defined(__vita__) || defined(__SWITCH__) || defined(__ANDROID__)
        if (cfg["data"].empty()) cfg["data"] = user_dir.string();   // the MPQs' folder there (userdir.hpp)
#endif
        const auto found = d2d::install::resolve_data_dir(data_dir_str, env ? env : "", base ? fs::path(base) : fs::path{},
                                                          fs::current_path(cwd_ec), cfg["data"]);
        if (!found.error.empty()) {
            d2d::log::error("{}", found.error);
            return 1;
        }
        data_dir = found.dir;
        d2d::log::info("  Data dir: {} ({})", data_dir.string(), found.from);
    }
    // An explicit patch layer wins over the install's own patch_d2.mpq:
    // $D2_PATCH_INSTALLER (the tests' convention), else `patch =`.
    const char* patch_env = std::getenv("D2_PATCH_INSTALLER");
    const fs::path patch_layer = patch_env && *patch_env ? fs::path(patch_env) : fs::path(cfg["patch"]);

    std::vector<std::uint8_t> framebuffer(std::size_t(kScreenWidth) * kScreenHeight * 4, 0);
    for (std::size_t i = 3; i < framebuffer.size(); i += 4) framebuffer[i] = 0xFF;
    auto scene = load_scene(data_dir, patch_layer);   // the menus' part; nullopt if the MPQs won't load
    // Nothing to run without them: say so (the log has which MPQ and why)
    // rather than go on to a game with no data.
    if (!scene) {
        const auto message = std::format("Couldn't load the Diablo II game data from\n{}\n\n"
                                         "Check that its MPQ files are complete, readable copies from Diablo II. "
                                         "The log has the details:\n{}",
                                         data_dir.string(), (user_dir / "d2d.log").string());
        if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "d2d", message.c_str(), nullptr);
        return 1;
    }
    // The rest loads while the title shows (run_windowed waits before the menus).
    std::future<void> loading;
    if (scene) loading = std::async(std::launch::async, [&scene, save_dir, map_seed] { finish_scene(*scene, save_dir, map_seed, game_seed); });

    std::atomic<std::uint64_t> frame_count{0};
    std::atomic<bool>          quit{false};

    d2d::devctl::Channel channel;
    channel.on("info", [&](const std::vector<std::string>&) {
        return "w=" + std::to_string(fb_width(framebuffer)) + " h=" + std::to_string(kScreenHeight)
             + " frame=" + std::to_string(frame_count.load()) + "\nok\n";
    });
    channel.on("screenshot", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err screenshot <path>\n");
        // Relative paths land in the user screenshots dir.
        const fs::path out = fs::path(args[1]).is_relative() ? shot_dir / args[1]
                                                             : fs::path(args[1]);
        const auto written = d2d::screenshot::save_png(out, shown_frame(framebuffer), std::uint32_t(fb_width(framebuffer)), kScreenHeight);   // as shown: ramped
        return "ok " + std::to_string(written) + "\n";
    });
    channel.on("quit", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("ok\n");
    });
    channel.on("crash-test", [&](const std::vector<std::string>& args) {
        if (!enable_crash_test || !sentry.enabled())
            return std::string("err crash testing is disabled; start with --send-crash-reports and --enable-crash-test\n");
        if (args.size() != 2 || args[1] != "CONFIRM_CRASH")
            return std::string("err usage: crash-test CONFIRM_CRASH\n");
        d2d::log::warn("Triggering the explicitly requested Sentry crash test.");
#ifdef D2D_WITH_SENTRY
        sentry_set_tag("crash_test", "true");
        sentry_crash();
#endif
        return std::string("err Sentry crash test unexpectedly returned\n");
    });
    channel.listen(devctl_path);

    g_start_screen   = start_screen;
    g_video          = !no_video && cfg["video"] != "0";
    {
        static const std::pair<std::string_view, bool*> kToggles[] = {
            { "trans_roof", &g_roof_cutout },   // the see-through circle in roofs round the player
            { "dismiss_effects", &g_dismiss_effects }, // optional, validated effect dismissal
            { "autoloot", &g_autoloot },        // gold walked over goes into the purse
            { "autoparty", &g_autoparty },      // a joined game: invite the others, accept their invites
            { "pad_controls", &g_pad_controls },   // Configure Controls' gamepad page
            { "click_keys", &g_click_keys },       // Attack / Use and Right-hand Skill on more keys (Mouse 3..5)
            { "coop_lobby", &g_coop_lobby },       // SINGLE PLAYER / COOP, partners pick their characters on character select
            { "light_grid", &g_light_grid },       // the light grid over the whole view, not game.exe's 48 x 48
            { "splash_centre", &g_splash_centre }, // rain splashes and mud bubbles centred on their water
            { "unit_slide", &g_unit_slide },       // units slide between ticks, not step at 25 Hz
            { "hwcursor", &g_hw_cursor },          // the gauntlet as the OS cursor, not drawn into the frame
            { "touch_classic_hud", &d2d::client::touch::g_touch_classic_hud },   // touch: keep the PC bottom bar on the field
        };
        if (const auto mode = scale_mode_named(cfg["scalemode"])) g_scale_mode = *mode;
        std::string_view rest = toggles;
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const auto item = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            const auto equals_at = item.find('=');
            const auto name = item.substr(0, equals_at), value = equals_at == std::string_view::npos ? std::string_view("on") : item.substr(equals_at + 1);
            if (name == "scalemode") {
                if (const auto mode = scale_mode_named(value)) g_scale_mode = *mode;
                else d2d::log::warn("--toggle: unknown '{}' (scalemode=pixelart|nearest|linear)", std::string(item));
                continue;
            }
            const auto toggle = std::ranges::find(kToggles, name, &std::pair<std::string_view, bool*>::first);
            if (toggle == std::end(kToggles) || (value != "on" && value != "off")) {
                d2d::log::warn("--toggle: unknown '{}' (known: trans_roof, autoloot, autoparty, pad_controls, click_keys, coop_lobby, light_grid, splash_centre, unit_slide, hwcursor, dismiss_effects, touch_classic_hud; values on|off; scalemode=pixelart|nearest|linear)", std::string(item));
                continue;
            }
            *toggle->second = value == "on";
        }
    }
    g_user_dir       = user_dir;
    g_join_host      = join_host;
    g_cli_join       = join_host;
    g_last_tcp_ip    = cfg["last_tcp_ip"];
    g_lan_connect = lan_connect;
    g_lan_host_port = lan_host_port;
    g_cli_lan_connect = lan_connect;
    g_cli_lan_host_port = lan_host_port;
    if (!game_exe.empty()) g_game_exe = game_exe;
    else if (const char* env = std::getenv("D2_GAME_EXE")) g_game_exe = env;
    else g_game_exe = fs::exists(data_dir / "game.exe") ? data_dir / "game.exe" : data_dir / "bin" / "game.exe";
    // Straight into a game needs a class: the Barbarian unless one's given.
    g_start_class    = start_class >= 0 ? start_class : g_start_screen == "ingame" ? 0 : -1;
    g_start_name     = start_name;
    g_start_hardcore = start_hardcore;
    g_start_classic  = start_classic;
    g_no_save        = no_save;
    if (cfg["touch_controls"] == "1") g_touch_controls = true;
    else if (cfg["touch_controls"] == "0" && touch_flag->count() == 0) g_touch_controls = false;
    g_start_cam_x    = start_cam_x;
    g_start_cam_y    = start_cam_y;
    g_scale          = std::clamp(scale, 1, 8);   // cfg value isn't CLI-checked
    g_fps_cap        = std::clamp(fps_cap, 0, 1000);
    g_fullscreen     = cfg["fullscreen"] == "1";
    g_widescreen     = cfg["widescreen"] == "1" || cli_widescreen || view_width > 0;
    g_forced_view_w  = std::clamp(view_width, 0, kMaxViewWidth);
    if (cfg.contains("master_volume")) g_master_volume = std::clamp(std::atoi(cfg["master_volume"].c_str()), 0, 100);
    {
        std::vector<std::string> problems;
        g_pad_config = pad::load_config([&](const char* key) { const auto found = cfg.find(key); return found == cfg.end() ? nullptr : &found->second; }, problems);
        g_click_bindings = keys::click_load(cfg["click_attack"], cfg["click_skill"]);
        for (const auto& problem : problems) d2d::log::warn("d2d.cfg: {}", problem);
    }
    {
        static constexpr std::array<std::pair<const char*, int>, 6> kAutomapKeys = { { { "automap_mode", 1 }, { "automap_fade", 3 }, { "automap_center", 1 },
                                                                                       { "automap_party", 1 }, { "automap_names", 1 }, { "automap_left", 1 } } };
        for (std::size_t option = 0; option < kAutomapKeys.size(); ++option)
            if (cfg.contains(kAutomapKeys[option].first)) g_automap_options[option] = std::clamp(std::atoi(cfg[kAutomapKeys[option].first].c_str()), 0, kAutomapKeys[option].second);
    }
    if (cfg.contains("music_volume")) g_music_volume = std::clamp(std::atoi(cfg["music_volume"].c_str()), 0, 100);
    if (cfg.contains("gamma")) g_gamma = std::clamp(std::atoi(cfg["gamma"].c_str()), d2d::palette::kGammaMin, d2d::palette::kGammaMax);
    if (cfg.contains("contrast")) g_contrast = std::clamp(std::atoi(cfg["contrast"].c_str()), d2d::palette::kContrastMin, d2d::palette::kContrastMax);
    d2d::palette::Palette::set_ramp(g_gamma, g_contrast);

    if (headless) {
        if (!channel.active()) {
            d2d::log::info("--headless with no --devctl has nothing to do. exiting.");
            return 0;
        }
        // Same loop as the windowed build on SDL's dummy video driver +
        // software renderer: no window, no GL, but events, devctl and
        // screenshots all work. (The "offscreen" driver needs EGL, which
        // macOS doesn't have.)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (!std::getenv("ALSOFT_DRIVERS")) SDL_setenv_unsafe("ALSOFT_DRIVERS", "null", 1);   // openal-soft's silent backend
    }
    return run_windowed(framebuffer, scene, loading, save_dir, channel, frame_count, quit);
}
