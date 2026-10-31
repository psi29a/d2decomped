// SPDX-License-Identifier: GPL-3.0-or-later
// Town (town.hpp): input: the mouse, gamepads, local co-op seats and focus. The rest is in
// this directory.
#include "town.hpp"

#include "audio.hpp"
#include "common.hpp"
#include "controls.hpp"
#include "cursor.hpp"
#include "gamepad.hpp"
#include "gamepad_map.hpp"
#include "ingame.hpp"
#include "internal.hpp"
#include "keyconfig.hpp"
#include "load.hpp"
#include "panels.hpp"
#include "party.hpp"
#include "platform.hpp"
#include "roster.hpp"
#include "scene.hpp"
#include "skilltree.hpp"
#include "store.hpp"
#include "ui.hpp"

#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <log.hpp>
#include <monsters.hpp>
#include <object_sounds.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cctype>
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
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

auto Town::input(const Mouse& mouse, bool over_ui) const -> std::vector<Command> {
        std::vector<Command> out;
        if (view.dead && (!view.ghost || view.all_dead)) {   // a click, once the death has played, respawns
            if (mouse.press_this_frame) out.push_back(cmd::Resurrect{});
            return out;
        }
        if (over_ui) return out;
        // Screen -> world: invert the iso projection around the player,
        // who sits at (kW/2, kViewY).
        const float iso_u = float(mouse.x - int(kScreenWidth) / 2) / (kIsoW / 2);
        const float iso_v = float(mouse.y - kViewY) / (kIsoH / 2);
        const float world_x = cam_x + (iso_u + iso_v) / 2, world_y = cam_y + (iso_v - iso_u) / 2;
        if (view.ghost) {                                    // a ghost only walks
            if (mouse.press_this_frame || mouse.down) out.push_back(cmd::Move{ world_x, world_y, mouse.press_this_frame });
            return out;
        }
        const int hovered_monster_index = hovered_monster();
        const bool live = hovered_monster_index >= 0 && view.monsters[std::size_t(hovered_monster_index)].alive();
        if (mouse.press_this_frame) {
            if (live) out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, view.monsters[std::size_t(hovered_monster_index)].id, true, skillbar.left_item });
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ view.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0 || (hovered_npc <= -2000 && hovered_npc > -2000 - int(d2d::game::World::kGamePortals)) || (hovered_npc <= -3000 && hovered_npc > -3016)
                     || (hovered_npc <= -4000 && hovered_npc > -4000 - int(view.others.size())))
                out.push_back(cmd::Interact{ hovered_npc });
            else if (function_held(keys::kStandStill)) out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, -1, true, skillbar.left_item });   // 0x7a0664: no step, the skill there
            else out.push_back(cmd::Move{ world_x, world_y, true });
        } else if (mouse.down) {                             // held: the attack goes on, else the walk re-aims
            if (const int attacked = view.monster(view.attack); attacked >= 0 && view.monsters[std::size_t(attacked)].alive())
                out.push_back(cmd::UseSkill{ view.attack_skill, world_x, world_y, view.attack, true, view.attack_skill == skillbar.left ? skillbar.left_item : -1 });
            else if (function_held(keys::kStandStill))
                out.push_back(cmd::UseSkill{ skillbar.left, world_x, world_y, -1, true, skillbar.left_item });
            else
                out.push_back(cmd::Move{ world_x, world_y, false });
        }
        if (mouse.rpress_this_frame && skillbar.right == kResurrectSkill) {   // co-op: the body nearest the click
            if (const auto* body = body_near(world_x, world_y, 1.5f)) out.push_back(cmd::UseSkill{ kResurrectSkill, body->x, body->y, body->id });
        } else if (mouse.rpress_this_frame) {
            out.push_back(cmd::UseSkill{ skillbar.right, world_x, world_y, live ? view.monsters[std::size_t(hovered_monster_index)].id : -1, false, skillbar.right_item });
        }
        return out;
    }

auto Town::pad_field() const -> bool {
        return have_world && !inv_open && !char_open && !tree_open && !stash_open && !cube_open && !belt_open && !quest_log.open && !hireling.open && !help.open && !party.open
            && !game_menu.open && !controls.open && !waypoint.open && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && !held && identify_with < 0
            && !skillbar.picking && !chat_typing && !gold_box && !(net_game && net_game->trade.state != 0);
    }

