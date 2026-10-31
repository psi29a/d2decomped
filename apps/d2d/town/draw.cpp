// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): drawing: menus, the view, chat, hints and portraits. The rest is in this
// directory.
#include "town.hpp"

#include "common.hpp"
#include "controls.hpp"
#include "gamepad_map.hpp"
#include "ingame.hpp"
#include "internal.hpp"
#include "items.hpp"
#include "keyconfig.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "platform.hpp"
#include "roster.hpp"
#include "scene.hpp"
#include "skilltree.hpp"
#include "store.hpp"
#include "tile_blit.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <monster_ids.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <shake.hpp>
#include <userdir.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

auto Town::open_menu(int npc) -> void {
        const auto& npc_info = level->npcs[std::size_t(npc)];
        const auto& state = view.npc_states[std::size_t(npc)];
        const float dx = (npc_info.path.empty() ? npc_info.x : state.x) - view.player.x, dy = (npc_info.path.empty() ? npc_info.y : state.y) - view.player.y;
        npc_menu = open_npc_menu(*scene, *level, npc,
            int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))),
            kViewY + int(std::lround((dx + dy) * (kIsoH / 2))),
            int(character.stats.get(d2d::d2s::kLevel)), d2d::rules::unidentified(character.items), [&] {
                const int difficulty = character.header.active_difficulty();
                const auto& quest_bits = character.header.quests[std::size_t(std::clamp(difficulty, 0, 2))];
                return !d2d::rules::quest::bit(quest_bits, d2d::rules::kRespecQuest, 0) && (d2d::rules::quest::bit(quest_bits, d2d::rules::kRespecQuest, 1) || difficulty == 2);
            }(), character.header.quest_flag(character.header.active_difficulty(),   // table 0x7253e0: Warriv by quest 6, Meshif by quest 14 (bit 0)
                                             npc_info.hc_idx == d2d::rules::monster_ids::kMeshif ? d2d::rules::SevenTombsQuest::kQuest : d2d::rules::AndyQuest::kQuest, 0),
            character.header.quest_flag(character.header.active_difficulty(), npc_info.hc_idx == d2d::rules::SiegeQuest::kLarzuk ? d2d::rules::SiegeQuest::kQuest
                                        : npc_info.hc_idx == d2d::rules::BetrayalQuest::kAnya ? d2d::rules::BetrayalQuest::kQuest : d2d::rules::ToolsQuest::kQuest, 1),
            character.header.merc_seed != 0 && character.header.merc_dead
                ? resurrect_line(*scene, character.header.merc_type, character.header.merc_name,
                                 d2d::rules::merc_resurrect_cost(d2d::rules::merc_stats(scene->rules, character.header.merc_type, character.header.merc_exp).level))
                : std::string());
    }

// Widescreen: the world draws into `framebuffer` (wider than kScreenWidth)
// and everything else into its centre 800x600, as it always did.
auto Town::draw(std::vector<std::uint8_t>& framebuffer, const Mouse& mouse, std::uint32_t frame_ms) -> void {
    const int screen_w = fb_width(framebuffer);
    const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open || party.open;
    if (screen_w == int(kScreenWidth)) {
        draw_view(framebuffer, nullptr, mouse, frame_ms);
        draw_coop_portraits(framebuffer, left_open);
        return;
    }
    const int off = (screen_w - int(kScreenWidth)) / 2;
    ui_buffer.resize(std::size_t(kScreenWidth) * kScreenHeight * 4);
    draw_view(ui_buffer, &framebuffer, mouse, frame_ms);
    for (std::size_t y = 0; y < kScreenHeight; ++y)
        std::copy_n(ui_buffer.begin() + std::ptrdiff_t(y * kScreenWidth * 4), kScreenWidth * 4,
                    framebuffer.begin() + std::ptrdiff_t((y * std::size_t(screen_w) + std::size_t(off)) * 4));
    draw_coop_portraits(framebuffer, left_open);       // on the whole frame: widescreen's spare width
}

