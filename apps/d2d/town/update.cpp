// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): the frame's update. The rest is in this directory.
#include "town.hpp"

#include "audio.hpp"
#include "common.hpp"
#include "controls.hpp"
#include "cursor.hpp"
#include "gamepad_map.hpp"
#include "ingame.hpp"
#include "keyconfig.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include "skilltree.hpp"
#include "speech_sound.hpp"
#include "store.hpp"
#include "ui.hpp"

#include <cues.hpp>
#include <d2gs/c2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <light.hpp>
#include <log.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <screenshot.hpp>
#include <shake.hpp>
#include <sound_ids.hpp>
#include <userdir.hpp>
#include <weather.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

auto Town::update(std::vector<std::uint8_t>& framebuffer, const Mouse& frame_mouse, const std::vector<SDL_Keycode>& frame_keys,
                Screen& screen, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms) -> void {
        now_ms = frame_ms;
        skillbar.coop = world.players.size() > 1;           // Resurrect on the right-hand picker
        if (skillbar.right == kResurrectSkill && !skillbar.usable(kResurrectSkill, false)) skillbar.right = 0;   // co-op over: Attack
        skillbar.drop_gone();                                       // a charged entry used up or taken off
        // A partner's screen with nothing of theirs open goes back to the first player.
        if (focus_id() != hero.id && focus_keys.empty() && pad_field() && !game_menu.open && !automap.open && !waypoint.open) take_focus(hero.id);
        auto keys_this_frame = frame_keys;               // and a partner's that took the screen
        keys_this_frame.insert(keys_this_frame.end(), focus_keys.begin(), focus_keys.end());
        focus_keys.clear();
        Mouse mouse = frame_mouse;             // the menu / mini-panel take their clicks from the rest
        // The gold box takes the keys and clicks while it's up: digits and
        // Backspace edit the amount (at most the gold it's from), the spinner
        // steps it, OK or Enter does the box's mode (0x454080), Cancel closes.
        if (gold_box && !game_menu.open) {
            using namespace d2d::d2s;
            const std::int64_t most = std::max<std::int64_t>(character.stats.get(gold_box->mode == 4 ? kGoldBank : kGold), 0);
            const auto amount = [&] { return std::min<std::int64_t>(gold_box->amount.empty() ? 0 : std::stoll(gold_box->amount), most); };
            for (const char digit : typed) if (digit >= '0' && digit <= '9' && gold_box->amount.size() < 10) gold_box->amount.push_back(digit);
            bool ok = false, cancel = false;
            for (const auto key : keys_this_frame) {
                if (key == SDLK_BACKSPACE && !gold_box->amount.empty()) gold_box->amount.pop_back();
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) ok = true;
            }
            if (!gold_box->amount.empty()) gold_box->amount = std::to_string(amount());
            const auto part = gold_box_at(mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                gold_box->spin = part == GoldBoxPart::kUp ? 1 : part == GoldBoxPart::kDown ? -1 : 0;
                gold_box->spin_ms = frame_ms;
                if (gold_box->spin != 0) gold_box->amount = std::to_string(std::clamp<std::int64_t>(amount() + gold_box->spin, 0, most));   // a click: one step
                gold_box->next_ms = frame_ms;
                gold_box->ok_down = part == GoldBoxPart::kOk;
                gold_box->cancel_down = part == GoldBoxPart::kCancel;
            }
            if (!mouse.down) gold_box->spin = 0;
            // Held (FUN_004bbfb0 through the box's 0x453fe0): a step at most every
            // 70 ms, 1 for the first second, then the draws held >> 5, >> 4, >> 2
            // past 1, 2, 3 s, and (ms held >> 11) x the draws past 4096 ms.
            // ponytail: game.exe counts its own draws (+0x40); here 25 a second, its in-game rate.
            if (gold_box->spin != 0 && frame_ms - gold_box->next_ms >= 70) {
                const std::uint32_t held_ms = frame_ms - gold_box->spin_ms;
                const std::int64_t draws = held_ms / 40;
                const std::int64_t step = held_ms > 0x1000 ? std::int64_t(held_ms >> 11) * draws : held_ms > 3000 ? draws >> 2 : held_ms > 2000 ? draws >> 4 : held_ms > 1000 ? draws >> 5 : 1;
                gold_box->amount = std::to_string(std::clamp<std::int64_t>(amount() + gold_box->spin * step, 0, most));
                gold_box->next_ms = frame_ms;
            }
            if (mouse.release_this_frame) {
                ok = ok || (gold_box->ok_down && part == GoldBoxPart::kOk);
                cancel = gold_box->cancel_down && part == GoldBoxPart::kCancel;
                gold_box->ok_down = gold_box->cancel_down = false;
            }
            if (ok) {
                const std::int64_t gold = amount();
                const int mode = gold_box->mode;
                if (mode == 2) { if (net_game) net_game->trade_gold(std::uint32_t(gold)); }
                else if (gold > 0 && mode >= 3) {
                    if (net_game) net_game->send_items({ d2d::net::d2gs::c2s::button(mode == 3 ? 0x14 : 0x13, std::uint16_t(gold >> 16), std::uint16_t(gold)) });
                    else send(cmd::StashGold{ int(gold), mode == 3 });
                } else if (gold > 0) {
                    if (net_game) net_game->send_items({ d2d::net::d2gs::c2s::drop_gold(net_game->self_id, std::uint32_t(gold)) });
                    else send(cmd::DropGold{ int(gold) });
                }
            }
            if (ok || cancel) gold_box.reset();
            mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // The Party screen's clicks (FUN_0049bb60 / FUN_0049bdd0).
        if (party.open && (net_game || coop_hud_hidden()) && !help.open && !controls.open && !game_menu.open) {
            const auto click = party.input(mouse, party_rows(), party_own());
            if (net_game && click.kind == PartyPanel::Click::kButton) net_game->party_button(click.player);
            if (net_game && click.kind == PartyPanel::Click::kFlag) net_game->relation(click.player, click.flag);
            if (net_game && click.kind == PartyPanel::Click::kLeave) net_game->leave_party();
            if (mouse.x >= 80 && mouse.x < 400 && mouse.y >= 60 && mouse.y < 492) mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        if (party.open && !net_game && !coop_hud_hidden() && party_preview.empty()) party.open = false;
        // The merc's portrait (FUN_00493760 / FUN_004936e0 / FUN_004938e0): 700 ms
        // on it (x .. x + 0x2e, y 0x18 .. 0x3c) with nothing in hand lights it
        // and shows its hint; a right-click opens the Hireling screen; a
        // potion let go on it goes to the merc (0x61 slot 0).
        {
            const bool over = merc_portrait_x >= 0 && mouse.x >= merc_portrait_x && mouse.x <= merc_portrait_x + 0x2e && mouse.y >= 0x18 && mouse.y <= 0x3c;
            if (!over) merc_hover_at = 0;
            else if (merc_hover_at == 0) merc_hover_at = std::max<std::uint32_t>(frame_ms, 1);
            merc_hovered = over && !held && frame_ms - merc_hover_at >= 700;
            if (over && mouse.rpress_this_frame && character.expansion && view.merc_sheet) {
                hireling.open = true;
                char_open = stash_open = cube_open = quest_log.open = false;
                mouse.rpress_this_frame = false;
            }
            if (over && mouse.press_this_frame && held) { send(cmd::MercItem{ 0 }); mouse.press_this_frame = mouse.release_this_frame = false; }
        }
        // The Hireling screen's clicks: its close button, an item on or off the merc (0x61).
        if (hireling.open && !help.open && !controls.open && !game_menu.open) {
            if (!view.merc_sheet) hireling.open = false;
            else if (const int asked = hireling.input(*scene, mouse, held.has_value()); asked >= 0) {
                send(cmd::MercItem{ asked });
                mouse.press_this_frame = mouse.release_this_frame = false;
            }
            if (mouse.x >= 80 && mouse.x < 400 && mouse.y >= 60 && mouse.y < 492) mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // The Help screen's close button; its other clicks go nowhere (FUN_00494f30).
        if (help.open && !controls.open && !game_menu.open) {
            if (help.input(mouse)) { help.open = true; toggle_help(); }
            mouse.down = mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // Configure Controls (UI 0xb) takes every key and click while it's up.
        if (controls.open) {
            std::vector<std::uint16_t> virtual_keys;
            for (const auto key : keys_this_frame) if (const auto code = virtual_key(key)) virtual_keys.push_back(code);
            const auto result = controls.input(*scene, virtual_keys, mouse, std::exchange(controls_pad_press, pad::Button::Count), frame_ms);
            if (result == ControlsUI::kAccept) accept_controls();
            if (result == ControlsUI::kCancel) open_game_menu();          // FUN_0047e090(0, 1): back to the menu
            keys_this_frame.clear();
            mouse.down = mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
            mouse.wheel = 0;
        }
        // ESC handled globally in handle_sdl_events (returns to Title).
        // D2 movement: press or hold the left button on the ground
        // and the character walks toward that point (the target
        // tracks the cursor while held); the camera follows.
        // Chat: Enter opens a line, typing and Backspace edit it, Enter sends
        // it (0x15) and Esc drops it. The host's 0x26 comes back to everyone,
        // us too, as "name: message"; without a host (single player, local
        // co-op) game.exe's own server echoes it so, d2d's chat shows it.
        // "@name message" whispers (FUN_0047bad0: a name under 0x32, then
        // the message; else the whole line goes to all).
        if (!gold_box && !game_menu.open) {
            if (chat_typing) chat_typing->append(typed.substr(0, 255 - std::min<std::size_t>(chat_typing->size(), 255)));
            for (const auto key : keys_this_frame) {
                if (chat_typing && key == SDLK_BACKSPACE && !chat_typing->empty()) chat_typing->pop_back();
                if (!chat_typing) { if (keys::bound(key_table, keys::kChat, virtual_key(key))) chat_typing = std::string{}; continue; }   // the key config's Chat (Enter)
                if (key != SDLK_RETURN && key != SDLK_KP_ENTER) continue;
                if (const std::string_view line = *chat_typing; !line.empty()) {
                    std::string_view target, message = line;
                    if (line.starts_with('@') && net_game)              // '@' needs a multiplayer game (its entry's flag)
                        if (const auto space = line.find(' '); space != std::string_view::npos && space > 1 && space - 1 < 0x32 && space + 1 < line.size()) {
                            target = line.substr(1, space - 1);
                            message = line.substr(space + 1);
                        }
                    if (net_game) net_game->say(message, target);
                    else add_chat(character.name + string_id(*scene, 0xfd0) + std::string(message), 0, character.name.size());
                }
                chat_typing.reset();
            }
        }
        if (net_game) {
            // FUN_0049f490: to all, the name gold (colour code 4) then
            // SysmsgPlayer1 ": " and the message in the packet's colour;
            // a whisper, "name whispers: message" all green (2).
            for (auto& line : std::exchange(net_game->chat, {})) {
                if (line.type == 2) add_chat(line.name + string_id(*scene, 0xe46) + line.message, 2);
                else add_chat(line.name + string_id(*scene, 0xfd0) + line.message, line.colour, line.name.size());
            }
            for (const auto& event : std::exchange(net_game->events, {}))
                if (auto text = event_text(event); !text.empty()) add_chat(text, event.colour);
        }
        std::erase_if(chat_lines, [&](const auto& line) { return frame_ms >= line.until; });
        for (const auto key : keys_this_frame) {
            if ((gold_box || chat_typing || coop_typing()) && key != SDLK_ESCAPE) continue;   // typing (a co-op roster's name too): no hotkeys
            if (game_menu.open && key != SDLK_ESCAPE) continue;   // the menu's input table (FUN_00467a70) has the keys
            // The key config's functions (docs/research/re/controls.md); Esc stays Esc.
            const auto fired = keys::actions_of(key_table, virtual_key(key));
            const auto fires = [&](int action) { return std::ranges::contains(fired, action); };
            if (fires(keys::kInventory)) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (fires(keys::kSkillTree)) { tree_open = !tree_open; if (tree_open) inv_open = false; }   // both right-hand panels
            if (fires(keys::kToggleRun)) send(cmd::Run{ !view.running });
            if (fires(keys::kSwapWeapons) && character.expansion) send(cmd::SwapWeapons{});
            for (int slot = 0; slot < 16; ++slot)
                if (fires(slot < 8 ? keys::kSkill1 + slot : keys::kSkill9 + slot - 8)) skillbar.hotkey(slot, mouse.x, mouse.y);
            if (fires(keys::kShowBelt)) belt_open = !belt_open;
            if (fires(keys::kAutomap)) automap.open = !automap.open;
            if (fires(keys::kCharacter)) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = hireling.open = false; }
            if (fires(keys::kQuestLog)) toggle_quest_log();
            if (key == SDLK_ESCAPE && view.dead && (!view.ghost || view.all_dead)) { send(cmd::Resurrect{}); continue; }
            for (int belt = 0; belt < 4; ++belt)
                if (fires(keys::kBelt1 + belt)) net.send(cmd::UseBelt{ belt, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 });
            for (int phrase = 0; phrase < 8; ++phrase)
                if (fires(phrase < 7 ? keys::kSay1 + phrase : int(keys::kSayRetreat))) say(phrase);
            if (fires(keys::kRun) && !view.running) { send(cmd::Run{ true }); run_held = true; }   // 0x469000: run while held
            if (fires(keys::kClearScreen)) clear_screen();
            if (fires(keys::kClearMessages)) chat_lines.clear();   // FUN_004a01e0: every line's time out
            if (fires(keys::kPreviousSkill)) skillbar.cycle(-1);
            if (fires(keys::kNextSkill)) skillbar.cycle(1);
            if (fires(keys::kScreenShot)) screenshot_wanted = true;
            if (fires(keys::kShowPortraits)) portraits_hidden = !portraits_hidden;   // 0x493840
            if (fires(keys::kSkillSpeedBar)) skillbar.picking = skillbar.picking == 2 ? 0 : 2;   // 0x468b00: UI 3, the right hand's picker (0x7c07fc = 0)
            if (fires(keys::kMessageLog)) message_log = !message_log;
            if (fires(keys::kHelp)) toggle_help();
            // 0x468960: UI 0x16, in a multiplayer game only (FUN_0044db30), local co-op's too; a left-hand panel.
            if (fires(keys::kParty) && (net_game || coop_hud_hidden())) {
                party.open = !party.open;
                if (party.open) char_open = stash_open = cube_open = quest_log.open = hireling.open = false;
            }
            // 0x469170: UI 0x24, an expansion game with a merc; a left-hand panel.
            if (fires(keys::kHireling) && character.expansion && view.merc_sheet) {
                hireling.open = !hireling.open;
                if (hireling.open) char_open = stash_open = cube_open = quest_log.open = false;
            }   // 0x468980: UI 0x18, which 1.14d never draws
            // The automap's (0x468a60..0x468ad0), the same values as Options -> Automap.
            auto& automap_values = game_menu.value[4];
            if (fires(keys::kFadeAutomap)) automap_values[2] = (automap_values[2] + 1) % 4;   // 0x468a70: the next fade
            if (fires(keys::kPartyOnAutomap)) automap_values[4] = !automap_values[4];
            if (fires(keys::kNamesOnAutomap)) automap_values[5] = !automap_values[5];
            if (fires(keys::kToggleMiniMap)) automap.left = !automap.left;          // "AutoMap Left": the mini map's corner
            // Center Automap (0x468a60) puts the mini map's box back (FUN_00457520); d2d's never moves.
            if (key == SDLK_ESCAPE && skillbar.picking) { skillbar.picking = 0; continue; }   // the picker first
            // Esc (0x4690b0): the NPC's windows first, then the game menu
            // closes, else every Esc-closable panel at once (FUN_00456300;
            // not the automap or the mini-panel), else the menu opens.
            if (key == SDLK_ESCAPE) {
                if (chat_typing) chat_typing.reset();                         // a chat line being typed first
                else if (gold_box) gold_box.reset();                          // then the gold box
                else if (net_game && net_game->trade.state != 0) { if (net_game->trade.state == 2) net_game->trade_answer(false); else net_game->trade_cancel(); }
                else if (waypoint.open) waypoint = {};
                else if (store.npc >= 0) { send(cmd::CloseTrade{}); inv_open = false; } // the store first
                else if (speech.npc >= 0) { speech = {}; menu_after_speech = -1; }   // then speech
                else if (npc_menu.npc >= 0) npc_menu = {};          // then the menu
                else if (game_menu.open) close_game_menu();
                else if (help.open) toggle_help();                    // then the Help screen
                else if (inv_open || char_open || stash_open || cube_open || tree_open || quest_log.open || hireling.open || party.open)   // then panels
                    inv_open = char_open = stash_open = cube_open = tree_open = quest_log.open = hireling.open = party.open = false;
                else open_game_menu();
            }
        }
        if (audio_say >= 0) {                              // on the hero: one line at a time
            audio.play_sfx(*scene, audio_say, 1.f, int(rng.next() & 1), 0.f, 0, d2d::game::unit_key(d2d::game::kSoundOnPlayer, hero.id));
            audio_say = -1;
        }
        if (net_game)                                      // the other players' Says
            for (const auto& said : std::exchange(net_game->phrases, {})) audio.play_sfx(*scene, say_sound(said.cls, said.phrase), 1.f, int(rng.next() & 1));
        // Run's key let go (0x4691c0): back to walking, unless the toggle runs.
        if (run_held && !function_held(keys::kRun)) { send(cmd::Run{ false }); run_held = false; }
        // The wheel's functions (Previous / Next Skill by default) when nothing else scrolls.
        if (mouse.wheel != 0 && pad_field_or_free()) {
            const auto fired = keys::actions_of(key_table, mouse.wheel > 0 ? keys::kWheelUp : keys::kWheelDown);
            if (std::ranges::contains(fired, int(keys::kPreviousSkill))) skillbar.cycle(-1);
            if (std::ranges::contains(fired, int(keys::kNextSkill))) skillbar.cycle(1);
        }
        // The game's save (FUN_0052e2a0: every player): Baal dead, the last portal.
        if (std::exchange(world.save_due, false)) save();
        // Classic Terror's End: the game closed under the player (FUN_00530590).
        if (world.game_closed) { world.game_closed = false; save(); screen = Screen::CharSelect; return; }
        // The game menu takes every click while it's up; Exit saves and goes
        // to the roster. Its volumes go to d2d.cfg.
        bool hud_click = false;
        if (game_menu.open) {
            const auto action = game_menu.input(*scene, audio, mouse, keys_this_frame, frame_ms);
            if (game_menu.volume_changed && !cfg_file.empty()
                && !d2d::userdir::save_cfg(cfg_file, { { "master_volume", std::to_string(audio.master_volume) },
                                                      { "music_volume", std::to_string(audio.music_volume) } }))
                d2d::log::warn("d2d.cfg: the volume not saved");
            game_menu.volume_changed = false;
            if (action != GameMenu::kNone) close_game_menu();
            if (action == GameMenu::kControls) {          // CfgOptions (0x47f400): UI 0xb
                controls.family = pad_family;
                controls.pad_extra = pad_controls_page;
                controls.show(key_table, pad_config, character.expansion);
            }
            // Save and Exit logs out whoever's pad has the menu. A partner leaves alone; the first
            // player alone ends the game, in local co-op the next player leads and it goes on.
            if (action == GameMenu::kExit) {
                if (const int seat = focus_seat(); seat > 0) { coop_leave(seat); partner_left = seat; }
                else if ((handed_over = lead_leaves()) < 0) { save(); screen = Screen::CharSelect; }
            }
            hud_click = true;
        } else {
            // The mini-panel's buttons (FUN_0047ec50) and the HUD's button for it.
            const bool left_open = char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open;
            int action = -1;
            hud_click = !coop_hud_hidden() && mini.input(*scene, audio, mouse, left_open, inv_open || tree_open, action);
            if (action == MiniPanel::kCharacter) { char_open = !char_open; if (char_open) stash_open = cube_open = quest_log.open = false; }
            if (action == MiniPanel::kInventory) { inv_open = !inv_open; if (inv_open) tree_open = false; }
            if (action == MiniPanel::kSkills) { tree_open = !tree_open; if (tree_open) inv_open = false; }
            if (action == MiniPanel::kAutomap) automap.open = !automap.open;
            if (action == MiniPanel::kQuests) toggle_quest_log();
            if (action == MiniPanel::kMenu) open_game_menu();
        }
        if (hud_click) {
            if (mouse.press_this_frame) press_on_ui = true;
            mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
        }
        // A trade with another player: its box or window takes its clicks;
        // the bags stay open beside it, the cursor's item goes into our
        // offer (0x18, buffer 2).
        if (net_game && net_game->trade.state != 0 && !game_menu.open) {
            auto& trade = net_game->trade;
            if (trade.state >= 3) { inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; }
            if (trade.state < 3 && gold_box && gold_box->mode == 2) gold_box.reset();
            const auto click = trade_click(*scene, trade.state, mouse.x, mouse.y);
            const bool over_window = trade.state >= 3 && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320 && mouse.y >= kCharPanelY && mouse.y < kCharPanelY + 432;
            if (mouse.press_this_frame && (click != TradeClick::kNone || over_window)) {
                if (click == TradeClick::kAccept) {
                    if (trade.state == 2) net_game->trade_answer(true);
                    else if (trade.state >= 3 && trade.state != 7) net_game->trade_accept();
                } else if (click == TradeClick::kGold) {
                    gold_box = GoldBox{ .mode = 2 };
                } else if (click == TradeClick::kDecline) {
                    if (trade.state == 2) net_game->trade_answer(false); else net_game->trade_cancel();
                } else if (click == TradeClick::kOurGrid && hero.held) {
                    const auto& grid = scene->trade_layout[1];
                    const int column = (mouse.x - grid.grid_x) / grid.box_w, row = (mouse.y - grid.grid_y) / grid.box_h;
                    if (const std::uint32_t held_id = net_cursor ? net_cursor : net_game->host_item(*hero.held)) {
                        net_game->send_items({ d2d::net::d2gs::c2s::cursor_to_grid(held_id, std::uint32_t(column), std::uint32_t(row), 2) });
                        auto offered = *hero.held;
                        offered.location = d2d::d2s::item_location::kStored;
                        offered.panel = 101;
                        offered.column = column;
                        offered.row = row;
                        offered.id = int(held_id);
                        trade.ours.push_back(std::move(offered));
                        hero.held.reset();
                        net_cursor = 0;
                    }
                }
                press_on_ui = true;
                mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = false;
            }
        }
        if (inv_open) tree_open = false;       // the stash / a store opened the inventory
        const auto& lay = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        // The inventory's and the character panel's close buttons (buysellbtn
        // 10, 11 held): pressed, then let go over it, shuts the panel
        // (FUN_0048edf0 at W - 80 - 0x12e, H - 0x7c; FUN_004a7d00 at 80 + 0x80, H - 0x78).
        auto close_button = [&](bool& down, bool& open, int left, int bottom) {
            const bool over = mouse.x >= left && mouse.x < left + 0x20 && mouse.y > bottom - 0x20 && mouse.y <= bottom;
            if (mouse.press_this_frame && over) { down = true; mouse.press_this_frame = false; }
            if (mouse.release_this_frame && down) { down = false; if (over) open = false; mouse.release_this_frame = false; }
        };
        if (inv_open && !held) close_button(inv_close_down, inv_open, lay.panel_x + 0x12, lay.panel_y + 416);
        if (char_open && !held) close_button(char_close_down, char_open, kCharPanelX + 0x80, kCharPanelY + 420);
        const bool over_panel =
            (tree_open && mouse.x >= 400 && mouse.x < 720 && mouse.y >= 60 && mouse.y < 540) ||
            (inv_open && mouse.x >= lay.panel_x && mouse.x < lay.panel_x + 320
                      && mouse.y >= lay.panel_y && mouse.y < lay.panel_y + 432) ||
            ((char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open) && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320
                       && mouse.y >= kCharPanelY && mouse.y < kCharPanelY + 432);
        // The belt: its HUD strip (row 1's boxes) toggles the popup;
        // strip and open popup take the click instead of the world.
        const auto& belt = scene->belts[std::size_t(belt_index(*scene, character.items))];
        const auto& first_box = belt.box[0];
        const auto& fourth_box = belt.box[3];
        const bool over_belt = !coop_hud_hidden() &&
            mouse.x >= first_box[0] && mouse.x <= fourth_box[1]
            && ((mouse.y >= first_box[2] && mouse.y <= first_box[3])
                || (belt_open && mouse.y >= belt.box[std::size_t(std::max(belt.boxes - 1, 0))][2]
                              && mouse.y <= first_box[3]));
        // The item cursor (not with a trade button toggled on, or a
        // menu or speech up). Held over the store's stock, a click
        // sells the item.
        bool item_click = false;
        // With an identify scroll picked, a click names the item for it and ends the pick.
        if (identify_with >= 0 && mouse.press_this_frame) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            if (const auto* to_cursor = click.cmd ? std::get_if<cmd::ToCursor>(&*click.cmd) : nullptr) net.send(cmd::IdentifyWith{ identify_with, to_cursor->item });
            identify_with = -1;
            item_click = true;
        }
        // With Charsi's Imbue chosen, a click on an item takes it up and imbues it.
        if (imbue_with >= 0 && mouse.press_this_frame && !item_click) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            if (click.cmd && std::holds_alternative<cmd::ToCursor>(*click.cmd)) {
                net.send(*click.cmd);
                net.send(cmd::Imbue{ imbue_with });
            }
            imbue_with = -1;
            item_click = true;
        }
        if (mouse.press_this_frame && !item_click && store.mode == 0 && npc_menu.npc < 0 && speech.npc < 0) {
            if (held && store.npc >= 0 && mouse.x >= 96 && mouse.x < 96 + 10 * 29
                && mouse.y >= 123 && mouse.y < 123 + 10 * 29) {
                net.send(cmd::Sell{ held->id });
                item_click = true;
            } else {
                const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                    { inv_open, stash_open, cube_open, belt_open, character.expansion, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 },
                                                    mouse.x, mouse.y);
                if (click.cmd) net.send(*click.cmd);
                item_click = click.consumed;
            }
        }
        if (mouse.press_this_frame && over_belt && mouse.y >= first_box[2] && !item_click) belt_open = !belt_open;
        // A right-click on a carried item uses it: a potion in the inventory,
        // stash or belt is drunk.
        if (mouse.rpress_this_frame && !held && store.npc < 0 && npc_menu.npc < 0 && speech.npc < 0) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            const auto* to_cursor = click.cmd ? std::get_if<cmd::ToCursor>(&*click.cmd) : nullptr;
            const auto used = to_cursor ? std::ranges::find(character.items, to_cursor->item, &d2d::d2s::Item::id) : character.items.end();
            if (imbue_with >= 0) imbue_with = -1;                             // a right-click drops the imbue pick too
            else if (identify_with >= 0) identify_with = -1;                  // a right-click drops the pick
            else if (used != character.items.end() && (used->code == "isc" || used->code == "ibk")) {
                identify_with = used->id;
                audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorIdentifyReady, 1.f, 0);
            }
            else if (to_cursor) net.send(cmd::UseItem{ to_cursor->item });
        }
        // Quest log (FUN_004a3e40 press, FUN_004a42a0 release): a tab switches
        // on the press; an icon with something to say, close and questlast
        // hold with a click (Sounds.txt 4); let go on the same icon picks it,
        // over close shuts the log, over questlast plays the picked quest's
        // message again (FUN_004a27d0).
        // ponytail: the description isn't hidden while that message plays (0x7bf2b3).
        if (quest_log.open) {
            const int button = quest_button_at(mouse.x, mouse.y);
            const int slot = quest_slot_at(mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                if (const int act = quest_tab_at(mouse.x, mouse.y, character.expansion); act >= 0) quest_log_tab(quest_log, quest_bits(), quest_state(), act, character.expansion);
                else if (slot >= 0) {
                    for (const auto& entry : kQuestLog)
                        if (entry.act == quest_log.act && entry.slot == slot && quest_text(quest_bits(), entry.quest, quest_state()).shown != 2) {
                            quest_log.pressed = slot;
                            audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
                        }
                } else if (button) {
                    (button == 1 ? quest_log.close_down : quest_log.last_down) = true;
                    audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
                }
            }
            if (mouse.release_this_frame) {
                if (quest_log.close_down) { if (button == 1) quest_log.open = false; }
                else if (quest_log.pressed >= 0) {
                    if (slot == quest_log.pressed) quest_log.remembered[std::size_t(quest_log.act)] = quest_log.slot = slot;
                } else if (quest_log.last_down && button == 2 && quest_log.slot >= 0) {
                    for (const auto& entry : kQuestLog)
                        if (entry.act == quest_log.act && entry.slot == quest_log.slot)
                            if (const auto text = quest_text(quest_bits(), entry.quest, quest_state()); text.speech) replay_speech = text.speech;
                }
                quest_log.pressed = -1;
                quest_log.close_down = quest_log.last_down = false;
            }
        }
        // The Quest Log button (UI 0x11): pressed with a click (FUN_004a2a20),
        // let go over it it goes and the log opens on its quest (FUN_004a4110).
        if (const auto quest_button = quest_log_button_now(); quest_button.x0 >= 0) {
            const bool over = over_quest_log_button(quest_button, mouse.x, mouse.y);
            if (mouse.press_this_frame && over) { quest_log.button_down = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
            if (mouse.release_this_frame) {
                if (quest_log.button_down && over) { quest_log.button = false; toggle_quest_log(); }
                quest_log.button_down = false;
            }
        }
        // Skill tree: tabs switch on press; a skill icon pressed and
        // released spends a point (FUN_004ab7e0 / FUN_004abc30).
        if (tree_open) {
            const int cls = std::max(character.character_class, 0);
            const int skill = skill_at(*scene, cls, tree_tab, mouse.x, mouse.y);
            if (mouse.press_this_frame) {
                if (const int tab = skill_tab_at(mouse.x, mouse.y); tab > 0) tree_tab = tab;
                skill_pressed = character.stats.get(d2d::d2s::kSkillPts) > 0
                    && d2d::rules::can_learn(scene->rules, cls, skill, character.stats.skills, int(character.stats.get(d2d::d2s::kLevel))) ? skill : -1;
            }
            if (mouse.release_this_frame) {
                if (skill_pressed >= 0 && skill == skill_pressed) net.send(cmd::SkillPoint{ skill });
                skill_pressed = -1;
            }
        }
        // Char panel stat buttons: press, then release on the same
        // button spends a point (Shift: all of them), FUN_004a78c0.
        if (char_open && character.stats.get(d2d::d2s::kStatPts) > 0) {
            const int stat_button = stat_button_at(mouse.x, mouse.y);
            if (mouse.press_this_frame && stat_button >= 0) stat_pressed = stat_button;
            if (mouse.release_this_frame) {
                if (stat_pressed >= 0 && stat_button == stat_pressed) {
                    const int count = (SDL_GetModState() & SDL_KMOD_SHIFT) ? int(character.stats.get(d2d::d2s::kStatPts)) : 1;
                    net.send(cmd::StatPoint{ kStatButtons[std::size_t(stat_button)].stat, count });
                }
                stat_pressed = -1;
            }
        } else {
            stat_pressed = -1;
        }
        // Right-clicking the Horadric Cube ("box") in the inventory
        // or the stash opens it in the left panel, as D2 does.
        if (mouse.rpress_this_frame)
            for (const auto& item : character.items) {
                if (item.code != "box" || item.location != d2d::d2s::item_location::kStored) continue;
                const bool in_inv = inv_open && item.panel == d2d::d2s::item_panel::kInventory, in_stash = stash_open && item.panel == d2d::d2s::item_panel::kStash;
                if (!in_inv && !in_stash) continue;
                const auto rect = grid_rect(*scene, in_inv ? lay : scene->stash_layout[character.expansion ? 1 : 0], item);
                if (mouse.x >= rect[0] && mouse.x < rect[0] + rect[2] && mouse.y >= rect[1] && mouse.y < rect[1] + rect[3]) {
                    if (net_game) send(cmd::UseItem{ item.id });  // the host opens it (0x20 -> 0x77 0x15)
                    else { cube_open = true; stash_open = char_open = false; }
                }
            }
        // The Cube's Transmute (FUN_0048a190): held on a press over it, sent
        // on letting go over it — the World's (cmd::Transmute), a joined
        // game's host's (C->S 0x4f 0x18).
        if (cube_open && mouse.press_this_frame && transmute_at(*scene, mouse.x, mouse.y)) {
            transmute_down = true;
            audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
        }
        if (transmute_down && !mouse.down) {
            if (cube_open && transmute_at(*scene, mouse.x, mouse.y)) {
                if (net_game) net_game->send_items({ d2d::net::d2gs::c2s::button(0x18) });
                else send(cmd::Transmute{});
            }
            transmute_down = false;
        }
        // The gold coins open the gold box (0x454150): the stash's to withdraw
        // (4), the inventory's to deposit (3) with the stash open, else to drop (1).
        const bool trading = net_game && net_game->trade.state != 0;
        if (!gold_box && !trading && store.npc < 0 && mouse.press_this_frame
            && ((stash_open && stash_coin_at(mouse.x, mouse.y)) || (inv_open && inventory_coin_at(mouse.x, mouse.y)))) {
            gold_box = GoldBox{ .mode = stash_open ? (stash_coin_at(mouse.x, mouse.y) ? 4 : 3) : 1 };
            audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0);
            mouse.press_this_frame = false;
        }
        // An open NPC menu takes every click: an entry runs its action,
        // anything else closes it.
        bool menu_click = false;
        if (g_drawn.level == level) {                                 // drawn last frame, in this level
            automap_reveal(*scene, *level, automap, g_drawn.cells);
            if (view.level == level) automap_icons(*world.game_data, *level, automap, view.player.x, view.player.y, view.npc_states);
            for (const auto& neighbour : level->nearby)               // what's in view across the edge
                if (neighbour.level->layer == level->layer) automap_reveal(*scene, *neighbour.level, automap, g_drawn.cells, neighbour.dx, neighbour.dy);
        }
        // The NPC's voice (FUN_004a10e0 plays FUN_004e0650's sound for
        // the speech string) follows the speech box.
        if (speech.npc < 0 && audio.voice.src) audio.stop_voice();
        // The level's SoundEnviron (Levels.txt SoundEnv -> Song, Day / Night
        // Ambience, Day / Night Event). Day is the day's phases 1..3
        // (FUN_004e42e0 via FUN_0061c220), 0° to 180°; else night.
        const bool day = view.day.phase >= 1 && view.day.phase <= 3;
        const int amb = day ? level->ambience : level->night_ambience;
        if (audio.music.sound == 0) {
            audio.play_music(*scene, level->song);
            audio.play(audio.ambience, *scene, amb);
            if (audio.music.sound == 0) audio.music.sound = -1;   // don't retry every frame
        }
        // Another level's song: game.exe (FUN_004dcaa0) switches 75 sound
        // ticks (3 s) into the new level, so skirting an edge doesn't flip
        // it, and cross-fades (Audio::crossfade_music). Its ambience then,
        // and at dusk and dawn.
        if (audio.music.sound > 0 && level->song > 0 && audio.music.sound != level->song && frame_ms - level_ms >= 75 * 40)
            audio.crossfade_music(*scene, level->song);
        if (audio.music.sound > 0 && audio.music.sound == level->song && audio.ambience.sound != amb) {
            const bool turn = (audio.ambience.sound == level->ambience && amb == level->night_ambience)
                           || (audio.ambience.sound == level->night_ambience && amb == level->ambience);
            if (turn) audio.crossfade_ambience(*scene, amb);   // dusk / dawn (FUN_004e42e0)
            else audio.play(audio.ambience, *scene, amb);
            audio.ambience.sound = amb;           // tried: not again every frame
        }
        // The weather, a client frame at a time where it rains (FUN_00473f50;
        // leaving, the drops go and the cycle waits), and its sound: Sounds.txt
        // 64 scene_rain at the density's volume, easing 6 a tick
        // (FUN_004e42e0).
        if (frame_ms - rain_ms > 1000) rain_ms = frame_ms;
        for (; frame_ms - rain_ms >= 40; rain_ms += 40) {
            den_tick(frame_ms);
            critter_tick(frame_ms);
            const float dx = cam_x - rain_cam_x, dy = cam_y - rain_cam_y;
            rain_cam_x = cam_x; rain_cam_y = cam_y;
            ++rain.clock;                                 // the client frame (0x7a0498)
            if (level->mud) rain.tick_bubbles(int(std::lround((dx - dy) * (kIsoW / 2))), int(std::lround((dx + dy) * (kIsoH / 2))));
            else rain.bubbles.clear();
            if (!level->rain) { rain.drops.clear(); rain.splashes.clear(); }
            else rain.tick(rain.rng, g_view_width, int(kScreenHeight), int(std::lround((dx - dy) * (kIsoW / 2))), int(std::lround((dx + dy) * (kIsoH / 2))),
                           d2d::rules::kDay[std::size_t(view.day.phase)].type, level->id, d2d::rules::level_ids::act_of(level->id));
            const int want = level->rain && !rain.snow ? int(rain.volume() * 255.f) : 0;   // FUN_00473c40: no sound in the snow
            rain_vol = rain_vol < want ? std::min(want, rain_vol + 6) : std::max(want, rain_vol - 6);
        }
        // The lightning's drawn frame (a frame drawn each update) (FUN_00473910): its flash, and thunder
        // queued that many sound ticks on.
        if (const auto strike = rain.frame(rain.rng, audio.dev != nullptr); strike.thunder >= 0) {
            thunder_at = frame_ms + std::uint32_t(strike.thunder) * 40; thunder_x = strike.x; thunder_y = strike.y; thunder_due = true;
        }
        if (thunder_due && frame_ms >= thunder_at) { thunder_due = false; audio.play_placed(*scene, d2d::rules::sound_ids::kEventThunder, 0, float(thunder_x), float(thunder_y)); }
        if (rain_vol > 0 && audio.rain.sound != d2d::rules::sound_ids::kSceneRain) audio.play(audio.rain, *scene, d2d::rules::sound_ids::kSceneRain);
        if (rain_vol == 0 && audio.rain.sound) audio.stop(audio.rain);
        if (audio.rain.src && scene->sounds.size() > d2d::rules::sound_ids::kSceneRain)
            audio.set_gain(audio.rain, float(scene->sounds[d2d::rules::sound_ids::kSceneRain].volume) / 255.f * float(rain_vol) / 255.f);
        // Every Event Delay ticks, give or take a third, one of the event
        // sounds from the left or right (FUN_004e42e0: x +-450..750 and
        // 2 dy -100..100 client pixels, FUN_004b99a0; the first within one
        // gap of arriving), mixed by that distance like any placed sound.
        {
            const std::uint32_t tick = frame_ms / 40;
            const int event_sound = day ? level->day_event : level->night_event, delay = level->event_delay;
            auto spread = [&](int spread_range) { return sound_rng(2 * spread_range + 1) - spread_range; };
            if (event_sound != amb_event) {
                amb_event = event_sound;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
                amb_last = tick - std::uint32_t(sound_rng(int(amb_next)));
            }
            if (event_sound > 0 && tick - amb_last >= amb_next && !audio.world_paused) {
                const int x = (sound_rng(2) ? 1 : -1) * (450 + sound_rng(301));
                const int y = sound_rng(201) - 100;
                audio.play_placed(*scene, event_sound, sound_rng(16), float(x), float(y));
                amb_last = tick;
                amb_next = std::uint32_t(std::max(1, delay + spread(delay / 3)));
            }
        }
        if (questdone_sound) { questdone_sound = false; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorQuestDone, 1.f, 0); }
        if (replay_speech) {
            const auto found = std::ranges::find_if(kSpeechSound, [&](const auto& entry) { return entry.first == replay_speech; });
            if (found != kSpeechSound.end()) audio.play_voice(*scene, found->second);
            replay_speech = 0;
        }
        if (speech.npc >= 0 && speech.voice == 0) {
            speech.voice = -1;
            const auto found = std::ranges::find_if(kSpeechSound, [&](const auto& entry) { return entry.first == speech.string; });
            if (found != kSpeechSound.end()) { speech.voice = found->second; audio.play_voice(*scene, found->second); }
        }
        if (speech.npc >= 0 && (speech.done(frame_ms) || mouse.press_this_frame)) {
            menu_click = mouse.press_this_frame;          // a click skips the speech
            speech = {};
            if (menu_after_speech >= 0 && std::size_t(menu_after_speech) < level->npcs.size()) open_menu(menu_after_speech);
            menu_after_speech = -1;
        } else if (npc_menu.npc >= 0 && npc_menu.scroll_max > 0 && mouse.wheel != 0) {   // the hire list scrolls
            npc_menu = open_hire_menu(*scene, npc_menu.npc, hire_offers, character.stats.get(d2d::d2s::kGold) + character.stats.get(d2d::d2s::kGoldBank),
                                      npc_menu.scroll - mouse.wheel);
        } else if (npc_menu.npc >= 0 && mouse.press_this_frame) {
            menu_click = true;                            // the click is the menu's, not a walk
            const int line_index = npc_menu.line_at(mouse.x, mouse.y);
            const auto action = line_index >= 0 ? npc_menu.lines[std::size_t(line_index)].action : NpcMenuState::kClose;
            const int npc_menu_arg = line_index >= 0 ? npc_menu.lines[std::size_t(line_index)].arg : -1;
            const int who = npc_menu.npc;
            const auto& npc = level->npcs[std::size_t(who)];
            const auto& state = view.npc_states[std::size_t(who)];
            const float dx = (npc.path.empty() ? npc.x : state.x) - view.player.x, dy = (npc.path.empty() ? npc.y : state.y) - view.player.y;
            const int screen_x = int(kScreenWidth) / 2 + int(std::lround((dx - dy) * (kIsoW / 2)));
            const int screen_y = kViewY + int(std::lround((dx + dy) * (kIsoH / 2)));
            npc_menu = {};
            if (action == NpcMenuState::kHire) {
                net.send(cmd::OpenHire{ who });             // Kashya's list: the World rolls it
            } else if (action == NpcMenuState::kHireOffer) {
                net.send(cmd::Hire{ npc_menu_arg });
            } else if (action == NpcMenuState::kIdentify) {
                net.send(cmd::Identify{});
            } else if (action == NpcMenuState::kGamble || action == NpcMenuState::kTrade) {
                net.send(cmd::OpenTrade{ who, action == NpcMenuState::kGamble });
            } else if (action == NpcMenuState::kTalk) {
                npc_menu = open_talk_menu(*scene, *level, who, screen_x, screen_y, npc_quest);
            } else if (action == NpcMenuState::kRespec) {
                npc_menu = open_respec_menu(*scene, who, screen_x, screen_y);
            } else if (action == NpcMenuState::kRespecOk) {
                net.send(cmd::Respec{ who });
            } else if (action == NpcMenuState::kCaravan) {
                send(cmd::Caravan{ who });                      // a joined game: the host's NPC (Town::send)
            } else if (action == NpcMenuState::kResurrectMerc) {
                net.send(cmd::ResurrectMerc{ who });
            } else if (action == NpcMenuState::kImbue) {
                // 0x4b35b0: the menu closes and the inventory opens; the
                // server imbues the item in hand (C->S 0x38 kind 0).
                // ponytail: game.exe opens a one-slot item panel (0x4c0620):
                // the item in hand goes in its slot, identified and
                // imbuable only (FUN_004bfc50), and the panel's button sends
                // (FUN_004bfaf0). Here an item in hand goes at once, else
                // the next click on an item takes it up and sends
                // (act1-end.md "Imbue").
                if (held) net.send(cmd::Imbue{ who });
                else { imbue_with = who; inv_open = true; char_open = stash_open = cube_open = quest_log.open = false; }
            } else if (action == NpcMenuState::kQuest) {
                speech = start_speech(*scene, who, std::uint16_t(npc_menu_arg), frame_ms);
                send(cmd::QuestMessage{ who, npc_menu_arg });
            } else if (action == NpcMenuState::kIntro || action == NpcMenuState::kGossip) {
                const auto found = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& entry) { return entry.hc_idx == npc.hc_idx; });
                if (found != kNpcTalk.end() && !found->topics.empty()) {
                    const int cls = std::max(character.character_class, 0);
                    if (gossip_pick.size() != level->npcs.size()) gossip_pick.assign(level->npcs.size(), -1);
                    int topic;
                    auto done = [&](int quest) { return character.header.quest_flag(character.header.active_difficulty(), quest, 0); };
                    if (action == NpcMenuState::kIntro) {
                        topic = talk_topic(*found, true, cls, rng, done);
                    } else {
                        if (gossip_pick[std::size_t(who)] < 0)
                            gossip_pick[std::size_t(who)] = talk_topic(*found, false, cls, rng, done);
                        topic = gossip_pick[std::size_t(who)];
                    }
                    speech = start_speech(*scene, who, found->topics[std::size_t(topic)].string, frame_ms);
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
                const int button_x = kCharPanelX - 1 + kBtnX[i], button_y = 476 - 32 + 1;
                const bool hovered = mouse.x >= button_x && mouse.x < button_x + 32 && mouse.y >= button_y && mouse.y < button_y + 32;
                if (hovered && mouse.down) store.pressed[std::size_t(i)] = true;
                if (hovered && mouse.release_this_frame && i < 2) store.mode = store.mode == i + 1 ? 0 : i + 1;
                // Repair vendors: repair (6) toggles like buy/sell,
                // repair all (18) fixes everything worn and carried.
                const bool repairer = store_button_frames(store)[2] == 6;
                if (hovered && mouse.release_this_frame && i == 2 && repairer) store.mode = store.mode == 3 ? 0 : 3;
                if (hovered && mouse.release_this_frame && i == 3 && repairer) net.send(cmd::Repair{ -1 });
                if (hovered && mouse.release_this_frame && i == 3 && store_button_frames(store)[3] == 10) {
                    send(cmd::CloseTrade{});
                    inv_open = false;
                    break;
                }
            }
            if (mouse.press_this_frame && mouse.y >= 60 && mouse.y <= 90
                && mouse.x >= kCharPanelX && mouse.x < kCharPanelX + 320)
                store.tab = (mouse.x - kCharPanelX) / 80;
            // Right-click on stock buys; with Buy or Sell toggled on,
            // a left click buys the stock item / sells your item.
            const int store_index = store.npc >= 0 ? store_item_at(*scene, store, mouse.x, mouse.y) : -1;
            if (store_index >= 0 && (mouse.rpress_this_frame || (mouse.press_this_frame && store.mode == 1)))
            {
                send(cmd::Buy{ store_index, store.tab });
            }
            // Sell: an inventory item; repair: that or a worn one.
            if (store.npc >= 0 && mouse.press_this_frame && (store.mode == 2 || store.mode == 3))
                for (std::size_t i = 0; i < character.items.size(); ++i) {
                    const auto& item = character.items[i];
                    const bool worn = item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast;
                    if (!(item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory) && !(worn && store.mode == 3)) continue;
                    const auto rect = worn ? lay.slots[std::size_t(item.slot)] : grid_rect(*scene, lay, item);
                    if (mouse.x >= rect[0] && mouse.x < rect[0] + rect[2] && mouse.y >= rect[1] && mouse.y < rect[1] + rect[3]) {
                        if (store.mode == 2) send(cmd::Sell{ item.id });
                        else send(cmd::Repair{ item.id });
                        break;
                    }
                }
        }
        // Waypoint panel: tabs switch acts, cancel (or the row of the
        // level you're in) closes it.
        if (waypoint.open) {
            const bool on_cancel = mouse.x >= kCharPanelX + 0x111 && mouse.x < kCharPanelX + 0x111 + 0x24
                                && mouse.y >= 60 + 0x183 && mouse.y < 60 + 0x183 + 0x22;
            waypoint.cancel_down = on_cancel && mouse.down;
            if (mouse.press_this_frame) {
                if (const int tab = waypoint_tab_at(character.header, character.expansion, mouse.x, mouse.y); tab >= 0) waypoint.tab = tab;
                else if (const int row = waypoint_row_at(*scene, waypoint, character.header, mouse.x, mouse.y); row >= 0) {
                    net.send(cmd::Waypoint{ waypoint.npc, scene->waypoint_levels[std::size_t(waypoint.tab)][std::size_t(row)].level });
                    waypoint = {};
                }
            }
            if (on_cancel && mouse.release_this_frame) waypoint = {};
        }
        // New Stats / New Skills: a press holds the button with a click
        // (Sounds.txt 4, FUN_004a66e0 / FUN_004a6790); let go over it and it
        // opens the character panel / tree (FUN_004a6840 / FUN_004a6920).
        const auto shown = level_buttons_now();
        const bool on_stats = over_stats_button(shown.stats_x, mouse.x, mouse.y);
        const bool on_skills = over_skills_button(shown.skills_x, mouse.x, mouse.y);
        if (shown.stats_x < 0) stats_down = false;          // hidden lets go (FUN_004a6b30)
        if (shown.skills_x < 0) skills_down = false;
        const bool level_click = mouse.press_this_frame && (on_stats || on_skills);
        if (level_click) { (on_stats ? stats_down : skills_down) = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
        if (mouse.release_this_frame) {
            if (stats_down && on_stats) { char_open = true; stash_open = cube_open = quest_log.open = false; }
            if (skills_down && on_skills) { tree_open = true; inv_open = false; }
            stats_down = skills_down = false;
        }
        // The run button: the same press and click (FUN_00499500); let go
        // over it and run toggles, as R does (FUN_004996a0).
        const bool on_run = !coop_hud_hidden() && over_run_button(mouse.x, mouse.y);
        const bool run_click = mouse.press_this_frame && on_run;
        if (run_click) { run_down = true; audio.play_sfx(*scene, d2d::rules::sound_ids::kCursorButtonClick, 1.f, 0); }
        if (mouse.release_this_frame) {
            if (run_down && on_run) send(cmd::Run{ !view.running });
            run_down = false;
        }
        // Holding an item, a click on the world drops it (C→S 0x17).
        const bool bar_click = (!coop_hud_hidden() || skillbar.picking) && skillbar.click(mouse);
        if (held && mouse.press_this_frame && !item_click && !over_panel && !over_belt && !menu_click && !bar_click && !level_click && !run_click
            && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && have_world)
            net.send(cmd::Drop{ held->id });
        const bool over_ui = over_panel || over_belt || menu_click || npc_menu.npc >= 0 || item_click || held || bar_click || level_click || run_click
                          || hud_click || game_menu.open;
        // A press on the UI stays the UI's while the button is held: no walk
        // starts under a menu that just closed.
        if (mouse.press_this_frame) press_on_ui = over_ui;
        else if (!mouse.down) press_on_ui = false;
        if (have_world) walk(mouse, over_ui || press_on_ui, audio, frame_ms, last_ms);
        // Walked away from the NPC whose windows are open: they shut, as Esc.
        // game.exe's panels leave no world to walk in; widescreen does
        // (deviations.md). 2 cells is ours: game.exe talks within 6 subtiles.
        if (const int npc_open = store.npc >= 0 ? store.npc : npc_menu.npc >= 0 ? npc_menu.npc : speech.npc;
            have_world && level && npc_open >= 0 && std::size_t(npc_open) < level->npcs.size() && std::size_t(npc_open) < view.npc_states.size()) {
            const auto& npc = level->npcs[std::size_t(npc_open)];
            const auto& state = view.npc_states[std::size_t(npc_open)];
            const float dx = (npc.path.empty() ? npc.x : state.x) - view.player.x, dy = (npc.path.empty() ? npc.y : state.y) - view.player.y;
            if (dx * dx + dy * dy > 2.f * 2.f) {
                if (store.npc >= 0) { send(cmd::CloseTrade{}); inv_open = false; }
                npc_menu = {}; speech = {}; menu_after_speech = -1;
            }
        }
        std::erase_if(quest_quakes, [&](d2d::rules::QuestQuake& control) {
            return control.step(frame_ms, shake, [&] {
                if (const auto found = scene->sound_index.find("monster_diablo_taunt_1"); found != scene->sound_index.end())
                    audio.play_sfx(*scene, found->second, 1.f, 0);
            });
        });
        audio.quake(*scene, shake.step(frame_ms, shake_rng));
        play_cues(cues, audio, view.player.x, view.player.y, rng, frame_ms);
        object_sounds(audio, frame_ms, view.player.x, view.player.y);
        audio.stop_owners([&](int owner) { return owner > 0 ? std::ranges::contains(view.missiles, owner, &d2d::game::View::Shot::sound) : std::ranges::contains(object_loops, owner); });
        audio.track(view.player.x, view.player.y, [&](int owner) -> std::optional<std::pair<float, float>> {
            if (const auto shot = std::ranges::find(view.missiles, owner, &d2d::game::View::Shot::sound); owner > 0 && shot != view.missiles.end()) return std::pair{ shot->x, shot->y };
            if (const auto object = std::size_t(-1 - owner); owner < 0 && object < view.npc_states.size()) return std::pair{ view.npc_states[object].x, view.npc_states[object].y };
            return std::nullopt;
        });
        audio.pause_world(false);                  // a cinematic's hold ends
        audio.menu_paused = !net_game && (game_menu.open || controls.open);   // the world's sounds fade out (Audio::step_ducks)
        draw(framebuffer, mouse, frame_ms);
        // Screen Shot (FUN_004fa7a0): at most one a second, Screenshot%03d,
        // the first number free. d2d writes PNG in its screenshots folder
        // (deviations.md), game.exe JPEG in the game's.
        if (std::exchange(screenshot_wanted, false) && !shot_dir.empty() && frame_ms - last_shot_ms >= 1000) {
            last_shot_ms = frame_ms;
            for (int number = 0; number < 1000; ++number) {
                const auto path = shot_dir / std::format("Screenshot{:03}.png", number);
                if (std::filesystem::exists(path)) continue;
                try {
                    d2d::screenshot::save_png(path, framebuffer, std::uint32_t(fb_width(framebuffer)), kScreenHeight);
                    d2d::log::info("screenshot: {}", path.string());
                } catch (const std::exception& error) { d2d::log::warn("screenshot: {}", error.what()); }
                break;
            }
        }
    }

}  // namespace d2d::client
