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

#include <mpq.hpp>
#include <cof.hpp>
#include <dc6.hpp>
#include <dcc.hpp>
#include <devctl.hpp>
#include <d2s.hpp>
#include <ds1.hpp>
#include <dt1.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <screenshot.hpp>
#include <tbl.hpp>
#include <userdir.hpp>

#include <SDL3/SDL.h>
#include <CLI/CLI.hpp>
#include <csignal>
#include <unordered_map>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

// All assets the frontend needs. Loaded once at startup; renderers paint
// from these each tick without touching the MPQ again. Sourced from
// FUN_0042e6d0 (main-menu loader) — see docs/research/re/frontend-menu-table.md.
struct Scene {
    d2d::palette::Palette pal;                // Sky — title/credits palette
    d2d::palette::Palette charselect_pal;     // fechar — char-select/create palette
    d2d::palette::Pl2     sky_pl2;            // Sky PL2 (title logo additive)
    d2d::palette::Pl2     fechar_pl2;         // fechar PL2 (campfire additive)
    d2d::dc6::Sprite      bg;                 // TitleScreen or gameselectscreenEXP
    d2d::dc6::Sprite      logo_static;        // Diablo2.dc6 — 320×151, classic only
    d2d::dc6::Sprite      logo_bl, logo_br;   // D2logoBlack{Left,Right} — silhouettes
    d2d::dc6::Sprite      logo_fl, logo_fr;   // D2logoFire{Left,Right} — animated fire
    d2d::dc6::Sprite      btn_wide;           // WideButtonBlank
    d2d::dc6::Sprite      btn_wide2;          // WideButtonBlank02
    d2d::dc6::Sprite      btn_narrow;         // NarrowButtonBlank
    d2d::dc6::Sprite      btn_short;          // ShortButtonBlank
    d2d::dc6::Sprite      credits_bg;         // creditsbckgexpand.dc6 (or classic fallback)
    // Character-creation screen (loaded by FUN_004326f0). SP button hops here.
    d2d::dc6::Sprite      charcreate_bg;      // charactercreationscreenEXP.dc6
    d2d::dc6::Sprite      fire;               // fire.DC6 — campfire between the classes
    d2d::dc6::Sprite      medium_sel_button;  // MediumSelButtonBlank.dc6 — char-create OK/EXIT chrome (per FUN_004326f0)
    d2d::dc6::Sprite      textbox;            // textbox.dc6 — name-entry chrome (single 169×26 frame)
    d2d::dc6::Sprite      clickbox;           // clickbox.dc6 — Hardcore checkbox chrome (2 frames × 15×16, unchecked/checked)
    // Character-select screen assets (RE FUN_004359d0, handle 0x00779734).
    // Slot chrome is 2-frame 256+16 wide × 93 tall (matches WideButton
    // composite pattern). BG is 12-frame 4×3 grid of ≤256×256 tiles.
    d2d::dc6::Sprite      charselect_bg;      // characterselectscreenEXP.dc6
    d2d::dc6::Sprite      charselect_box;     // charselectbox.dc6 (filled slot)
    d2d::dc6::Sprite      charselect_boxgrey; // charselectboxgrey.dc6 (empty slot)
    d2d::dc6::Sprite      charselect_scroll;  // FrontEnd\joingamescrollbars.dc6
    d2d::dc6::Sprite      tall_button;        // TallButtonBlank.dc6 (168×60) — CREATE / DELETE
    // In-game player — town-neutral (TN) idle, LIT armor tier, per class.
    // The COF names the body-part layers and their per-frame draw order;
    // each layer is its own DCC, indexed here by COF composite type
    // (0 HD, 1 TR, 2 LG, 3 RA, 4 LA, 5 RH, 6 LH, 7 SH, 8.. S1..S8).
    // ponytail: RH/LH/SH (weapon/shield) resolve by item code, so bare
    // LIT layers only; wire in item codes once inventory is parsed.
    struct PlayerAnim {
        d2d::cof::Cof                     cof;
        std::array<d2d::dcc::Sprite, 16>  layers;
    };
    // Loaded on first use (player_anim) — decoding all 7 classes' layers
    // up front doubled startup (0.75 s -> 1.4 s). Cache is `mutable` so
    // the const Scene the renderers get can still fill it.
    mutable std::array<std::optional<PlayerAnim>, 7> player;
    const PlayerAnim& player_anim(int class_idx) const;
    // Kept open for lazy loads after startup.
    d2d::mpq::Stack mpqs;
    // Class animations — 7 classes × 5 states, per the RE'd class table at
    // 0x00708a00. State order matches D2's suffix scheme: nu1, nu2, fw,
    // nu3, bw. Class order (rows in the table): assassin, druid, amazon,
    // necromancer, barbarian, sorceress, paladin — but we store them in
    // our left-to-right visual order (barb/necro/pally/ama/sorc/druid/assn)
    // to match Scene::class positions.
    std::array<std::array<d2d::dc6::Sprite, 5>, 7> class_anims;
    d2d::font::Font       font;
    // Credits.txt / ExpansionCredits.txt parsed to plain Latin-1 lines.
    // A '*' prefix on a line marks a section header in D2's format.
    std::vector<std::string> credits;
    // Character saves from <user dir>/save/*.d2s, sorted by name.
    std::vector<d2d::d2s::Header> saves;
    // D2's three-tier string tables. Lookup order per D2's own convention:
    //   patchstring.tbl (826 entries) — patch-shipped overrides, wins
    //   expansionstring.tbl (2788 entries) — LoD additions (Druid/Assassin
    //     class names live here in some builds, but 1.14d put them in
    //     patchstring.tbl — see class-table.md)
    //   string.tbl (5099 entries) — base classic keys
    // Frontend button labels come out via ID lookup (see
    // docs/research/re/frontend-menu-table.md — records at 0x708ec0+ carry
    // TBL ids 0x13f2..0x13f7 in the +0x18 field). Class-name keys are bare
    // ("Barbarian", "Assassin", "Druid", …) — see class-table.md.
    d2d::tbl::Table       strings;         // string.tbl
    d2d::tbl::Table       patch_strings;   // patchstring.tbl (has Druid/Assassin)
    d2d::tbl::Table       exp_strings;     // expansionstring.tbl
    int                   bg_tiles_across{4};

    // Rogue-camp world data — one DS1 + the DT1s it references, plus a
    // (style, sequence)->tile lookup pre-built for floor rendering. See
    // render_ingame_world() for the compositor. Empty when the assets
    // aren't found (headless / bad data dir).
    d2d::ds1::Map                            world_ds1;
    std::vector<d2d::dt1::Archive>           world_dt1s;
    // Keyed by (style, seq, type) — one map covers floors, walls, trees,
    // shadows, roofs; the DT1's `type` field disambiguates orientations
    // that share (style, seq). First matching tile wins across DT1s.
    std::unordered_map<std::uint64_t, const d2d::dt1::Tile*> world_tile_lookup;
    // ACT1 palette — the actual town palette (fechar/sky are frontend-only).
    d2d::palette::Palette                    act1_pal;
};

// D2 TBL values are UTF-16; our font is Latin-1. Downcast char by char.
// (Forward decl — full definition below title_ui.)
std::string u16_to_latin1(std::u16string_view s);

// TBL lookup with D2's precedence: patch → expansion → base. First-hit wins,
// matching how the game resolves any string ID/key at runtime.
inline std::optional<std::u16string_view>
lookup_string(const Scene& s, std::string_view key) {
    if (auto v = s.patch_strings.get(key); v && !v->empty()) return v;
    if (auto v = s.exp_strings.get(key);   v && !v->empty()) return v;
    if (auto v = s.strings.get(key);       v && !v->empty()) return v;
    return std::nullopt;
}
inline std::optional<std::u16string_view>
lookup_string(const Scene& s, std::uint16_t id) {
    if (auto v = s.patch_strings.get(id); v && !v->empty()) return v;
    if (auto v = s.exp_strings.get(id);   v && !v->empty()) return v;
    if (auto v = s.strings.get(id);       v && !v->empty()) return v;
    return std::nullopt;
}

// --- Screen state machine + mouse routing ---------------------------------

enum class Screen { Title, Credits, CharSelect, CharCreate, InGame };

// Per-class animation state on the char-create screen. Matches D2's flow:
// classes idle in place (nu1); on click the "just clicked" class walks
// forward one time (fw) and then stands in the selected pose (nu3 loop);
// clicking a different class or Cancel walks the current selected back
// (bw once) before returning to idle.
enum class ClassState : std::uint8_t { Idle, Selecting, Selected, Deselecting };

struct ClassUI {
    ClassState    state = ClassState::Idle;
    std::uint32_t state_start_ms = 0;
};

// Class placement records — sourced from RE'd char-create menu table at
// 0x70aed0..0x70b470 (each entry is one kind=3 record from the master
// table). (x, y, w, h) is the bounding-box rect the D2 drawer uses to
// position the class sprite and to hit-test clicks. Sprite renders so
// its logical origin (feet-centre) lands at (x + w/2, y + h), which
// combined with each frame's own DC6 offset positions the actual pixels.
// Order matches Scene::class_anims (BA, NE, PA, AM, SO, DZ, AS — our
// left-to-right visual order). Individual records source addresses:
//   AM 0x70b050  NE 0x70afc0  AS 0x70b440  BA 0x70af30
//   PA 0x70b020  SO 0x70aff0  DZ 0x70b470
struct ClassPos { int x, y, w, h; };
constexpr ClassPos kClassPos[7] = {
    {400, 330, 88, 184},   // Barbarian
    {217, 360, 88, 184},   // Necromancer
    {521, 339, 88, 184},   // Paladin
    {100, 337, 88, 184},   // Amazon
    {626, 353, 88, 184},   // Sorceress
    {720, 370, 88, 184},   // Druid
    {232, 364, 88, 184},   // Assassin
};
// Fallbacks used when string.tbl can't resolve a class-name key. Order
// matches Scene::class_anims (BA, NE, PA, AM, SO, DZ, AS).
constexpr const char* kClassKey[7] = {
    "Barbarian", "Necromancer", "Paladin", "Amazon",
    "Sorceress", "Druid",       "Assassin",
};

// .d2s class id (AM SO NE PA BA DZ AS) -> our visual-order index
// (BA NE PA AM SO DZ AS, see kClassKey).
constexpr int kSaveClassToUi[7] = { 3, 4, 1, 2, 0, 5, 6 };

// D2's frontend records store (x, y, w, h) with y = the BOTTOM row
// (bottom-left anchor, like its DC6 blits): the full-screen BG record is
// (0, 599, 800, 600). Our blits and hit tests are top-left, so convert.
constexpr int rec_top(int y_bottom, int h) { return y_bottom - h + 1; }

// Char-select slot grid, 2 columns x 4 rows — RE'd from the LoD
// char-select init FUN_0043ae30 / drawer FUN_004380f0: 8 visible slots
// (DAT_0070cc0c), column x alternates 0x25/0x135, row bottom y =
// 0xb2 + 0x5d*row — so tops at 86..365, filling the panel's 4x93 interior.
// Each 272x93 slot is a 200x92 text box (records 0x84..0x8b) + a 72x93
// portrait cell on its right (0x8c..0x93). See docs/research/re/char-select.md.
constexpr int kSlotW = 272, kSlotH = 93;
constexpr int kSlotX[2] = { 37, 309 };
constexpr int kSlotY[4] = { rec_top(178, kSlotH), rec_top(271, kSlotH),
                            rec_top(364, kSlotH), rec_top(457, kSlotH) };