auto Town::draw_view(std::vector<std::uint8_t>& framebuffer, std::vector<std::uint8_t>* wide, const Mouse& mouse, std::uint32_t frame_ms) -> void {
        auto& unit = view.player;                           // the client's copy: its animation clock is the client's
        const int pmode = view.pmode;
        const bool run_on = view.running && character.stats.values[d2d::d2s::kStamina] > 0;   // no run at stamina 0 (FUN_0057f090)
        if (const bool running = unit.walking && run_on; pmode < 0 && (unit.walking != player_walked || running != player_ran)) {
            player_walked = unit.walking; player_ran = running; walk_ms = frame_ms;
        }
        if (pmode < 0) unit.mode_ms = std::max(unit.mode_ms, walk_ms);     // a swing's end restarts it too
        const int ui_cls = kSaveClassToUi[std::max(character.character_class, 0)];
        // Monsters in view, as units the world draws by depth.
        std::vector<Unit> extra;
        // The camera (and the player's unit) between the World's last two
        // ticks; a jump (a warp, devctl) snaps.
        // ponytail: the other units move at the tick rate, as game.exe draws them.
        const float blend = std::clamp(float(frame_ms - world_ms) / float(kTickMs), 0.f, 1.f);
        const bool jump = std::hypot(unit.x - prev_x, unit.y - prev_y) > 2.f;
        const float hero_x = jump ? unit.x : prev_x + (unit.x - prev_x) * blend, hero_y = jump ? unit.y : prev_y + (unit.y - prev_y) * blend;
        cam_x = hero_x; cam_y = hero_y;
        if (!coop.empty()) {
            // Local co-op: the camera eases to the party's middle (one screen,
            // World::leash keeps it there); a jump (a level, a warp) snaps.
            float sum_x = hero_x, sum_y = hero_y, count = 1;
            for (const auto& other : view.others)
                if (std::ranges::contains(coop, int(other.id), &CoopPlayer::id)) { sum_x += other.unit.x; sum_y += other.unit.y; ++count; }
            const float want_x = sum_x / count, want_y = sum_y / count;
            const float ease = std::clamp(float(frame_ms - party_ms) / 120.f, 0.f, 1.f);
            party_ms = frame_ms;
            const bool party_jump = jump || std::hypot(want_x - party_x, want_y - party_y) > 8.f;
            party_x = party_jump ? want_x : party_x + (want_x - party_x) * ease;
            party_y = party_jump ? want_y : party_y + (want_y - party_y) * ease;
            cam_x = party_x; cam_y = party_y;
        }
        // Inverse isometric projection: shift the camera, keeping every
        // world unit (including the local player) at its actual position.
        cam_x += float(shake.offset.first) / kIsoW + float(shake.offset.second) / kIsoH;
        cam_y += float(shake.offset.second) / kIsoH - float(shake.offset.first) / kIsoW;
        const std::pair<float, float> hero_at{ hero_x, hero_y };
        state_clock.now = frame_ms;
        view_units(*scene, view, cam_x, cam_y, &merc_label, extra, den_beams, frame_ms, &character.name, std::max(character.character_class, 0), &state_clock);
        for (const auto& critter : critters)                      // the client's critters: walking or standing, never hovered
            if (std::size_t(critter.row) < world.game_data->mon_npc.size() && std::abs(critter.x - cam_x) < 14 && std::abs(critter.y - cam_y) < 14) {
                const auto& anim = scene->npc_anim(world.game_data->mon_npc[std::size_t(critter.row)], critter.walking ? std::string_view("WL") : std::string_view("NU"));
                if (anim.length_ms() > 0) extra.push_back({ critter.x, critter.y, &anim, critter.dir, nullptr, critter.mode_ms, -3 });
            }
        // The host's overhead messages (0x26 type 5): a shrine's comes as
        // its string id in decimal ("3684", ShrMsg1). Each stays (length x 8
        // + 125) frames (FUN_00661110). An object's goes on d2d's object
        // nearest the host's; a player's or monster's on theirs.
        // ponytail: text that isn't a string id isn't shown.
        std::vector<UnitState> said_npcs;                    // the NPC states drawn, with the host's objects' messages: a copy (the View's are replicated by change)
        if (net_game) {
            for (const auto& said : std::exchange(net_game->overheads, {})) {
                const auto number = std::strtoul(said.text.c_str(), nullptr, 10);
                if (said.text.empty() || said.text.find_first_not_of("0123456789") != std::string::npos || number == 0 || number > 0xffff) continue;
                net_said[std::uint64_t(said.type) << 32 | said.id] = { std::uint16_t(number), frame_ms + std::uint32_t(said.text.size() * 8 + 125) * kTickMs };
            }
            std::erase_if(net_said, [&](const auto& entry) { return frame_ms >= entry.second.until; });
            for (const auto& [unit_key, said] : net_said) {
                const auto host = net_game->units.find(unit_key);
                if (host == net_game->units.end()) continue;
                if (host->second.type == 2) {
                    int nearest = -1;
                    float best = 4.f;
                    for (std::size_t i = 0; i < level->npcs.size() && i < view.npc_states.size(); ++i)
                        if (level->npcs[i].root == "objects")
                            if (const float apart = std::hypot((level->npcs[i].x + float(level->world_x)) * 5.f - host->second.x, (level->npcs[i].y + float(level->world_y)) * 5.f - host->second.y); apart < best) { best = apart; nearest = int(i); }
                    if (nearest < 0) continue;
                    if (said_npcs.empty()) said_npcs = view.npc_states;
                    said_npcs[std::size_t(nearest)].says = said.string;
                    continue;
                }
                for (auto& drawn : extra) {
                    const bool is_player = drawn.npc <= -4000 && std::size_t(-4000 - drawn.npc) < view.others.size() && view.others[std::size_t(-4000 - drawn.npc)].id == host->second.id && host->second.type == 0;
                    const bool is_monster = drawn.npc <= -10 && drawn.npc > -4000 && std::size_t(-10 - drawn.npc) < view.monsters.size() && std::uint32_t(view.monsters[std::size_t(-10 - drawn.npc)].id) == host->second.id && host->second.type == 1;
                    if (is_player || is_monster) drawn.says = said.string;
                }
            }
        }
        std::erase_if(state_clock.seen, [&](const auto& entry) { return frame_ms - entry.second.last > 5000; });
        if (alert_level != level) { alert_clocks.clear(); alert_level = level; }
        alert_clocks.resize(view.npc_states.size());
        for (std::size_t i = 0; i < view.npc_states.size(); ++i) {
            alert_clocks[i].set(view.npc_states[i].alert, frame_ms);
            if (net_game && view.npc_states[i].alert) {
                const auto* host_npc = net_npc(int(i));
                const auto shown = host_npc ? net_alert_ms.find(host_npc->id) : net_alert_ms.end();
                if (shown != net_alert_ms.end()) alert_clocks[i].start = frame_ms - (world_ms - shown->second);
            }
        }
        Unit player_look{};
        auto look_states = player_states(*scene, view);
        if (net_game)
            for (const int row : net_game->self_states) if (std::size_t(row) < scene->state_names.size()) look_states.emplace_back(scene->state_names[std::size_t(row)]);
        player_look.x = view.player.x; player_look.y = view.player.y;
        dress(*scene, player_look, -1, look_states, &state_clock);
        // A state gone plays its offsound once, where it was; one leaving the
        // view with its unit (the in_view box, 14 cells) hasn't ended.
        for (auto& [key, seen] : state_clock.seen)
            if (seen.last != frame_ms && !seen.off) {
                seen.off = true;
                if (key.second->off_sound > 0 && std::abs(seen.x - cam_x) < 13 && std::abs(seen.y - cam_y) < 13) state_clock.sounds.push_back({ key.second->off_sound, seen.x, seen.y });
            }
        for (const auto& sound : state_clock.sounds) cues.cue(sound.sound, frame_ms, sound.x, sound.y);
        state_clock.sounds.clear();
        const bool town = d2d::rules::level_ids::is_town(level->id);                       // TN/TW in town, NU/WL outside
        // A dead player has no DD composite: DT held on its last frame.
        const auto cls = std::max(character.character_class, 0);
        if (pmode == kModeDD && !view.ghost) unit.mode_ms = frame_ms - (scene->composite(cls, kModeDT, view.gfx).length_ms() - 1);
        int mode = view.ghost ? (unit.walking ? (town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU)   // a ghost walks along, its body lies apart
                 : pmode == kModeDD ? kModeDT : pmode >= 0 ? pmode : unit.walking ? (run_on ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        std::uint32_t mode_ms = unit.mode_ms;
        float rate = pmode >= 0 && pmode != kModeDD ? view.prate : 1.f;
        // A walk, run or town walk plays at the walk's rate, not AnimData's (FUN_00623f50).
        if (pmode < 0 && !view.ghost && (mode == kModeWL || mode == kModeRN || mode == kModeTW))
            if (const auto& walk_anim = scene->composite(cls, mode, view.gfx); walk_anim.speed || walk_anim.cof_speed)
                rate = float(d2d::rules::moving_anim_rate(mode == kModeRN, view.walk_percent)) / float(walk_anim.speed ? walk_anim.speed : walk_anim.cof_speed);
        if (!view.seq.empty() && attack_mode(pmode)) { std::tie(mode, mode_ms) = view_seq(*scene, int(cls), view, frame_ms); rate = 1.f; }   // an SQ skill's frame
        const auto light = frame_light(*scene, view, cam_x, cam_y, den_beams, den_ambient(), extra, &player_look, frame_ms, &hero_at);
        // A pad in the field shows no cursor: it hovers what A would act on
        // instead (its name, and a monster's life bar), a spot up its body.
        // ponytail: a spot, not its box: something drawn in front can take the hover.
        int hover_x = held ? -1 : mouse.x, hover_y = held ? -1 : mouse.y;
        if (pad_cue) {
            hover_x = hover_y = -1;
            if (const auto pick = pad_pick(pad, focused().player, true); pick.kind != PadPick::kNone && focus_id() == hero.id) {
                const float dx = pick.x - cam_x, dy = pick.y - cam_y;
                hover_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
                hover_y = kViewY + int(std::lround((dx + dy) * (kIsoH / 2))) - (pick.kind == PadPick::kItem ? 4 : 24);
            }
        }
        automap_frame();
        render_ingame(framebuffer, *scene, *view.level, ui_cls,
                      view.gfx,
                      character.name, character.hardcore,
                      cam_x, cam_y, mode,
                      unit.dir, frame_ms, hover_x, hover_y, said_npcs.empty() ? std::span<const UnitState>(view.npc_states) : std::span<const UnitState>(said_npcs),
                      inv_open ? &character.items : nullptr,
                      char_open ? &character.stats : nullptr, &character.stats, &character.panel, mode_ms, &character.items,
                      &hovered_npc, stash_open || cube_open ? &character.items : nullptr, character.expansion, belt_open,
                      cube_open ? (transmute_down ? 2 : 1) : 0, &npc_menu, &speech, &automap, &store, stat_pressed,
                      nullptr, nullptr, nullptr, extra, rate, light.pal ? &light : nullptr, &rain,
                      !(pmode == kModeDD && !view.corpses.empty()),    // dead, the corpse lies there instead
                      &player_look, alt_held || function_held(keys::kShowItems),   // the key config's Show Items (Alt)
                      Hud{ view.poisoned, view.running, run_down, character.header.weapon_set != 0, coop_hud_hidden(),
                          char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open, inv_open || tree_open, view.stamina_blue }, wide, { hero_x - cam_x, hero_y - cam_y }, view.ghost, alert_clocks);
        for (const auto& body : view.bodies)                       // local co-op: a body being raised
            if (body.raisers > 0 || body.raised > 0) {
                const float dx = body.x - cam_x, dy = body.y - cam_y;
                draw_raise_aura(framebuffer, int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2))), kViewY + int(std::lround((dx + dy) * (kIsoH / 2))), body.raised, frame_ms);
            }
        if (scene->store_buttons.frames_per_direction() > 11) {     // the close buttons, over their panels
            const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
            const auto& lay = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
            if (inv_open) blit_at_anchor(framebuffer, scene->store_buttons.frame(0, inv_close_down ? 11 : 10), pal, lay.panel_x + 0x12, lay.panel_y + 416);
            if (char_open) blit_at_anchor(framebuffer, scene->store_buttons.frame(0, char_close_down ? 11 : 10), pal, kCharPanelX + 0x80, kCharPanelY + 420);
        }
        // A level's "entering" art for 2 s after a change, none for the game's
        // first (FUN_00455510): its frames side by side, centred at W/2 - 1,
        // bottom at H/2 - 140, in text colour 1, red (FUN_00502680's shift); the Den's / the Tower's quest art while that
        // quest's open in this game (FUN_004b92e0: neither done nor closed).
        if (level->id != entry_level) { entry_until = entry_level != 0 ? frame_ms + 2000 : 0; entry_level = level->id; }
        if (frame_ms < entry_until) {
            static constexpr std::array<std::pair<int, int>, 2> kQuestArt{ { { 8, 1 }, { 20, 5 } } };   // level, quest
            std::string path = scene->level_entry.contains(level->id) ? scene->level_entry.at(level->id) : std::string();
            for (const auto& [quest_level, quest] : kQuestArt)
                if (level->id == quest_level && scene->level_entry_quest.contains(quest_level)
                    && !(view.game_quests[std::size_t(quest)] >> 13 & 1) && !(view.game_quests[std::size_t(quest)] >> 15 & 1))
                    path = scene->level_entry_quest.at(quest_level);
            if (const auto art = scene->entry_art.find(path); art != scene->entry_art.end()) {
                const auto frames = art->second.frames_per_direction();
                int width = 0;
                for (std::uint32_t frame = 0; frame < frames; ++frame) width += int(art->second.frame(0, frame).width);
                const auto& pal = !scene->act_text_red.entries().empty() ? scene->act_text_red : scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
                for (std::uint32_t frame = 0; frame < frames; ++frame)
                    blit_at_anchor(framebuffer, art->second.frame(0, frame), pal, fb_width(framebuffer) / 2 - 1 - width / 2 + int(frame) * 0x100, int(kScreenHeight) / 2 - 0x8c);
            }
        }
        view_overlays(framebuffer, *scene, view, hovered_monster());
        if (!coop_hud_hidden() || skillbar.picking) skillbar.draw(framebuffer, held ? -1 : mouse.x, held ? -1 : mouse.y, tip_env());
        if (pad_cue && focus_id() == hero.id) draw_pad_hint(framebuffer);
        if (net_game && net_game->trade.state != 0) {
            std::vector<d2d::d2s::Item> theirs;
            for (const auto& [id, stocked] : net_game->store_items) {
                theirs.push_back(stocked);
                theirs.back().location = d2d::d2s::item_location::kStored;
                theirs.back().panel = 100;
            }
            const auto& trade = net_game->trade;
            std::string with = trade.with_name;
            if (with.empty())
                if (const auto partner = net_game->units.find(trade.with); partner != net_game->units.end()) with = partner->second.name;
            draw_trade(framebuffer, *scene, trade.state, with.empty() ? std::string("Another player") : with, character.name, theirs, trade.ours, trade.their_gold,
                       trade.our_gold, nullptr, held ? -1 : mouse.x, held ? -1 : mouse.y, nullptr);
        }
        draw_portraits(framebuffer, char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open);
        if (gold_box) draw_gold_box(framebuffer, *scene, *gold_box, frame_ms);
        draw_chat(framebuffer, char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open, inv_open || tree_open);
        if (quest_log_was_open && !quest_log.open) {    // shut by any means (FUN_00455ae0 → FUN_004a28d0)
            quest_log.open = true;
            quest_log_close(quest_log, quest_bits(), quest_state());
        }
        quest_log_was_open = quest_log.open;
        if (quest_log.open && draw_quest_log(framebuffer, *scene, quest_log, quest_bits(), quest_state(), frame_ms, character.expansion, mouse.x, mouse.y))
            questdone_sound = true;                    // cursor_questdone
        if (tree_open)
            draw_skill_tree(framebuffer, *scene, int(kUiToSaveClass[ui_cls]), tree_tab, character.stats.skills, character.stats, tip_env(),
                            skill_pressed, held ? -1 : mouse.x, held ? -1 : mouse.y);
        if (waypoint.open)
            draw_waypoints(framebuffer, *scene, waypoint, character.header, character.expansion, level->id, mouse.x, mouse.y);
        draw_level_buttons(framebuffer, *scene, level_buttons_now(), mouse.x, mouse.y);   // after the panels, as the frame's draw
        draw_quest_log_button(framebuffer, *scene, quest_log_button_now(), quest_log.button_down, mouse.x, mouse.y);
        const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
        if (!coop_hud_hidden()) mini.draw(framebuffer, *scene, left_open, inv_open || tree_open, mouse.x, mouse.y);
        if (game_menu.open) game_menu.draw(framebuffer, *scene);
        if (party.open && (net_game || coop_hud_hidden() || !party_preview.empty())) party.draw(framebuffer, *scene, party_rows(), party_own(), mouse.x, mouse.y);
        if (hireling.open && view.merc_sheet)
            hireling.draw(framebuffer, *scene, *view.merc_sheet, merc_label, character.merc_items, held, mouse.x, mouse.y);
        if (help.open) help.draw(framebuffer, *scene, key_table);
        if (controls.open) controls.draw(framebuffer, *scene, now_ms);
        // A joining pad's pick: down the left, clear of the action in the middle and of the portraits' column.
        const int spare = wide ? (fb_width(*wide) - int(kScreenWidth)) / 2 : 0;
        const int roster_x = std::max(15, kCoopPortraitX + kCoopPortraitWidth + 8 - spare);
        int roster_top = 0x60;
        for (const auto& coop_player : coop)
            if (coop_player.roster) roster_top += draw_roster_at(framebuffer, *scene, *coop_player.roster, roster_x, roster_top, coop_player.colour) + 4;
    }