// What the pad's A would act on now: a monster in reach in the facing (the
// held target first), else, with `uses` (a fresh press), the ground item,
// then the NPC, object, town portal or corpse in reach there.
auto Town::pad_aim(int skill, bool steering) const -> pad::Aim {
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        // Skills.txt's range is none / h2h / range / both; spells (Fire Ball: none) fire a missile.
        const bool ranged = info && (!info->srvmissile.empty() || info->range == "range");
        if (!ranged) return {};
        return { 12.f, 14.f, steering ? pad::Aim{}.cone_cos : -2.f };   // the stick left alone: any way round
    }

auto Town::pad_pick(const PadPlayer& player_pad, const UnitState& unit, bool uses, const pad::Aim& aim) const -> PadPick {
        const float hero_x = unit.x, hero_y = unit.y, face_x = player_pad.face_x, face_y = player_pad.face_y;
        std::vector<pad::Candidate> monsters;
        for (const auto& monster : view.monsters)
            if (monster.alive() && !monster.ally_not_interact(*scene)) monsters.push_back({ monster.id, monster.unit.x, monster.unit.y });   // not a converted one, as the cursor
        if (const int hit = pad::pick_target(hero_x, hero_y, face_x, face_y, monsters, player_pad.target, aim); hit >= 0)
            return { PadPick::kMonster, monsters[std::size_t(hit)].id, monsters[std::size_t(hit)].x, monsters[std::size_t(hit)].y };
        if (!uses) return {};
        std::vector<pad::Candidate> drops;
        for (const auto& item : view.ground) drops.push_back({ item.id, item.x, item.y });
        if (const int pick = pad::pick_target(hero_x, hero_y, face_x, face_y, drops, -1, pad::Aim{ 3.5f, 3.5f, -0.2f }); pick >= 0)
            return { PadPick::kItem, drops[std::size_t(pick)].id, drops[std::size_t(pick)].x, drops[std::size_t(pick)].y };
        std::vector<pad::Candidate> things;      // NPCs and objects you can use
        if (level)
            for (std::size_t i = 0; i < level->npcs.size(); ++i) {
                const auto& npc = level->npcs[i];
                const UnitState* state = i < view.npc_states.size() ? &view.npc_states[i] : nullptr;
                if ((state && state->hidden) || npc.name.empty()) continue;
                if (state && !state->mode.empty() && npc.root == "objects" && !(npc.selectable >> game::mode_index(state->mode) & 1)) continue;
                things.push_back({ int(i), state ? state->x : npc.x, state ? state->y : npc.y });
            }
        for (const auto& portal : view.portals)  // Interact's ids, as the mouse's hover names them (Cain's, 8, has no name)
            if (portal.which < int(d2d::game::World::kGamePortals)) things.push_back({ -2000 - portal.which, portal.x, portal.y });
        for (const auto& corpse : view.corpses) things.push_back({ -3000 - corpse.which, corpse.x, corpse.y });
        if (const int pick = pad::pick_target(hero_x, hero_y, face_x, face_y, things, -1, pad::Aim{ 4.5f, 4.5f, -0.2f }); pick >= 0)
            return { PadPick::kThing, things[std::size_t(pick)].id, things[std::size_t(pick)].x, things[std::size_t(pick)].y };
        return {};
    }

// The pad's buttons in the field: hit what is in reach in the facing, else
// pick up / talk to what is, else swing that way. Nothing is latched: a
// released button forgets its target. The aiming stick turns the facing
// (and so the target) without walking; else the walk does.
auto Town::pad_act(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, int left_skill, int right_skill, const Send& out, int left_item, int right_item) -> void {
        const auto& move = player_pad.move;
        if (move.aim || move.active) {
            const auto face = move.aim ? pad::world_step(move.aim_x, move.aim_y, kIsoW, kIsoH, 1.f) : pad::world_step(move.x, move.y, kIsoW, kIsoH, 1.f);
            player_pad.face_x = face.x; player_pad.face_y = face.y;
        }
        const bool want = move.attack || move.attack_down || move.alt;
        if (!want || dead || !field) { if (!want) player_pad.target = -1; return; }
        const float hero_x = unit.x, hero_y = unit.y, face_x = player_pad.face_x, face_y = player_pad.face_y;
        if (move.alt && right_skill == kResurrectSkill) {    // co-op: the body nearest, in reach
            if (const auto* body = body_near(hero_x, hero_y, 4.f)) out(cmd::UseSkill{ kResurrectSkill, body->x, body->y, body->id });
            return;
        }
        const bool left = !move.alt;
        const int skill = left ? left_skill : right_skill, item = left ? left_item : right_item;
        const PadPick pick = pad_pick(player_pad, unit, left && move.attack, pad_aim(skill, move.aim || move.active));
        if (pick.kind == PadPick::kMonster) {
            player_pad.target = pick.id;
            out(cmd::UseSkill{ skill, pick.x, pick.y, pick.id, left, item });
            return;
        }
        player_pad.target = -1;
        if (move.attack_down && !move.attack && !move.alt) return;   // held with nothing left to hit
        if (pick.kind == PadPick::kItem) { out(cmd::Pickup{ pick.id }); return; }
        if (pick.kind == PadPick::kThing) { out(cmd::Interact{ pick.id }); return; }
        // Nothing in reach: the left skill swings only with a monster about (in camp A never just walks); the right one always casts (buffs, teleport).
        const bool foe_near = std::ranges::any_of(view.monsters, [&](const auto& foe) { return foe.alive() && !foe.ally_not_interact(*scene) && std::hypot(foe.unit.x - hero_x, foe.unit.y - hero_y) < 10.f; });
        if (move.alt || (move.attack && foe_near))
            out(cmd::UseSkill{ skill, hero_x + face_x * 3.f, hero_y + face_y * 3.f, -1, left, item });
    }

