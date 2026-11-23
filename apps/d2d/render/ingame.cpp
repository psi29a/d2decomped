// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for render/ingame.hpp: the in-game frame and the rain.
#include "render/ingame.hpp"

#include <overlays.hpp>

#include "common.hpp"
#include "panels/panels.hpp"
#include "panels/store.hpp"
#include "render/overhead.hpp"
#include "render/tile_blit.hpp"
#include "render/world_view.hpp"
#include "scene.hpp"
#include "touch/input.hpp"
#include "ui.hpp"
#include "watchdog.hpp"

#include <blend.hpp>
#include <d2s_items.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <panel_view.hpp>
#include <rules.hpp>
#include <show_items.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {

void draw_rain(std::vector<std::uint8_t>& framebuffer, const d2d::rules::Rain& rain, const d2d::palette::Palette& pal, int panel_side) {
    // FUN_00473470: drops and flakes whose spot (or a drop's end) lies in
    // the view less the bottom 47 pixels, the half a side panel leaves
    // (FUN_0045ae90: 1 right open, 2 left), drawn as the software modes'
    // line (FUN_006c8c80: solid, a palette index, the alpha unused).
    const int screen_w = fb_width(framebuffer), screen_h = int(framebuffer.size() / 4) / std::max(screen_w, 1);
    const int bottom = int(kScreenHeight) - 47;
    const int left = panel_side == 2 ? screen_w / 4 * 2 : 0, right = panel_side == 1 ? screen_w - screen_w / 4 * 2 : screen_w;
    const auto inside = [&](int x, int y) { return x >= left && x < right && y >= 0 && y < bottom; };
    if (rain.flashing) {                                  // the lightning (FUN_004f6300: colour 0xff, mode 5, solid)
        const auto white = pal[0xff];
        for (int y = 0; y < std::min(bottom, screen_h); ++y)
            for (int x = std::max(left, 0); x < std::min(right, screen_w); ++x) {
                auto* pixel = framebuffer.data() + (std::size_t(y) * std::size_t(screen_w) + std::size_t(x)) * 4;
                pixel[0] = white.r; pixel[1] = white.g; pixel[2] = white.b;
            }
        return;
    }
    const auto plot = [&](int x, int y, std::uint8_t index) {
        if (x < 0 || y < 0 || x >= screen_w || y >= screen_h) return;
        const auto colour = pal[index];
        auto* pixel = framebuffer.data() + (std::size_t(y) * std::size_t(screen_w) + std::size_t(x)) * 4;
        pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b;
    };
    const auto line = [&](int x, int y, int to_x, int to_y, std::uint8_t index) {   // FUN_006c8c80
        int run_x = to_x - x, run_y = to_y - y;
        const int step_x = run_x < 0 ? -1 : 1, step_y = run_y < 0 ? -1 : 1;
        run_x = std::abs(run_x); run_y = std::abs(run_y);
        plot(x, y, index);
        int error = 0;
        if (run_x < run_y) {
            for (int left_steps = run_y; left_steps > 0; --left_steps) {
                y += step_y; error += run_x;
                if (run_y < error) { x += step_x; error -= run_y; }
                plot(x, y, index);
            }
        } else {
            for (int left_steps = run_x; left_steps > 0; --left_steps) {
                x += step_x; error += run_y;
                if (run_x < error) { y += step_y; error -= run_x; }
                plot(x, y, index);
            }
        }
    };
    for (const auto& drop : rain.drops) {
        if (rain.snow) {
            if (!inside(drop.x, drop.y) || drop.size < 0 || drop.size > 7) continue;
            const auto index = rain.index[3][std::size_t(d2d::rules::flake_tone(drop.kind, drop.size))];
            for (const auto& [from_x, from_y, to_x, to_y] : { d2d::rules::kFlake[std::size_t(drop.size)], d2d::rules::kFlake[std::size_t(drop.size) + 1] })
                line(drop.x + from_x, drop.y + from_y, drop.x + to_x, drop.y + to_y, index);
            continue;
        }
        const auto index = rain.index[std::size_t(drop.kind == 0 ? 0 : drop.kind == 2 ? 2 : 1)][std::size_t(drop.tone)];
        if (drop.landed) { if (inside(drop.x, drop.y)) line(drop.x, drop.y, drop.x, drop.y, index); continue; }
        int dx = int(d2d::rules::cos512(rain.wind) * float(drop.len));
        int dy = int(d2d::rules::sin512(rain.wind) * float(drop.len));
        if (const int room = drop.bottom - drop.y; room < dy) { dx = room * dx / dy; dy = room; }
        if (inside(drop.x, drop.y) || inside(drop.x + dx, drop.y + dy)) line(drop.x, drop.y, drop.x + dx, drop.y + dy, index);
    }
}