// Chat text broken at spaces into lines no wider than `width` (a longer
// word by characters).
// ponytail: FUN_00502970 (the edit box's wrap) taken to break as the chat
// lines' does.
auto Town::wrap_chat(const std::string& text, int width) const -> std::vector<std::string> {
    const auto& font = scene->font_chat.line_height() > 0 ? scene->font_chat : scene->font;
    std::vector<std::string> wrapped{ std::string{} };
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = std::min(text.find(' ', start), text.size());
        const auto word = text.substr(start, end - start);
        const auto joined = wrapped.back().empty() ? word : wrapped.back() + " " + word;
        if (!wrapped.back().empty() && font.measure(joined) > width) wrapped.push_back(word);
        else wrapped.back() = joined;
        while (wrapped.back().size() > 1 && font.measure(wrapped.back()) > width) {   // a word wider than the box: cut by characters
            auto& line = wrapped.back();
            std::size_t fits = 1;
            while (fits + 1 < line.size() && font.measure(line.substr(0, fits + 1)) <= width) ++fits;
            std::string rest = line.substr(fits);
            line.resize(fits);
            wrapped.push_back(std::move(rest));
        }
        start = end + 1;
    }
    return wrapped;
}

// A chat line (FUN_0049e3a0): it stays 10 s, wrapped to the screen's width
// - 70 (at most 6 lines); past 18 lines on screen the oldest message goes.
auto Town::add_chat(const std::string& text, int colour, std::size_t gold) -> void {
    auto wrapped = wrap_chat(text, int(kScreenWidth) - 70);
    wrapped.resize(std::min<std::size_t>(wrapped.size(), 6));
    for (auto& line : wrapped) {
        chat_lines.push_back({ std::move(line), now_ms + 10000, colour, gold });
        gold = 0;                                               // the name is on the first line
    }
    if (chat_lines.size() > 18) chat_lines.erase(chat_lines.begin(), chat_lines.end() - 18);   // ponytail: by line, not by message
}

