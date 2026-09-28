// Menu screens: title, credits, char-select, char-create, cinematics.
#pragma once

#include "scene.hpp"
#include "ui.hpp"

namespace d2d::client {

void render_title(std::vector<std::uint8_t>& framebuffer,
                  const Scene& scene,
                  std::span<const Button> buttons,
                  std::uint32_t elapsed_ms);

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
int charselect_max_scroll(int count);

// D2 font colours used on char-select (FUN_004fc9b0 colour arg).
constexpr std::uint8_t kTextRed[3]   = { 255, 77, 77 };    // 1
constexpr std::uint8_t kTextGreen[3] = { 0, 255, 0 };      // 2
constexpr std::uint8_t kTextGold[3]  = { 199, 179, 119 };  // 4

// Name prefix earned by beating difficulties — FUN_005068a0 picks a tier
// from the progression byte, FUN_00505640 the (hard-coded English) word.
// Tier: classic <4/<8/<12/else, LoD <5/<10/<15/else -> 0..3; hardcore
// shifts non-zero tiers by 3. Female: Amazon, Sorceress, Assassin.
std::string char_title(const d2d::d2s::Header& header);

// Visible slot index under (x, y), or -1. Row-major: slot i = row i/2, col i%2.
int charselect_slot_at(int x, int y);

void render_charselect(std::vector<std::uint8_t>& framebuffer,
                       const Scene& scene,
                       const CharSelectUI& select_ui,
                       std::uint32_t elapsed_ms);

// Full-screen credits background + scrolling text. The scroll starts with
// the first line off the bottom of the screen and advances upward at ~1 px
// per D2 tick (25 Hz). When the last line clears the top, the scroll loops.
void render_credits(std::vector<std::uint8_t>& framebuffer,
                    const Scene& scene,
                    std::uint32_t elapsed_ms);

// Character-creation screen — the iconic seven-classes-around-a-campfire
// scene, loaded by FUN_004326f0. For MVP we blit each class's nu1 (idle)
// cycle at hardcoded positions matching the D2 layout, plus the fire
// animation in the pit. Selection / hover / class labels are follow-ups.

// Advance the per-class state machine — completes one-shot animations
// (Selecting → Selected, Deselecting → Idle) once they finish.
void advance_char_states(CharCreateUI& create_ui,
                         const Scene& scene,
                         std::uint32_t elapsed_ms);

// Hit-test click position against class silhouettes and trigger selection
// transitions. Only one class is Selected/Selecting at a time; picking a
// new one first sends the previous into Deselecting.
// Hardcore-checkbox rect — RE'd char-create master table 0x70b0b0.
constexpr int kHardcoreX = 319, kHardcoreW = 15, kHardcoreH = 16;
constexpr int kHardcoreY = rec_top(560, kHardcoreH);

void handle_charcreate_click(CharCreateUI& create_ui,
                             const Mouse& mouse,
                             std::uint32_t elapsed_ms);

void render_charcreate(std::vector<std::uint8_t>& framebuffer,
                       const Scene& scene,
                       const CharCreateUI& create_ui,
                       std::uint32_t elapsed_ms);

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
// The LoD cinematics screen (FUN_00431600 -> FUN_004313d0): frontend
// records 8 (title background), 0x3d (CinematicsSelectionEXP panel,
// bottom-left (237, 505)), 0x3e (the "SELECT CINEMATICS" 0x13fa text box,
// (262, 153) 272x35), 0x3f..0x45 (WideButtons at x 262, bottoms 181..439,
// strings 0x5525..0x552b) and 0x46 (CANCEL 0x13ef, MediumButton at
// (334, 488)). The first `unlocked` entries work; the rest are disabled
// (FUN_004f96f0). Unlocking follows game.exe's "Aux Battle.net" bits.
struct CinematicsUI {
    std::array<Button, 7> entry{};
    std::array<std::string, 7> labels;
    Button cancel{};
    std::string cancel_label, heading;
    int unlocked = 1;
};
// The videos behind the seven entries, in order (handlers 0x434320..).
constexpr std::array<const char*, 7> kCinematicVideo = {
    R"(data\local\video\ENG\d2intro640x292.bik)",   R"(data\local\video\ENG\Act02start640x292.bik)",
    R"(data\local\video\ENG\Act03start640x292.bik)", R"(data\local\video\ENG\Act04start640x292.bik)",
    R"(data\local\video\ENG\Act04end640x292.bik)",   R"(data\local\video\ENG\D2x_Intro_640x292.bik)",
    R"(data\local\video\ENG\D2x_Out_640x292.bik)" };

// How many entries are unlocked, from what has been seen: game.exe's
// chain (FUN_00431600) over its registry bits — 0x01 LoD ending: 7,
// 0x80 LoD intro: 6, 0x10 act 4 end: 5, 0x08 act 4: 4, 0x40 act 3: 3,
// 0x04 act 2: 2, else 1. We keep the same facts in cinematics_seen.
int cinematics_unlocked(const std::string& seen);

CinematicsUI cinematics_ui(const Scene& scene, int unlocked);

void render_cinematics(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const CinematicsUI& cinematics);

TitleUI title_ui(const Scene& scene);

}  // namespace d2d::client
