// In-game panels: inventory, character, HUD, stash/cube, belt, automap, waypoints.
#pragma once

#include "common.hpp"
#include "scene.hpp"

#include <d2s.hpp>
#include <d2s_items.hpp>
#include <dc6.hpp>
#include <quests.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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
                    const std::vector<d2d::d2s::Item>& items, int mouse_x = -1, int mouse_y = -1, int clvl = 1,
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
// The maxima include what's worn (Fight::item_max).
// ponytail: no poison tint, stamina bar, skill icons or run/walk yet.
void draw_hud(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const d2d::d2s::Stats& stats);

// The stash panel: art frames 0..3 as 2x2 at the left-panel spot, items
// (location 0, panel 5) in the inventory.txt bank grid.
// ponytail: no gold line, no "close" button; classic stash untested.
// Rect {x, y, w, h} of a stored item in a grid layout.
std::array<int, 4> grid_rect(const Scene& scene, const Scene::InvLayout& layout, const d2d::d2s::Item& item);

// A left-side storage panel: the stash (panel 5) or the cube (panel 4).
void draw_storage(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
                  const d2d::dc6::Sprite& art, const Scene::InvLayout& layout, int panel,
                  int mouse_x, int mouse_y, int clvl);

// The belt: items in location 2 keep their slot (0..15, 4 per row) in the
// column field and sit centred in the belt's belts.txt boxes. Row 1 is the
// HUD strip; with the popup open (0x499136) each further row gets a
// ctrlpnl_popbelt frame 0, bottom-anchored at x W/2+21, bottom H-41-32i,
// and its items. Hovering an item shows its hover text.
// ponytail: no slot hotkey numbers.
void draw_belt(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<d2d::d2s::Item>& items,
               int mouse_x, int mouse_y, int clvl, bool popup);

// The automap (UI\automap.cpp). Each revealed tile adds one cell
// (FUN_00457cf0): its cel from AutoMap.txt (FUN_0061fff0) at the tile's
// world pixel position ((x - y) * 80, (x + y) * 40) / 10, lower walls
// (orientation > 15) 24 further down. Drawn (FUN_00459700/FUN_00459440)
// at cell - scroll, scroll = player's world pixels / 10 - screen / 2 +
// (40, 15), with DC6's bottom-left anchoring.
// ponytail: reveals tiles within 12 of the player (D2 reveals by room),
// cel picked by a tile hash rather than the game's RNG, no fade near the
// centre, no player/NPC marks.
// One automap per Levels.txt Layer: the town and the act 1 wilderness
// share layer 0, so the map shows them together; cells sit in act
// coordinates.
struct Automap {
    struct Cell { int cel, x, y; };
    std::vector<Cell> cells;
    std::unordered_map<int, std::vector<std::uint8_t>> revealed;   // per level, per DS1 tile
    bool open = false;
};

int automap_cel(const Scene& scene, const Level& level, int orientation, int main, int sub, std::uint32_t hash);

void automap_reveal(const Scene& scene, const Level& level, Automap& automap, float player_x, float player_y);

void draw_automap(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Automap& automap, float player_x, float player_y);

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
// Slots' icons, bottom-left (0x723ea8); the act tabs' x (the expansion's);
// the name's baseline and the description's first, 20 apart, 270 wide
// (0x724210..0x724218).
inline constexpr std::array<std::pair<int, int>, 6> kQuestSlot = { { { 26, 121 }, { 123, 121 }, { 220, 121 }, { 26, 218 }, { 123, 218 }, { 220, 218 } } };
inline constexpr std::array<int, 6> kQuestTabX = { 5, 0x43, 0x81, 0xbf, 0xfd, 0x13b };
// The log's state per quest (client side): which is open, and each finished
// quest's done animation (frames 1..24, 100 ms each, cursor_questdone at
// the first) — shown once a game: its end sets the client's copy of the
// quest's bit 12 (0x4a3943), which the server never saves.
struct QuestLog {
    bool open = false; int act = 0, slot = -1;
    std::array<int, 41> frame{};
    std::array<std::uint32_t, 41> frame_ms{};
    std::array<bool, 41> seen{};
    bool close_down = false, last_down = false;
};

// What the log says about a quest (d2d::rules::quest_text, FUN_004a1950).
using d2d::rules::QuestState;
using d2d::rules::QuestText;
using d2d::rules::quest_text;
// An icon's frame by `shown` (FUN_004a34f0): 2 → 26 (not started), 3 → 0
// (25 while selected), 1 → 24 (done); 0 is the done animation, frames 1..24
// (100 ms each, cursor_questdone at the first), then 24.
// ponytail: the questdone plate a selected finished quest draws instead
// isn't here.
inline int quest_icon_frame(int shown, bool selected) {
    return shown == 2 ? 26 : shown == 3 ? (selected ? 25 : 0) : 24;
}
// Its buttons on the bottom line (FUN_004a34f0): close (the store buttons'
// frames 10 / 11) at x 0x116 and questlast (replay the quest's message) at
// 0xe2, their bottoms 58 above the screen's; hit boxes 0x24 x 0x22 and
// 0x1e x 0x21. 0: none, 1 close, 2 questlast.
inline int quest_button_at(int mouse_x, int mouse_y) {
    const int panel_x = mouse_x - kCharPanelX, panel_y = mouse_y - kCharPanelY;
    if (panel_x >= 0x116 && panel_x < 0x116 + 0x24 && panel_y >= 422 - 0x22 && panel_y < 422) return 1;
    if (panel_x >= 0xe6 && panel_x < 0xe6 + 0x1e && panel_y >= 422 - 0x21 && panel_y < 422) return 2;
    return 0;
}
inline int quest_tab_at(int mouse_x, int mouse_y) {
    if (mouse_y < kCharPanelY || mouse_y >= kCharPanelY + 33) return -1;
    for (int act = 4; act >= 0; --act) if (mouse_x >= kCharPanelX + kQuestTabX[std::size_t(act)] && mouse_x < kCharPanelX + kQuestTabX[std::size_t(act) + 1]) return act;
    return -1;
}
inline int quest_slot_at(const Scene& scene, int mouse_x, int mouse_y) {
    for (int k = 0; k < 6; ++k) {
        const auto [x, y] = kQuestSlot[std::size_t(k)];
        const int width = scene.quest_icons[0].frames_per_direction() ? int(scene.quest_icons[0].frame(0, 0).width) : 64;
        const int height = scene.quest_icons[0].frames_per_direction() ? int(scene.quest_icons[0].frame(0, 0).height) : 64;
        if (mouse_x >= kCharPanelX + x && mouse_x < kCharPanelX + x + width && mouse_y >= kCharPanelY + y - height && mouse_y < kCharPanelY + y) return k;
    }
    return -1;
}
// Returns true when a done animation starts (the caller plays
// cursor_questdone, Sounds.txt 14).
bool draw_quest_log(std::vector<std::uint8_t>& framebuffer, const Scene& scene, QuestLog& quest_log, const d2d::rules::QuestBits& quest_bits,
                    const QuestState& quest_state, std::uint32_t now_ms);

}  // namespace d2d::client
