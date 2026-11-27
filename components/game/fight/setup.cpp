// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for fight.hpp: Fight, the combat subsystem the World owns.
// This part: Fight's setup, potions, regeneration, sounds and player modes. The rest of Fight
// (fight.hpp) is in this directory, by subsystem.
#include "fight.hpp"

#include "ai.hpp"
#include "ai/units.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "level.hpp"
#include "log.hpp"
#include "spawning.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <drink.hpp>
#include <hire.hpp>
#include <item_price.hpp>
#include <level_ids.hpp>
#include <merc.hpp>
#include <monsters.hpp>
#include <overlays.hpp>
#include <points.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>
#include <state_death.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// A skill start's cast overlay (FUN_004c6140, rules::cast_overlay_starts) on its caster's
// one-shots `shown`: the player's, a monster's, the merc's or a pet's.
auto Fight::start_cast_overlay(const d2d::rules::Skill& skill, bool from_item, d2d::rules::OneShots& shown, std::uint32_t now_ms) const -> void {
        const auto& rows = game_data->overlay_rows;
        const int row = d2d::rules::cast_overlay_row(game_data->overlay_row(skill.cast_overlay), game_data->overlay_row(skill.item_cast_overlay), from_item);
        if (row < 0 || std::size_t(row) >= rows.size() || !rows[std::size_t(row)]) return;
        if (d2d::rules::cast_overlay_starts(row, shown, now_ms, rows[std::size_t(row)]->rate, rows[std::size_t(row)]->frames)) d2d::rules::add_one_shot(shown, row, now_ms);
    }

// One that came back over an edge is in the list twice (monsters_cross):
// the copy it left going is `gone`, so the one still here comes first.
auto Fight::monster_index(int id) const -> int {
        if (id < 0) return -1;
        auto found = std::ranges::find_if(monsters, [&](const Monster& monster) { return monster.id == id && !monster.gone; });
        if (found == monsters.end()) found = std::ranges::find(monsters, id, &Monster::id);
        return found == monsters.end() ? -1 : int(found - monsters.begin());
    }

auto Fight::add_monster(Monster monster) -> int { return add_monster(std::move(monster), mon_level); }

auto Fight::add_monster(Monster monster, const Level* where) -> int {
        monster.id = next_id++;
        monster.classic = !game_expansion;
        const auto* made_level = where ? where : mon_level ? mon_level : level;
        if (made_level && (monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique))
            made_bosses.push_back({ monster.id, monster.type, made_level->id });
        auto& list = where == mon_level || !where ? monsters : kept[where];
        list.push_back(std::move(monster));
        return int(list.size()) - 1;
    }

// game.exe has no level test as a unit walks from room to room: a step
// (FUN_00554ca0, from the mode actions as FUN_005a7670) that changes the
// path's room takes the next room if it's one of the act's rooms in play
// (FUN_005545c0: the list at game +0xbc + act * 4, any level), then
// FUN_00554670 tells the clients that see the old and the new room. So a
// chase goes on over an outdoor edge. d2d runs one level's monsters, those
// within 30 of a player (near_players): those of the level left come
// along into the next one's list, moved by the offset as pets_cross moves
// pets. Each leaves a `gone` copy, so the indices of the level left (its
// leaders, its spawn_slots) still hold; its leader, bound partner and skill
// target (skill_unit) are remapped, -1 if left behind (a living bound pair
// crosses together). Its spawn area goes, as FUN_00554670 clears monster
// data +0x50 on a room change. Only from the level the player walks out
// of: after a town portal `monsters` is still the wild level left (enter).
// ponytail: only the walk over an edge with one screen (World::cross_level):
// stage 9's split levels keep each level's own monsters.
auto Fight::monsters_cross(const Level* destination, float dx, float dy) -> void {
        if (remote_monsters || !mon_level || mon_level != level || destination == mon_level || in_town(mon_level) || in_town(destination)) return;
        std::vector<bool> carried(monsters.size(), false);
        for (std::size_t index = 0; index < monsters.size(); ++index) {
            const auto& monster = monsters[index];
            if (!monster.alive() || !near_players(float(monster.unit.x) - dx, float(monster.unit.y) - dy)) continue;
            carried[index] = true;
            if (monster.bound >= 0 && std::size_t(monster.bound) < monsters.size() && monsters[std::size_t(monster.bound)].alive()) carried[std::size_t(monster.bound)] = true;
        }
        auto& into = kept[destination];
        std::vector<int> moved_to(monsters.size(), -1);
        for (std::size_t index = 0, next = into.size(); index < monsters.size(); ++index)
            if (carried[index]) moved_to[index] = int(next++);
        const auto remap = [&](int old_index) { return old_index >= 0 && std::size_t(old_index) < moved_to.size() ? moved_to[std::size_t(old_index)] : -1; };
        const auto shift = [&](UnitState& unit) {
            unit.x -= dx; unit.y -= dy;
            unit.goal_x -= dx; unit.goal_y -= dy;
            for (auto& [step_x, step_y] : unit.path) { step_x -= dx; step_y -= dy; }
        };
        const int sub_dx = int(std::lround(dx * 5)), sub_dy = int(std::lround(dy * 5));
        for (std::size_t index = 0; index < monsters.size(); ++index) {
            if (!carried[index]) continue;
            Monster monster = monsters[index];
            monsters[index].gone = true;
            shift(monster.unit);
            shift(monster.landing_mark);
            monster.home_x -= dx; monster.home_y -= dy;
            for (auto& [step_x, step_y] : monster.steps) { step_x -= sub_dx; step_y -= sub_dy; }
            monster.end_x -= sub_dx; monster.end_y -= sub_dy;
            monster.skill_x -= sub_dx; monster.skill_y -= sub_dy;
            monster.leap_from_x -= dx; monster.leap_from_y -= dy; monster.leap_to_x -= dx; monster.leap_to_y -= dy;
            monster.leader = remap(monster.leader);
            monster.bound = remap(monster.bound);
            monster.skill_unit = remap(monster.skill_unit);
            monster.area = -1;
            into.push_back(std::move(monster));
        }
    }

auto Fight::own_stats() const -> d2d::d2s::Stats {
        using namespace d2d::d2s;
        auto stats = actor->character.stats;
        stats.values[kMaxLife] -= actor->bo_life + actor->item_max[0]; stats.values[kMaxMana] -= actor->bo_mana + actor->item_max[1]; stats.values[kMaxStamina] -= actor->item_max[2];
        return stats;
    }

auto Fight::new_game(int difficulty) -> void {
        game_difficulty = difficulty;
        game_expansion = actor->character.expansion;
        spawning = start_spawning(*game_data, difficulty);
        monsters.clear();
        made_bosses.clear();
        mon_level = nullptr;
        area_seen.clear();
        actor->curses = {};
        actor->marks = {};
        actor->chilled = 0;
        actor->stunned = 0;
        kept.clear();
        kept_missiles.clear();
        next_id = 1;
        missiles.clear();
        actor->regen.clear();
        actor->merc_hurt = 0; actor->merc_poison_rate = actor->merc_poison_acc = 0; actor->merc_poison_until = 0; actor->merc_heal_rate = actor->merc_heal_acc = 0; actor->merc_heal_until = 0; actor->merc_potion_states = {};
        actor->charges.clear();
        actor->self_states.clear();
        actor->boost = {};
        actor->stamina_pot = {};
        actor->potion_states = {};
        actor->life_sent = {};                    // the join's update is forced (FUN_00539760)
        actor->pets.clear();
        actor->attack_mon = -1;
        actor->item_charge = actor->attack_charge = {};            // no cast from a charged entry carried in
        actor->pmode = -1;
        actor->struck_class = actor->shown_class = 0;   // a new game's unit: no hit class yet
    }