auto Town::pad_walk(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, const Send& out) -> void {
        const bool walk = field && player_pad.move.active && !dead;
        if (walk) {
            const auto step = pad::world_step(player_pad.move.x, player_pad.move.y, kIsoW, kIsoH, 6.f);
            // Walk or run is the run toggle's (R, the left stick's click), as with the mouse.
            out(cmd::Move{ unit.x + step.x, unit.y + step.y, !player_pad.walking });
        } else if (player_pad.walking) {
            out(cmd::Move{ unit.x, unit.y, true });   // stick centred: stop here, not at the old destination
        }
        player_pad.walking = walk;
    }

auto Town::coop_seat(int seat, int colour, const pad::Sample& pad_now) -> void {
        if (!scene || std::ranges::contains(coop, seat, &CoopPlayer::seat)) return;
        std::vector<std::string> playing;
        for (const auto& in_game : world.players) playing.push_back(in_game->character.name);
        auto& waiting = coop.emplace_back();
        waiting.mapper = pad::Mapper{ pad_config };
        (void)waiting.mapper.step(pad_now, pad::Context::Menu, 0);
        waiting.seat = seat;
        waiting.colour = colour;
        waiting.roster = open_roster(*scene, playing, world.lead().character.header);
    }

auto Town::lead_leaves() -> int {
        // World::leave makes slot [1] the lead: the seat of whoever plays it, so their own pad keeps them.
        if (world.players.size() < 2) return -1;
        const auto next = std::ranges::find(coop, world.players[1]->id, &CoopPlayer::id);
        if (next == coop.end()) return -1;
        take_focus(hero.id);
        hand_items.erase(hero.id);
        if (scene && level) save_automap(automap, automap_dir(world), character.name, scene->map_seed, level->layer);
        if (const auto why = world.leave(hero.id); !why.empty()) d2d::log::warn("{}", why);
        // The next player's character is the screen's now (`hero` is slot [0], theirs).
        static_cast<Character&>(character) = hero.character;
        character.hardcore = hero.character.header.hardcore();
        character.selected = kSaveClassToUi[std::clamp(int(hero.character.header.cls), 0, 6)];
        skillbar.left = int(hero.character.header.left_skill); skillbar.right = int(hero.character.header.right_skill);
        load_hands(hero.id);
        skillbar_set = int(hero.character.header.weapon_set);
        const int seat = next->seat;
        lead_colour = next->colour;
        pad = next->pad;
        coop.erase(next);
        view_enc.reset();                             // the next View whole: another player's
        publish();
        return seat;
    }