constexpr int kSlots = 8;
// Scrollbar (record 0xa7, joingamescrollbars.dc6: f0/f2 up, f1/f3 down
// normal/pressed, f4 thumb; 12x14 each). Shown only when saves > 8; one
// step = one row = 2 saves (callback 0x439df0).
// RE'd box (564, 457, 34, 371) = x 564..597, y 87..457: the rail drawn
// into the BG art's right edge. The 12 px bar sits right-aligned in it.
// ponytail: right-aligned by eye against the art; D2Win's kind-5 widget
// (FUN_005084f0 / draw FUN_00508370) has the exact math if it's off by a px.
constexpr int kScrollX = 564 + 34 - 12, kScrollTop = rec_top(457, 371),
              kScrollBot = 457 + 1, kScrollArrow = 14;

struct Button {
    int x{}, y{}, w{}, h{};
    const char*             label   = nullptr;
    const d2d::dc6::Sprite* chrome  = nullptr;
    // Action: set BOTH goto_screen (screen switch) OR quit (exit). Neither
    // means "no-op for now" — used for the Battle.net / Multiplayer buttons.
    Screen                  goto_screen = Screen::Title;
    bool                    do_switch   = false;
    bool                    quit        = false;
    // Transient per-frame state, updated from mouse events.
    bool                    hovered = false;
    bool                    pressed = false;
};

struct Mouse {
    int  x = 0, y = 0;
    bool down = false;                 // current button state
    bool press_this_frame = false;     // rising edge
    bool release_this_frame = false;   // falling edge
    int  wheel = 0;                    // wheel notches this frame, +up
};

// Composite the chrome for one button. D2's chrome DC6s come in two shapes
// (verified against the actual assets):
//   * 2 frames — single-piece, full width. f=0 normal, f=1 pressed.
//     (ShortButtonBlank 135×25, MediumButtonBlank 128×35.)
//   * 4 frames — two-piece: `wide + sliver`, drawn side-by-side. f=0 is
//     the 256×h left piece, f=1 is the 16×h right sliver. f=2 + f=3 are
//     the pressed variants of the same split.
//     (WideButtonBlank / WideButtonBlank02 / NarrowButtonBlank.)
// There is NO hover-only frame — D2 signals hover by brightening the LABEL
// instead. Callers pick the label tint separately.
void blit_button_chrome(std::vector<std::uint8_t>& fb,
                        const d2d::palette::Palette& pal,
                        const d2d::dc6::Sprite& chrome,
                        int x, int y, bool pressed) {
    const auto n = chrome.frames_per_direction();
    if (n == 0) return;
    if (n == 2) {
        const auto& fr = chrome.frame(0, pressed ? 1 : 0);
        blit_sprite(fb, fr, pal, x, y);
        return;
    }
    // 4-frame split. First half goes at x; second half right after it.
    const auto base = pressed ? 2u : 0u;
    if (n > base) {
        const auto& left = chrome.frame(0, base);
        blit_sprite(fb, left, pal, x, y);
        if (n > base + 1) {
            const auto& right = chrome.frame(0, base + 1);
            blit_sprite(fb, right, pal, x + int(left.width), y);
        }
    }
}

// Update hover/pressed state and, on a mouse-up over a hovered+pressed
// button, invoke the action. Returns true if any action was taken so the
// caller can early-out.
struct CharCreateUI {
    std::array<ClassUI, 7> classes{};
    int selected = -1;           // index of currently-selected class or -1
    Button ok_btn{};
    Button cancel_btn{};
    // Name entry — SDL text-input feeds this buffer, capped at 15 chars
    // to match D2's char-name limit (per D2's actual character record
    // struct). Left/right arrows and non-printable keys are ignored.
    std::string input_name;
    // Hardcore checkbox — the char-create master-table record at 0x70b0b0
    // (kind=6 button, x=319, y=560, w=15, h=16, handle=DAT_007797c0
    // (clickbox.dc6), on_click=FUN_00430730 which sets bit 0x04 of the
    // character-struct flags word at [0x7795d4]+0x1ef — that's the D2S
    // "Character Status" hardcore bit). Label from patchstring.tbl id
    // 0x1406 ("Hardcore"). See docs/research/re/char-create-table.md for
    // the full 33-record breakdown, including the Ladder (bit 0x40) and
    // Expansion (bit 0x20) checkbox records also present in the table.
    bool hardcore = false;
    // Owned label buffers so Button.label pointers stay live for the
    // frame; sourced from string.tbl by ID.
    std::string ok_label;
    std::string cancel_label;
    std::string hardcore_label;
    // Selected-class name from string.tbl (u16 → Latin-1). Empty when
    // no class is picked yet.
    std::string selected_name;
};

bool update_button(Button& b, const Mouse& m, Screen& current_screen,
                   std::atomic<bool>& quit) {
    b.hovered = m.x >= b.x && m.x < b.x + b.w
             && m.y >= b.y && m.y < b.y + b.h;
    if (b.hovered && m.press_this_frame) b.pressed = true;
    if (!m.down)                          b.pressed = false;
    if (b.hovered && m.release_this_frame) {
        if (b.do_switch) { current_screen = b.goto_screen; return true; }
        if (b.quit)      { quit = true; return true; }
    }
    return false;
}

// Parse D2's UTF-16LE-with-BOM credits.txt into Latin-1 lines. The file's
// section headers use a '*' prefix. Skips empty lines but keeps '*' lines
// as-is (renderer decides whether to style them).
std::vector<std::string> parse_credits_utf16(std::span<const std::byte> b) {
    std::vector<std::string> out;
    std::size_t i = 0;
    // Skip BOM (FF FE) if present.
    if (b.size() >= 2 && std::uint8_t(b[0]) == 0xFF
                      && std::uint8_t(b[1]) == 0xFE) i = 2;
    std::string cur;
    while (i + 1 < b.size()) {
        const auto lo = std::uint8_t(b[i]);
        const auto hi = std::uint8_t(b[i + 1]);
        i += 2;
        if (hi == 0 && lo == '\r') continue;         // ignore CR
        if (hi == 0 && lo == '\n') { out.push_back(std::move(cur)); cur.clear(); continue; }
        // Latin-1 subset: keep low byte, drop chars we can't render.
        if (hi == 0 && lo >= 32) cur.push_back(char(lo));
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// Forward decl — full body lives after Scene{} construction so it can use
// the same members without repeating field types.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path);

// One class's town-neutral composite: the COF plus every LIT layer DCC it
// names. Missing layers (weapon/shield) stay empty and are skipped at draw.
Scene::PlayerAnim load_player(const d2d::mpq::Stack& mpqs, int class_idx) {
    // Class → CHARS folder + weapon class (Assassin lives under folder
    // "AI", the dev codename). Starting-weapon classes: Barb/Necro/Sorc/
    // Druid/Assassin = HTH bare-hand; Paladin = 1HS; Amazon = 1HT.
    struct C { const char* folder; const char* wpn; };
    constexpr C cs[7] = {
        {"BA", "HTH"}, {"NE", "HTH"}, {"PA", "1HS"},
        {"AM", "1HT"}, {"SO", "HTH"}, {"DZ", "HTH"},
        {"AI", "HTH"},
    };
    constexpr const char* kLayer[16] = {
        "HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
        "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
    };
    const auto& c = cs[class_idx];
    Scene::PlayerAnim out;
    char path[256];
    std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\COF\%sTN%s.cof)",
                  c.folder, c.folder, c.wpn);
    try {
        auto cof = mpqs.try_read(path);
        if (!cof) return out;
        out.cof = d2d::cof::Cof(*cof);
        for (const auto& L : out.cof.layer_defs()) {
            if (L.type >= 16) continue;
            std::string wc = L.weapon_class;
            for (auto& ch : wc) ch = char(std::toupper(ch));
            // <CC>\<LY>\<CC><LY>LIT<mode><wclass>.dcc
            std::snprintf(path, sizeof(path),
                R"(data\global\CHARS\%s\%s\%s%sLITTN%s.dcc)",
                c.folder, kLayer[L.type], c.folder, kLayer[L.type], wc.c_str());
            if (auto b = mpqs.try_read(path))
                out.layers[L.type] = d2d::dcc::Sprite(*b);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] %s: %s\n", path, e.what());
    }
    return out;
}

const Scene::PlayerAnim& Scene::player_anim(int class_idx) const {
    auto& slot = player[std::size_t(class_idx)];
    if (!slot) slot = load_player(mpqs, class_idx);
    return *slot;
}

// Headers of every valid .d2s in `dir`, sorted by name. Bad files are
// logged and skipped — saves are user-supplied.
std::vector<d2d::d2s::Header> load_saves(const fs::path& dir) {
    std::vector<d2d::d2s::Header> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".d2s") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
        try {
            out.push_back(d2d::d2s::parse_header(std::as_bytes(std::span(raw))));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "[d2d] %s: %s\n", e.path().string().c_str(), ex.what());
        }
    }
    std::ranges::sort(out, {}, &d2d::d2s::Header::name);
    return out;
}