// player_join_init (FUN_00569f20) seeds the player's unit off the game seed
// (FUN_00552df0 with EDX game +0xd0, 0x569f4b) as its save's header is read
// (FUN_0056a090); its save's items follow, each a unit FUN_00558cb0 makes
// through FUN_00555230 type 4: FUN_00552df0 then FUN_00552e90, two game seed
// steps whatever the record (its own seed comes from its fingerprint after,
// FUN_0062cbe0). A socketed item is a record of its own (FUN_005335e0's
// inner loop), as are the corpse's and the merc's lists'.
auto Fight::player_joins(std::size_t saved_items) -> void {
        actor->seed = d2d::rules::Rng{ spawning.game.next() };
        for (std::size_t record = 0; record < saved_items; ++record) { spawning.game.next(); spawning.game.next(); }
    }

auto Fight::merc_joins() -> void {
        const auto& header = actor->character.header;
        actor->merc_target = -1; actor->merc_chase = false;
        actor->merc_hurt = 0; actor->merc_poison_rate = actor->merc_poison_acc = 0; actor->merc_poison_until = 0; actor->merc_heal_rate = actor->merc_heal_acc = 0; actor->merc_heal_until = 0; actor->merc_potion_states = {};
        actor->merc_growth = 0; actor->merc_aura = -1; actor->merc_skill = -1; actor->merc_seq = nullptr; actor->merc_buff = {}; actor->merc_cry = {}; actor->leap.reset(); actor->knock.reset();
        merc_level_stats();
        if (!actor->merc) return;
        actor->merc->hit_points = merc_max_life();
        actor->merc->mode = "NU"; actor->merc->next_act = 0; actor->merc->move_pct = 0;
        actor->merc->seed = d2d::rules::Rng(header.merc_seed);
        actor->merc->type = actor->merc_npc ? game_data->monsters.row(actor->merc_npc->id) : -1;
        // Given to the client (FUN_00571f90, a monster): SetStat(0x148, (x + y) & 0xffff), its subtile spot (FUN_00620870).
        const auto& joined = actor->merc->unit;
        actor->merc_pierce_idx = std::uint32_t(int(std::floor(joined.x * 5)) + int(std::floor(joined.y * 5))) & 0xffffu;
        actor->merc_given = { actor->id };
    }

// The merc's stats and skill levels for its experience (FUN_00572840).
auto Fight::merc_level_stats() -> void {
        const auto& header = actor->character.header;
        actor->merc_st = d2d::rules::merc_stats(game_data->rules, header.merc_type, header.merc_exp);
        actor->merc_ids = {}; actor->merc_levels = {};
        if (const auto* row = d2d::rules::hireling_row(game_data->rules, header.merc_type, actor->merc_st.level)) {
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k)
                if (const auto* skill = skill_named(row->skills[k].name)) actor->merc_ids[k] = skill->id;
            actor->merc_levels = d2d::rules::merc_skill_levels(*row, actor->merc_ids, actor->merc_st.level, [&](int id) {
                const auto* skill = game_data->skills.get(id);
                return skill ? skill->reqlevel : 0x7fff;
            });
        }
        // The level-up sets stat 0x4a again (FUN_00572840); a merc whose regeneration runs takes the new one.
        if (actor->merc && actor->merc->stats.regen > 0) actor->merc->stats.regen = d2d::rules::merc_life_regen(actor->merc_st.life);
    }

auto Fight::revive(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        // The respawn handler (client packet 0x41, FUN_0054c0e0, only for a dead
        // unit) sets stats 6, 8 and 0xa to the maxima of stats 7, 9 and 0xb
        // (FUN_00625d10 / 25d60 / 25db0): 0x54c17c, 0x54c18a, 0x54c198.
        static constexpr std::pair<StatId, StatId> kRefill[] = { { kLife, kMaxLife }, { kMana, kMaxMana }, { kStamina, kMaxStamina } };
        for (const auto& [cur, max] : kRefill)
            actor->character.stats.values[cur] = actor->character.stats.values[max];
        actor->regen.clear();
        actor->stamina_pot = {};
        actor->potion_states = {};
        actor->charges.clear();
        actor->self_states.clear();
        actor->curses[0] = {}; actor->marks[0] = {};
        actor->stunned = 0;
        actor->pmode = -1; actor->player.mode_ms = now_ms; actor->attack_mon = -1;
        if (actor->merc) { actor->merc->unit.x = actor->player.x + 1; actor->merc->unit.y = actor->player.y + 1; actor->merc->unit.path.clear(); actor->merc->mode = "NU"; actor->merc_target = -1; }
    }

// Keys 1-4 drink the belt's bottom-row potion in that column: healing
// and mana potions restore their amount (the class's bonus, a chance
// of double: rules::potion_amount) over their length, joined with
// what's left of the last one; a rejuvenation its percentages at once
// (FUN_005beac0, no bonus).

// The doubling rolls on the drinker's seed (unit +0x20, Hero::seed;
// rules::potion_amount).
auto Fight::drink(int col, std::uint32_t now_ms) -> void {
        if (!dead()) potion(d2d::rules::drink_belt(game_data->rules, actor->character.items, col), now_ms);
    }

// Shift with the key: the merc drinks it (FUN_00562390 with the merc
// flag: a live merc, ItemTypes hpot / apot / wpot only — rejuvenation
// counts as hpot; anything else stays in the belt). A healing potion
// heals it over its length, a rejuvenation its % at once. d2d keeps
// no merc chill for thawing to end. Healing as rules::potion_amount
// has it for a non-player (x2; doubled on its gear's vitality).

// An antidote ends its poison; either puts its state (+50 resist, +10
// max) on it as on the player (FUN_005bef90, FUN_005beda0).
auto Fight::feed_merc(int col, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        if (dead() || !actor->merc || actor->merc->hit_points <= 0) return;
        const auto& tables = game_data->rules;
        const auto bottle = std::ranges::find_if(actor->character.items, [&](const Item& item) { return item.location == item_location::kBelt && item.column == col; });
        if (bottle == actor->character.items.end() || !tables.item_info.contains(bottle->code)) return;
        const auto& type = tables.item_info.at(bottle->code).type;
        if (!d2d::rules::type_is(tables, type, "hpot") && !d2d::rules::type_is(tables, type, "apot") && !d2d::rules::type_is(tables, type, "wpot")) return;
        merc_drinks(d2d::rules::drink_belt(tables, actor->character.items, col), now_ms);
    }