// The chat lines (FUN_0049dc40): FontInGameChat (0xd), at x 15 (the
// right half's when only a left panel is open, FUN_0045ae90 2 (?)),
// baselines 20 + 15 a line, each on a dark box from x - 5, 14 up,
// 16 high, the text's width + 10 (FUN_0046efd0 colour 0, mode 1).
// The edit box (FUN_0047b720, UI\chat.cpp): a dark box at x 0x7f, y H - 0x67,
// W - 0xff wide, 0x2f high; the typed text wrapped to W - 0x109, white, at
// x 0x83 on baselines H - 0x58 + 15 a line, the first 3 lines only; the
// caret, string 0xd4c "_", after the last line drawn, on 500 ms and off
// 100 (FUN_0047b450). Colours 6 / 7 (black, tan) are drawn white.
auto Town::draw_chat(std::vector<std::uint8_t>& framebuffer, bool left_open, bool right_open) const -> void {
    if (chat_lines.empty() && !chat_typing) return;
    const auto& font = scene->font_chat.line_height() > 0 ? scene->font_chat : scene->font;
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const int x = left_open && !right_open ? int(kScreenWidth) / 2 + 15 : 15;
    int baseline = portraits_shown ? 0x5f : 20;   // ponytail: taken as below the portraits (UI flag 0x13 (?))
    static constexpr std::array<std::array<std::uint8_t, 3>, 10> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 },
                                                                          { 105, 105, 105 }, { 255, 255, 255 }, { 255, 255, 255 }, { 255, 168, 0 }, { 255, 255, 100 } } };
    auto darken = [&](int left, int top, int width, int height) {   // FUN_0046efd0 colour 0, mode 1
        for (int y = std::max(top, 0); y < std::min(top + height, int(kScreenHeight)); ++y)
            for (int column = std::max(left, 0); column < std::min(left + width, int(kScreenWidth)); ++column)
                for (int channel = 0; channel < 3; ++channel) {
                    auto& value = framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(column)) * 4 + std::size_t(channel)];
                    value = std::uint8_t(value / 2);
                }
    };
    auto line = [&](const std::string& text, int colour, std::size_t gold) {
        darken(x - 5, baseline - 14, font.measure(text) + 10, 16);
        const auto& rgb = kRgb[std::size_t(colour >= 0 && colour < 10 ? colour : 0)];
        const std::string name = text.substr(0, gold), rest = text.substr(name.size());
        const int top = baseline - font.line_height() + 1;
        if (!name.empty()) font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, top, name, 199, 179, 119);
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + font.measure(name), top, rest, rgb[0], rgb[1], rgb[2]);
        baseline += 15;
    };
    for (const auto& shown : chat_lines) line(shown.text, shown.colour, shown.gold);
    if (!chat_typing) return;
    constexpr int kWidth = int(kScreenWidth), kHeight = int(kScreenHeight);
    darken(0x7f, kHeight - 0x67, kWidth - 0xff, 0x2f);
    const auto lines = chat_typing->empty() ? std::vector<std::string>{ std::string{} } : wrap_chat(*chat_typing, kWidth - 0x109);
    const auto shown = std::min<std::size_t>(lines.size(), 3);
    for (std::size_t i = 0; i < shown; ++i)
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 0x83, kHeight - 0x58 + 15 * int(i) - font.line_height() + 1, lines[i], 255, 255, 255);
    if (now_ms % 600 < 500)   // ponytail: blinks on the clock, not from when typing began
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 0x83 + font.measure(lines[shown - 1]), kHeight - 0x58 + 15 * int(shown - 1) - font.line_height() + 1, "_", 255, 255, 255);
}