std::optional<Scene> load_scene(const fs::path& data_dir) {
    const auto d2data = data_dir / "d2data.mpq";
    if (!fs::exists(d2data)) return std::nullopt;
    try {
        d2d::mpq::Stack mpqs;
        const auto d2exp = data_dir / "d2exp.mpq";
        if (fs::exists(d2exp)) mpqs.push(d2exp);
        mpqs.push(d2data);
        // Character animations live in d2char.mpq — Stack lookup is
        // priority-ordered so later pushes rank lower; DCC-not-found is
        // silent in load_scene and per-class loaders skip on miss.
        const auto d2char = data_dir / "d2char.mpq";
        if (fs::exists(d2char)) mpqs.push(d2char);

        // Prefer the LoD title asset (fenced rogue camp at night). Classic
        // TitleScreen is only 4×3 sub-frames; LoD is the same layout.
        auto title = mpqs.try_read(R"(data\global\ui\FrontEnd\gameselectscreenEXP.dc6)");
        if (!title) title = mpqs.try_read(R"(data\global\ui\FrontEnd\TitleScreen.DC6)");
        if (!title) throw std::runtime_error("no title screen asset");

        Scene scene = Scene{
            // Sky = title/credits (game.exe hardcodes palette\sky\pal.pl2 in
            // 5 sites of the menu loader — docs/research/re/frontend-menu-table.md).
            .pal            = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\Sky\pal.dat)")),
            // fechar = "Front End CHARacter", the char-select/creation palette.
            // game.exe's FUN_00435580 (char-select init) loads it right after
            // the char-select asset loader (FUN_004326f0). Firelit warm tones
            // — night camp scene lit by the campfire the classes stand around.
            .charselect_pal = d2d::palette::Palette(mpqs.read(
                                R"(data\global\palette\fechar\pal.dat)")),
            .sky_pl2        = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\Sky\Pal.PL2)")),
            .fechar_pl2     = d2d::palette::Pl2(mpqs.read(
                                R"(data\global\palette\fechar\Pal.PL2)")),
            .bg          = d2d::dc6::Sprite(*title),
            .logo_static = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\Diablo2.dc6)")),
            .logo_bl     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackLeft.DC6)")),
            .logo_br     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoBlackRight.DC6)")),
            .logo_fl     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireLeft.DC6)")),
            .logo_fr     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\D2logoFireRight.DC6)")),
            .btn_wide    = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank.dc6)")),
            .btn_wide2   = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\WideButtonBlank02.dc6)")),
            .btn_narrow  = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\NarrowButtonBlank.dc6)")),
            .btn_short   = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\ShortButtonBlank.dc6)")),
            .credits_bg  = [&] {
                // creditsbckgexpand.dc6 (LoD) → creditsbckg.dc6 (classic).
                auto b = mpqs.try_read(R"(data\global\ui\CharSelect\creditsbckgexpand.dc6)");
                if (!b) b = mpqs.read(R"(data\global\ui\CharSelect\creditsbckg.dc6)");
                return d2d::dc6::Sprite(*b);
            }(),
            .charcreate_bg = [&] {
                auto b = mpqs.try_read(R"(data\global\ui\FrontEnd\charactercreationscreenEXP.dc6)");
                if (!b) b = mpqs.read(R"(data\global\ui\FrontEnd\CharacterCreate.dc6)");
                return d2d::dc6::Sprite(*b);
            }(),
            .fire       = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\fire.DC6)")),
            .medium_sel_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumSelButtonBlank.dc6)")),
            .textbox           = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\textbox.dc6)")),
            .clickbox          = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\clickbox.dc6)")),
            .charselect_bg     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\characterselectscreenEXP.dc6)")),
            .charselect_box    = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectbox.dc6)")),
            .charselect_boxgrey = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectboxgrey.dc6)")),
            .charselect_scroll = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\joingamescrollbars.dc6)")),
            .tall_button       = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\TallButtonBlank.dc6)")),
            .class_anims = [&] {
                // Anim files per class, in order {nu1, nu2, fw, nu3, bw}.
                // Class prefix pairs from FUN_004326f0's loader.
                struct C { const char* dir; const char* prefix; };
                constexpr C cs[7] = {
                    {"barbarian",   "ba"},
                    {"necromancer", "ne"},
                    {"paladin",     "pa"},
                    {"amazon",      "am"},
                    {"sorceress",   "so"},
                    {"druid",       "dz"},
                    {"assassin",    "as"},
                };
                constexpr const char* suffix[5] = {"nu1", "nu2", "fw", "nu3", "bw"};
                std::array<std::array<d2d::dc6::Sprite, 5>, 7> out;
                for (std::size_t ci = 0; ci < 7; ++ci) {
                    for (std::size_t si = 0; si < 5; ++si) {
                        char path[256];
                        std::snprintf(path, sizeof(path),
                            R"(data\global\ui\FrontEnd\%s\%s%s.dc6)",
                            cs[ci].dir, cs[ci].prefix, suffix[si]);
                        out[ci][si] = d2d::dc6::Sprite(mpqs.read(path));
                    }
                }
                return out;
            }(),
            .font        = d2d::font::Font(
                             mpqs.read(R"(data\local\FONT\LATIN\font16.tbl)"),
                             d2d::dc6::Sprite(mpqs.read(R"(data\local\FONT\LATIN\font16.dc6)"))),
            .credits     = [&] {
                auto b = mpqs.try_read(R"(data\local\UI\ENG\ExpansionCredits.txt)");
                if (!b) b = mpqs.try_read(R"(data\local\ui\eng\Credits.txt)");
                return b ? parse_credits_utf16(*b) : std::vector<std::string>{};
            }(),
            // Frontend button labels (IDs 0x13f2..0x13f7) live in the base
            // string.tbl per probe. patchstring.tbl (826 entries) overrides
            // specific IDs when Blizzard shipped patches; expansionstring.tbl
            // (2788 entries) carries LoD-specific additions. For MVP we use
            // string.tbl directly; when a subsystem needs a patch-shifted
            // entry, load all three and query in order (patch → expansion →
            // base).
            .strings     = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\string.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }(),
            .patch_strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\patchstring.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }(),
            .exp_strings = [&] {
                auto b = mpqs.try_read(R"(data\local\LNG\ENG\expansionstring.tbl)");
                return b ? d2d::tbl::Table(*b) : d2d::tbl::Table{};
            }(),
        };
        // Rogue-camp world data — separate call so a DS1/DT1 miss doesn't
        // nuke the whole scene; the InGame screen falls back to the credits
        // placeholder when world is empty.
        load_world(scene, mpqs,
                   R"(data\global\tiles\ACT1\TOWN\townE1.ds1)");
        scene.mpqs = std::move(mpqs);
        return scene;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] load_scene: %s\n", e.what());
        return std::nullopt;
    }
}

// Translate a DS1-embedded tileset path (e.g. "\d2\data\global\tiles\act1\
// town\floor.dt1") into the MPQ path we can hand to Stack::try_read. The
// DS1 files store paths as they were on Blizzard's build box, with a
// leading "\d2\" prefix and forward slashes never — normalize both.
[[nodiscard]] inline std::string ds1_path_to_mpq(std::string_view s) {
    if (s.size() > 4 && (s.starts_with("\\d2\\") || s.starts_with("/d2/")))
        s.remove_prefix(4);
    else if (!s.empty() && (s[0] == '\\' || s[0] == '/'))
        s.remove_prefix(1);
    std::string out(s);
    for (auto& c : out) if (c == '/') c = '\\';
    return out;
}

// Encode (style, sequence, type) into a single lookup key. Style + sequence
// are DS1-record bytes; type is the DT1 orientation code (0..16 per D2's
// tile-type table). 24 bits × 24 bits × 16 bits comfortably fits u64.
[[nodiscard]] inline std::uint64_t tile_key(int style, int seq, int type) {
    return (std::uint64_t(std::uint32_t(style)) << 40)
         | (std::uint64_t(std::uint32_t(seq  )) << 16)
         |  std::uint64_t(std::uint16_t(type ));
}

// Load one DS1 + every DT1 it references (silently skips missing ones —
// some rogue-camp DS1s reference .tg1 tile-group files, which aren't
// present in 1.14d). Populates world_ds1, world_dt1s, world_floor_lookup
// and act1_pal on the scene. Idempotent, called once during load_scene.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path) {
    auto b = mpqs.try_read(ds1_path);
    if (!b) {
        std::fprintf(stderr, "[d2d] world: %s not found — placeholder mode\n",
                     ds1_path);
        return;
    }
    scene.world_ds1 = d2d::ds1::Map(*b);
    scene.world_dt1s.reserve(scene.world_ds1.files().size());
    for (const auto& f : scene.world_ds1.files()) {
        const auto mpq_path = ds1_path_to_mpq(f);
        auto db = mpqs.try_read(mpq_path);
        if (!db) continue;   // .tg1 or otherwise-missing — silent skip
        try {
            scene.world_dt1s.emplace_back(*db);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[d2d] world: %s: %s\n",
                         mpq_path.c_str(), e.what());
        }
    }
    // Populate the (style, seq, type) lookup across all DT1s. First DT1
    // to define a tuple wins — matches how D2's renderer resolves tile
    // priority against its Stack-ordered tileset list. Covers floors,
    // walls, trees, roofs, shadows in one map.
    for (const auto& dt1 : scene.world_dt1s) {
        for (const auto& t : dt1.tiles()) {
            const auto k = tile_key(t.style, t.sequence, t.type);
            scene.world_tile_lookup.try_emplace(k, &t);
        }
    }
    if (auto pb = mpqs.try_read(R"(data\global\palette\ACT1\pal.dat)"))
        scene.act1_pal = d2d::palette::Palette(*pb);
}

// D2's base game/anim tick is 25 Hz — every animation rate in AnimData.d2
// is `25 * animRate / 256`. The frontend menu runs at that base rate;
// tying our advance to wall-clock ms keeps playback correct regardless of
// how fast we happen to be rendering (60 Hz, 120 Hz, headless, whatever).
constexpr std::uint32_t kBaseFrameMs = 40;   // 1000 / 25