// A healing, antidote or thawing potion in hand given to the merc (0x61,
// FUN_005bf240): true when it drank it.
auto Fight::feed_merc_item(const d2d::d2s::Item& item, std::uint32_t now_ms) -> bool {
        const auto& tables = game_data->rules;
        if (dead() || !actor->merc || actor->merc->hit_points <= 0 || !tables.item_info.contains(item.code) || !tables.potions.contains(item.code)) return false;
        const auto& type = tables.item_info.at(item.code).type;
        if (!d2d::rules::type_is(tables, type, "hpot") && !d2d::rules::type_is(tables, type, "apot") && !d2d::rules::type_is(tables, type, "wpot")) return false;
        merc_drinks(item.code, now_ms);
        return true;
    }

auto Fight::merc_drinks(const std::string& code, std::uint32_t now_ms) -> void {
        if (code.empty() || !actor->merc) return;
        const auto& potion = game_data->rules.potions.at(code);
        if (potion.antidote) { actor->merc_poison_until = 0; actor->merc_poison_acc = 0; }
        if (potion.antidote || potion.thawing) {
            auto& state = actor->merc_potion_states[potion.antidote ? 0 : 1];
            state.stats = potion.state_stats;
            state.until = std::max(state.until, now_ms) + std::uint32_t(std::max(potion.ticks, 0)) * 40;
        }
        if (potion.percent) actor->merc->hit_points = std::min(merc_max_life(), actor->merc->hit_points + merc_max_life() * potion.life / 100);
        else if (potion.life > 0) {
            const int len = std::max(potion.ticks, 1);
            const double amount = d2d::rules::potion_amount(potion.life, -1, true, int(merc_gear().sum[d2d::d2s::kVit]), actor->merc->seed) / 256.0;   // FUN_0062a5d0: a non-player's x2, on its seed
            const double left = actor->merc_heal_until > now_ms ? actor->merc_heal_rate * double(actor->merc_heal_until - now_ms) : 0;
            actor->merc_heal_until = std::max(actor->merc_heal_until, now_ms) + std::uint32_t(len) * 40;
            actor->merc_heal_rate = (left + amount) / double(actor->merc_heal_until - now_ms);
            // A non-player's potion state files its event 3 (FUN_005be3f0, 0x5be62b..0x5be659): its regeneration starts.
            if (actor->merc->hit_points > 0) actor->merc->stats.regen = d2d::rules::merc_life_regen(actor->merc_st.life);
        }
        cues.cue("item_potion_drink", now_ms, actor->merc->unit.x, actor->merc->unit.y);
    }

auto Fight::drink_item(int id, std::uint32_t now_ms) -> void {
        if (!dead()) potion(d2d::rules::drink_item(game_data->rules, actor->character.items, id), now_ms);
    }

auto Fight::potion(const std::string& code, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        if (code.empty()) return;
        const auto& potion = game_data->rules.potions.at(code);
        // Antidote / thawing (pSpell 6, FUN_005bef90): poison / chill gone,
        // then its own state (FUN_005beda0): its stats (+50 resist, +10 max),
        // the length (len, 750) added to what's left of one already on.
        if (potion.antidote) std::erase_if(actor->regen, [](const Regen& regen_entry) { return regen_entry.poison; });
        if (potion.thawing) actor->chilled = 0;
        if (potion.antidote || potion.thawing) {
            auto& state = actor->potion_states[potion.antidote ? 0 : 1];
            state.stats = potion.state_stats;
            state.until = std::max(state.until, now_ms) + std::uint32_t(std::max(potion.ticks, 0)) * 40;
        }
        if (potion.stamina) actor->stamina_pot = { potion.stamina, now_ms + std::uint32_t(std::max(potion.ticks, 1)) * 40 };
        if (potion.antidote || potion.thawing || potion.stamina) { cues.cue("item_potion_drink", now_ms, actor->player.x, actor->player.y); return; }
        if (potion.percent) {
            actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife] + actor->character.stats.values[kMaxLife] * potion.life / 100);
            actor->character.stats.values[kMana] = std::min(actor->character.stats.values[kMaxMana], actor->character.stats.values[kMana] + actor->character.stats.values[kMaxMana] * potion.mana / 100);
        } else {                                       // FUN_005be3f0: into the healthpot / manapot state, with what's left of it
            const bool life = potion.life > 0;
            const int stat = int(actor->character.stats.get(life ? kVit : kEne) + actor->character.panel.bonus[life ? kVit : kEne]);
            const int amount = d2d::rules::potion_amount(life ? potion.life : potion.mana, actor->character.character_class, life, stat, actor->seed);
            auto state = std::ranges::find_if(actor->regen, [&](const Regen& regen_entry) { return !regen_entry.poison && (life ? regen_entry.life > 0 : regen_entry.mana > 0); });
            if (state == actor->regen.end()) state = actor->regen.insert(actor->regen.end(), Regen{});
            const int left = state->until > now_ms ? int((state->until - now_ms) / 40) : 0;
            const int len = std::max(potion.ticks, 1);
            const int rate = d2d::rules::potion_rate(int(std::lround((life ? state->life : state->mana) * 40.0)), left, amount, len);
            (life ? state->life : state->mana) = rate / 40.0;
            state->until = now_ms + std::uint32_t(left + len) * 40;
        }
        cues.cue("item_potion_drink", now_ms, actor->player.x, actor->player.y);
    }

// A healer's and a well's cure (FUN_00578d30 / FUN_00585720): poison
// (state 2), freeze (state 1: a player's is chill, FUN_0057b230, so
// none) and the curses (FUN_00578c20, the states flagged at
// DataTables+0xfc); chill stays. Each pet and the merc (FUN_00574de0)
// gets its life filled and its poison and curse cleared (FUN_00578ca0 /
// FUN_005856a0). True when anything was: for a well (`well`) a pet's or
// the merc's life only, as FUN_005856a0 counts.
auto Fight::cure(std::uint32_t now_ms, bool well) -> bool {
        bool counts = now_ms < actor->curses[0].until;
        counts |= std::erase_if(actor->regen, [](const Regen& regen_entry) { return regen_entry.poison; }) > 0;
        actor->curses[0] = {};
        if (actor->merc && actor->merc->hit_points > 0) {
            const int max_life = merc_max_life();
            counts |= actor->merc->hit_points < max_life;
            counts |= !well && (now_ms < actor->curses[1].until || now_ms < actor->merc_poison_until);
            actor->merc->hit_points = max_life;
            actor->curses[1] = {};
            actor->merc_poison_until = 0; actor->merc_poison_acc = 0;
        }
        for (auto& pet : actor->pets)
            if (pet.monster.alive()) {
                counts |= pet.monster.hit_points < pet.monster.stats.hit_points;
                pet.monster.hit_points = pet.monster.stats.hit_points;
            }
        return counts;
    }

// Potions and poison, then the steady regeneration: replenish life
// (hpregen, N/256 a tick) and mana (rules::mana_per_frame, FUN_005806f0:
// max mana over CharStats ManaRegen seconds, a frame at a time).