// The field's pad prompt over the control panel: the button A is bound to,
// and what it does to the thing it hovers. A d2d extra: game.exe has no pad.
auto Town::draw_pad_hint(std::vector<std::uint8_t>& framebuffer) const -> void {
    const auto pick = pad_pick(pad, view.player, true);
    if (pick.kind == PadPick::kNone) return;
    // A town portal (-2000 - which) is used, a corpse (-3000 - which) picked up, as an object / ground item.
    const bool portal = pick.kind == PadPick::kThing && pick.id <= -2000 && pick.id > -3000;
    const bool corpse = pick.kind == PadPick::kThing && pick.id <= -3000 && pick.id > -4000;
    const bool object = pick.kind == PadPick::kThing && pick.id >= 0 && level && std::size_t(pick.id) < level->npcs.size() && level->npcs[std::size_t(pick.id)].root == "objects";
    const std::string_view verb = pick.kind == PadPick::kMonster ? "Attack" : pick.kind == PadPick::kItem || corpse ? "Pick up" : object || portal ? "Use" : "Talk";
    const auto& primary = pad_config.bindings[pad::Action::Primary];   // its first button bound
    const auto button = primary[0] != pad::Bindings::kNone ? primary[0] : primary[1];
    if (button == pad::Bindings::kNone) return;
    const std::string text = std::string(pad::label(pad_family, button)) + "  " + std::string(verb);
    const auto& font = scene->font_chat.line_height() > 0 ? scene->font_chat : scene->font;
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, (int(kScreenWidth) - font.measure(text)) / 2, int(kScreenHeight) - 0x70, text, 199, 179, 119);
}

