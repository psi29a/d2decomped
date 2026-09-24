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
#include <compcode.hpp>
#include <dc6.hpp>
#include <dcc.hpp>
#include <devctl.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <dt1.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <screenshot.hpp>
#include <tbl.hpp>
#include <txt.hpp>
#include <userdir.hpp>

#include "npc_menu.hpp"
#include "npc_talk.hpp"
#include "speech_sound.hpp"
#include "obj_preset.hpp"

#include <SDL3/SDL.h>
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
    d2d::dc6::Sprite      medium_button;      // MediumButtonBlank.dc6 — char-select OK/EXIT (handle 0x77973c, Sky palette)
    d2d::dc6::Sprite      medium_sel_button;  // MediumSelButtonBlank.dc6 — char-create OK/EXIT chrome (per FUN_004326f0)
    d2d::dc6::Sprite      textbox;            // textbox.dc6 — name-entry chrome (single 169×26 frame)
    d2d::dc6::Sprite      clickbox;           // clickbox.dc6 — Hardcore checkbox chrome (2 frames × 15×16, unchecked/checked)
    // Character-select screen assets (RE FUN_004359d0, handle 0x00779734).
    // Slot chrome is 2-frame 256+16 wide × 93 tall (matches WideButton
    // composite pattern). BG is 12-frame 4×3 grid of ≤256×256 tiles.
    d2d::dc6::Sprite      charselect_bg;      // characterselectscreenEXP.dc6
    d2d::dc6::Sprite      charselect_box;     // charselectbox.dc6 (filled slot)
    d2d::dc6::Sprite      charselect_scroll;  // FrontEnd\joingamescrollbars.dc6
    d2d::dc6::Sprite      tall_button;        // TallButtonBlank.dc6 (168×60) — CREATE / DELETE
    d2d::dc6::Sprite      cursor;             // CURSOR\ohand.dc6 — D2's gauntlet, 8 frames
    // Character composites (in-game player, char-select portraits). The
    // COF names the body-part layers and their per-frame draw order; each
    // layer is its own DCC, indexed by COF composite type (0 HD, 1 TR,
    // 2 LG, 3 RA, 4 LA, 5 RH, 6 LH, 7 SH, 8.. S1..S8). What each layer
    // wears comes from 16 appearance bytes — a .d2s header's, or a new
    // character's starting gear — through `comp` (components/compcode).
    struct PlayerAnim {
        d2d::cof::Cof                     cof;
        std::array<d2d::dcc::Sprite, 16>  layers;
        std::string                       name;       // COF base name, e.g. "AITW1HS"
        std::uint32_t                     speed = 0;  // animdata.d2 rate (256 = a frame per tick)
    };
    // data\global\animdata.d2: COF name -> animation speed. The game's
    // rate source; COFs of walk/run modes store 0.
    std::unordered_map<std::string, std::uint32_t> anim_speed;
    using Appearance = std::array<std::uint8_t, 16>;
    std::vector<d2d::compcode::Entry> comp;         // appearance byte -> component
    std::array<Appearance, 7>         starting_gear{};  // per d2s class, CharStats.txt
    // Loaded on first use and kept — decoding every composite up front
    // doubled startup. `mutable` so the const Scene renderers can fill it.
    mutable std::map<std::array<std::uint8_t, 18>, PlayerAnim> composites;
    const PlayerAnim& composite(int d2s_class, int mode, const Appearance& gfx) const;
    // Kept open for lazy loads after startup.
    d2d::mpq::Stack mpqs;
    // True when 1.14d patch data is layered in. Without it patchstring.tbl
    // is the CD's (826 entries), whose IDs don't match what 1.14d code asks
    // for (10832 is "CREATE NEW" in 1.14d, "Bonus to Attack Rating" on CD).
    bool patched = false;
    std::vector<std::int64_t> exp_next;          // experience.txt: exp for level+1, by level
    std::array<std::int64_t, 3> resist_penalty{ 0, -40, -100 };   // DifficultyLevels.txt
    // Class animations — 7 classes × 5 states, per the RE'd class table at
    // 0x00708a00. State order matches D2's suffix scheme: nu1, nu2, fw,
    // nu3, bw. Class order (rows in the table): assassin, druid, amazon,
    // necromancer, barbarian, sorceress, paladin — but we store them in
    // our left-to-right visual order (barb/necro/pally/ama/sorc/druid/assn)
    // to match Scene::class positions.
    std::array<std::array<d2d::dc6::Sprite, 5>, 7> class_anims;
    d2d::font::Font       font;
    d2d::font::Font       font_small;         // font8 — long panel values
    d2d::font::Font       font_tiny;          // font6 — panel labels
    // Credits.txt / ExpansionCredits.txt parsed to plain Latin-1 lines.
    // A '*' prefix on a line marks a section header in D2's format.
    std::vector<std::string> credits;
    // Character saves from <user dir>/save/*.d2s, most recently played first,
    // with each save's items (empty if they couldn't be parsed).
    std::vector<d2d::d2s::Header> saves;
    std::vector<std::vector<d2d::d2s::Item>> save_items;
    std::vector<d2d::d2s::Stats> save_stats;
    // Items: parse tables (needs 1.14d ItemStatCost.txt), per-code
    // inventory graphic + size, and the 800x600 inventory panel/layouts.
    std::optional<d2d::d2s::ItemTables> item_tables;
    struct ItemInfo { std::string invfile; int w = 1, h = 1; std::string namestr, type; int kind = 0;  // kind: 0 misc, 1 armor, 2 weapon
                      int belt = -1; };                  // armor.txt belt: belts.txt index
    std::unordered_map<std::string, std::array<std::string, 2>> type_equiv;   // ItemTypes Equiv1/2
    // gems.txt socket bonuses by gem/rune code, per slot kind (weapon,
    // helm/armour, shield), already resolved through Properties.txt.
    std::unordered_map<std::string, std::array<std::vector<d2d::d2s::ItemProp>, 3>> gem_props;
    // String keys for item names, indexed the way the save's IDs are (see
    // item_lines): uniques/sets by row without separators, magic affixes by
    // raw row, rare names by raw row, runewords by RunewordN rank.
    struct ItemNames {
        std::vector<std::string> unique, set, prefix, suffix, rare_pre, rare_suf, runeword;
    } item_names;
    // ItemStatCost.txt description columns, by stat ID, and what the skill
    // descfuncs need: skill name keys by skill ID, CharStats strings by class.
    struct StatDesc {
        int prio = 0, func = 0, val = 0, op = 0, op_param = 0, dgrp = 0, dgrp_func = 0, dgrp_val = 0;
        std::string pos, neg, str2, dgrp_pos, dgrp_neg, dgrp_str2;
    };
    std::vector<StatDesc> stat_desc;
    std::vector<std::string> skill_name;         // string key
    std::vector<int>         skill_class;        // CharStats row, -1 none
    struct ClassStrs { std::string all_skills, tab[3], only; };
    std::array<ClassStrs, 7> class_strs;
    std::unordered_map<std::string, ItemInfo> item_info;
    mutable std::unordered_map<std::string, std::optional<d2d::dc6::Sprite>> item_sprites;
    // An item's inventory graphic: the unique's/set item's own invfile,
    // else the picture variant (ItemTypes InvGfx<n>), else the base's.
    const d2d::dc6::Sprite* item_sprite(const d2d::d2s::Item& it) const;
    std::vector<std::string> unique_inv, set_inv;             // invfile, rows as item_names
    // belts.txt 800x600 rows ("belt2" .. "uber belt", after the Expansion
    // separator) by armor.txt `belt` index: box count and boxes 1..16
    // {left, right, top, bottom}. Index 2 ("default") when no belt is worn.
    struct Belt { int boxes = 4; std::array<std::array<int, 4>, 16> box{}; };
    std::array<Belt, 7> belts{};
    d2d::dc6::Sprite popbelt;                          // PANEL\ctrlpnl_popbelt
    std::unordered_map<std::string, std::array<std::string, 6>> type_invgfx;
    struct InvLayout {
        int panel_x = 400, panel_y = 60;
        int grid_x = 0, grid_y = 0, box_w = 29, box_h = 29;
        std::array<std::array<int, 4>, 11> slots{};   // by body slot 1..10: x, y, w, h
    };
    std::array<InvLayout, 7> inv_layout{};            // by d2s class
    // Stash: inventory.txt "Bank Page2" (classic, 6x4) / "Big Bank Page2"
    // (expansion, 6x8) grids; art PANEL\bank / PANEL\TradeStash (game.exe
    // loads the latter only for expansion games, FUN_00489e50), drawn in
    // the left-panel spot like the char panel (0x48f1a4).
    std::array<InvLayout, 2> stash_layout{};          // [expansion]
    std::array<d2d::dc6::Sprite, 2> stash_panel;
    // Horadric Cube: inventory.txt "Transmogrify Box2" (3x4), art
    // PANEL\supertransmogrifier (FUN_0048a4b0), same left-panel spot
    // (panel 0xe, 0x48eeca).
    InvLayout cube_layout{};
    d2d::dc6::Sprite cube_panel;
    d2d::dc6::Sprite inv_panel;                       // PANEL\invchar6.dc6
    d2d::dc6::Sprite ctrl_panel, globes, globe_glass; // 800ctrlpnl7 / hlthmana / overlap
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
    // Walkability: every floor/wall tile's 5x5 subtile flags OR'd onto
    // its cell, (width*5) x (height*5), row-major. 0x01 blocks walking,
    // 0x08 blocks player walking (DT1 subtile flag bits).
    std::vector<std::uint8_t> world_walk;
    // Things the DS1 places (its object list): NPCs (type 1, from
    // data\global\monsters) and objects (type 2 — torches, fires, the
    // waypoint..., from data\global\objects). Both are composites.
    struct Npc {
        std::string root;                    // "monsters" or "objects"
        std::string code;                    // <root>\<code>\ ...
        std::string mode;                    // animation mode token (NU, ON...)
        std::string base_w;                  // weapon class ("hth" for objects)
        std::array<std::string, 16> comp;    // per layer, "" = not present
        float x = 0, y = 0;
        int size_x = 0, size_y = 0;          // collision footprint, subtiles
        std::string name;                    // hover label; "" = not selectable
        std::vector<std::pair<float, float>> path;   // DS1 patrol points, cells
        float velocity = 3;                  // MonStats Velocity
        int operate_fn = 0;                  // objects.txt OperateFn (32: the town stash)
        int hc_idx = -1;                     // MonStats hcIdx (NPC menu table key)
    };
    d2d::dc6::Sprite focus16;                          // UI\CURSOR\focus16: menu hover marks
    d2d::font::Font  font_formal11;                    // FontFormal11: NPC speech (font id 8)
    // Sounds.txt by Index: file (under data\global\sfx or, for speech,
    // data\local\sfx) and volume 0..255.
    struct Sound { std::string file; int volume = 255; bool loop = false, music = false; };
    std::vector<Sound> sounds;
    int town_song = 0, town_ambience = 0;               // SoundEnviron.txt for the town level
    fs::path data_dir;                                  // the MPQs' folder
    // CharStats WalkVelocity / RunVelocity by d2s class. Running adds
    // run*100/walk - 100 to velocitypercent (FUN_00620e80): +50%.
    std::array<int, 7> walk_velocity{ 6, 6, 6, 6, 6, 6, 6 }, run_velocity{ 9, 9, 9, 9, 9, 9, 9 };
    std::vector<Npc> world_npcs;
    mutable std::map<std::string, PlayerAnim> npc_anims;   // by root/code/mode
    const PlayerAnim& npc_anim(const Npc& n, std::string_view mode) const;
    [[nodiscard]] bool blocked(float x, float y) const {
        const int w = world_ds1.width() * 5, h = world_ds1.height() * 5;
        const int sx = int(std::floor(x * 5)), sy = int(std::floor(y * 5));
        if (sx < 0 || sy < 0 || sx >= w || sy >= h || world_walk.empty()) return true;
        return world_walk[std::size_t(sy) * std::size_t(w) + std::size_t(sx)] & 0x09;
    }
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
    // Numeric IDs are banked, not layered (RE'd from char-select: 0x58cb =
    // 22731 resolves to expansionstring[2731] "EXPANSION CHARACTER"):
    //   0..9999 string.tbl, 10000..19999 patchstring, 20000+ expansionstring.
    // Trying every table with the raw ID hits the wrong one — string.tbl
    // 2731 is "Bile".
    if (id >= 10000 && id < 20000 && !s.patched) return std::nullopt;
    const auto& t = id >= 20000 ? s.exp_strings : id >= 10000 ? s.patch_strings : s.strings;
    const auto local = std::uint16_t(id >= 20000 ? id - 20000 : id >= 10000 ? id - 10000 : id);
    if (auto v = t.get(local); v && !v->empty()) return v;
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
constexpr int kUiToSaveClass[7] = { 4, 2, 3, 0, 1, 5, 6 };

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
// Scrollbar: record 0xa7 is a text box with flag 4 ("has scrollbar"),
// whose child scrollbar (kind 5, FUN_005084f0) takes its geometry from
// {585, 457, 363} at 0x708d00: x, bottom y, height. Art is
// joingamescrollbars.dc6 (f0/f2 up, f1/f3 down normal/pressed, f4 thumb,
// f5 track; 12x14 each). Shown only when saves > 8; one step = one row =
// 2 saves (callback 0x439df0). Draw math from FUN_00508370, bottom-left
// anchored like every D2 cel: down arrow at bottom y-1, track tiles every
// 10 px above it, up arrow at bottom (y-h)+9, thumb at bottom
// (h-30)*pos/max - h + 19 + y.
constexpr int kScrollX = 585, kScrollBottom = 457, kScrollH = 363, kScrollArrow = 14;
constexpr int kScrollUpTop   = rec_top(kScrollBottom - kScrollH + 9, kScrollArrow);  // 90
constexpr int kScrollDownTop = rec_top(kScrollBottom - 1, kScrollArrow);             // 443

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
    // Second label line (char-select's tall buttons: FUN_00500bf0 adds
    // one under the record's own label). Last so positional inits hold.
    const char*             label2  = nullptr;
};

struct Mouse {
    int  x = 0, y = 0;
    bool down = false;                 // current button state
    bool press_this_frame = false;     // rising edge
    bool release_this_frame = false;   // falling edge
    bool rpress_this_frame = false;    // right button, rising edge
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
// Is item type `t` (or an Equiv ancestor) `want`?
bool type_is(const Scene& s, const std::string& t, std::string_view want, int depth = 0) {
    if (t.empty() || depth > 8) return false;
    if (t == want) return true;
    const auto e = s.type_equiv.find(t);
    return e != s.type_equiv.end()
        && (type_is(s, e->second[0], want, depth + 1) || type_is(s, e->second[1], want, depth + 1));
}

// What a filled socket adds to `parent`: a jewel's own properties, or the
// gem/rune's gems.txt bonus for the parent's kind (weapon, shield, else
// helm/armour).
std::vector<d2d::d2s::ItemProp> socket_props(const Scene& s, const d2d::d2s::Item& parent,
                                             const d2d::d2s::Item& filled) {
    auto out = filled.props;
    const auto g = s.gem_props.find(filled.code);
    const auto info = s.item_info.find(parent.code);
    if (g == s.gem_props.end() || info == s.item_info.end()) return out;
    const int k = info->second.kind == 2 ? 0 : type_is(s, info->second.type, "shld") ? 2 : 1;
    const auto& add = g->second[std::size_t(k)];
    out.insert(out.end(), add.begin(), add.end());
    return out;
}

// The char panel's computed values (stat 30 next level, 31 defence,
// resistances 39/43/41/45), as FUN_004a7d00 shows them. From the save's
// base stats and gear.
// ponytail: equipped slots 1..10 (the primary weapon set), socket
// bonuses and charms; no set bonuses, skills or auras.
struct PanelStats {
    std::int64_t next = -1, defense = 0;
    std::array<std::int64_t, 4> res{};           // fire, cold, lightning, poison
};

PanelStats panel_stats(const Scene& s, const d2d::d2s::Header& h,
                       const std::vector<d2d::d2s::Item>& items, const d2d::d2s::Stats& st) {
    PanelStats p;
    const auto lvl = st.get(d2d::d2s::kLevel);
    if (lvl >= 0 && std::size_t(lvl) + 1 < s.exp_next.size()) p.next = s.exp_next[std::size_t(lvl)];
    std::array<std::int64_t, 64> sum{};
    auto add = [&](const std::vector<d2d::d2s::ItemProp>& props) {
        for (const auto& pr : props) if (pr.stat >= 0 && pr.stat < 64) sum[std::size_t(pr.stat)] += pr.value;
    };
    std::int64_t item_def = 0, per_level = 0;
    for (const auto& it : items) {
        const bool worn = it.location == 1 && it.slot >= 1 && it.slot <= 10;
        const bool charm = it.location == 0 && it.panel == 1
                        && (it.code == "cm1" || it.code == "cm2" || it.code == "cm3");
        if (!worn && !charm) continue;
        add(it.props);
        for (const auto& j : it.socketed_items) add(socket_props(s, it, j));
        std::int64_t ed = 0;
        for (const auto& pr : it.props) {
            if (pr.stat == 16) ed += pr.value;            // item_armor_percent: this item's base
            if (pr.stat == 214) per_level += pr.value;    // item_armor_perlevel, 1/8 per level
        }
        if (it.defense > 0) item_def += it.defense * (100 + ed) / 100;
    }
    p.defense = item_def + sum[31] + per_level * lvl / 8 + st.get(d2d::d2s::kDex) / 4;
    const int diff = h.active_difficulty();
    const std::int64_t penalty = h.expansion() ? s.resist_penalty[std::size_t(diff)]
                                               : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(diff)];
    constexpr int kRes[4] = { 39, 43, 41, 45 };
    for (int i = 0; i < 4; ++i) {
        const auto cap = std::min<std::int64_t>(75 + sum[std::size_t(kRes[i] + 1)], 95);
        p.res[std::size_t(i)] = std::clamp<std::int64_t>(sum[std::size_t(kRes[i])] + penalty, -100, cap);
    }
    return p;
}