// Poison is negative hpregen, and the player's regen tick (FUN_00580610)
// never takes life below 1: poison can't kill a player
// (docs/research/re/combat.md).
auto Fight::apply_regen(std::uint32_t now_ms, std::uint32_t last_ms) -> void {
        using namespace d2d::d2s;
        double life = 0, mana = 0;
        for (const auto& regen_entry : actor->regen) {
            const double duration = double(std::min(now_ms, regen_entry.until) - std::min(last_ms, regen_entry.until));
            life += regen_entry.life * duration; mana += regen_entry.mana * duration;
        }
        std::erase_if(actor->regen, [&](const Regen& regen_entry) { return now_ms >= regen_entry.until; });
        const double elapsed = double(now_ms - last_ms);
        life += actor->player_combat.life_regen * 256.0 / 256.0 * elapsed / 40.0;
        // The steady mana comes a whole frame at a time (FUN_005806f0).
        const auto& gains = game_data->class_gains[std::size_t(std::max(actor->character.character_class, 0))];
        // State 0x55 on (Prayer's): the ManaRegen part is skipped, manarecovery (26) alone.
        mana += double(d2d::rules::mana_per_frame(actor->character.stats.values[kMaxMana], gains.mana_regen, actor->player_combat.mana_regen, int(actor->psum[kManaRecovery]),
                                                  actor->no_mana_regen))
              * double(now_ms / 40 - last_ms / 40);
        actor->regen_acc_life += life; actor->regen_acc_mana += mana;
        if (actor->merc && actor->merc->hit_points > 0 && last_ms < actor->merc_poison_until) {   // its poison: life lost with the next hits
            actor->merc_poison_acc += actor->merc_poison_rate * double(std::min(now_ms, actor->merc_poison_until) - last_ms);
            const int loss = int(actor->merc_poison_acc);
            actor->merc_poison_acc -= loss;
            actor->merc_hurt += loss;
        }
        if (actor->merc && actor->merc->hit_points > 0 && last_ms < actor->merc_heal_until) {   // the merc's healthpot
            actor->merc_heal_acc += actor->merc_heal_rate * double(std::min(now_ms, actor->merc_heal_until) - last_ms);
            const int gain = int(actor->merc_heal_acc);
            actor->merc_heal_acc -= gain;
            actor->merc->hit_points = std::min(merc_max_life(), actor->merc->hit_points + gain);
        }
        // The merc's own regeneration, a monster's unit event 3 once a frame
        // (FUN_005a6920: a merc is unit type 1, FUN_005417d0 files it in list 1):
        // its total stat 0x4a (the level-up's, `stats.regen`, and its items'
        // Replenish Life) on its life. A total of 0 ends the event; a negative one
        // under one point of life does nothing in a town (FUN_0061ab00) or out of
        // a room; over its max life it's the max and the event ends; under one
        // point it dies. stats.regen is 0 while the event isn't filed (a hurt or a
        // poison files it in hurt_side, a healing potion in merc_drinks).
        if (actor->merc && actor->merc->stats.regen > 0 && actor->merc->hit_points > 0) {
            const std::int64_t total = actor->merc->stats.regen + merc_gear().sum[kHitPointRegeneration];
            const std::int64_t max_life = std::int64_t(merc_max_life()) << 8;
            const bool in_town = !level || d2d::rules::level_ids::is_town(level->id);
            std::int64_t merc_life = actor->merc->life_fine();
            for (auto frames = now_ms / 40 - last_ms / 40; frames > 0 && actor->merc->stats.regen > 0 && merc_life > 0; --frames) {
                if (total == 0) { actor->merc->stats.regen = 0; break; }
                if (total < 0 && merc_life < 0x100 && in_town) continue;
                merc_life += total;
                if (merc_life > max_life) { merc_life = max_life; actor->merc->stats.regen = 0; }
                merc_life = std::max<std::int64_t>(merc_life, 0);
            }
            actor->merc->set_life_fine(int(merc_life));
            if (merc_life == 0) {                                     // the kill (FUN_0057ccb0), as a hit's in hurt_side
                actor->merc->mode = "DT"; actor->merc->unit.mode_ms = now_ms; actor->merc->unit.walking = false; actor->merc->unit.path.clear();
                actor->merc->mode_until = now_ms + game_data->npc_timing(actor->merc->npc, "DT").length_ms();
            }
        }
        const auto life_gain = std::int64_t(actor->regen_acc_life), mana_gain = std::int64_t(actor->regen_acc_mana);
        actor->regen_acc_life -= double(life_gain); actor->regen_acc_mana -= double(mana_gain);
        auto& life_stat = actor->character.stats.values[kLife];
        if (life_gain < 0) life_stat = std::max<std::int64_t>(std::min<std::int64_t>(life_stat, 256), life_stat + life_gain);
        else life_stat = std::max(life_stat, std::min(actor->character.stats.values[kMaxLife], life_stat + life_gain));
        actor->character.stats.values[kMana] = std::max(actor->character.stats.values[kMana], std::min(actor->character.stats.values[kMaxMana], actor->character.stats.values[kMana] + mana_gain));
    }

