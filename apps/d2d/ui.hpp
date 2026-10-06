// SPDX-License-Identifier: GPL-3.0-or-later
// Frontend widgets: screen enum, buttons, mouse, NPC menus + speech, char-create state.
#pragma once

#include "common.hpp"
#include "scene.hpp"

#include <dc6.hpp>
#include <palette.hpp>
#include <quests.hpp>
#include <rules.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace d2d::client {

// --- Screen state machine + mouse routing ---------------------------------

enum class Screen { Title, Credits, CharSelect, CharCreate, InGame, Video, Cinematics };

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

// Class placement records: the kind-3 records game.exe builds the LoD
// classes from (FUN_00435580: FUN_0042f430(index), record 0x708d10 +
// index * 0x30). (x, y) is the sprite anchor; w x h isn't used (see
// render_charcreate). Order matches Scene::class_anims (BA, NE, PA, AM,
// SO, DZ, AS). Records:
//   AM 0x70b050  NE 0x70af00  AS 0x70b440  BA 0x70af30
//   PA 0x70b020  SO 0x70aff0  DZ 0x70b470
struct ClassPos { int x, y, width, height; };
constexpr ClassPos kClassPos[7] = {
    {400, 330, 88, 184},   // Barbarian
    {301, 333, 88, 184},   // Necromancer
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
// The screen's class order (kClassKey) <-> the .d2s class id (AM SO NE
// PA BA DZ AS: Character::character_class).
constexpr int kSaveClassToUi[7] = { 3, 4, 1, 2, 0, 5, 6 };
constexpr int kUiToSaveClass[7] = { d2d::d2s::kBarbarian, d2d::d2s::kNecromancer, d2d::d2s::kPaladin, d2d::d2s::kAmazon,
                                    d2d::d2s::kSorceress, d2d::d2s::kDruid, d2d::d2s::kAssassin };

// D2's frontend records store (x, y, w, h) with y = the BOTTOM row
// (bottom-left anchor, like its DC6 blits): the full-screen BG record is
// (0, 599, 800, 600). Our blits and hit tests are top-left, so convert.
constexpr int rec_top(int y_bottom, int height) { return y_bottom - height + 1; }

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
    int x{}, y{}, width{}, height{};
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
void blit_button_chrome(std::vector<std::uint8_t>& framebuffer,
                        const d2d::palette::Palette& pal,
                        const d2d::dc6::Sprite& chrome,
                        int x, int y, bool pressed);

// Update hover/pressed state and, on a mouse-up over a hovered+pressed
// button, invoke the action. Returns true if any action was taken so the
// caller can early-out.

// An open NPC menu, as game.exe builds and lays it out (FUN_004b4830,
// FUN_004b85f0, FUN_004b8410; docs/research/re/npc-menu.md): a gold
// font16 header with the NPC's name (21 px line), the npc_menu.hpp
// entries and "cancel" (string 0x102e), 15 px lines, white. Box: widest
// line + 20 by the line heights + 15, placed at (cx - w/2, cy - h/3) where
// (cx, cy) is the NPC's feet on screen raised 150 px (FUN_004b1c80), then
// kept inside the screen.
struct NpcMenuState {
    int npc = -1;                            // Level::npcs index, -1 = closed
    // What choosing a line does.
    enum Action { kClose, kTalk, kIntro, kGossip, kTrade, kGamble, kHire, kIdentify, kHireOffer, kQuest, kRespec, kRespecOk, kGoEast, kImbue, kResurrectMerc };
    struct Line { std::string text; int height = 15, width = 0, x = 0; bool header = false; Action action = kClose; int arg = -1; };
    std::vector<Line> lines;
    int x = 0, y = 0, box_width = 0, box_height = 0;
    // Index of the selectable line under (mx, my), or -1.
    [[nodiscard]] int line_at(int mouse_x, int mouse_y) const {
        if (npc < 0 || mouse_x < x || mouse_x >= x + box_width) return -1;
        int base = y;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            base += lines[i].height;
            if (!lines[i].header && mouse_y > base - lines[i].height && mouse_y <= base) return int(i);
        }
        return -1;
    }
};

void layout_npc_menu(const Scene& scene, NpcMenuState& menu, int screen_x, int screen_y);

// clvl and the unidentified item count adjust the table like game.exe:
// Kashya gains "hire" above level 7 (FUN_004b66b0 -> FUN_004b6410
// patches her record to talk, hire); "identify items" only shows when
// something needs it (FUN_004b4830). Akara's record is patched to talk,
// trade, "Reset Stat/Skill Points" (0x2ba0, 0x4b6da0), the last shown while
// `respec` (quest 41: not used, and open — or any time in Hell). A hire
// NPC (rules::kMercNpcs) gains `resurrect` (entry 0x1507, its text built
// by resurrect_line) while the merc is dead, ahead of "hire" or else last
// (FUN_004b6440).
NpcMenuState open_npc_menu(const Scene& scene, const Level& level, int npc, int screen_x, int screen_y, int clvl = 1, int unidentified = 0,
                           bool respec = false, bool east = false,   // east: Warriv's Go East (quest 6 done, table 0x7253e0)
                           bool imbue = false,                       // imbue: Charsi's Imbue (quest 3 bit 1, 0x4b3700)
                           const std::string& resurrect = {});