// The level's objects' sounds as the client plays them (FUN_004cb460's object
// branch): the row a mode plays as an object enters it (none as it first
// shows), and its looping row while it's in that mode in sight (the 14 cells
// view_units draws: the unit is there), started again when it ends.
auto Town::object_sounds(const Audio& audio, std::uint32_t frame_ms, float sight_x, float sight_y) -> void {
        object_loops.clear();
        if (!level || !scene) return;
        if (object_level != level) { object_modes.assign(level->npcs.size(), {}); object_called.assign(level->npcs.size(), 0); object_level = level; }   // a new level: nothing has changed yet
        if (object_modes.size() != level->npcs.size()) { object_modes.resize(level->npcs.size()); object_called.resize(level->npcs.size()); }
        for (std::size_t i = 0; i < level->npcs.size() && i < view.npc_states.size(); ++i) {
            const auto& npc = level->npcs[i];
            const auto& state = view.npc_states[i];
            auto& was = object_modes[i];
            if (npc.root != "objects" || state.hidden || std::abs(state.x - sight_x) >= 14 || std::abs(state.y - sight_y) >= 14) { was = {}; continue; }
            const std::string_view mode = state.mode.empty() ? std::string_view("NU") : std::string_view(state.mode);
            const auto object_unit = d2d::game::unit_key(d2d::game::kSoundOnObject, int(i));
            if (!was.empty() && was != mode)
                cues.cue_on(object_unit, d2d::rules::object_sound(npc.object_id, d2d::game::mode_index(mode), d2d::game::mode_index(was)), frame_ms, state.x, state.y);
            was = mode;
            const auto subtile = [](float value) { return int(std::floor(value * 5)); };
            const int range = d2d::rules::unit_range(subtile(state.x) - subtile(sight_x), subtile(state.y) - subtile(sight_y), npc.size_x, 2);
            if (const int call = d2d::rules::object_call(npc.object_id, d2d::game::mode_index(mode), range, object_called[i] != 0); call > 0) {
                cues.cue_on(object_unit, call, frame_ms, state.x, state.y);
                object_called[i] = 1;
            }
            if (const int loop = d2d::rules::object_loop(npc.object_id, d2d::game::mode_index(mode)); loop > 0) {
                const int owner = -1 - int(i);
                object_loops.push_back(owner);
                if (!audio.owner_playing(owner) && !std::ranges::contains(cues.due, owner, &d2d::game::Cues::Cue::owner)) cues.cue_on(object_unit, loop, frame_ms, state.x, state.y, owner);
            }
        }
    }

auto Town::set_pad_config(const pad::Config& config) -> void {
        pad_config = config;
        lead_mapper = pad::Mapper{ config };
        for (auto& coop_player : coop) coop_player.mapper = pad::Mapper{ config };
    }

auto Town::coop_join(int seat, const Character& joining) -> void {
        auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) { coop.emplace_back().seat = seat; found = coop.end() - 1; found->mapper = pad::Mapper{ pad_config }; }
        found->id = world.join(joining).id;
        found->roster.reset();
    }

auto Town::coop_text(std::string_view text, bool backspace) -> void {
        for (auto& coop_player : coop)
            if (coop_player.roster && roster_typing(*coop_player.roster)) { roster_text(*coop_player.roster, text, backspace); return; }
    }

auto Town::coop_typing() const -> bool {
        return std::ranges::any_of(coop, [](const CoopPlayer& coop_player) { return coop_player.roster && roster_typing(*coop_player.roster); });
    }

auto Town::coop_enter() -> void {
        if (!scene) return;
        for (auto& coop_player : coop)
            if (coop_player.roster && roster_typing(*coop_player.roster)) {
                if (const auto choice = roster_input(*coop_player.roster, pad::Actions{ .keys = { pad::Key::Return } }, *scene)) (void)coop_choose(coop_player.seat, *choice);
                return;
            }
    }

auto Town::coop_leave(int seat) -> void {
        const auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) return;
        if (found->id == focus_id()) take_focus(hero.id);   // their screen goes back first
        if (found->id > 0)                              // still choosing: no one in the game to leave
            if (const auto why = world.leave(found->id); !why.empty()) d2d::log::warn("co-op seat {}: {}", seat, why);
        coop.erase(found);
    }

auto Town::coop_pad(int seat, const pad::Sample& sample, float seconds) -> void {
        const auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) return;
        auto& coop_player = *found;
        if (coop_player.roster) {                       // still choosing: the pad plays the roster
            const auto actions = coop_player.mapper.step(sample, pad::Context::Menu, seconds);
            if (const auto choice = scene ? roster_input(*coop_player.roster, actions, *scene) : std::nullopt) (void)coop_choose(seat, *choice);
            return;
        }
        const auto in_world = std::ranges::find(world.players, coop_player.id, [](const auto& each) { return each->id; });
        if (in_world == world.players.end() || coop_player.id == focus_id()) return;   // the screen's: main's pad pipeline plays it
        const auto opens = field_play(coop_player.pad, coop_player.mapper, **in_world, sample, seconds,
                                      [&](const Command& command) { coop_player.orders.push_back(command); });
        // A panel, the game menu or the skill picker: the screen is theirs, when no one else has it.
        if (opens.empty() || !focus_free()) return;
        take_focus(coop_player.id);
        for (const pad::Key key : opens)                // the keys that opened it, for this frame's UI
            if (key == pad::Key::Up) pad_open_picker(); // field_play's stand-in for the picker
            else focus_keys.push_back(pad_keycode(key));
    }