// An open NPC menu, as game.exe builds and lays it out (FUN_004b4830,
// FUN_004b85f0, FUN_004b8410; docs/research/re/npc-menu.md): a gold
// font16 header with the NPC's name (21 px line), the npc_menu.hpp
// entries and "cancel" (string 0x102e), 15 px lines, white. Box: widest
// line + 20 by the line heights + 15, placed at (cx - w/2, cy - h/3) where
// (cx, cy) is the NPC's feet on screen raised 150 px (FUN_004b1c80), then
// kept inside the screen.
struct NpcMenuState {
    int npc = -1;                            // world_npcs index, -1 = closed
    // What choosing a line does. ponytail: trade/hire/gamble/... just close.
    enum Action { kClose, kTalk, kIntro, kGossip };
    struct Line { std::string text; int height = 15, width = 0, x = 0; bool header = false; Action action = kClose; };
    std::vector<Line> lines;
    int x = 0, y = 0, w = 0, h = 0;
    // Index of the selectable line under (mx, my), or -1.
    [[nodiscard]] int line_at(int mx, int my) const {
        if (npc < 0 || mx < x || mx >= x + w) return -1;
        int base = y;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            base += lines[i].height;
            if (!lines[i].header && my > base - lines[i].height && my <= base) return int(i);
        }
        return -1;
    }
};

std::string string_id(const Scene& s, std::uint16_t id) {
    const auto v = lookup_string(s, id);
    return v ? u16_to_latin1(*v) : std::string{};
}

void layout_npc_menu(const Scene& s, NpcMenuState& m, int screen_x, int screen_y);

NpcMenuState open_npc_menu(const Scene& s, int npc, int screen_x, int screen_y) {
    NpcMenuState m;
    const auto& n = s.world_npcs[std::size_t(npc)];
    const auto it = std::ranges::find_if(kNpcMenus, [&](const NpcMenu& e) { return e.hc_idx == n.hc_idx; });
    if (it == kNpcMenus.end()) return m;
    m.npc = npc;
    m.lines.push_back({ n.name, 21, 0, 0, true });
    for (const auto id : it->entries)
        if (id) m.lines.push_back({ string_id(s, id), 15, 0, 0, false,
                                    id == 0xd35 ? NpcMenuState::kTalk : NpcMenuState::kClose });
    m.lines.push_back({ string_id(s, 0x102e), 15 });
    layout_npc_menu(s, m, screen_x, screen_y);
    return m;
}

// The talk submenu (FUN_004b5890): header "talk" (gold), "introduction"
// unless the NPC's talk record says no_intro, "gossip", then "cancel"
// (0xd48). ponytail: no quest topics (FUN_0049f900) or Greiz/Cain extras.
NpcMenuState open_talk_menu(const Scene& s, int npc, int screen_x, int screen_y) {
    NpcMenuState m;
    const auto& n = s.world_npcs[std::size_t(npc)];
    const auto t = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& e) { return e.hc_idx == n.hc_idx; });
    m.npc = npc;
    m.lines.push_back({ string_id(s, 0xd35), 21, 0, 0, true });
    if (t != kNpcTalk.end() && !t->topics.empty()) {
        if (!t->no_intro) m.lines.push_back({ string_id(s, 0xd47), 15, 0, 0, false, NpcMenuState::kIntro });
        m.lines.push_back({ string_id(s, 0xd43), 15, 0, 0, false, NpcMenuState::kGossip });
    }
    m.lines.push_back({ string_id(s, 0xd48), 15 });
    layout_npc_menu(s, m, screen_x, screen_y);
    return m;
}

void layout_npc_menu(const Scene& s, NpcMenuState& m, int screen_x, int screen_y) {
    int tw = 0;
    for (auto& l : m.lines) { l.width = s.font.measure(l.text); tw = std::max(tw, l.width); m.h += l.height; }
    m.w = tw + 20;
    m.h += 15;
    for (auto& l : m.lines) l.x = l.width < m.w ? (m.w - l.width + 1) / 2 + 1 : 0;
    const int cx = screen_x, cy = std::max(screen_y - 150, 20);
    m.x = cx - m.w / 2;
    m.y = cy - m.h / 3;
    if (m.x + m.w > int(kW) - 10) m.x = int(kW) - m.w;
    if (m.y + m.h > int(kH) - 0x3a) m.y = int(kH) - m.h - 0x30;
    if (m.x < 11) m.x = 10;
    if (m.y < 11) m.y = 10;
}

// An NPC's speech topic for the player's class. Introduction
// (FUN_004b41e0): topic 1 when its class is the player's, else topic 0.
// Gossip (FUN_004b1680): a random topic >= 2 whose class is 7 (any) or the
// player's and, if quest-gated, whose quest state matches — up to 10
// tries, else topic 2. game.exe picks it once per game per NPC; so do we.
// Quest states are the save's flags for the difficulty it was played on.
int talk_topic(const NpcTalk& t, bool intro, int cls, std::uint32_t& rng,
               const std::function<bool(int quest)>& quest_done) {
    if (intro) return t.topics.size() > 1 && int(t.topics[1].cls) == cls ? 1 : 0;
    const int n = int(t.topics.size());
    for (int tries = 10; tries > 0 && n > 0; --tries) {
        rng = rng * 0x6ac690c5u + 1u;
        const int i = int(rng % std::uint32_t(n));
        if (i < 2) continue;
        const auto& tp = t.topics[std::size_t(i)];
        if (tp.cls != 7 && int(tp.cls) != cls) continue;
        if (tp.quest_gated && quest_done(int(tp.quest)) != (tp.quest_state != 0)) continue;
        return i;
    }
    return std::min(2, n - 1);
}

// NPC speech (FUN_004a10e0 / FUN_004a05e0 / the draw at 0x49d5a0): the
// string's line 0 is the scroll rate (8 if not a number), the rest is
// pre-wrapped. A half-dark 325x122 box at ((W-325)/2, 12-5); FontFormal11
// lines at x+16, 18 px apart, entering at the bottom (baseline top+112)
// and rising by (ms/4)*rate/1024 px. Done once the offset passes
// (lines-1)*18 + 112. ponytail: whole lines clipped to the box instead of
// FUN_00501df0's partial-line reveal; no voice.
struct Speech {
    int npc = -1;
    std::vector<std::string> lines;
    int rate = 8;
    std::uint32_t start_ms = 0;
    std::uint16_t string = 0;            // string.tbl id
    int voice = 0;                       // Sounds.txt index, 0 = not started, -1 = none
    [[nodiscard]] int offset_px(std::uint32_t ms) const { return int(std::uint64_t(ms - start_ms) / 4 * std::uint64_t(rate) >> 10); }
    [[nodiscard]] bool done(std::uint32_t ms) const {
        return offset_px(ms) > std::max(int(lines.size()) - 1, 1) * 18 + 0x70;
    }
};

Speech start_speech(const Scene& s, int npc, std::uint16_t string, std::uint32_t ms) {
    Speech sp;
    sp.npc = npc;
    sp.start_ms = ms;
    sp.string = string;
    const std::string text = string_id(s, string);
    std::size_t a = 0;
    bool first = true;
    while (a <= text.size()) {
        const auto nl = text.find('\n', a);
        const std::string line = text.substr(a, nl == std::string::npos ? std::string::npos : nl - a);
        if (first) {
            first = false;
            const bool num = !line.empty() && std::ranges::all_of(line, [](char c) { return c >= '0' && c <= '9'; });
            sp.rate = num ? std::atoi(line.c_str()) : 8;
            if (!num) sp.lines.push_back(line);
        } else {
            sp.lines.push_back(line);
        }
        if (nl == std::string::npos) break;
        a = nl + 1;
    }
    return sp;
}

void draw_speech(std::vector<std::uint8_t>& fb, const Scene& s, const Speech& sp, std::uint32_t ms) {
    if (sp.npc < 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int bx = (int(kW) - 0x145) / 2, top = 12;
    for (int y = std::max(0, top - 5); y < top - 5 + 0x7a; ++y)
        for (int x = bx; x < bx + 0x145; ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 2); p[1] = std::uint8_t(p[1] / 2); p[2] = std::uint8_t(p[2] / 2);
        }
    const auto& f = s.font_formal11.line_height() > 0 ? s.font_formal11 : s.font;
    const int cell = f.sheet().frames_per_direction() > 0 ? int(f.sheet().frame(0, 0).height) : 16;
    const int off = sp.offset_px(ms);
    for (std::size_t i = 0; i < sp.lines.size(); ++i) {
        const int base = top + 0x70 - off + int(i) * 18;
        if (base < top - 5 || base - cell > top - 5 + 0x7a) continue;
        f.draw_tinted(fb, kW, kH, pal, bx + 16, base - cell + 1, sp.lines[i], 255, 255, 255,
                      top - 5, top - 5 + 0x7a);
    }
}

// FUN_004b8100: a black box at draw mode 1 (half transparent), each line's
// text with its baseline at the box top + the heights so far + its own;
// the hovered entry gets focus16 (frame = draw count % 7) bottom-anchored
// at (x - 24, baseline + 4) and (x + width + 2, baseline + 4).
void draw_npc_menu(std::vector<std::uint8_t>& fb, const Scene& s, const NpcMenuState& m,
                   int mx, int my, std::uint32_t ms) {
    if (m.npc < 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    for (int y = std::max(0, m.y); y < std::min(int(kH), m.y + m.h); ++y)
        for (int x = std::max(0, m.x); x < std::min(int(kW), m.x + m.w); ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 2); p[1] = std::uint8_t(p[1] / 2); p[2] = std::uint8_t(p[2] / 2);
        }
    const int hover = m.line_at(mx, my);
    const int cell = s.font.sheet().frames_per_direction() > 0 ? int(s.font.sheet().frame(0, 0).height) : 16;
    int base = m.y;
    for (std::size_t i = 0; i < m.lines.size(); ++i) {
        const auto& l = m.lines[i];
        base += l.height;
        const int x = m.x + l.x;
        if (l.header) s.font.draw_tinted(fb, kW, kH, pal, x, base - cell + 1, l.text, 199, 179, 119);
        else          s.font.draw(fb, kW, kH, pal, x, base - cell + 1, l.text);
        if (int(i) == hover && s.focus16.frames_per_direction() > 0) {
            const auto& f = s.focus16.frame(0, (ms / 40) % std::min<std::uint32_t>(7, s.focus16.frames_per_direction()));
            blit_sprite(fb, f, pal, x - 24, base + 4 - int(f.height) + 1);
            blit_sprite(fb, f, pal, x + l.width + 2, base + 4 - int(f.height) + 1);
        }
    }
}

struct CharCreateUI {
    std::array<ClassUI, 7> classes{};
    int selected = -1;           // index of currently-selected class or -1
    // Gear the in-game character wears: a loaded save's appearance bytes,
    // or unset for a fresh character (starting gear).
    std::optional<std::array<std::uint8_t, 16>> appearance;
    std::vector<d2d::d2s::Item> items;   // a loaded save's items
    d2d::d2s::Stats stats;               // ... and attributes
    PanelStats panel;                    // ... and what the char panel computes
    bool expansion = true;               // the save's expansion flag (stash size)
    d2d::d2s::Header header;             // the loaded save's header (quest flags ...)
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

// Set by run_windowed: D2Win buttons (control type 6) play
// sfx\cursor\button.wav when pressed (FUN_00501090).
std::function<void()> g_on_button_press;

bool update_button(Button& b, const Mouse& m, Screen& current_screen,
                   std::atomic<bool>& quit) {
    b.hovered = m.x >= b.x && m.x < b.x + b.w
             && m.y >= b.y && m.y < b.y + b.h;
    if (b.hovered && m.press_this_frame) {
        b.pressed = true;
        if (g_on_button_press) g_on_button_press();
    }
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

// Composite tokens: d2s class id -> CHARS folder (Assassin is "AI", its
// dev codename), D2 mode ids we use, and layer names by COF type.
constexpr const char* kCharCode[7] = { "AM", "SO", "NE", "PA", "BA", "DZ", "AI" };
constexpr int kModeNU = 1, kModeRN = 3, kModeTN = 5, kModeTW = 6;

// ponytail: town walk speed picked by eye so the TW cycle doesn't skate
// (~2 cells = 10 subtiles/s). CharStats.txt WalkVelocity (6) is the real
// input; derive from it once movement units are RE'd.
// Movement speed from a unit's velocity (CharStats Walk/RunVelocity,
// MonStats Velocity): the path velocity is velocity << 8 (scaled by
// velocitypercent, FUN_00462a20), and a unit covers path velocity / 4096
// subtiles per 40 ms tick (arrival time (dist << 16) / ((v >> 8) << 12),
// 0x4c86a3) — velocity / 16 subtiles a tick. Walk 6: 1.875 cells/s.
constexpr float cells_per_sec(float velocity) { return velocity / 16.f * 25.f / 5.f; }

// Direction (0..15, D2's DCC order) for a world-space step (dx, dy) in
// cells. Directions are screen-space: project to screen, take the angle
// clockwise from straight down, and map the 16 sectors through D2's
// ordering — the 8 main directions first (0 SW, 1 NW, 2 NE, 3 SE, 4 S,
// 5 W, 6 N, 7 E), then the half-steps (8 between S and SW, ...).
inline int direction16(float dx, float dy) {
    constexpr int kFromSector[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
    const float sx = (dx - dy) * (kIsoW / 2), sy = (dx + dy) * (kIsoH / 2);
    const float a = std::atan2(-sx, sy);                    // 0 = down, + = clockwise
    const int sector = int(std::lround(a / (2 * 3.14159265f / 16)));
    return kFromSector[std::size_t((sector % 16 + 16) % 16)];
}
constexpr const char* kModeCode[7] = { "DT", "NU", "WL", "RN", "GH", "TN", "TW" };
constexpr const char* kLayerCode[16] = {
    "HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8",
};

// Load one composite: COF <CC><mode><wclass>, then per COF layer the DCC
// <CC><LY><component><mode><layer wclass>. The weapon class comes from
// the hand/shield bytes (compcode::weapon_class, falling back to hth when
// D2 would reject the combination). Empty body layers wear "lit" (a bare
// head under a circlet, say); empty RH/LH/SH draw nothing.
// ponytail: tints (appearance+16) and the dead-hardcore ghost aren't
// applied yet; add the item colormaps when a portrait's colours matter.
Scene::PlayerAnim load_composite(const d2d::mpq::Stack& mpqs,
                                 const std::vector<d2d::compcode::Entry>& comp,
                                 int cls, int mode, const Scene::Appearance& gfx) {
    Scene::PlayerAnim out;
    const char* cc = kCharCode[cls];
    std::string wc(comp.empty() ? std::string_view{}
                                : d2d::compcode::weapon_class(cls, comp, gfx[5], gfx[6], gfx[7]));
    char path[256];
    auto cof_path = [&](std::string_view w) {
        std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\COF\%s%s%.*s.cof)",
                      cc, cc, kModeCode[mode], int(w.size()), w.data());
        return mpqs.try_read(path);
    };
    auto cof = cof_path(wc.empty() ? "hth" : wc);
    if (!cof) cof = cof_path("hth");
    if (!cof) return out;
    try {
        out.cof = d2d::cof::Cof(*cof);
        const std::string_view pv(path);
        out.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.name) ch = char(std::toupper(ch));
        for (const auto& L : out.cof.layer_defs()) {
            if (L.type >= 16) continue;
            const auto b = gfx[L.type];
            std::string code = (b != 0 && b != 0xff && b < comp.size()) ? comp[b].code : "";
            if (code.empty()) {
                if (L.type >= 5 && L.type <= 7) continue;   // empty hand / no shield
                code = "lit";
            }
            std::string lw = L.weapon_class;
            for (auto* t : { &code, &lw }) for (auto& ch : *t) ch = char(std::toupper(ch));
            std::snprintf(path, sizeof(path), R"(data\global\CHARS\%s\%s\%s%s%s%s%s.dcc)",
                          cc, kLayerCode[L.type], cc, kLayerCode[L.type], code.c_str(),
                          kModeCode[mode], lw.c_str());
            if (auto d = mpqs.try_read(path)) out.layers[L.type] = d2d::dcc::Sprite(*d);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] %s: %s\n", path, e.what());
    }
    return out;
}