// An event message's line (FUN_0049eb10, by +1; the strings by id), or
// "" for one d2d doesn't show. The party's (7, FUN_0049e8f0) name the
// player of +3; a death (6) names the killer by +7: 0 a player (the
// second name), 1 a monster (MonStats NameStr of +3; a SuperUnique's,
// +0x18 its row, when set), 2 an object (an Exploding Barrel, 0xb).
// ponytail: 0xb / 0xf / 0x11 / 0x12 (realm news, SOJ, Diablo Walks the
// Earth) aren't shown; nor an object's own name in a death.
auto Town::event_text(const NetGame::Event& event) const -> std::string {
    auto text = [&](std::uint16_t id) { return string_id(*scene, id); };
    auto format = [](std::string pattern, std::initializer_list<std::string> values) {
        for (const auto& value : values)
            if (const auto found = pattern.find("%s"); found != std::string::npos) pattern.replace(found, 2, value);
        return pattern;
    };
    switch (event.event) {
    case 0: return event.name + text(0xe37);                   // " dropped due to timeout."
    case 1: return event.name + text(0xe38);
    case 2:                                                    // joined (not us)
        if (event.name == character.name) return {};
        return event.second.empty() ? format(text(0xe39), { event.name }) : format(text(0xe3b), { event.name, event.second });
    case 3: return event.second.empty() ? format(text(0xe3a), { event.name }) : format(text(0xe3c), { event.name, event.second });
    case 4: return event.name + text(0xe3d);                   // " is not in the game."
    case 5: return event.name + text(0xe3e);
    case 6: {
        std::string killer;
        std::uint16_t slain_by = 0xe3f;                              // " was slain by "
        if (event.sub == 0 && !event.second.empty()) { killer = event.second; slain_by = 0xe40; }
        else if (event.sub == 1 && event.second.empty() && event.argument < scene->monsters.types.size()) {
            const auto& key = scene->monsters.types[event.argument].name_key;
            const auto found = lookup_string(*scene, key);
            killer = found ? u16_to_latin1(*found) : key;
        } else if (event.sub == 1 && !event.second.empty() && event.second_id < scene->superuniques.size()) {
            killer = scene->superuniques[event.second_id].name;
            slain_by = 0xe40;
        } else if (event.sub == 2 && event.argument == 0xb) killer = text(0xc98);
        return killer.empty() ? event.name + text(0xe41) : event.name + text(slain_by) + killer;
    }
    case 7: {
        std::string who;
        if (net_game)
            if (const auto found = net_game->units.find(std::uint64_t(event.argument)); found != net_game->units.end()) who = found->second.name;
        static constexpr std::array<std::uint16_t, 11> kParty{ 0x277a, 0x277b, 0xfba, 0xfbb, 0xfbc, 0xfbd, 0xfbe, 0xfbf, 0xfc0, 0x277c, 0x277d };
        if (event.sub < 1 || event.sub > 11 || who.empty()) return {};
        const auto line = text(kParty[std::size_t(event.sub - 1)]);
        if (event.sub <= 2 || event.sub >= 10) return format(line, { who });   // "%s permits you to loot his corpse."
        return event.sub == 8 ? line + who : who + line;      // "You are now allied with " / " has left your party."
    }
    case 8: return event.name + text(0xfcc);                   // " is busy"
    case 9: return text(0xfcf);
    case 0xa: return event.name.empty() ? text(0x1027) : event.name + text(0x1026);
    case 0xd: return event.name + text(0xe44);
    case 0x10: return text(0x2a38);
    default: return {};
    }
}

namespace {

// A portrait's bar: `percent` of `width` filled `rgb`, the rest black.
void fill_bar(std::vector<std::uint8_t>& framebuffer, int x, int top, int width, int height, int percent, const std::array<std::uint8_t, 3>& rgb) {
    const int frame_w = fb_width(framebuffer), filled = x + std::clamp(percent, 0, 100) * width / 100;
    for (int y = std::max(top, 0); y < std::min(top + height, int(kScreenHeight)); ++y)
        for (int column = std::max(x, 0); column < std::min(x + width, frame_w); ++column) {
            auto* pixel = &framebuffer[(std::size_t(y) * std::size_t(frame_w) + std::size_t(column)) * 4];
            for (std::size_t channel = 0; channel < 3; ++channel) pixel[channel] = column < filled ? rgb[channel] : 0;
        }
}

// FUN_00493a00's: green, from under 50 % yellow, under 25 % red.
std::array<std::uint8_t, 3> life_colour(int life) {
    return life < 25 ? std::array<std::uint8_t, 3>{ 0xff, 0, 0 } : life < 50 ? std::array<std::uint8_t, 3>{ 0xc0, 0xc0, 0 } : std::array<std::uint8_t, 3>{ 0, 0x80, 0 };
}

}  // namespace

