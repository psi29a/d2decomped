// SPDX-License-Identifier: GPL-3.0-or-later
// In-game panels: inventory, character, HUD, stash/cube, belt, automap, waypoints.
#pragma once

#include "common.hpp"
#include "scene.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <dc6.hpp>
#include <quests.hpp>
#include <rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// The inventory panel (inventory.txt "<Class>2" layout, invchar6.dc6):
// grid items (panel 1) centred in their w x h cell block, equipped items
// centred in their body slot's box. Palette: the act's, like the world.
// The item under (mx, my) gets its hover text.
// Items draw through their inventory colormap (Scene::item_pal).
// ponytail: no cube.
void draw_inventory(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Scene::InvLayout& layout,
                    const std::vector<d2d::d2s::Item>& items, int mouse_x = -1, int mouse_y = -1, const d2d::rules::Wearer* wearer = nullptr,
                    const std::function<std::string(const d2d::d2s::Item&)>* price = nullptr);

// Character panel (left of the inventory: 800x600 puts the left panels
// at 80..400). Art: invchar6.dc6 frames 0..3. Label and value boxes are
// game.exe's own tables — labels {x0, y, x1, string id} at 0x724818
// (18-byte records), values {x0, y, x1, stat id} at 0x724928 — in panel
// coordinates; text is centred in [x0, x1].
constexpr int kCharPanelX = 80, kCharPanelY = 60;
struct PanelText { int left, y, right, id; };
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
// The left / right skill's attack blocks (FUN_004ed570): {x0, y, x1} at
// 0x72d840, 6 per block: name, "Damage", its value, the attack rating
// label, unused, its value. id is unused.
constexpr PanelText kAttackBlock[] = {
    { 162,  93, 258, 0 }, { 162, 101, 258, 0 }, { 263,  98, 307, 0 }, { 162, 160, 270, 0 }, { 162, 163, 270, 0 }, { 270, 160, 310, 0 },
    { 162, 117, 258, 0 }, { 162, 125, 258, 0 }, { 263, 122, 307, 0 }, { 162, 184, 270, 0 }, { 162, 187, 270, 0 }, { 270, 184, 310, 0 },
};

// Stat point buttons, from game.exe's table at 0x724a48 (14-byte records
// {u32 x, u32 y, u32 pressed, u16 stat}): the button's bottom-left in panel
// coordinates, and the stat a click spends on. Hit box x in (x, x+40),
// y in (y-22, y) (FUN_004a7720 / FUN_004a78c0).
struct StatButton { int x, y, stat; };
constexpr StatButton kStatButtons[4] = { { 117, 105, 0 }, { 117, 167, 2 }, { 117, 253, 3 }, { 117, 315, 1 } };

// The stat button under (mx, my) (screen), or -1.
int stat_button_at(int mouse_x, int mouse_y);

void draw_char_panel(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats,
                     const PanelStats& panel,
                     std::string_view name, int class_idx, int pressed_button = -1);

