// SPDX-License-Identifier: GPL-3.0-or-later
// The World (world.hpp): sounds, steps, stamina and the 25 Hz tick. The rest is in this
// directory.
#include "world.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "fight.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "log.hpp"
#include "protocol.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <light.hpp>
#include <monster_ids.hpp>
#include <object_ids.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace d2d::game {

// The client's ticks from an object's OP to its ON (FUN_004bcbb0): the
// counter runs from Start1 at FrameDelta1 a tick (FUN_00624390) until it
// reaches FrameCnt1 - 1; -1: never (FrameCnt1 1, or a counter that doesn't move).
static int op_ticks(const Npc& npc) {
    if (!npc.op_frames) return 0;
    if (npc.frames[1] == 1) return -1;
    const int need = (int(npc.frames[1]) - 1 - int(npc.loop_from[1])) * 256;
    if (need <= 0) return 0;
    if (!npc.frame_delta[1]) return -1;
    return (need + npc.frame_delta[1] - 1) / npc.frame_delta[1];
}

auto World::greet_sound(const Npc& npc, int kind) -> int {
        const auto* greeting = d2d::rules::sound_ids::greeting_of(npc.hc_idx);
        if (!greeting) return 0;
        auto& last = greeted_with[npc.hc_idx];
        int row = 0;
        // ponytail: the member is a plain draw; FUN_00482680 also keeps from the group's last ones.
        for (int tries = 0; tries < 20; ++tries) {
            row = d2d::rules::sound_ids::greeting_row(*greeting, kind, day.phase, [&](int bound) { return int(fight.sound_rng(bound)); });
            if (row > 0 && std::size_t(row) < game_data->sounds.size())
                if (const int group = game_data->sounds[std::size_t(row)].group; group > 1) row += int(fight.sound_rng(group));
            if (row != last) break;
        }
        last = row;
        return row;
    }

// A walking or running player's footsteps (FUN_004caf60): two a cycle of the
// walk's animation, from its start, on the floor tile under them (its DT1
// material, else the level's SoundEnviron Material 1), each at least two
// thirds of a step after the last. The animation's rate is the walk's
// (rules::moving_anim_rate), as the client's footsteps read it (unit +0x4c).
auto World::footstep_frame(std::uint32_t tick_ms) -> void {
        using namespace d2d::rules::sound_ids;
        const int cls = actor->character.character_class;
        const bool walking = actor->player.walking && actor->pmode < 0;
        if (walking && !actor->stepping) actor->walk_from_ms = tick_ms;   // a walk starting: its cycle from here
        actor->stepping = walking;
        if (!walking || fight.dead() || cls < 0 || cls >= 7 || actor->ghost) return;
        const bool run_on = actor->running && actor->character.stats.values[d2d::d2s::kStamina] > 0;   // RN as drawn, in town too
        const bool run = run_on && !in_town(level);
        const auto& anim = fight.player_anim(run_on ? kModeRN : in_town(level) ? kModeTW : kModeWL);
        const int rate = std::max(d2d::rules::moving_anim_rate(run_on, walk_percent(*actor, tick_ms)), 1);
        const int length = int(anim.frames ? anim.frames : anim.cof_frames) * 256, count = 2;
        const int pos = int((tick_ms - actor->walk_from_ms) / 40) * rate % std::max(length, 1);
        if (!footfall(pos, rate, length, count, 0)) return;
        if (tick_ms - actor->step_sound_ms < std::uint32_t(2 * length / (count * rate) / 3) * 40) return;
        actor->step_sound_ms = tick_ms;
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), footstep(kPlayerVoice[std::size_t(cls)].step, fight.floor_material_at(actor->player.x, actor->player.y), run), tick_ms, actor->player.x, actor->player.y);
    }

// FUN_00623f50's walk %: velocitypercent (100; running, run x 100 / walk),
// faster run/walk's effective % (150 x v / (150 + v)) and chill's -50, at
// least 25. It scales both the walk's velocity and its animation.
auto World::walk_percent(const GameData& data, int save_class, bool run, int frw, bool chilled) -> int {
        if (data.walk_velocity.empty() || data.run_velocity.size() < data.walk_velocity.size()) return 100;
        const auto row = std::size_t(std::clamp(save_class, 0, int(data.walk_velocity.size()) - 1));
        const int walk = std::max(data.walk_velocity[row], 1);
        const int base = run ? data.run_velocity[row] * 100 / walk : 100;
        const int chill = chilled ? -50 : 0;   // Fight::chill_rate, for any player
        return std::max(base + d2d::rules::effective_speed(frw, 150) + chill, 25);
    }