// The portraits (FUN_00494020): each other player in our party
// (FUN_00493b50), the merc (FUN_00493ce0), then the pets by PetType.txt row
// (icon type 1 FUN_00493e10, 2 FUN_00493eb0, 3 FUN_00493f80), left to right
// from x 15, 0x38 apart: the icon with its bottom at y 0x3c (a player's
// Hireables\<Class>Icon; a merc's / pet's the PetType micon of its MonStats
// class, else baseicon, FUN_00493240), a life bar above it (FUN_00493a00:
// x .. x + 0x2e, y 0xe .. 0x13, filled life % of the way, green (0, 0x80,
// 0) / from under 50 % (0xc0, 0xc0, 0) / under 25 % red, the rest black;
// icon type 2 only for one with drawhp), icon type 2's count in Font6 at
// (x + 8, baseline 0x37), and the name (the player's, the merc's, the
// PetType name) in Font6, cut to 0x42 px, centred on x + 0x16, its baseline
// at y 0x48 and 0x52 by turns (FUN_00492fa0). None while a left panel is
// open (FUN_0045ae90 2 / 3 (?)).
// The bars are mode 5 (as is); Z (Show Portraits, DAT_007beecc) hides them.
// The first portraits of an expansion game, the hint armed (FUN_004939b0):
// the Hireling screen opens once (UI 0x24; with no merc it shuts again, the
// hint spent all the same) and d2d.cfg's popup_hireling (game.exe: the
// registry's PopupHireling) is 1.
// A joined game's summons are the host's: its pet list (0x7a), ours by owner.
// ponytail: their life is the unit's (0x0c hits), not the list's +0x1c,
// whose sender isn't traced.
auto Town::draw_portraits(std::vector<std::uint8_t>& framebuffer, bool left_open) -> void {
    portraits_shown = false;
    merc_portrait_x = -1;                                             // none to hover or drop on until drawn
    if (left_open || portraits_hidden || coop_hud_hidden()) { merc_hovered = false; merc_hover_at = 0; return; }   // local co-op's: draw_coop_portraits
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto& font = scene->font_tiny.line_height() > 0 ? scene->font_tiny : scene->font;
    int x = 15, name_y = 0x48;
    // Each icon at draw mode 1 (FUN_004f6480: half over what's there); the
    // merc's at mode 7 while hovered (its table untraced, lighting.md: drawn as is).
    const auto portrait = [&](const d2d::dc6::Sprite* icon, int life, std::string name, int count, bool lit = false) {
        if (icon && icon->total_frames() > 0) {
            const auto& frame = icon->frame(0, 0);
            const int top = 0x3c - int(frame.height) + 1, frame_w = fb_width(framebuffer);
            for (std::uint32_t row = 0; row < frame.height; ++row)
                for (std::uint32_t column = 0; column < frame.width; ++column) {
                    const int pixel_x = x + int(column), pixel_y = top + int(row);
                    const auto index = frame.pixels[row * frame.width + column];
                    if (index == 0 || pixel_x < 0 || pixel_x >= frame_w || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                    const auto colour = pal[index];
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(frame_w) + std::size_t(pixel_x)) * 4];
                    pixel[0] = lit ? colour.r : std::uint8_t((pixel[0] + colour.r) / 2);
                    pixel[1] = lit ? colour.g : std::uint8_t((pixel[1] + colour.g) / 2);
                    pixel[2] = lit ? colour.b : std::uint8_t((pixel[2] + colour.b) / 2);
                }
        }
        if (life >= 0) fill_bar(framebuffer, x, 0xe, 0x2e, 5, std::clamp(life, 0, 100), life_colour(std::clamp(life, 0, 100)));
        if (count > 0) {
            auto text = std::to_string(count);
            while (!text.empty() && font.measure(text) > 0x2e) text.pop_back();
            font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, x + 8, 0x37 - font.line_height() + 1, text);
        }
        while (!name.empty() && font.measure(name) > 0x42) name.pop_back();
        font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, std::max(x + 0x16 - font.measure(name) / 2, 0), name_y - font.line_height() + 1, name);
        name_y = name_y == 0x48 ? 0x52 : 0x48;
        x += 0x38;
        portraits_shown = true;
    };
    if (net_game)
        if (const auto self = net_game->units.find(std::uint64_t(net_game->self_id)); self != net_game->units.end() && self->second.party != 0xffff)
            for (const auto& [unit_key, unit] : net_game->units) {
                if (unit.type != 0 || unit.id == net_game->self_id || unit.party != self->second.party || unit.cls < 0 || unit.cls > 6) continue;
                portrait(&scene->class_icons[std::size_t(unit.cls)], unit.party_life, unit.name, 0);
            }
    const auto& game_data = *world.game_data;
    const auto icon_of = [&](const GameData::PetType& type, int monster_class) {
        for (std::size_t i = 0; i < type.class_icons.size(); ++i)
            if (type.class_icons[i].first == monster_class && !type.class_icons[i].second.empty()) return scene->hireable_icon(type.class_icons[i].second);
        return scene->hireable_icon(type.base_icon);
    };
    const auto gone = [](std::string_view mode) { return mode == "DT" || mode == "DD"; };
    if (view.merc && !gone(view.merc->mode))
        if (const auto* type = game_data.pet_type("hireable")) {
            merc_portrait_x = x; merc_portrait_y = name_y;
            portrait(icon_of(*type, view.merc_sheet ? view.merc_sheet->cls : -1),
                     view.merc_sheet ? int(std::int64_t(view.merc_sheet->life) * 100 / std::max(view.merc_sheet->max_life, 1)) : -1, merc_label, 0, merc_hovered);
        }
    for (const auto& type : game_data.pet_types) {
        if (type.icon_type < 1 || type.icon_type > 3) continue;
        const View::Pet* first = nullptr;
        int count = 0;
        if (!net_game)                                                // a joined game's: the host's list only
            for (const auto& pet : view.pets)
                if (pet.pettype == type.type && !gone(pet.mode)) { if (!first) first = &pet; ++count; }
        int first_class = first ? first->npc.hc_idx : -1, first_life = first ? first->life : -1;
        if (net_game) {
            const auto row = int(&type - game_data.pet_types.data());
            for (const auto& pet : net_game->pets) {
                if (pet.owner != net_game->self_id || pet.pettype != row) continue;
                const auto unit = net_game->units.find(std::uint64_t(1) << 32 | pet.id);
                if (count++ == 0) { first_class = pet.cls; first_life = unit == net_game->units.end() ? 100 : unit->second.life * 100 / 128; }
            }
        }
        if (count == 0) continue;
        auto name = lookup_string(*scene, std::string_view(type.name));
        portrait(icon_of(type, first_class), type.icon_type != 2 || (count == 1 && type.draw_life) ? first_life : -1,
                 name ? u16_to_latin1(*name) : std::string{}, type.icon_type == 2 ? count : 0);
    }
    if (hireling_hint && character.expansion && !hireling.open) {
        hireling_hint = false;
        hireling.open = true;
        if (!cfg_file.empty() && !d2d::userdir::save_cfg(cfg_file, { { "popup_hireling", "1" } })) d2d::log::warn("d2d.cfg: popup_hireling not saved");
    }
    // The merc's hint (FUN_00494020's tail): 0x58a3 "Right-click to Open
    // Inventory (%s)" with the Hireling key (FUN_0046a530: action 0x36), 0xf9e
    // "\n", 0x58a2 "Drop Potion on Portrait to Heal"; FUN_00502280 at
    // (x, y + 0x24), last line on top.
    if (merc_hovered && merc_portrait_x >= 0) {
        auto key = keys::key_of(key_table, keys::kHireling, 1);
        if (key == keys::kNone) key = keys::key_of(key_table, keys::kHireling, 0);
        const auto id = keys::name_id(key);
        const std::string key_name = key == keys::kNone ? std::string{} : id ? string_id(*scene, id) : std::string(1, char(key & 0xff));
        auto open = string_id(*scene, 0x58a3);
        if (const auto spot = open.find("%s"); spot != std::string::npos) open.replace(spot, 2, key_name);
        draw_hover_text(framebuffer, *scene, { { string_id(*scene, 0x58a2), kTxtWhite }, { std::move(open), kTxtWhite } }, merc_portrait_x, merc_portrait_x,
                        merc_portrait_y + 0x24, merc_portrait_y + 0x24);
    }
}