// The bottom HUD, as game.exe's 800x600 path draws it (FUN_004983d0 for
// the bar, FUN_00496f80 / FUN_00497110 for the globes). All cels are
// bottom-anchored on the screen's bottom edge:
//   bar: frame 0 (life housing) at x 0, frames 1..4 at 400-235, -107,
//   +21, +149, frame 5 (mana housing) at 800-117; the 48px gaps are the
//   skill buttons.
//   globes: fill = cur * 80 / max rows of hlthmana frame 0 (life; 2 when
//   poisoned) / 1 (mana), bottom at H-13, x 29 / W-111; then the glass
//   (overlap frame 0 at x 28, bottom H-5; frame 1 at W-110, bottom H-9).
//   life: at least 2 rows while alive; hlthmana frame 2 poisoned (state 2).
//   exp bar (FUN_00498ea0): (exp - prev) * 119 / (next - prev) px, two
//   lines at H-38 / H-37 from W/2-144 in palette index 255; none at the
//   max level.
//   run button (FUN_00497480): runbutton frame 0 walk / 2 run (+1 held
//   under the cursor), x W/2-145, bottom H-10.
//   stamina bar (FUN_004975b0): cur * 102 / max px by 18 at (W/2-127,
//   H-27), draw mode 2; gold (FUN_004fb180(f4, c0, 4c)), red under 25 px,
//   blue over max.
//   hover (FUN_00502280, centred, bottom): "Stamina: %d / %d" at W/2-76,
//   H-52; "Experience: %u / %u" at W/2-146, H-51; "Run (key)" at W/2-145,
//   H-23 (FUN_00497300). Globes (FUN_00498120): "Life: %d / %d" /
//   "Mana: %d / %d" plain, centred on 65 / W-80, bottom H-95.
// The maxima include what's worn (Fight::item_max).
// ponytail: no skill icons; no potion preview fill (states 100 / 0x6a), no
// smoothing of the shown values (FUN_00496dd0), no stamina potion's blue
// (state 0x18 / 0x88), no Show HP / MP Text toggles (a globe click,
// DAT_007befdc / e0); the run key shown is R, not the hotkey's binding.
struct Hud { bool poisoned = false, running = false, run_down = false; };
void draw_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats,
              const Hud& hud = {}, int mouse_x = -1, int mouse_y = -1);
// The run button's hit box (FUN_00497440): x W/2-145..W/2-128 by
// H-28..H-8. A press plays Sounds.txt 4 (FUN_004b9a00); the release over
// it toggles run (FUN_004996a0 → FUN_0044be80).
bool over_run_button(int mouse_x, int mouse_y);

// The red New Stats / New Skills buttons over the HUD while stat 4 / 5
// are unspent (UI 6 / 7, set each frame by FUN_004a64c0; drawn by
// FUN_004a6b30 / FUN_004a6e60). x is the socket's left, -1 hidden:
// stats at 40 (W/2+40 with a left panel open), skills at W-73 (W/2-73
// with a right one); both hidden with both sides open or a store, stats
// with the character panel, skills with the tree. levelsocket frame 0
// bottom at H-105, level frame 0 (1 held and under the cursor) at x+3,
// bottom H-109; the label (3986 / 3987) centred on the socket, bottom
// H-142.
struct LevelButtons {
    int stats_x = -1, skills_x = -1;
    bool stats_down = false, skills_down = false;
};
LevelButtons level_buttons(const d2d::d2s::Stats& stats, bool left_open, bool right_open, bool char_open, bool tree_open, bool store_open);
// The hit boxes (FUN_004a6580 / FUN_004a6630), both edges excluded:
// x+1..x+33 by H-138..H-103 (stats), x+1..x+32 by H-137..H-103 (skills).
bool over_stats_button(int x, int mouse_x, int mouse_y);
bool over_skills_button(int x, int mouse_x, int mouse_y);
void draw_level_buttons(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const LevelButtons& buttons, int mouse_x, int mouse_y);

// The stash panel: art frames 0..3 as 2x2 at the left-panel spot, items
// (location 0, panel 5) in the inventory.txt bank grid.
// ponytail: no gold line, no "close" button; classic stash untested.
// Rect {x, y, w, h} of a stored item in a grid layout.
std::array<int, 4> grid_rect(const Scene& scene, const Scene::InvLayout& layout, const d2d::d2s::Item& item);

// A left-side storage panel: the stash (panel 5) or the cube (panel 4).
void draw_storage(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& layout, int panel,
                  int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer);

// A trade with another player (a joined game; NetGame::Trade's state):
// 1 / 2 a box (waiting on them / their request, accept or decline);
// 3.. the window in the left-panel spot, PANEL\trade, their offer in
// "Trade Page 1-2" (items' panel 100), ours in "Trade Page 2-2" (101),
// Accept Trade and Cancel where game.exe has them (FUN_004b8730's boxes,
// tooltips 0x1023 / 0x1022), their accept beside their grid.
// Gold: a click on our bar types an amount (Enter offers it).
// ponytail: the amount is typed in the bar, not game.exe's gold box.
enum class TradeClick { kNone, kAccept, kDecline, kOurGrid, kGold };
void draw_trade(std::vector<std::uint8_t>& framebuffer, const Scene& scene, int state, const std::string& with, const std::string& our_name,
                const std::vector<d2d::d2s::Item>& theirs, const std::vector<d2d::d2s::Item>& ours, std::uint32_t their_gold,
                std::uint32_t our_gold, const std::string* typing, int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer);