// A seat's roster choice made (in the game or the lobby): a save joins, a
// new character is made and saved at once (as char-create's OK). False
// for a name a player in the game or a save on disk has (char-create's).
auto Town::coop_choose(int seat, const RosterChoice& choice) -> bool {
        if (!scene) return false;
        const auto& lead = world.lead().character.header;
        if (choice.save >= 0) {
            coop_join(seat, character_of_save(*scene, std::size_t(choice.save)));
            return true;
        }
        const auto lower = [](char letter) { return std::tolower(static_cast<unsigned char>(letter)); };
        const auto same = [&](const std::string& name) { return std::ranges::equal(name, choice.name, {}, lower, lower); };
        if (std::ranges::any_of(world.players, [&](const auto& each) { return same(each->character.name); })
            || (world.characters && std::filesystem::exists(world.characters->path(choice.name)))) return false;
        coop_join(seat, made_character(*scene, choice.cls, choice.name, lead.hardcore(), lead.expansion(), rng));
        auto& made = *world.players.back();
        world.each_player([&](World::Player& each) {
            if (&each != &made) return;
            if (const auto why = world.save(); !why.empty()) d2d::log::warn("{}: {}", made.character.name, why);
        });
        return true;
    }

auto Town::lead_pad(const pad::Sample& sample, float seconds) -> void {
        (void)field_play(lead_field, lead_mapper, hero, sample, seconds, [&](const Command& command) { lead_orders.push_back(command); });
    }

auto Town::field_play(PadPlayer& player_pad, pad::Mapper& mapper, const World::Player& hero_there, const pad::Sample& sample, float seconds,
                      const Send& out) -> std::vector<pad::Key> {
        const auto actions = mapper.step(sample, pad::Context::Game, seconds);
        player_pad.move = { actions.move_x, actions.move_y, actions.move_active, actions.click_press, actions.click_down, actions.rclick_press,
                            actions.aim_active, actions.aim_x, actions.aim_y };
        std::vector<pad::Key> opens;
        for (const pad::Key key : actions.keys)
            switch (key) {
                case pad::Key::ToggleRun:   out(cmd::Run{ !hero_there.running }); break;
                case pad::Key::Belt1:       out(cmd::UseBelt{ 0 }); break;
                case pad::Key::Belt2:       out(cmd::UseBelt{ 1 }); break;
                case pad::Key::Belt3:       out(cmd::UseBelt{ 2 }); break;
                case pad::Key::Belt4:       out(cmd::UseBelt{ 3 }); break;
                case pad::Key::SwapWeapons: out(cmd::SwapWeapons{}); break;
                case pad::Key::Inventory: case pad::Key::Character: case pad::Key::SkillTree: case pad::Key::QuestLog:
                case pad::Key::Automap: case pad::Key::Escape:
                    opens.push_back(key); break;
                default: break;
            }
        if (actions.skill_picker) opens.push_back(pad::Key::Up);          // the right stick's click: the picker
        const bool dead = hero_there.pmode == game::kModeDT || hero_there.pmode == game::kModeDD;
        const bool field = !game_menu.open;
        pad_walk(player_pad, hero_there.player, dead && !hero_there.ghost, field, out);   // a ghost walks
        // Its hands' charged entries (kept while another player has the
        // screen); one gone since is Attack / 0, as the skillbar's drop_gone.
        auto& hands = hand_items[hero_there.id];
        int left_skill = int(hero_there.character.header.left_skill), right_skill = int(hero_there.character.header.right_skill);
        const auto entries = d2d::game::charged_skills(hero_there.character.items);
        const auto gone = [&](int skill, int item) { return item >= 0 && std::ranges::none_of(entries, [&](const d2d::game::ChargedSkill& had) { return had.item == item && had.skill == skill; }); };
        if (gone(left_skill, hands.left)) { left_skill = 0; hands.left = -1; }
        if (gone(right_skill, hands.right)) { right_skill = 0; hands.right = -1; }
        pad_act(player_pad, hero_there.player, dead, field, left_skill, right_skill, out, hands.left, hands.right);
        return opens;
    }

auto Town::focus_id() const -> int { return focus > 0 ? focus : hero.id; }

auto Town::body_near(float x, float y, float reach) const -> const View::Body* {
        const View::Body* best = nullptr;
        for (const auto& body : view.bodies)
            if (const float apart = std::hypot(body.x - x, body.y - y); apart <= reach && (!best || apart < std::hypot(best->x - x, best->y - y))) best = &body;
        return best;
    }

auto Town::focused() -> World::Player& {
        const auto found = std::ranges::find(world.players, focus_id(), [](const auto& each) { return each->id; });
        return found == world.players.end() ? hero : **found;
    }

auto Town::focus_free() const -> bool { return focus_id() == hero.id && pad_field() && !game_menu.open && !automap.open; }