// Local co-op's portraits (d2d's own): every player, the first first, one
// under the other down the frame's left edge (widescreen's spare width when
// there is some, clear of the 800x600 UI), each a life and a mana bar, the
// class icon and the name in the seat's colour. Room to their right for
// more. Under a left panel (no spare width) they don't show.
auto Town::draw_coop_portraits(std::vector<std::uint8_t>& framebuffer, bool left_open) -> void {
    if (!coop_hud_hidden() || portraits_hidden) return;
    const int frame_w = fb_width(framebuffer), spare = (frame_w - int(kScreenWidth)) / 2;
    if (left_open && spare < kCoopPortraitX + kCoopPortraitWidth) return;
    using namespace d2d::d2s;
    const auto percent = [](const Stats& stats, int now, int most) {
        return int(std::clamp<std::int64_t>(stats.fixed(now) * 100 / std::max<std::int64_t>(stats.fixed(most), 1), 0, 100));
    };
    const auto& pal = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto& font = scene->font_tiny.line_height() > 0 ? scene->font_tiny : scene->font;
    constexpr int kBarWidth = 0x2e;
    const int x = kCoopPortraitX, bar_x = x + (kCoopPortraitWidth - kBarWidth) / 2;
    int top = 8;
    for (const auto& member : world.players) {
        const int cls = int(member->character.header.cls);
        if (cls < 0 || cls > 6) continue;
        const auto& stats = member->character.stats;
        const int life = percent(stats, kLife, kMaxLife);
        fill_bar(framebuffer, bar_x, top, kBarWidth, 5, life, life_colour(life));
        fill_bar(framebuffer, bar_x, top + 6, kBarWidth, 3, percent(stats, kMana, kMaxMana), { 0x20, 0x40, 0xe0 });
        int below = top + 12;
        if (const auto& icon = scene->class_icons[std::size_t(cls)]; icon.total_frames() > 0) {
            const auto& frame = icon.frame(0, 0);
            blit_sprite(framebuffer, frame, pal, x + (kCoopPortraitWidth - int(frame.width)) / 2, below);
            below += int(frame.height);
        }
        const auto seat = std::ranges::find(coop, member->id, &CoopPlayer::id);
        const auto& colour = kSeatColours[std::size_t(std::clamp(seat == coop.end() ? lead_colour : seat->colour, 0, 3))];
        std::string name = member->character.name;
        while (!name.empty() && font.measure(name) > kCoopPortraitWidth) name.pop_back();
        font.draw_tinted(framebuffer, std::uint32_t(frame_w), kScreenHeight, pal, x + (kCoopPortraitWidth - font.measure(name)) / 2, below + 2, name, colour[0], colour[1], colour[2]);
        top = below + 2 + font.line_height() + 10;
    }
}

}  // namespace d2d::client
