// SPDX-License-Identifier: GPL-3.0-or-later
// ClientSession (session.hpp): input: the mouse, gamepads, local co-op seats and focus. The rest is in
// this directory.
#include "session.hpp"

#include "audio/audio.hpp"
#include "common.hpp"
#include "frontend/roster.hpp"
#include "input/gamepad.hpp"
#include "input/gamepad_map.hpp"
#include "input/hand_hold.hpp"
#include "input/hold_confirm.hpp"
#include "input/keyconfig.hpp"
#include "input/pad_hold.hpp"
#include "input/touch/input.hpp"
#include "internal.hpp"
#include "items/cursor.hpp"
#include "items/identify_prompt.hpp"
#include "items/item_use.hpp"
#include "load.hpp"
#include "panels/belt_popup.hpp"
#include "panels/controls.hpp"
#include "panels/panels.hpp"
#include "panels/party.hpp"
#include "panels/store.hpp"
#include "platform.hpp"
#include "render/ingame.hpp"
#include "scene.hpp"
#include "skills/pad.hpp"
#include "skills/tree.hpp"
#include "skills/tree_close.hpp"
#include "ui.hpp"

#include <ai/units.hpp>
#include <click_spot.hpp>
#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <inventory.hpp>
#include <item_gen.hpp>
#include <item_text.hpp>
#include <level_ids.hpp>
#include <log.hpp>
#include <monsters.hpp>
#include <object_sounds.hpp>
#include <rules.hpp>
#include <search_enemy.hpp>
#include <skills.hpp>
#include <wear.hpp>

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
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::client {

auto ClientSession::input(const Mouse& mouse, bool over_ui) const -> std::vector<Command> {
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
        const int hovered_monster_index = hovered_monster();
        const bool live = hovered_monster_index >= 0 && view.monsters[std::size_t(hovered_monster_index)].alive();
        // A hand's skill at the cursor, at `unit` or the ground (-1): the press's FUN_004629a0 (left) /
        // FUN_00462ba0 (right), and a held frame's FUN_00462a20 / FUN_00462c20, all into FUN_004625b0.
        // The skill's own search may name one first (FUN_004621d0 -> FUN_00467880): SearchEnemyNear
        // on any click, and it alone (near_pick); else with no unit there and shift (stand still)
        // held SearchEnemyXY (shift_pick), then SearchOpenXY. The command says shift was held.
        // None of them runs unless the skill can go now (FUN_004d9fc0 0: View::hand_status), nor on a
        // held frame of the left hand without shift while the player walks (FUN_00467880's first
        // gate: flags 8 and 1, FUN_00622d00; rules::skill_search_goes).
        const auto use_hand = [&](bool left, int unit, bool held_frame = false) {
            const int skill = left ? skillbar.left : skillbar.right;
            const bool stand = function_held(keys::kStandStill);
            float at_x = world_x, at_y = world_y;
            const std::uint32_t flags = (held_frame ? d2d::rules::kClickHeld : d2d::rules::kClickPress) | (left ? d2d::rules::kClickLeft : 0) | (stand ? d2d::rules::kClickShift : 0);
            if (d2d::rules::skill_search_goes(flags, view.player.walking) && view.hand_status[left ? 0 : 1] == 0 && !near_pick(skill, unit, at_x, at_y)) {
                if (unit < 0 && stand) unit = shift_pick(skill);
                if (const auto spot = open_spot(skill, unit, world_x, world_y)) {   // off a wall, at the ground
                    std::tie(at_x, at_y) = *spot;
                    unit = -1;
                }
            }
            out.push_back(cmd::UseSkill{ skill, at_x, at_y, unit, left, left ? skillbar.left_item : skillbar.right_item, stand });
        };
        // A held frame (hand_hold.hpp; FUN_00462930 lets it through). False: the left hand walks instead.
        const auto hold_hand = [&](bool left) {
            const int target = view.monster(click_target);
            switch (hand_hold::held(left, target >= 0 && view.monsters[std::size_t(target)].alive(), function_held(keys::kStandStill))) {
                case hand_hold::Held::kLatched: use_hand(left, click_target, true); return true;
                case hand_hold::Held::kSkill: use_hand(left, -1, true); return true;
                case hand_hold::Held::kWalk:  return false;
            }
            return false;
        };
        // The right press: Resurrect (co-op) takes the body nearest the click; any other skill goes at the cursor.
        const auto right_press = [&] {
            if (skillbar.right != kResurrectSkill) use_hand(false, live ? view.monsters[std::size_t(hovered_monster_index)].id : -1);
            else if (const auto* body = body_near(world_x, world_y, 1.5f)) out.push_back(cmd::UseSkill{ kResurrectSkill, body->x, body->y, body->id });
        };
        if (touch_enabled && last_device == touch::LastDevice::Touch) {
            // Touch stick and ground taps own locomotion; a leftover mouse packet must not click-to-walk.
            if (!(pad_field() && !touch_pointer.active)) return out;
            if (mouse.rpress_this_frame) right_press();
            return out;
        }
        // A walk on the ground (FUN_004623c0): in whole subtiles from the unit's,
        // the spot goes out to a least distance (3 a press, 4 held, 5 running;
        // rules::click_spot), and none goes on the unit's own subtile. The run
        // bit is the run key or toggle with stamina left (FUN_0044bec0,
        // FUN_00462d00), as the run button draws it.
        const auto ground_move = [&](bool press, std::vector<Command>& into) {
            const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
            const bool run = view.running && character.stats.values[d2d::d2s::kStamina] > 0;
            const std::uint32_t flags = (press ? d2d::rules::kClickPress : d2d::rules::kClickHeld) | (run ? d2d::rules::kClickRun : 0);
            const int click_x = subtile(world_x), click_y = subtile(world_y);
            const auto spot = d2d::rules::click_spot(subtile(view.player.x), subtile(view.player.y), click_x, click_y, flags);
            if (!spot) return;
            if (spot->x == click_x && spot->y == click_y) into.push_back(cmd::Move{ world_x, world_y, press });
            else into.push_back(cmd::Move{ (float(spot->x) + 0.5f) / 5.f, (float(spot->y) + 0.5f) / 5.f, press });   // the spot's subtile centre
        };
        if (view.ghost) {                                    // a ghost only walks
            if (mouse.press_this_frame || mouse.down) ground_move(mouse.press_this_frame, out);
            return out;
        }
        if (mouse.press_this_frame) {
            if (live) use_hand(true, view.monsters[std::size_t(hovered_monster_index)].id);
            else if (hovered_ground() >= 0) out.push_back(cmd::Pickup{ view.ground[std::size_t(hovered_ground())].id });
            else if (hovered_npc >= 0 || (hovered_npc <= -2000 && hovered_npc > -2000 - int(d2d::game::World::kGamePortals)) || (hovered_npc <= -3000 && hovered_npc > -3016)
                     || (hovered_npc <= -4000 && hovered_npc > -4000 - int(view.others.size())))
                out.push_back(cmd::Interact{ hovered_npc });
            else if (function_held(keys::kStandStill)) use_hand(true, -1);   // 0x7a0664: no step, the skill there
            else ground_move(true, out);
        } else if (mouse.down) {                             // held: the attack goes on, else the walk re-aims
            if (!hold_hand(true) && !hold_interact)          // with an interact queued the hold does nothing (FUN_00462a20, LAB_00462b1d)
                ground_move(false, out);
        }
        if (mouse.rpress_this_frame) right_press();
        else if (mouse.rdown && right_hold && skillbar.right != kResurrectSkill) hold_hand(false);
        return out;
    }

// A shift-click at the ground with a SearchEnemyXY skill (Attack, Bash, Jab, Zeal...): game.exe's
// client first takes a monster on one of five spots about 3 subtiles from the player, its facing's
// way first (FUN_00467880 -> FUN_00467660, rules::search_enemy_xy; cursor-units.md "The skill's
// own target search"). A spot counts with a unit's own collision bit on it (0x180: a player's or
// a live monster's plus, a SizeX 3 monster's 3 x 3). The monster's id, or -1.
// Only at skill status 0 (FUN_004d9fc0, View::hand_status: use_hand).
// ponytail: a walking player faces its path's target there (FUN_006238c0), the units go in the
// near rooms' list order (FUN_00641cb0), pets aren't among the units (view.monsters), and the
// press flag 2 that also asks for the search isn't made.
auto ClientSession::shift_pick(int skill) const -> int {
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        if (!info || !info->search_enemy_xy) return -1;
        const auto subtile = [](d2d::game::Fine fine) { return int(fine.raw >> 16); };
        const int level_id = level ? level->id : 0;
        std::vector<d2d::rules::SearchUnit> units;
        units.reserve(view.monsters.size());
        for (const auto& monster : view.monsters) {
            const bool known = monster.type >= 0 && std::size_t(monster.type) < scene->monsters.types.size();
            const bool shift_sel = known && scene->monsters.types[std::size_t(monster.type)].shift_sel;
            units.push_back({ subtile(monster.unit.x), subtile(monster.unit.y), monster.unit.shape.size, 1, monster.alive() ? 1 : 0, monster.targetable(), shift_sel,
                              !monster.ally_not_interact(*scene, level_id) });
        }
        const auto stamped = [&subtile](const UnitState& unit, int spot_x, int spot_y) {
            if (!unit.shape.stamps()) return false;
            const int across = std::abs(spot_x - subtile(unit.x)), down = std::abs(spot_y - subtile(unit.y));
            return unit.shape.box() ? across <= 1 && down <= 1 : across + down <= 1;
        };
        const auto unit_bits = [&](int spot_x, int spot_y) {
            return stamped(view.player, spot_x, spot_y)
                || std::ranges::any_of(view.others, [&](const auto& other) { return !other.ghost && stamped(other.unit, spot_x, spot_y); })
                || std::ranges::any_of(view.monsters, [&](const auto& monster) { return monster.alive() && stamped(monster.unit, spot_x, spot_y); });
        };
        // The facing in 64 (path +0x65, FUN_00648810); one drawn apart from it is the 16 way's.
        const auto& self = view.player;
        int facing = self.facing.goal;
        if (d2d::game::missile_direction(self.facing.now, 16) != self.dir) {
            const auto sector = std::ranges::find(d2d::game::kDir16FromSector, self.dir) - std::begin(d2d::game::kDir16FromSector);
            facing = int(sector * 4) & 0x3f;
        }
        const int pick = d2d::rules::search_enemy_xy(facing, subtile(self.x), subtile(self.y), info->targetable_only, units, unit_bits);
        return pick < 0 ? -1 : view.monsters[std::size_t(pick)].id;
    }

// A SearchEnemyNear skill (Raise Skeleton, Corpse Explosion, Iron Golem, Psychic Hammer, Fist of the
// Heavens, Taunt...): game.exe's client takes the unit nearest the click under 10 (FUN_00467880 ->
// FUN_00467490, rules::search_enemy_near; cursor-units.md "The skill's own target search"), from
// the target monster's subtile when there is one: a live monster, or with a TargetCorpse skill a
// corpse in DD; Iron Golem a ground item of a metal base (Items bitfield1 & 2), at any distance.
// None leaves the target and the click as they were. The spot goes to the pick's subtile centre.
// ponytail: the units go in view order, not the near rooms' list order
// (FUN_0065ac70), so equals tie apart; other players aren't among them (two players without the
// hostility flag fail FUN_00466870's FUN_00465c60); a corpse's hit test is usable_corpse (the
// target kind callbacks at 0x711f34, FUN_00464310, aren't traced per skill); Iron Golem's item goes
// as its spot, and the World takes the one there (Fight::golem_item_at), not by its id.
auto ClientSession::near_pick(int skill, int& unit, float& at_x, float& at_y) const -> bool {
        constexpr int kIronGolemSkill = 0x5a;   // FUN_00467490's FUN_00643ce0 == 0x5a
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        if (!info || !info->search_enemy_near) return false;
        const auto subtile = [](d2d::game::Fine fine) { return int(fine.raw >> 16); };
        const auto cell_subtile = [](float cells) { return int(std::floor(cells * 5)); };
        const int level_id = level ? level->id : 0;
        const bool iron_golem = skill == kIronGolemSkill;
        std::vector<d2d::rules::NearUnit> units;
        units.reserve(view.monsters.size() + (iron_golem ? view.ground.size() : 0));
        for (const auto& monster : view.monsters) {
            const bool alive = monster.alive();
            const int mode = alive ? 1 : monster.mode == "DD" ? 0xc : 0;
            const bool hit = !monster.ally_not_interact(*scene, level_id) && (alive ? monster.targetable() : info->target_corpse && monster.usable_corpse());
            units.push_back({ 1, subtile(monster.unit.x), subtile(monster.unit.y), mode, false, hit, false });
        }
        if (iron_golem)
            for (const auto& ground : view.ground) {
                const auto base = scene->rules.item_base.find(ground.item.code);
                const bool metal = base != scene->rules.item_base.end() && (base->second.bitfield1 & 2) != 0;
                units.push_back({ 4, cell_subtile(ground.x), cell_subtile(ground.y), 0, false, true, metal });
            }
        int spot_x = cell_subtile(at_x), spot_y = cell_subtile(at_y);
        const int pick = d2d::rules::search_enemy_near(info->target_corpse, iron_golem, d2d::rules::level_ids::is_town(level_id), -1, view.monster(unit),
                                                       spot_x, spot_y, units);
        if (pick < 0) return true;
        const auto& found = units[std::size_t(pick)];
        unit = std::size_t(pick) < view.monsters.size() ? view.monsters[std::size_t(pick)].id : -1;
        at_x = (float(found.x) + 0.5f) / 5;
        at_y = (float(found.y) + 0.5f) / 5;
        return true;
    }

// A SearchOpenXY skill (Fire Wall, Meteor, Blizzard, Eruption, Volcano) cast at a wall: game.exe's
// client moves it to the nearest subtile without bit 1 within 7 and drops the target for the
// ground (FUN_00467880 -> FUN_004677b0, game::search_open_xy; cursor-units.md "The skill's own
// target search"). From the target monster's subtile, else the click's. The spot's centre, or none.
// A dead target becoming the ground first (FUN_00467880, FUN_00466de0) can't happen here: the
// client only passes a live monster (hover, or a held frame's latched one while it lives).
auto ClientSession::open_spot(int skill, int unit, float click_x, float click_y) const -> std::optional<std::pair<float, float>> {
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        if (!info || !info->search_open_xy || !level) return std::nullopt;
        int spot_x = int(std::floor(click_x * 5)), spot_y = int(std::floor(click_y * 5));
        if (const int target = view.monster(unit); target >= 0) {
            const auto& monster = view.monsters[std::size_t(target)].unit;
            spot_x = int(monster.x.raw >> 16);
            spot_y = int(monster.y.raw >> 16);
        }
        const auto flags = [&](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 1) ? 1 : 0; };
        const auto spot = d2d::game::search_open_xy(spot_x, spot_y, flags);
        if (!spot) return std::nullopt;
        return std::pair{ (float(spot->first) + 0.5f) / 5, (float(spot->second) + 0.5f) / 5 };
    }