const Scene::PlayerAnim& Scene::composite(int d2s_class, int mode, const Appearance& gfx) const {
    std::array<std::uint8_t, 18> key{ std::uint8_t(d2s_class), std::uint8_t(mode) };
    std::copy(gfx.begin(), gfx.end(), key.begin() + 2);
    auto it = composites.find(key);
    if (it == composites.end()) {
        it = composites.emplace(key, load_composite(mpqs, comp, d2s_class, mode, gfx)).first;
        if (const auto a = anim_speed.find(it->second.name); a != anim_speed.end()) it->second.speed = a->second;
    }
    return it->second;
}

// Load an NPC/object composite: COF <root>\<code>\COF\<code><mode><BaseW>,
// then per COF layer <root>\<code>\<LY>\<code><LY><comp><mode><wclass>
// with the recipe's component for that layer ("lit" when blank).
Scene::PlayerAnim load_npc_composite(const d2d::mpq::Stack& mpqs, const Scene::Npc& n,
                                     const std::string& mode) {
    Scene::PlayerAnim out;
    char path[256];
    std::snprintf(path, sizeof(path), R"(data\global\%s\%s\COF\%s%s%s.cof)",
                  n.root.c_str(), n.code.c_str(), n.code.c_str(), mode.c_str(), n.base_w.c_str());
    auto cof = mpqs.try_read(path);
    if (!cof) return out;
    try {
        out.cof = d2d::cof::Cof(*cof);
        const std::string_view pv(path);
        out.name = std::string(pv.substr(pv.rfind('\\') + 1, pv.rfind('.') - pv.rfind('\\') - 1));
        for (auto& ch : out.name) ch = char(std::toupper(ch));
        for (const auto& L : out.cof.layer_defs()) {
            if (L.type >= 16) continue;
            std::string comp = n.comp[L.type].empty() ? "lit" : n.comp[L.type];
            std::string lw = L.weapon_class;
            for (auto* t : { &comp, &lw }) for (auto& ch : *t) ch = char(std::toupper(ch));
            std::snprintf(path, sizeof(path), R"(data\global\%s\%s\%s\%s%s%s%s%s.dcc)",
                          n.root.c_str(), n.code.c_str(), kLayerCode[L.type], n.code.c_str(),
                          kLayerCode[L.type], comp.c_str(), mode.c_str(), lw.c_str());
            if (auto d = mpqs.try_read(path)) out.layers[L.type] = d2d::dcc::Sprite(*d);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[d2d] %s: %s\n", path, e.what());
    }
    return out;
}

const Scene::PlayerAnim& Scene::npc_anim(const Npc& n, std::string_view mode) const {
    const std::string m(mode);
    const auto key = n.root + "/" + n.code + "/" + m;
    auto it = npc_anims.find(key);
    if (it == npc_anims.end()) {
        it = npc_anims.emplace(key, load_npc_composite(mpqs, n, m)).first;
        if (const auto a = anim_speed.find(it->second.name); a != anim_speed.end()) it->second.speed = a->second;
    }
    return it->second;
}

const d2d::dc6::Sprite* Scene::item_sprite(const d2d::d2s::Item& item) const {
    const auto info = item_info.find(item.code);
    if (info == item_info.end()) return nullptr;
    auto pick = [](const std::vector<std::string>& v, int i) {
        return i >= 0 && std::size_t(i) < v.size() ? v[std::size_t(i)] : std::string{};
    };
    std::string file = item.quality == 7 ? pick(unique_inv, item.unique_id)
                     : item.quality == 5 ? pick(set_inv, item.set_id) : std::string{};
    if (file.empty() && item.picture >= 0 && item.picture < 6)
        if (const auto g = type_invgfx.find(info->second.type); g != type_invgfx.end())
            file = g->second[std::size_t(item.picture)];
    if (file.empty()) file = info->second.invfile;
    if (file.empty()) return nullptr;
    auto [it, fresh] = item_sprites.try_emplace(file);
    if (fresh)
        if (auto b = mpqs.try_read(R"(data\global\items\)" + file + ".dc6"))
            it->second = d2d::dc6::Sprite(*b);
    return it->second ? &*it->second : nullptr;
}

// Act 1 town NPCs from the DS1's type-1 objects: id -> MonPreset.txt
// (Act 1 rows) Place -> MonStats2 row (by Id) -> MonStats row at the same
// index for the monster token (both are indexed by hcIdx; our MonStats is
// the CD's, whose names differ, 1.14d's being a compressed patch entry).
// Positions are in subtiles; a unit stands at its subtile's centre.
// ponytail: act 1 only, NU idle only; "place_*" spawn markers skipped.
void load_npcs(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* n) {
        auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    const auto preset = txt("MonPreset"), ms = txt("MonStats"), ms2 = txt("MonStats2");
    if (preset.size() == 0 || ms2.size() == 0) return;
    std::vector<std::size_t> act1;
    for (std::size_t r = 0; r < preset.size(); ++r)
        if (preset.get(r, "Act") == "1") act1.push_back(r);
    std::unordered_map<std::string, std::size_t> ms2_row;
    for (std::size_t r = 0; r < ms2.size(); ++r) ms2_row.emplace(std::string(ms2.get(r, "Id")), r);
    static constexpr const char* kVariant[16] = {
        "HDv", "TRv", "LGv", "RAv", "LAv", "RHv", "LHv", "SHv",
        "S1v", "S2v", "S3v", "S4v", "S5v", "S6v", "S7v", "S8v",
    };
    for (const auto& o : scene.world_ds1.objects()) {
        if (o.type != 1 || o.id < 0 || std::size_t(o.id) >= act1.size()) continue;
        const std::string place(preset.get(act1[std::size_t(o.id)], "Place"));
        const auto it = ms2_row.find(place);
        if (it == ms2_row.end()) continue;           // place_* markers etc.
        Scene::Npc n;
        n.root   = "monsters";
        n.mode   = "NU";
        n.code   = std::string(ms.get(it->second, "Code"));
        n.hc_idx = std::atoi(std::string(ms.get(it->second, "hcIdx")).c_str());
        n.base_w = std::string(ms2.get(it->second, "BaseW"));
        n.size_x = std::atoi(std::string(ms2.get(it->second, "SizeX")).c_str());
        n.size_y = std::atoi(std::string(ms2.get(it->second, "SizeY")).c_str());
        if (const auto v = ms.get(it->second, "Velocity"); !v.empty()) n.velocity = float(std::atoi(std::string(v).c_str()));
        // Hover name: MonStats' string key, only for units MonStats2 marks
        // selectable (isSel) — not the chicken or the camp's guard rogues,
        // whose name key "Dummy" reads "an evil force".
        if (ms2.get(it->second, "isSel") == "1") {
            std::string key(ms.get(it->second, "NameStr"));        // 1.14d; "namco" on the CD
            if (key.empty()) key = std::string(ms.get(it->second, "namco"));
            auto v = lookup_string(scene, key);
            n.name = v ? u16_to_latin1(*v) : key;
        }
        if (n.code.empty()) continue;
        if (n.base_w.empty()) n.base_w = "hth";
        for (std::size_t l = 0; l < 16; ++l) {
            if (ms2.get(it->second, kLayerCode[l]) != "1") continue;
            const auto v = ms2.get(it->second, kVariant[l]);
            const auto first = v.substr(0, v.find(','));
            n.comp[l] = first.empty() ? "lit" : std::string(first);
        }
        n.x = (float(o.x) + 0.5f) / 5;
        n.y = (float(o.y) + 0.5f) / 5;
        for (const auto& pt : o.path)
            n.path.emplace_back((float(pt.x) + 0.5f) / 5, (float(pt.y) + 0.5f) / 5);
        scene.world_npcs.push_back(std::move(n));
    }

    // Type-2 objects: id -> objects.txt Id through game.exe's own preset
    // table (obj_preset.hpp), then that row's Token and layer flags. Start
    // mode: ON for things with a light in ON (torches, fires, the camp
    // waypoint), else NU. ponytail: D2 sets it per object in its InitFn.
    const auto objects = txt("objects");
    std::unordered_map<std::string, std::size_t> obj_row;
    for (std::size_t r = 0; r < objects.size(); ++r) obj_row.emplace(std::string(objects.get(r, "Id")), r);
    for (const auto& o : scene.world_ds1.objects()) {
        if (o.type != 2 || o.id < 0 || o.id >= 150) continue;
        const int oid = kObjPreset[0][std::size_t(o.id)];    // act 1
        const auto it = obj_row.find(std::to_string(oid));
        if (oid == 0 || it == obj_row.end()) continue;
        const auto r = it->second;
        Scene::Npc n;
        n.root   = "objects";
        n.code   = std::string(objects.get(r, "Token"));
        n.operate_fn = std::atoi(std::string(objects.get(r, "OperateFn")).c_str());
        n.base_w = "hth";
        const bool on = objects.get(r, "Mode2") == "1" && !objects.get(r, "Lit2").empty()
                     && objects.get(r, "Lit2") != "0";
        n.mode   = on ? "ON" : "NU";
        // Hover name when selectable in its start mode (Selectable0 = NU,
        // 2 = ON): objects.txt Name through the string tables.
        if (objects.get(r, on ? "Selectable2" : "Selectable0") == "1") {
            const std::string key(objects.get(r, "Name"));
            auto v = lookup_string(scene, key);
            n.name = v ? u16_to_latin1(*v) : key;
        }
        // Blocks walking in its start mode (HasCollision0 = NU, 2 = ON).
        if (objects.get(r, on ? "HasCollision2" : "HasCollision0") == "1") {
            n.size_x = std::atoi(std::string(objects.get(r, "SizeX")).c_str());
            n.size_y = std::atoi(std::string(objects.get(r, "SizeY")).c_str());
        }
        for (std::size_t l = 0; l < 16; ++l)
            if (objects.get(r, kLayerCode[l]) == "1") n.comp[l] = "lit";
        if (n.code.empty()) continue;
        n.x = (float(o.x) + 0.5f) / 5;
        n.y = (float(o.y) + 0.5f) / 5;
        scene.world_npcs.push_back(std::move(n));
    }

    // Footprints into the walk grid, centred on each unit's subtile.
    // ponytail: static — fine while NPCs only idle; moving units need a
    // separate occupancy layer.
    const int ww = scene.world_ds1.width() * 5, wh = scene.world_ds1.height() * 5;
    for (const auto& n : scene.world_npcs) {
        if (!n.path.empty()) continue;           // walkers don't hold a spot
        const int cx = int(n.x * 5), cy = int(n.y * 5);
        for (int y = cy - n.size_y / 2; y < cy - n.size_y / 2 + n.size_y; ++y)
            for (int x = cx - n.size_x / 2; x < cx - n.size_x / 2 + n.size_x; ++x)
                if (x >= 0 && y >= 0 && x < ww && y < wh)
                    scene.world_walk[std::size_t(y) * std::size_t(ww) + std::size_t(x)] |= 0x01;
    }
}

// Excel tables + the derived composite data: the component table and each
// class's starting-gear appearance (CharStats.txt item1..: "rarm" item in
// the right hand, a "larm" shield on the shield layer; body parts "lit").
void load_composite_data(Scene& scene, const d2d::mpq::Stack& mpqs) {
    auto txt = [&](const char* n) {
        auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt");
        return b ? d2d::txt::Table(*b) : d2d::txt::Table{};
    };
    const auto types = txt("ItemTypes"), weapons = txt("weapons"), armor = txt("armor"),
               misc = txt("misc"), charstats = txt("CharStats");
    if (types.size() == 0 || weapons.size() == 0) return;
    scene.comp = d2d::compcode::build(types, weapons, armor, misc);

    // Char panel: next-level experience (row "<level>", same for every
    // class) and the expansion's resistance penalty per difficulty.
    if (const auto xp = txt("experience"); xp.size() > 0)
        for (std::size_t r = 0; r < xp.size(); ++r)
            if (const auto l = xp.get(r, "Level"); !l.empty() && l[0] >= '0' && l[0] <= '9') {
                const auto lv = std::size_t(std::stoi(std::string(l)));
                if (lv >= scene.exp_next.size()) scene.exp_next.resize(lv + 1, -1);
                scene.exp_next[lv] = std::stoll(std::string(xp.get(r, "Amazon")));
            }
    if (const auto dl = txt("DifficultyLevels"); dl.size() >= 3)
        for (std::size_t r = 0; r < 3; ++r)
            scene.resist_penalty[r] = std::stoll(std::string(dl.get(r, "ResistPenalty")));

    // Items. ItemStatCost.txt only exists in the 1.14d patch data.
    if (const auto isc = txt("ItemStatCost"); isc.size() > 0)
        scene.item_tables = d2d::d2s::ItemTables::from(isc, armor, weapons, misc);
    for (const auto* t : { &weapons, &armor, &misc })
        for (std::size_t r = 0; r < t->size(); ++r)
            scene.item_info[std::string(t->get(r, "code"))] = {
                std::string(t->get(r, "invfile")),
                std::max(1, std::atoi(std::string(t->get(r, "invwidth")).c_str())),
                std::max(1, std::atoi(std::string(t->get(r, "invheight")).c_str())),
                std::string(t->get(r, "namestr")), std::string(t->get(r, "type")),
                t == &armor ? 1 : t == &weapons ? 2 : 0,
                t == &armor && !t->get(r, "belt").empty() ? std::atoi(std::string(t->get(r, "belt")).c_str()) : -1 };
    for (std::size_t r = 0; r < types.size(); ++r) {
        const std::string code(types.get(r, "Code"));
        scene.type_equiv[code] = { std::string(types.get(r, "Equiv1")), std::string(types.get(r, "Equiv2")) };
        auto& g = scene.type_invgfx[code];
        for (int i = 0; i < 6; ++i) g[std::size_t(i)] = std::string(types.get(r, "InvGfx" + std::to_string(i + 1)));
    }
    auto keys = [&](const char* n, const char* col, bool all) {
        std::vector<std::string> v;
        if (auto b = mpqs.try_read(std::string(R"(data\global\excel\)") + n + ".txt")) {
            const d2d::txt::Table t(*b, all);
            for (std::size_t r = 0; r < t.size(); ++r) v.emplace_back(t.get(r, col));
        }
        return v;
    };
    if (auto b = mpqs.try_read(R"(data\global\excel\ItemStatCost.txt)")) {
        const d2d::txt::Table t(*b);
        auto num = [&](std::size_t r, const char* c) { return std::atoi(std::string(t.get(r, c)).c_str()); };
        for (std::size_t r = 0; r < t.size(); ++r) {
            const auto id = std::size_t(num(r, "ID"));
            if (id >= scene.stat_desc.size()) scene.stat_desc.resize(id + 1);
            scene.stat_desc[id] = { num(r, "descpriority"), num(r, "descfunc"), num(r, "descval"),
                                    num(r, "op"), num(r, "op param"), num(r, "dgrp"), num(r, "dgrpfunc"),
                                    num(r, "dgrpval"),
                                    std::string(t.get(r, "descstrpos")), std::string(t.get(r, "descstrneg")),
                                    std::string(t.get(r, "descstr2")), std::string(t.get(r, "dgrpstrpos")),
                                    std::string(t.get(r, "dgrpstrneg")), std::string(t.get(r, "dgrpstr2")) };
        }
    }
    {
        // Socket bonuses: gems.txt mods -> Properties.txt funcs -> stats.
        // Property funcs used by gems: 1/3 value, 15/16 min/max, 17 param,
        // 5/6/7 min/max/% damage. ponytail: other funcs dropped.
        std::unordered_map<std::string, int> stat_id;
        if (auto b = mpqs.try_read(R"(data\global\excel\ItemStatCost.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r)
                stat_id[std::string(t.get(r, "Stat"))] = std::atoi(std::string(t.get(r, "ID")).c_str());
        }
        std::unordered_map<std::string, std::vector<std::pair<int, int>>> prop;   // code -> (func, stat)
        const auto pt = txt("Properties");
        for (std::size_t r = 0; r < pt.size(); ++r)
            for (int i = 1; i <= 7; ++i) {
                const int f = std::atoi(std::string(pt.get(r, "func" + std::to_string(i))).c_str());
                if (!f) continue;
                const auto st = stat_id.find(std::string(pt.get(r, "stat" + std::to_string(i))));
                prop[std::string(pt.get(r, "code"))].emplace_back(f, st == stat_id.end() ? -1 : st->second);
            }
        const auto gt = txt("gems");
        static constexpr const char* kSlot[3] = { "weaponMod", "helmMod", "shieldMod" };
        for (std::size_t r = 0; r < gt.size(); ++r)
            for (int k = 0; k < 3; ++k)
                for (int m = 1; m <= 3; ++m) {
                    const std::string pre = std::string(kSlot[k]) + std::to_string(m);
                    const auto it = prop.find(std::string(gt.get(r, pre + "Code")));
                    if (it == prop.end()) continue;
                    auto num = [&](const char* c) { return std::atoi(std::string(gt.get(r, pre + c)).c_str()); };
                    const int par = num("Param"), mn = num("Min"), mx = num("Max");
                    auto& out = scene.gem_props[std::string(gt.get(r, "code"))][std::size_t(k)];
                    for (auto [f, st] : it->second) {
                        switch (f) {
                            case 1: case 3: if (st >= 0) out.push_back({ st, par, mn }); break;
                            case 15: if (st >= 0) out.push_back({ st, 0, mn }); break;
                            case 16: if (st >= 0) out.push_back({ st, 0, mx }); break;
                            case 17: if (st >= 0) out.push_back({ st, 0, par }); break;
                            case 5: out.push_back({ 21, 0, mn }); break;
                            case 6: out.push_back({ 22, 0, mx }); break;
                            case 7: out.push_back({ 17, 0, mn }); out.push_back({ 18, 0, mn }); break;
                            default: break;
                        }
                    }
                }
    }
    {
        std::unordered_map<std::string, std::string> desc_name;     // SkillDesc key -> "str name"
        if (auto b = mpqs.try_read(R"(data\global\excel\SkillDesc.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r)
                desc_name[std::string(t.get(r, "skilldesc"))] = std::string(t.get(r, "str name"));
        }
        static constexpr std::string_view kCls[] = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
        if (auto b = mpqs.try_read(R"(data\global\excel\skills.txt)")) {
            const d2d::txt::Table t(*b);
            for (std::size_t r = 0; r < t.size(); ++r) {
                const auto id = std::size_t(std::atoi(std::string(t.get(r, "Id")).c_str()));
                if (id >= scene.skill_name.size()) { scene.skill_name.resize(id + 1); scene.skill_class.resize(id + 1, -1); }
                scene.skill_name[id] = desc_name[std::string(t.get(r, "skilldesc"))];
                const auto cc = t.get(r, "charclass");
                for (int c = 0; c < 7; ++c) if (cc == kCls[c]) scene.skill_class[id] = c;
            }
        }
        const auto cs = txt("CharStats");
        for (std::size_t r = 0; r < std::min<std::size_t>(cs.size(), 7); ++r) {
            if (const auto w = std::atoi(std::string(cs.get(r, "WalkVelocity")).c_str()); w > 0) scene.walk_velocity[r] = w;
            if (const auto v = std::atoi(std::string(cs.get(r, "RunVelocity")).c_str()); v > 0) scene.run_velocity[r] = v;
        }
        for (std::size_t r = 0; r < std::min<std::size_t>(cs.size(), 7); ++r)
            scene.class_strs[r] = { std::string(cs.get(r, "StrAllSkills")),
                                    { std::string(cs.get(r, "StrSkillTab1")), std::string(cs.get(r, "StrSkillTab2")),
                                      std::string(cs.get(r, "StrSkillTab3")) },
                                    std::string(cs.get(r, "StrClassOnly")) };
    }
    // animdata.d2: blocks of u32 count + count * 160-byte records
    // {char name[8], u32 frames/dir, u32 speed, u8 events[144]}.
    if (auto b = mpqs.try_read(R"(data\global\animdata.d2)"))
        for (std::size_t p = 0; p + 4 <= b->size();) {
            std::uint32_t n; std::memcpy(&n, b->data() + p, 4); p += 4;
            for (std::uint32_t i = 0; i < n && p + 160 <= b->size(); ++i, p += 160) {
                std::string name(reinterpret_cast<const char*>(b->data() + p), 8);
                name.resize(std::strlen(name.c_str()));
                for (auto& ch : name) ch = char(std::toupper(ch));
                std::uint32_t spd; std::memcpy(&spd, b->data() + p + 12, 4);
                scene.anim_speed.emplace(std::move(name), spd);
            }
        }
    if (const auto bt = txt("belts"); bt.size() >= 14)
        for (std::size_t b = 0; b < 7; ++b) {
            const std::size_t r = 7 + b;                 // the 800x600 half
            auto& B = scene.belts[b];
            B.boxes = std::clamp(std::atoi(std::string(bt.get(r, "numboxes")).c_str()), 0, 16);
            for (int i = 0; i < B.boxes; ++i)
                for (int k = 0; k < 4; ++k) {
                    static constexpr const char* kSide[4] = { "left", "right", "top", "bottom" };
                    B.box[std::size_t(i)][std::size_t(k)] = std::atoi(std::string(
                        bt.get(r, "box" + std::to_string(i + 1) + kSide[k])).c_str());
                }
        }
    if (auto b = mpqs.try_read(R"(data\global\ui\PANEL\ctrlpnl_popbelt.dc6)")) scene.popbelt = d2d::dc6::Sprite(*b);
    if (auto b = mpqs.try_read(R"(data\global\ui\CURSOR\focus16.dc6)")) scene.focus16 = d2d::dc6::Sprite(*b);
    // Rogue Encampment (Levels.txt Id 1) -> SoundEnv -> SoundEnviron Song /
    // Day Ambience (Sounds.txt indices).
    {
        const auto lv = txt("Levels"), se = txt("SoundEnviron");
        for (std::size_t r = 0; r < lv.size(); ++r) {
            if (lv.get(r, "Id") != "1") continue;
            const auto env = lv.get(r, "SoundEnv");
            for (std::size_t e = 0; e < se.size(); ++e)
                if (se.get(e, "Index") == env) {
                    scene.town_song = std::atoi(std::string(se.get(e, "Song")).c_str());
                    scene.town_ambience = std::atoi(std::string(se.get(e, "Day Ambience")).c_str());
                }
        }
    }
    if (const auto st = txt("Sounds"); st.size() > 0)
        for (std::size_t r = 0; r < st.size(); ++r) {
            const int i = std::atoi(std::string(st.get(r, "Index")).c_str());
            if (i < 0 || i > 20000) continue;
            if (std::size_t(i) >= scene.sounds.size()) scene.sounds.resize(std::size_t(i) + 1);
            scene.sounds[std::size_t(i)] = { std::string(st.get(r, "FileName")),
                                             std::atoi(std::string(st.get(r, "Volume")).c_str()),
                                             st.get(r, "Loop") == "1", st.get(r, "Music Vol") == "1" };
        }
    if (auto t = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.tbl)"))
        if (auto d = mpqs.try_read(R"(data\local\FONT\LATIN\fontformal11.dc6)"))
            scene.font_formal11 = d2d::font::Font(*t, d2d::dc6::Sprite(*d));
    auto& nm = scene.item_names;
    nm.unique   = keys("UniqueItems", "index", false);
    scene.unique_inv = keys("UniqueItems", "invfile", false);
    scene.set_inv    = keys("SetItems", "invfile", false);
    nm.set      = keys("SetItems", "index", false);
    nm.prefix   = keys("MagicPrefix", "Name", true);
    nm.suffix   = keys("MagicSuffix", "Name", true);
    nm.rare_pre = keys("RarePrefix", "name", true);
    nm.rare_suf = keys("RareSuffix", "name", true);
    {
        // Runes.txt "RunewordN" rows sorted by N (80 and 96 don't exist);
        // the save's ID is that rank + 27.
        std::vector<std::pair<int, std::string>> rw;
        for (const auto& k : keys("Runes", "Name", false))
            if (k.starts_with("Runeword")) rw.emplace_back(std::atoi(k.c_str() + 8), k);
        std::ranges::sort(rw);
        for (auto& [n, k] : rw) nm.runeword.push_back(std::move(k));
    }
    for (auto [name, into] : { std::pair{ "font8", &scene.font_small }, { "font6", &scene.font_tiny } })
        if (auto t = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".tbl"))
            if (auto d = mpqs.try_read(std::string(R"(data\local\FONT\LATIN\)") + name + ".dc6"))
                *into = d2d::font::Font(*t, d2d::dc6::Sprite(*d));
    for (auto [path, into] : { std::pair{ R"(data\global\ui\PANEL\800ctrlpnl7.dc6)", &scene.ctrl_panel },
                               { R"(data\global\ui\PANEL\hlthmana.dc6)", &scene.globes },
                               { R"(data\global\ui\PANEL\overlap.dc6)", &scene.globe_glass } })
        if (auto b = mpqs.try_read(path)) *into = d2d::dc6::Sprite(*b);
    if (auto p = mpqs.try_read(R"(data\global\ui\PANEL\invchar6.dc6)"))
        scene.inv_panel = d2d::dc6::Sprite(*p);
    // inventory.txt "<Class>2" rows are the 800x600 layouts.
    const auto inv = txt("inventory");
    for (std::size_t r = 0; r < inv.size(); ++r) {
        const auto cls = inv.get(r, "class");
        const int e = cls == "Big Bank Page2" ? 1 : cls == "Bank Page2" ? 0
                    : cls == "Transmogrify Box2" ? 2 : -1;
        if (e < 0) continue;
        auto num = [&](const char* col) { return std::atoi(std::string(inv.get(r, col)).c_str()); };
        auto& L = e == 2 ? scene.cube_layout : scene.stash_layout[std::size_t(e)];
        L.grid_x = num("gridLeft"); L.grid_y = num("gridTop");
        L.box_w = num("gridBoxWidth"); L.box_h = num("gridBoxHeight");
    }
    for (auto [path, e] : { std::pair{ R"(data\global\ui\PANEL\bank.dc6)", 0 },
                            { R"(data\global\ui\PANEL\TradeStash.dc6)", 1 } })
        if (auto b = mpqs.try_read(path)) scene.stash_panel[std::size_t(e)] = d2d::dc6::Sprite(*b);
    if (auto b = mpqs.try_read(R"(data\global\ui\PANEL\supertransmogrifier.dc6)"))
        scene.cube_panel = d2d::dc6::Sprite(*b);
    static constexpr const char* kInvClass[7] = {
        "Amazon2", "Sorceress2", "Necromancer2", "Paladin2", "Barbarian2", "Druid2", "Assassin2" };
    static constexpr const char* kSlotCol[11] = {
        nullptr, "head", "neck", "torso", "rArm", "lArm", "rHand", "lHand", "belt", "feet", "gloves" };
    for (std::size_t c = 0; c < 7; ++c)
        for (std::size_t r = 0; r < inv.size(); ++r) {
            if (inv.get(r, "class") != kInvClass[c]) continue;
            auto num = [&](std::string col) { return std::atoi(std::string(inv.get(r, col)).c_str()); };
            auto& L = scene.inv_layout[c];
            L.panel_x = num("invLeft"); L.panel_y = num("invTop");
            L.grid_x = num("gridLeft"); L.grid_y = num("gridTop");
            L.box_w = num("gridBoxWidth"); L.box_h = num("gridBoxHeight");
            for (std::size_t sl = 1; sl < 11; ++sl) {
                const std::string k = kSlotCol[sl];
                L.slots[sl] = { num(k + "Left"), num(k + "Top"), num(k + "Width"), num(k + "Height") };
            }
        }
    auto index_of = [&](std::string_view code) {
        for (std::size_t i = 1; i < scene.comp.size(); ++i)
            if (scene.comp[i].code == code) return std::uint8_t(i);
        return std::uint8_t(0xff);
    };
    for (std::size_t c = 0; c < 7 && c < charstats.size(); ++c) {
        auto& g = scene.starting_gear[c];
        g.fill(0xff);
        for (int l : { 1, 2, 3, 4, 8, 9 }) g[std::size_t(l)] = 1;   // TR LG RA LA S1 S2 = lit
        for (int i = 1; i <= 10; ++i) {
            const auto item = charstats.get(c, "item" + std::to_string(i));
            const auto loc  = charstats.get(c, "item" + std::to_string(i) + "loc");
            const auto idx  = index_of(item);
            if (idx == 0xff) continue;
            if (loc == "rarm") g[5] = idx;
            else if (loc == "larm") g[scene.comp[idx].armor ? 7 : 6] = idx;
        }
    }
}

// Draws a composite frame, feet at the anchor (defined with the other
// DCC blitters below).
void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y);

// Headers (and items) of every valid .d2s in `dir`, most recently played first. Bad files are
// logged and skipped — saves are user-supplied.
void load_saves(Scene& scene, const fs::path& dir) {
    struct Entry { d2d::d2s::Header header; std::vector<d2d::d2s::Item> items; d2d::d2s::Stats stats; };
    std::vector<Entry> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".d2s") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
        const auto bytes = std::as_bytes(std::span(raw));
        try {
            Entry en{ d2d::d2s::parse_header(bytes), {}, {} };
            if (scene.item_tables) {
                try {
                    en.stats = d2d::d2s::parse_stats(bytes, *scene.item_tables);
                    en.items = d2d::d2s::parse_items(bytes, *scene.item_tables);
                }
                catch (const std::exception& ex) {
                    std::fprintf(stderr, "[d2d] %s items: %s\n", e.path().string().c_str(), ex.what());
                }
            }
            out.push_back(std::move(en));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "[d2d] %s: %s\n", e.path().string().c_str(), ex.what());
        }
    }
    // Newest first — LoD inserts each character by last-played time,
    // descending (FUN_00438ad0), and preselects slot 0.
    // Ties (e.g. synthetic saves with no timestamp) fall back to name so
    // the order doesn't depend on directory iteration.
    std::ranges::sort(out, [](const Entry& a, const Entry& b) {
        return std::tie(b.header.last_played, a.header.name)
             < std::tie(a.header.last_played, b.header.name);
    });
    scene.saves.clear(); scene.save_items.clear(); scene.save_stats.clear();
    for (auto& en : out) {
        scene.saves.push_back(std::move(en.header));
        scene.save_items.push_back(std::move(en.items));
        scene.save_stats.push_back(en.stats);
    }
}