auto Town::focus_seat() const -> int {
        const auto found = std::ranges::find(coop, focus_id(), &CoopPlayer::id);
        return found == coop.end() ? 0 : found->seat;
    }

auto Town::focus_colour() const -> int {
        const auto found = std::ranges::find(coop, focus_id(), &CoopPlayer::id);
        return found == coop.end() ? lead_colour : found->colour;
    }

auto Town::load_hands(int id) -> void {
        const auto kept = hand_items.contains(id) ? hand_items.at(id) : HandItems{};
        skillbar.left_item = kept.left; skillbar.right_item = kept.right; skillbar.hotkey_items = kept.hotkeys;
    }

auto Town::take_focus(int id) -> void {
        if (id == focus_id()) return;
        hand_items[focus_id()] = { skillbar.left_item, skillbar.right_item, skillbar.hotkey_items };   // the last owner's charged entries, kept
        // Whatever the last owner had open shuts (it's theirs, not the next one's).
        inv_open = char_open = stash_open = cube_open = tree_open = belt_open = false;
        quest_log.open = false; automap.open = false; skillbar.picking = 0;
        npc_menu = {}; speech = {}; waypoint = {}; store = {}; menu_after_speech = -1;
        focus = id == hero.id ? 0 : id;
        auto& owner = focused();
        static_cast<Character&>(character) = owner.character;
        character.hardcore = owner.character.header.hardcore();
        character.selected = kSaveClassToUi[std::clamp(int(owner.character.header.cls), 0, 6)];
        skillbar.left = int(owner.character.header.left_skill); skillbar.right = int(owner.character.header.right_skill);
        load_hands(id);
        skillbar_set = int(owner.character.header.weapon_set);
        quest_log.sent_known = false;
        pad = {};
        view_enc.reset();                             // the next View whole: another player's
        publish();
        d2d::log::info("co-op: the screen is {}'s", owner.character.name);
    }

auto Town::pad_focus() const -> std::optional<PadFocus> {
        if (controls.open) {                                            // Configure Controls: the selected row's column
            const auto [x, y] = controls.focus();
            return PadFocus{ 9000 + controls.sel * 2 + controls.column + (controls.pad_page ? 1000 : 0), x, y };
        }
        if (skillbar.picking) {                                         // the hand's skill now, else its button
            const bool on_left = skillbar.picking == 1;
            const auto rows = skillbar.rows(on_left);
            for (std::size_t row = 0; row < rows.size(); ++row)
                for (std::size_t col = 0; col < rows[row].size(); ++col)
                    if (rows[row][col] == skillbar.hand(on_left)) {
                        const auto [x, y] = skillbar.picker_at(rows, on_left, int(row), int(col));
                        return PadFocus{ 3000 + skillbar.picking, x + SkillBar::kIcon / 2, y - SkillBar::kIcon / 2 };
                    }
            return PadFocus{ 3000 + skillbar.picking, (on_left ? SkillBar::kButtonLeftX : SkillBar::kButtonRightX) + SkillBar::kIcon / 2, int(kScreenHeight) - SkillBar::kIcon / 2 };
        }
        if (game_menu.open) {                                           // its highlight: the d-pad steps it by key, the cursor follows
            const int count = int(game_menu.rows().size());
            return PadFocus{ 1000 + game_menu.menu * 64 + game_menu.sel, int(kScreenWidth) / 2, game_menu.top() + game_menu.step() * game_menu.sel + count + game_menu.step() / 2 };
        }
        if (waypoint.open) return PadFocus{ 4000 + waypoint.tab, kCharPanelX + 17 + 140, 60 + kWpHitTop[0] + 15 };   // the tab's first row
        if (npc_menu.npc >= 0) {
            int base = npc_menu.y;
            for (const auto& line : npc_menu.lines) {                   // the topmost line you can choose
                base += line.height;
                if (!line.header) return PadFocus{ 2000 + npc_menu.npc * 64 + int(npc_menu.lines.size()), npc_menu.x + npc_menu.box_width / 2, base - line.height / 2 };
            }
        }
        return std::nullopt;
    }