// Whether the mouse's move goes out, as game.exe's client lets it
// (collision.md "What a held frame sends"). On a warp: a held frame does
// nothing while the player walks (FUN_00462930), and a click goes once
// each 500 ms (FUN_00461dc0's type 5), a held one after a stop as a fresh
// click. On the ground a held frame of a walking player goes each 7 ticks
// (FUN_004623c0). The World tells a warp's click by its spot, so this does.
auto ClientSession::click_goes(cmd::Move& move, std::uint32_t frame_ms) -> bool {
        const int mode = view.player.walking ? 2 : 1;
        if (level && std::ranges::any_of(level->warps, [&](const auto& warp) { return d2d::game::World::warp_clicked(warp, move.x, move.y); })) {
            if (!move.fresh && !d2d::rules::held_frame_goes(d2d::rules::kUnitWarp, mode)) return false;
            if (!d2d::rules::warp_click_goes(frame_ms, warp_click_until)) return false;
            warp_click_until = frame_ms + d2d::rules::kWarpClickWaitMs;
            move.fresh = true;
            return true;
        }
        const std::uint32_t tick = frame_ms / d2d::game::kTickMs;
        if (!d2d::rules::held_walk_goes(move.fresh ? d2d::rules::kClickPress : d2d::rules::kClickHeld, mode, tick, held_walk_tick)) return false;
        held_walk_tick = tick;
        return true;
    }