TradeClick trade_click(const Scene& scene, int state, int mouse_x, int mouse_y);

// The belt: items in location 2 keep their slot (0..15, 4 per row) in the
// column field and sit centred in the belt's belts.txt boxes. Row 1 is the
// HUD strip; with the popup open (0x499136) each further row gets a
// ctrlpnl_popbelt frame 0, bottom-anchored at x W/2+21, bottom H-41-32i,
// and its items. Hovering an item shows its hover text.
// ponytail: no slot hotkey numbers.
void draw_belt(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
               int mouse_x, int mouse_y, const d2d::rules::Wearer* wearer, bool popup);

// The automap (UI\automap.cpp). Each revealed tile adds one cell
// (FUN_00457cf0): its cel from AutoMap.txt (FUN_0061fff0) at the tile's
// world pixel position ((x - y) * 80, (x + y) * 40) / 10, lower walls
// (orientation > 15) 24 further down. Drawn (FUN_00459700/FUN_00459440)
// at cell - scroll, scroll = player's world pixels / 10 - screen / 2 +
// (40, 15), with DC6's bottom-left anchoring.
// ponytail: reveals tiles within 12 of the player (D2 reveals by room),
// cel picked by a tile hash rather than the game's RNG, no fade near the
// centre, no player/NPC marks; no unit icons or town miniatures of its own
// (a game.exe file's are kept, the miniatures not drawn).
// One automap per Levels.txt Layer: the town and the act 1 wilderness
// share layer 0, so the map shows them together; cells sit in act
// coordinates.
// A cell goes in its list's tree (FUN_00457b00) unless one sits at its
// (x, y) already that it matches: same cel group (0x711258 -> 0x7a3150),
// or it has none.
// Saved next to the character (d2s_automap.hpp): load_automap reads the
// layer's cells in, save_automap appends the ones added since.
struct Automap {
    struct Cell { int cel, x, y; int list = 0; bool saved = false; };   // list: 0 floors, 1 walls, 2 units, 3 miniatures
    std::vector<Cell> cells;
    std::set<std::tuple<int, int, int, int>> placed;               // list, y, x, cel group (-1 none)
    std::unordered_map<int, std::vector<std::uint8_t>> revealed;   // per level, per DS1 tile
    bool open = false;
    bool add(const Cell& cell);
};

// Interchangeable cels (0x711258, cel then group).
inline constexpr int kCelGroups[][2] = {
    {0,0},{1,0},{2,0},{3,0},{6,1},{7,1},{8,1},{11,2},{12,2},{13,3},{14,3},{20,4},{38,4},{21,5},{39,5},{46,6},{47,6},
    {48,6},{49,6},{51,7},{52,7},{53,7},{54,7},{60,8},{70,8},{61,9},{71,9},{120,10},{169,10},{171,10},{121,11},{170,11},
    {172,11},{257,12},{258,12},{259,12},{266,13},{267,13},{337,14},{338,14},{472,15},{473,15},{474,15},{475,15},
    {520,16},{521,16},{522,16},{533,17},{534,17} };
// The player mark's outline (0x6d6638), half-scale steps.
inline constexpr int kMark[13][2] = { {0,-1},{2,-2},{4,-1},{2,0},{4,1},{2,2},{0,1},{-2,2},{-4,1},{-2,0},{-4,-1},{-2,-2},{0,-1} };

int automap_cel(const Scene& scene, const Level& level, int orientation, int main, int sub, std::uint32_t hash);

void automap_reveal(const Scene& scene, const Level& level, Automap& automap, float player_x, float player_y);

