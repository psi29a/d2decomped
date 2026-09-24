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
//   --data <dir>      MPQ directory (default: ~/Workspace/private/diablo2)
//   --headless        no window (SDL dummy driver); needs --devctl
//   --scale <n>       window = 800x600 * n, SDL zooms (also `scale` in d2d.cfg)

#include "window.hpp"


namespace {


// Set by main() before entering the loop — a lazy way to plumb --start-*
// through without threading extra parameters everywhere.
static std::string g_start_screen;
static bool        g_video = true;          // startup cinematics (--no-video / cfg video = 0)
static fs::path    g_user_dir;
static int         g_start_class = 0;
static std::string g_start_name;
static bool        g_start_hardcore = false;
static int         g_start_cam_x = -1;   // -1 = "use map center"
static int         g_start_cam_y = -1;
static int         g_scale = 1;          // window = game res * g_scale

static Screen parse_screen(std::string_view s) {
    if (s == "credits")    return Screen::Credits;
    if (s == "charselect") return Screen::CharSelect;
    if (s == "charcreate") return Screen::CharCreate;
    if (s == "ingame")     return Screen::InGame;
    if (s == "video")      return Screen::Video;
    if (s == "cinematics") return Screen::Cinematics;
    return Screen::Title;
}

static const char* screen_name(Screen s) {
    switch (s) {
        case Screen::Title:      return "title";
        case Screen::Credits:    return "credits";
        case Screen::CharSelect: return "charselect";
        case Screen::CharCreate: return "charcreate";
        case Screen::InGame:     return "ingame";
        case Screen::Video:      return "video";
        case Screen::Cinematics: return "cinematics";
    }
    return "?";
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

int run_windowed(std::vector<std::uint8_t>& fb,
                 const std::optional<Scene>& scene,
                 d2d::devctl::Channel& ch,
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
    if (!win.open(int(kW), int(kH), g_scale)) { SDL_Quit(); return 1; }
    d2d::log::info("  Window: {}x{} (scale {}), renderer {}", kW * g_scale, kH * g_scale, g_scale,
                   SDL_GetRendererName(win.r));
    Audio audio;
    audio.init();
    if (scene)
        g_on_button_press = [&] { audio.play_file(audio.ui, *scene, R"(data\global\sfx\cursor\button.wav)"); };
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
        for (const char* n : { "d2xvideo.mpq", "d2video.mpq" })
            if (fs::exists(scene->data_dir / n)) video_mpqs.push(scene->data_dir / n);
    }
    if (scene && !video_mpqs.empty() && g_video && (g_start_screen.empty() || g_start_screen == "video")) {
        video_queue = { R"(Data\Local\Video\New_BLIZ640x480.bik)", R"(Data\Local\Video\BlizNorth640x480.bik)" };
        std::string seen;
        if (std::ifstream in(g_user_dir / "cinematics_seen"); in) std::getline(in, seen, '\0');
        const char* intro = R"(data\local\video\ENG\d2intro640x292.bik)";
        const char* xintro = R"(data\local\video\ENG\D2x_Intro_640x292.bik)";
        std::string mark;
        if (seen.find("d2intro") == std::string::npos && video_mpqs.contains(intro)) {
            video_queue.push_back(intro); mark = "d2intro";
        } else if (seen.find("d2xintro") == std::string::npos && video_mpqs.contains(xintro)) {
            video_queue.push_back(xintro); mark = "d2xintro";
        }
        if (!mark.empty()) {
            std::error_code ec;
            fs::create_directories(g_user_dir, ec);
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
        if (std::ifstream in(g_user_dir / "cinematics_seen"); in) std::getline(in, seen, '\0');
        return seen;
    };
    CinematicsUI cin_ui = scene ? cinematics_ui(*scene, cinematics_unlocked(read_seen())) : CinematicsUI{};
    Screen last_screen = screen;
    Mouse  mouse;
    TitleUI ui = scene ? title_ui(*scene) : TitleUI{};

    // Char-create UI. Positions from RE'd master-table records; labels
    // from string.tbl by ID (0x13ed = EXIT, 0x13ee = OK per record +0x18).
    // OK/EXIT bottom-row buttons are RE'd as records 0x70ade0 and 0x70ae10
    // — the last two entries of the char-select master table, shared
    // with char-create by convention (see char-create-table.md).
    CharCreateUI cc;
    if (scene) {
        auto tbl_label = [&](std::uint16_t id, const char* fallback) {
            if (auto v = lookup_string(*scene, id)) return u16_to_latin1(*v);
            return std::string(fallback);
        };
        cc.cancel_label   = tbl_label(0x13ed, "EXIT");
        cc.ok_label       = tbl_label(0x13ee, "OK");
        cc.hardcore_label = tbl_label(0x1406, "Hardcore");
        cc.cancel_btn = Button{ 33, rec_top(572, 35), 128, 35, cc.cancel_label.c_str(),
                                &scene->medium_sel_button,
                                Screen::CharSelect, /*do_switch=*/true };
        // OK's target is InGame; do_switch flips true per tick once a class
        // is picked AND a name is entered (see the per-frame gate below).
        cc.ok_btn     = Button{ 627, rec_top(572, 35), 128, 35, cc.ok_label.c_str(),
                                &scene->medium_sel_button,
                                Screen::InGame, /*do_switch=*/false };
        // Preload class/name if --start-screen ingame was given.
        if (g_start_class >= 0 && g_start_class < 7) cc.selected = g_start_class;
        if (!g_start_name.empty()) cc.input_name = g_start_name;
        cc.hardcore = g_start_hardcore;
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
            if (auto v = lookup_string(*scene, id)) return u16_to_latin1(*v);
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

    const auto t0 = SDL_GetTicks();
    // SDL text input only while CharCreate's name field is up. While it's
    // on, macOS routes every key through the input method (IMK); leaving
    // it on everywhere cost ~100 ms inside SDL_PollEvent on Esc in InGame
    // (logged with "error messaging the mach port for
    // IMKCFRunLoopWakeUpReliable"). It was left on permanently as a
    // beachball suspect; that beachball was the pan-left float loop.
    bool text_active = false;
    // The player in the InGame world, in DS1 cells (continuous; x.5 is a
    // cell centre). The camera follows them. Seeded to --start-cam-x/y or
    // the middle of the loaded map.
    const bool have_world = scene && !scene->world_dt1s.empty();
    float player_x = (g_start_cam_x >= 0 ? float(g_start_cam_x)
                      : have_world ? float(scene->world_ds1.width() / 2) : 0.f) + 0.5f;
    float player_y = (g_start_cam_y >= 0 ? float(g_start_cam_y)
                      : have_world ? float(scene->world_ds1.height() / 2) : 0.f) + 0.5f;
    // The town start, as game.exe picks it on joining: DS1 special walls
    // (orientation 10/11) with main index 30..33 become the level's spawn
    // list (code at 0x667d09: main 30 sub n -> index n, 31 -> n+5,
    // 32 -> 10, 33 -> 11 (town-portal arrival)); a join asks for index 0,
    // which matches any of group 0 (indices 0..4) at random
    // (FUN_0066ac40), at subtile tile*5+3 (FUN_0061b060), then the nearest
    // free spot. ponytail: first match instead of a random one — each
    // Act 1 town DS1 has exactly one.
    if (have_world && g_start_cam_x < 0 && g_start_cam_y < 0) {
        const auto& m = scene->world_ds1;
        for (const auto& L : m.walls())
            for (std::size_t i = 0; i < L.cells.size(); ++i) {
                const auto& t = L.cells[i];
                if ((t.wall_type == 10 || t.wall_type == 11) && t.style == 30 && t.sequence <= 4) {
                    player_x = (float(i % m.width()) * 5 + 3 + 0.5f) / 5;
                    player_y = (float(i / m.width()) * 5 + 3 + 0.5f) / 5;
                    goto found_start;
                }
            }
    found_start:;
    }
    // Never start inside a tent: search outward, a subtile (0.2 cell) per
    // ring, for the nearest walkable spot.
    if (have_world && scene->blocked(player_x, player_y)) {
        const auto [fx, fy] = [&]() -> std::pair<float, float> {
            for (int r = 1; r < 200; ++r)
                for (int i = -r; i <= r; ++i)
                    for (auto [ox, oy] : { std::pair{i, -r}, {i, r}, {-r, i}, {r, i} })
                        if (!scene->blocked(player_x + ox * 0.2f, player_y + oy * 0.2f))
                            return { player_x + ox * 0.2f, player_y + oy * 0.2f };
            return { player_x, player_y };
        }();
        player_x = fx; player_y = fy;
    }
    std::vector<NpcState> npc_states = scene ? npc_start(*scene) : std::vector<NpcState>{};
    float target_x = player_x, target_y = player_y;
    bool  walking = false;
    bool  running = false;                 // R toggles, like D2's run/walk button
    bool  stash_open = false;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Automap automap;                       // Tab
    Store store;                           // an open vendor store (npc < 0: none)
    WaypointUI waypoint;                   // the waypoint panel
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    std::uint32_t talk_rng = 0x2545f491u;
    std::array<bool, 8> frontend_played{};
    int   hovered_npc = -1;                // world_npcs index under the cursor (last frame)
    int   interact_npc = -1;               // clicked object being walked to
    bool  player_walked = false;           // `walking` as of the last frame
    std::uint32_t player_mode_ms = 0;      // when the player's walk/idle mode started
    bool  player_ran = false;
    int   player_dir = 4;   // south, facing the viewer
    bool  inv_open = false;   // 'I' — inventory panel
    bool  char_open = false;  // 'C' — character panel
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

    // Input + state verbs for scripted tests. Registered here because they
    // touch loop locals; the channel is only pumped inside this loop, so
    // the captures never outlive it.
    auto click_verb = [&](const std::vector<std::string>& args, std::uint8_t button) {
        if (args.size() < 3) return std::string("err click <x> <y>\n");
        // Args are game pixels; queued events carry window coords and get
        // converted back by SDL_ConvertEventToRenderCoordinates on poll.
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(win.r, std::stof(args[1]), std::stof(args[2]), &x, &y);
        // Motion + down + up land in the next frame's poll. One frame is
        // enough: update_button and the slot picker both accept a press
        // and release in the same frame.
        SDL_Event ev{};
        ev.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(win.w), .x = x, .y = y };
        SDL_PushEvent(&ev);
        for (auto type : { SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP }) {
            ev = {};
            ev.button = { .type = type, .windowID = SDL_GetWindowID(win.w),
                          .button = button,
                          .down = type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                          .clicks = 1, .x = x, .y = y };
            SDL_PushEvent(&ev);
        }
        return std::string("ok\n");
    };
    ch.on("click", [&](const std::vector<std::string>& a) { return click_verb(a, SDL_BUTTON_LEFT); });
    ch.on("rclick", [&](const std::vector<std::string>& a) { return click_verb(a, SDL_BUTTON_RIGHT); });
    ch.on("key", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err key <name>\n");
        const SDL_Keycode k = SDL_GetKeyFromName(args[1].c_str());
        if (k == SDLK_UNKNOWN) return std::string("err unknown key\n");
        for (auto type : { SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP }) {
            SDL_Event ev{};
            ev.key.type = type;
            ev.key.windowID = SDL_GetWindowID(win.w);
            ev.key.key = k;
            ev.key.down = type == SDL_EVENT_KEY_DOWN;
            SDL_PushEvent(&ev);
        }
        return std::string("ok\n");
    });
    ch.on("move", [&](const std::vector<std::string>& args) {
        if (args.size() < 3) return std::string("err move <x> <y>\n");
        float x = 0, y = 0;
        SDL_RenderCoordinatesToWindow(win.r, std::stof(args[1]), std::stof(args[2]), &x, &y);
        SDL_Event ev{};
        ev.motion = { .type = SDL_EVENT_MOUSE_MOTION, .windowID = SDL_GetWindowID(win.w), .x = x, .y = y };
        SDL_PushEvent(&ev);
        return std::string("ok\n");
    });
    ch.on("wheel", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err wheel <dy>\n");
        SDL_Event ev{};
        ev.wheel = { .type = SDL_EVENT_MOUSE_WHEEL, .windowID = SDL_GetWindowID(win.w),
                     .integer_y = std::stoi(args[1]) };
        SDL_PushEvent(&ev);
        return std::string("ok\n");
    });
    ch.on("debug", [&](const std::vector<std::string>& args) {
        if (args.size() >= 2 && args[1] == "automap" && scene) {    // reveal the whole level
            const auto& m = scene->world_ds1;
            for (int y = 0; y < int(m.height()); y += 12)
                for (int x = 0; x < int(m.width()); x += 12) automap_reveal(*scene, automap, float(x), float(y));
            return "ok " + std::to_string(automap.cells.size()) + "\n";
        }
        if (args.size() < 2 || args[1] != "collision") return std::string("err debug collision|automap\n");
        g_debug_collision = !g_debug_collision;
        return std::string(g_debug_collision ? "ok on\n" : "ok off\n");
    });
    // Every item of the in-game character with its hover text, one item
    // per paragraph (headless check for the tooltips).
    ch.on("items", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (const auto& it : cc.items) {
            out += "[" + it.code + " loc=" + std::to_string(it.location) + " slot=" + std::to_string(it.slot)
                 + " q=" + std::to_string(it.quality) + "]\n";
            for (const auto& l : item_lines(*scene, it, int(cc.stats.get(d2d::d2s::kLevel))))
                out += "  " + l.text + "\n";
        }
        return out + "ok\n";
    });
    // Named NPCs/objects with their feet on screen (game pixels) and
    // whether they have an NPC menu: "<name>\t<x>\t<y>\t<menu 0/1>".
    ch.on("npcs", [&](const std::vector<std::string>&) {
        if (!scene) return std::string("err no scene\n");
        std::string out;
        for (std::size_t i = 0; i < scene->world_npcs.size(); ++i) {
            const auto& n = scene->world_npcs[i];
            if (n.name.empty()) continue;
            const bool live = i < npc_states.size() && !n.path.empty();
            const float dx = (live ? npc_states[i].x : n.x) - player_x, dy = (live ? npc_states[i].y : n.y) - player_y;
            const int sx = int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int sy = int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == n.hc_idx; });
            out += n.name + "\t" + std::to_string(sx) + "\t" + std::to_string(sy) + "\t" + (menu ? "1" : "0") + "\n";
        }
        return out + "ok\n";
    });
    // The open NPC menu's lines: "<text>\t<x>\t<y>" with a point inside
    // each (game pixels), header first.
    ch.on("menu", [&](const std::vector<std::string>&) {
        std::string out;
        int base = npc_menu.y;
        for (const auto& l : npc_menu.lines) {
            base += l.height;
            out += l.text + "\t" + std::to_string(npc_menu.x + npc_menu.w / 2) + "\t"
                 + std::to_string(base - l.height / 2) + "\n";
        }
        return out + "ok\n";
    });
    ch.on("state", [&](const std::vector<std::string>&) {
        return std::string("screen=") + screen_name(screen)
             + " save=" + std::to_string(csu.selected)
             + " scroll=" + std::to_string(csu.scroll)
             + " class=" + std::to_string(cc.selected)
             + " name=" + cc.input_name
             + " hardcore=" + (cc.hardcore ? "1" : "0")
             + " cam=" + std::format("{:.2f},{:.2f}", player_x, player_y)
             + " walking=" + (walking ? "1" : "0") + " dir=" + std::to_string(player_dir)
             + " saves=" + std::to_string(scene ? scene->saves.size() : 0)
             + " stash=" + (stash_open ? "1" : "0")
             + " cube=" + (cube_open ? "1" : "0")
             + " store=" + std::to_string(store.npc >= 0 ? store.vendor : -1)
             + " gold=" + std::to_string(cc.stats.get(d2d::d2s::kGold))
             + " waypoint=" + std::to_string(waypoint.open ? int(scene->waypoint_levels[std::size_t(waypoint.tab)].size()) : 0)
             + " items=" + std::to_string(cc.items.size())
             + " menu=" + std::to_string(npc_menu.npc >= 0 ? int(npc_menu.lines.size()) : 0)
             + " automap=" + std::to_string(automap.open ? int(automap.cells.size()) : 0)
             + " speech=" + std::to_string(speech.npc >= 0 ? int(speech.lines.size()) : 0)
             + " voice=" + std::to_string(audio.voice_sound())
             + " music=" + std::to_string(audio.music.sound)
             + "\nok\n";
    });

    while (!quit) {
        // Signal-driven quit — Ctrl-C / SIGTERM. The atomic write from
        // d2d_sigint_handler is polled here; SDL_EVENT_QUIT and window
        // close still work through handle_sdl_events.
        if (g_sigint_quit) { quit = true; break; }
        const std::uint32_t frame_start_ms = std::uint32_t(SDL_GetTicks());
        // Toggled inside the `input` timing window, so any IME cost of the
        // switch itself shows up there.
        if (const bool want = screen == Screen::CharCreate; want != text_active) {
            if (want) SDL_StartTextInput(win.w);
            else      SDL_StopTextInput(win.w);
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
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // SDL3 doesn't rescale event coords under logical presentation;
            // convert so the mouse lands in 800x600 game pixels.
            SDL_ConvertEventToRenderCoordinates(win.r, &ev);
            handle_sdl_events(ev, mouse, screen, text_this_frame,
                              backspace_this_frame, keys_this_frame, quit);
        }
        current_phase.store(std::uint32_t(MainPhase::Devctl),
                            std::memory_order_relaxed);
        if (ch.active()) ch.pump();

        const std::uint32_t t_after_input = std::uint32_t(SDL_GetTicks());
        current_phase.store(std::uint32_t(MainPhase::Render),
                            std::memory_order_relaxed);
        const auto ms = std::uint32_t(SDL_GetTicks() - t0);
        // Level music and ambience belong to the game screen.
        if (screen != Screen::InGame && audio.music.sound > 0) {
            audio.stop(audio.music);
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
            int i = int((talk_rng = talk_rng * 0x6ac690c5u + 1u) % 8u);
            while (frontend_played[std::size_t(i)]) i = (i + 1) % 8;
            frontend_played[std::size_t(i)] = true;
            audio.play_music_path(*scene, kFrontendMusic[i], -(i + 1), 1.f, false);
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
                    auto f = video_mpqs.open(path);
                    const bool letterbox = path.find("x292") != std::string::npos;
                    if (f && video.open(std::move(*f), int(kW), letterbox ? int(kH) * 292 / 480 : int(kH))) {
                        video_playing = true;
                        video_start = ms;
                        audio.video_start(video.sample_rate());
                    }
                }
                if (!video_playing) { screen = video_return; video_return = Screen::Title; break; }
                if (!video.advance(double(ms - video_start) / 1000.0)) {
                    video_playing = false;
                    audio.video_stop();
                }
                audio.video_feed(video.audio());
                std::fill(fb.begin(), fb.end(), std::uint8_t{0});
                for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
                const int y0 = (int(kH) - video.height()) / 2;
                const auto& px = video.rgba();
                for (int y = 0; y < video.height(); ++y)
                    std::memcpy(&fb[(std::size_t(y0 + y) * kW) * 4], &px[std::size_t(y) * kW * 4], std::size_t(kW) * 4);
                break;
            }
            case Screen::Cinematics: {
                if (prev_screen != Screen::Cinematics)             // entering: refresh what's unlocked
                    cin_ui = cinematics_ui(*scene, cinematics_unlocked(read_seen()));
                for (int i = 0; i < 7; ++i) {
                    auto& b = cin_ui.entry[std::size_t(i)];
                    if (i >= cin_ui.unlocked) { b.hovered = b.pressed = false; continue; }
                    Screen dummy = screen;
                    update_button(b, mouse, dummy, quit);
                    if (b.hovered && mouse.release_this_frame && video_mpqs.contains(kCinematicVideo[std::size_t(i)])) {
                        video_queue = { kCinematicVideo[std::size_t(i)] };
                        video_return = Screen::Cinematics;
                        audio.stop(audio.music);
                        screen = Screen::Video;
                    }
                }
                if (update_button(cin_ui.cancel, mouse, screen, quit)) { quit = false; screen = Screen::Title; }
                render_cinematics(fb, *scene, cin_ui);
                break;
            }
            case Screen::Title:
                if (prev_screen == Screen::Video) {                 // Esc skipped the videos
                    audio.video_stop();
                    video_playing = false;
                    video_queue.clear();
                }
                for (auto& b : ui.buttons) update_button(b, mouse, screen, quit);
                render_title(fb, *scene, ui.buttons, ms);
                break;
            case Screen::Credits:
                if (mouse.release_this_frame) screen = Screen::Title;
                render_credits(fb, *scene, ms);
                break;
            case Screen::CharSelect: {
                const int n = int(scene->saves.size());
                const int max_scroll = charselect_max_scroll(n);
                int rows = -mouse.wheel;   // wheel up = scroll toward the top
                bool play = false;
                if (mouse.press_this_frame) {
                    const int slot = charselect_slot_at(mouse.x, mouse.y);
                    if (slot >= 0 && csu.scroll + slot < n) {
                        // A second press on the same character within
                        // 500 ms plays it, like OK (FUN_0043a9d0).
                        play = csu.selected == csu.scroll + slot && ms - csu.last_click_ms < 500;
                        csu.selected = csu.scroll + slot;
                        csu.last_click_ms = ms;
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
                for (const auto k : keys_this_frame) {
                    if (n == 0) break;
                    int& sel = csu.selected;
                    if (sel < 0) sel = 0;
                    else if (k == SDLK_HOME)                       sel = 0;
                    else if (k == SDLK_END)                        sel = n - 1;
                    else if (k == SDLK_LEFT  && sel % 2 == 1)      sel -= 1;
                    else if (k == SDLK_RIGHT && sel % 2 == 0 && sel + 1 < n) sel += 1;
                    else if (k == SDLK_UP    && sel >= 2)          sel -= 2;
                    else if (k == SDLK_DOWN  && sel + 2 < n)       sel += 2;
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) play = true;
                    const int row0 = sel / 2 * 2;
                    if (row0 < csu.scroll) csu.scroll = row0;
                    if (row0 > csu.scroll + kSlots - 2) csu.scroll = row0 - (kSlots - 2);
                }
                // OK only enters the game with a save picked.
                csu.ok_btn.do_switch = csu.selected >= 0;
                for (Button* b : {&csu.create_btn, &csu.convert_btn, &csu.delete_btn,
                                  &csu.cancel_btn, &csu.ok_btn})
                    update_button(*b, mouse, screen, quit);
                if (play && csu.selected >= 0 && csu.selected < n) screen = Screen::InGame;
                if (screen == Screen::InGame) {
                    // Load the picked save into the in-game character.
                    const auto& h = scene->saves[std::size_t(csu.selected)];
                    cc.selected   = kSaveClassToUi[h.cls];
                    cc.input_name = h.name;
                    cc.hardcore   = h.hardcore();
                    cc.appearance = h.appearance;
                    cc.items = csu.selected < int(scene->save_items.size())
                                   ? scene->save_items[std::size_t(csu.selected)]
                                   : std::vector<d2d::d2s::Item>{};
                    cc.stats = csu.selected < int(scene->save_stats.size())
                                   ? scene->save_stats[std::size_t(csu.selected)] : d2d::d2s::Stats{};
                    cc.panel = panel_stats(*scene, h, cc.items, cc.stats);
                    cc.expansion = h.expansion();
                    cc.header = h;
                }
                render_charselect(fb, *scene, csu, ms);
                break;
            }
            case Screen::InGame: {
                // ESC handled globally in handle_sdl_events (returns to Title).
                // D2 movement: press or hold the left button on the ground
                // and the character walks toward that point (the target
                // tracks the cursor while held); the camera follows.
                for (const auto k : keys_this_frame) {
                    if (k == SDLK_I) inv_open = !inv_open;
                    if (k == SDLK_R) running = !running;              // D2's run/walk toggle
                    if (k == SDLK_GRAVE) belt_open = !belt_open;      // D2's "Show Belt" key
                    if (k == SDLK_TAB) automap.open = !automap.open;  // D2's automap toggle
                    if (k == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = false; }
                    if (k == SDLK_ESCAPE) {
                        if (waypoint.open) waypoint = {};
                        else if (store.npc >= 0) { store = {}; inv_open = false; } // the store first
                        else if (speech.npc >= 0) speech = {};              // then speech
                        else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                        else if (inv_open || char_open || stash_open || cube_open)   // then panels
                            inv_open = char_open = stash_open = cube_open = false;
                        else screen = Screen::CharSelect;
                    }
                }
                const auto& lay = scene->inv_layout[std::size_t(kUiToSaveClass[std::max(cc.selected, 0)])];
                const bool over_panel =
                    (inv_open && mouse.x >= lay.panel_x && mouse.x < lay.panel_x + 320
                              && mouse.y >= lay.panel_y && mouse.y < lay.panel_y + 432) ||
                    ((char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open) && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320
                               && mouse.y >= kCharPanelY && mouse.y < kCharPanelY + 432);
                // The belt: its HUD strip (row 1's boxes) toggles the popup;
                // strip and open popup take the click instead of the world.
                const auto& belt = scene->belts[std::size_t(belt_index(*scene, cc.items))];
                const auto& b0 = belt.box[0];
                const auto& b3 = belt.box[3];
                const bool over_belt =
                    mouse.x >= b0[0] && mouse.x <= b3[1]
                    && ((mouse.y >= b0[2] && mouse.y <= b0[3])
                        || (belt_open && mouse.y >= belt.box[std::size_t(std::max(belt.boxes - 1, 0))][2]
                                      && mouse.y <= b0[3]));
                if (mouse.press_this_frame && over_belt && mouse.y >= b0[2]) belt_open = !belt_open;
                // Right-clicking the Horadric Cube ("box") in the inventory
                // or the stash opens it in the left panel, as D2 does.
                if (mouse.rpress_this_frame)
                    for (const auto& it : cc.items) {
                        if (it.code != "box" || it.location != 0) continue;
                        const bool in_inv = inv_open && it.panel == 1, in_stash = stash_open && it.panel == 5;
                        if (!in_inv && !in_stash) continue;
                        const auto r = grid_rect(*scene, in_inv ? lay : scene->stash_layout[cc.expansion ? 1 : 0], it);
                        if (mouse.x >= r[0] && mouse.x < r[0] + r[2] && mouse.y >= r[1] && mouse.y < r[1] + r[3]) {
                            cube_open = true; stash_open = char_open = false;
                        }
                    }
                // An open NPC menu takes every click: an entry runs (only
                // "cancel" so far — every entry closes it), anything else
                // closes it. ponytail: talk/trade/hire/gamble not built.
                bool menu_click = false;
                automap_reveal(*scene, automap, player_x, player_y);
                // The NPC's voice (FUN_004a10e0 plays FUN_004e0650's sound for
                // the speech string) follows the speech box.
                if (speech.npc < 0 && audio.voice.src) audio.stop_voice();
                // The level's SoundEnviron (Levels.txt SoundEnv -> Song, Day
                // Ambience). ponytail: the Rogue Encampment's, env 1: song
                // 4673 music_town_1, ambience 70; no night or events yet.
                if (audio.music.sound == 0) {
                    audio.play_music(*scene, scene->town_song);
                    audio.play(audio.ambience, *scene, scene->town_ambience);
                    if (audio.music.sound == 0) audio.music.sound = -1;   // don't retry every frame
                }
                if (speech.npc >= 0 && speech.voice == 0) {
                    speech.voice = -1;
                    const auto v = std::ranges::find_if(kSpeechSound, [&](const auto& e) { return e.first == speech.string; });
                    if (v != kSpeechSound.end()) { speech.voice = v->second; audio.play_voice(*scene, v->second); }
                }
                if (speech.npc >= 0 && (speech.done(ms) || mouse.press_this_frame)) {
                    menu_click = mouse.press_this_frame;          // a click skips the speech
                    speech = {};
                } else if (npc_menu.npc >= 0 && mouse.press_this_frame) {
                    const int li = npc_menu.line_at(mouse.x, mouse.y);
                    const auto action = li >= 0 ? npc_menu.lines[std::size_t(li)].action : NpcMenuState::kClose;
                    const int who = npc_menu.npc;
                    const auto& n = scene->world_npcs[std::size_t(who)];
                    const auto& st = npc_states[std::size_t(who)];
                    const float dx = (n.path.empty() ? n.x : st.x) - player_x, dy = (n.path.empty() ? n.y : st.y) - player_y;
                    const int sx = int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
                    const int sy = int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2)));
                    npc_menu = {};
                    if (action == NpcMenuState::kTrade) {
                        store = open_store(*scene, who, talk_rng);
                        store.header = cc.header;
                        inv_open = true; char_open = stash_open = cube_open = false;
                    } else if (action == NpcMenuState::kTalk) {
                        npc_menu = open_talk_menu(*scene, who, sx, sy);
                    } else if (action == NpcMenuState::kIntro || action == NpcMenuState::kGossip) {
                        const auto t = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& e) { return e.hc_idx == n.hc_idx; });
                        if (t != kNpcTalk.end() && !t->topics.empty()) {
                            const int cls = int(kUiToSaveClass[std::max(cc.selected, 0)]);
                            if (gossip_pick.size() != scene->world_npcs.size()) gossip_pick.assign(scene->world_npcs.size(), -1);
                            int topic;
                            auto done = [&](int q) { return cc.header.quest_flag(cc.header.active_difficulty(), q, 0); };
                            if (action == NpcMenuState::kIntro) {
                                topic = talk_topic(*t, true, cls, talk_rng, done);
                            } else {
                                if (gossip_pick[std::size_t(who)] < 0)
                                    gossip_pick[std::size_t(who)] = talk_topic(*t, false, cls, talk_rng, done);
                                topic = gossip_pick[std::size_t(who)];
                            }
                            speech = start_speech(*scene, who, t->topics[std::size_t(topic)].string, ms);
                        }
                    }
                    menu_click = true;
                }
                // Store: tabs (x 80+80i, y 60..90) switch; the buttons press;
                // close (button 4 at non-repair vendors) closes.
                store.pressed.fill(false);
                if (store.npc >= 0) {
                    static constexpr int kBtnX[4] = { 116, 169, 221, 273 };
                    for (int i = 0; i < 4; ++i) {
                        const int bx = kCharPanelX - 1 + kBtnX[i], by = 476 - 32 + 1;
                        const bool on = mouse.x >= bx && mouse.x < bx + 32 && mouse.y >= by && mouse.y < by + 32;
                        if (on && mouse.down) store.pressed[std::size_t(i)] = true;
                        if (on && mouse.release_this_frame && i < 2) store.mode = store.mode == i + 1 ? 0 : i + 1;
                        if (on && mouse.release_this_frame && i == 3 && store_button_frames(*scene, store)[3] == 10) {
                            store = {}; inv_open = false;
                            break;
                        }
                    }
                    if (mouse.press_this_frame && mouse.y >= 60 && mouse.y <= 90
                        && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320)
                        store.tab = (mouse.x - kCharPanelX) / 80;
                    // Right-click on stock buys; with Buy or Sell toggled on,
                    // a left click buys the stock item / sells your item.
                    const int si = store.npc >= 0 ? store_item_at(*scene, store, mouse.x, mouse.y) : -1;
                    if (si >= 0 && (mouse.rpress_this_frame || (mouse.press_this_frame && store.mode == 1)))
                        store_buy(*scene, store, si, cc.items, cc.stats);
                    if (store.npc >= 0 && mouse.press_this_frame && store.mode == 2)
                        for (std::size_t i = 0; i < cc.items.size(); ++i) {
                            const auto& it = cc.items[i];
                            if (it.location != 0 || it.panel != 1) continue;
                            const auto r = grid_rect(*scene, lay, it);
                            if (mouse.x >= r[0] && mouse.x < r[0] + r[2] && mouse.y >= r[1] && mouse.y < r[1] + r[3]) {
                                store_sell(*scene, store, i, cc.items, cc.stats);
                                break;
                            }
                        }
                }
                // Waypoint panel: tabs switch acts, cancel (or the row of the
                // level you're in) closes it.
                // ponytail: no travel yet — another row closes the panel too.
                if (waypoint.open) {
                    const bool on_cancel = mouse.x >= kCharPanelX + 0x111 && mouse.x < kCharPanelX + 0x111 + 0x24
                                        && mouse.y >= 60 + 0x183 && mouse.y < 60 + 0x183 + 0x22;
                    waypoint.cancel_down = on_cancel && mouse.down;
                    if (mouse.press_this_frame) {
                        if (const int t = waypoint_tab_at(cc.header, cc.expansion, mouse.x, mouse.y); t >= 0) waypoint.tab = t;
                        else if (waypoint_row_at(*scene, waypoint, cc.header, mouse.x, mouse.y) >= 0) waypoint = {};
                    }
                    if (on_cancel && mouse.release_this_frame) waypoint = {};
                }
                const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0;
                if (have_world) {
                    const float dt = float(ms - last_ms) / 1000.f;
                    if ((mouse.down || mouse.press_this_frame) && !over_ui) {
                        // Screen -> world: invert the iso projection around
                        // the player, who sits at (kW/2, kH/2 + kIsoH/2).
                        const float u = float(mouse.x - int(kW) / 2) / (kIsoW / 2);
                        const float v = float(mouse.y - int(kH) / 2 - kIsoH / 2) / (kIsoH / 2);
                        target_x = player_x + (u + v) / 2;
                        target_y = player_y + (v - u) / 2;
                        walking = true;
                        // Clicking an object you can operate walks to it
                        // first (D2 operates on arrival).
                        interact_npc = -1;
                        if (mouse.press_this_frame && hovered_npc >= 0) {
                            const auto& o = scene->world_npcs[std::size_t(hovered_npc)];
                            const bool menu = std::ranges::any_of(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == o.hc_idx; });
                            if (o.operate_fn == 32 || o.operate_fn == 23 || (o.root == "monsters" && menu)) {
                                interact_npc = hovered_npc;
                                const auto& st = npc_states[std::size_t(hovered_npc)];
                                target_x = o.path.empty() ? o.x : st.x;
                                target_y = o.path.empty() ? o.y : st.y;
                            }
                        }
                    }
                    // Close enough to the stash: open it with the inventory.
                    // ponytail: 2 cells, not D2's per-object operate range.
                    if (interact_npc >= 0) {
                        const auto& o = scene->world_npcs[std::size_t(interact_npc)];
                        const auto& st = npc_states[std::size_t(interact_npc)];
                        const float ox = o.path.empty() ? o.x : st.x, oy = o.path.empty() ? o.y : st.y;
                        if (std::hypot(ox - player_x, oy - player_y) < 2.f) {
                            if (o.operate_fn == 32) {
                                stash_open = inv_open = true; char_open = false;
                            } else if (o.operate_fn == 23) {
                                // Touching it activates it (the town's: wp 0).
                                // ponytail: town only; a wilderness waypoint would need its level.
                                cc.header.waypoints[std::size_t(cc.header.active_difficulty())][0] |= 1;
                                waypoint = { .open = true };
                                inv_open = char_open = stash_open = cube_open = false;
                            } else {
                                // The NPC's feet on screen, as render_world projects them.
                                const float dx = ox - player_x, dy = oy - player_y;
                                npc_menu = open_npc_menu(*scene, interact_npc,
                                    int(kW) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
                                    int(kH) / 2 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))));
                            }
                            walking = false; interact_npc = -1;
                        } else if (!walking) {
                            interact_npc = -1;                         // blocked on the way
                        } else {
                            target_x = ox; target_y = oy;              // follow a walking NPC
                        }
                    }
                    if (walking) {
                        const float dx = target_x - player_x, dy = target_y - player_y;
                        const auto sc = std::size_t(kUiToSaveClass[std::max(cc.selected, 0)]);
                        const float vel = float(running ? scene->run_velocity[sc] : scene->walk_velocity[sc]);
                        const float dist = std::hypot(dx, dy), step = cells_per_sec(vel) * dt;
                        if (dist > 0.05f) player_dir = direction16(dx, dy);
                        const float k = dist <= step ? 1.f : step / dist;
                        const float nx = player_x + dx * k, ny = player_y + dy * k;
                        // Blocked subtile ahead: slide along one axis, else
                        // stop. ponytail: D2 paths around obstacles; this
                        // only slides along walls.
                        if      (!scene->blocked(nx, ny))       { player_x = nx; player_y = ny; }
                        else if (!scene->blocked(nx, player_y)) { player_x = nx; }
                        else if (!scene->blocked(player_x, ny)) { player_y = ny; }
                        else walking = false;
                        if (dist <= step) walking = false;
                    }
                    npc_patrol(*scene, npc_states, { npc_menu.npc, speech.npc, store.npc }, ms, dt);
                }
                if (const bool m = walking && running; walking != player_walked || m != player_ran) {
                    player_walked = walking; player_ran = m; player_mode_ms = ms;
                }
                const int ui_cls = std::max(cc.selected, 0);
                render_ingame(fb, *scene, ui_cls,
                              cc.appearance ? *cc.appearance
                                            : scene->starting_gear[std::size_t(kUiToSaveClass[ui_cls])],
                              cc.input_name, cc.hardcore,
                              player_x, player_y, walking ? (running ? kModeRN : kModeTW) : kModeTN,
                              player_dir, ms, mouse.x, mouse.y, npc_states,
                              inv_open ? &cc.items : nullptr,
                              char_open ? &cc.stats : nullptr, &cc.stats, &cc.panel, player_mode_ms, &cc.items,
                              &hovered_npc, stash_open || cube_open ? &cc.items : nullptr, cc.expansion, belt_open,
                              cube_open, &npc_menu, &speech, &automap, &store);
                if (waypoint.open)
                    draw_waypoints(fb, *scene, waypoint, cc.header, cc.expansion, 1, mouse.x, mouse.y);
                break;
            }
            case Screen::CharCreate: {
                cc.appearance.reset();   // a new character wears starting gear
                cc.items.clear();
                cc.stats = {};
                // Text input into the name buffer (15-char cap = D2's
                // character-record name limit).
                if (!text_this_frame.empty()) {
                    for (char c : text_this_frame) {
                        if (cc.input_name.size() < 15) cc.input_name.push_back(c);
                    }
                }
                if (backspace_this_frame && !cc.input_name.empty())
                    cc.input_name.pop_back();

                // OK is only enabled once a class is picked and a name is
                // entered — mirrors D2's OK-button gating.
                cc.ok_btn.do_switch = (cc.selected >= 0 && !cc.input_name.empty());
                update_button(cc.cancel_btn, mouse, screen, quit);
                update_button(cc.ok_btn,     mouse, screen, quit);
                if (!cc.cancel_btn.hovered && !cc.ok_btn.hovered)
                    handle_charcreate_click(cc, mouse, ms);
                advance_char_states(cc, *scene, ms);
                render_charcreate(fb, *scene, cc, ms);
                break;
            }
            }
            // D2's own cursor, drawn into the frame so it scales with the
            // game (the OS pointer is hidden). DC6 frames anchor bottom-
            // left, which puts the fingertip on the hotspot. Palette of
            // the screen underneath.
            // ponytail: frame 0 idle, the closed hand (7) while pressed;
            // D2 plays the grab frames in between.
            if (screen != Screen::Video && scene->cursor.frames_per_direction() >= 8) {   // hidden over cinematics
                const auto& pal = screen == Screen::InGame
                                      ? (scene->act1_pal.entries().empty() ? scene->pal : scene->act1_pal)
                                  : screen == Screen::CharCreate ? scene->charselect_pal : scene->pal;
                const auto& f = scene->cursor.frame(0, mouse.down ? 7 : 0);
                blit_sprite(fb, f, pal, mouse.x + f.offset_x,
                            mouse.y + f.offset_y - int(f.height) + 1);
            }
        } else {
            paint_test_pattern(fb);
        }

        const std::uint32_t t_after_render = std::uint32_t(SDL_GetTicks());
        // Skip GPU work when the window is minimized — Metal's swapchain
        // stalls if we keep pushing frames to a hidden drawable, which
        // is the classic macOS beachball trigger for SDL apps that
        // don't gate render on window visibility.
        const auto wflags = SDL_GetWindowFlags(win.w);
        if (!(wflags & SDL_WINDOW_MINIMIZED)) {
            current_phase.store(std::uint32_t(MainPhase::Upload),
                                std::memory_order_relaxed);
            SDL_UpdateTexture(win.t, nullptr, fb.data(), int(kW * 4));
            SDL_RenderClear(win.r);
            SDL_RenderTexture(win.r, win.t, nullptr, nullptr);
            current_phase.store(std::uint32_t(MainPhase::Present),
                                std::memory_order_relaxed);
            SDL_RenderPresent(win.r);
        }
        const std::uint32_t t_after_present = std::uint32_t(SDL_GetTicks());
        if (++frame_count == 1) d2d::log::info("First frame presented at {} ms after launch.", d2d::log::ms());
        last_ms = ms;
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
                dt_input, dt_render, dt_present, int(screen), int(player_x), int(player_y));
        }
        if (ms - stat_last_report_ms >= 5000) {
            const std::uint32_t avg_r = stat_frames ? stat_render_sum  / stat_frames : 0;
            const std::uint32_t avg_p = stat_frames ? stat_present_sum / stat_frames : 0;
            d2d::log::info(
                "alive: {} frames/5s | input max={} | render avg={} max={} | present avg={} max={} | screen={} cam=({},{})",
                stat_frames, stat_input_max, avg_r, stat_render_max, avg_p, stat_present_max,
                int(screen), int(player_x), int(player_y));
            stat_frames = 0;      stat_input_max = 0;
            stat_render_sum = 0;  stat_render_max = 0;
            stat_present_sum = 0; stat_present_max = 0;
            stat_last_report_ms = ms;
        }
        current_phase.store(std::uint32_t(MainPhase::PaceDelay),
                            std::memory_order_relaxed);
        pace_frame(frame_start_ms);
    }
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
    std::signal(SIGPIPE, SIG_IGN);

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
    for (const auto& d : { d2d::userdir::global_dir("d2d"), fs::path("."), user_dir })
        if (fs::exists(d / "d2d.cfg")) {
            d2d::userdir::load_cfg(d / "d2d.cfg", cfg);
            d2d::log::info("  Config: {}", (d / "d2d.cfg").string());
        }

    std::string devctl_path;
    fs::path    data_dir = default_data_dir(cfg["data"]);
    bool        headless = false;
    std::string start_screen;   // "title" | "credits" | "charcreate" | "ingame"
    int         start_class = 0;
    std::string start_name;
    bool        start_hardcore = false;

    CLI::App app{"d2d — Diablo II re-implementation (dev build)"};
    app.add_option("--devctl", devctl_path,
                   "Unix-socket dev-control channel path");
    std::string data_dir_str = data_dir.string();
    int scale = cfg.contains("scale") ? std::atoi(cfg["scale"].c_str()) : 1;
    app.add_option("--scale", scale, "Window scale (game renders at 800x600)")
        ->check(CLI::Range(1, 8));
    app.add_option("--data", data_dir_str,
                   "Path to the D2 MPQ directory");
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
    bool no_video = false;
    app.add_flag  ("--no-video", no_video, "Skip the startup cinematics");
    int start_cam_x = -1, start_cam_y = -1;
    app.add_option("--start-cam-x", start_cam_x,
                   "InGame camera x (grid cell)");
    app.add_option("--start-cam-y", start_cam_y,
                   "InGame camera y (grid cell)");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    data_dir = data_dir_str;
    d2d::log::info("  Data dir: {}", data_dir.string());

    std::vector<std::uint8_t> fb(std::size_t(kW) * kH * 4, 0);
    for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
    auto scene = load_scene(data_dir, cfg["patch"]);   // nullopt if MPQ dir is missing
    if (scene) load_saves(*scene, save_dir);

    std::atomic<std::uint64_t> frame_count{0};
    std::atomic<bool>          quit{false};

    d2d::devctl::Channel ch;
    ch.on("info", [&](const std::vector<std::string>&) {
        return "w=" + std::to_string(kW) + " h=" + std::to_string(kH)
             + " frame=" + std::to_string(frame_count.load()) + "\nok\n";
    });
    ch.on("screenshot", [&](const std::vector<std::string>& args) {
        if (args.size() < 2) return std::string("err screenshot <path>\n");
        // Relative paths land in the user screenshots dir.
        const fs::path out = fs::path(args[1]).is_relative() ? shot_dir / args[1]
                                                             : fs::path(args[1]);
        const auto n = d2d::screenshot::save_png(out, fb, kW, kH);
        return "ok " + std::to_string(n) + "\n";
    });
    ch.on("quit", [&](const std::vector<std::string>&) {
        quit = true;
        return std::string("ok\n");
    });
    ch.listen(devctl_path);

    g_start_screen   = start_screen;
    g_video          = !no_video && cfg["video"] != "0";
    g_user_dir       = user_dir;
    g_start_class    = start_class;
    g_start_name     = start_name;
    g_start_hardcore = start_hardcore;
    g_start_cam_x    = start_cam_x;
    g_start_cam_y    = start_cam_y;
    g_scale          = std::clamp(scale, 1, 8);   // cfg value isn't CLI-checked

    if (headless) {
        if (!ch.active()) {
            d2d::log::info("--headless with no --devctl has nothing to do. exiting.");
            return 0;
        }
        // Same loop as the windowed build on SDL's dummy video driver +
        // software renderer: no window, no GL, but events, devctl and
        // screenshots all work. (The "offscreen" driver needs EGL, which
        // macOS doesn't have.)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        setenv("ALSOFT_DRIVERS", "null", 0);            // openal-soft's silent backend
    }
    return run_windowed(fb, scene, ch, frame_count, quit);
}