/**
 * Report whether gameplay input owns the field. Open panels, assignment editors, held items,
 * dialogs and chat prevent field actions from leaking through UI interaction.
 */
auto ClientSession::pad_field() const -> bool {
        return have_world && !inv_open && !char_open && !tree_open && !stash_open && !cube_open && !belt_open && !belt_up.shown && !quest_log.open && !hireling.open && !help.open && !party.open
            && !game_menu.open && !controls.open && !waypoint.open && npc_menu.npc < 0 && speech.npc < 0 && store.npc < 0 && !held && identify_with < 0
            && !skillbar.picking && skill_strip < 0 && touch_assign == skillpad::kUnset && touch_bind == TouchBindPhase::Off && !chat_typing && !gold_box
            && !(net_game && net_game->trade.state != 0);
    }

auto ClientSession::mini_right() const -> bool {
        if (inv_open || tree_open) return true;
        if (!scene) return false;
        const int boxes = scene->belts[std::size_t(belt_index(*scene, character.items))].boxes;
        return belt_popup::moves_mini_panel(belt_up, belt_open, boxes);
    }

// What the pad's A would act on now: a monster in reach in the facing (the
// held target first), else, with `uses` (a fresh press), the ground item,
// then the NPC, object, town portal or corpse in reach there.
auto ClientSession::pad_aim(int skill, bool steering) const -> pad::Aim {
        const auto* info = scene ? scene->skills.get(skill) : nullptr;
        // Skills.txt's range is none / h2h / range / both; spells (Fire Ball: none) fire a missile.
        const bool ranged = info && (!info->srvmissile.empty() || info->range == "range");
        if (!ranged) return {};
        return { 12.f, 14.f, steering ? pad::Aim{}.cone_cos : -2.f };   // the stick left alone: any way round
    }