void draw_automap(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Automap& automap, float player_x, float player_y);

// The layer's saved cells into `automap` (FUN_00458750), and its new ones
// out (FUN_004584c0); `dir` "" saves nothing.
void load_automap(Automap& automap, const std::filesystem::path& dir, const std::string& name, std::uint32_t map_seed, int layer);
void save_automap(Automap& automap, const std::filesystem::path& dir, const std::string& name, std::uint32_t map_seed, int layer);

// The waypoint panel (FUN_0049c9c0; hit tests FUN_0049c490/0049c510),
// in the left-panel spot at 800x600 (+80, +60 on game.exe's numbers):
// - waygatebackground 2x2; act tabs at bottom 94, x 80 + {3, 67, 129,
//   191, 253} (expansion, 64 wide hits) or {3, 81, 159, 237} (80 wide),
//   frame 2i active / 2i+1 other, drawn only once the act is open
//   (quest 7, 15, 23, 26 done: the previous act's end);
// - up to 9 rows (0x7224e8, 6 ints each): icon at x 97, bottom 149 +
//   ~36i (frame 0 the level you're in, 3/4 activated / hovered, none
//   when not activated), name (Levels.txt LevelName) in font16 at x 160,
//   baseline 144 + 35i: grey not activated, blue hovered or current;
//   hover rows are activated ones, x 97..377, 30 tall from 120 + ~36i;
// - title centred at 240, baseline 108: "Choose your destination"
//   (0xf96) or "No Other Waypoints Activated" (0xf97);
// - cancel: buysellbtn frame 10/11 at 353, bottom 477 (hit 353..389,
//   447..481), "Cancel" (0x1022) on hover.
struct WaypointUI {
    bool open = false;
    int tab = 0, hover = -1, npc = -1;                // npc: the waypoint object touched
    bool cancel_down = false;
};
constexpr int kWpIconBottom[9] = { 89, 125, 161, 197, 234, 270, 306, 342, 378 };
constexpr int kWpTextBase[9]   = { 84, 119, 154, 189, 224, 259, 294, 329, 364 };
constexpr int kWpHitTop[9]     = { 60, 96, 132, 168, 205, 241, 277, 313, 349 };

bool waypoint_act_open(const d2d::d2s::Header& header, int act);

// Row under the cursor (activated waypoints only), or -1.
int waypoint_row_at(const Scene& scene, const WaypointUI& waypoints, const d2d::d2s::Header& header, int mouse_x, int mouse_y);

// Tab under the cursor (open acts only), or -1.
int waypoint_tab_at(const d2d::d2s::Header& header, bool expansion, int mouse_x, int mouse_y);

void draw_waypoints(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const WaypointUI& waypoints,
                    const d2d::d2s::Header& header, bool expansion, int current_level, int mouse_x, int mouse_y);