void render_title(std::vector<std::uint8_t>& fb,
                  const Scene& s,
                  std::span<const Button> buttons,
                  std::uint32_t elapsed_ms) {
    // Full-screen background — no need to clear; the 4×3 grid tiles fill
    // exactly 800×600 with no gaps.
    blit_dc6_grid(fb, s.bg, s.pal, 0, 0, s.bg_tiles_across);

    // Diablo2.dc6 (menu record 0x708e00) is the CLASSIC-D2 static DIABLO II
    // logo — a 320×151 pre-baked title asset. In LoD it stays LOADED but
    // never drawn: the animated D2logo{Black,Fire}{L,R} pieces replace it,
    // and gameselectscreenEXP.dc6 already has "EXPANSION SET / Lord of
    // Destruction" baked into the background. Drawing both stacks two
    // "DIABLO II"s at different anchors — Bret caught the doubled letters.
    // RE: game.exe gates the classic-mode fallback on DAT_007795ec == 0
    // (the "expansion installed" flag; see char-create-table.md). We
    // never load classic-only, so `logo_static` is intentionally unused.
    (void)s.logo_static;

    // "DIABLO II" animated logo halves — per RE records at 0x708e30 and
    // 0x708e60, both anchored at (400, 120). Each DC6 frame's bottom-left
    // origin (per DCC convention) places itself relative to that anchor.
    // Black silhouettes first, then fire fills on top with the warm-tint
    // hack (PL2 hue-variation colormap remains a follow-up).
    const auto n_logo = s.logo_bl.frames_per_direction();
    const auto fi = (n_logo == 0) ? 0u
        : std::uint32_t((elapsed_ms / kBaseFrameMs) % n_logo);
    constexpr int kLogoAnchorX = 400;
    constexpr int kLogoAnchorY = 120;
    blit_at_anchor    (fb, s.logo_bl.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_at_anchor    (fb, s.logo_br.frame(0, fi), s.pal, kLogoAnchorX, kLogoAnchorY);
    blit_additive  (fb, s.logo_fl.frame(0, fi), s.pal, &s.sky_pl2, kLogoAnchorX, kLogoAnchorY);
    blit_additive  (fb, s.logo_fr.frame(0, fi), s.pal, &s.sky_pl2, kLogoAnchorX, kLogoAnchorY);

    // Buttons — chrome frames from RE'd assets: 2-frame (normal/pressed)
    // for Short/Medium, 4-frame two-piece composite for Wide/Narrow. Hover
    // brightens the label to gold; no chrome-only hover frame exists.
    for (const auto& b : buttons) {
        if (b.chrome) {
            blit_button_chrome(fb, s.pal, *b.chrome, b.x, b.y,
                               b.hovered && b.pressed);
        }
        if (b.label && *b.label) {
            const int lw = s.font.measure(b.label);
            const int lh = s.font.line_height();
            const int lx = b.x + (b.w - lw) / 2;
            const int ly = b.y + (b.h - lh) / 2;
            if (b.hovered) {
                // Gold hover — matches the highlight D2 draws through a
                // PL2 text-colour shift.
                s.font.draw_tinted(fb, kW, kH, s.pal, lx, ly, b.label,
                                   255, 208, 80);
            } else {
                s.font.draw(fb, kW, kH, s.pal, lx, ly, b.label);
            }
        }
    }

    s.font.draw(fb, kW, kH, s.pal, 8, int(kH) - 14, "d2d dev build");
}

// Character-select screen — RE FUN_004359d0 (init) + FUN_0042ef50 (BG draw).
// LoD layout: characterselectscreenEXP as BG, 2 columns × 4 rows of
// character slots (charselectbox / charselectboxgrey — 272x93 assembled).
// Four buttons: CREATE / DELETE (TallButtonBlank chrome, top row at y=528)
// and OK / EXIT (MediumSelButtonBlank, bottom row at y=572 shared with
// char-create per char-create-table.md).
//
// MVP: we have no persisted characters yet, so every slot renders as the
// empty (grey) variant and OK stays disabled until you actually make a
// character. CREATE hops to CharCreate. DELETE and OK are no-ops for now.
struct CharSelectUI {
    Button create_btn{};
    Button delete_btn{};
    Button cancel_btn{};
    Button ok_btn{};
    std::string create_label;
    std::string delete_label;
    std::string cancel_label;
    std::string ok_label;
    int selected = -1;           // index into Scene::saves, or -1
    int scroll = 0;              // index of the save in slot 0; always even
};

// Largest valid CharSelectUI::scroll for `n` saves: last row at the bottom.
int charselect_max_scroll(int n) {
    return std::max(0, (n + 1) / 2 * 2 - kSlots);
}

// Visible slot index under (x, y), or -1. Row-major: slot i = row i/2, col i%2.
int charselect_slot_at(int x, int y) {
    for (int i = 0; i < kSlots; ++i) {
        const int sx = kSlotX[i % 2], sy = kSlotY[i / 2];
        if (x >= sx && x < sx + kSlotW && y >= sy && y < sy + kSlotH) return i;
    }
    return -1;
}

void render_charselect(std::vector<std::uint8_t>& fb,
                       const Scene& s,
                       const CharSelectUI& ui,
                       std::uint32_t /*elapsed_ms*/) {
    const auto& pal = s.pal;   // char-select shares the Sky palette
    blit_dc6_grid(fb, s.charselect_bg, pal, 0, 0, s.bg_tiles_across);

    for (int i = 0; i < kSlots; ++i) {
        const int x = kSlotX[i % 2], y = kSlotY[i / 2];
        // Two-frame composite: main 256-wide half + 16-wide sliver.
        // Selected slot gets the filled box, the rest the grey one.
        const int si = ui.scroll + i;   // save index shown in this slot
        const auto& box = si == ui.selected ? s.charselect_box : s.charselect_boxgrey;
        if (box.frames_per_direction() >= 2) {
            blit_sprite(fb, box.frame(0, 0), pal, x,       y);
            blit_sprite(fb, box.frame(0, 1), pal, x + 256, y);
        }
        if (si >= int(s.saves.size())) continue;
        // ponytail: text only, no class portrait; add the DCC idle in the
        // 72px cell on the slot's right when the slot needs to look like D2's.
        const auto& h = s.saves[std::size_t(si)];
        const int ci = kSaveClassToUi[h.cls];
        std::string cls = kClassKey[ci];
        if (auto v = lookup_string(s, kClassKey[ci])) cls = u16_to_latin1(*v);
        const std::string line2 = "Level " + std::to_string(h.level) + " " + cls;
        s.font.draw_tinted(fb, kW, kH, pal, x + 12, y + 20, h.name.c_str(),
                           255, 208, 80);
        s.font.draw(fb, kW, kH, pal, x + 12, y + 40, line2.c_str());
        if (h.hardcore())
            s.font.draw_tinted(fb, kW, kH, pal, x + 12, y + 60, "Hardcore",
                               255, 64, 64);
    }

    if (const int max = charselect_max_scroll(int(s.saves.size()));
        max > 0 && s.charselect_scroll.frames_per_direction() >= 5) {
        const auto& sb = s.charselect_scroll;
        blit_sprite(fb, sb.frame(0, 0), pal, kScrollX, kScrollTop);
        blit_sprite(fb, sb.frame(0, 1), pal, kScrollX, kScrollBot - kScrollArrow);
        // Thumb slides between the arrows, proportional to the scroll row.
        const int track = kScrollBot - kScrollTop - 3 * kScrollArrow;
        blit_sprite(fb, sb.frame(0, 4), pal, kScrollX,
                    kScrollTop + kScrollArrow + track * ui.scroll / max);
    }

    if (s.saves.empty()) {
        // Empty-list placeholder text — centred on the panel.
        constexpr const char* empty1 = "NO CHARACTERS YET";
        constexpr const char* empty2 = "click CREATE NEW CHARACTER to start";
        const int w1 = s.font.measure(empty1);
        const int w2 = s.font.measure(empty2);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - w1/2, 260,
                           empty1, 255, 208, 80);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - w2/2, 280,
                           empty2, 200, 200, 200);
    }

    // Buttons. TallButtonBlank is single-piece 168x60 (frames 0/1 for
    // normal/pressed); MediumSelButtonBlank uses blit_button_chrome.
    auto draw_tall = [&](const Button& b, bool /*disabled*/=false) {
        if (!b.chrome) return;
        const auto& fr = b.chrome->frame(0, b.hovered && b.pressed ? 1 : 0);
        blit_sprite(fb, fr, pal, b.x, b.y);
        if (b.label && *b.label) {
            const int lw = s.font.measure(b.label);
            const int lh = s.font.line_height();
            s.font.draw(fb, kW, kH, pal,
                        b.x + (b.w - lw) / 2,
                        b.y + (b.h - lh) / 2, b.label);
        }
    };
    draw_tall(ui.create_btn);
    draw_tall(ui.delete_btn);

    for (const Button* b : {&ui.cancel_btn, &ui.ok_btn}) {
        if (!b->chrome) continue;
        blit_button_chrome(fb, pal, *b->chrome, b->x, b->y,
                           b->hovered && b->pressed);
        if (b->label && *b->label) {
            const int lw = s.font.measure(b->label);
            const int lh = s.font.line_height();
            const int lx = b->x + (b->w - lw) / 2;
            const int ly = b->y + (b->h - lh) / 2;
            // OK is grey — we have no character to play.
            if (b == &ui.ok_btn)
                s.font.draw_tinted(fb, kW, kH, pal, lx, ly, b->label, 96, 96, 96);
            else
                s.font.draw(fb, kW, kH, pal, lx, ly, b->label);
        }
    }
}

// Full-screen credits background + scrolling text. The scroll starts with
// the first line off the bottom of the screen and advances upward at ~1 px
// per D2 tick (25 Hz). When the last line clears the top, the scroll loops.
void render_credits(std::vector<std::uint8_t>& fb,
                    const Scene& s,
                    std::uint32_t elapsed_ms) {
    // creditsbckgexpand.dc6 has the same 4×3 sub-frame grid as the title
    // background; both add up to exactly 800×600.
    blit_dc6_grid(fb, s.credits_bg, s.pal, 0, 0, s.bg_tiles_across);

    if (s.credits.empty()) {
        s.font.draw(fb, kW, kH, s.pal, 300, 300, "(no credits.txt found)");
    } else {
        // Line pitch: font16 line-height + 4 px spacing.
        const int pitch = s.font.line_height() + 6;
        const int total_h = int(s.credits.size()) * pitch;
        // scroll_y = distance the first line has moved above the bottom.
        // 40 ms per tick = D2 base; advance one px per tick.
        const int scroll = int(elapsed_ms / kBaseFrameMs);
        // total scroll cycle: total_h + kH (start at bottom, end past top).
        const int cycle = total_h + int(kH);
        const int off   = scroll % (cycle > 0 ? cycle : 1);
        // Base y for line 0.
        int y = int(kH) - off;
        // Gold-ish tint for section headers, approximating D2's PL2
        // hue-shift. Regular lines draw untinted (255,255,255 = pass-through).
        constexpr std::uint8_t kHdrR = 255, kHdrG = 208, kHdrB = 80;
        for (const auto& line : s.credits) {
            if (y > int(kH))          { y += pitch; continue; }
            if (y + pitch < 0)        { y += pitch; continue; }
            const bool header = !line.empty() && line.front() == '*';
            std::string_view text = line;
            if (header) text.remove_prefix(1);
            if (text.empty())         { y += pitch; continue; }
            const int lw = s.font.measure(text);
            const int x = int(kW) / 2 - lw / 2;
            if (header) {
                s.font.draw_tinted(fb, kW, kH, s.pal, x, y, text,
                                   kHdrR, kHdrG, kHdrB);
            } else {
                s.font.draw(fb, kW, kH, s.pal, x, y, text);
            }
            y += pitch;
        }
    }

    // Small hint at the bottom-left so anyone can find their way back.
    s.font.draw(fb, kW, kH, s.pal, 8, int(kH) - 14,
                "d2d dev build — click or Esc to return");
}

// Character-creation screen — the iconic seven-classes-around-a-campfire
// scene, loaded by FUN_004326f0. For MVP we blit each class's nu1 (idle)
// cycle at hardcoded positions matching the D2 layout, plus the fire
// animation in the pit. Selection / hover / class labels are follow-ups.

// D2 iso-diamond tile dimensions. Each cell footprint = 160x80; each
// step in x moves (+80, +40) on screen, each step in y moves (-80, +40).
// See OpenDiablo2's mapengine for the same convention.
constexpr int kIsoW = 160;
constexpr int kIsoH = 80;

// Blit one DT1 tile's pre-decoded palette-indexed pixels through `pal`.
// The tile's pixel buffer is (tile.width x abs(tile.height)); index 0 is
// transparent. Screen position is the buffer's top-left; caller does the
// iso math to place it. Bounds-checked per-pixel — off-screen tiles are
// clipped rather than skipped so the compositor can walk the whole grid.
void blit_dt1_tile(std::vector<std::uint8_t>& fb,
                   const d2d::dt1::Tile& t,
                   const d2d::palette::Palette& pal,
                   int sx, int sy) {
    const int th = std::abs(t.height);
    for (int y = 0; y < th; ++y) {
        const int py = sy + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = t.pixels.data() + std::size_t(y) * t.width;
        for (int x = 0; x < t.width; ++x) {
            const std::uint8_t idx = row[x];
            if (idx == 0) continue;   // transparent
            const int px = sx + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 0xFF;
        }
    }
}