auto World::walk_percent(const Player& player, std::uint32_t now_ms) const -> int {
        const bool run = player.running && player.character.stats.values[d2d::d2s::kStamina] > 0;
        return walk_percent(*game_data, player.character.character_class, run, player.player_combat.frw, now_ms < player.chilled);
    }

// A frame's stamina (FUN_00580c20 / FUN_00580810 event 3): a run outside
// town drains it (FUN_0057f240), at 0 the run is a walk (FUN_0057f090: no
// run at stamina 0); then it regens by mode (FUN_00580500).
// ponytail: the run stays a run while stamina lasts and walks at 0 each
// frame, not at the next move as game.exe; the same since a walk at 0 never
// regens.
auto World::stamina_frame() -> void {
        using namespace d2d::d2s;
        if (fight.dead()) return;
        const bool town = in_town(level);
        auto& stamina = actor->character.stats.values[kStamina];
        const bool run = actor->player.walking && actor->pmode < 0 && actor->running && stamina > 0;
        if (run && !town) {
            int armor_speed = 0;
            for (const auto& item : actor->character.items)
                if (item.location == d2d::d2s::item_location::kEquipped && item.slot == d2d::d2s::body_location::kTorso)
                    if (const auto found = game_data->rules.item_base.find(item.code); found != game_data->rules.item_base.end()) armor_speed = found->second.speed;
            stamina = std::max<std::int64_t>(0, stamina - d2d::rules::stamina_drain(game_data->run_drain[std::size_t(std::max(actor->character.character_class, 0))],
                                                                                  armor_speed, int(actor->psum[kStaminaDrainPercent])));
        }
        const int mode = actor->pmode >= 0 ? -1 : actor->player.walking ? (run ? kModeRN : town ? kModeTW : kModeWL) : town ? kModeTN : kModeNU;
        stamina = d2d::rules::stamina_regen(stamina, actor->character.stats.values[kMaxStamina], mode, int(actor->psum[kStaminaRecoveryBonus]));
    }