// The quest log (QuestLog.cpp: FUN_004a34f0 draws it), a left-hand panel
// where the character panel goes. Its table at 0x723f30 (16 bytes a quest:
// shown, icon, slot, act, name record, quest number): act, slot 0..5,
// icon (0x6da2c8's a1q1 ..), the quest's flags number, its name string.
struct QuestEntry { int act, slot, icon, quest, name; };
inline constexpr std::array<QuestEntry, 27> kQuestLog = { {
    { 0, 0, 0, 1, 3714 }, { 0, 1, 1, 2, 3715 }, { 0, 4, 2, 3, 3716 }, { 0, 2, 3, 4, 3717 }, { 0, 3, 4, 5, 3718 }, { 0, 5, 5, 6, 3719 },
    { 1, 0, 6, 9, 923 }, { 1, 1, 7, 10, 924 }, { 1, 2, 8, 11, 925 }, { 1, 3, 9, 12, 926 }, { 1, 4, 10, 13, 927 }, { 1, 5, 11, 14, 928 },
    { 2, 3, 12, 17, 930 }, { 2, 2, 13, 18, 931 }, { 2, 1, 14, 19, 932 }, { 2, 0, 15, 20, 933 }, { 2, 4, 16, 21, 934 }, { 2, 5, 17, 22, 935 },
    { 3, 0, 18, 25, 937 }, { 3, 2, 20, 26, 938 }, { 3, 1, 19, 27, 939 },
    { 4, 0, 21, 35, 22618 }, { 4, 1, 22, 36, 22622 }, { 4, 2, 23, 37, 22627 }, { 4, 3, 24, 38, 22633 }, { 4, 4, 25, 39, 22637 }, { 4, 5, 26, 40, 22641 },
} };
// The slots (0x723ea8, 16 bytes each): the icon's bottom-left, then its
// hit box's top-left (0x38 x 0x34, FUN_004a2630). The act tabs' x: the
// expansion's five (0x40 apart to click), the classic game's four (0x50);
// their bottoms at 33 / 32. The name's baseline and the description's
// first, 20 apart, 270 wide, from x 16 (0x724210..0x724218), in
// FontFormal11 (font 8).
struct QuestSlot { int x, y, hit_x, hit_y; };
inline constexpr std::array<QuestSlot, 6> kQuestSlot = { { { 26, 121, 32, 65 }, { 123, 121, 128, 65 }, { 220, 121, 224, 65 },
                                                            { 26, 218, 32, 163 }, { 123, 218, 128, 163 }, { 220, 218, 224, 163 } } };
inline constexpr std::array<int, 5> kQuestTabX = { 5, 0x43, 0x81, 0xbf, 0xfd };
inline constexpr std::array<int, 4> kQuestTabXClassic = { 5, 0x52, 0x9f, 0xec };
// The log's client state (QuestLog.cpp's globals).
struct QuestLog {
    bool open = false;                       // UI 0xf
    int act = 0, slot = -1;                  // the tab (0x7c0255) and the quest picked (0x7bf2b9)
    int pressed = -1;                        // the icon held down (0x7bf2b5)
    std::array<int, 5> remembered{ -1, -1, -1, -1, -1 };   // the last picked, by tab (0x7bf2bd)
    std::array<int, 5> pending{ -1, -1, -1, -1, -1 };      // the Quest Log button's quest, by tab (0x7bf280)
    std::array<int, 41> last_state{};        // each quest's state last read (0x7bf380): a change is news
    // Each finished quest's done animation (frames 1..24, 100 ms each,
    // cursor_questdone at the first), once a game: its end, or the log
    // shutting on it, sets the client's copy of the quest's bit 12
    // (0x4a3943, FUN_004a2760), which the server never saves.
    std::array<int, 41> frame{};
    std::array<std::uint32_t, 41> frame_ms{};
    std::array<bool, 41> seen{};
    bool close_down = false, last_down = false;   // 0x7bf2b2, 0x7bf2b4
    // The Quest Log button (UI 0x11) that a quest's news brings up while the
    // log is shut (FUN_004a2cb0): on, held (0x7bf2b0), and its quest waiting
    // for the log to open (0x7bf298 1).
    bool button = false, button_down = false, notified = false;
    std::array<std::uint8_t, 7> sent{};      // the log states last told, to spot a new one (S->C 0x5d)
    bool sent_known = false;
};