auto Fight::monster_sounds(Monster& monster, std::uint32_t now_ms) -> void {
        const auto& superunique = monster.super >= 0 && std::size_t(monster.super) < game_data->superuniques.size() ? game_data->superuniques[std::size_t(monster.super)].sound : std::string();
        const auto found = monster.type < 0 ? game_data->mon_sounds.end()
                         : game_data->mon_sounds.find(d2d::rules::boss_sound(game_data->monsters.types[std::size_t(monster.type)], monster.boss, superunique));
        const GameData::MonSound* sounds = found == game_data->mon_sounds.end() ? nullptr : &found->second;
        const auto play = [&](int sound, std::uint32_t when) { cues.cue_on(unit_key(kSoundOnMonster, monster.id), sound, when, monster.unit.x, monster.unit.y); };
        const bool town = d2d::rules::level_ids::is_town(level->id);
        if (monster.flee_sound) {
            if (sounds) play(sounds->flee, now_ms);
            monster.flee_sound = false;
        }
        if (std::exchange(monster.help_sound, false)) play(d2d::rules::RescueQuest::kHelpMe, now_ms);
        if (monster.taunt_sound) {                                     // FUN_004cbde0 event 0x10: Taunt
            if (sounds) play(sounds->taunt, now_ms);
            monster.taunt_sound = false;
        }
        // Jab's event-3 rows (FUN_004f4590): a monster's dosound b, none once it's dead.
        if (monster.alive())
            for (int row = 0; row < monster.seq_sounds; ++row) play(monster.seq_sound, now_ms);
        monster.seq_sounds = 0;
        // As it first shows (FUN_004cc380): its Init, outside town, a Neutral's start.
        // A corpse (a joined game's monster already dead as it comes) waits:
        // FUN_004cc380 itself only gates on the +0x80 timer, its caller untraced.
        if (!monster.sounded && monster.alive()) {
            monster.sounded = true;
            if (sounds && sounds->init > 0 && !town) {
                play(sounds->init, now_ms);
                monster.neutral_ms = neutral_ms = now_ms;
                neutral_gap = 30 + sound_rng(90);
            }
        }
        // A skill it starts (S→C 0x4c / 0x4d, unit command 0x15 / 0x16 -> FUN_004c6140):
        // the start sounds as a player's, on a monster (unit type 1, never a class sound),
        // and its castoverlay. A stsuccessonly start stops before both when its
        // cltstfunc fails (FUN_004c6140 at 0x4c649e).
        if (const int started = std::exchange(monster.skill_sound, -1); started >= 0 && monster.alive())
            if (const auto* skill = game_data->skills.get(started)) {
                d2d::rules::ClientStartContext context;
                if (skill->clt_st_func == 17) {                       // Hydra's FUN_004e39a0
                    context.caster_has_room = room_of(*level, monster.unit.x, monster.unit.y) >= 0;
                    context.target_has_room = room_of(*level, (float(monster.skill_x) + 0.5f) / 5, (float(monster.skill_y) + 0.5f) / 5) >= 0;
                    context.target_in_town = d2d::rules::level_ids::is_town(level->id);
                    context.skill_in_town = skill->in_town;
                }
                // Charge's FUN_004c9b40 succeeds here: d2d records this start only
                // after its server path or in-reach swing has started, and every
                // live Monster owns the client routine's skill record and path.
                if (!skill->st_success_only || d2d::rules::client_start_succeeds(skill->clt_st_func, context)) {
                    const int rate = mode_rate(*game_data, monster, monster.mode);   // FUN_00623f50's +0x4c
                    const d2d::rules::sound_ids::SkillStart start{ .st_sound = skill->st_sound, .st_sound_class = skill->st_sound_class, .item_cast_sound = skill->item_cast_sound,
                                                                  .delay = skill->st_sound_delay, .weapon = skill->weapon_sound };
                    for (const auto cue : d2d::rules::sound_ids::skill_start(start, 1, monster.type, monster.npc.hit_class, rate, false, false))
                        if (cue.sound > 0) play(cue.sound, now_ms + std::uint32_t(std::max(cue.ticks, 0)) * 40);
                    start_cast_overlay(*skill, false, monster.one_shots, now_ms);   // and its castoverlay (Resurrect's)
                }
            }
        if (monster.mode != monster.last_mode) {
            monster.last_mode = monster.mode;
            monster.sound_ms = now_ms;                  // a mode's sound (unit +0x7c: FUN_004cb6a0, FUN_004cc410, FUN_004cb890)
            // Struck (FUN_004cc410): the hit's impact and its element's layer, but a critter's.
            // Its state 0x92 skip needs no test: only a gone Baal (FUN_005ef620) and
            // the Ancients' statues (FUN_0063efa0 case 0x219, objects here) take it.
            if ((monster.mode == "GH" || monster.mode == "KB" || monster.mode == "DT") && monster.hit_class
                && !(monster.type >= 0 && game_data->monsters.types[std::size_t(monster.type)].critter)) {
                const auto impact = d2d::rules::sound_ids::impact(monster.hit_class, monster.mode == "DT");
                cues.cue_on(unit_key(kSoundOnMonster, monster.id), impact.impact, now_ms, monster.unit.x, monster.unit.y);
                cues.cue_on(unit_key(kSoundOnMonster, monster.id), impact.element, now_ms, monster.unit.x, monster.unit.y);
                monster.hit_class = 0;
            }
            // Its skill's start (FUN_004c6140): cltstfunc 46 (FUN_004f0160, Baal
            // Taunt's) plays the prgsound on it.
            if (const auto* skill = monster.skill >= 0 ? game_data->skills.get(monster.skill) : nullptr; skill && skill->clt_st_func == 46 && skill->prg_sound > 0)
                play(skill->prg_sound, now_ms);
            if (sounds) {
                if (monster.mode == "A1" || monster.mode == "A2") {
                    const std::size_t attack_index = monster.mode == "A2";
                    if (int(sound_rng(100)) < sounds->att_prb[attack_index]) play(sounds->attack[attack_index], now_ms + std::uint32_t(sounds->att_del[attack_index]) * 40);
                    play(sounds->weapon[attack_index], now_ms + std::uint32_t(sounds->wea_del[attack_index]) * 40);
                } else if (monster.mode == "GH" || monster.mode == "KB" || monster.mode == "DT") {
                    namespace snd = d2d::rules::sound_ids;
                    const bool dying = monster.mode == "DT";
                    const int struck = dying ? sounds->death : sounds->hit;
                    play(struck, now_ms + std::uint32_t(dying ? sounds->death_del : sounds->hit_del) * 40);
                    // Diablo's death row brings its schwing and hit after it (FUN_004cc410 at 0x4cc520,
                    // tested on either row it plays).
                    if (struck == snd::kDiabloDeath) {
                        play(snd::kDiabloDeathSchwing, now_ms + std::uint32_t(snd::kDiabloSchwingTicks) * 40);
                        play(snd::kDiabloDeathHit, now_ms + std::uint32_t(snd::kDiabloHitTicks) * 40);
                    }
                } else if (monster.mode.size() == 2 && monster.mode[0] == 'S' && monster.mode[1] >= '1' && monster.mode[1] <= '4') {
                    play(sounds->skill[std::size_t(monster.mode[1] - '1')], now_ms);   // FUN_004cb890
                }
                // FUN_004cbde0's Taunt event (0x10) accompanies Fallen S2.
                // Other S2 modes have no Taunt row, so their skill sound stays
                // on the Skill2 path above.
                if (monster.mode == "S2") play(sounds->taunt, now_ms);
            }
        }
        const bool about = monster.mode == "NU" || monster.mode == "WL" || monster.mode == "RN";
        if (!sounds || !monster.alive() || !about) return;
        // Neutral (FUN_004cb460): a 1 in 15 roll a tick, a random 30..119 ticks after any
        // monster's last, NeuTime after its own last sound and Neutral, outside town, within
        // 31 subtiles of a player (FUN_004b9e10: dx^2 + (2 dy)^2 < 490000 in client pixels).
        const bool by_player = std::ranges::any_of(heroes, [&](const Hero* hero) {
            return hero && std::hypot(hero->player.x - monster.unit.x, hero->player.y - monster.unit.y) * 5 < 31; });
        const std::uint32_t own = std::uint32_t(sounds->neutral_time) * 40;
        if (sounds->neutral > 0 && !town && by_player && now_ms - neutral_ms >= neutral_gap * 40 && now_ms - monster.sound_ms >= own
            && now_ms - monster.neutral_ms >= own && now_ms / 40 != monster.sound_tick && (monster.sound_tick = now_ms / 40, sound_rng(15) == 0)) {
            play(sounds->neutral, now_ms);
            monster.neutral_ms = neutral_ms = now_ms;
            neutral_gap = 30 + sound_rng(90);
        }
        // Its footsteps walking or running (FUN_004caf60): FsCnt a cycle from FsOff, the
        // floor's material (its Footstep the dirt one), FootstepLayer at FsPrb %.
        if (monster.mode == "NU" || sounds->footstep <= 0 || sounds->fs_count <= 0) return;
        const auto& anim = game_data->npc_timing(monster.npc, monster.mode);
        const int rate = int(anim.speed ? anim.speed : anim.cof_speed ? anim.cof_speed : 256);
        const int length = int(anim.frames ? anim.frames : anim.cof_frames) * 256;
        if (length <= 0) return;
        const int pos = int((now_ms - monster.unit.mode_ms) / 40) * rate % length;
        if (now_ms / 40 == monster.step_ms / 40 || !d2d::rules::sound_ids::footfall(pos, rate, length, sounds->fs_count, sounds->fs_offset << 8)) return;
        if (sounds->fs_count > 1 && now_ms - monster.step_ms < std::uint32_t(2 * length / (sounds->fs_count * rate) / 3) * 40) return;
        monster.step_ms = now_ms;
        play(d2d::rules::sound_ids::footstep(sounds->footstep, floor_material_at(monster.unit.x, monster.unit.y), monster.mode == "RN"), now_ms);
        if (sounds->footstep_layer > 0 && int(sound_rng(100)) < sounds->fs_chance) play(sounds->footstep_layer, now_ms);
    }