// Watchdog — a background thread that fires a diagnostic when the main
// thread stops advancing its heartbeat. This is our only visibility
// into a beachball, because a hung main thread stops running our
// per-frame `slow frame` / `alive` prints. The watchdog needs to touch
// NO SDL state (SDL is main-thread only on macOS); it only reads two
// atomics and writes to stderr.
enum class MainPhase : std::uint32_t {
    Idle           = 0,
    PollEvents     = 1,
    Devctl         = 2,
    Render         = 3,
    Upload         = 4,   // SDL_UpdateTexture
    Present        = 5,   // SDL_RenderPresent
    PaceDelay      = 6,   // SDL_Delay at end of frame
    // InGame sub-phases so we can pinpoint the stuck one exactly.
    IngameClear    = 10,
    IngameFloor    = 11,
    IngameShadow   = 12,
    IngameWalls    = 13,
    IngamePlayer   = 14,
    IngameHudText  = 15,
};
inline const char* main_phase_name(std::uint32_t p) {
    switch (MainPhase(p)) {
        case MainPhase::Idle:          return "idle";
        case MainPhase::PollEvents:    return "poll-events";
        case MainPhase::Devctl:        return "devctl-pump";
        case MainPhase::Render:        return "render";
        case MainPhase::Upload:        return "sdl-update-texture";
        case MainPhase::Present:       return "sdl-render-present";
        case MainPhase::PaceDelay:     return "sdl-delay-pace";
        case MainPhase::IngameClear:   return "ingame:fb-clear";
        case MainPhase::IngameFloor:   return "ingame:world-floor";
        case MainPhase::IngameShadow:  return "ingame:world-shadow";
        case MainPhase::IngameWalls:   return "ingame:world-walls";
        case MainPhase::IngamePlayer:  return "ingame:player-dcc";
        case MainPhase::IngameHudText: return "ingame:hud-text";
    }
    return "?";
}

// Set by the main loop before entering render_ingame so the watchdog
// can report which sub-phase we're stuck in. Declared ahead of
// render_ingame so it can write the ingame sub-phases.
inline std::atomic<std::uint32_t>* g_current_phase_ptr = nullptr;
inline void set_phase(MainPhase p) {
    if (g_current_phase_ptr)
        g_current_phase_ptr->store(std::uint32_t(p),
                                   std::memory_order_relaxed);
}

// Render the loaded DS1 onto the framebuffer, centered on grid cell
// (camera_cx, camera_cy). Draws in D2's back-to-front Z order:
//   1. All floor tiles (type=0) in row order — the ground plane
//   2. All shadow tiles (type=13) in row order — soft dark decals
//   3. Walls / trees / roofs per row, top-back rows first, so
//      lower/nearer rows can occlude higher/farther ones
//
// Missing tile lookups are silent — a cell whose (style, seq, type)
// tuple isn't in any loaded DT1 leaves whatever's below it showing
// through, which is the same behaviour D2 itself has for stripped
// tilesets.
void render_world(std::vector<std::uint8_t>& fb,
                  const Scene& s,
                  int camera_cx, int camera_cy) {
    const auto& m = s.world_ds1;
    if (m.width() == 0 || m.height() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int cx0 = int(kW) / 2;
    const int cy0 = int(kH) / 2;
    const int mw  = m.width();

    // Iso footprint for the 800x600 window: each screen cell is 160x80.
    // A ±12 grid-cell window around the camera covers > 2× screen area,
    // leaving room for tall walls (up to 128+px) to reach in from cells
    // that are off-screen at their base.
    constexpr int kR = 12;

    // Blit a single tile at cell (gx, gy)'s iso position, honouring the
    // 80-tall-diamond-at-bottom convention shared by floor/wall pixel
    // buffers.
    auto blit_cell = [&](int gx, int gy, const d2d::dt1::Tile& t) {
        const int dx = gx - camera_cx;
        const int dy = gy - camera_cy;
        const int iso_x = cx0 + (dx - dy) * (kIsoW / 2);
        const int iso_y = cy0 + (dx + dy) * (kIsoH / 2);
        const int th = std::abs(t.height);
        const int sx = iso_x - t.width / 2;
        const int sy = iso_y - (th - kIsoH);
        blit_dt1_tile(fb, t, pal, sx, sy);
    };

    auto find_tile = [&](int style, int seq, int type)
        -> const d2d::dt1::Tile* {
        const auto it = s.world_tile_lookup.find(tile_key(style, seq, type));
        return it == s.world_tile_lookup.end() ? nullptr : it->second;
    };

    // Row-major sweep so back rows render first. dy increases downward
    // in screen space, so we iterate low→high dy for back-to-front.
    for (int dy = -kR; dy <= kR; ++dy) {
        for (int dx = -kR; dx <= kR; ++dx) {
            const int gx = camera_cx + dx;
            const int gy = camera_cy + dy;
            if (gx < 0 || gy < 0 || gx >= mw || gy >= m.height()) continue;
            const std::size_t off = std::size_t(gy) * mw + gx;

            // Floor (single layer typical). Type 0 in the floor stream
            // is the "no floor here" marker (dropped by the game); we
            // still need to look up type=0 for actual floors from DT1s.
            for (const auto& fl : m.floors()) {
                const auto& c = fl.cells[off];
                if (c.hidden) continue;
                if (c.style == 0 && c.sequence == 0 && c.wall_type == 0) {
                    // Rogue-camp floors often have (0, 0, 0) as literal
                    // grass tile — draw it. Only skip cells the DS1
                    // marks hidden.
                }
                if (auto* t = find_tile(c.style, c.sequence, /*type=*/0))
                    blit_cell(gx, gy, *t);
            }

            // Shadow layer — 50% alpha decals under characters/objects.
            // For MVP we blit them as regular tiles (index-0 transparent);
            // proper Pl2 blend50 compositing is a follow-up.
            for (const auto& sh : m.shadows()) {
                const auto& c = sh.cells[off];
                if (c.hidden) continue;
                if (c.style == 0 && c.sequence == 0 && c.wall_type == 0) continue;
                if (auto* t = find_tile(c.style, c.sequence, /*type=*/13))
                    blit_cell(gx, gy, *t);
            }
        }
    }

    set_phase(MainPhase::IngameWalls);
    // Walls / trees / roofs — same row-major sweep, per-cell one-pass
    // draw. All non-floor orientation types share the same iso
    // positioning; the DT1 tile's own y_shift + per-block y encode the
    // vertical layout, so height-varying elements (columns, trees, roofs)
    // land correctly relative to the cell iso anchor without special
    // per-type math here. Roofs (type 15) get a small extra vertical
    // hoist from the DS1 orientation dword's upper 24 bits when
    // present — for MVP we use the DT1's per-tile roof_height instead.
    for (int dy = -kR; dy <= kR; ++dy) {
        for (int dx = -kR; dx <= kR; ++dx) {
            const int gx = camera_cx + dx;
            const int gy = camera_cy + dy;
            if (gx < 0 || gy < 0 || gx >= mw || gy >= m.height()) continue;
            const std::size_t off = std::size_t(gy) * mw + gx;
            for (const auto& wl : m.walls()) {
                const auto& c = wl.cells[off];
                if (c.hidden) continue;
                const int type = c.wall_type;
                if (type == 0) continue;         // floor marker in wall stream
                if (type == 13) continue;        // shadow (drawn above)
                if (auto* t = find_tile(c.style, c.sequence, type)) {
                    if (type == 15) {
                        // Roof — hoist by the DT1's own roof_height plus
                        // any DS1-encoded offset in wall_zero's upper bits.
                        const int dx_ = gx - camera_cx;
                        const int dy_ = gy - camera_cy;
                        const int iso_x = cx0 + (dx_ - dy_) * (kIsoW / 2);
                        const int iso_y = cy0 + (dx_ + dy_) * (kIsoH / 2)
                                              - t->roof_height;
                        const int th = std::abs(t->height);
                        const int sx = iso_x - t->width / 2;
                        const int sy = iso_y - (th - kIsoH);
                        blit_dt1_tile(fb, *t, pal, sx, sy);
                    } else {
                        blit_cell(gx, gy, *t);
                    }
                }
            }
        }
    }
}

// In-game placeholder — a hero has been created; we don't have the actual
// world/map render yet, so celebrate the character info and offer Esc to
// go back to the title. Using the credits bg (dark corridor) as backdrop.
// Blit a DCC frame with its origin at (anchor_x, anchor_y). A DCC frame's
// y_offset is its BOTTOM row relative to the origin (feet), so the pixel
// block's top-left is (box_left, box_top). Palette-indexed; index 0 is
// transparent so limbs compose cleanly over each other and over tiles.
void blit_dcc_frame(std::vector<std::uint8_t>& fb,
                    const d2d::dcc::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y) {
    const int dst_x = anchor_x + f.box_left;
    const int dst_y = anchor_y + f.box_top;
    for (std::int32_t y = 0; y < f.height; ++y) {
        const int py = dst_y + y;
        if (py < 0 || py >= int(kH)) continue;
        const auto* row = f.pixels.data() + std::size_t(y) * f.width;
        for (std::int32_t x = 0; x < f.width; ++x) {
            const auto idx = row[x];
            if (idx == 0) continue;
            const int px = dst_x + x;
            if (px < 0 || px >= int(kW)) continue;
            const auto c = pal[idx];
            auto* p = fb.data() + (std::size_t(py) * kW + px) * 4;
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 0xFF;
        }
    }
}

// Render the picked class's town-idle composite at the camera-center tile:
// every loaded body layer, in the COF's per-(direction, frame) draw order.
// Frame time from the COF speed byte: D2 advances speed/256 frames per
// 25 Hz tick, so one frame lasts 40 ms * 256 / speed (BA 80 → 128 ms).
// ponytail: COF speed as the rate; AnimData.d2 is authoritative — read it
// when an animation visibly runs at the wrong pace. No shadow, no
// transparent-layer draw effects yet (no TN layer sets `transparent`).
void render_player_at_camera(std::vector<std::uint8_t>& fb,
                             const Scene& s,
                             int class_idx,
                             std::uint32_t elapsed_ms) {
    const auto& p = s.player_anim(class_idx);
    const auto dirs = p.cof.directions();
    const auto fpd  = p.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return;
    // Direction 4 of 16 = SW (facing screen).
    const auto dir = std::uint8_t(std::min(4, dirs - 1));
    const auto ms_per_frame = 40u * 256u / std::max<std::uint32_t>(p.cof.speed(), 1);
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    // Camera-center cell top-corner projects to screen center; the
    // character's feet plant at the diamond bottom center, which is
    // (kW/2, kH/2 + kIsoH/2).
    for (const auto type : p.cof.priority(dir, frame)) {
        if (type >= p.layers.size()) continue;
        const auto& spr = p.layers[type];
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        blit_dcc_frame(fb, spr.frame(dir, frame), pal,
                       int(kW) / 2, int(kH) / 2 + kIsoH / 2);
    }
}

void render_ingame(std::vector<std::uint8_t>& fb,
                   const Scene& s,
                   int class_idx,
                   std::string_view name,
                   bool hardcore,
                   int camera_cx,
                   int camera_cy,
                   std::uint32_t elapsed_ms) {
    // Prefer the real tile-composited world when townE1.ds1 loaded; fall
    // back to the credits DC6 placeholder when it didn't (headless CI, a
    // stripped MPQ dir, etc.). Palette follows the render path: ACT1 for
    // the tiles, Sky for the credits DC6 which was authored against it.
    if (!s.world_dt1s.empty()) {
        set_phase(MainPhase::IngameClear);
        // Clear to black — tiles don't cover every subtile so an
        // uninitialized fb would leak the previous frame's contents.
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        set_phase(MainPhase::IngameFloor);   // render_world does floor+shadow+walls internally
        render_world(fb, s, camera_cx, camera_cy);
        // Player sprite sits on top of the floor. Follows the class
        // picked on char-create; falls through silently for classes
        // whose DCC didn't load.
        if (class_idx >= 0 && class_idx < 7) {
            set_phase(MainPhase::IngamePlayer);
            render_player_at_camera(fb, s, class_idx, elapsed_ms);
        }
        set_phase(MainPhase::IngameHudText);
    } else {
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        blit_dc6_grid(fb, s.credits_bg, s.pal, 0, 0, s.bg_tiles_across);
    }
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;

    std::string cls = kClassKey[class_idx];
    if (auto v = lookup_string(s, kClassKey[class_idx])) cls = u16_to_latin1(*v);

    constexpr const char* welcome = "WELCOME TO SANCTUARY";
    const int ww = s.font.measure(welcome);
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ww/2, 220,
                       welcome, 255, 208, 80);

    // On hardcore, D2 marks the caption with a red " (HC)" suffix — we
    // fudge that with a red tint on the trailing tag.
    const std::string line = name.empty() ? cls : std::string(name) + " the " + cls;
    const int lw = s.font.measure(line);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - lw/2, 260, line);
    if (hardcore) {
        constexpr const char* tag = " (HARDCORE)";
        const int tw = s.font.measure(tag);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - lw/2 + lw, 260,
                           tag, 220, 60, 60);
        (void)tw;
    }

    constexpr const char* hint =
        "d2d dev build — townE1.ds1 rendering; walls + objects + scroll are next";
    const int hw = s.font.measure(hint);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - hw/2, int(kH) - 60, hint);
    constexpr const char* esc = "press Esc to return to title";
    const int ew = s.font.measure(esc);
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ew/2, int(kH) - 40,
                       esc, 200, 200, 200);
}