// What the log says about a quest (d2d::rules::quest_text, FUN_004a1950).
using d2d::rules::QuestState;
using d2d::rules::QuestText;
using d2d::rules::quest_text;
// FUN_004a1950 as the log runs it: the text, and the quest's state kept
// (0x7bf380); `fresh` (+0x263) when that state is new.
QuestText quest_log_read(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, int quest, const QuestState& quest_state, bool& fresh);
// Opening (FUN_004a3fe0: FUN_004a2300, then FUN_004a3220(act, 0)): the tab
// of the player's act, as far as it's open; the animations start over.
void quest_log_open(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state, int player_act);
// Shutting (FUN_004a28d0 → FUN_004a2760): the tab's done animations count
// as seen.
void quest_log_close(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state);
// A tab clicked (FUN_004a3e40): another act, falling back to an open one.
void quest_log_tab(QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits, const QuestState& quest_state, int act, bool expansion);
// A quest's log state was sent (S->C 0x5d with no flags, FUN_004a2cb0): the
// log shut brings up the Quest Log button for it, the log open picks it if
// it's in the tab.
void quest_log_notify(QuestLog& quest_log, int quest);
// Its buttons on the bottom line (FUN_004a34f0): close (the store buttons'
// frames 10 / 11) at x 0x116 and questlast (replay the quest's message) at
// 0xe2, their bottoms 58 above the panel's; hit 0x116..0x139 by
// 0x188..0x1a9 (FUN_004a2690) and 0xe6..0x103 by 0x187..0x1a7
// (FUN_004a2610). 0: none, 1 close, 2 questlast. Hover texts "Close"
// (4144) centred at 0x128 and "Speech" (3720) at 0xf1, bottom 0x5f above
// the panel's.
inline int quest_button_at(int mouse_x, int mouse_y) {
    const int panel_x = mouse_x - kCharPanelX, panel_y = mouse_y - kCharPanelY;
    if (panel_x >= 0x116 && panel_x < 0x116 + 0x24 && panel_y >= 0x188 && panel_y < 0x188 + 0x22) return 1;
    if (panel_x >= 0xe6 && panel_x < 0xe6 + 0x1e && panel_y >= 0x187 && panel_y < 0x187 + 0x21) return 2;
    return 0;
}
// The tab under the cursor (FUN_004a26f0): the top 0x1c rows, 0x40 a tab
// (0x50 in the classic game).
inline int quest_tab_at(int mouse_x, int mouse_y, bool expansion) {
    const int panel_x = mouse_x - kCharPanelX, panel_y = mouse_y - kCharPanelY;
    if (panel_y < 0 || panel_y >= 0x1c || panel_x < 0 || panel_x >= 0x140) return -1;
    return panel_x / (expansion ? 0x40 : 0x50);
}
inline int quest_slot_at(int mouse_x, int mouse_y) {
    const int panel_x = mouse_x - kCharPanelX, panel_y = mouse_y - kCharPanelY;
    for (int slot = 0; slot < 6; ++slot) {
        const auto& box = kQuestSlot[std::size_t(slot)];
        if (panel_x >= box.hit_x && panel_x < box.hit_x + 0x38 && panel_y >= box.hit_y && panel_y < box.hit_y + 0x34) return slot;
    }
    return -1;
}
// Returns true when a done animation starts (the caller plays
// cursor_questdone, Sounds.txt 14).
bool draw_quest_log(std::vector<std::uint8_t>& framebuffer, const Scene& scene, QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits,
                    const QuestState& quest_state, std::uint32_t now_ms, bool expansion, int mouse_x, int mouse_y);
// The Quest Log button (UI 0x11, FUN_004a2a80): levelsocket and the level
// button as New Stats', its label "Quest Log" (3928) centred over it. Its
// rows (0x7241c0, 20 bytes: x0, x1, y0, y1, the label's bottom; FUN_004a2900
// picks): x 40, or W/2+40 with only the left panel open; y H-195..H-160
// (label H-198), lower at H-140..H-105 (label H-143) with the character
// panel open. Not drawn with both panels open or the log up. Hit strictly
// inside (FUN_004a2970); pressed (cursor_button_click) and let go over it
// (FUN_004a4110) it goes and the log opens on its quest.
struct QuestLogButton { int x0 = -1, x1 = 0, y0 = 0, y1 = 0, label = 0; };
QuestLogButton quest_log_button(const QuestLog& quest_log, bool left_open, bool right_open, bool char_open);
inline bool over_quest_log_button(const QuestLogButton& button, int mouse_x, int mouse_y) {
    return button.x0 >= 0 && mouse_x > button.x0 && mouse_x < button.x1 && mouse_y > button.y0 && mouse_y < button.y1;
}
void draw_quest_log_button(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const QuestLogButton& button, bool held, int mouse_x, int mouse_y);

}  // namespace d2d::client