auto ClientSession::pad_pick(const PadPlayer& player_pad, const UnitState& unit, bool uses, const pad::Aim& aim, bool monsters) const -> PadPick {
        const float hero_x = unit.x, hero_y = unit.y, face_x = player_pad.face_x, face_y = player_pad.face_y;
        if (monsters) {
            std::vector<pad::Candidate> foes;
            for (const auto& monster : view.monsters)
                if (monster.alive() && !monster.ally_not_interact(*scene, level ? level->id : 0)) foes.push_back({ monster.id, monster.unit.x, monster.unit.y });   // not a converted one, as the cursor
            if (const int hit = pad::pick_target(hero_x, hero_y, face_x, face_y, foes, player_pad.target, aim); hit >= 0)
                return { PadPick::kMonster, foes[std::size_t(hit)].id, foes[std::size_t(hit)].x, foes[std::size_t(hit)].y };
        }
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
                const std::string_view object_mode = state && !state->mode.empty() ? std::string_view(state->mode) : std::string_view(npc.mode);
                if (npc.root == "objects" && !(npc.selectable >> game::mode_index(object_mode) & 1)) continue;
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
auto ClientSession::pad_act(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, int left_skill, int right_skill, const Send& out, int left_item, int right_item) -> void {
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
        // A hold keeps sending the skill, so the attack continues after this swing.
        // It does not pick up or talk.
        if (move.attack_down && !move.attack && !move.alt) {
            if (!(level && d2d::rules::level_ids::is_town(level->id)))
                out(cmd::UseSkill{ skill, hero_x + face_x * 3.f, hero_y + face_y * 3.f, -1, left, item });
            return;
        }
        if (pick.kind == PadPick::kItem) { out(cmd::Pickup{ pick.id }); return; }
        if (pick.kind == PadPick::kThing) { out(cmd::Interact{ pick.id }); return; }
        // Nothing in reach: the left skill swings only with a monster about (in camp A never just walks); the right one always casts (buffs, teleport).
        const bool foe_near = std::ranges::any_of(view.monsters, [&](const auto& foe) { return foe.alive() && !foe.ally_not_interact(*scene, level ? level->id : 0) && std::hypot(foe.unit.x - hero_x, foe.unit.y - hero_y) < 10.f; });
        if (move.alt || (move.attack && foe_near))
            out(cmd::UseSkill{ skill, hero_x + face_x * 3.f, hero_y + face_y * 3.f, -1, left, item });
    }

auto ClientSession::pad_walk(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, const Send& out) -> void {
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

auto ClientSession::coop_seat(int seat, int colour, const pad::Sample& pad_now) -> void {
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

auto ClientSession::lead_leaves() -> int {
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
        slide_from.clear();                          // a new View, not the next tick: nothing slides into it
        publish();
        return seat;
    }

// The level's objects' sounds as the client plays them (FUN_004cb460's object
// branch): the row a mode plays as an object enters it (none as it first
// shows), and its looping row while it's in that mode in sight (the 14 cells
// view_units draws: the unit is there), started again when it ends.
auto ClientSession::object_sounds(const Audio& audio, std::uint32_t frame_ms, float sight_x, float sight_y) -> void {
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

auto ClientSession::set_pad_config(const pad::Config& config) -> void {
        pad.teleport.clear();
        pad_config = config;
        lead_mapper = pad::Mapper{ config };
        for (auto& coop_player : coop) coop_player.mapper = pad::Mapper{ config };
    }

auto ClientSession::coop_join(int seat, const Character& joining) -> void {
        auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) { coop.emplace_back().seat = seat; found = coop.end() - 1; found->mapper = pad::Mapper{ pad_config }; }
        found->id = world.join(joining).id;
        found->roster.reset();
        load_skill_sheet(joining.name);
    }

auto ClientSession::coop_text(std::string_view text, bool backspace) -> void {
        for (auto& coop_player : coop)
            if (coop_player.roster && roster_typing(*coop_player.roster)) { roster_text(*coop_player.roster, text, backspace); return; }
    }

auto ClientSession::coop_typing() const -> bool {
        return std::ranges::any_of(coop, [](const CoopPlayer& coop_player) { return coop_player.roster && roster_typing(*coop_player.roster); });
    }

auto ClientSession::coop_enter() -> void {
        if (!scene) return;
        for (auto& coop_player : coop)
            if (coop_player.roster && roster_typing(*coop_player.roster)) {
                if (const auto choice = roster_input(*coop_player.roster, pad::Actions{ .keys = { pad::Key::Return } }, *scene)) (void)coop_choose(coop_player.seat, *choice);
                return;
            }
    }

auto ClientSession::coop_leave(int seat) -> void {
        const auto found = std::ranges::find(coop, seat, &CoopPlayer::seat);
        if (found == coop.end()) return;
        if (found->id == focus_id()) take_focus(hero.id);   // their screen goes back first
        if (found->id > 0)                              // still choosing: no one in the game to leave
            if (const auto why = world.leave(found->id); !why.empty()) d2d::log::warn("co-op seat {}: {}", seat, why);
        coop.erase(found);
    }

auto ClientSession::coop_pad(int seat, const pad::Sample& sample, float seconds) -> void {
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
        // This pad's own flag. Another seat's field_play cannot clear it.
        if (std::exchange(coop_player.pad.wants_strip, false) && focus_free()) { take_focus(coop_player.id); skill_strip = 0; return; }
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
auto ClientSession::coop_choose(int seat, const RosterChoice& choice) -> bool {
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

auto ClientSession::lead_pad(const pad::Sample& sample, float seconds) -> void {
        (void)field_play(lead_field, lead_mapper, hero, sample, seconds, [&](const Command& command) { lead_orders.push_back(command); });
        lead_field.wants_strip = false;            // the lead's Start does not take the screen back
    }

/**
 * Resolve controller movement and field action commands for the selected player. A fresh press
 * permits interaction/pickup; held combat does not acquire loot after a kill.
 */
auto ClientSession::field_play(PadPlayer& player_pad, pad::Mapper& mapper, const World::Player& hero_there, const pad::Sample& sample, float seconds,
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
        if (actions.skill_picker) opens.push_back(pad::Key::Up);          // the picker: the wheel when skill buttons are on
        player_pad.wants_strip = actions.panel_strip;
        const bool dead = hero_there.pmode == game::kModeDT || hero_there.pmode == game::kModeDD;
        const bool field = !game_menu.open;
        pad_walk(player_pad, hero_there.player, dead && !hero_there.ghost, field, out);   // a ghost walks
        if (mapper.config().skill_buttons) {
            apply_skill_pad(player_pad, hero_there.player, dead, field, hero_there.character.name, int(hero_there.character.header.right_skill),
                            hero_there.character.header.expansion(), actions, out);
            static constexpr std::array<pad::Key, 5> kPanelKeys = {
                pad::Key::Character, pad::Key::Inventory, pad::Key::SkillTree, pad::Key::Automap, pad::Key::QuestLog };
            const int panel = std::exchange(player_pad.wants_panel, -1);
            if (panel >= 0 && panel < int(kPanelKeys.size())) opens.push_back(kPanelKeys[std::size_t(panel)]);
            return opens;
        }
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

auto ClientSession::focus_id() const -> int { return focus > 0 ? focus : hero.id; }

auto ClientSession::body_near(float x, float y, float reach) const -> const View::Body* {
        const View::Body* best = nullptr;
        for (const auto& body : view.bodies)
            if (const float apart = std::hypot(body.x - x, body.y - y); apart <= reach && (!best || apart < std::hypot(best->x - x, best->y - y))) best = &body;
        return best;
    }

auto ClientSession::focused() -> World::Player& {
        const auto found = std::ranges::find(world.players, focus_id(), [](const auto& each) { return each->id; });
        return found == world.players.end() ? hero : **found;
    }

auto ClientSession::focus_free() const -> bool { return focus_id() == hero.id && pad_field() && !game_menu.open && !automap.open; }

auto ClientSession::focus_seat() const -> int {
        const auto found = std::ranges::find(coop, focus_id(), &CoopPlayer::id);
        return found == coop.end() ? 0 : found->seat;
    }

auto ClientSession::focus_colour() const -> int {
        const auto found = std::ranges::find(coop, focus_id(), &CoopPlayer::id);
        return found == coop.end() ? lead_colour : found->colour;
    }

auto ClientSession::load_hands(int id) -> void {
        const auto kept = hand_items.contains(id) ? hand_items.at(id) : HandItems{};
        skillbar.left_item = kept.left; skillbar.right_item = kept.right; skillbar.hotkey_items = kept.hotkeys;
    }

auto ClientSession::take_focus(int id) -> void {
        if (id == focus_id()) return;
        hand_items[focus_id()] = { skillbar.left_item, skillbar.right_item, skillbar.hotkey_items };   // the last owner's charged entries, kept
        // Whatever the last owner had open shuts (it's theirs, not the next one's).
        inv_open = char_open = stash_open = cube_open = tree_open = belt_open = false;
        quest_log.open = false; automap.open = false; skillbar.picking = 0;
        npc_menu = {}; speech = {}; waypoint = {}; store = {}; use_item = {}; menu_after_speech = -1;
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
        slide_from.clear();                          // a new View, not the next tick: nothing slides into it
        publish();
        d2d::log::info("co-op: the screen is {}'s", owner.character.name);
    }

/**
 * Return the current panel control for controller focus, or no focus for a self-highlighting
 * radial. Prefer a skill icon when entering the skill tree.
 */
auto ClientSession::pad_focus() const -> std::optional<PadFocus> {
        if (controls.open) {                                            // Configure Controls: the selected row's column
            const auto [focus_x, focus_y] = controls.focus();
            return PadFocus{ 9000 + controls.sel * 2 + controls.column + (controls.pad_page ? 1000 : 0), focus_x, focus_y };
        }
        if (skillbar.picking == 3) return std::nullopt;                // the wheel draws its own highlight
        if (skillbar.picking) {                                         // the hand's skill now, else its button
            const bool on_left = skillbar.picking == 1;
            const auto rows = skillbar.rows(on_left);
            for (std::size_t row = 0; row < rows.size(); ++row)
                for (std::size_t col = 0; col < rows[row].size(); ++col)
                    if (rows[row][col] == skillbar.hand(on_left)) {
                        const auto [icon_x, icon_y] = skillbar.picker_at(rows, on_left, int(row), int(col));
                        return PadFocus{ 3000 + skillbar.picking, icon_x + SkillBar::kIcon / 2, icon_y - SkillBar::kIcon / 2 };
                    }
            return PadFocus{ 3000 + skillbar.picking, (on_left ? SkillBar::kButtonLeftX : SkillBar::kButtonRightX) + SkillBar::kIcon / 2, int(kScreenHeight) - SkillBar::kIcon / 2 };
        }
        if (tree_open && scene) {
            for (const auto& node : scene->rules.class_skills[std::size_t(std::max(character.character_class, 0))])
                if (node.page == tree_tab) {
                    const auto [pixel_x, pixel_y] = skill_icon_at(node);
                    if (pixel_x > -1000) return PadFocus{ 7000 + tree_tab, pixel_x + 24, pixel_y - 24 };
                }
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
                if (!line.header) return PadFocus{ 2000 + npc_menu.npc * 64 + int(npc_menu.lines.size()), npc_menu.x + npc_menu.box_width / 2, base - 4 };
            }
        }
        return std::nullopt;
    }

// Where a panel's d-pad hops the cursor: each item and free cell of the open
// grids, the body slots, and a store's stock (with an item held, the stock
// grid's middle: dropping it there sells it).
/**
 * Collect valid controller cursor destinations for the currently open panel. Skill-tree entries and
 * its close button share the same geometry as drawing and hit testing.
 */
auto ClientSession::pad_snap_points() const -> std::vector<pad::Point> {
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
                    const auto [icon_x, icon_y] = skillbar.picker_at(rows, on_left, int(row), int(col));
                    centre(icon_x, icon_y - SkillBar::kIcon, SkillBar::kIcon, SkillBar::kIcon);
                }
            for (const int x : { SkillBar::kButtonLeftX, SkillBar::kButtonRightX }) centre(x, int(kScreenHeight) - SkillBar::kIcon, SkillBar::kIcon, SkillBar::kIcon);
        }
        if (tree_open) {                                                // the tab's skills and the three tabs
            const auto& list = scene->rules.class_skills[std::size_t(std::max(character.character_class, 0))];
            for (const auto& class_skill : list)
                if (class_skill.page == tree_tab)
                    if (const auto [icon_x, icon_y] = skill_icon_at(class_skill); icon_x > -1000) centre(icon_x, icon_y - 0x30, 0x30, 0x30);
            for (const int top : { kTreeB - 0x174, kTreeB - 0x108, kTreeB - 0x9c }) centre(kTreeR - 0x58, top, 0x58, 0x6b);
        }
        if (tree_open) {
            const auto close = skill_tree_close(character.character_class, tree_tab);
            centre(close.left, close.bottom - 34, 34, 34);
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
        if (belt_up.shown) {                                            // the belt's boxes
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
                if (!line.header) points.push_back({ npc_menu.x + npc_menu.box_width / 2, base - 4 });
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

// The inventory item under the cursor, or nullptr. Not the stash, the cube or the belt.
auto ClientSession::bag_at(int mouse_x, int mouse_y) const -> const d2d::d2s::Item* {
        if (!scene || !inv_open) return nullptr;
        const auto& inventory = touch_enabled ? touch_inv_layout() : scene->inv_layout[std::size_t(std::max(character.character_class, 0))];
        for (const auto& item : character.items) {
            if (item.location != d2d::d2s::item_location::kStored || item.panel != d2d::d2s::item_panel::kInventory) continue;
            const auto rect = grid_rect(*scene, inventory, item);
            if (mouse_x >= rect[0] && mouse_x < rect[0] + rect[2] && mouse_y >= rect[1] && mouse_y < rect[1] + rect[3]) return &item;
        }
        return nullptr;
    }

// A store's X buys the stock under the cursor. Selling is Hold A, so X does not sell.
auto ClientSession::pad_trade(int mouse_x, int mouse_y) -> void {
        if (store.npc < 0 || !scene) return;
        if (const int stock = store_item_at(*scene, store, mouse_x, mouse_y, touch_store_panel_x()); stock >= 0) send(cmd::Buy{ stock, store.tab });
    }

/**
 * Resolve the hovered item availability using the shared item-use policy and current player state.
 * The result supplies both inventory prompts and item dispatch.
 */
auto ClientSession::item_use(const d2d::d2s::Item& item) const -> ItemUse {
    const bool potion = potion_code(item.code) || (scene && scene->rules.potions.contains(item.code));
    const bool unidentified = std::ranges::any_of(character.items, [&](const auto& carried) { return carried.id != item.id && !carried.identified; });
    return item_use_action(item.code, item.quantity, potion, level && d2d::rules::level_ids::is_town(level->id), view.dead, unidentified);
}

// Skill buttons, inventory only. A shop's X stays Buy. The item in hand drops.
// A bag item is picked up and dropped in that order: if the pick-up is refused the drop matches nothing.
// A joined game skips that pair. The drop packet needs a cursor the pickup has not filled yet.
auto ClientSession::pad_drop(int mouse_x, int mouse_y) -> void {
        if (!pad_config.skill_buttons || store.npc >= 0 || !inv_open) return;
        if (held) { send(cmd::Drop{ held->id }); return; }
        if (!pad_drops_from_bag(net_game != nullptr)) return;
        if (const d2d::d2s::Item* item = bag_at(mouse_x, mouse_y)) {
            send(cmd::ToCursor{ item->id });
            send(cmd::Drop{ item->id });
        }
    }

/**
 * Update controller hold actions and contextual inventory/shop hints. Hold-to-sell and field-use
 * state are handled separately so prompts describe the available action.
 */
auto ClientSession::drive_pad_hold(Mouse& mouse, float seconds) -> void {
        pad_hold_text.clear();
        pad_hold_fraction = 0.f;
        const d2d::d2s::Item* item = bag_at(mouse.x, mouse.y);
        PadHoldQuery query;
        query.shop = store.npc >= 0;
        query.gamble = store.gamble;
        query.identify = identify_with >= 0;
        query.imbue = use_item.open();
        query.hand_empty = !held;
        query.bag = item != nullptr;
        query.store_mode = store.mode;
        if (item && scene && world.game_data) {
            query.sellable = d2d::rules::sellable(scene->rules, *item);
            query.potion = potion_code(item->code) || scene->rules.potions.contains(item->code);
            const int save_cls = std::max(character.character_class, 0);
            const int level_needed = d2d::game::required_level(*world.game_data, *item, save_cls);
            const auto worn_by = d2d::game::wearer(*world.game_data, save_cls, character.items, character.stats);
            query.equippable = d2d::rules::bag_equip_slot(scene->rules, character.items, held, item->id, worn_by, level_needed) >= 0;
        }
        query.equippable = pad_hold_can_equip(net_game != nullptr, query.equippable);
        PadHoldKind kind = pad_hold_button(query, pad_south_down, pad_rt_down);
        if (!pad_south_down && !pad_rt_down && pad_hold.armed) kind = PadHoldKind(pad_hold.context);
        const bool eligible = kind != PadHoldKind::None;
        const int item_id = item ? item->id : -1;
        if (kind == PadHoldKind::Sell) pad_hold.delay = pad::kHoldSellSeconds;
        else if (kind == PadHoldKind::Equip) pad_hold.delay = pad::kHoldEquipSeconds;
        else pad_hold.delay = pad::kHoldFillSeconds;
        const bool button_down = pad_south_down || (pad_rt_down && kind == PadHoldKind::FillOne);
        const bool edge = pad_south_down ? pad_south_edge : pad_rt_edge;
        if (!inv_open || (pad_hold.armed && !button_down && pad_hold.context != int(kind))) {
            pad_hold.clear();
            return;
        }
        const hold_confirm::Step step = pad_hold.step(edge, button_down, eligible, item_id, int(kind), seconds);
        const bool tracking = pad_hold.armed && !pad_hold.cancelled;
        if (tracking || step == hold_confirm::Step::Click || step == hold_confirm::Step::Act) mouse.press_this_frame = false;
        const bool can_potion = query.potion && query.bag && query.hand_empty && !query.shop && !query.identify && !query.imbue;
        const bool can_equip = query.equippable && query.bag && query.hand_empty && !query.shop && !query.identify && !query.imbue;
        const bool can_sell = query.sellable && query.bag && query.hand_empty && query.shop && !query.gamble && !query.identify && !query.imbue
                              && query.store_mode != 1 && query.store_mode != 3;
        if (pad_playing && pad_config.skill_buttons && query.shop && query.hand_empty && scene &&
            store_item_at(*scene, store, mouse.x, mouse.y, touch_store_panel_x()) >= 0) {
            const auto& pair = pad_config.bindings[pad::Action::Trade];
            const auto button = pair[0] != pad::Bindings::kNone ? pair[0] : pair[1];
            if (button != pad::Bindings::kNone)
                pad_hold_text = std::string(pad::label(pad_family, button)) + " to buy";
        }
        const auto label_of = [&](pad::Action action) {
            const auto& pair = pad_config.bindings[action];
            return std::string(pad::label(pad_family, pair[0] != pad::Bindings::kNone ? pair[0] : pair[1]));
        };
        if (pad_playing && identify_with >= 0) {
            const bool valid = item && item->id != identify_with && !item->identified;
            pad_hold_text = identify_prompt(pad_config.bindings, pad_family, valid);
        } else if (pad_playing && pad_config.skill_buttons && item && query.hand_empty && !query.imbue) {
            const auto use = item_use(*item);
            if (!use.label.empty()) pad_hold_text = (use.kind == ItemUseKind::None ? "" : label_of(pad::Action::Interact) + " . ") + std::string(use.label) + "  ";
        }
        if (pad_shows_hold_cue(pad_playing, can_potion, can_equip, can_sell)) {
            if (tracking) pad_hold_fraction = pad_hold.fraction();
            const std::string_view aye = pad::label(pad_family, pad::Button::South);
            const std::string_view trigger = pad::label(pad_family, pad::Button::RightTrigger);
            if (can_potion) pad_hold_text += std::string("Hold ") + std::string(aye) + " to fill belt.  Hold " + std::string(trigger) + " to belt this.";
            else if (can_sell) pad_hold_text += std::string("Hold ") + std::string(aye) + " to sell";
            else pad_hold_text += std::string("Hold ") + std::string(aye) + " to equip";
        }
        if (step == hold_confirm::Step::Click && scene) {
            const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                                    { inv_open, stash_open, cube_open, belt_open, character.expansion }, mouse.x, mouse.y);
            if (click.cmd) send(*click.cmd);
        } else if (step == hold_confirm::Step::Act && item) {
            if (kind == PadHoldKind::Sell) send(cmd::Sell{ item->id });
            else if (kind == PadHoldKind::Equip) send(cmd::EquipFromBag{ item->id });
            else if ((kind == PadHoldKind::Fill || kind == PadHoldKind::FillOne) && scene) {
                auto trial = character.items;
                const int boxes = scene->belts[std::size_t(belt_index(*scene, character.items))].boxes;
                std::vector<int> moved;
                const auto try_item = [&](int carried_id) {
                    if (d2d::rules::auto_belt(scene->rules, trial, carried_id, boxes)) moved.push_back(carried_id);
                };
                const auto is_potion = [&](std::string_view code) {
                    return potion_code(code) || scene->rules.potions.contains(std::string(code));
                };
                try_item(item->id);
                if (kind == PadHoldKind::Fill)
                    for (const auto& carried : character.items)
                        if (carried.id != item->id && is_potion(carried.code)) try_item(carried.id);
                for (const int carried_id : moved) send(cmd::AutoBelt{ carried_id });
            }
        }
    }

auto ClientSession::tip_env() const -> d2d::rules::CalcEnv {
        d2d::rules::CalcEnv env{ [this](int id) { return d2d::game::skill_base_level(*scene, character, id); },
                                 [this](int id) { return skillbar.level(id); },   // with the skill shrine's +all skills
                                 {}, int(character.stats.get(d2d::d2s::kLevel)), nullptr };
        auto sum = std::make_shared<d2d::rules::StatSum>();
        for (const auto& [stat, value] : view.stat_sum)
            if (stat >= 0 && std::size_t(stat) < sum->size()) (*sum)[std::size_t(stat)] = value;
        env.stat = [stat_sum = std::move(sum)](int id) { return id >= 0 && std::size_t(id) < stat_sum->size() ? int((*stat_sum)[std::size_t(id)]) : 0; };
        return env;
    }

auto ClientSession::pad_back(int mouse_x, int mouse_y) const -> bool {
        if (gold_box) return true;                                   // the gold box: B is its Cancel
        if (!scene || held || !(stash_open || cube_open)) return false;
        const auto click = item_cursor_command(*scene, character.items, held, std::max(character.character_class, 0),
                                            { inv_open, stash_open, cube_open, belt_up.shown, character.expansion }, mouse_x, mouse_y);
        return !(click.cmd && std::holds_alternative<cmd::ToCursor>(*click.cmd));
    }

// The field's right stick click: the right hand's skill picker (A on the
// left skill button switches to the left's), the cursor on its skill.
/**
 * Open the controller skill-assignment editor in Skill Buttons mode, or the existing hand picker in
 * the classic mode.
 */
auto ClientSession::pad_open_picker() -> void {
        if (pad_config.skill_buttons) {
            if (skillbar.picking == 3) { close_assignment(); return; }
            if (!pad_field() && skillbar.picking != 0) return;
            open_assignment(0);
            return;
        }
        if (pad_field()) skillbar.picking = 2;
    }

// A store's shoulders: the next tab with stock on it, that way round.
/**
 * Return the active panel in shoulder-navigation order, or -1 when none of those panels is open.
 */
auto ClientSession::pad_panel() const -> int {
        if (controls.open || stash_open || cube_open || store.npc >= 0 || waypoint.open || npc_menu.npc >= 0 || speech.npc >= 0 || help.open || skillbar.picking) return -1;
        if (game_menu.open) return 5;
        if (tree_open) return 4;
        if (inv_open) return 3;
        if (char_open) return 2;
        if (quest_log.open) return 1;
        if (party.open) return 0;
        return -1;
    }

/**
 * Cycle open gameplay panels with the shoulder inputs, preserving classic/store tab behavior.
 * Refuse a panel switch while item, identification, imbue or gold transactions are pending.
 */
auto ClientSession::pad_tab(int step) -> void {
        if (store.npc < 0) {
            const int current = pad_panel();
            if (current < 0 || held || identify_with >= 0 || use_item.open() || gold_box) return;
            const int next = (current + (step > 0 ? 1 : 5)) % 6;
            if (game_menu.open) close_game_menu();
            inv_open = char_open = tree_open = quest_log.open = party.open = false;
            inv_close_down = char_close_down = tree_close_down = false;
            if (next == 0) party.open = true;
            if (next == 1) toggle_quest_log();
            if (next == 2) char_open = true;
            if (next == 3) inv_open = true;
            if (next == 4) tree_open = true;
            if (next == 5) { open_game_menu(); game_menu.show(1); }
            sync_panel_cam();
            return;
        }
        for (int i = 1; i < 4; ++i)
            if (const int tab = ((store.tab + step * i) % 4 + 4) % 4; !store.tabs[std::size_t(tab)].empty()) { store.tab = tab; return; }
    }

}  // namespace d2d::client