std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer) {
    const auto d2data = data_dir / "d2data.mpq";
    if (!fs::exists(d2data)) return std::nullopt;
    try {
        d2d::mpq::Stack mpqs;
        // 1.14d's patch layer ranks above everything (game.exe opens
        // patch_d2.mpq first). A real install has patch_d2.mpq; a CD-copied
        // data dir can use the LODPatch_114d.exe installer instead (d2d.cfg
        // `patch = ...`, or dropped next to the MPQs).
        bool patched = false;
        if (fs::exists(data_dir / "patch_d2.mpq")) {
            mpqs.push(data_dir / "patch_d2.mpq");
            patched = true;
        } else {
            for (const auto& p : { patch_installer, data_dir / "LODPatch_114d.exe" }) {
                if (p.empty() || !fs::exists(p)) continue;
                try { mpqs.push_installer(p); patched = true; break; }
                catch (const std::exception& e) { std::fprintf(stderr, "[d2d] %s\n", e.what()); }
            }
        }
        if (!patched)
            std::fprintf(stderr, "[d2d] no 1.14d patch data (patch_d2.mpq or LODPatch_114d.exe): "
                                 "using CD-era tables and strings\n");
        const auto d2exp = data_dir / "d2exp.mpq";
        if (fs::exists(d2exp)) mpqs.push(d2exp);
        mpqs.push(d2data);
        // Character animations live in d2char.mpq — Stack lookup is
        // priority-ordered so later pushes rank lower; DCC-not-found is
        // silent in load_scene and per-class loaders skip on miss.
        const auto d2char = data_dir / "d2char.mpq";
        if (fs::exists(d2char)) mpqs.push(d2char);
        // Sounds: expansion speech, speech, effects, music (Sounds.txt paths).
        // (Music is read off the main thread from its own handles: Audio.)
        for (const char* n : { "d2xtalk.mpq", "d2speech.mpq", "d2sfx.mpq" })
            if (fs::exists(data_dir / n)) mpqs.push(data_dir / n);

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
            .medium_button     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumButtonBlank.dc6)")),
            .medium_sel_button = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\MediumSelButtonBlank.dc6)")),
            .textbox           = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\textbox.dc6)")),
            .clickbox          = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\clickbox.dc6)")),
            .charselect_bg     = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\characterselectscreenEXP.dc6)")),
            .charselect_box    = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\charselectbox.dc6)")),
            .charselect_scroll = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\FrontEnd\joingamescrollbars.dc6)")),
            .tall_button       = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CharSelect\TallButtonBlank.dc6)")),
            .cursor            = d2d::dc6::Sprite(mpqs.read(R"(data\global\ui\CURSOR\ohand.dc6)")),
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
        load_composite_data(scene, mpqs);
        load_npcs(scene, mpqs);
        scene.patched = patched;
        scene.data_dir = data_dir;
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
    // Collision grid from the same tiles the renderer draws: floors
    // (type 0) and walls/objects, but not shadows (13) or roofs (15).
    // ponytail: subtile flag k taken as (x, y) = (k % 5, k / 5); objects
    // and NPCs (DS1 object list) don't block yet.
    const auto& m = scene.world_ds1;
    const int ww = m.width() * 5;
    scene.world_walk.assign(std::size_t(ww) * std::size_t(m.height()) * 5, 0);
    auto stamp = [&](int gx, int gy, int style, int seq, int type) {
        const auto it = scene.world_tile_lookup.find(tile_key(style, seq, type));
        if (it == scene.world_tile_lookup.end()) return;
        for (int k = 0; k < 25; ++k)
            scene.world_walk[std::size_t(gy * 5 + k / 5) * std::size_t(ww) + std::size_t(gx * 5 + k % 5)]
                |= it->second->subtile_flags[std::size_t(k)];
    };
    for (int gy = 0; gy < m.height(); ++gy)
        for (int gx = 0; gx < m.width(); ++gx) {
            const std::size_t off = std::size_t(gy) * std::size_t(m.width()) + std::size_t(gx);
            for (const auto& fl : m.floors())
                if (!fl.cells[off].hidden) stamp(gx, gy, fl.cells[off].style, fl.cells[off].sequence, 0);
            for (const auto& wl : m.walls()) {
                const auto& c = wl.cells[off];
                if (c.hidden || c.wall_type == 0 || c.wall_type == 13 || c.wall_type == 15) continue;
                stamp(gx, gy, c.style, c.sequence, c.wall_type);
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
// character slots; one gold charselectbox (272x93 assembled) marks the pick.
// Four buttons: CREATE / DELETE (TallButtonBlank, record bottom y=528) and
// OK / EXIT (MediumButtonBlank, bottom y=572). Char-create's OK/EXIT sit at
// the same spot but use MediumSelButtonBlank (handle 0x779744), which is
// authored for the fechar palette — on this Sky screen it speckles.
//
// Slots list <user dir>/save/*.d2s; OK enters InGame once one is picked.
// CREATE hops to CharCreate. DELETE is a no-op for now.
struct CharSelectUI {
    Button create_btn{};
    Button convert_btn{};
    Button delete_btn{};
    Button cancel_btn{};
    Button ok_btn{};
    std::string create_label, create_label2;
    std::string convert_label, convert_label2;
    std::string delete_label, delete_label2;
    std::string cancel_label;
    std::string ok_label;
    int selected = -1;           // index into Scene::saves, or -1
    int scroll = 0;              // index of the save in slot 0; always even
    std::uint32_t last_click_ms = 0;   // double-click = OK (FUN_0043a9d0)
};

// Largest valid CharSelectUI::scroll for `n` saves: last row at the bottom.
int charselect_max_scroll(int n) {
    return std::max(0, (n + 1) / 2 * 2 - kSlots);
}

// D2 font colours used on char-select (FUN_004fc9b0 colour arg).
constexpr std::uint8_t kTextRed[3]   = { 255, 77, 77 };    // 1
constexpr std::uint8_t kTextGreen[3] = { 0, 255, 0 };      // 2
constexpr std::uint8_t kTextGold[3]  = { 199, 179, 119 };  // 4

// Name prefix earned by beating difficulties — FUN_005068a0 picks a tier
// from the progression byte, FUN_00505640 the (hard-coded English) word.
// Tier: classic <4/<8/<12/else, LoD <5/<10/<15/else -> 0..3; hardcore
// shifts non-zero tiers by 3. Female: Amazon, Sorceress, Assassin.
std::string char_title(const d2d::d2s::Header& h) {
    const int p = h.progression;
    const bool lod = h.expansion();
    int tier = lod ? (p < 5 ? 0 : p < 10 ? 1 : p < 15 ? 2 : 3)
                   : (p < 4 ? 0 : p < 8 ? 1 : p < 12 ? 2 : 3);
    if (tier == 0) return {};
    if (h.hardcore()) tier += 3;
    const bool female = h.cls == 0 || h.cls == 1 || h.cls == 6;
    static constexpr const char* kClassic[2][7] = {
        { "", "Sir ",  "Lord ", "Baron ",    "Count ",    "Duke ",    "King "  },
        { "", "Dame ", "Lady ", "Baroness ", "Countess ", "Duchess ", "Queen " },
    };
    static constexpr const char* kLod[2][7] = {
        { "", "Slayer ", "Champion ", "Patriarch ", "Destroyer ", "Conqueror ", "Guardian " },
        { "", "Slayer ", "Champion ", "Matriarch ", "Destroyer ", "Conqueror ", "Guardian " },
    };
    return (lod ? kLod : kClassic)[female][tier];
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
                       std::uint32_t elapsed_ms) {
    const auto& pal = s.pal;   // char-select shares the Sky palette
    blit_dc6_grid(fb, s.charselect_bg, pal, 0, 0, s.bg_tiles_across);

    for (int i = 0; i < kSlots; ++i) {
        const int x = kSlotX[i % 2], y = kSlotY[i / 2];
        // LoD draws ONE frame: the gold charselectbox (record 0x96) moved
        // onto the selected slot while it's on screen (FUN_004390a0). The
        // grey box is classic-only (record 0x97, FUN_0043b080).
        // Two-frame composite: main 256-wide half + 16-wide sliver.
        const int si = ui.scroll + i;   // save index shown in this slot
        const auto& box = s.charselect_box;
        if (si == ui.selected && box.frames_per_direction() >= 2) {
            blit_sprite(fb, box.frame(0, 0), pal, x,       y);
            blit_sprite(fb, box.frame(0, 1), pal, x + 256, y);
        }
        if (si >= int(s.saves.size())) continue;
        // Text, per FUN_004380f0: lines go into the slot's text box (record
        // 0x84+i: 200x92, left margin 76, top margin 3), right of the
        // portrait. [title] name in red (hardcore) or gold, "Level N Class"
        // in white, then "EXPANSION CHARACTER" in green for LoD chars.
        const auto& h = s.saves[std::size_t(si)];
        // Portrait: the character's own composite (FUN_00438ad0 builds it
        // from the save's appearance bytes; FUN_004380f0 parks it at slot
        // x + 30, slot bottom - 13). Town-neutral, or NU for a living
        // LoD hardcore character. Direction 0 (FUN_005051a0(anim, 0)).
        // ponytail: dead hardcore should use the ghost class (8/9).
        {
            const bool nu = h.hardcore() && !h.died() && (h.expansion() || h.cls < 5);
            draw_composite(fb, s.composite(h.cls, nu ? kModeNU : kModeTN, h.appearance),
                           pal, 0, elapsed_ms, x + 30, y + kSlotH - 1 - 13);
        }
        const int ci = kSaveClassToUi[h.cls];
        std::string cls = kClassKey[ci];
        if (auto v = lookup_string(s, kClassKey[ci])) cls = u16_to_latin1(*v);
        std::string level = "Level";
        if (auto v = lookup_string(s, std::uint16_t(0xfd9))) level = u16_to_latin1(*v);
        const std::string line1 = char_title(h) + h.name;
        const std::string line2 = level + " " + std::to_string(h.level) + " " + cls;
        const int tx = x + 76, lh = s.font.line_height();
        int ty = y + 3;
        const auto& name_rgb = h.hardcore() ? kTextRed : kTextGold;
        s.font.draw_tinted(fb, kW, kH, pal, tx, ty, line1.c_str(),
                           name_rgb[0], name_rgb[1], name_rgb[2]);
        s.font.draw(fb, kW, kH, pal, tx, ty += lh, line2.c_str());
        if (h.expansion()) {
            std::string exp = "EXPANSION CHARACTER";
            if (auto v = lookup_string(s, std::uint16_t(22731))) exp = u16_to_latin1(*v);
            s.font.draw_tinted(fb, kW, kH, pal, tx, ty += lh, exp.c_str(),
                               kTextGreen[0], kTextGreen[1], kTextGreen[2]);
        }
    }

    if (const int max = charselect_max_scroll(int(s.saves.size()));
        max > 0 && s.charselect_scroll.frames_per_direction() >= 5) {
        const auto& sb = s.charselect_scroll;
        blit_sprite(fb, sb.frame(0, 1), pal, kScrollX, kScrollDownTop);
        if (sb.frames_per_direction() >= 6)
            for (int n = 20, b = kScrollBottom - 1; n < kScrollH; n += 10)
                blit_sprite(fb, sb.frame(0, 5), pal, kScrollX, rec_top(b -= 10, kScrollArrow));
        blit_sprite(fb, sb.frame(0, 0), pal, kScrollX, kScrollUpTop);
        // Thumb: pos/max in rows (the widget counts rows, we store saves).
        const int thumb_bottom = (kScrollH - 30) * (ui.scroll / 2) / (max / 2)
                               - kScrollH + 19 + kScrollBottom;
        blit_sprite(fb, sb.frame(0, 4), pal, kScrollX, rec_top(thumb_bottom, kScrollArrow));
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
    // normal/pressed); the medium OK/EXIT chrome uses blit_button_chrome.
    auto draw_tall = [&](const Button& b, bool disabled = false) {
        if (!b.chrome) return;
        const auto& fr = b.chrome->frame(0, b.hovered && b.pressed ? 1 : 0);
        blit_sprite(fb, fr, pal, b.x, b.y);
        const int lh = s.font.line_height();
        const int lines = b.label2 ? 2 : 1;
        int ly = b.y + (b.h - lines * lh) / 2;
        for (const char* t : { b.label, b.label2 }) {
            if (!t || !*t) continue;
            const int lx = b.x + (b.w - s.font.measure(t)) / 2;
            if (disabled) s.font.draw_tinted(fb, kW, kH, pal, lx, ly, t, 96, 96, 96);
            else          s.font.draw(fb, kW, kH, pal, lx, ly, t);
            ly += lh;
        }
    };
    draw_tall(ui.create_btn);
    // Convert-to-expansion only applies to a classic character (ponytail:
    // drawn, never actionable).
    const bool classic_pick = ui.selected >= 0 && ui.selected < int(s.saves.size())
                           && !s.saves[std::size_t(ui.selected)].expansion();
    draw_tall(ui.convert_btn, !classic_pick);
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
            // OK greys out until a save is picked (do_switch is gated
            // on that each frame, before this draw).
            if (b == &ui.ok_btn && !b->do_switch)
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

// Render the loaded DS1 onto the framebuffer around the camera point
// (cam_x, cam_y), in cells. Draws in D2's back-to-front Z order:
//   1. All floor tiles (type=0) in row order — the ground plane
//   2. All shadow tiles (type=13) in row order — soft dark decals
//   3. Walls / trees / roofs per row, top-back rows first, so
//      lower/nearer rows can occlude higher/farther ones
//
// Missing tile lookups are silent — a cell whose (style, seq, type)
// tuple isn't in any loaded DT1 leaves whatever's below it showing
// through, which is the same behaviour D2 itself has for stripped
// tilesets.
// Something drawn in the wall pass by depth: the player, an NPC.
// Where each world NPC is right now (index-aligned with Scene::world_npcs):
// patrolling NPCs walk their DS1 path, pausing at each point.
struct NpcState {
    float x = 0, y = 0;
    int dir = 0;
    bool walking = false;
    std::size_t next = 0;             // path point being walked to
    std::uint32_t wait_until = 0;     // ms; idle until then
    std::uint32_t mode_ms = 0;        // when the current mode (walk/idle) started
};

struct Unit {
    float x = 0, y = 0;                  // world position, cells
    const Scene::PlayerAnim* anim = nullptr;
    int dir = 0;
    const std::string* name = nullptr;   // hover label, if selectable
    // When the unit's current mode started: animations run from frame 0
    // of each mode, like game.exe's mode start (FUN_005533d0 zeroes the
    // 8.8 frame counter at unit+0x30).
    std::uint32_t mode_ms = 0;
    int npc = -1;                        // Scene::world_npcs index, -1 = the player
};

// Screen rectangle a composite's current frame covers with its feet at
// (ax, ay): the union of every drawn layer's frame box. {x0, y0, x1, y1}.
std::array<int, 4> composite_bounds(const Scene::PlayerAnim& p, int dir_want,
                                    std::uint32_t elapsed_ms, int ax, int ay) {
    std::array<int, 4> r{ INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
    const auto dirs = p.cof.directions(), fpd = p.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return r;
    const auto dir = std::uint8_t(std::min(dir_want, dirs - 1));
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = 40u * 256u / std::max<std::uint32_t>(p.speed ? p.speed : p.cof.speed(), 1);
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (const auto& spr : p.layers) {
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        const auto& f = spr.frame(dir, frame);
        r = { std::min(r[0], ax + f.box_left), std::min(r[1], ay + f.box_top),
              std::max(r[2], ax + f.box_right), std::max(r[3], ay + f.box_bottom) };
    }
    return r;
}

void render_world(std::vector<std::uint8_t>& fb,
                  const Scene& s,
                  float cam_x, float cam_y,
                  std::uint32_t elapsed_ms = 0,
                  std::span<const Unit> units = {},
                  int mouse_x = -1, int mouse_y = -1,
                  std::pair<const Unit*, std::array<int, 4>>* hovered = nullptr) {
    const auto& m = s.world_ds1;
    if (m.width() == 0 || m.height() == 0) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int cx0 = int(kW) / 2;
    const int cy0 = int(kH) / 2;
    const int mw  = m.width();
    const int base_x = int(std::floor(cam_x)), base_y = int(std::floor(cam_y));
    // Screen position of cell (gx, gy)'s top diamond corner. The camera
    // point (cam_x, cam_y) — continuous, in cells — lands at (kW/2,
    // kH/2 + kIsoH/2), i.e. a cell centre when the camera sits on one.
    auto iso = [&](int gx, int gy) {
        const float dx = float(gx) - cam_x, dy = float(gy) - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          cy0 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };
    // Screen position of a continuous world point (a unit's feet).
    auto iso_point = [&](float x, float y) {
        const float dx = x - cam_x, dy = y - cam_y;
        return std::pair{ cx0 + int(std::lround((dx - dy) * (kIsoW / 2))),
                          cy0 + kIsoH / 2 + int(std::lround((dx + dy) * (kIsoH / 2))) };
    };

    // Iso footprint for the 800x600 window: each screen cell is 160x80.
    // A ±12 grid-cell window around the camera covers > 2× screen area,
    // leaving room for tall walls (up to 128+px) to reach in from cells
    // that are off-screen at their base.
    constexpr int kR = 12;

    // Blit a single tile at cell (gx, gy)'s iso position, honouring the
    // 80-tall-diamond-at-bottom convention shared by floor/wall pixel
    // buffers.
    auto blit_cell = [&](int gx, int gy, const d2d::dt1::Tile& t) {
        const auto [iso_x, iso_y] = iso(gx, gy);
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
            const int gx = base_x + dx;
            const int gy = base_y + dy;
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
    //
    // Walls go back to front by iso depth (gx + gy, one diagonal at a
    // time), and each unit is drawn once its own cell's diagonal is done:
    // a tent north of the player stays behind them, one south of them
    // covers them. Units on the same diagonal go in screen-y order.
    // ponytail: cell-granular; D2 sorts units and walls by subtile and
    // wall orientation, which matters once units stand inside a cell's
    // wall line.
    std::vector<const Unit*> order;
    for (const auto& u : units) if (u.anim) order.push_back(&u);
    auto diag_of = [&](const Unit* u) {
        return (int(std::floor(u->x)) - base_x) + (int(std::floor(u->y)) - base_y);
    };
    std::ranges::sort(order, {}, [](const Unit* u) { return u->x + u->y; });
    std::size_t next_unit = 0;
    const auto& upal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    auto draw_units_through = [&](int diag) {
        for (; next_unit < order.size() && diag_of(order[next_unit]) <= diag; ++next_unit) {
            const Unit& u = *order[next_unit];
            const auto [ax, ay] = iso_point(u.x, u.y);
            if (ax < -200 || ax > int(kW) + 200 || ay < -100 || ay > int(kH) + 300) continue;
            draw_composite(fb, *u.anim, upal, u.dir, elapsed_ms - u.mode_ms, ax, ay);
            // Last drawn unit under the cursor = the frontmost one.
            if (hovered && u.name && !u.name->empty()) {
                const auto b = composite_bounds(*u.anim, u.dir, elapsed_ms - u.mode_ms, ax, ay);
                if (mouse_x >= b[0] && mouse_x < b[2] && mouse_y >= b[1] && mouse_y < b[3])
                    *hovered = { &u, b };
            }
        }
    };
    for (int diag = -2 * kR; diag <= 2 * kR; ++diag) {
        draw_units_through(diag - 1);
        for (int dx = std::max(-kR, diag - kR); dx <= std::min(kR, diag + kR); ++dx) {
            const int dy = diag - dx;
            const int gx = base_x + dx;
            const int gy = base_y + dy;
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
                        auto [iso_x, iso_y] = iso(gx, gy);
                        iso_y -= t->roof_height;
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
    draw_units_through(1 << 20);
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

// Draw a composite's current frame with its feet at (anchor_x, anchor_y):
// every loaded layer, in the COF's per-(direction, frame) draw order.
// Frame time from the COF speed byte: D2 advances speed/256 frames per
// 25 Hz tick, so one frame lasts 40 ms * 256 / speed (BA 80 -> 128 ms).
// ponytail: COF speed as the rate; AnimData.d2 is authoritative — read it
// when an animation visibly runs at the wrong pace. No shadow, no
// transparent-layer draw effects yet (no TN layer sets `transparent`).
void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y) {
    const auto dirs = p.cof.directions();
    const auto fpd  = p.cof.frames_per_direction();
    if (dirs == 0 || fpd == 0) return;
    const auto dir = std::uint8_t(std::min(dir_want, dirs - 1));
    // 25 ticks/s; each tick advances speed/256 frames.
    const auto ms_per_frame = 40u * 256u / std::max<std::uint32_t>(p.speed ? p.speed : p.cof.speed(), 1);
    const auto frame = std::uint8_t((elapsed_ms / ms_per_frame) % fpd);
    for (const auto type : p.cof.priority(dir, frame)) {
        if (type >= p.layers.size()) continue;
        const auto& spr = p.layers[type];
        if (dir >= spr.directions() || frame >= spr.frames_per_direction()) continue;
        blit_dcc_frame(fb, spr.frame(dir, frame), pal, anchor_x, anchor_y);
    }
}

// An item's hover text, top line first, in D2's quality colours. Name IDs
// resolve as verified against 19 real saves (test data): unique/set ID =
// UniqueItems/SetItems row without "Expansion" separators; magic
// prefix/suffix ID = MagicPrefix/MagicSuffix raw data row (row 0 is the
// blank "none"); rare names = RarePrefix row (id - 156) + RareSuffix row
// (id - 1); runeword ID = rank of its RunewordN + 27.
// ponytail: no requirements, durability, set bonus lines or weapon damage.
struct TextLine { std::string text; std::array<std::uint8_t, 3> rgb; };

// D2's printf subset in its strings: %d, %+d, %s, %%. Args in order.
std::string d2_format(std::string_view f, std::initializer_list<std::variant<std::int64_t, std::string>> args) {
    std::string out;
    auto a = args.begin();
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%' || i + 1 == f.size()) { out += f[i]; continue; }
        if (f[i + 1] == '%') { out += '%'; ++i; continue; }
        const bool plus = f[i + 1] == '+';
        const std::size_t k = i + (plus ? 2 : 1);
        if (k >= f.size() || (f[k] != 'd' && f[k] != 's' && f[k] != 'i')) { out += f[i]; continue; }
        if (a != args.end()) {
            if (const auto* n = std::get_if<std::int64_t>(&*a)) out += (plus && *n >= 0 ? "+" : "") + std::to_string(*n);
            else out += std::get<std::string>(*a);
            ++a;
        }
        i = k;
    }
    return out;
}

// An item's property list as the tooltip's text lines, by ItemStatCost
// descfunc (the column semantics the game's item-description code uses),
// highest descpriority first. dgrp groups (all resistances, all
// attributes) collapse into one line when every member is present with
// the same value; min/max damage pairs become "Adds X-Y ..." and 17/18
// "+X% Enhanced Damage", as the game hard-codes them.
// ponytail: no descfunc 17/18 (time-of-day), 22/23 (monster types);
// charges/skill lines use the skill's string key.
std::vector<std::string> prop_lines(const Scene& s, std::vector<d2d::d2s::ItemProp> props, int clvl) {
    auto str = [&](std::string_view key) {
        if (key.empty()) return std::string{};
        const auto v = lookup_string(s, key);
        std::string t = v ? u16_to_latin1(*v) : std::string(key);
        while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
        return t;
    };
    auto desc = [&](int stat) -> const Scene::StatDesc* {
        return stat >= 0 && std::size_t(stat) < s.stat_desc.size() ? &s.stat_desc[std::size_t(stat)] : nullptr;
    };
    auto skill = [&](int id) {
        return id >= 0 && std::size_t(id) < s.skill_name.size() ? str(s.skill_name[std::size_t(id)]) : std::string{};
    };
    // One stat list, like the unit's: repeats of a (stat, param) add up.
    {
        std::vector<d2d::d2s::ItemProp> merged;
        for (const auto& p : props) {
            auto m = std::ranges::find_if(merged, [&](const auto& q) { return q.stat == p.stat && q.param == p.param; });
            if (m != merged.end() && p.stat != 204) m->value += p.value;      // 204: charges don't add
            else merged.push_back(p);
        }
        props = std::move(merged);
    }
    struct Out { int prio; std::string text; };
    std::vector<Out> out;
    auto value_of = [&](int stat) -> std::optional<std::int64_t> {
        for (const auto& p : props) if (p.stat == stat) return p.value;
        return std::nullopt;
    };
    std::unordered_set<int> done;
    // Hard-coded pairs.
    auto range = [&](int lo, int hi, const char* range_key, const char* single_key, int prio, int len_stat = -1) {
        const auto a = value_of(lo), b = value_of(hi);
        if (!a || !b) return;
        std::int64_t x = *a, y = *b;
        std::optional<std::int64_t> secs;
        if (len_stat >= 0) {
            const auto len = value_of(len_stat).value_or(0);
            x = x * len / 256; y = y * len / 256; secs = len / 25;
            done.insert(len_stat);
        }
        std::string t = x == y && single_key ? (secs ? d2_format(str(single_key), { x, *secs })
                                                     : d2_format(str(single_key), { x }))
                      : secs ? d2_format(str(range_key), { x, y, *secs }) : d2_format(str(range_key), { x, y });
        out.push_back({ prio, std::move(t) });
        done.insert(lo); done.insert(hi);
    };
    range(21, 22, "strModMinDamageRange", nullptr, 127);
    // Two-handed/thrown copies of min/max damage only show without the base.
    if (value_of(21)) { done.insert(23); done.insert(159); }
    if (value_of(22)) { done.insert(24); done.insert(160); }
    range(48, 49, "strModFireDamageRange", "strModFireDamage", 102);
    range(50, 51, "strModLightningDamageRange", "strModLightningDamage", 99);
    range(52, 53, "strModMagicDamageRange", "strModMagicDamage", 104);
    range(54, 55, "strModColdDamageRange", "strModColdDamage", 96);
    if (done.contains(54)) done.insert(56);
    range(57, 58, "strModPoisonDamageRange", "strModPoisonDamage", 92, 59);
    if (const auto a = value_of(17), b = value_of(18); a && b && *a == *b) {
        out.push_back({ 130, "+" + std::to_string(*a) + "% " + str("strModEnhancedDamage") });
        done.insert(17); done.insert(18);
    }
    // dgrp groups.
    std::unordered_map<int, std::vector<int>> groups;
    for (std::size_t i = 0; i < s.stat_desc.size(); ++i)
        if (s.stat_desc[i].dgrp) groups[s.stat_desc[i].dgrp].push_back(int(i));
    for (const auto& [g, members] : groups) {
        std::optional<std::int64_t> v;
        bool same = true;
        for (int m : members) {
            const auto x = value_of(m);
            if (!x || (v && *v != *x)) { same = false; break; }
            v = x;
        }
        if (!same || !v) continue;
        const auto& d = *desc(members.front());
        const std::string s1 = str(*v < 0 ? d.dgrp_neg : d.dgrp_pos);
        std::string t;
        switch (d.dgrp_func) {
            case 1: case 6: case 12: t = d.dgrp_val == 2 ? s1 + " " + d2_format("%+d", { *v }) : d2_format("%+d", { *v }) + " " + s1; break;
            case 3: case 9: t = d.dgrp_val == 2 ? s1 + " " + std::to_string(*v) : std::to_string(*v) + " " + s1; break;
            case 4: case 8: t = d.dgrp_val == 2 ? s1 + " " + d2_format("%+d%%", { *v }) : d2_format("%+d%%", { *v }) + " " + s1; break;
            case 19: t = d2_format(s1, { *v }); break;
            default: t = d2_format(s1, { *v }); break;
        }
        out.push_back({ d.prio, std::move(t) });
        for (int m : members) done.insert(m);
    }
    for (const auto& p : props) {
        if (done.contains(p.stat)) continue;
        const auto* d = desc(p.stat);
        if (!d || d->func == 0) continue;
        std::int64_t v = p.value;
        // Per-level stats (op 2..5 with op param): value * clvl >> param.
        if (d->op >= 2 && d->op <= 5 && d->op_param > 0) v = v * clvl >> d->op_param;
        const std::string s1 = str(v < 0 ? d->neg : d->pos), s2 = str(d->str2);
        auto place = [&](const std::string& num) {
            if (d->val == 0) return s1;
            return d->val == 2 ? s1 + " " + num : num + " " + s1;
        };
        auto with2 = [&](std::string t) { return s2.empty() ? t : t + " " + s2; };
        std::string t;
        switch (d->func) {
            case 1: case 12: t = place(d2_format("%+d", { v })); break;
            case 2: t = place(std::to_string(v) + "%"); break;
            case 3: t = place(std::to_string(v)); break;
            case 4: t = place(d2_format("%+d%%", { v })); break;
            case 5: t = place(std::to_string(v * 100 / 128) + "%"); break;
            case 6: t = with2(place(d2_format("%+d", { v }))); break;
            case 7: t = with2(place(std::to_string(v) + "%")); break;
            case 8: t = with2(place(d2_format("%+d%%", { v }))); break;
            case 9: t = with2(place(std::to_string(v))); break;
            case 10: t = with2(place(std::to_string(v * 100 / 128) + "%")); break;
            case 11: t = d2_format(s1, { std::int64_t(1), v ? 100 / v : 0 }); break;
            case 13: t = d2_format("%+d", { v }) + " " + (p.param >= 0 && p.param < 7
                         ? str(s.class_strs[std::size_t(p.param)].all_skills) : std::string{}); break;
            case 14: {
                const int cls = p.param >> 3, tab = p.param & 7;
                if (cls < 0 || cls >= 7 || tab > 2) break;
                const auto& cs = s.class_strs[std::size_t(cls)];
                t = d2_format(str(cs.tab[tab]), { v }) + " " + str(cs.only);
                break;
            }
            case 15: t = d2_format(s1, { v, std::int64_t(p.param & 63), skill(p.param >> 6) }); break;
            case 16: t = d2_format(s1, { v, skill(p.param) }); break;
            case 19: t = d2_format(s1, { v }); break;
            case 20: t = place(std::to_string(-v) + "%"); break;
            case 21: t = place(std::to_string(-v)); break;
            case 24:                                       // descstr is just "(%d/%d Charges)"
                t = "Level " + std::to_string(p.param & 63) + " " + skill(p.param >> 6) + " "
                  + d2_format(s1, { v & 255, v >> 8 });
                break;
            case 27: {
                const int c = std::size_t(p.param) < s.skill_class.size() ? s.skill_class[std::size_t(p.param)] : -1;
                t = d2_format("%+d", { v }) + " to " + skill(p.param)
                  + (c >= 0 ? " " + str(s.class_strs[std::size_t(c)].only) : "");
                break;
            }
            case 28: t = d2_format("%+d", { v }) + " to " + skill(p.param); break;
            default: break;
        }
        if (!t.empty()) out.push_back({ d->prio, std::move(t) });
    }
    std::ranges::stable_sort(out, [](const Out& a, const Out& b) { return a.prio > b.prio; });
    std::vector<std::string> lines;
    for (auto& o : out) lines.push_back(std::move(o.text));
    return lines;
}
constexpr std::array<std::uint8_t, 3> kTxtWhite{ 255, 255, 255 }, kTxtBlue{ 105, 105, 255 },
    kTxtGreen{ 0, 255, 0 }, kTxtGold{ 199, 179, 119 }, kTxtYellow{ 255, 255, 100 },
    kTxtOrange{ 255, 168, 0 }, kTxtGrey{ 105, 105, 105 };

std::vector<TextLine> item_lines(const Scene& s, const d2d::d2s::Item& it, int clvl) {
    auto str = [&](std::string_view key) {
        if (key.empty()) return std::string{};
        const auto v = lookup_string(s, key);
        return v ? u16_to_latin1(*v) : std::string(key);
    };
    auto at = [](const std::vector<std::string>& v, int i) -> std::string_view {
        return i >= 0 && std::size_t(i) < v.size() ? std::string_view(v[std::size_t(i)]) : std::string_view{};
    };
    const auto& nm = s.item_names;
    const auto info = s.item_info.find(it.code);
    const std::string base = str(info != s.item_info.end() && !info->second.namestr.empty()
                                     ? std::string_view(info->second.namestr) : std::string_view(it.code));
    std::vector<TextLine> out;
    auto two = [&](std::string name, std::array<std::uint8_t, 3> c) {
        if (name.empty()) { out.push_back({ base, c }); return; }
        out.push_back({ std::move(name), c });
        out.push_back({ base, c });
    };
    auto join = [](std::string a, const std::string& b) {
        if (a.empty()) return b;
        if (b.empty()) return a;
        return a + " " + b;
    };
    if (it.runeword) {
        const int rank = it.runeword_id == 2718 ? 48 - 27 : it.runeword_id - 27;   // 2718: Delirium's odd ID
        out.push_back({ str(at(nm.runeword, rank)), kTxtGold });
        out.push_back({ base, kTxtGrey });
    } else switch (it.quality) {
        case 1: {
            static constexpr const char* kLow[] = { "Crude", "Cracked", "Damaged", "Low Quality" };
            out.push_back({ join(str(it.qsub >= 0 && it.qsub < 4 ? kLow[it.qsub] : ""), base), kTxtWhite });
            break;
        }
        case 3: out.push_back({ join(str("Hiquality"), base), kTxtWhite }); break;
        case 4: out.push_back({ join(join(str(at(nm.prefix, it.prefix)), base), str(at(nm.suffix, it.suffix))),
                                kTxtBlue }); break;
        case 5: two(str(at(nm.set, it.set_id)), kTxtGreen); break;
        case 7: two(str(at(nm.unique, it.unique_id)), kTxtGold); break;
        case 6: case 8:
            two(join(str(at(nm.rare_pre, it.rare1 - 156)), str(at(nm.rare_suf, it.rare2 - 1))),
                it.quality == 6 ? kTxtYellow : kTxtOrange);
            break;
        default: out.push_back({ base, it.ethereal || it.socketed ? kTxtGrey : kTxtWhite }); break;
    }
    if (it.personalized && !it.owner.empty()) out.front().text = it.owner + "'s " + out.front().text;
    if (it.defense >= 0) {
        // Shown with the item's own +% and flat defence applied (16, 31).
        std::int64_t ed = 0, flat = 0;
        for (const auto& p : it.props) {
            if (p.stat == 16) ed += p.value;
            if (p.stat == 31) flat += p.value;
            if (p.stat == 214) flat += p.value * clvl / 8;         // armor per level
        }
        const auto def = std::int64_t(it.defense) * (100 + ed) / 100 + flat;
        out.push_back({ str("ItemStats1h") + " " + std::to_string(def), def != it.defense ? kTxtBlue : kTxtWhite });
    }
    if (it.quantity >= 0) out.push_back({ "Quantity: " + std::to_string(it.quantity), kTxtWhite });
    auto props = it.props;
    for (const auto& j : it.socketed_items) {
        const auto sp = socket_props(s, it, j);
        props.insert(props.end(), sp.begin(), sp.end());
    }
    for (auto& l : prop_lines(s, std::move(props), clvl)) out.push_back({ std::move(l), kTxtBlue });
    if (it.ethereal) out.push_back({ "Ethereal", kTxtBlue });
    if (it.socketed) out.push_back({ "Socketed (" + std::to_string(it.sockets) + ")", kTxtBlue });
    return out;
}

// Hover text box: lines centred over [x0, x1], bottom on `bottom` (below
// `top` instead when it would leave the screen), on a darkened backdrop.
void draw_hover_text(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<TextLine>& lines,
                     int x0, int x1, int top, int bottom) {
    if (lines.empty()) return;
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int lh = 16;                                   // font16 cell height
    int w = 0;
    for (const auto& l : lines) w = std::max(w, s.font.measure(l.text));
    const int h = lh * int(lines.size());
    int bx = std::clamp((x0 + x1) / 2 - w / 2 - 2, 0, std::max(0, int(kW) - w - 4));
    int by = bottom - h - 2;
    if (by < 0) by = std::min(top, int(kH) - h - 4);
    for (int y = std::max(0, by); y < std::min(int(kH), by + h + 4); ++y)
        for (int x = bx; x < std::min(int(kW), bx + w + 4); ++x) {
            auto* p = &fb[(std::size_t(y) * kW + std::size_t(x)) * 4];
            p[0] = std::uint8_t(p[0] / 4); p[1] = std::uint8_t(p[1] / 4); p[2] = std::uint8_t(p[2] / 4);
        }
    int y = by + 2;
    for (const auto& l : lines) {
        const int lw = s.font.measure(l.text);
        s.font.draw_tinted(fb, kW, kH, pal, bx + 2 + (w - lw) / 2, y, l.text, l.rgb[0], l.rgb[1], l.rgb[2]);
        y += lh;
    }
}

// The inventory panel (inventory.txt "<Class>2" layout, invchar6.dc6):
// grid items (panel 1) centred in their w x h cell block, equipped items
// centred in their body slot's box. Palette: the act's, like the world.
// The item under (mx, my) gets its hover text.
// ponytail: no colour tints (item transform colormaps), cube.
void draw_inventory(std::vector<std::uint8_t>& fb, const Scene& s, const Scene::InvLayout& L,
                    const std::vector<d2d::d2s::Item>& items, int mx = -1, int my = -1, int clvl = 1) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    // invchar6.dc6 holds two 2x2 panels (256+64 wide, 256+176 tall);
    // frames 4..7 are the inventory, 0..3 the character-stats page.
    if (s.inv_panel.frames_per_direction() >= 8) {
        const auto& f0 = s.inv_panel.frame(0, 4);
        blit_sprite(fb, f0, pal, L.panel_x, L.panel_y);
        blit_sprite(fb, s.inv_panel.frame(0, 5), pal, L.panel_x + int(f0.width), L.panel_y);
        blit_sprite(fb, s.inv_panel.frame(0, 6), pal, L.panel_x, L.panel_y + int(f0.height));
        blit_sprite(fb, s.inv_panel.frame(0, 7), pal, L.panel_x + int(f0.width), L.panel_y + int(f0.height));
    }
    auto draw_in = [&](const d2d::d2s::Item& it, int x, int y, int w, int h) {
        const auto* spr = s.item_sprite(it);
        if (!spr || spr->frames_per_direction() == 0) return;
        const auto& f = spr->frame(0, 0);
        blit_sprite(fb, f, pal, x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
    };
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hover_box{};
    for (const auto& it : items) {
        std::array<int, 4> r{};
        if (it.location == 0 && it.panel == 1) {
            const auto info = s.item_info.find(it.code);
            const int iw = info != s.item_info.end() ? info->second.w : 1;
            const int ih = info != s.item_info.end() ? info->second.h : 1;
            r = { L.grid_x + it.column * L.box_w, L.grid_y + it.row * L.box_h, iw * L.box_w, ih * L.box_h };
        } else if (it.location == 1 && it.slot >= 1 && it.slot <= 10) {
            r = L.slots[std::size_t(it.slot)];
        }
        if (r[2] <= 0) continue;
        draw_in(it, r[0], r[1], r[2], r[3]);
        if (mx >= r[0] && mx < r[0] + r[2] && my >= r[1] && my < r[1] + r[3]) { hover = &it; hover_box = r; }
    }
    if (hover)
        draw_hover_text(fb, s, item_lines(s, *hover, clvl), hover_box[0], hover_box[0] + hover_box[2],
                        hover_box[1] + hover_box[3], hover_box[1]);
}

// Character panel (left of the inventory: 800x600 puts the left panels
// at 80..400). Art: invchar6.dc6 frames 0..3. Label and value boxes are
// game.exe's own tables — labels {x0, y, x1, string id} at 0x724818
// (18-byte records), values {x0, y, x1, stat id} at 0x724928 — in panel
// coordinates; text is centred in [x0, x1].
constexpr int kCharPanelX = 80, kCharPanelY = 60;
struct PanelText { int x0, y, x1, id; };
constexpr PanelText kCharLabels[] = {
    {  11,  44,  52, 0xfd9 }, {  65,  44, 180, 0xfda }, { 193,  44, 308, 0xfdb },
    {  10,  97,  73, 0xfdc }, {  10, 160,  73, 0xfde }, { 174, 207, 268, 0xfe0 },
    {  10, 245,  73, 0xfe2 }, { 174, 245, 228, 0xfe3 }, { 174, 269, 228, 0xfe4 },
    {  10, 307,  73, 0xfe5 }, { 174, 307, 228, 0xfe6 }, { 190, 346, 268, 0xfe7 },
    { 190, 370, 268, 0xfe8 }, { 190, 395, 268, 0xfe9 }, { 190, 419, 268, 0xfea },
};
constexpr PanelText kCharValues[] = {
    {  13,  59,  53, 12 }, {  67,  59, 180, 13 }, { 195,  59, 308, 30 },
    {  77,  99, 112,  0 }, {  77, 161, 112,  2 }, { 273, 209, 307, 31 },
    {  77, 247, 112,  3 }, { 232, 246, 267, 11 }, { 273, 246, 308, 10 },
    { 232, 270, 267,  7 }, { 273, 270, 308,  6 }, {  77, 308, 112,  1 },
    { 232, 308, 267,  9 }, { 273, 308, 308,  8 }, { 273, 348, 307, 39 },
    { 273, 372, 307, 43 }, { 273, 396, 307, 41 }, { 273, 420, 307, 45 },
};

void draw_char_panel(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st,
                     const PanelStats& ps,
                     std::string_view name, int class_idx) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int px = kCharPanelX, py = kCharPanelY;
    if (s.inv_panel.frames_per_direction() >= 8) {
        const auto& f0 = s.inv_panel.frame(0, 0);
        blit_sprite(fb, f0, pal, px, py);
        blit_sprite(fb, s.inv_panel.frame(0, 1), pal, px + int(f0.width), py);
        blit_sprite(fb, s.inv_panel.frame(0, 2), pal, px, py + int(f0.height));
        blit_sprite(fb, s.inv_panel.frame(0, 3), pal, px + int(f0.width), py + int(f0.height));
    }
    // As FUN_004a7d00 draws it: text centred in [x0, x1] as
    // x0 + (x1 - x0 + 1 - w) / 2 (left-aligned when it doesn't fit), y the
    // baseline. Labels in font6, a "Fire\nResistance" pair at y-4 / y+4;
    // values in font16, dropping to font8 when > 999 or too wide (life/
    // mana/stamina); name font16/font8/font6 by length, class font16.
    // ponytail: all white; the game colours boosted/lowered values blue/red
    // and maxed resistances gold (local_8 in FUN_004a7d00).
    auto pick = [&](const d2d::font::Font& f) -> const d2d::font::Font& {
        return f.line_height() > 0 ? f : s.font;
    };
    const auto& f16 = s.font;
    const auto& f8 = pick(s.font_small);
    const auto& f6 = pick(s.font_tiny);
    auto text = [&](const d2d::font::Font& f, int x0, int x1, int y, const std::string& t) {
        const int w = f.measure(t);
        const int x = w < x1 - x0 + 1 ? x0 + (x1 - x0 + 1 - w) / 2 : x0;
        // Glyph cells blit bottom-anchored at y, like any DC6 (font6 cells
        // are 11 tall with the baseline on row 8).
        f.draw(fb, kW, kH, pal, px + x, py + y - int(f.sheet().frame(0, 0).height) + 1, t);
    };
    for (const auto& t : kCharLabels) {
        const auto v = lookup_string(s, std::uint16_t(t.id));
        if (!v) continue;
        const auto txt = u16_to_latin1(*v);
        if (const auto nl = txt.find('\n'); nl != txt.npos) {
            text(f6, t.x0, t.x1, t.y - 4, txt.substr(0, nl));
            text(f6, t.x0, t.x1, t.y + 4, txt.substr(nl + 1));
        } else {
            text(f6, t.x0, t.x1, t.y, txt);
        }
    }
    for (const auto& t : kCharValues) {
        const bool fixed = t.id >= 6 && t.id <= 11;       // life/mana/stamina, 8.8
        std::int64_t v = fixed ? st.fixed(t.id) : st.get(t.id);
        switch (t.id) {
            case 30: v = ps.next; break;
            case 31: v = ps.defense; break;
            case 39: v = ps.res[0]; break;
            case 43: v = ps.res[1]; break;
            case 41: v = ps.res[2]; break;
            case 45: v = ps.res[3]; break;
            default: break;
        }
        if (t.id == 30 && v < 0) continue;                // max level: blank
        const auto txt = std::to_string(v);
        const bool small = (fixed || t.id == 31) && (v > 999 || f16.measure(txt) >= t.x1 - t.x0);
        text(small ? f8 : f16, t.x0, t.x1, t.y, txt);
    }
    std::string cls = class_idx >= 0 && class_idx < 7 ? kClassKey[class_idx] : "";
    if (auto v = lookup_string(s, cls)) cls = u16_to_latin1(*v);
    const auto& fname = name.size() + 1 <= 11 ? f16 : name.size() + 1 < 14 ? f8 : f6;
    text(fname, 13, 13 + 0xa1 - 0xd - 1, 25, std::string(name));
    text(f16, 0xc1, 0x137 - 1, 25, cls);
}

// The bottom HUD, as game.exe's 800x600 path draws it (FUN_004983d0 for
// the bar, FUN_00496f80 / FUN_00497110 for the globes). All cels are
// bottom-anchored on the screen's bottom edge:
//   bar: frame 0 (life housing) at x 0, frames 1..4 at 400-235, -107,
//   +21, +149, frame 5 (mana housing) at 800-117; the 48px gaps are the
//   skill buttons.
//   globes: fill = cur * 80 / max rows of hlthmana frame 0 (life; 2 when
//   poisoned) / 1 (mana), bottom at H-13, x 29 / W-111; then the glass
//   (overlap frame 0 at x 28, bottom H-5; frame 1 at W-110, bottom H-9).
// ponytail: saved life/mana are base values (no item bonuses); no
// poison tint, stamina bar, skill icons or run/walk yet.
void draw_hud(std::vector<std::uint8_t>& fb, const Scene& s, const d2d::d2s::Stats& st) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const int W = int(kW), H = int(kH);
    auto at_bottom = [&](const d2d::dc6::Sprite& spr, int frame, int x, int bottom) {
        if (frame >= int(spr.frames_per_direction())) return;
        const auto& f = spr.frame(0, std::uint32_t(frame));
        blit_sprite(fb, f, pal, x, bottom - int(f.height));
    };
    if (s.ctrl_panel.frames_per_direction() >= 6) {
        const int xs[6] = { 0, W / 2 - 0xeb, W / 2 - 0x6b, W / 2 + 0x15, W / 2 + 0x95, W - 0x75 };
        for (int i = 0; i < 6; ++i) at_bottom(s.ctrl_panel, i, xs[i], H);
    }
    // Globe fill: only the bottom `rows` rows of the 80x80 cel.
    auto fill = [&](int frame, int x, std::int64_t cur, std::int64_t max) {
        if (max <= 0 || frame >= int(s.globes.frames_per_direction())) return;
        const auto& f = s.globes.frame(0, std::uint32_t(frame));
        const int rows = int(std::clamp<std::int64_t>(cur * 80 / max, 0, 80));
        const int top = H - 13 - int(f.height);
        for (int y = int(f.height) - rows; y < int(f.height); ++y)
            for (int x0 = 0; x0 < int(f.width); ++x0) {
                const auto idx = f.pixels[std::size_t(y) * f.width + std::size_t(x0)];
                const int px = x + x0, py = top + y;
                if (!idx || px < 0 || py < 0 || px >= W || py >= H) continue;
                const auto c = pal[idx];
                auto* d = fb.data() + (std::size_t(py) * kW + std::size_t(px)) * 4;
                d[0] = c.r; d[1] = c.g; d[2] = c.b;
            }
    };
    fill(0, 29, st.fixed(d2d::d2s::kLife), st.fixed(d2d::d2s::kMaxLife));
    fill(1, W - 0x6f, st.fixed(d2d::d2s::kMana), st.fixed(d2d::d2s::kMaxMana));
    at_bottom(s.globe_glass, 0, 28, H - 5);
    at_bottom(s.globe_glass, 1, W - 0x6e, H - 9);
}

// The stash panel: art frames 0..3 as 2x2 at the left-panel spot, items
// (location 0, panel 5) in the inventory.txt bank grid.
// ponytail: no gold line, no "close" button; classic stash untested.
// Rect {x, y, w, h} of a stored item in a grid layout.
std::array<int, 4> grid_rect(const Scene& s, const Scene::InvLayout& L, const d2d::d2s::Item& it) {
    const auto info = s.item_info.find(it.code);
    return { L.grid_x + it.column * L.box_w, L.grid_y + it.row * L.box_h,
             (info != s.item_info.end() ? info->second.w : 1) * L.box_w,
             (info != s.item_info.end() ? info->second.h : 1) * L.box_h };
}

// A left-side storage panel: the stash (panel 5) or the cube (panel 4).
void draw_storage(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& L, int panel,
                  int mx, int my, int clvl) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (art.frames_per_direction() >= 4) {
        const auto& f0 = art.frame(0, 0);
        blit_sprite(fb, f0, pal, kCharPanelX, kCharPanelY);
        blit_sprite(fb, art.frame(0, 1), pal, kCharPanelX + int(f0.width), kCharPanelY);
        blit_sprite(fb, art.frame(0, 2), pal, kCharPanelX, kCharPanelY + int(f0.height));
        blit_sprite(fb, art.frame(0, 3), pal, kCharPanelX + int(f0.width), kCharPanelY + int(f0.height));
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : items) {
        if (it.location != 0 || it.panel != panel) continue;
        const auto [x, y, w, h] = grid_rect(s, L, it);
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, pal, x + (w - int(f.width)) / 2, y + (h - int(f.height)) / 2);
        }
        if (mx >= x && mx < x + w && my >= y && my < y + h) { hover = &it; hb = { x, y, w, h }; }
    }
    if (hover) draw_hover_text(fb, s, item_lines(s, *hover, clvl), hb[0], hb[0] + hb[2], hb[1] + hb[3], hb[1]);
}

// The equipped belt's belts.txt index (armor.txt `belt`), 2 ("default":
// one row) without one — as the popup code picks it (0x49906b).
int belt_index(const Scene& s, const std::vector<d2d::d2s::Item>& items) {
    for (const auto& it : items)
        if (it.location == 1 && it.slot == 8)
            if (const auto i = s.item_info.find(it.code); i != s.item_info.end() && i->second.belt >= 0
                && i->second.belt < 7)
                return i->second.belt;
    return 2;
}

// The belt: items in location 2 keep their slot (0..15, 4 per row) in the
// column field and sit centred in the belt's belts.txt boxes. Row 1 is the
// HUD strip; with the popup open (0x499136) each further row gets a
// ctrlpnl_popbelt frame 0, bottom-anchored at x W/2+21, bottom H-41-32i,
// and its items. Hovering an item shows its hover text.
// ponytail: no slot hotkey numbers.
void draw_belt(std::vector<std::uint8_t>& fb, const Scene& s, const std::vector<d2d::d2s::Item>& items,
               int mx, int my, int clvl, bool popup) {
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    const auto& B = s.belts[std::size_t(belt_index(s, items))];
    const int rows = B.boxes / 4;
    if (popup && s.popbelt.frames_per_direction() > 0) {
        const auto& f = s.popbelt.frame(0, 0);
        for (int i = 0; i + 1 < rows; ++i)
            blit_sprite(fb, f, pal, int(kW) / 2 + 21, int(kH) - 41 - 32 * i - int(f.height) + 1);
    }
    const d2d::d2s::Item* hover = nullptr;
    std::array<int, 4> hb{};
    for (const auto& it : items) {
        if (it.location != 2 || it.column < 0 || it.column >= B.boxes || (!popup && it.column > 3)) continue;
        const auto& b = B.box[std::size_t(it.column)];
        if (b[1] <= b[0]) continue;
        if (const auto* spr = s.item_sprite(it); spr && spr->frames_per_direction() > 0) {
            const auto& f = spr->frame(0, 0);
            blit_sprite(fb, f, pal, b[0] + (b[1] - b[0] + 1 - int(f.width)) / 2,
                        b[2] + (b[3] - b[2] + 1 - int(f.height)) / 2);
        }
        if (mx >= b[0] && mx <= b[1] && my >= b[2] && my <= b[3]) { hover = &it; hb = b; }
    }
    if (hover) draw_hover_text(fb, s, item_lines(s, *hover, clvl), hb[0], hb[1] + 1, hb[3] + 1, hb[2]);
}

void render_ingame(std::vector<std::uint8_t>& fb,
                   const Scene& s,
                   int class_idx,
                   const Scene::Appearance& gfx,
                   std::string_view name,
                   bool hardcore,
                   float cam_x,
                   float cam_y,
                   int player_mode,
                   int player_dir,
                   std::uint32_t elapsed_ms,
                   int mouse_x = -1, int mouse_y = -1,
                   std::span<const NpcState> npcs = {},
                   const std::vector<d2d::d2s::Item>* inventory = nullptr,
                   const d2d::d2s::Stats* char_stats = nullptr,
                   const d2d::d2s::Stats* hud_stats = nullptr,
                   const PanelStats* panel = nullptr,
                   std::uint32_t player_mode_ms = 0,
                   const std::vector<d2d::d2s::Item>* belt = nullptr,
                   int* hovered_npc = nullptr,
                   const std::vector<d2d::d2s::Item>* stash = nullptr, bool stash_expansion = true,
                   bool belt_popup = false, bool cube_open = false,
                   const NpcMenuState* npc_menu = nullptr, const Speech* speech = nullptr) {
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
        // Player: the camera follows them, so their feet sit on the camera
        // point (kW/2, kH/2 + kIsoH/2); render_world slots them into the
        // wall pass by depth. Wears the loaded save's gear, or the
        // class's starting gear.
        std::vector<Unit> units;
        units.reserve(s.world_npcs.size() + 1);
        if (class_idx >= 0 && class_idx < 7)
            units.push_back({ cam_x, cam_y, &s.composite(kUiToSaveClass[class_idx], player_mode, gfx),
                              player_dir, nullptr, player_mode_ms });
        // NPCs and objects, at their live position when they patrol.
        for (std::size_t i = 0; i < s.world_npcs.size(); ++i) {
            const auto& n = s.world_npcs[i];
            const NpcState* st = i < npcs.size() ? &npcs[i] : nullptr;
            const float x = st ? st->x : n.x, y = st ? st->y : n.y;
            if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
            const auto& anim = s.npc_anim(n, st && st->walking ? std::string_view("WL") : std::string_view(n.mode));
            units.push_back({ x, y, &anim, st ? st->dir : 0, &n.name, st ? st->mode_ms : 0, int(i) });
        }
        std::pair<const Unit*, std::array<int, 4>> hovered{ nullptr, {} };
        render_world(fb, s, cam_x, cam_y, elapsed_ms, units, mouse_x, mouse_y, &hovered);
        if (hovered_npc) *hovered_npc = hovered.first ? hovered.first->npc : -1;
        // Name over whatever the cursor points at, centred above it.
        // ponytail: no highlight tint yet (D2 brightens the unit too).
        if (hovered.first) {
            const auto& nm = *hovered.first->name;
            const auto& b  = hovered.second;
            const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
            s.font.draw(fb, kW, kH, pal, (b[0] + b[2]) / 2 - s.font.measure(nm) / 2,
                        b[1] - s.font.line_height() - 2, nm);
        }
        if (inventory && class_idx >= 0 && class_idx < 7)
            draw_inventory(fb, s, s.inv_layout[std::size_t(kUiToSaveClass[class_idx])], *inventory,
                           mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
        if (char_stats) draw_char_panel(fb, s, *char_stats, panel ? *panel : PanelStats{}, name, class_idx);
        if (stash) {
            const int e = stash_expansion ? 1 : 0;
            if (cube_open)
                draw_storage(fb, s, *stash, s.cube_panel, s.cube_layout, 4, mouse_x, mouse_y,
                             hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
            else
                draw_storage(fb, s, *stash, s.stash_panel[std::size_t(e)], s.stash_layout[std::size_t(e)], 5,
                             mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1);
        }
        if (npc_menu) draw_npc_menu(fb, s, *npc_menu, mouse_x, mouse_y, elapsed_ms);
        if (speech) draw_speech(fb, s, *speech, elapsed_ms);
        if (hud_stats) draw_hud(fb, s, *hud_stats);
        if (belt) draw_belt(fb, s, *belt, mouse_x, mouse_y, hud_stats ? int(hud_stats->get(d2d::d2s::kLevel)) : 1,
                            belt_popup);
        // Dev overlay: a red dot on every blocked subtile around the camera.
        if (g_debug_collision) {
            const int cx = int(cam_x * 5), cy = int(cam_y * 5);
            for (int sy = cy - 60; sy <= cy + 60; ++sy)
                for (int sx = cx - 60; sx <= cx + 60; ++sx) {
                    const float wx = (float(sx) + 0.5f) / 5, wy = (float(sy) + 0.5f) / 5;
                    if (!s.blocked(wx, wy)) continue;
                    const int px = int(kW) / 2 + int(std::lround(((wx - cam_x) - (wy - cam_y)) * (kIsoW / 2)));
                    const int py = int(kH) / 2 + kIsoH / 2 + int(std::lround(((wx - cam_x) + (wy - cam_y)) * (kIsoH / 2)));
                    for (int oy = -1; oy <= 1; ++oy)
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int x = px + ox, y = py + oy;
                            if (x < 0 || y < 0 || x >= int(kW) || y >= int(kH)) continue;
                            auto* d = fb.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
                            d[0] = 255; d[1] = 0; d[2] = 0;
                        }
                }
        }
        set_phase(MainPhase::IngameHudText);
    } else {
        std::fill(fb.begin(), fb.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < fb.size(); i += 4) fb[i] = 0xFF;
        blit_dc6_grid(fb, s.credits_bg, s.pal, 0, 0, s.bg_tiles_across);
    }
    const auto& pal = s.act1_pal.entries().empty() ? s.pal : s.act1_pal;
    if (speech && speech->npc >= 0) return;               // the dev overlay would cover the speech box

    std::string cls = kClassKey[class_idx];
    if (auto v = lookup_string(s, kClassKey[class_idx])) cls = u16_to_latin1(*v);

    constexpr const char* welcome = "WELCOME TO SANCTUARY";
    const int ww = s.font.measure(welcome);
    // Dev HUD at the top edge, clear of the player at screen centre.
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ww/2, 8,
                       welcome, 255, 208, 80);

    // On hardcore, D2 marks the caption with a red " (HC)" suffix — we
    // fudge that with a red tint on the trailing tag.
    const std::string line = name.empty() ? cls : std::string(name) + " the " + cls;
    const int lw = s.font.measure(line);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - lw/2, 28, line);
    if (hardcore) {
        constexpr const char* tag = " (HARDCORE)";
        const int tw = s.font.measure(tag);
        s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - lw/2 + lw, 28,
                           tag, 220, 60, 60);
        (void)tw;
    }

    if (inventory || char_stats || stash || belt_popup) return;   // the hint would run under a panel
    constexpr const char* hint =
        "d2d dev build — click to walk around the Rogue camp";
    const int hw = s.font.measure(hint);
    s.font.draw(fb, kW, kH, pal, int(kW)/2 - hw/2, int(kH) - 140, hint);   // above the HUD bar
    constexpr const char* esc = "press Esc to return to title";
    const int ew = s.font.measure(esc);
    s.font.draw_tinted(fb, kW, kH, pal, int(kW)/2 - ew/2, int(kH) - 120,
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
// Sound output: SDL3 audio streams on the default playback device (the
// dummy driver when headless). Channels: the NPC voice, the level's song
// and its ambience; Sounds.txt `Loop` entries are re-queued as they drain.
// Files: music (`Music Vol` 1) under data\global\music, the rest under
// data\global\sfx, speech under data\local\sfx.
// ponytail: no mixer limits, 3D falloff, fades or effect sounds yet.
struct Audio {
    struct Channel {
        SDL_AudioStream* stream = nullptr;
        std::vector<Uint8> pcm;
        int sound = 0;                           // Sounds.txt index, 0 = silent
        bool loop = false;
    };
    bool ok = false;
    Channel voice, music, ambience, ui;
    // Songs are ~20 MB WAVs (240 ms to read): decoded on a worker with its
    // own MPQ handles (StormLib handles aren't shared across threads), the
    // stream made here once it's ready.
    struct Decoded { SDL_AudioSpec spec{}; std::vector<Uint8> pcm; };
    std::future<std::optional<Decoded>> music_job;
    int music_job_sound = 0;
    float music_job_gain = 1.f;
    int voice_sound() const { return voice.sound; }

    void init() { ok = SDL_InitSubSystem(SDL_INIT_AUDIO); if (!ok) std::fprintf(stderr, "[d2d] audio: %s\n", SDL_GetError()); }
    static std::optional<Decoded> decode(std::span<const std::byte> wav) {
        Decoded d;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(wav.data(), wav.size()), true, &d.spec, &buf, &len)) return std::nullopt;
        d.pcm.assign(buf, buf + len);
        SDL_free(buf);
        return d;
    }
    void play_music(const Scene& s, int index) {
        stop(music);
        if (!ok || index <= 0 || std::size_t(index) >= s.sounds.size()) return;
        const auto& snd = s.sounds[std::size_t(index)];
        music.sound = index;                     // pending until the job lands
        music_job_sound = index;
        music_job_gain = float(std::clamp(snd.volume, 0, 255)) / 255.f;
        music_job = std::async(std::launch::async, [dir = s.data_dir, file = snd.file]() -> std::optional<Decoded> {
            d2d::mpq::Stack st;
            for (const char* n : { "d2xmusic.mpq", "d2music.mpq" })
                if (fs::exists(dir / n)) st.push(dir / n);
            const auto wav = st.try_read(std::string(R"(data\global\music\)") + file);
            return wav ? decode(*wav) : std::nullopt;
        });
    }
    static void stop(Channel& c) {
        if (c.stream) SDL_DestroyAudioStream(c.stream);
        c = {};
    }
    void stop_voice() { stop(voice); }
    // Start WAV bytes on a channel (replacing what it played).
    bool start(Channel& c, std::span<const std::byte> wav, float gain, bool loop, int index) {
        stop(c);
        auto d = decode(wav);
        if (!d) { std::fprintf(stderr, "[d2d] sound %d: %s\n", index, SDL_GetError()); return false; }
        c.pcm = std::move(d->pcm);
        c.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &d->spec, nullptr, nullptr);
        if (!c.stream) { c.pcm.clear(); return false; }
        SDL_SetAudioStreamGain(c.stream, gain);
        SDL_PutAudioStreamData(c.stream, c.pcm.data(), int(c.pcm.size()));
        if (!loop) SDL_FlushAudioStream(c.stream);
        SDL_ResumeAudioStreamDevice(c.stream);
        c.sound = index;
        c.loop = loop;
        return true;
    }
    void play(Channel& c, const Scene& s, int index) {
        stop(c);
        if (!ok || index <= 0 || std::size_t(index) >= s.sounds.size()) return;
        const auto& snd = s.sounds[std::size_t(index)];
        std::optional<std::vector<std::byte>> wav;
        if (snd.music) wav = s.mpqs.try_read(std::string(R"(data\global\music\)") + snd.file);
        for (const char* root : { R"(data\global\sfx\)", R"(data\local\sfx\)" })
            if (!wav) wav = s.mpqs.try_read(std::string(root) + snd.file);
        if (!wav) { std::fprintf(stderr, "[d2d] sound %d: %s not found\n", index, snd.file.c_str()); return; }
        start(c, *wav, float(std::clamp(snd.volume, 0, 255)) / 255.f, snd.loop, index);
    }
    // A fixed-path UI sound (game.exe names these directly, not via Sounds.txt).
    // The decoded WAV is cached; each play restarts the channel.
    std::unordered_map<std::string, std::vector<std::byte>> file_cache;
    void play_file(Channel& c, const Scene& s, const std::string& path) {
        if (!ok) return;
        auto it = file_cache.find(path);
        if (it == file_cache.end()) {
            auto b = s.mpqs.try_read(path);
            if (!b) return;
            it = file_cache.emplace(path, std::move(*b)).first;
        }
        start(c, it->second, 1.f, false, -1);
    }
    void play_voice(const Scene& s, int index) { play(voice, s, index); }
    // Re-queue loops before they drain; drop one-shots that have played out.
    void update() {
        if (music_job.valid() && music_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto d = music_job.get();
            if (!d) std::fprintf(stderr, "[d2d] music %d: not loaded\n", music_job_sound);
            if (d && music.sound == music_job_sound) {
                music.pcm = std::move(d->pcm);
                music.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &d->spec, nullptr, nullptr);
                if (!music.stream) std::fprintf(stderr, "[d2d] music: %s\n", SDL_GetError());
                if (music.stream) {
                    SDL_SetAudioStreamGain(music.stream, music_job_gain);
                    SDL_PutAudioStreamData(music.stream, music.pcm.data(), int(music.pcm.size()));
                    SDL_ResumeAudioStreamDevice(music.stream);
                    music.loop = true;
                }
            }
        }
        for (auto* c : { &voice, &music, &ambience, &ui }) {
            if (!c->stream) continue;
            const int queued = SDL_GetAudioStreamQueued(c->stream);
            if (c->loop && queued < int(c->pcm.size() / 4))
                SDL_PutAudioStreamData(c->stream, c->pcm.data(), int(c->pcm.size()));
            else if (!c->loop && queued <= 0)
                stop(*c);
        }
    }
};

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
        SDL_HideCursor();   // d2d draws D2's gauntlet into the frame
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
        // Keep printable Latin-1 (0x20..0xFF) and line breaks (two-line
        // labels like "Fire\nResistance"), drop the rest — D2 UI strings
        // are ASCII with occasional accented chars, all inside Latin-1.
        if ((c >= 0x20 && c <= 0xFF) || c == '\n') out.push_back(char(c));
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
void handle_sdl_events(SDL_Event& ev, Mouse& m, Screen& current_screen,
                       std::string& text_input, bool& text_backspace,
                       std::vector<SDL_Keycode>& keys, std::atomic<bool>& quit) {
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
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        if (ev.key.key == SDLK_ESCAPE) {
            // Esc pops one layer up:
            //   Title       -> quit
            //   CharCreate  -> CharSelect  (the flow you came from)
            //   InGame      -> closes an open panel, else CharSelect (the
            //                  roster; matches D2) — handled in-game
            //   everything else -> Title
            switch (current_screen) {
                case Screen::Title:      quit = true; break;
                case Screen::CharCreate: current_screen = Screen::CharSelect; break;
                case Screen::InGame:     keys.push_back(ev.key.key); break;
                default:                 current_screen = Screen::Title; break;
            }
        } else if (ev.key.key == SDLK_BACKSPACE) {
            text_backspace = true;
        } else {
            keys.push_back(ev.key.key);   // per-screen key handling
        }
    } else if (ev.type == SDL_EVENT_TEXT_INPUT) {
        // ev.text.text is UTF-8; keep the printable Latin-1 subset.
        for (const char* p = ev.text.text; *p; ++p) {
            const auto c = static_cast<unsigned char>(*p);
            if (c >= 0x20 && c <= 0x7e) text_input.push_back(char(c));
        }
    } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        m.x = int(ev.motion.x);
        m.y = int(ev.motion.y);
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        m.x = int(ev.button.x);
        m.y = int(ev.button.y);
        if (ev.button.button == SDL_BUTTON_LEFT) {
            m.down = true;
            m.press_this_frame = true;
        } else if (ev.button.button == SDL_BUTTON_RIGHT) {
            m.rpress_this_frame = true;
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
    Audio audio;
    audio.init();
    if (scene)
        g_on_button_press = [&] { audio.play_file(audio.ui, *scene, R"(data\global\sfx\cursor\button.wav)"); };
    struct ClearHook { ~ClearHook() { g_on_button_press = nullptr; } } clear_hook;   // audio dies with this scope

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
    std::vector<NpcState> npc_states;
    if (scene)
        for (std::size_t i = 0; i < scene->world_npcs.size(); ++i) {
            const auto& n = scene->world_npcs[i];
            npc_states.push_back({ n.x, n.y, 0, false, 0, std::uint32_t(1000 + 700 * i % 3000) });
        }
    float target_x = player_x, target_y = player_y;
    bool  walking = false;
    bool  running = false;                 // R toggles, like D2's run/walk button
    bool  stash_open = false;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    std::uint32_t talk_rng = 0x2545f491u;
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
        if (args.size() < 2 || args[1] != "collision") return std::string("err debug collision\n");
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
             + " menu=" + std::to_string(npc_menu.npc >= 0 ? int(npc_menu.lines.size()) : 0)
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
        if (screen != Screen::InGame && audio.music.sound != 0) {
            Audio::stop(audio.music);
            Audio::stop(audio.ambience);
            Audio::stop(audio.voice);
        }
        audio.update();
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
                    if (k == SDLK_C) { char_open = !char_open; if (char_open) stash_open = cube_open = false; }
                    if (k == SDLK_ESCAPE) {
                        if (speech.npc >= 0) speech = {};                   // speech first
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
                    ((char_open || stash_open || cube_open) && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320
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
                // The NPC's voice (FUN_004a10e0 plays FUN_004e0650's sound for
                // the speech string) follows the speech box.
                if (speech.npc < 0 && audio.voice.stream) audio.stop_voice();
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
                    if (action == NpcMenuState::kTalk) {
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
                            if (o.operate_fn == 32 || (o.root == "monsters" && menu)) {
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
                    // Patrolling NPCs: walk to the next DS1 path point, idle
                    // a few seconds there, move on. ponytail: the per-point
                    // action (1..4 — likely S1 specials like Charsi's
                    // hammering) isn't interpreted; pauses are 2-5 s.
                    for (std::size_t i = 0; i < npc_states.size(); ++i) {
                        const auto& path = scene->world_npcs[i].path;
                        if (path.empty()) continue;
                        auto& st = npc_states[i];
                        if (int(i) == npc_menu.npc || int(i) == speech.npc) {   // talking: stand still
                            if (st.walking) { st.walking = false; st.mode_ms = ms; }
                            st.wait_until = ms + 2000;
                            continue;
                        }
                        if (!st.walking) {
                            if (ms >= st.wait_until) { st.walking = true; st.mode_ms = ms; }
                            continue;
                        }
                        const auto [tx, ty] = path[st.next % path.size()];
                        const float dx = tx - st.x, dy = ty - st.y;
                        const float dist = std::hypot(dx, dy),
                                    step = cells_per_sec(scene->world_npcs[i].velocity) * dt;
                        if (dist > 0.05f) st.dir = direction16(dx, dy);
                        if (dist <= step) {
                            st.x = tx; st.y = ty; st.walking = false; st.mode_ms = ms;
                            st.next = (st.next + 1) % path.size();
                            st.wait_until = ms + 2000 + std::uint32_t((i * 1237 + st.next * 911) % 3000);
                        } else {
                            st.x += dx / dist * step; st.y += dy / dist * step;
                        }
                    }
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
                              cube_open, &npc_menu, &speech);
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
            if (scene->cursor.frames_per_direction() >= 8) {
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
        ++frame_count;
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
            std::fprintf(stderr,
                "[d2d] slow frame: input=%u ms render=%u ms present=%u ms screen=%d cam=(%d,%d)\n",
                dt_input, dt_render, dt_present, int(screen), int(player_x), int(player_y));
        }
        if (ms - stat_last_report_ms >= 5000) {
            const std::uint32_t avg_r = stat_frames ? stat_render_sum  / stat_frames : 0;
            const std::uint32_t avg_p = stat_frames ? stat_present_sum / stat_frames : 0;
            std::fprintf(stderr,
                "[d2d] alive: %u frames/5s | input max=%u | render avg=%u max=%u | present avg=%u max=%u | screen=%d cam=(%d,%d)\n",
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
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    return run_windowed(fb, scene, ch, frame_count, quit);
}