// The floor's material at (x, y) cells (FUN_004cadb0): the first floor tile's DT1 flags, else the level's SoundEnviron Material 1.
auto Fight::floor_material_at(float x, float y) const -> int {
        const int cell_x = int(std::floor(x)), cell_y = int(std::floor(y));
        // FUN_004cadb0: the unit's room (FUN_004cace0), then each room of its
        // near list (FUN_00619790) when that room has no floor at the cell.
        // A neighbour level's tiles sit on its own origin.
        const int here = room_of(*level, x, y);
        struct Step {
            const std::vector<Level::Floor>* tiles = nullptr;
            int shift_x = 0;
            int shift_y = 0;
        };
        std::vector<Step> steps;
        auto add = [&](const Level* holder, int room) {
            if (!holder || room < 0 || std::size_t(room) >= holder->room_floors.size()) return;
            steps.push_back({ &holder->room_floors[std::size_t(room)], level->world_x - holder->world_x, level->world_y - holder->world_y });
        };
        if (here >= 0) add(level, here);
        if (game_data)
            for (const auto& [holder, room] : near_rooms(*game_data, *level, x, y))
                if (holder != level || room != here) add(holder, room);
        return d2d::rules::sound_ids::floor_material_near(steps, cell_x, cell_y, level->material);
    }

auto Fight::gfx() const -> const GameData::Appearance& {
        return actor->character.appearance ? *actor->character.appearance : game_data->starting_gear[std::size_t(std::max(actor->character.character_class, 0))];
    }

auto Fight::player_anim(int mode) const -> const GameData::AnimTiming& {
        return game_data->composite_timing(std::max(actor->character.character_class, 0), mode, gfx());
    }

// Impact and the class cry (FUN_004cc410). A hit too small for mode 4 still
// plays them: the client's command 0x13 calls FUN_004cc5b0 with mode 0x13,
// and that slot is the same struck handler. It does not change pmode.
auto Fight::play_player_hit(std::uint32_t now_ms) -> void {
        const bool dying = actor->pmode == kModeDT || actor->pmode == kModeDD;
        const auto impact = d2d::rules::sound_ids::impact(actor->struck_class, dying);
        const auto key = unit_key(kSoundOnPlayer, actor->id);
        cues.cue_on(key, impact.impact, now_ms, actor->player.x, actor->player.y);
        cues.cue_on(key, impact.element, now_ms, actor->player.x, actor->player.y);
        if (const int cls = actor->character.character_class; cls >= 0 && cls < 7) {
            const auto& voice = d2d::rules::sound_ids::kPlayerVoice[std::size_t(cls)];
            cues.cue_on(key, dying ? voice.death : voice.hit, now_ms, actor->player.x, actor->player.y, 0, dying ? 0 : Cues::kHush);
        }
    }

auto Fight::action_at() const -> std::uint32_t {
        return attack_mode(actor->pmode) ? actor->action_ms : std::uint32_t(float(player_anim(actor->pmode).action_ms()) / actor->prate);
    }

// A swing takes attack_ticks for the item attack speed and weapon
// speed; a cast takes cast_ticks (FCR), get-hit and block recover
// faster with FHR / FBR (rules::speed_frames on the base frame count).