// Advance the per-class state machine — completes one-shot animations
// (Selecting → Selected, Deselecting → Idle) once they finish.
void advance_char_states(CharCreateUI& ui,
                         const Scene& s,
                         std::uint32_t elapsed_ms) {
    for (std::size_t i = 0; i < 7; ++i) {
        auto& cu = ui.classes[i];
        if (cu.state == ClassState::Selecting) {
            const auto& fw = s.class_anims[i][2];
            const auto n = fw.frames_per_direction();
            const auto elapsed = elapsed_ms - cu.state_start_ms;
            if (n == 0 || elapsed / kBaseFrameMs >= n) {
                cu.state = ClassState::Selected;
                cu.state_start_ms = elapsed_ms;
            }
        } else if (cu.state == ClassState::Deselecting) {
            const auto& bw = s.class_anims[i][4];
            const auto n = bw.frames_per_direction();
            const auto elapsed = elapsed_ms - cu.state_start_ms;
            if (n == 0 || elapsed / kBaseFrameMs >= n) {
                cu.state = ClassState::Idle;
                cu.state_start_ms = elapsed_ms;
            }
        }
    }
}

// Hit-test click position against class silhouettes and trigger selection
// transitions. Only one class is Selected/Selecting at a time; picking a
// new one first sends the previous into Deselecting.
// Hardcore-checkbox rect — RE'd char-create master table 0x70b0b0.
constexpr int kHardcoreX = 319, kHardcoreW = 15, kHardcoreH = 16;
constexpr int kHardcoreY = rec_top(560, kHardcoreH);

void handle_charcreate_click(CharCreateUI& ui,
                             const Mouse& m,
                             std::uint32_t elapsed_ms) {
    if (!m.release_this_frame) return;
    // Hardcore checkbox toggle — the click zone is the RE'd hitbox at
    // 0x70b080 (339, 561, 100, 32) unioned with the box chrome itself, so
    // clicking either the box OR its label toggles.
    const int hcRx = kHardcoreX, hcRw = kHardcoreW + 5 + 100;   // box + gap + label
    const int hcRy = rec_top(561, 32), hcRh = 32;              // 0x70b080's rows
    if (m.x >= hcRx && m.x < hcRx + hcRw &&
        m.y >= hcRy && m.y < hcRy + hcRh) {
        ui.hardcore = !ui.hardcore;
        return;
    }
    // Class hitboxes ARE the (x, y, w, h) rects from the RE'd records — 88×184
    // per class, position varies. D2 uses the same rects for hover + click
    // detection AND for sprite placement anchor.
    for (int i = 0; i < 7; ++i) {
        const auto p = kClassPos[i];
        if (m.x >= p.x && m.x < p.x + p.w && m.y >= p.y && m.y < p.y + p.h) {
            if (ui.selected == i) return;   // clicked selected class → no-op
            // Deselect old.
            if (ui.selected >= 0) {
                auto& prev = ui.classes[ui.selected];
                prev.state = ClassState::Deselecting;
                prev.state_start_ms = elapsed_ms;
            }
            auto& cur = ui.classes[i];
            cur.state = ClassState::Selecting;
            cur.state_start_ms = elapsed_ms;
            ui.selected = i;
            return;
        }
    }
}

void render_charcreate(std::vector<std::uint8_t>& fb,
                       const Scene& s,
                       const CharCreateUI& ui,
                       std::uint32_t elapsed_ms) {
    // fechar palette per FUN_00435580 RE.
    const auto& pal = s.charselect_pal;

    blit_dc6_grid(fb, s.charcreate_bg, pal, 0, 0, s.bg_tiles_across);

    // Campfire — RE record 0x70ae70: (x=345, y=470, w=110, h=127).
    // D2's drawer treats (x, y) as sprite origin (feet-of-flame); anchor
    // for BOTTOM-LEFT DC6 convention = (x + w/2, y + h). A shadow layer
    // exists at (345, 454) — same handle, 16 px higher — draw both.
    const auto nf = s.fire.frames_per_direction();
    if (nf > 0) {
        const auto ff = std::uint32_t(((elapsed_ms + 7) / kBaseFrameMs) % nf);
        // Shadow first, then main flame on top.
        blit_additive(fb, s.fire.frame(0, ff), pal, &s.fechar_pl2, 345 + 55, 454 + 127);
        blit_additive(fb, s.fire.frame(0, ff), pal, &s.fechar_pl2, 345 + 55, 470 + 127);
    }

    // Per-class draw: pick anim + frame based on state, place at the RE'd
    // rect's centre-bottom (matches D2's per-class positioning).
    for (std::size_t i = 0; i < 7; ++i) {
        const auto& cu = ui.classes[i];
        const auto p = kClassPos[i];
        int anim = 0;
        std::uint32_t elapsed_for_frame = elapsed_ms + std::uint32_t(i) * 7;
        bool one_shot = false;
        switch (cu.state) {
        case ClassState::Idle:        anim = 0; break;
        case ClassState::Selecting:   anim = 2; one_shot = true; break;
        case ClassState::Selected:    anim = 3; break;
        case ClassState::Deselecting: anim = 4; one_shot = true; break;
        }
        const auto& spr = s.class_anims[i][anim];
        const auto n = spr.frames_per_direction();
        if (n == 0) continue;
        std::uint32_t fi;
        if (one_shot) {
            const auto e = elapsed_ms - cu.state_start_ms;
            fi = std::min<std::uint32_t>(e / kBaseFrameMs, n - 1);
        } else {
            fi = std::uint32_t((elapsed_for_frame / kBaseFrameMs) % n);
        }
        // Anchor is the box's centre-bottom point; each frame's own DC6
        // offset places the actual pixels relative to that anchor.
        blit_at_anchor(fb, spr.frame(0, fi), pal, p.x + p.w / 2, p.y + p.h);
    }

    // Selected class name — big warm-gold caption above the panel area.
    // TBL-sourced ("Amazon" / "Sorceress" / etc. all live under those exact
    // keys in string.tbl per a probe of the file).
    std::string caption;
    if (ui.selected >= 0) {
        if (auto v = lookup_string(s, kClassKey[ui.selected]))
            caption = u16_to_latin1(*v);
        else
            caption = kClassKey[ui.selected];
    } else {
        caption = "SELECT HERO CLASS";
    }
    {
        const int w = s.font.measure(caption);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - w/2, 22, caption,
                           255, 208, 80);
    }

    // Name-entry field — per RE record 0x70b290: (319, 519, 169, 26)
    // with textbox.dc6 chrome. Draw the chrome, then the typed name
    // centered inside. Prompt "Character Name" shows above the box.
    if (s.textbox.frames_per_direction() > 0) {
        // Above-box prompt (string.tbl id 0x1405 = "Character Name"; a
        // TBL scan of the 0x1380..0x1500 id band confirmed this).
        std::string np_owned;
        const char* nprompt = "CHARACTER NAME";
        if (auto v = lookup_string(s, std::uint16_t(0x1405))) {
            np_owned = u16_to_latin1(*v);
            if (!np_owned.empty()) nprompt = np_owned.c_str();
        }
        const int npw = s.font.measure(nprompt);
        constexpr int kNameTop = rec_top(519, 26);
        s.font.draw_tinted(fb, kW, kH, pal, 319 + (169 - npw)/2, kNameTop - 14,
                           nprompt, 200, 200, 200);
        blit_sprite(fb, s.textbox.frame(0, 0), pal, 319, kNameTop);
        // Typed name over the box.
        const int nw = s.font.measure(ui.input_name);
        const int nlh = s.font.line_height();
        s.font.draw_tinted(fb, kW, kH, pal,
                           319 + (169 - nw) / 2,
                           kNameTop + (26 - nlh) / 2,
                           ui.input_name, 255, 208, 80);
        // Simple blinking cursor after the last char (D2 uses a blinking
        // underline; we use a solid "|" for now).
        if (((elapsed_ms / 500) & 1) == 0) {
            s.font.draw_tinted(fb, kW, kH, pal,
                               319 + (169 - nw) / 2 + nw,
                               kNameTop + (26 - nlh) / 2,
                               "|", 255, 208, 80);
        }
    }

    // Hardcore checkbox — chrome from clickbox.dc6 (frame 0 unchecked,
    // 1 checked). Label rendered to the LEFT of the box in the pale
    // grey D2 uses for inactive-but-toggleable text; goes bright gold
    // when checked.
    if (s.clickbox.frames_per_direction() >= 2 && !ui.hardcore_label.empty()) {
        const auto& fr = s.clickbox.frame(0, ui.hardcore ? 1 : 0);
        blit_sprite(fb, fr, pal, kHardcoreX, kHardcoreY);
        // Label rendered right of the box — RE'd hitbox 0x70b080 sits at
        // x=339 (20px right of the box's x=319), covering the label's
        // click zone.
        const int lh = s.font.line_height();
        const int lx = kHardcoreX + kHardcoreW + 5;
        const int ly = kHardcoreY + (kHardcoreH - lh) / 2;
        if (ui.hardcore)
            s.font.draw_tinted(fb, kW, kH, pal, lx, ly,
                               ui.hardcore_label, 255, 208, 80);
        else
            s.font.draw_tinted(fb, kW, kH, pal, lx, ly,
                               ui.hardcore_label, 180, 180, 180);
    }

    // OK / EXIT buttons at the bottom — MediumSelButtonBlank chrome.
    // OK renders muted grey when disabled (no class picked yet or empty name).
    for (const Button* b : {&ui.cancel_btn, &ui.ok_btn}) {
        const bool disabled = (b == &ui.ok_btn) && !b->do_switch;
        if (!b->chrome) continue;
        blit_button_chrome(fb, pal, *b->chrome, b->x, b->y,
                           b->hovered && b->pressed);
        if (b->label && *b->label) {
            const int lw = s.font.measure(b->label);
            const int lh = s.font.line_height();
            const int lx = b->x + (b->w - lw) / 2;
            const int ly = b->y + (b->h - lh) / 2;
            if (disabled)
                s.font.draw_tinted(fb, kW, kH, pal, lx, ly, b->label, 96, 96, 96);
            else if (b->hovered)
                s.font.draw_tinted(fb, kW, kH, pal, lx, ly, b->label, 255,208,80);
            else
                s.font.draw(fb, kW, kH, pal, lx, ly, b->label);
        }
    }

    s.font.draw(fb, kW, kH, pal, 8, int(kH) - 14,
                "d2d dev build — pick class, type name, hit OK");
}