// Where a panel's d-pad hops the cursor: each item and free cell of the open
// grids, the body slots, and a store's stock (with an item held, the stock
// grid's middle: dropping it there sells it).
auto Town::pad_snap_points() const -> std::vector<pad::Point> {
        std::vector<pad::Point> points;
        if (!scene) return points;
        const auto centre = [&](int x, int y, int width, int height) { points.push_back({ x + width / 2, y + height / 2 }); };
        const auto& inv = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        std::vector<std::pair<const Scene::InvLayout*, int>> grids;
        if (inv_open) grids.emplace_back(&inv, d2d::d2s::item_panel::kInventory);
        if (cube_open) grids.emplace_back(&scene->cube_layout, d2d::d2s::item_panel::kCube);
        else if (stash_open) grids.emplace_back(&scene->stash_layout[character.expansion ? 1 : 0], d2d::d2s::item_panel::kStash);
        for (const auto& [layout, panel] : grids) {
            std::vector<bool> taken(std::size_t(layout->cols * layout->rows));
            for (const auto& item : character.items) {
                if (item.location != d2d::d2s::item_location::kStored || item.panel != panel) continue;
                const auto rect = grid_rect(*scene, *layout, item);
                centre(rect[0], rect[1], rect[2], rect[3]);
                const auto [width, height] = d2d::rules::item_size(scene->rules, item.code);
                for (int row = item.row; row < item.row + height && row < layout->rows; ++row)
                    for (int col = item.column; col < item.column + width && col < layout->cols; ++col) taken[std::size_t(row * layout->cols + col)] = true;
            }
            for (int row = 0; row < layout->rows; ++row)
                for (int col = 0; col < layout->cols; ++col)
                    if (!taken[std::size_t(row * layout->cols + col)])
                        centre(layout->grid_x + col * layout->box_w, layout->grid_y + row * layout->box_h, layout->box_w, layout->box_h);
        }
        if (inv_open)
            for (int slot = d2d::d2s::body_location::kFirst; slot <= d2d::d2s::body_location::kLast; ++slot)
                if (const auto& rect = inv.slots[std::size_t(slot)]; rect[2] > 0) centre(rect[0], rect[1], rect[2], rect[3]);
        if (skillbar.picking) {                                         // the picker's icons and both hands' buttons
            const bool on_left = skillbar.picking == 1;
            const auto rows = skillbar.rows(on_left);
            for (std::size_t row = 0; row < rows.size(); ++row)
                for (std::size_t col = 0; col < rows[row].size(); ++col) {
                    const auto [x, y] = skillbar.picker_at(rows, on_left, int(row), int(col));
                    centre(x, y - SkillBar::kIcon, SkillBar::kIcon, SkillBar::kIcon);
                }
            for (const int x : { SkillBar::kButtonLeftX, SkillBar::kButtonRightX }) centre(x, int(kScreenHeight) - SkillBar::kIcon, SkillBar::kIcon, SkillBar::kIcon);
        }
        if (tree_open) {                                                // the tab's skills and the three tabs
            const auto& list = scene->rules.class_skills[std::size_t(std::max(character.character_class, 0))];
            for (const auto& class_skill : list)
                if (class_skill.page == tree_tab)
                    if (const auto [x, y] = skill_icon_at(class_skill); x > -1000) centre(x, y - 0x30, 0x30, 0x30);
            for (const int top : { kTreeB - 0x174, kTreeB - 0x108, kTreeB - 0x9c }) centre(kTreeR - 0x58, top, 0x58, 0x6b);
        }
        if (char_open && character.stats.get(d2d::d2s::kStatPts) > 0)  // the stat buttons
            for (const auto& button : kStatButtons) centre(kCharPanelX + button.x, kCharPanelY + button.y - 22, 40, 22);
        if (quest_log.open) {                                           // the acts' tabs, the quests, Close and the last quest
            for (int act = 0; act < (character.expansion ? 5 : 4); ++act) centre(kCharPanelX + act * (character.expansion ? 0x40 : 0x50), kCharPanelY, character.expansion ? 0x40 : 0x50, 0x1c);
            for (const auto& slot : kQuestSlot) centre(kCharPanelX + slot.hit_x, kCharPanelY + slot.hit_y, 0x38, 0x34);
            centre(kCharPanelX + 0x116, kCharPanelY + 0x188, 0x24, 0x22);
            centre(kCharPanelX + 0xe6, kCharPanelY + 0x187, 0x1e, 0x21);
        }
        if (waypoint.open) {                                            // the open acts' tabs, the activated rows, Cancel
            for (int act = 0; act < (character.expansion ? 5 : 4); ++act)
                if (waypoint_act_open(character.header, act)) centre(kCharPanelX + act * (character.expansion ? 64 : 80), 60, character.expansion ? 64 : 80, 30);
            const auto& rows = scene->waypoint_levels[std::size_t(waypoint.tab)];
            for (std::size_t i = 0; i < rows.size() && i < 9; ++i)
                if (character.header.waypoint(character.header.active_difficulty(), rows[i].waypoint)) centre(kCharPanelX + 17, 60 + kWpHitTop[i], 280, 30);
            centre(kCharPanelX + 0x111, 60 + 0x183, 0x24, 0x22);
        }
        if (net_game && net_game->trade.state != 0)                     // a trade's buttons, gold and our offer's cells
            for (const auto& box : trade_boxes(*scene, net_game->trade.state)) centre(box[0], box[1], box[2] - box[0], box[3] - box[1]);
        if (belt_open) {                                                // the belt's boxes
            const auto& belt = scene->belts[std::size_t(belt_index(*scene, character.items))];
            for (int box = 0; box < belt.boxes; ++box) {
                const auto& edges = belt.box[std::size_t(box)];
                centre(edges[0], edges[2], edges[1] - edges[0], edges[3] - edges[2]);
            }
        }
        if (npc_menu.npc >= 0 && store.npc < 0) {                      // the lines you can choose
            int base = npc_menu.y;
            for (const auto& line : npc_menu.lines) {
                base += line.height;
                if (!line.header) points.push_back({ npc_menu.x + npc_menu.box_width / 2, base - line.height / 2 });
            }
        }
        if (store.npc >= 0) {
            if (held) centre(96, 123, 10 * 29, 10 * 29);
            else
                for (const auto& stock : store.tabs[std::size_t(store.tab)]) {
                    const auto [width, height] = d2d::rules::item_size(scene->rules, stock.code);
                    centre(96 + stock.column * 29, 123 + stock.row * 29, width * 29, height * 29);
                }
        }
        return points;
    }