// Sets the actor's player mode and runs the start-of-mode cues, incl. the inferno list of cltstfunc 15 (LAB_004e3060).
auto Fight::set_pmode(int mode, std::uint32_t now_ms) -> void {
        actor->pmode = mode;
        actor->seq = {};                                           // a new mode's frames: start_sequence's, or a cast sequence's below
        if (mode != kModeSC && mode != kModeA1) actor->owed = {};   // a cast cut off before its action frame pays nothing
        actor->player.mode_ms = now_ms;
        actor->player.walking = false;
        actor->player.path.clear();
        const auto& anim = player_anim(mode);
        std::uint32_t len = anim.length_ms();
        std::optional<std::uint32_t> strike;   // an attack's action tick in ms
        if (attack_mode(mode) && anim.frames) {
            // An Amazon's or Sorceress's plain swing starts frames in (FUN_00623b10); a skill that swings another record (a throw plays A1 here) does not.
            const auto* swing_row = game_data->skills.get(actor->swing_skill);
            const bool plain = actor->swing_skill == 0 || (swing_row && swing_row->anim == "A1");
            const int start = mode == kModeA1 && plain ? actor->player_combat.attack_start : 0;
            // The start's attackrate list (FUN_0056e520: Dragon Tail's -40) holds for this mode: rules::start_attack_rate.
            // The client's own list (cltstfunc 9, FUN_004f26d0: the row's Param4, Dragon Tail's only) is the same
            // value, but nothing the client plays from it depends on the rate here: Dragon Tail's KK has no
            // stsounddelay or weaponsnd, and a Bash-family start puts its list on the server's unit only (a
            // flags-4 list is not queued for sending). tools/emu/client_attackrate.py.
            const int start_rate = swing_row && actor->swing_skill != 0
                ? d2d::rules::start_attack_rate(game_data->skills, *swing_row, calc_env(), skill_level ? skill_level(actor->swing_skill) : 1) : 0;
            const int rate = int(anim.speed ? anim.speed : 256), sias = actor->player_combat.sias + chill_rate(now_ms) + start_rate;
            const auto ticks = d2d::rules::attack_ticks(int(anim.frames), rate, actor->player_combat.ias, actor->player_combat.wsm, sias, start);
            len = std::uint32_t(ticks) * 40;
            if (anim.action >= 0) strike = std::uint32_t(d2d::rules::attack_action_tick(anim.action, rate, actor->player_combat.ias, actor->player_combat.wsm, sias, start)) * 40;
            // The swing (FUN_004cc5b0: A1 / A2, a Barbarian's S3, an Assassin's S4): its weapon_* that many frames in.
            // A sequence plays in SQ (0x12), which the dispatcher leaves silent (the table at 0x4cc74c).
            const int cls = actor->character.character_class;
            if (skill_sequence().empty() && (mode == kModeA1 || mode == kModeA2 || (mode == kModeS3 && cls == d2d::d2s::kBarbarian) || (mode == kModeS4 && cls == d2d::d2s::kAssassin))) {
                const auto swing = d2d::rules::sound_ids::kSwing[std::size_t(actor->player_combat.hit_class & 0xf)];
                const int speed = d2d::rules::attack_speed(int(anim.speed ? anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, actor->player_combat.sias + chill_rate(now_ms));
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), swing.sound, now_ms + std::uint32_t((swing.frames * 256 + 128) / speed) * 40, actor->player.x, actor->player.y);
            }
        } else if (mode == kModeSC && anim.frames) {
            // A cast sequence (seqtrans SC, Lightning's 19 steps) runs out its steps at the cast speed
            // (rules::cast_sequence_ticks), each step's frame drawn in its turn (View::seq: +0x38 on by the
            // cast speed a tick, FUN_00623e00, its row's mode and frame, FUN_00621210).
            const auto* cast_row = game_data->skills.get(actor->swing_skill);
            const auto steps = cast_row && cast_row->seqtrans == "SC" ? skill_sequence() : std::span<const d2d::rules::SeqFrame>{};
            const int rate = int(anim.speed ? anim.speed : 256), fcr = actor->player_combat.fcr;
            len = std::uint32_t(steps.empty() ? d2d::rules::cast_ticks(int(anim.frames), rate, fcr) : d2d::rules::cast_sequence_ticks(int(steps.size()), rate, fcr)) * 40;
            actor->seq = steps;
            actor->seq_loop = false;
            actor->seq_frame_ms = std::uint32_t(std::max(40 * 256 / std::max(d2d::rules::cast_speed(rate, fcr), 1), 1));
        } else if (mode == kModeGH && anim.frames) {
            // The client draws its frames at the get-hit rate and ends a tick early (FUN_00463390: the hero's draw, session/draw.cpp).
            len = d2d::game::get_hit_ms(anim, actor->player_combat.fhr);
        } else if (mode == kModeBL && anim.frames) {
            // FUN_00623f50's block branch: the rate adds 50 with Holy Shield's state, then the get-hit's end event.
            len = std::uint32_t(d2d::rules::block_ticks(int(anim.frames), int(anim.speed ? anim.speed : 256), actor->player_combat.fbr, state_of("holyshield") != nullptr)) * 40;
        }
        actor->prate = len ? float(anim.length_ms()) / float(len) : 1.f;
        actor->action_ms = strike ? *strike : std::uint32_t(float(anim.action_ms()) / actor->prate);
        if (mode == kModeSC && anim.frames && anim.action >= 0) {
            // A cast sequence's action is its event-1 step (Lightning's 7): FUN_00623e00 moves the position
            // (+0x38, from 0) on by the cast speed (+0x3c) a tick and FUN_00663410 reports the events of the
            // steps it passed, so it fires on the first tick reaching 256 x step, as a plain cast's frame.
            const auto* cast_row = game_data->skills.get(actor->swing_skill);
            const auto steps = cast_row && cast_row->seqtrans == "SC" ? skill_sequence() : std::span<const d2d::rules::SeqFrame>{};
            const auto hit = std::ranges::find(steps, std::uint8_t{ 1 }, &d2d::rules::SeqFrame::event);
            const int action = hit == steps.end() ? anim.action : int(hit - steps.begin());
            actor->action_ms = std::uint32_t(d2d::rules::cast_action_ticks(action, int(anim.speed ? anim.speed : 256), actor->player_combat.fcr)) * 40;
        }
        actor->pmode_until = mode == kModeDD ? 0 : now_ms + len;
        // Struck or dying (FUN_004cc410). The get-hit cry is hushed while a speech line plays.
        if (mode == kModeGH || mode == kModeDT) {
            play_player_hit(now_ms);
            actor->struck_class = 0;
        }
        // A block (FUN_004cb860): by the hit class the client last had (unit +0xb0), the
        // last landed hit's. A blocked hit clears its flag 1 (FUN_0057ec10), so FUN_0057cee0
        // keeps the old class and no hit packet (S→C 0x0c) carries a new one.
        if (mode == kModeBL)
            cues.cue(d2d::rules::sound_ids::block(actor->shown_class), now_ms, actor->player.x, actor->player.y);
        // A kick (FUN_004cc5b0's KK): weapon_kick, an Assassin's 4 ticks on.
        if (mode == kModeKK)
            cues.cue(d2d::rules::sound_ids::kKick, now_ms + (actor->character.character_class == d2d::d2s::kAssassin ? d2d::rules::sound_ids::kKickDelayMs : 0u), actor->player.x, actor->player.y);
        // A skill starting (FUN_004c6140): its stsound (its ItemCastSound from an item), the
        // weapon's swing for weaponsnd, and the class's own stsoundclass; stsounddelay holds
        // them back the swing's frames less 3 ticks (FUN_004ca500). Then its castoverlay
        // (ItemCastOverlay from an item), with no delay.
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && (attack_mode(mode) || mode == kModeSC)) {
            static constexpr std::array<std::string_view, 7> kClassCode = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
            const auto swing = d2d::rules::sound_ids::kSwing[std::size_t(actor->player_combat.hit_class & 0xf)];
            const int rate = int(anim.speed ? anim.speed : 256);
            const int speed = attack_mode(mode) ? d2d::rules::attack_speed(rate, actor->player_combat.ias, actor->player_combat.wsm, actor->player_combat.sias + chill_rate(now_ms)) : rate;
            const int swing_ticks = (swing.frames * 256 + 128) / speed;
            const std::uint32_t start_at = now_ms + (skill->st_sound_delay ? std::uint32_t(std::max(swing_ticks - 3, 0)) * 40 : 0);
            const bool item = from_charges(skill->id) && skill->item_cast_sound > 0;   // a charged entry (its owner's guid, FUN_004c6140)
            cues.cue_on(unit_key(kSoundOnPlayer, actor->id), item ? skill->item_cast_sound : skill->st_sound, start_at, actor->player.x, actor->player.y);
            if (skill->weapon_sound) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), swing.sound, now_ms + std::uint32_t(swing_ticks) * 40, actor->player.x, actor->player.y);
            const int cls = actor->character.character_class;
            if (!item && cls >= 0 && std::size_t(cls) < kClassCode.size() && skill->cls == kClassCode[std::size_t(cls)])
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), skill->st_sound_class, start_at, actor->player.x, actor->player.y);
            start_cast_overlay(*skill, from_charges(skill->id), actor->one_shots, now_ms);   // its castoverlay, at once
            // Cltstfunc 15 (LAB_004e3060: Inferno, Arctic Blast). While state inferno (0xc) is off on the client's
            // unit, the prgsound on no unit (FUN_004b9a00, EDX 0: unplaced, the listener's). Then the state's list:
            // found, its timeout is set to 7 frames, else the client's own is made with 0x15 (FUN_006251f0 flags 2).
            // With the state off, first its prgoverlay (+0x10c, a row in the table: FUN_00470390 kind 2 at
            // 0x4e30c2, the row's playing one restarted: Arctic Blast's arctic_blast_cast), then the sound.
            // The server never sends state 0xc (States.txt inferno is nosend: FUN_005711d0 skips +0x10 bit 0 at
            // 0x5712f0), so the client's unit has no server list or bit for it: only its own (Hero::inferno_list;
            // the server's Hero::burning doesn't count). tools/emu/state_nosend.py.
            // With the list there it puts the sequence at frame 8 (FUN_006212c0) with 0x500 to run (+0x48,
            // 0x4e3154): not ported, start_sequence sets the frames after this.
            if (skill->clt_st_func == 15) inferno_client_start(*skill, now_ms);
        }
    }