// --- SDL3 render loop ------------------------------------------------------

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
        if (!w) { std::fprintf(stderr, "[d2d] SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
        r = SDL_CreateRenderer(w, nullptr);
        if (!r) { std::fprintf(stderr, "[d2d] SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
        // VSync avoids tearing AND caps our frame rate at the monitor
        // refresh — the primary yield mechanism. Failure is not fatal;
        // pace_frame() delays anyway as a floor.
        SDL_SetRenderVSync(r, 1);
        SDL_SetRenderLogicalPresentation(r, w_, h_, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        // RGBA32 is defined as ABGR8888 on LE / RGBA8888 on BE — memory order
        // is always (r, g, b, a), matching our framebuffer.
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING, w_, h_);
        if (!t) { std::fprintf(stderr, "[d2d] SDL_CreateTexture: %s\n", SDL_GetError()); return false; }
        // Linear filter on upscale, as thirdeye does.
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return true;
    }
};

// D2 TBL values are UTF-16; our font is Latin-1. Downcast char by char.
std::string u16_to_latin1(std::u16string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char16_t c : s) {
        // Keep printable Latin-1 (0x20..0xFF), drop the rest — D2 UI strings
        // are ASCII with occasional accented chars, all inside Latin-1.
        if (c >= 0x20 && c <= 0xFF) out.push_back(char(c));
    }
    return out;
}

// Store labels alongside the buttons so we own the string memory through
// the frame. Called once at startup; string lookups happen only there.
struct TitleUI {
    std::vector<Button>     buttons;
    std::vector<std::string> labels;   // owns text buffers Button.label points into
};

// Build the main-menu button list. Positions from RE (LoD variant records
// 0x709010..0x7090a0 + shared bottom row 0x708f80..0x708fe0). Labels sourced
// from patchstring.tbl by ID (from the +0x18 field of each menu record) —
// falls back to a plausible English string when the TBL entry is missing.
TitleUI title_ui(const Scene& s) {
    struct Spec {
        int x, y, w, h;
        std::uint16_t tbl_id;
        const char* fallback;
        const d2d::dc6::Sprite* chrome;
        bool do_switch = false;
        Screen goto_screen = Screen::Title;
        bool quit = false;
    };
    // ID map derived from menu records — see docs/research/re/frontend-menu-table.md.
    const Spec specs[] = {
        {264, 324, 272, 35, 0x13f2, "SINGLE PLAYER",     &s.btn_wide,
            true, Screen::CharSelect, false},
        {264, 366, 272, 35, 0x13f3, "BATTLE.NET",        &s.btn_wide2},
        {264, 391, 272, 25, 0,      "GATEWAY: LOCAL",    &s.btn_narrow},
        {264, 433, 272, 35, 0x13f4, "OTHER MULTIPLAYER", &s.btn_wide},
        {264, 528, 135, 25, 0x13f6, "CREDITS",           &s.btn_short,
            true, Screen::Credits, false},
        {402, 528, 135, 25, 0x13f7, "CINEMATICS",        &s.btn_short},
        {264, 568, 272, 35, 0x13f5, "EXIT DIABLO II",    &s.btn_wide,
            false, Screen::Title, true},
    };
    TitleUI ui;
    ui.labels.reserve(sizeof(specs) / sizeof(specs[0]));
    ui.buttons.reserve(ui.labels.capacity());
    for (const auto& sp : specs) {
        std::string label;
        if (sp.tbl_id) {
            if (auto v = lookup_string(s, sp.tbl_id)) label = u16_to_latin1(*v);
        }
        if (label.empty() && sp.fallback) label = sp.fallback;
        ui.labels.push_back(std::move(label));
        ui.buttons.push_back(Button{
            sp.x, rec_top(sp.y, sp.h), sp.w, sp.h,
            ui.labels.back().c_str(),
            sp.chrome, sp.goto_screen, sp.do_switch, sp.quit, false, false,
        });
    }
    return ui;
}

// Turn SDL mouse + text events into a per-tick snapshot. Rising/falling
// edges are recomputed each tick from the raw button state. When the
// active screen has a text field, the caller flips SDL text input on/off.
struct PanKeys {
    bool& left; bool& right; bool& up; bool& down;
};

void handle_sdl_events(SDL_Event& ev, Mouse& m, Screen& current_screen,
                       std::string& text_input, bool& text_backspace,
                       PanKeys pan, bool& mouse_seen,
                       std::atomic<bool>& quit) {
    // SDL_EVENT_QUIT fires on app-level termination (Cmd-Q, all windows
    // closed). WINDOW_CLOSE_REQUESTED fires when a specific window's ✕
    // is clicked — SDL3 does NOT auto-promote it to QUIT. Both mean
    // "user wants out" for us since we're single-window.
    if (ev.type == SDL_EVENT_QUIT ||
        ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        quit = true; return;
    }
    // Cmd-Q backup — some window managers eat the SDL_EVENT_QUIT.
    if (ev.type == SDL_EVENT_KEY_DOWN &&
        ev.key.key == SDLK_Q && (ev.key.mod & SDL_KMOD_GUI)) {
        quit = true; return;
    }
    auto pan_flag = [&](SDL_Keycode k, bool v) {
        switch (k) {
            case SDLK_A: case SDLK_LEFT:  pan.left  = v; break;
            case SDLK_D: case SDLK_RIGHT: pan.right = v; break;
            case SDLK_W: case SDLK_UP:    pan.up    = v; break;
            case SDLK_S: case SDLK_DOWN:  pan.down  = v; break;
            default: break;
        }
    };
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        if (ev.key.key == SDLK_ESCAPE) {
            // Esc pops one layer up:
            //   Title       -> quit
            //   CharCreate  -> CharSelect  (the flow you came from)
            //   InGame      -> CharSelect  (leaving the game returns to
            //                               the roster; matches D2)
            //   everything else -> Title
            switch (current_screen) {
                case Screen::Title:      quit = true; break;
                case Screen::CharCreate:
                case Screen::InGame:     current_screen = Screen::CharSelect; break;
                default:                 current_screen = Screen::Title; break;
            }
        } else if (ev.key.key == SDLK_BACKSPACE) {
            text_backspace = true;
        } else {
            pan_flag(ev.key.key, true);
        }
    } else if (ev.type == SDL_EVENT_KEY_UP) {
        pan_flag(ev.key.key, false);
    } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
        // ev.text.text is UTF-8; keep the printable Latin-1 subset.
        for (const char* p = ev.text.text; *p; ++p) {
            const auto c = static_cast<unsigned char>(*p);
            if (c >= 0x20 && c <= 0x7e) text_input.push_back(char(c));
        }
    } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        m.x = int(ev.motion.x);
        m.y = int(ev.motion.y);
        mouse_seen = true;
    } else if (ev.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        // Kill mouse-edge pan when the pointer leaves the window — a
        // cursor sitting on the terminal (or anywhere off-window)
        // otherwise pins the last-seen edge and pans forever.
        mouse_seen = false;
    } else if (ev.type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
        mouse_seen = true;
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        m.x = int(ev.button.x);
        m.y = int(ev.button.y);
        if (ev.button.button == SDL_BUTTON_LEFT) {
            m.down = true;
            m.press_this_frame = true;
        }
    } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        m.wheel += ev.wheel.integer_y;
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        m.x = int(ev.button.x);
        m.y = int(ev.button.y);
        if (ev.button.button == SDL_BUTTON_LEFT) {
            m.down = false;
            m.release_this_frame = true;
        }
    }
}

// Set by main() before entering the loop — a lazy way to plumb --start-*
// through without threading extra parameters everywhere.
static std::string g_start_screen;
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
    return Screen::Title;
}