// The resurrect entry's text (FUN_004b4830): "Resurrect %s: %d" (0x58a8)
// with the merc's name and the cost.
std::string resurrect_line(const Scene& scene, int merc_type, int name_index, int cost);

// The reset's confirmation (0x4b5ad0): its name (gold), "ok" (0xd49),
// "cancel" (0xd48).
NpcMenuState open_respec_menu(const Scene& scene, int npc, int screen_x, int screen_y);

// The talk submenu (FUN_004b5890): header "talk" (gold), "introduction"
// unless the NPC's talk record says no_intro, "gossip", then "cancel"
// (0xd48). The NPC's quest topics (the server's kind-2 messages,
// FUN_0049f900) go after gossip, each under its quest's name (the table at
// 0x722678: message -> name).
// ponytail: the Den of Evil's names only (messages 64..80 -> 3714); no
// Greiz/Cain extras.
NpcMenuState open_talk_menu(const Scene& scene, const Level& level, int npc, int screen_x, int screen_y,
                            const std::vector<d2d::rules::QuestMsg>& quest = {});

void layout_npc_menu(const Scene& scene, NpcMenuState& menu, int screen_x, int screen_y);

// An NPC's speech topic for the player's class. Introduction
// (FUN_004b41e0): topic 1 when its class is the player's, else topic 0.
// Gossip (FUN_004b1680): a random topic >= 2 whose class is 7 (any) or the
// player's and, if quest-gated, whose quest state matches — up to 10
// tries, else topic 2. game.exe picks it once per game per NPC; so do we.
// Quest states are the save's flags for the difficulty it was played on.
int talk_topic(const NpcTalk& talk, bool intro, int cls, d2d::rules::Rng& rng,
               const std::function<bool(int quest)>& quest_done);

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
    [[nodiscard]] int offset_px(std::uint32_t now_ms) const { return int(std::uint64_t(now_ms - start_ms) / 4 * std::uint64_t(rate) >> 10); }
    [[nodiscard]] bool done(std::uint32_t now_ms) const {
        return offset_px(now_ms) > std::max(int(lines.size()) - 1, 1) * 18 + 0x70;
    }
};

Speech start_speech(const Scene& scene, int npc, std::uint16_t string, std::uint32_t now_ms);

void draw_speech(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Speech& speech, std::uint32_t now_ms);

// FUN_004b8100: a black box at draw mode 1 (half transparent), each line's
// text with its baseline at the box top + the heights so far + its own;
// the hovered entry gets focus16 (frame = draw count % 7) bottom-anchored
// at (x - 24, baseline + 4) and (x + width + 2, baseline + 4).
void draw_npc_menu(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const NpcMenuState& menu,
                   int mouse_x, int mouse_y, std::uint32_t now_ms);

// The character-creation screen over the Character it makes; name entry
// (SDL text input) fills Character::name, capped at 15 chars like D2's
// character record; left/right arrows and non-printable keys are ignored.
struct CharCreateUI : Character {
    int selected = -1;           // the class picked, in the screen's order (kSaveClassToUi), -1 none
    // Hardcore checkbox — the char-create master-table record at 0x70b0b0
    // (kind=6 button, x=319, y=560, w=15, h=16, handle=DAT_007797c0
    // (clickbox.dc6), on_click=FUN_00430730 which sets bit 0x04 of the
    // character-struct flags word at [0x7795d4]+0x1ef — that's the D2S
    // "Character Status" hardcore bit). Label from patchstring.tbl id
    // 0x1406 ("Hardcore"). See docs/research/re/char-create-table.md for
    // the full 33-record breakdown, including the Ladder (bit 0x40) and
    // Expansion (bit 0x20) checkbox records also present in the table.
    bool hardcore = false;
    std::array<ClassUI, 7> classes{};
    Button ok_btn{};
    Button cancel_btn{};
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
inline std::function<void()> g_on_button_press;

bool update_button(Button& button, const Mouse& mouse, Screen& current_screen,
                   std::atomic<bool>& quit);

// Parse D2's UTF-16LE-with-BOM credits.txt into Latin-1 lines. The file's
// section headers use a '*' prefix. Skips empty lines but keeps '*' lines
// as-is (renderer decides whether to style them).
std::vector<std::string> parse_credits_utf16(std::span<const std::byte> bytes);

}  // namespace d2d::client