auto World::tick(std::span<const Order> orders, std::uint32_t now_ms, std::uint32_t last_ms) -> void {
        const float elapsed = float(now_ms - last_ms) / 1000.f;
        now = now_ms;
        // The look follows what's worn (compcode::look, the save header's bytes).
        if (!game_data->item_pieces.empty() && !game_data->comp.empty())
            each_player([&](Player& player) {
                std::vector<std::string_view> active_states;
                for (const auto& state : player.self_states) if (const auto* skill = game_data->skills.get(state.skill)) active_states.push_back(skill->aurastate);
                player.character.appearance = game_data->look_of(player.character.items, active_states);
            });
        // Levels: finished builds come in; the ones next to the player's
        // start building when it changes (GameData::level).
        game_data->poll_levels();
        fight.spawning.game_quests = game_bits;          // the town markers' inits read them as rooms come up
        // a3q7: the Dark Wanderer gone (ai.cpp) marks every player in act 3 (FUN_005bd260: word 0x20 bit 0, if missing).
        if (d2d::rules::level_act(level->id) == 2
            && std::ranges::any_of(fight.monsters, [](const Monster& monster) { return monster.gone && monster.type == d2d::rules::monster_ids::kDarkWanderer; }))
            each_player([&](Player&) { if (!d2d::rules::quest::bit(quests(), 0x20, 0)) d2d::rules::quest::set(quests(), 0x20, 0); });
        // FUN_005bd390, once a game: round where he went (0x741538's
        // (dx, dy), from index `quest seed & 1`: 7 or 8; index 7 repeats
        // index 2, bugs.md #20) object 131 at a free spot near each, mode 1,
        // gone 27 frames on (InitFn 59), two ticks after he goes (the record's
        // timer, FUN_005bd4a0), each at the spot FUN_00545340 finds round it
        // (size 3, mask 0x3f11, 100 rings: Level::free_area); none, not made.
        if (const auto wanderer = std::ranges::find_if(fight.monsters, [](const Monster& monster) { return monster.gone && monster.type == d2d::rules::monster_ids::kDarkWanderer; });
            !afterglow_made && wanderer != fight.monsters.end() && !game_data->afterglow.code.empty()) {
            if (afterglow_due == 0) afterglow_due = now_ms + 2 * kTickMs;
        }
        if (const auto wanderer = std::ranges::find_if(fight.monsters, [](const Monster& monster) { return monster.gone && monster.type == d2d::rules::monster_ids::kDarkWanderer; });
            !afterglow_made && afterglow_due != 0 && now_ms >= afterglow_due && wanderer != fight.monsters.end()) {
            afterglow_made = true;
            cues.cue_on(unit_key(kSoundOnMonster, wanderer->id), "object_wanderer_fissure", now_ms, wanderer->unit.x, wanderer->unit.y);
            static constexpr std::array<std::pair<int, int>, 8> kAround{ { { -3, -3 }, { -3, 0 }, { -3, 3 }, { 0, -3 }, { 0, 3 }, { 3, -3 }, { 3, 0 }, { -3, 3 } } };
            const int from_x = int(std::floor(wanderer->unit.x * 5)), from_y = int(std::floor(wanderer->unit.y * 5));
            for (auto at = std::size_t(fight.spawning.quests.next() & 1); at < kAround.size(); ++at)
                if (const auto spot = fight.mon_level->free_area(from_x + kAround[at].first, from_y + kAround[at].second, 3, 0x3f11, 100))   // his level, though the player's left it
                    fires.push_back({ fight.mon_level, &game_data->afterglow, (float(spot->first) + 0.5f) / 5, (float(spot->second) + 0.5f) / 5, now_ms, now_ms + 27 * kTickMs });
            d2d::log::info("the Dark Wanderer's afterglow: {}", std::ranges::count(fires, &game_data->afterglow, &Fire::npc));
        }
        std::erase_if(fires, [&](const Fire& fire) { return fire.gone && now_ms >= fire.gone; });
        if (level != wanted_near) { want_nearby(*game_data, *level); wanted_near = level; }
        each_player([&](Player&) { item_ids(); });
        // The day moves a frame a tick (40 ms); a stall doesn't fast-forward it.
        if (now_ms - day_at > 1000) day_at = now_ms;
        for (; now_ms - day_at >= kTickMs; day_at += kTickMs) {
            day.step(act());
            credit_party([&] { act1_tick(); act2_tick(); });
            each_player([&](Player&) { stamina_frame(); footstep_frame(day_at); find_voice(day_at); });
        }
        each_player([&](Player& player) {
            if (player.boost.until && now_ms >= player.boost.until) {     // the state runs out
                if (shrine_code(player.boost.shrine) == 14) player.character.stats.values[d2d::d2s::kStamina] = player.character.stats.values[d2d::d2s::kMaxStamina];
                player.boost = {};
            }
            fight.update_fighters(now_ms);
        });
        spring_traps(now_ms);
        // Used shrines and chests: OP while it plays, then ON; a shrine back
        // to NU after its reset time (Shrines.txt Reset x 1200 + 1 frames;
        // 0 never).
        for (auto entry = operated.begin(); entry != operated.end();) {
            const auto& [key, when_ms] = *entry;
            const auto npc_index = std::size_t(key.second);
            const auto& npc = key.first->npcs[npc_index];
            const int reset = npc.operate_fn == d2d::rules::operate_fn::kShrine && std::size_t(npc.shrine) < game_data->shrines.size() ? game_data->shrines[std::size_t(npc.shrine)].reset : 0;
            const bool back = reset > 0 && now_ms - when_ms >= d2d::rules::shrine_reset_frames(reset) * kTickMs;
            if (key.first == level && npc_index < npc_states.size()) {
                const auto on_ticks = op_ticks(npc);
                const std::uint32_t on_ms = on_ticks < 0 ? 0 : when_ms + std::uint32_t(on_ticks) * kTickMs;
                const bool opening = on_ticks < 0 || now_ms < on_ms;
                const std::string_view mode = back ? std::string_view{} : opening ? "OP" : "ON";
                // Its footprint in the new mode (ON: FUN_00623830 when HasCollision2 is off; jugs q4 / q5, xj).
                // ponytail: on this level's tick only; one left mid-OP keeps OP's until it's back in view.
                if (npc_states[npc_index].mode != mode) set_footprint(*level, npc, npc.collision >> mode_index(mode.empty() ? npc.mode : mode) & 1);
                npc_states[npc_index].mode = mode;
                npc_states[npc_index].mode_ms = back ? now_ms : opening ? when_ms : on_ms;   // its animation runs from the mode's start
                // Its message overhead (ShrMsgN, 3683 + row: FUN_00661110 on
                // "%d", 4 chars x 8 + 125 frames).
                npc_states[npc_index].says = std::uint16_t(npc.operate_fn == d2d::rules::operate_fn::kShrine && !back && now_ms - when_ms < 157u * kTickMs ? 3683 + npc.shrine : 0);
            }
            entry = back ? operated.erase(entry) : std::next(entry);
        }
        for (const auto& [key, door] : doors)                  // doors in the mode they were left in
            if (key.first == level && std::size_t(key.second) < npc_states.size())
                std::tie(npc_states[std::size_t(key.second)].mode, npc_states[std::size_t(key.second)].mode_ms) = std::pair{ kObjectModes[std::size_t(door.mode)], door.when };
        // The Rogue camp's bonfire (and three sound emitters): the client sets
        // EnvEffect objects NU by day, OP otherwise, at birth and each time the
        // day's type changes (FUN_004bc5e0 via FUN_004bc720 / FUN_004646c0).
        const bool daylight = (day.eclipse ? d2d::rules::kEclipse : d2d::rules::kDay)[std::size_t(day.phase)].type == 0;
        grow_states(npc_states, *level);
        for (std::size_t index = 0; index < level->npcs.size() && index < npc_states.size(); ++index)
            if (level->npcs[index].env_effect) npc_states[index].mode = daylight ? "NU" : "OP";
        Crowd crowd;                           // who's in whose way this frame
        for (const auto& player : players) {
            crowd.units.push_back(&player->player);
            if (player->merc) crowd.units.push_back(&player->merc->unit);
        }
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (level->npcs[i].root != "objects" && !npc_states[i].hidden) crowd.units.push_back(&npc_states[i]);
        const bool in_moor = !in_town(level);     // outside: this level's monsters are about
        if (in_moor) fight.crowd(crowd);       // the monsters around the player
        // Dead: the death plays out, then Resurrect respawns in camp; a
        // swing or a flinch holds the player in place until it ends, and
        // an attack goes on while the button's held (the left skill sent
        // again).
        std::vector<std::pair<float, float>> before;    // where each player stood (the leash)
        for (const auto& player : players) before.emplace_back(player->player.x, player->player.y);
        each_player([&](Player& player) {
            // Local co-op: players walk through each other (one screen; a
            // partner in the way would wall them in). Monsters still meet every player.
            Crowd walk_crowd = crowd;
            std::erase_if(walk_crowd.units, [&](const UnitState* unit) {
                return unit != &player.player && std::ranges::any_of(players, [&](const auto& other) { return &other->player == unit; });
            });
            std::vector<Command> cmds;                  // this player's
            for (const auto& order : orders) if (order.player == player.id) cmds.push_back(order.command);
            const bool button = std::ranges::any_of(cmds, [](const Command& command) {
                const auto* use_skill = std::get_if<cmd::UseSkill>(&command);
                return std::holds_alternative<cmd::Move>(command) || (use_skill && use_skill->left);
            });
            const bool resurrect = std::ranges::any_of(cmds, [](const Command& command) { return std::holds_alternative<cmd::Resurrect>(command); });
            // Back in camp after dying: alone, or in co-op once no one's left alive.
            if (fight.player_modes(button, now_ms, elapsed) && resurrect && (players.size() == 1 || !anyone_alive())) respawn(now_ms);
            if (actor->pmode != actor->last_pmode) {                 // death's stages: the penalty as it starts, the corpse as it ends
                if (actor->pmode == kModeDT) death_penalty(now_ms);
                if (actor->pmode == kModeDD) { if (players.size() > 1) become_ghost(player); else make_corpse(); }   // co-op: a ghost, the gear kept
                actor->last_pmode = actor->pmode;
            }
            if (!fight.dead() || actor->ghost) {
            const bool busy = actor->pmode >= 0 && !actor->ghost;
            for (const auto& command : cmds) {
                if (std::holds_alternative<cmd::Resurrect>(command)) continue;
                if (actor->ghost && !std::holds_alternative<cmd::Move>(command) && !std::holds_alternative<cmd::Run>(command)) continue;   // a ghost only walks
                apply(command, now_ms);
            }
            // Close enough to the stash: open it with the inventory.
            // ponytail: 2 cells, not D2's per-object operate range.
            if (actor->interact_npc >= 0) {
                const auto& npc = level->npcs[std::size_t(actor->interact_npc)];
                const auto& state = npc_states[std::size_t(actor->interact_npc)];
                const float npc_x = npc.path.empty() ? npc.x : state.x, npc_y = npc.path.empty() ? npc.y : state.y;
                if (std::hypot(npc_x - actor->player.x, npc_y - actor->player.y) < 2.f) {
                    if (operable(npc.operate_fn) || npc.operate_fn == d2d::rules::operate_fn::kMoldyTome || npc.operate_fn == d2d::rules::ToolsQuest::kStand || quest_object(npc.operate_fn)) {
                        credit_party([&] { operate(actor->interact_npc, now_ms); });
                    } else if (npc.operate_fn == d2d::rules::operate_fn::kStash) {
                        actor->events.push_back(ev::OpenUI{ ev::OpenUI::stash, actor->interact_npc });
                    } else if (npc.operate_fn == d2d::rules::operate_fn::kWaypoint) {
                        // FUN_00584e30: touching activates this level's waypoint
                        // (the town's, index 0, always is); an unlit one lights
                        // up (mode 1 → 2) and the panel waits for the next touch.
                        set_waypoint(0);
                        set_waypoint(waypoint_index(*game_data, level->id));
                        if (npc.mode == "NU" && !operated.contains({ level, actor->interact_npc })) operated[{ level, actor->interact_npc }] = now_ms;
                        else actor->events.push_back(ev::OpenUI{ ev::OpenUI::waypoint, actor->interact_npc });
                    } else {
                        if (d2d::rules::is_healer(npc.hc_idx)) { d2d::rules::heal(actor->character.stats); fight.cure(now_ms, false); }
                        // In Hell, a Den of Evil done before the reset existed
                        // opens it on meeting Akara (FUN_0058fd20: quest 41 bits
                        // 13 and 1).
                        if (npc.hc_idx == d2d::rules::monster_ids::kAkara && actor->character.header.active_difficulty() == 2) {
                            auto& quest_bits = quests();
                            namespace quest = d2d::rules::quest;
                            constexpr int kRespec = d2d::rules::kRespecQuest;
                            if (quest::bit(quest_bits, d2d::rules::DenQuest::kQuest, 0) && !quest::bit(quest_bits, kRespec, 1) && !quest::bit(quest_bits, kRespec, 0)) { d2d::rules::quest::set(quest_bits, kRespec, 13); d2d::rules::quest::set(quest_bits, kRespec, 1); }
                        }
                        auto lines = quest_talk(npc.hc_idx);
                        // Its greeting as the menu opens (FUN_004b66b0, kind 0), unless it has news to tell.
                        // ponytail: kind 2's "return" (the talk record's +0x12 flag) isn't traced.
                        if (!std::ranges::any_of(lines, &d2d::rules::QuestMsg::greet)) cues.cue_on(unit_key(kSoundOnMonster, -1 - actor->interact_npc), greet_sound(npc, 0), now_ms, actor->player.x, actor->player.y);
                        actor->events.push_back(ev::OpenUI{ ev::OpenUI::talk, actor->interact_npc, std::move(lines) });
                    }
                    actor->player.walking = false; actor->interact_npc = -1;
                } else if (!actor->player.walking) {
                    actor->interact_npc = -1;                         // blocked on the way
                } else {
                    actor->target_x = npc_x; actor->target_y = npc_y;              // follow a walking NPC
                }
            }
            if (actor->pick_item >= 0 && loot.index_of(actor->pick_item) < 0) actor->pick_item = -1;   // someone took it
            if (actor->pick_item >= 0 && !busy) {
                const auto ground_index = std::size_t(loot.index_of(actor->pick_item));
                const auto& ground_item = loot.ground[ground_index];
                if (std::hypot(ground_item.x - actor->player.x, ground_item.y - actor->player.y) <= 1.f) {
                    const bool malus = ground_item.item.code == "hdm";
                    loot.take(ground_index);
                    if (malus && holding_malus() && tools.picked_up(quests())) d2d::log::info("Tools of the Trade: the malus picked up (the player's line, FUN_00553380)");
                    actor->pick_item = -1; actor->player.walking = false; actor->player.path.clear();
                } else {
                    actor->target_x = ground_item.x; actor->target_y = ground_item.y; actor->player.walking = true;
                }
            }
            if (autoloot_gold && !fight.dead() && !actor->ghost)  // d2d: walking over gold picks it up (game.exe wants a click)
                for (std::size_t i = loot.ground.size(); i-- > 0;) {
                    auto& ground_item = loot.ground[i];
                    if (ground_item.item.code != "gld") continue;
                    const bool underfoot = std::hypot(ground_item.x - actor->player.x, ground_item.y - actor->player.y) <= 0.5f;
                    if (ground_item.dropped_by == actor->id) { if (!underfoot) ground_item.dropped_by = 0; continue; }
                    if (underfoot) {
                        if (loot.index_of(actor->pick_item) == int(i)) actor->pick_item = -1;
                        loot.take(i);
                    }
                }
            bool to_unit = actor->interact_npc >= 0 && std::size_t(actor->interact_npc) < level->npcs.size() && level->npcs[std::size_t(actor->interact_npc)].root == "monsters";
            if (const auto target = fight.engage(now_ms)) { std::tie(actor->target_x, actor->target_y) = *target; actor->player.walking = true; to_unit = true; }
            if (actor->player.walking && (actor->pmode < 0 || actor->ghost)) {
                // A route to the target (path type 7), re-planned when the
                // target moves off its end (dragging, a walking NPC) and when
                // it ran out short of it (FUN_006503f0's last test).
                // ponytail: a target unit re-paths at 0.3 cells, not game.exe's
                // 5 subtiles off SP2.
                const auto subtile = [](float value) { return int(std::floor(value * 5)); };
                if (actor->player.path.empty() || std::hypot(actor->player.goal_x - actor->target_x, actor->player.goal_y - actor->target_y) > 0.3f) {
                    actor->player.path = player_walk(*level, actor->player.x, actor->player.y, actor->target_x, actor->target_y, to_unit, walk_crowd, &actor->player);
                    actor->player.goal_x = actor->target_x; actor->player.goal_y = actor->target_y;
                }
                const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
                // FUN_00623f50: the walk velocity x a % (at least 25) of 100 (run: run x 100 / walk),
                // faster run/walk's effective % (150 x v / (150 + v)) and chill's -50.
                const int walk = std::max(game_data->walk_velocity[save_class], 1);
                const float vel = float(walk * walk_percent(*actor, now_ms)) / 100.f;
                const float before_x = actor->player.x, before_y = actor->player.y;
                actor->player.walking = follow_path(*level, actor->player, cells_per_sec(vel) * elapsed, walk_crowd);
                // The merc's think reads the player's footsteps (FUN_00580c20): walking or
                // running, 25 ms on from the last, over 45 (squared) from it.
                if (const int step_x = int(std::floor(actor->player.x * 5)), step_y = int(std::floor(actor->player.y * 5)); now_ms > actor->footstep_ms + 25) {
                    const auto [last_x, last_y] = actor->footsteps[std::size_t(actor->footstep_cursor == 0 ? 0x13 : actor->footstep_cursor - 1)];
                    if ((step_x - last_x) * (step_x - last_x) + (step_y - last_y) * (step_y - last_y) > 0x2d) fight.footstep(step_x, step_y, now_ms);
                }
                if (!actor->player.walking) {
                    actor->player.path.clear();
                    // Short of the target with a step made: it paths again from here.
                    actor->player.walking = (subtile(actor->player.x) != subtile(actor->target_x) || subtile(actor->player.y) != subtile(actor->target_y))
                                     && (actor->player.x != before_x || actor->player.y != before_y);
                }
            }
            }
        });
        leash(before);
        // The world goes on with the player dead (game.exe runs the rooms
        // around the corpse till the respawn): monsters see the dead player
        // as not alive (Fight::world) and think on.
        std::array<int, 3> talking{ -1, -1, -1 };    // the NPCs a player has a menu, speech or store open with
        for (const auto& player : players)
            for (std::size_t k = 0; k < talking.size(); ++k) if (player->talking[k] >= 0) talking[k] = player->talking[k];
        // ponytail: an NPC with news greets the first player.
        npc_patrol(*game_data, *level, npc_states, talking, now_ms, elapsed, crowd, &lead().player, arcane.guard_state == 2);
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            if (std::exchange(npc_states[i].greets, false)) cues.cue_on(unit_key(kSoundOnMonster, -1 - int(i)), greet_sound(level->npcs[i], 1), now_ms, lead().player.x, lead().player.y);
        cain_step(now_ms, elapsed);
        for (const auto& neighbour : level->nearby) {           // over the edge, still in play
            auto& states = other_npcs[neighbour.level];
            if (states.size() > neighbour.level->npcs.size()) states = npc_start(*neighbour.level);   // a new game's
            grow_states(states, *neighbour.level);
            npc_patrol(*game_data, *neighbour.level, states, { -1, -1, -1 }, now_ms, elapsed, Crowd{});
        }
        // The "!" (FUN_00544590, from the NPC's think): only the NPC's own act's
        // quests are asked, and objects have no think.
        using ActAlert = bool (World::*)(int);
        static constexpr std::array<ActAlert, 5> kActAlerts{ &World::act1_alert, &World::act2_alert, &World::act3_alert, &World::act4_alert, &World::act5_alert };
        const ActAlert act_alert = kActAlerts[std::size_t(act())];
        for (std::size_t i = 0; i < npc_states.size() && i < level->npcs.size(); ++i)
            npc_states[i].alert = level->npcs[i].root != "objects" && (this->*act_alert)(level->npcs[i].hc_idx);
        // The player's mode as the merc's think reads it: 2 walk, 3 run, 6 walk in town.
        for (auto& player : players)
            player->owner_mode = !player->player.walking ? 1 : player->running && player->character.stats.values[d2d::d2s::kStamina] > 0 ? 3 : in_moor ? 2 : 6;
        fight.world(in_moor, now_ms, elapsed, crowd);
        den_count(now_ms);                                       // each player's credit its own (DenQuest::credit)
        if (den_log_at && now_ms >= den_log_at) { den.log = 5; den_log_at = 0; }
        each_player([&](Player& player) {
            if (std::exchange(player.reassigned, false)) player.events.push_back(ev::Reassigned{});
            if (std::exchange(player.storm_struck, false)) player.events.push_back(ev::Lightning{});
            if (!fight.dead()) {
                use_warp();
                if (actor->portal_due) { actor->portal_due = false; open_portal(now_ms); }
                if (actor->take_corpse >= 0 && std::size_t(actor->take_corpse) < actor->corpses.size() && actor->corpses[std::size_t(actor->take_corpse)].level == level) {
                    const auto& corpse = actor->corpses[std::size_t(actor->take_corpse)];
                    if (std::hypot(corpse.x - actor->player.x, corpse.y - actor->player.y) < 2.f) { take_corpse_items(std::size_t(actor->take_corpse), now_ms); actor->take_corpse = -1; }
                    else if (!actor->player.walking) actor->take_corpse = -1;
                }
                use_portal(now_ms);
            }
        });
        for (const auto& kill : hosted_quests ? std::vector<Fight::Kill>{} : fight.kills) {
            const auto killer = std::ranges::find(players, kill.killer, [](const auto& player) { return player->id; });
            act_as(killer == players.end() ? lead() : **killer);   // the hooks read the killer's quests
            credit_party([&] {
                act1_kill(kill, now_ms);
                act2_kill(kill, now_ms);
                act3_kill(kill, now_ms);
                act4_kill(kill, now_ms);
                act5_kill(kill, now_ms);
            });
        }
        act_as(lead());
        fight.kills.clear();
        credit_party([&] {
            act2_update();
            act3_update(now_ms);
            act4_update(now_ms);
            act5_update(now_ms);
        });
        each_player([&](Player&) { cross_level(); });
        for (const auto& player : players) fight.rooms_up(*level, player->player.x, player->player.y, false);
        act3_bind_new_bosses();
        reopen_doors();
        grow_states(npc_states, *level);
        stones_up();
        each_player([&](Player&) { if (!fight.dead()) fight.apply_regen(now_ms, last_ms); });
        raise_step(elapsed, now_ms);
        if (in_town(level))                                      // the party in a town: its ghosts are up again
            each_player([&](Player& player) { if (player.ghost) revive(player, now_ms); });
    }

}  // namespace d2d::game