static const char* screen_name(Screen s) {
    switch (s) {
        case Screen::Title:      return "title";
        case Screen::Credits:    return "credits";
        case Screen::CharSelect: return "charselect";
        case Screen::CharCreate: return "charcreate";
        case Screen::InGame:     return "ingame";
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
        std::fprintf(stderr, "[d2d] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Window win;
    if (!win.open(int(kW), int(kH), g_scale)) { SDL_Quit(); return 1; }

    Screen screen = g_start_screen.empty() ? Screen::Title
                                            : parse_screen(g_start_screen);
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

    // Char-select UI. Labels from string.tbl: 0x1498 = DELETE, 0x1499 =
    // CREATE NEW CHARACTER; EXIT and OK reuse 0x13ed / 0x13ee (same as
    // char-create's bottom row per RE'd char-select master table at
    // 0x70ac00..0x70ae40). Positions verbatim from RE'd records.
    CharSelectUI csu;
    if (scene) {
        auto tbl_label = [&](std::uint16_t id, const char* fallback) {
            if (auto v = lookup_string(*scene, id)) return u16_to_latin1(*v);
            return std::string(fallback);
        };
        csu.create_label = tbl_label(0x1499, "CREATE NEW CHARACTER");
        csu.delete_label = tbl_label(0x1498, "DELETE");
        csu.cancel_label = tbl_label(0x13ed, "EXIT");
        csu.ok_label     = tbl_label(0x13ee, "OK");
        csu.create_btn = Button{ 233, rec_top(528, 60), 168, 60, csu.create_label.c_str(),
                                 &scene->tall_button,
                                 Screen::CharCreate, /*do_switch=*/true };
        csu.delete_btn = Button{ 433, rec_top(528, 60), 168, 60, csu.delete_label.c_str(),
                                 &scene->tall_button,
                                 Screen::CharSelect, /*do_switch=*/false };
        csu.cancel_btn = Button{ 33, rec_top(572, 35), 128, 35, csu.cancel_label.c_str(),
                                 &scene->medium_sel_button,
                                 Screen::Title, /*do_switch=*/true };
        csu.ok_btn     = Button{ 627, rec_top(572, 35), 128, 35, csu.ok_label.c_str(),
                                 &scene->medium_sel_button,
                                 Screen::InGame, /*do_switch=*/false };
    }

    const auto t0 = SDL_GetTicks();
    // Text-input is enabled ONCE for the lifetime of the window. Reason:
    // SDL_StartTextInput() / SDL_StopTextInput() on macOS talk to the
    // system IME, which can stall the main thread — a real user of ours
    // hit a beachball right after the last alive print on CharCreate,
    // and the toggle is the only per-screen SDL call that changes there.
    // Callers gate the text-input consumers themselves (only CharCreate
    // reads text_this_frame); TEXT_INPUT events for other screens are
    // handed to the frame but no consumer picks them up.
    SDL_StartTextInput(win.w);
    // Camera position on the InGame world (in DS1 cells). Seeded to the
    // middle of the loaded map — arrow keys / WASD / mouse-edge pan from
    // there. Persists across frames so panning is continuous rather than
    // step-per-keypress.
    int camera_cx = g_start_cam_x >= 0 ? g_start_cam_x
                    : (scene && !scene->world_dt1s.empty()
                        ? scene->world_ds1.width()  / 2 : 0);
    int camera_cy = g_start_cam_y >= 0 ? g_start_cam_y
                    : (scene && !scene->world_dt1s.empty()
                        ? scene->world_ds1.height() / 2 : 0);
    std::uint32_t last_ms = 0;
    // Camera-pan latched key state (updated from SDL_EVENT_KEY_*). Using
    // discrete events instead of SDL_GetKeyboardState works whether or not
    // the window has real keyboard focus (headless CI, background test
    // spawns, etc.) — SDL_GetKeyboardState was hanging under the harness's
    // detached-window scenario.
    bool pan_left = false, pan_right = false, pan_up = false, pan_down = false;
    bool mouse_seen = false;

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
                std::fprintf(stderr,
                    "[d2d] MAIN STUCK: %u ms in phase='%s' (frame not advancing)\n",
                    since, main_phase_name(phase));
                std::fflush(stderr);
                last_reported = now;
            }
        }
    });

    // Input + state verbs for scripted tests. Registered here because they
    // touch loop locals; the channel is only pumped inside this loop, so
    // the captures never outlive it.
    ch.on("click", [&](const std::vector<std::string>& args) {
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
                          .button = SDL_BUTTON_LEFT,
                          .down = type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                          .clicks = 1, .x = x, .y = y };
            SDL_PushEvent(&ev);
        }
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
    ch.on("state", [&](const std::vector<std::string>&) {
        return std::string("screen=") + screen_name(screen)
             + " save=" + std::to_string(csu.selected)
             + " scroll=" + std::to_string(csu.scroll)
             + " class=" + std::to_string(cc.selected)
             + " name=" + cc.input_name
             + " hardcore=" + (cc.hardcore ? "1" : "0")
             + " cam=" + std::to_string(camera_cx) + "," + std::to_string(camera_cy)
             + " saves=" + std::to_string(scene ? scene->saves.size() : 0)
             + "\nok\n";
    });

    while (!quit) {
        // Signal-driven quit — Ctrl-C / SIGTERM. The atomic write from
        // d2d_sigint_handler is polled here; SDL_EVENT_QUIT and window
        // close still work through handle_sdl_events.
        if (g_sigint_quit) { quit = true; break; }
        const std::uint32_t frame_start_ms = std::uint32_t(SDL_GetTicks());
        heartbeat_ms.store(frame_start_ms, std::memory_order_relaxed);

        mouse.press_this_frame = false;
        mouse.release_this_frame = false;
        mouse.wheel = 0;
        std::string text_this_frame;
        bool        backspace_this_frame = false;
        current_phase.store(std::uint32_t(MainPhase::PollEvents),
                            std::memory_order_relaxed);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // SDL3 doesn't rescale event coords under logical presentation;
            // convert so the mouse lands in 800x600 game pixels.
            SDL_ConvertEventToRenderCoordinates(win.r, &ev);
            handle_sdl_events(ev, mouse, screen, text_this_frame,
                              backspace_this_frame,
                              PanKeys{pan_left, pan_right, pan_up, pan_down},
                              mouse_seen, quit);
        }
        current_phase.store(std::uint32_t(MainPhase::Devctl),
                            std::memory_order_relaxed);
        if (ch.active()) ch.pump();

        current_phase.store(std::uint32_t(MainPhase::Render),
                            std::memory_order_relaxed);
        const auto ms = std::uint32_t(SDL_GetTicks() - t0);
        if (scene) {
            switch (screen) {
            case Screen::Title:
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
                if (mouse.press_this_frame) {
                    const int slot = charselect_slot_at(mouse.x, mouse.y);
                    if (slot >= 0 && csu.scroll + slot < n)
                        csu.selected = csu.scroll + slot;
                    // Scrollbar arrows (only live while the bar is shown).
                    if (max_scroll > 0 && mouse.x >= kScrollX
                        && mouse.x < kScrollX + 12) {
                        if (mouse.y >= kScrollTop && mouse.y < kScrollTop + kScrollArrow)
                            rows = -1;
                        if (mouse.y >= kScrollBot - kScrollArrow && mouse.y < kScrollBot)
                            rows = 1;
                    }
                }
                csu.scroll = std::clamp(csu.scroll + 2 * rows, 0, max_scroll);
                // OK only enters the game with a save picked.
                csu.ok_btn.do_switch = csu.selected >= 0;
                for (Button* b : {&csu.create_btn, &csu.delete_btn,
                                  &csu.cancel_btn, &csu.ok_btn})
                    update_button(*b, mouse, screen, quit);
                if (screen == Screen::InGame) {
                    // Load the picked save into the in-game character.
                    const auto& h = scene->saves[std::size_t(csu.selected)];
                    cc.selected   = kSaveClassToUi[h.cls];
                    cc.input_name = h.name;
                    cc.hardcore   = h.hardcore();
                }
                render_charselect(fb, *scene, csu, ms);
                break;
            }
            case Screen::InGame: {
                // ESC handled globally in handle_sdl_events (returns to Title).
                // Camera pan: WASD or arrow keys, plus D2-style mouse-edge
                // scroll when the pointer sits in the outer 16px of the
                // window. Rate: ~6 cells/sec, budgeted from real elapsed
                // ms so pan speed is frame-rate-independent.
                if (!scene->world_dt1s.empty()) {
                    // float, not uint32: int kx * uint32 dt promotes -1 to
                    // 4294967295u, sending ax past 2^24 where `ax -= 1.f`
                    // is a no-op — the pan-left/up beachball.
                    const float dt = float(ms - last_ms);
                    int kx = 0, ky = 0;
                    if (pan_left)  --kx;
                    if (pan_right) ++kx;
                    if (pan_up)    --ky;
                    if (pan_down)  ++ky;
                    // Mouse-edge scroll only kicks in after the pointer has
                    // moved at least once (mouse_seen), so the initial
                    // (0, 0) default doesn't drift the camera to (0, 0).
                    constexpr int kEdge = 16;
                    if (mouse_seen) {
                        if (mouse.x < kEdge)            --kx;
                        if (mouse.x >= int(kW) - kEdge) ++kx;
                        if (mouse.y < kEdge)            --ky;
                        if (mouse.y >= int(kH) - kEdge) ++ky;
                    }
                    // Fractional accumulator so 6 cells/sec = 6*dt/1000
                    // and diagonals don't jitter.
                    static float ax = 0.f, ay = 0.f;
                    ax += kx * dt * 0.006f;
                    ay += ky * dt * 0.006f;
                    while (ax >=  1.f) { ++camera_cx; ax -= 1.f; }
                    while (ax <= -1.f) { --camera_cx; ax += 1.f; }
                    while (ay >=  1.f) { ++camera_cy; ay -= 1.f; }
                    while (ay <= -1.f) { --camera_cy; ay += 1.f; }
                    // Keep camera inside the DS1 grid.
                    const int mw = scene->world_ds1.width();
                    const int mh = scene->world_ds1.height();
                    if (camera_cx < 0)   camera_cx = 0;
                    if (camera_cy < 0)   camera_cy = 0;
                    if (camera_cx >= mw) camera_cx = mw - 1;
                    if (camera_cy >= mh) camera_cy = mh - 1;
                }
                render_ingame(fb, *scene, std::max(cc.selected, 0),
                              cc.input_name, cc.hardcore,
                              camera_cx, camera_cy, ms);
                break;
            }
            case Screen::CharCreate: {
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
        ++frame_count;
        last_ms = ms;
        // Per-frame diagnostics — break the frame into `render` (all our
        // CPU blits, filling the framebuffer) and `present` (SDL upload +
        // clear + texture + present, which is where Metal can stall). The
        // 5s alive line prints both averages, both maxes, AND the current
        // camera position so we can correlate a stall with a specific
        // area of the map.
        const std::uint32_t dt_render  = t_after_render  - frame_start_ms;
        const std::uint32_t dt_present = t_after_present - t_after_render;
        static std::uint32_t stat_frames = 0;
        static std::uint32_t stat_render_sum = 0, stat_render_max = 0;
        static std::uint32_t stat_present_sum = 0, stat_present_max = 0;
        static std::uint32_t stat_last_report_ms = 0;
        ++stat_frames;
        stat_render_sum  += dt_render;
        stat_present_sum += dt_present;
        if (dt_render  > stat_render_max)  stat_render_max  = dt_render;
        if (dt_present > stat_present_max) stat_present_max = dt_present;
        // Any single phase > 100ms is a stall candidate — log it with
        // whichever phase spiked so we can tell CPU-side from GPU-side.
        if (dt_render > 100 || dt_present > 100) {
            std::fprintf(stderr,
                "[d2d] slow frame: render=%u ms present=%u ms screen=%d cam=(%d,%d)\n",
                dt_render, dt_present, int(screen), camera_cx, camera_cy);
        }
        if (ms - stat_last_report_ms >= 5000) {
            const std::uint32_t avg_r = stat_frames ? stat_render_sum  / stat_frames : 0;
            const std::uint32_t avg_p = stat_frames ? stat_present_sum / stat_frames : 0;
            std::fprintf(stderr,
                "[d2d] alive: %u frames/5s | render avg=%u max=%u | present avg=%u max=%u | screen=%d cam=(%d,%d)\n",
                stat_frames, avg_r, stat_render_max, avg_p, stat_present_max,
                int(screen), camera_cx, camera_cy);
            stat_frames = 0;
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
    d2d::userdir::Config cfg;
    for (const auto& d : { d2d::userdir::global_dir("d2d"), fs::path("."), user_dir })
        d2d::userdir::load_cfg(d / "d2d.cfg", cfg);

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
        ->check(CLI::IsMember({"title", "credits", "charselect", "charcreate", "ingame"}));
    app.add_option("--start-class", start_class,
                   "Preselect a class index (0..6)")
        ->check(CLI::Range(0, 6));
    app.add_option("--start-name", start_name,
                   "Preload character name");
    app.add_flag  ("--start-hardcore", start_hardcore,
                   "Preload the Hardcore checkbox");
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

    std::vector<std::uint8_t> fb(std::size_t(kW) * kH * 4, 0);
    for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
    auto scene = load_scene(data_dir);   // nullopt if MPQ dir is missing
    if (scene) scene->saves = load_saves(save_dir);

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
    g_start_class    = start_class;
    g_start_name     = start_name;
    g_start_hardcore = start_hardcore;
    g_start_cam_x    = start_cam_x;
    g_start_cam_y    = start_cam_y;
    g_scale          = std::clamp(scale, 1, 8);   // cfg value isn't CLI-checked

    if (headless) {
        if (!ch.active()) {
            std::printf("d2d: --headless with no --devctl has nothing to do. exiting.\n");
            return 0;
        }
        // Same loop as the windowed build on SDL's dummy video driver +
        // software renderer: no window, no GL, but events, devctl and
        // screenshots all work. (The "offscreen" driver needs EGL, which
        // macOS doesn't have.)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    return run_windowed(fb, scene, ch, frame_count, quit);
}