void render_ingame(std::vector<std::uint8_t>& framebuffer,
                   const Scene& scene,
                   const Level& level,
                   int class_idx,
                   const Scene::Appearance& gfx,
                   std::string_view name,
                   [[maybe_unused]] bool hardcore,   // game.exe draws no caption in game
                   float cam_x,
                   float cam_y,
                   int player_mode,
                   int player_dir,
                   std::uint32_t elapsed_ms,
                   int mouse_x , int mouse_y ,
                   std::span<const UnitState> npcs ,
                   const std::vector<d2d::d2s::Item>* inventory ,
                   const d2d::d2s::Stats* char_stats ,
                   const d2d::d2s::Stats* hud_stats ,
                   const PanelStats* panel ,
                   std::uint32_t player_mode_ms ,
                   const std::vector<d2d::d2s::Item>* belt ,
                   int* hovered_npc ,
                   const std::vector<d2d::d2s::Item>* stash , bool stash_expansion ,
                   bool belt_popup , int cube_open ,
                   const NpcMenuState* npc_menu , const Speech* speech ,
                   const Automap* automap , const Store* store ,
                   int stat_pressed ,
                   const Npc* merc , const UnitState* merc_state ,
                   const std::string* merc_label ,
                   std::span<const Unit> extra_units , float player_rate ,
                   const Lighting* light , d2d::rules::Rain* rain , bool player_visible ,
                   const Unit* player_look ,     // its states' colour shift and overlays
                   bool show_items,               // Alt held: every ground item's name
                   const Hud& hud,
                   std::vector<std::uint8_t>* wide, std::pair<float, float> player_offset, bool player_spirit,
                   std::span<const d2d::rules::OverlayClock> alert_clocks,
                   std::span<const ObjectFlash> object_flashes) {
    // Widescreen: the world (and its labels) draw on `wide`; its centre
    // 800x600 is then copied into `framebuffer`, where the HUD and panels draw
    // as at 800x600. Mouse x is relative to that centre, so the world's is
    // shifted by `off`.
    auto& world = wide ? *wide : framebuffer;
    const int world_w = fb_width(world), off = (world_w - int(kScreenWidth)) / 2;
    const int world_mouse_x = mouse_y < 0 ? -1 : mouse_x + off;
    // Prefer the real tile-composited world when townE1.ds1 loaded; fall
    // back to the credits DC6 placeholder when it didn't (headless CI, a
    // stripped MPQ dir, etc.). Palette follows the render path: ACT1 for
    // the tiles, Sky for the credits DC6 which was authored against it.
    if (!level.dt1s.empty()) {
        set_phase(MainPhase::IngameClear);
        // Clear to black — tiles don't cover every subtile so an
        // uninitialized fb would leak the previous frame's contents.
        std::fill(world.begin(), world.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < world.size(); i += 4) world[i] = 0xFF;
        set_phase(MainPhase::IngameFloor);   // render_world does floor+shadow+walls internally
        // Player: the camera follows them, so their feet sit on the camera
        // point (kW/2, kViewY), off it by `player_offset` in local co-op
        // (the camera on the party's middle); render_world slots them into the
        // wall pass by depth. Wears the loaded save's gear, or the
        // class's starting gear.
        std::vector<Unit> units;
        units.reserve(level.npcs.size() + 1);
        if (player_visible && class_idx >= 0 && class_idx < 7)
            units.push_back({ cam_x + player_offset.first, cam_y + player_offset.second, &scene.composite(kUiToSaveClass[class_idx], player_mode, gfx),
                              player_dir, nullptr, player_mode_ms });
        if (!units.empty()) {
            units.back().rate = player_rate;
            units.back().overlay_class = 1;                 // FUN_006223a0: a player's Height2
            if (player_look) { units.back().shift = player_look->shift; units.back().overs = player_look->overs; units.back().says = player_look->says; units.back().said = player_look->said; }
            if (player_spirit) { units.back().spirit = true; units.back().shadow = false; }
        }
        // An object in a mode its CycleAnim doesn't loop (a chest's OP) stops on the last frame (FUN_004bcbb0).
        auto holds = [](const Npc& npc, const UnitState* state) {
            const auto mode = state && !state->mode.empty() ? std::string_view(state->mode) : std::string_view(npc.mode);
            const int index = game::mode_index(mode);
            return npc.root == "objects" && index >= 0 && !(npc.cycle >> index & 1);
        };
        // An object's length in its mode is its FrameCnt from Start at FrameDelta, a loop going back to Start (FUN_004bcbb0).
        auto frames_of = [](const Npc& npc, const UnitState* state, Unit& unit) {
            const auto mode = state && !state->mode.empty() ? std::string_view(state->mode) : std::string_view(npc.mode);
            const int index = game::mode_index(mode);
            if (npc.root != "objects" || index < 0) return;
            unit.frames = npc.frames[std::size_t(index)];
            unit.loop_from = npc.loop_from[std::size_t(index)];
            unit.frame_delta = std::uint16_t(game::object_frame_delta(npc, std::size_t(index)));
        };
        // NPCs and objects, at their live position when they patrol.
        for (std::size_t i = 0; i < level.npcs.size(); ++i) {
            const auto& npc = level.npcs[i];
            const UnitState* state = i < npcs.size() ? &npcs[i] : nullptr;
            if (state && state->hidden) continue;
            const float x = state ? state->x : npc.x, y = state ? state->y : npc.y;
            if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
            const auto& anim = scene.npc_anim(npc, state && state->walking ? std::string_view("WL") : state && !state->mode.empty() ? state->mode : std::string_view(npc.mode));
            static const std::string none;
            // An object's name shows in the modes it's Selectable in: its start mode, or the one it's in now (an open door).
            const std::string_view object_mode = state && !state->mode.empty() ? std::string_view(state->mode) : std::string_view(npc.mode);
            const bool unselectable = npc.root == "objects" && !(npc.selectable >> game::mode_index(object_mode) & 1);
            units.push_back({ x, y, &anim, state ? state->dir : 0, unselectable ? &none : &npc.name, state ? state->mode_ms : 0, int(i) });
            if (state && state->alert) {
                units.back().overlay = &scene.npc_alert;
                if (i < alert_clocks.size()) units.back().overlay_start = alert_clocks[i].start;
            }
            if (state) units.back().says = state->says;
            units.back().object = npc.root == "objects";
            units.back().hold = holds(npc, state);
            frames_of(npc, state, units.back());
            units.back().name_offset = npc.name_offset;
            for (const auto& object_flash : object_flashes)   // played once from its start (FUN_00470390 kind 2)
                if (object_flash.npc == int(i) && object_flash.row >= 0 && std::size_t(object_flash.row) < scene.overlay_rows.size() && scene.overlay_rows[std::size_t(object_flash.row)])
                    units.back().overs.push_back({ scene.overlay_rows[std::size_t(object_flash.row)], object_flash.start, true });
        }
        // The neighbour levels' objects and NPCs (torches by the camp's
        // gate, Flavie, patrolling rogues), where they are now: D2 draws the
        // rooms round the player whichever level they're in. Their states
        // follow this level's in `npcs` (World::View::npc_states). Not
        // clickable from here (npc -3); quest-gated ones (Cain) left out.
        static const std::string no_name;
        std::size_t next_state = level.npcs.size();
        for (const auto& neighbour : level.nearby)
            for (const auto& npc : neighbour.level->npcs) {
                const UnitState* state = next_state < npcs.size() ? &npcs[next_state] : nullptr;
                ++next_state;
                if (npc.quest || (state && state->hidden)) continue;
                const float x = (state ? state->x : npc.x) + float(neighbour.dx), y = (state ? state->y : npc.y) + float(neighbour.dy);
                if (std::abs(x - cam_x) >= 14 || std::abs(y - cam_y) >= 14) continue;
                const auto mode = state && state->walking ? std::string_view("WL") : state && !state->mode.empty() ? state->mode : std::string_view(npc.mode);
                units.push_back({ x, y, &scene.npc_anim(npc, mode), state ? state->dir : 0, &no_name, state ? state->mode_ms : 0, -3 });
                units.back().object = npc.root == "objects";
                units.back().hold = holds(npc, state);
                frames_of(npc, state, units.back());
            }
        if (merc && merc_state)                    // npc -2: not an NPC-menu unit
            units.push_back({ merc_state->x, merc_state->y,
                              &scene.npc_anim(*merc, merc_state->walking ? std::string_view("WL") : std::string_view("NU")),
                              merc_state->dir, merc_label, merc_state->mode_ms, -2 });
        units.insert(units.end(), extra_units.begin(), extra_units.end());
        if (hovered_npc && *hovered_npc != -1)            // last frame's: brighter (render_world)
            for (auto& unit : units) if (unit.npc == *hovered_npc && unit.name) unit.highlight = true;
        std::pair<const Unit*, std::array<int, 4>> hovered{ nullptr, {} };
        std::vector<std::pair<const Unit*, std::array<int, 2>>> items;   // each drawn ground item and where it stands on screen
        render_world(world, scene, level, cam_x, cam_y, elapsed_ms, units, world_mouse_x, mouse_y, &hovered, light, rain, show_items ? &items : nullptr);
        if (rain) draw_rain(world, *rain, scene.act_pal.entries().empty() ? scene.pal : scene.act_pal,
                            hud.right_open == hud.left_open ? 0 : hud.right_open ? 1 : 2);
        if (hovered_npc) *hovered_npc = hovered.first ? hovered.first->npc : -1;
        // What a unit says (a shrine's message): FUN_004a0b30 (players)
        // and FUN_004a0d20 (the rest) take its screen point (FUN_00620900:
        // the path's +8 / +0xc, FUN_00643290 of its 16.16 spot >> 11, so
        // (x - y) * 16, (x + y) * 8 a subtile) less the view's corner
        // 0x7bf214 (FUN_0044c990: the player's point less half the view
        // {0, 0, W, H - 40}): kFloorViewY, 12 over the feet d2d draws at
        // kViewY. That 0x1e / 10 up, then they drop
        // it with the point off screen (y outside 1..H-1, x outside
        // -99..W+99). FUN_004a0a00: the text (at most 199 characters)
        // wrapped to 0xb4 in FontInGameChat (0xd, FUN_00502970); a box the
        // widest line + 0x14 wide, 15 a line + 6 high, centred on x, its
        // top 0x32 + 15 a line + 3 over that. FUN_0049d9a0 drops a box
        // that starts under an open side panel (left x past W/2 - 10 with
        // the right one, under W/2 + 10 with the left, any with both);
        // FUN_0049d8e0 one not wholly on screen in y, then darkens (left,
        // top + 4, width, height - 5) (FUN_0046efd0 colour 0, mode 1) and
        // draws at most 10 lines white at left + 10, on baselines top +
        // 0x12 + 15 a line (the first lines in the last slots). Before
        // both, FUN_0049e070 moves a box clear of the frame's earlier ones
        // (place_overhead), or drops it.
        // FUN_004a0e70 starts the frame's slots with the NPC speech box while
        // one is up (0x7bf1c0 == 1): its x (0x7bf278, (W - 0x145) / 2), its
        // top (0x7225fc, 12), 0x145 by 0x70 (npc-talk.md "Box"), so no box
        // lands on it; then the players', the monsters' and the objects' tables.
        // ponytail: each table's own order isn't traced; within each d2d goes in `units` order.
        // The speech box in the world buffer's pixels (the screen's start at `off`); the touch layout's box
        // is where draw_speech paints it there (a d2d layout, no game.exe slot of its own).
        std::optional<OverheadRect> speech_box;
        if (speech && speech->npc >= 0) {
            OverheadRect box = speech_slot(int(kScreenWidth));
            if (hud.touch_compact) {
                const int box_h = touch_speech_box_height(int(speech->lines.size()), touch::safe_inset.top, touch::safe_inset.bottom);
                const int box_x = int(touch::safe_inset.left + (800.f - touch::safe_inset.left - touch::safe_inset.right - 0x145) * .5f);
                const int top = touch_speech_box_top(box_h, touch::safe_inset.top, touch::safe_inset.bottom);
                box = { box_x, top - 5, box_x + 0x145, top - 5 + box_h };
            }
            box.left += off; box.right += off;
            speech_box = box;
        }
        auto overhead_slots = overhead_frame_slots(speech_box);
        const auto speakers = overhead_order(std::span<const Unit>(units), [](const Unit& unit) { return unit.says || !unit.said.empty(); },
                                             [](const Unit& unit) { return unit.npc == -1 || unit.npc <= -4000; });
        const auto& chat_font = scene.font_chat.line_height() > 0 ? scene.font_chat : scene.font;
        const auto& says_pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
        for (const Unit* speaker : speakers) {
            const auto& unit = *speaker;
            const bool player = unit.npc == -1 || unit.npc <= -4000;
            const int point_x = world_w / 2 + int(std::lround(((unit.x - cam_x) - (unit.y - cam_y)) * (kIsoW / 2)));
            const int point_y = kFloorViewY + int(std::lround(((unit.x - cam_x) + (unit.y - cam_y)) * (kIsoH / 2))) - (player ? 0x1e : 10);
            if (point_y <= 0 || point_y >= int(kScreenHeight) || point_x <= -100 || point_x >= world_w + 100) continue;
            const auto lines = d2d::font::wrap_edit_box((unit.says ? string_id(scene, unit.says) : unit.said).substr(0, 199), 0xb4, [&chat_font](unsigned char code) {
                if (const auto* glyph = chat_font.find(code)) return int(glyph->width);
                return 0;
            });
            if (lines.empty()) continue;
            int widest = 0;
            for (const auto& line : lines) widest = std::max(widest, chat_font.measure(line));
            const int count = int(lines.size()), shown = std::min(count, 10);
            const int width = widest + 0x14, height = count * 15 + 6;
            const int box_left = point_x - width / 2, box_top = point_y - 0x32 - count * 15 - 3;
            const auto spot = place_overhead(overhead_slots, { box_left, box_top, box_left + width, box_top + height }, world_w, int(kScreenHeight));
            if (!spot) continue;
            const int left = spot->left, top = spot->top;
            if ((hud.left_open && hud.right_open) || (hud.right_open && left > world_w / 2 - 10) || (hud.left_open && left < world_w / 2 + 10)) continue;
            if (top < 0 || top + height >= int(kScreenHeight)) continue;
            for (int row = std::max(top + 4, 0); row < std::min(top + 4 + height - 5, int(kScreenHeight)); ++row)
                for (int column = std::max(left, 0); column < std::min(left + width, world_w); ++column)
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        auto& value = world[(std::size_t(row) * std::size_t(world_w) + std::size_t(column)) * 4 + channel];
                        value = std::uint8_t(value / 2);
                    }
            for (int line = 0; line < shown; ++line)
                chat_font.draw_tinted(world, std::uint32_t(world_w), kScreenHeight, says_pal, left + 10,
                                      glyph_top(chat_font, top + 0x12 + 15 * (line + count - shown)), lines[std::size_t(line)], 255, 255, 255);
        }
        // A box under a label (FUN_004f6300): `rgb` at draw mode 1 (half,
        // over black), 2 (a quarter of the colour kept) or 5 (solid).
        auto fill = [&](const std::array<int, 4>& rect, int mode, std::array<std::uint8_t, 3> rgb) {
            for (int y = std::max(rect[1], 0); y < std::min(rect[3], int(kScreenHeight)); ++y)
                for (int x = std::max(rect[0], 0); x < std::min(rect[2], world_w); ++x) {
                    auto* pixel = &world[(std::size_t(y) * std::size_t(world_w) + std::size_t(x)) * 4];
                    const d2d::palette::Rgba under{ pixel[0], pixel[1], pixel[2], 0xFF }, over{ rgb[0], rgb[1], rgb[2], 0xFF };
                    const auto mixed = mode == 5 ? over : mode == 1 ? d2d::palette::blend(d2d::palette::Blend::kAlpha50, under, over)
                                                                    : d2d::palette::blend(d2d::palette::Blend::kAlpha75, under, { 0, 0, 0, 0xFF });
                    pixel[0] = mixed.r; pixel[1] = mixed.g; pixel[2] = mixed.b;
                }
        };
        // Alt ("Show Items", UI\showitems.cpp FUN_004c0810): the drawn
        // ground items of the player's near rooms in their walk
        // (Unit::label_rank), each name over its item, moved clear of the
        // ones already placed (rules::show_items: at most 0x20). A label is
        // black at half (draw mode 1) in the item's colour; the first one
        // under the mouse, or naming the item the cursor is on, goes solid
        // (mode 5) in the palette's nearest to (0, 64, 128) (FUN_004fb180)
        // with white text, and a click picks that item up.
        // ponytail: a level's floor kept and come back to keeps d2d's order
        // (View::ground's); how game.exe's client gets those items again
        // (their order in their rooms) isn't traced.
        if (show_items) {
            const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
            std::array<std::uint8_t, 3> hover_blue{};
            const auto source = pal.entries();
            std::uint8_t found = 0;
            for (int best = 1 << 30, index = 0; index < 256 && std::size_t(index) < source.size(); ++index) {
                const auto& colour = source[std::size_t(index)];
                const int distance = colour.r * colour.r + (colour.g - 64) * (colour.g - 64) + (colour.b - 128) * (colour.b - 128);
                if (distance < best) { best = distance; found = std::uint8_t(index); }
            }
            const auto chosen = pal[found];
            hover_blue = { chosen.r, chosen.g, chosen.b };
            std::erase_if(items, [](const auto& entry) { return entry.first->label_rank < 0 || !entry.first->name || entry.first->name->empty(); });
            std::ranges::sort(items, {}, [](const auto& entry) { return entry.first->label_rank; });
            const int line_height = scene.font.line_height();
            std::vector<d2d::rules::LabelItem> walk;
            std::ptrdiff_t cursor_item = -1;
            for (const auto& [unit, feet] : items) {
                // Its spot: where the item stands, 16 up (rules::label_item); its height h as top = y - h + 4 has it.
                if (hovered.first == unit) cursor_item = std::ptrdiff_t(walk.size());
                walk.push_back(d2d::rules::label_item(feet[0], feet[1], scene.font.measure(*unit->name), line_height + 6));
            }
            // The columns: the screen, its left half less 0x20 with a right
            // panel open (FUN_0045ae90 1), its right half with a left one (2).
            const int open_side = hud.right_open == hud.left_open ? 0 : hud.right_open ? 1 : 2;
            bool label_hovered = false;
            for (const auto& label : d2d::rules::show_items(walk, world_w, int(kScreenHeight), open_side, world_mouse_x, mouse_y, cursor_item)) {
                const Unit* unit = items[label.item].first;
                const std::array<int, 4> rect{ label.rect.left, label.rect.top, label.rect.right, label.rect.bottom };
                fill(rect, label.lit ? 5 : 1, label.lit ? hover_blue : std::array<std::uint8_t, 3>{});
                const auto& colour = label.lit ? std::array<std::uint8_t, 3>{ 255, 255, 255 } : unit->rgb;
                scene.font.draw_tinted(world, std::uint32_t(world_w), kScreenHeight, pal, rect[0] + 4, rect[1] + 1, *unit->name, colour[0], colour[1], colour[2]);
                if (label.lit) {
                    label_hovered = true;
                    if (hovered_npc) *hovered_npc = unit->npc;
                }
            }
            if (label_hovered) hovered = { nullptr, {} };   // the label names it: no second name
        }
        // Name over whatever the cursor points at, centred above it, on a
        // box a quarter as bright (FUN_00503000: draw mode 2, text + 8 wide).
        if (hovered.first && (hovered.first->npc > -10 || hovered.first->npc <= -1000)) {   // monsters: their bar at the top
            const auto& hovered_name = *hovered.first->name;
            const auto& box  = hovered.second;
            const auto& colour  = hovered.first->rgb;
            const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
            const int width = scene.font.measure(hovered_name) + 8, top = box[1] - scene.font.line_height() - 2;
            fill({ (box[0] + box[2]) / 2 - width / 2, top, (box[0] + box[2]) / 2 - width / 2 + width, top + scene.font.line_height() + 2 }, 2, {});
            scene.font.draw_tinted(world, std::uint32_t(world_w), kScreenHeight, pal, (box[0] + box[2]) / 2 - scene.font.measure(hovered_name) / 2,
                               top, hovered_name, colour[0], colour[1], colour[2]);
        }
        const bool dim_automap = automap && !automap->covered && hud.touch_compact && automap->open;
        if (dim_automap && wide)
            for (std::size_t index = 0; index + 2 < world.size(); index += 4) {
                world[index] = std::uint8_t(world[index] * 3 / 4);
                world[index + 1] = std::uint8_t(world[index + 1] * 3 / 4);
                world[index + 2] = std::uint8_t(world[index + 2] * 3 / 4);
            }
        if (wide)
            for (std::size_t y = 0; y < kScreenHeight; ++y)
                std::copy_n(world.begin() + std::ptrdiff_t((y * std::size_t(world_w) + std::size_t(off)) * 4), kScreenWidth * 4,
                            framebuffer.begin() + std::ptrdiff_t(y * kScreenWidth * 4));
        // Who reads the hover text: red requirements, class lines, attack speed.
        std::optional<d2d::rules::Wearer> wearer_value;
        if (inventory && hud_stats && class_idx >= 0 && class_idx < 7) {
            wearer_value = d2d::game::wearer(scene, kUiToSaveClass[class_idx], *inventory, *hud_stats);
            wearer_value->holy_shield = hud.holy_shield;
        }
        const d2d::rules::Wearer* hover_wearer = wearer_value ? &*wearer_value : nullptr;
        // The automap, under the border and every panel, and not at all with
        // both sides covered (FUN_00456ee0: FUN_0045ad60 first, while
        // FUN_0045ae90 isn't 3). Round the camera less the panel's quarter, which
        // the map makes its own (rules::automap_scroll).
        if (automap && !automap->covered) {
            if (dim_automap && !wide) {
                for (std::size_t index = 0; index < framebuffer.size(); index += 4) {
                    framebuffer[index] = std::uint8_t(framebuffer[index] * 3 / 4);
                    framebuffer[index + 1] = std::uint8_t(framebuffer[index + 1] * 3 / 4);
                    framebuffer[index + 2] = std::uint8_t(framebuffer[index + 2] * 3 / 4);
                }
            }
            const float unshift = float(d2d::rules::panel_view_shift(automap->panel_side, world_w)) / kIsoW;
            draw_automap(framebuffer, scene, *automap, cam_x - unshift + float(level.world_x), cam_y + unshift + float(level.world_y));
        }
        // The 800x600 border round an open side panel, under the panels
        // (FUN_00499450: FUN_00498630 left, FUN_00498700 right; bottom-left
        // anchored at these spots).
        if (fb_width(framebuffer) == int(kScreenWidth) && scene.border_frame.frames_per_direction() >= 10) {
            static constexpr std::array<std::array<int, 2>, 10> kSpots{ { { 0, 0xfd }, { 0x100, 0x3f }, { 0, 0x1e4 }, { 0, 0x229 }, { 0x100, 0x229 },
                                                                          { 400, 0x3f }, { 0x220, 0xfd }, { 0x2c9, 0x1e4 }, { 0x220, 0x229 }, { 400, 0x229 } } };
            for (std::size_t frame = 0; frame < kSpots.size(); ++frame)
                if (frame < 5 ? hud.left_open : hud.right_open)
                    blit_at_anchor(framebuffer, scene.border_frame.frame(0, std::uint32_t(frame)), scene.act_pal.entries().empty() ? scene.pal : scene.act_pal, kSpots[frame][0], kSpots[frame][1]);
        }
        // With a store open, your items show what the vendor pays ("Sell value: ", 0xd03):
        // the inventory's and the belt's (FUN_0048dd90, FUN_00497a40).
        std::function<std::string(const d2d::d2s::Item&)> sell_price;
        if (store && store->npc >= 0)
            sell_price = [&](const d2d::d2s::Item& item) {
                return string_id(scene, 0xd03) + std::to_string(d2d::rules::item_price(scene.rules, item, store->npc_id, true, store->header));
            };
        auto draw_inventory_panel = [&] {
            if (inventory && class_idx >= 0 && class_idx < 7)
            {
                Scene::InvLayout layout = scene.inv_layout[std::size_t(kUiToSaveClass[class_idx])];
                if (hud.touch_inv_anchor) {
                    const int dx = touch::kTouchInvPanelX - layout.panel_x, dy = touch::kTouchInvPanelY - layout.panel_y;
                    layout.panel_x += dx;
                    layout.panel_y += dy;
                    layout.grid_x += dx;
                    layout.grid_y += dy;
                    for (auto& slot : layout.slots) {
                        slot[0] += dx;
                        slot[1] += dy;
                    }
                }
                draw_inventory(framebuffer, scene, layout, *inventory,
                               mouse_x, mouse_y, hover_wearer, &sell_price, hud.weapon_set_two, hud.header, hud.uninterruptable, hud.item_tip);
                if (hud_stats) draw_gold(framebuffer, scene, *hud_stats, false);
            }
        };
        auto draw_store_panel = [&] {
            if (store && store->npc >= 0)
            {
                const int store_x = hud.touch_store_right ? touch::kTouchStorePanelX : kCharPanelX;
                draw_store(framebuffer, scene, *store, mouse_x, mouse_y, hover_wearer, inventory, store_x);
                if (hud_stats) draw_gold(framebuffer, scene, *hud_stats, true);
            }
        };
        if (char_stats) {
            draw_char_panel(framebuffer, scene, *char_stats, panel ? *panel : PanelStats{}, name, class_idx, stat_pressed);
            draw_char_tooltips(framebuffer, scene, *char_stats, panel ? *panel : PanelStats{}, hud, mouse_x, mouse_y);
        }
        // The store draws its own hover text: the panel under the mouse goes last so
        // the other doesn't cover it (the store on the left, the inventory right).
        // The inventory's and storage's go to hud.item_tip when set, drawn after every panel.
        if (mouse_x < int(kScreenWidth) / 2) { draw_inventory_panel(); draw_store_panel(); }
        else                                 { draw_store_panel(); draw_inventory_panel(); }
        if (stash) {
            const int expansion_index = stash_expansion ? 1 : 0;
            if (cube_open)
                draw_storage(framebuffer, scene, *stash, scene.cube_panel, scene.cube_layout, d2d::d2s::item_panel::kCube, mouse_x, mouse_y,
                             hover_wearer, cube_open == 2, hud.item_tip);
            else
            {
                draw_storage(framebuffer, scene, *stash, scene.stash_panel[std::size_t(expansion_index)], scene.stash_layout[std::size_t(expansion_index)], d2d::d2s::item_panel::kStash,
                             mouse_x, mouse_y, hover_wearer, false, hud.item_tip);
                if (hud_stats) draw_stash_gold(framebuffer, scene, *hud_stats, mouse_x, mouse_y, stash_expansion);
            }
        }
        if (npc_menu) draw_npc_menu(framebuffer, scene, *npc_menu, elapsed_ms);
        if (speech) draw_speech(framebuffer, scene, *speech, elapsed_ms, hud.touch_compact);
        // FUN_00456ee0 calls FUN_0049ff10 and FUN_004a0e70 in stock's panel/text draw order;
        // d2d's typed item descriptions, speech and NPC menus are drawn by the surrounding frame code.
        if (hud_stats && !hud.hidden) draw_hud(framebuffer, scene, *hud_stats, hud, mouse_x, mouse_y);
        if (belt && !hud.hidden) draw_belt(framebuffer, scene, *belt, mouse_x, mouse_y, hover_wearer,
                            belt_popup, hud.belt_keys, hud.pad_bar, &sell_price);
        // Dev overlay: a red dot on every blocked subtile around the camera.
        if (g_debug_collision) {
            const int center_x = int(cam_x * 5), center_y = int(cam_y * 5);
            for (int screen_y = center_y - 60; screen_y <= center_y + 60; ++screen_y)
                for (int screen_x = center_x - 60; screen_x <= center_x + 60; ++screen_x) {
                    const float world_x = (float(screen_x) + 0.5f) / 5, world_y = (float(screen_y) + 0.5f) / 5;
                    if (!level.blocked(world_x, world_y)) continue;
                    const int pixel_x = int(kScreenWidth) / 2 + int(std::lround(((world_x - cam_x) - (world_y - cam_y)) * (kIsoW / 2)));
                    const int pixel_y = kViewY + int(std::lround(((world_x - cam_x) + (world_y - cam_y)) * (kIsoH / 2)));
                    for (int offset_y = -1; offset_y <= 1; ++offset_y)
                        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                            const int x = pixel_x + offset_x, y = pixel_y + offset_y;
                            if (x < 0 || y < 0 || x >= int(kScreenWidth) || y >= int(kScreenHeight)) continue;
                            auto* pixel = framebuffer.data() + (std::size_t(y) * kScreenWidth + std::size_t(x)) * 4;
                            pixel[0] = 255; pixel[1] = 0; pixel[2] = 0;
                        }
                }
        }
        set_phase(MainPhase::IngameHudText);
    } else {
        if (wide) {
            std::fill(world.begin(), world.end(), std::uint8_t{0});
            for (std::size_t i = 3; i < world.size(); i += 4) world[i] = 0xFF;
        }
        std::fill(framebuffer.begin(), framebuffer.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < framebuffer.size(); i += 4) framebuffer[i] = 0xFF;
        blit_dc6_grid(framebuffer, scene.credits_bg, scene.pal, 0, 0, scene.bg_tiles_across);
    }
}

}  // namespace d2d::client