// A store's X: the stock under the cursor bought, the inventory item under
// it sold, or the one held sold (as a drop on the stock grid).
auto Town::pad_trade(int mouse_x, int mouse_y) -> void {
        if (store.npc < 0 || !scene) return;
        if (held) { net.send(cmd::Sell{ held->id }); return; }
        if (const int stock = store_item_at(*scene, store, mouse_x, mouse_y); stock >= 0) { send(cmd::Buy{ stock, store.tab }); return; }
        const auto& inv = scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        for (const auto& item : character.items) {
            if (item.location != d2d::d2s::item_location::kStored || item.panel != d2d::d2s::item_panel::kInventory) continue;
            const auto rect = grid_rect(*scene, inv, item);
            if (mouse_x >= rect[0] && mouse_x < rect[0] + rect[2] && mouse_y >= rect[1] && mouse_y < rect[1] + rect[3]) { send(cmd::Sell{ item.id }); return; }
        }
    }

auto Town::tip_env() const -> d2d::rules::CalcEnv {
        d2d::rules::CalcEnv env{ [this](int id) { return d2d::game::skill_base_level(*scene, character, id); },
                                 [this](int id) { return skillbar.level(id); },   // with the skill shrine's +all skills
                                 {}, int(character.stats.get(d2d::d2s::kLevel)), nullptr };
        // The stats the calcs and masteries read: the gear's and the passives' (no weapon-type ones).
        auto sum = std::make_shared<d2d::rules::StatSum>();
        for (const auto& prop : d2d::game::gear_props(*scene, character.items))
            if (prop.stat >= 0 && std::size_t(prop.stat) < sum->size()) (*sum)[std::size_t(prop.stat)] += prop.value;
        for (const auto& passive : d2d::rules::passive_stats(scene->skills, env))
            if (passive.itype.empty() && passive.stat >= 0 && std::size_t(passive.stat) < sum->size()) (*sum)[std::size_t(passive.stat)] += passive.value;
        env.stat = [stat_sum = std::move(sum)](int id) { return id >= 0 && std::size_t(id) < stat_sum->size() ? int((*stat_sum)[std::size_t(id)]) : 0; };
        return env;
    }

auto Town::pad_back(int mouse_x, int mouse_y) const -> bool {
        if (gold_box) return true;                                   // the gold box: B is its Cancel
        if (!scene || held || !(stash_open || cube_open)) return false;
        const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                            { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse_x, mouse_y);
        return !(click.cmd && std::holds_alternative<cmd::ToCursor>(*click.cmd));
    }

// The field's right stick click: the right hand's skill picker (A on the
// left skill button switches to the left's), the cursor on its skill.
auto Town::pad_open_picker() -> void {
        if (pad_field()) skillbar.picking = 2;
    }

// A store's shoulders: the next tab with stock on it, that way round.
auto Town::pad_tab(int step) -> void {
        for (int i = 1; i < 4; ++i)
            if (const int tab = ((store.tab + step * i) % 4 + 4) % 4; !store.tabs[std::size_t(tab)].empty()) { store.tab = tab; return; }
    }

}  // namespace d2d::client
