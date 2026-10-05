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

#include "audio.hpp"
#include "common.hpp"
#include "cursor.hpp"
#include "devctl_verbs.hpp"
#include "frontend.hpp"
#include "load.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "town.hpp"
#include "ui.hpp"
#include "video.hpp"
#include "watchdog.hpp"
#include "window.hpp"
#include "world_view.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <devctl.hpp>
#include <install.hpp>
#include <join.hpp>
#include <log.hpp>
#include <mpq.hpp>
#include <screenshot.hpp>
#include <userdir.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace d2d::client;

namespace {

// Set by main() before entering the loop — a lazy way to plumb --start-*
// through without threading extra parameters everywhere.
static std::string g_start_screen;
static bool        g_video = true;          // startup cinematics (--no-video / cfg video = 0)
static fs::path    g_user_dir;
static int         g_master_volume = 100, g_music_volume = 50;   // d2d.cfg master_volume / music_volume (the game menu sets them)
static int         g_start_class = 0;
static std::string g_start_name;
static bool        g_start_hardcore = false;
static bool        g_no_save = false;         // --no-save: nothing is written (scripted tests)
static int         g_start_cam_x = -1;   // -1 = "use map center"
static int         g_start_cam_y = -1;
// A game's map seed (FUN_0052c280): a fixed one if given, else the save's
// own when it was last played on this difficulty (single player,
// FUN_0056a090), else a new one — the random one this run started with.
// ponytail: game.exe takes a saved 0 too; d2d's early saves hold 0, so 0
// means none here.
static std::string   g_join_host;           // --join: a game.exe TCP/IP host to join
static fs::path      g_game_exe;            // --game-exe: its tables (the codec's, read at runtime)
static bool          g_seed_fixed = false;  // --seed
static std::uint32_t g_map_seed = 0;
static std::uint32_t game_seed(const d2d::d2s::Header& header) {
    if (!g_seed_fixed && header.difficulty[std::size_t(header.active_difficulty())] & 0x80 && header.map_id) return header.map_id;
    return g_map_seed;
}
static int         g_scale = 1;          // window = game res * g_scale

static Screen parse_screen(std::string_view text) {
    if (text == "credits")    return Screen::Credits;
    if (text == "charselect") return Screen::CharSelect;
    if (text == "charcreate") return Screen::CharCreate;
    if (text == "ingame")     return Screen::InGame;
    if (text == "video")      return Screen::Video;
    if (text == "cinematics") return Screen::Cinematics;
    return Screen::Title;
}

// Frame pacer — hits target FPS via SDL_Delay for whatever's left of the
// budget after render, then a mandatory 1ms floor. Matches D2's own
// pattern (FUN_004f6190 in game.exe): compute time budget remaining,
// Sleep 1..5ms if we're ahead. Without this, an unlucky vsync miss or a
// windowed compositor that skips vsync sends us into a 100% CPU spin.
constexpr std::uint32_t kFrameBudgetMs = 16;   // ~60 fps ceiling
inline void pace_frame(std::uint32_t frame_start_ms) {
    const std::uint32_t elapsed = std::uint32_t(SDL_GetTicks()) - frame_start_ms;
    if (elapsed < kFrameBudgetMs) {
        SDL_Delay(kFrameBudgetMs - elapsed);
    } else {
        // Even when we blew the budget, yield 1ms so we don't monopolise
        // the scheduler.
        SDL_Delay(1);
    }
}

int run_windowed(std::vector<std::uint8_t>& framebuffer,
                 std::optional<Scene>& scene,
                 const fs::path& save_dir,
                 d2d::devctl::Channel& channel,
                 std::atomic<std::uint64_t>& frame_count,
                 std::atomic<bool>& quit) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        d2d::log::error("SDL_Init: {}", SDL_GetError());
        return 1;
    }
    const int sdl_v = SDL_GetVersion();
    d2d::log::info("Initializing SDL... done! SDL {}.{}.{} ({})", SDL_VERSIONNUM_MAJOR(sdl_v),
                   SDL_VERSIONNUM_MINOR(sdl_v), SDL_VERSIONNUM_MICRO(sdl_v), SDL_GetCurrentVideoDriver());
    Window win;
    if (!win.open(int(kScreenWidth), int(kScreenHeight), g_scale)) { SDL_Quit(); return 1; }
    d2d::log::info("  Window: {}x{} (scale {}), renderer {}", kScreenWidth * g_scale, kScreenHeight * g_scale, g_scale,
                   SDL_GetRendererName(win.renderer));
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
        for (const char* name : { "d2xvideo.mpq", "d2video.mpq" })
            if (const auto path = d2d::install::find_file(scene->data_dir, name)) video_mpqs.push(*path);
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
    auto read_seen = [&] {
        std::string seen;
        if (std::ifstream file(g_user_dir / "cinematics_seen"); file) std::getline(file, seen, '\0');
        return seen;
    };
    CinematicsUI cin_ui = scene ? cinematics_ui(*scene, cinematics_unlocked(read_seen())) : CinematicsUI{};
    Screen last_screen = screen;
    Mouse  mouse;
    TitleUI title = scene ? title_ui(*scene) : TitleUI{};

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
    }

    // Char-select UI, from the LoD init (FUN_0043ae30): records 0xa4..0xa6
    // are the tall buttons CREATE NEW / CONVERT TO / DELETE at x 33/233/433,
    // each with a second line set by FUN_00500bf0 (0x5524 "CHARACTER",
    // 0x58ca "EXPANSION"); 0xa2/0xa3 are OK/EXIT (0x13ee / 0x13ed).
    CharSelectUI csu;
    // LoD init (FUN_0043ae30) starts the selection at 0: first character
    // preselected, OK live straight away.
    if (scene && !scene->saves.empty()) csu.selected = 0;
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
    }

    const auto start_ticks = SDL_GetTicks();
    // SDL text input only while CharCreate's name field is up. While it's
    // on, macOS routes every key through the input method (IMK); leaving
    // it on everywhere cost ~100 ms inside SDL_PollEvent on Esc in InGame
    // (logged with "error messaging the mach port for
    // IMKCFRunLoopWakeUpReliable"). It was left on permanently as a
    // beachball suspect; that beachball was the pan-left float loop.
    bool text_active = false;
    Town town(scene ? &*scene : nullptr, character, g_start_cam_x, g_start_cam_y);
    // The characters' saves (character_store.hpp): the World writes through
    // it when the player leaves the game or quits; the roster is read again.
    const CharacterStore characters{ save_dir, scene && scene->item_tables ? &*scene->item_tables : nullptr };
    town.world.characters = g_no_save ? nullptr : &characters;
    town.world.autoloot_gold = g_autoloot;
    if (!g_no_save) town.cfg_file = g_user_dir / "d2d.cfg";
    if (screen == Screen::InGame && scene) {
        // --start-screen ingame: a new character of the class and name given,
        // made as char-create's OK makes one (unsaved until the game saves).
        character.character_class = kUiToSaveClass[std::size_t(std::max(character.selected, 0))];
        auto made = new_character(*scene, character.character_class, character.name.empty() ? std::string("Tester") : character.name,
                                  character.hardcore, character.expansion, town.rng);
        character.header = std::move(made.header);
        character.stats = made.stats;
        character.items = std::move(made.items);
        character.panel = panel_stats(*scene, character.header, character.items, character.stats);
        set_map_seed(*scene, game_seed(character.header));
        town.enter();
    }
    std::array<bool, 8> frontend_played{};   // title-screen ambience picks
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

    // Input, debug and state verbs for scripted tests (devctl_verbs.hpp).
    register_game_verbs(channel, win, screen, csu, character, town, scene, audio);

    while (!quit) {
        // Signal-driven quit — Ctrl-C / SIGTERM. The atomic write from
        // d2d_sigint_handler is polled here; SDL_EVENT_QUIT and window
        // close still work through handle_sdl_events.
        if (g_sigint_quit) { quit = true; break; }
        const std::uint32_t frame_start_ms = std::uint32_t(SDL_GetTicks());
        // Toggled inside the `input` timing window, so any IME cost of the
        // switch itself shows up there.
        if (const bool want = screen == Screen::CharCreate; want != text_active) {
            if (want) SDL_StartTextInput(win.window);
            else      SDL_StopTextInput(win.window);
            text_active = want;
        }
        heartbeat_ms.store(frame_start_ms, std::memory_order_relaxed);

        mouse.press_this_frame = false;
        mouse.release_this_frame = false;
        mouse.rpress_this_frame = false;
        mouse.wheel = 0;
        std::string text_this_frame;
        bool        backspace_this_frame = false;
        std::vector<SDL_Keycode> keys_this_frame;
        current_phase.store(std::uint32_t(MainPhase::PollEvents),
                            std::memory_order_relaxed);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // SDL3 doesn't rescale event coords under logical presentation;
            // convert so the mouse lands in 800x600 game pixels.
            SDL_ConvertEventToRenderCoordinates(win.renderer, &event);
            handle_sdl_events(event, mouse, screen, text_this_frame,
                              backspace_this_frame, keys_this_frame, quit);
        }
        current_phase.store(std::uint32_t(MainPhase::Devctl),
                            std::memory_order_relaxed);
        if (channel.active()) channel.pump();

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
        // Frontend music (FUN_00516250's music thread + FUN_00514990): on
        // the menus, whenever nothing plays, a random not-yet-played track
        // of the 8-track list (the LoD one at 0x72f8b8; classic uses
        // 0x72f878's), each played by path at full volume; all played ->
        // start over (FUN_00514860). Entering a game stops it.
        if (scene && screen != Screen::InGame && screen != Screen::Video && audio.ok && audio.music.sound == 0) {
            static constexpr const char* kFrontendMusic[8] = {
                R"(data\global\music\introedit.wav)", R"(data\global\music\act5\icecaves.wav)",
                R"(data\global\music\act5\xtemple.wav)", R"(data\global\music\act2\desert.wav)",
                R"(data\global\music\act2\sewer.wav)", R"(data\global\music\act3\kurast.wav)",
                R"(data\global\music\act3\kurastsewer.wav)", R"(data\global\music\act4\diablo.wav)" };
            if (std::ranges::all_of(frontend_played, std::identity{})) frontend_played.fill(false);
            int pick = town.rng(8);
            while (frontend_played[std::size_t(pick)]) pick = (pick + 1) % 8;
            frontend_played[std::size_t(pick)] = true;
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
                    if (entry.hovered && mouse.release_this_frame && video_mpqs.contains(kCinematicVideo[std::size_t(i)])) {
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
            case Screen::Title:
                if (prev_screen == Screen::Video) {                 // Esc skipped the videos
                    audio.video_stop();
                    video_playing = false;
                    video_queue.clear();
                }
                for (auto& button : title.buttons) update_button(button, mouse, screen, quit);
                render_title(framebuffer, *scene, title.buttons, now_ms);
                break;
            case Screen::Credits:
                if (mouse.release_this_frame) screen = Screen::Title;
                render_credits(framebuffer, *scene, now_ms);
                break;
            case Screen::CharSelect: {
                const int count = int(scene->saves.size());
                const int max_scroll = charselect_max_scroll(count);
                int rows = -mouse.wheel;   // wheel up = scroll toward the top
                bool play = false;
                if (mouse.press_this_frame) {
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
                if (screen == Screen::InGame) {
                    // Load the picked save into the in-game character.
                    const auto& header = scene->saves[std::size_t(csu.selected)];
                    character.selected   = kSaveClassToUi[header.cls];
                    character.character_class = header.cls;
                    character.name = header.name;
                    character.hardcore   = header.hardcore();
                    character.appearance = header.look();
                    character.items = csu.selected < int(scene->save_items.size())
                                   ? scene->save_items[std::size_t(csu.selected)]
                                   : std::vector<d2d::d2s::Item>{};
                    character.stats = csu.selected < int(scene->save_stats.size())
                                   ? scene->save_stats[std::size_t(csu.selected)] : d2d::d2s::Stats{};
                    character.corpse = csu.selected < int(scene->save_corpses.size())
                                    ? scene->save_corpses[std::size_t(csu.selected)] : std::vector<d2d::d2s::Item>{};
                    character.panel = panel_stats(*scene, header, character.items, character.stats);
                    character.expansion = header.expansion();
                    character.header = header;
                    if (g_join_host.empty()) {
                        set_map_seed(*scene, game_seed(character.header));
                        town.enter();                            // the World takes the character
                    } else {
                        // --join: the host's game, at its map seed. Nothing is saved
                        // from it (the host keeps the character; ponytail: its B3
                        // save-back isn't written either). A failed join stays here.
                        std::ifstream save_file(save_dir / (header.name + ".d2s"), std::ios::binary);
                        std::vector<std::uint8_t> save{ std::istreambuf_iterator<char>(save_file), {} };
                        const auto log_path = g_user_dir / std::format("net-{:%Y%m%dT%H%M%SZ}.log", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
                        d2d::log::info("Joining {} as {} (net log {})", g_join_host, header.name, log_path.string());
                        auto joined = NetGame::join(g_join_host, g_game_exe, std::move(save), log_path, 10000,
                                                    scene->item_tables ? &*scene->item_tables : nullptr);
                        if (!joined) {
                            d2d::log::error("join failed: {}", joined.error());
                            screen = Screen::CharSelect;
                        } else {
                            auto& net = **joined;
                            d2d::log::info("  in {}'s game: act {}, map seed {:#x}, difficulty {}", g_join_host, net.act + 1, net.map_seed, net.difficulty);
                            set_map_seed(*scene, net.map_seed);
                            town.world.characters = nullptr;
                            town.world.fight.remote_monsters = true;
                            town.net_game = std::move(*joined);
                            town.enter();
                            if (const auto* here = town.world.level; here && net.self_x > 0) {
                                town.world.player.x = (net.self_x + 0.5f) / 5.f - float(here->world_x);
                                town.world.player.y = (net.self_y + 0.5f) / 5.f - float(here->world_y);
                                town.publish();
                            }
                            town.net_game->set_running(town.view.running);   // the host starts us walking
                        }
                    }
                }
                render_charselect(framebuffer, *scene, csu, now_ms);
                break;
            }
            case Screen::InGame: {
                town.update(framebuffer, mouse, keys_this_frame, screen, audio, now_ms, last_ms);
                if (screen == Screen::CharSelect && town.net_game) {   // a joined game: leave the host, saving on again
                    town.net_game->leave();
                    town.net_game.reset();
                    town.net_monsters.clear();
                    town.world.fight.remote_monsters = false;
                    town.world.characters = g_no_save ? nullptr : &characters;
                }
                if (screen == Screen::CharSelect && scene) {         // left the game (saved): the roster again,
                    load_saves(*scene, save_dir);                      // the character just played first
                    csu.selected = scene->saves.empty() ? -1 : 0;
                    csu.scroll = 0;
                }
                break;
            }
            case Screen::CharCreate: {
                character.appearance.reset();   // a new character wears starting gear
                character.items.clear();
                character.stats = {};
                // Text input into the name buffer (15-char cap = D2's
                // character-record name limit): letters, and one of ' - _
                // not first, as a game.exe host takes a name (FUN_0052c5b0).
                // unverified (source: the host's check): the create screen's
                // own filter isn't traced.
                for (const char letter : text_this_frame) {
                    const bool alpha = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z');
                    const bool mark = (letter == '\'' || letter == '-' || letter == '_') && !character.name.empty()
                                   && character.name.find_first_of("'-_") == std::string::npos;
                    if ((alpha || mark) && character.name.size() < 15) character.name.push_back(letter);
                }
                if (backspace_this_frame && !character.name.empty())
                    character.name.pop_back();

                // OK is only enabled once a class is picked and a name is
                // entered — mirrors D2's OK-button gating.
                character.ok_btn.do_switch = character.selected >= 0 && d2d::net::valid_join_name(character.name);
                update_button(character.cancel_btn, mouse, screen, quit);
                update_button(character.ok_btn,     mouse, screen, quit);
                if (!character.cancel_btn.hovered && !character.ok_btn.hovered)
                    handle_charcreate_click(character, mouse, now_ms);
                // OK: a new character (CharStats.txt's start), saved at once so
                // it's on the roster. A name that has a save already is refused.
                if (screen == Screen::InGame && scene) {
                    if (fs::exists(characters.path(character.name))) {
                        d2d::log::info("a character named {} exists already", character.name);
                        screen = Screen::CharCreate;
                    } else {
                        character.character_class = kUiToSaveClass[std::size_t(std::max(character.selected, 0))];
                        auto made = new_character(*scene, character.character_class, character.name,
                                               character.hardcore, character.expansion, town.rng);
                        character.header = std::move(made.header);
                        character.stats = made.stats;
                        character.items = std::move(made.items);
                        character.corpse.clear();
                        character.appearance.reset();
                        character.panel = panel_stats(*scene, character.header, character.items, character.stats);
                        set_map_seed(*scene, game_seed(character.header));
                        town.enter();
                        town.save();
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
            // ponytail: frame 0 idle, the closed hand (7) while pressed;
            // D2 plays the grab frames in between.
            if (screen == Screen::InGame && town.held) {
                draw_held(framebuffer, *scene, *town.held, mouse.x, mouse.y);
            } else if (screen == Screen::InGame && town.identify_with >= 0 && scene->cursor_buysell.frames_per_direction() > 0) {
                // An identify pick: cursor state 6, buysell's frame = Books.txt
                // SpellIcon (0; S->C 0x3f, FUN_00468010), bottom 0x21 below the
                // hotspot (FUN_00468460).
                const auto& pal = scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal;
                const auto& frame = scene->cursor_buysell.frame(0, 0);
                blit_sprite(framebuffer, frame, pal, mouse.x + frame.offset_x,
                            mouse.y + 0x21 + frame.offset_y - int(frame.height) + 1);
            } else if (screen != Screen::Video && scene->cursor.frames_per_direction() >= 8) {   // hidden over cinematics
                const auto& pal = screen == Screen::InGame
                                      ? (scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal)
                                  : screen == Screen::CharCreate ? scene->charselect_pal : scene->pal;
                const auto& frame = scene->cursor.frame(0, mouse.down ? 7 : 0);
                blit_sprite(framebuffer, frame, pal, mouse.x + frame.offset_x,
                            mouse.y + frame.offset_y - int(frame.height) + 1);
            }
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
            SDL_UpdateTexture(win.texture, nullptr, framebuffer.data(), int(kScreenWidth * 4));
            SDL_RenderClear(win.renderer);
            SDL_RenderTexture(win.renderer, win.texture, nullptr, nullptr);
            current_phase.store(std::uint32_t(MainPhase::Present),
                                std::memory_order_relaxed);
            SDL_RenderPresent(win.renderer);
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
                dt_input, dt_render, dt_present, int(screen), int(town.player.x), int(town.player.y));
        }
        if (now_ms - stat_last_report_ms >= 5000) {
            const std::uint32_t avg_r = stat_frames ? stat_render_sum  / stat_frames : 0;
            const std::uint32_t avg_p = stat_frames ? stat_present_sum / stat_frames : 0;
            d2d::log::info(
                "alive: {} frames/5s | input max={} | render avg={} max={} | present avg={} max={} | screen={} cam=({},{})",
                stat_frames, stat_input_max, avg_r, stat_render_max, avg_p, stat_present_max,
                int(screen), int(town.player.x), int(town.player.y));
            stat_frames = 0;      stat_input_max = 0;
            stat_render_sum = 0;  stat_render_max = 0;
            stat_present_sum = 0; stat_present_max = 0;
            stat_last_report_ms = now_ms;
        }
        current_phase.store(std::uint32_t(MainPhase::PaceDelay),
                            std::memory_order_relaxed);
        pace_frame(frame_start_ms);
    }
    if (screen == Screen::InGame) town.save();   // quitting from the game saves it
    if (town.net_game) town.net_game->leave();   // a joined game: leave the host (0x69), don't just drop it
    // Shut the watchdog down cleanly so it doesn't outlive SDL_Quit()
    // and touch stale pointers.
    watchdog_stop.store(true, std::memory_order_relaxed);
    watchdog.join();
    g_current_phase_ptr = nullptr;
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

int main(int argc, char** argv) {
    // Ctrl-C and SIGTERM set the loop-quit flag instead of terminating
    // mid-frame. SIGPIPE gets ignored so a closed devctl client doesn't
    // kill the game.
    std::signal(SIGINT,  d2d_sigint_handler);
    std::signal(SIGTERM, d2d_sigint_handler);
#ifdef SIGPIPE                                        // POSIX only; Windows has no SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
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
    d2d::log::info("d2d — Diablo II re-implementation (dev build)");
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
    std::string start_screen;   // "title" | "credits" | "charcreate" | "ingame"
    int         start_class = 0;
    std::string start_name;
    bool        start_hardcore = false;
    bool        no_save = false;
    std::uint32_t map_seed = std::random_device{}();   // --seed fixes it (3 puts the Blood Moor east, townE1)

    CLI::App app{"d2d — Diablo II re-implementation (dev build)"};
    app.add_option("--seed", map_seed, "Map seed (act 1 layout and the Blood Moor)");
    app.add_option("--devctl", devctl_path,
                   "Unix-socket dev-control channel path");
    std::string data_dir_str;
    int scale = cfg.contains("scale") ? std::atoi(cfg["scale"].c_str()) : 1;
    app.add_option("--scale", scale, "Window scale (game renders at 800x600)")
        ->check(CLI::Range(1, 8));
    app.add_option("--data", data_dir_str,
                   "D2 MPQ directory (else $D2_MPQ_DIR, d2data.mpq beside d2d or in ./, d2d.cfg data =)");
    app.add_flag  ("--headless", headless,
                   "Run without opening a window");
    app.add_option("--start-screen", start_screen,
                   "Jump directly to a screen at startup")
        ->check(CLI::IsMember({"title", "credits", "charselect", "charcreate", "ingame", "video"}));
    app.add_option("--start-class", start_class,
                   "Preselect a class index (0..6)")
        ->check(CLI::Range(0, 6));
    app.add_option("--start-name", start_name,
                   "Preload character name");
    app.add_flag  ("--start-hardcore", start_hardcore,
                   "Preload the Hardcore checkbox");
    app.add_flag  ("--no-save", no_save,
                   "Never write character saves (scripted tests)");
    bool no_video = false;
    // d2d's own changes to game.exe (deviations.md), each on by default:
    // --toggle trans_roof=off,... Names in kToggles.
    std::string toggles;
    app.add_option("--toggle", toggles, "Turn d2d's deviations on/off: name=on|off[,...] (trans_roof, autoloot)");
    app.add_flag  ("--no-video", no_video, "Skip the startup cinematics");
    std::string join_host, game_exe, log_level = "info";
    app.add_option("--log-level", log_level, "error, warning, info, debug or trace");
    app.add_option("--join", join_host, "Join a game.exe TCP/IP game at this address (your own network only)");
    app.add_option("--game-exe", game_exe, "Your 1.14d game.exe, for --join (else $D2_GAME_EXE, game.exe or bin/game.exe beside the MPQs)");
    int start_cam_x = -1, start_cam_y = -1;
    app.add_option("--start-cam-x", start_cam_x,
                   "InGame camera x (grid cell)");
    app.add_option("--start-cam-y", start_cam_y,
                   "InGame camera y (grid cell)");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }
    if (const auto level = d2d::log::parse_level(log_level)) d2d::log::set_level(*level);
    else { d2d::log::error("--log-level {}: one of error, warning, info, debug, trace", log_level); return 2; }
    g_seed_fixed = app.count("--seed") > 0;
    g_map_seed = map_seed;
    // The MPQs: first hit wins, each a hard stop (install-detect.md,
    // decision 5). No guessing: the launcher writes `data =` into d2d.cfg.
    {
        const char* env = std::getenv("D2_MPQ_DIR");
        const char* base = SDL_GetBasePath();
        std::error_code cwd_ec;
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
    auto scene = load_scene(data_dir, patch_layer, map_seed);   // nullopt if the MPQs won't load
    if (scene) load_saves(*scene, save_dir);
    if (scene && !scene->saves.empty()) set_map_seed(*scene, game_seed(scene->saves.front()));   // the likely pick's map
    if (scene) want_nearby(*scene, scene->town);        // the Blood Moor builds while the menus run

    std::atomic<std::uint64_t> frame_count{0};
    std::atomic<bool>          quit{false};

    d2d::devctl::Channel channel;
    channel.on("info", [&](const std::vector<std::string>&) {
        return "w=" + std::to_string(kScreenWidth) + " h=" + std::to_string(kScreenHeight)
             + " frame=" + std::to_string(frame_count.load()) + "\nok\n";
    });
    channel.on("screenshot", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err screenshot <path>\n");
        // Relative paths land in the user screenshots dir.
        const fs::path out = fs::path(args[1]).is_relative() ? shot_dir / args[1]
                                                             : fs::path(args[1]);
        const auto written = d2d::screenshot::save_png(out, framebuffer, kScreenWidth, kScreenHeight);
        return "ok " + std::to_string(written) + "\n";
    });
    channel.on("quit", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("ok\n");
    });
    channel.listen(devctl_path);

    g_start_screen   = start_screen;
    g_video          = !no_video && cfg["video"] != "0";
    {
        static const std::pair<std::string_view, bool*> kToggles[] = {
            { "trans_roof", &g_roof_cutout },   // the see-through circle in roofs round the player
            { "autoloot", &g_autoloot },        // gold walked over goes into the purse
        };
        std::string_view rest = toggles;
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const auto item = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            const auto equals_at = item.find('=');
            const auto name = item.substr(0, equals_at), value = equals_at == std::string_view::npos ? std::string_view("on") : item.substr(equals_at + 1);
            const auto toggle = std::ranges::find(kToggles, name, &std::pair<std::string_view, bool*>::first);
            if (toggle == std::end(kToggles) || (value != "on" && value != "off")) {
                d2d::log::warn("--toggle: unknown '{}' (known: trans_roof, autoloot; values on|off)", std::string(item));
                continue;
            }
            *toggle->second = value == "on";
        }
    }
    g_user_dir       = user_dir;
    g_join_host      = join_host;
    if (!game_exe.empty()) g_game_exe = game_exe;
    else if (const char* env = std::getenv("D2_GAME_EXE")) g_game_exe = env;
    else g_game_exe = fs::exists(data_dir / "game.exe") ? data_dir / "game.exe" : data_dir / "bin" / "game.exe";
    g_start_class    = start_class;
    g_start_name     = start_name;
    g_start_hardcore = start_hardcore;
    g_no_save        = no_save;
    g_start_cam_x    = start_cam_x;
    g_start_cam_y    = start_cam_y;
    g_scale          = std::clamp(scale, 1, 8);   // cfg value isn't CLI-checked
    if (cfg.contains("master_volume")) g_master_volume = std::clamp(std::atoi(cfg["master_volume"].c_str()), 0, 100);
    if (cfg.contains("music_volume")) g_music_volume = std::clamp(std::atoi(cfg["music_volume"].c_str()), 0, 100);

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
    return run_windowed(framebuffer, scene, save_dir, channel, frame_count, quit);
}