// Cltstfunc 15's list and its sound (above); a held Inferno's next cast runs it too (Fight::breath_again):
// the start's branch for a list already on (7 frames, no sound) is that one's. Only the client's own list
// counts: state 0xc is nosend (FUN_005711d0), so the server's never reaches the client.
auto Fight::inferno_client_start(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const bool state_on = now_ms < actor->inferno_list;
        if (const int row = game_data->overlay_row(skill.prg_overlay); !state_on && row > 0 && std::size_t(row) < game_data->overlay_rows.size()
            && game_data->overlay_rows[std::size_t(row)])
            d2d::rules::add_one_shot(actor->one_shots, row, now_ms);
        if (!state_on) cues.cue_on(0, skill.prg_sound, now_ms, actor->player.x, actor->player.y);
        actor->inferno_list = now_ms + std::uint32_t(state_on ? 7 : 0x15) * 40;
    }

// The skill's do (FUN_004c6680, its client do-function done): dosound on the
// caster, tgtsound on its target (FUN_004648f0; none, no sound).
auto Fight::do_sounds(std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (!skill) return;
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), skill->do_sound, now_ms, actor->player.x, actor->player.y);
        if (const int target = actor->attack_mon; target >= 0 && std::size_t(target) < monsters.size() && skill->tgt_sound > 0)
            cues.cue_on(unit_key(kSoundOnMonster, monsters[std::size_t(target)].id), skill->tgt_sound, now_ms, monsters[std::size_t(target)].unit.x, monsters[std::size_t(target)].unit.y);
    }

auto Fight::dead() const -> bool { return actor->pmode == kModeDT || actor->pmode == kModeDD; }

// FUN_0057f330 -> FUN_00639fb0: start the player's death and prune the modeled
// player states without touching the living merc's independent state bitmap.
auto Fight::die(std::uint32_t now_ms) -> void {
    actor->character.stats.values[d2d::d2s::kLife] = 0;
    actor->killer = std::exchange(actor->struck_by, {});
    set_pmode(kModeDT, now_ms);
    actor->attack_mon = -1;
    actor->smove = {};           // a Whirlwind / Charge / Leap cut short isn't resumed by the next attack mode
    actor->kicks_left = 0;
    // FUN_0057f330 -> FUN_00639fb0 (a player, boss 0): each covered state whose States.txt row does not mark
    // plrstaydeath leaves the player's bitmap. The merc is a separate, living unit and is not passed to this call.
    std::vector<std::uint32_t> held_bits((game_data->state_names.size() + 31) / 32), parked_bits(held_bits.size()), stay_mask(held_bits.size());
    for (const auto& [state_name, info] : game_data->states)
        if (info.plr_stay_death && info.row >= 0) stay_mask[std::size_t(info.row) / 32] |= 1u << (info.row % 32);
    const auto add_state = [&](std::string_view state_name) {
        const auto info = game_data->states.find(std::string(state_name));
        if (info != game_data->states.end() && info->second.row >= 0)
            held_bits[std::size_t(info->second.row) / 32] |= 1u << (info->second.row % 32);
    };
    const auto skill_state = [&](int skill_id, bool target) -> std::string_view {
        const auto* skill = game_data->skills.get(skill_id);
        return !skill ? std::string_view{} : target ? std::string_view(skill->auratarget) : std::string_view(skill->aurastate);
    };
    for (const auto& state : actor->self_states) add_state(skill_state(state.skill, false));
    for (const auto& charge : actor->charges) add_state(skill_state(charge.skill, false));
    add_state(skill_state(actor->aura, false));
    add_state(skill_state(actor->curses[0].skill, true));
    add_state(skill_state(actor->marks[0].skill, true));
    for (const auto& regen : actor->regen) {
        if (regen.poison) add_state("poison");
        else {
            if (regen.life != 0) add_state("healthpot");
            if (regen.mana != 0) add_state("manapot");
        }
    }
    if (actor->potion_states[0].until != 0) add_state("antidote");
    if (actor->potion_states[1].until != 0) add_state("thawing");
    if (actor->stamina_pot.until != 0) add_state("staminapot");
    if (actor->chilled != 0) add_state("cold");
    if (actor->burning != 0) add_state("inferno");
    if (actor->stunned != 0) add_state("stunned");
    if (actor->no_mana_regen) add_state("nomanaregen");
    if (actor->skill_delay != 0) add_state("skilldelay");
    if (const int shrine_row = actor->boost.shrine; actor->boost.until != 0 && shrine_row > 0 && std::size_t(shrine_row) < game_data->shrines.size())
        add_state(d2d::rules::shrine_state(game_data->shrines[std::size_t(shrine_row)].code));
    d2d::rules::states_leave_on_death(held_bits, parked_bits, stay_mask);
    const auto state_kept = [&](std::string_view state_name) {
        const auto info = game_data->states.find(std::string(state_name));
        const int row = info != game_data->states.end() ? info->second.row : -1;
        return row < 0 || (held_bits[std::size_t(row) / 32] >> (row % 32) & 1u) != 0;
    };
    std::erase_if(actor->self_states, [&](const SelfState& state) {
        return !state_kept(skill_state(state.skill, false));
    });
    std::erase_if(actor->charges, [&](const Charge& charge) {
        return !state_kept(skill_state(charge.skill, false));
    });
    if (!state_kept(skill_state(actor->aura, false))) actor->aura = 0;
    if (!state_kept(skill_state(actor->curses[0].skill, true))) actor->curses[0] = {};
    if (!state_kept(skill_state(actor->marks[0].skill, true))) actor->marks[0] = {};
    std::erase_if(actor->regen, [&](const Regen& regen) {
        if (regen.poison) return !state_kept("poison");
        return (regen.life != 0 && !state_kept("healthpot")) || (regen.mana != 0 && !state_kept("manapot"));
    });
    if (!state_kept("antidote")) actor->potion_states[0] = {};
    if (!state_kept("thawing")) actor->potion_states[1] = {};
    if (!state_kept("staminapot")) actor->stamina_pot = {};
    if (!state_kept("cold")) actor->chilled = 0;
    if (!state_kept("inferno")) actor->burning = 0;
    if (!state_kept("stunned")) actor->stunned = 0;
    if (!state_kept("nomanaregen")) actor->no_mana_regen = false;
    if (!state_kept("skilldelay")) actor->skill_delay = 0;
    if (const int shrine_row = actor->boost.shrine; shrine_row > 0 && std::size_t(shrine_row) < game_data->shrines.size()
        && !state_kept(d2d::rules::shrine_state(game_data->shrines[std::size_t(shrine_row)].code))) actor->boost = {};
    d2d::log::info("the player died");
}

}  // namespace d2d::game
