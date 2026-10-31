// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): casting, targets and the fight's part of the tick. The rest is in this
// directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "internal.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

// Diablo's score of a pet or monster (FUN_005e8530 reads any unit's stats
// so): its resists as stats 0x24 / 0x25 / 0x27 / 0x29 / 0x2b, raw, and its
// aura as its right skill (FUN_005701b0 sets it: a merc's FUN_005e4d30, a
// summon's FUN_005c4470, Aura Enchanted's FUN_005a1650) at its level.
// A monster's or summon's max damage is its last swing's (rules::swing_max_damage),
// the merc's its max damage stats (merc_foe).
// None has a left skill: every FUN_005701b0 call with EDX 1 is a player's
// (FUN_0055c5c0, FUN_0056af80 test unit type 0; FUN_0055da70 / FUN_00580280
// put a player's saved hands back).
d2d::rules::DiabloPick unit_pick(const GameData& game_data, std::array<int, 5> res, int aura, int aura_level, int max_damage) {
    d2d::rules::DiabloPick pick;
    pick.max_damage = max_damage;
    std::tie(pick.damage_resist, pick.magic_resist, pick.fire_resist, pick.light_resist, pick.cold_resist) = std::tuple{ res[0], res[1], res[2], res[3], res[4] };
    if (const auto* skill = aura > 0 ? game_data.skills.get(aura) : nullptr) { pick.right_skill = skill->id; pick.right_level = aura_level; pick.right_rank = skill->attack_rank; }
    return pick;
}

// A cast's cost (FUN_0056bfe0, after the skill's start or do function):
// from a charged entry for that skill, one charge (FUN_0056bec0) and no
// mana; else its mana.
auto Fight::from_charges(int skill) const -> bool { return actor->item_charge.item >= 0 && actor->item_charge.skill == skill; }

auto Fight::charge_live() const -> bool {
        const auto& cast = actor->item_charge;
        return std::ranges::any_of(charged_skills(actor->character.items),
                                   [&](const ChargedSkill& had) { return had.item == cast.item && had.skill == cast.skill && had.level == cast.level; });
    }

auto Fight::cast_level(int skill) const -> int {
        if (from_charges(skill) && !charge_live()) return 0;
        return skill_level ? skill_level(skill) : 0;
    }

auto Fight::cost_of(const d2d::rules::Skill& skill, int lvl) const -> int { return from_charges(skill.id) ? 0 : d2d::rules::mana_cost(skill, lvl); }

auto Fight::pay(const d2d::rules::Skill& skill, int cost) -> void {
        if (from_charges(skill.id)) (void)use_charge(actor->character.items, actor->item_charge);
        else actor->character.stats.values[d2d::d2s::kMana] -= cost;
    }

auto Fight::cast(int skill, std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || !self_cast(*skill_row)) return false;
        const int lvl = cast_level(skill);
        const int cost = cost_of(*skill_row, lvl);
        if (lvl > 0 && actor->character.stats.values[kMana] < cost) need_mana(now_ms);
        if (lvl <= 0 || actor->character.stats.values[kMana] < cost) return false;
        pay(*skill_row, cost);
        actor->swing_skill = skill;
        actor->attack_mon = -1;
        set_pmode(kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::frenzy(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const auto found = std::ranges::find(actor->self_states, skill.id, &SelfState::skill);
        const int frenzy_charges = std::min(found == actor->self_states.end() ? 1 : found->level + 1, lvl);
        const auto until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data->skills, skill.auralen, env, skill.id, lvl), 1)) * 40;
        if (found == actor->self_states.end()) actor->self_states.push_back({ skill.id, frenzy_charges, until });
        else *found = { skill.id, frenzy_charges, until };
    }

auto Fight::next_target() -> bool {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (skill && skill->srvdofunc == ServerDoFunction::kRepeatedHit) { other_target(); return actor->attack_mon >= 0; }
        return actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive();
    }

auto Fight::weapon_range() const -> int {
        for (const int slot : { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm }) {
            const auto held = std::ranges::find_if(actor->character.items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
            if (held == actor->character.items.end()) continue;
            const auto info = game_data->rules.item_info.find(held->code);
            if (info != game_data->rules.item_info.end() && d2d::rules::type_is(game_data->rules, info->second.type, "weap") && (held->flags & 0x4100u) == 0) return info->second.range_adder;
        }
        return 0;
    }

auto Fight::in_reach() const -> std::vector<int> {
        std::vector<int> out;
        if (d2d::rules::level_ids::is_town(level->id)) return out;
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        const auto wall = [&](std::uint16_t mask) { return [this, mask](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, mask); }; };
        const int range = weapon_range(), radius = range + 4, player_x = sub(actor->player.x), player_y = sub(actor->player.y);
        const auto rooms = near_rooms(*game_data, *level, actor->player.x, actor->player.y);
        for (std::size_t i = 0; i < monsters.size(); ++i) {
            const auto& monster = monsters[i];
            if (!monster.targetable() || monster.align == 2) continue;   // a converted one is no foe (FUN_00554200)
            if (!std::ranges::contains(rooms, std::pair{ level, room_of(*level, monster.unit.x, monster.unit.y) })) continue;
            const int dx = sub(monster.unit.x) - player_x, dy = sub(monster.unit.y) - player_y;
            if (dx * dx + dy * dy > radius * radius) continue;
            const int size = game_data->monsters.types[std::size_t(monster.type)].size;
            const int apart = d2d::rules::unit_distance(dx, dy, 2, size);
            const bool tentacle = monster.type == int(game_data->mon_bin[0x102]) || monster.type == int(game_data->mon_bin[0x105]);
            const bool melee = (tentacle && apart < range + 8) || apart < 1
                            || (apart <= range + 1 && !d2d::rules::sight_blocked(player_x, player_y, 2, player_x + dx, player_y + dy, size, wall(0x804)));
            if (melee && !d2d::rules::sight_blocked(player_x, player_y, 0, player_x + dx, player_y + dy, 0, wall(0x04))) out.push_back(int(i));
        }
        return out;
    }

auto Fight::engage(std::uint32_t now_ms) -> std::optional<std::pair<float, float>> {
        if (actor->attack_mon < 0 || actor->pmode >= 0) return std::nullopt;
        const auto& target = monsters[std::size_t(actor->attack_mon)];
        // Dead, or turned to the player's side while the button was held (Conversion): dropped, as
        // World::apply turns a fresh command on it away.
        if (!target.alive() || target.ally_not_interact(*game_data)) { actor->attack_mon = -1; return std::nullopt; }
        const auto* skill = game_data->skills.get(actor->attack_skill);
        if (skill && (missile_skill(*skill) || spot_skill(*skill))) {   // from here, at it
            cast_missile(actor->attack_skill, target.unit.x, target.unit.y, now_ms);
            return std::nullopt;
        }
        if (skill && moving_skill(*skill) && (skill->srvdofunc != ServerDoFunction::kCharge || std::hypot(target.unit.x - actor->player.x, target.unit.y - actor->player.y) > kMeleeReach)) {
            actor->player.dir = direction16(target.unit.x - actor->player.x, target.unit.y - actor->player.y);
            actor->move_x = target.unit.x; actor->move_y = target.unit.y;                  // Whirlwind to it, Charge and Leap Attack at it
            start_swing(now_ms);
            return std::nullopt;
        }
        if (std::hypot(target.unit.x - actor->player.x, target.unit.y - actor->player.y) <= kMeleeReach) {
            actor->player.dir = direction16(target.unit.x - actor->player.x, target.unit.y - actor->player.y);
            start_swing(now_ms);
            return std::nullopt;
        }
        return std::pair{ target.unit.x, target.unit.y };
    }

auto Fight::crowd(Crowd& crowd_out) -> void {
        for (auto& monster : monsters)
            if (monster.targetable() && std::abs(monster.unit.x - actor->player.x) < 12 && std::abs(monster.unit.y - actor->player.y) < 12) crowd_out.units.push_back(&monster.unit);
        for (auto& pet : actor->pets)                                 // pets stand in the way too (Bone Wall)
            if (pet.monster.alive() && pet.where == level) crowd_out.units.push_back(&pet.monster.unit);
    }

auto Fight::player_foe(std::uint32_t now_ms) -> Foe {
        Foe foe{ actor->player.x, actor->player.y, int(actor->character.stats.get(d2d::d2s::kLevel)), !dead(), actor->player.walking, actor->player_combat };
        foe.running = actor->owner_mode == 3;                   // RN (World: walking with run on and stamina)
        foe.neutral = actor->pmode < 0 && actor->owner_mode == 1;   // mode 1: no swing, cast or get-hit, not moving
        foe.id = player_foe_id();
        foe.life_pct = int(std::int64_t(actor->character.stats.get(d2d::d2s::kLife)) * 100 / std::max<std::int64_t>(actor->character.stats.get(d2d::d2s::kMaxLife), 1));
        // Diablo's score of the player (FUN_005e8530).
        const auto stat = [&](int id) { return int(actor->psum[std::size_t(id)]); };
        auto& pick = foe.pick;
        pick.damage_resist = stat(0x24); pick.magic_resist = stat(0x25); pick.fire_resist = stat(0x27); pick.light_resist = stat(0x29); pick.cold_resist = stat(0x2b);
        pick.max_damage = stat(0x16) + stat(0x31) + stat(0x33) + stat(0x35) + stat(0x37) + (stat(0x3a) >> 8);
        pick.state_b = now_ms < actor->chilled;
        const auto hand = [&](std::uint32_t id, int& out_id, int& out_level, int& out_rank) {
            const auto* skill = game_data->skills.get(int(id));
            out_id = skill ? skill->id : -1; out_level = skill && skill_level ? skill_level(skill->id) : skill ? 1 : 0; out_rank = skill ? skill->attack_rank : 0;
        };
        hand(actor->character.header.left_skill, pick.left_skill, pick.left_level, pick.left_rank);
        hand(actor->character.header.right_skill, pick.right_skill, pick.right_level, pick.right_rank);
        on_foe_states(foe, 0, now_ms);
        return foe;
    }

auto Fight::merc_foe(std::uint32_t now_ms) -> Foe {
        if (now_ms >= actor->curses[1].until) actor->curses[1] = {};   // merc_fighter reads one still on
        Foe foe{ actor->merc ? actor->merc->unit.x : 0, actor->merc ? actor->merc->unit.y : 0, actor->merc_st.level, actor->merc && actor->merc->mode != "DT",
                 actor->merc && actor->merc->unit.walking, merc_fighter() };
        foe.pet = true;                                  // the merc: its MonStats class's size
        foe.id = merc_foe_id();
        foe.neutral = actor->merc && actor->merc->mode == "NU";
        foe.life_pct = actor->merc ? int(std::int64_t(actor->merc->hit_points) * 100 / std::max(merc_max_life(), 1)) : 0;
        if (const int row = actor->merc_npc ? game_data->monsters.row(actor->merc_npc->id) : -1; row >= 0) {
            const auto& type_info = game_data->monsters.types[std::size_t(row)];
            foe.size = type_info.size; foe.threat = type_info.threat;
            foe.knock_under = type_info.large ? 0x20 : type_info.small ? 0x80 : 0x40;   // as a monster's (monster_foe_props); crush a hireling's 10
        }
        // The merc's curse's damageresist (Amplify Damage's -100 %) for
        // Diablo's score; merc_fighter has all its aurastats.
        int curse_dr = 0;
        if (const auto* skill = actor->curses[1].skill >= 0 ? game_data->skills.get(actor->curses[1].skill) : nullptr)
            for (std::size_t i = 0; i < skill->aurastat.size(); ++i)
                if (skill->aurastat[i] == d2d::d2s::kDamageResist) curse_dr += calc(*skill, skill->aura_calc[i], actor->curses[1].level);
        if (actor->merc) {                                          // its stats for Diablo's score: base + gear + curse, raw (no penalty or cap)
            const auto sum = merc_gear().sum;
            const auto stat = [&](int id) { return int(sum[std::size_t(id)]); };
            int aura_level = 0;
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k) if (actor->merc_ids[k] == actor->merc_aura) aura_level = actor->merc_levels[k];
            foe.pick = unit_pick(*game_data, { stat(0x24) + curse_dr, stat(0x25), actor->merc_st.resist + stat(0x27), actor->merc_st.resist + stat(0x29), actor->merc_st.resist + stat(0x2b) },
                                 actor->merc_aura, aura_level,
                                 stat(0x16) + stat(0x31) + stat(0x33) + stat(0x35) + stat(0x37) + (stat(0x3a) >> 8));   // 0x16 holds no base (merc_fighter)
        }
        on_foe_states(foe, 1, now_ms);
        return foe;
    }

// The states on the player (which 0) or the merc (1) the AIs read (their skills'
// +0x82), and the zealots' spare (FUN_0065c310(pcdata +0x10 + diff * 4,
// 0x15, 0): a pet's owner's, FUN_0058f090).
auto Fight::on_foe_states(Foe& foe, std::size_t which, std::uint32_t now_ms) const -> void {
        for (const auto* held : { &actor->curses[which], &actor->marks[which] })
            if (const auto* skill = now_ms < held->until ? game_data->skills.get(held->skill) : nullptr) foe.states.push_back(skill->auratarget);
        foe.a3q5_done = zealots_spare();
    }

auto Fight::zealots_spare() const -> bool {
        return d2d::rules::quest::bit(actor->character.header.quests[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))], 0x15, 0);
    }

auto Fight::near_players(float x, float y) const -> bool {
        return std::ranges::any_of(heroes, [&](const Hero* hero) { return std::abs(x - hero->player.x) < 30 && std::abs(y - hero->player.y) < 30; });
    }

auto Fight::world(bool in_moor, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        Hero& acting = *actor;
        // Monsters think while a player is near (D2 runs the rooms around
        // each player); what they hit comes off that player's life, and a hit
        // of a twelfth of max life or more makes them flinch (GH).
        if (in_moor) {
            // Each player's side: the player and the merc, then everyone's
            // pets; the monsters (Confuse, Attract) after them.
            struct Side { Hero* hero; std::size_t at = 0, pets = 0; };
            std::vector<Side> sides;
            std::vector<Foe> foes;
            for (Hero* hero : heroes) {
                act_as(*hero);
                sides.push_back({ hero, foes.size() });
                foes.push_back(player_foe(now_ms));
                foes.push_back(merc_foe(now_ms));
            }
            monster_auras(foes, now_ms);
            for (auto& side : sides) {
                act_as(*side.hero);
                side.pets = foes.size();
                for (const auto& pet : actor->pets) foes.push_back(pet_foe(pet));
                for (std::size_t k = side.pets; k < foes.size(); ++k) foes[k].a3q5_done = zealots_spare();
            }
            // With Confuse or Attract about, every monster is a foe too, after
            // the players' lists (lists 8 / 9, FUN_005dd0b0 mode 5's units),
            // hit as a player is (combat.md). Its death drops; the player
            // gets the experience when the killer or the dead one carries
            // its curse (FUN_0057e7b0: the stat list's owner, flag 0x800).
            // ponytail: a monster's missiles, poison and Mana Burn reach only
            // the players' side; monsters block as a player does; the
            // experience is the first player's.
            const std::size_t allies = foes.size();
            if (std::ranges::any_of(monsters, [](const Monster& monster) { return monster.align || monster.set_kind; }))
                for (const auto& monster : monsters) {
                    const auto target = monster.target(*game_data);
                    auto fighter = d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit, target.armor_class);
                    fighter.block = target.block;
                    for (std::size_t k = 0; k < 4; ++k) fighter.res[k] = std::min(target.res[2 + k], 95);
                    const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
                    Foe& foe = foes.emplace_back(Foe{ monster.unit.x, monster.unit.y, monster.stats.level, monster.targetable() && monster.mode != "DT", monster.unit.walking, fighter });
                    foe.size = type_info.size; foe.threat = type_info.threat; foe.of = &monster; foe.id = monster.id;
                    monster_foe_props(*game_data, monster, foe);
                    foe.pick = unit_pick(*game_data, { target.res[0], target.res[1], target.res[2], target.res[3], target.res[4] }, monster.aura, monster.aura_lvl,
                                         d2d::rules::swing_max_damage(monster.stats, monster.swing_mode));
                    foe.pick.state_b = now_ms < monster.chill_until;   // state 0xb, cold
                }
            std::vector<Monster> born;
            auto* rooms_in_play = &spawning.levels[level];
            act_as(*heroes.front());
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                auto& monster = monsters[i];
                if (near_players(monster.unit.x, monster.unit.y)
                    && monster_update(*game_data, *level, monster, foes, rng, now_ms, elapsed, crowd, missiles, monsters, &born, &area_seen, &open_door, rooms_in_play))
                    killed(i, now_ms);                           // on a player's thorns
            }
            const auto cursed = [&](const Monster& monster) { return monster.curse.skill >= 0 && now_ms < monster.curse.until; };
            for (std::size_t i = allies; i < foes.size(); ++i) {
                const auto monster_index = i - allies;
                auto& struck = monsters[monster_index];
                const bool was_alive = struck.alive();
                // One damage record a landed blow (FUN_0057c6c0 then FUN_0057cee0's monster branch, which a
                // pet or a hireling takes too): its own armor pool, then its get-hit on its own class and mark.
                bool dead = false;
                for (const auto& blow : foes[i].blows)
                    if (!dead) dead = hurt(*game_data, struck, soak(struck, blow.physical, blow.damage), now_ms, 0, blow.hit_class, blow.marked);
                // Its Crushing Blows after the hits, less its own damageresist
                // (FUN_005bffc0), and a knockback as the player's (knock_back).
                for (const int divisor : foes[i].crushed)
                    if (!dead && struck.alive()) dead = hurt(*game_data, struck, int(crushing_blow(struck.hit_points, divisor, struck.target(*game_data).res[0])), now_ms);
                if (!dead && was_alive && struck.alive() && foes[i].knocked_from) knock_back(struck, foes[i].knocked_from->first, foes[i].knocked_from->second, now_ms);
                if (dead)
                    killed(monster_index, now_ms, cursed(struck) || std::ranges::any_of(foes[i].melee_by, [&](int striker) { const int at_index = this->monster_index(striker); return at_index >= 0 && cursed(monsters[std::size_t(at_index)]); }));
            }
            for (const auto& side : sides)
                if (foes[side.at].blessed) side.hero->wisp_blessing = { foes[side.at].blessed, now_ms + 0x1a5e00u * 40 };   // FUN_006251f0: 0x1a5e00 frames
            // Impregnate's state off at a death (FUN_005d21b0): the skill's summon
            // (painworm1, else 0x227) in its summode at the dead one
            // (FUN_005b2490: the level's own of its family, level_monster, by
            // FUN_005b23c0 -> FUN_005b2a00 radius 1 in the dead one's room,
            // spawn_spot; none: no worm).
            for (auto& monster : monsters)
                if (!monster.alive() && std::exchange(monster.impregnated, false))
                    if (const auto* impregnate = game_data->skills.get(game_data->skills.by_name.contains("Impregnate") ? game_data->skills.by_name.at("Impregnate") : -1)) {
                        const auto worm = game_data->monsters.by_id.find(impregnate->summon);
                        const int worm_type = level_monster(*game_data, *level, worm != game_data->monsters.by_id.end() ? int(worm->second) : int(game_data->mon_bin[0x227]));
                        int at_x = 0, at_y = 0;
                        if (!spawn_spot(*game_data, *level, crowd, &spawning.levels[level], worm_type, int(std::floor(monster.unit.x * 5)), int(std::floor(monster.unit.y * 5)), 1,
                                        room_of(*level, monster.unit.x, monster.unit.y), at_x, at_y)) continue;
                        auto young = make_monster(*game_data, worm_type, (float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, rng, monster.difficulty, true, players_in(foes));
                        young.aware = true;
                        young.seed = d2d::rules::Rng{ rng.next() };
                        set_mode(*game_data, young, impregnate->summode.empty() ? "NU" : impregnate->summode, now_ms);
                        d2d::log::info("a painworm comes out of {}", monster.npc.name);
                        born.push_back(std::move(young));
                    }
            foes.resize(allies);
            for (auto& young : born) add_monster(std::move(young));
            boss_events(foes, now_ms);
            monster_dots(now_ms, elapsed);
            for (auto& monster : monsters) monster.in_aura = false;
            for (const auto& side : sides) {
                act_as(*side.hero);
                monster_states(now_ms);
                buff_events(foes[side.at], now_ms);
                if (actor->merc) merc_buff_events(foes[side.at + 1], now_ms);
                buff_tick(now_ms);
                aura_pulse(now_ms);
                for (auto& monster : monsters) monster.in_aura = monster.in_aura || (monster.alive() && in_aura(monster));   // its auratargetstate shows (states.md)
                for (std::size_t k = 0; k < actor->pets.size(); ++k) {
                    auto& pet = actor->pets[k];
                    const auto& foe = foes[side.pets + k];
                    for (const auto& blow : foe.blows) pet_hurt(pet, blow.damage, now_ms, blow.hit_class, blow.marked);   // a record a blow, as the monsters'
                    for (const int divisor : foe.crushed)                // Crushing Blows, after the hits (FUN_005bffc0)
                        if (pet.monster.alive()) pet_hurt(pet, int(crushing_blow(pet.monster.hit_points, divisor, foe.pick.damage_resist)), now_ms);
                    if (foe.knocked_from && pet.monster.alive()) knock_back(pet.monster, foe.knocked_from->first, foe.knocked_from->second, now_ms);
                    if (const auto curse = monster_curse(foe.cursed, now_ms); curse.skill >= 0 && pet.monster.alive()) { pet.curse = curse; pet_buffs(pet); }
                }
                pets_turn(now_ms, elapsed, crowd);
                player_shots(now_ms);
            }
            missile_tick(now_ms);
            // The merc's arrows strike the first live monster they reach.
            const auto reach = [&](float x, float y) { return missile_rooms(*game_data, *level, x, y); };
            // A missile's sounds (FUN_004cd540, FUN_004d2d70): its TravelSound from its first frame,
            // on it while it flies; its HitSound where it struck (gone before its end).
            struct Flying { int sound; std::uint32_t dies; float x, y; int hit_sound; };
            std::vector<Flying> flying;
            for (auto& missile : missiles) {
                if (!missile.info) continue;
                const int phase = int((std::uint64_t(now_ms - missile.born) * std::uint32_t(missile.info->anim_speed)) / (40u * 16u));
                const int length = std::max(missile.info->anim_len, 1);
                if (missile.info->prog_sound > 0 && phase % length == length - 2 && phase != missile.prog_sound_phase) {
                    cues.cue_on(missile.sound ? unit_key(kSoundOnMissile, missile.sound) : 0, missile.info->prog_sound, now_ms, missile.x, missile.y);
                    missile.prog_sound_phase = phase;
                }
                if (missile.sound == 0) {
                    missile.sound = ++missile_sounds;
                    cues.cue_on(unit_key(kSoundOnMissile, missile.sound), missile.info->travel_sound, now_ms, missile.x, missile.y, missile.sound);
                }
                flying.push_back({ missile.sound, missile.dies, missile.x, missile.y, missile.info->hit_sound });
            }
            missiles_update(*level, missiles, foes, rng, now_ms, reach, [&](Missile& missile) {
                if (Hero* caster = hero_of(missile.caster)) act_as(*caster);
                merc_blow = casting_merc = missile.by_merc;
                const bool moving = missile.velocity_x != 0 || missile.velocity_y != 0;
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    auto& monster = monsters[i];
                    if (!monster.targetable()) continue;
                    if (moving ? !missile_struck(*level, missile, monster.unit.x, monster.unit.y, game_data->monsters.types[std::size_t(monster.type)].size)
                               : std::hypot(monster.unit.x - missile.x, monster.unit.y - missile.y) > 0.5f) continue;
                    if (missile.row) {                             // a shrine's potion: everyone in its burst
                        for (std::size_t j = 0; j < monsters.size(); ++j)
                            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y) * 5 <= float(std::max(missile.burst, 1)))
                                land(j, d2d::rules::missile_blow(*missile.row, target_of(j), {}, rng), true, now_ms, -1);   // a shrine's: no unit of the foes
                        return true;
                    }
                    if (missile.skill >= 0) {
                        if (std::ranges::contains(*missile.struck, int(i))) continue;
                        if (skill_missile_hits(missile, i, now_ms)) return true;
                        continue;
                    }
                    auto blow = d2d::rules::player_blow(d2d::rules::simple_fighter(missile.min, missile.max, missile.attack_rating), target_of(i), missile.level, rng);
                    if (missile.info) blow.hit_class = missile.info->hit_class;   // an arrow's, not hand to hand's (as a skill's missile, Missiles.txt HitClass)
                    land(i, blow, false, now_ms, merc_foe_id());   // a plain missile: the merc's arrow
                    return true;
                }
                return false;
            }, [&](int owner) { const int index = monster_index(owner); return index >= 0 && monsters[std::size_t(index)].alive(); });
            merc_blow = casting_merc = false;
            for (auto& foe : foes)                               // the shooters' MonProp events (knock, crush)
                for (const int owner : foe.shot_by)
                    if (const int shooter = monster_index(owner); shooter >= 0) monprop_hit(monsters[std::size_t(shooter)], foe, true);
            for (const auto& side : sides) {
                act_as(*side.hero);
                return_fire(foes[side.at], now_ms);              // the missiles that reached the player just now
            }
            for (const auto& flown : flying)
                if (now_ms < flown.dies && !std::ranges::contains(missiles, flown.sound, &Missile::sound))
                    cues.cue_on(unit_key(kSoundOnMissile, flown.sound), flown.hit_sound, now_ms, flown.x, flown.y);
            std::ranges::move(pending, std::back_inserter(missiles));
            pending.clear();
            for (auto& monster : monsters) {
                if (near_players(monster.unit.x, monster.unit.y)) monster_sounds(monster, now_ms);
                else monster.seq_sounds = 0;             // a far Jab's event-3 rows go unheard, not saved for later
            }
            for (const auto& side : sides) {
                act_as(*side.hero);
                hurt_side(std::span(foes).subspan(side.at, 2), now_ms);
            }
        }
        for (Hero* hero : heroes) {
            act_as(*hero);
            if (!in_moor) {                                      // pets follow the player about camp
                const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
                for (auto& pet : actor->pets)
                    if (pet.where == level && pet.monster.alive()) {
                        pet_follow(*level, pet.monster.unit, actor->player.x, actor->player.y, cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f, now_ms, elapsed, crowd);
                        if (const std::string_view want = pet.monster.unit.walking ? "WL" : "NU"; pet.monster.mode != want) set_mode(*game_data, pet.monster, want, now_ms);
                    }
            }
            if (actor->merc && actor->merc_npc) merc_turn(now_ms, elapsed, crowd, !in_moor);
        }
        act_as(acting);
    }

// What the monsters did to the acting player (`side` [0]) and their merc ([1])
// this frame: the merc's life and flinch, then the player's poison, chill,
// stun, knockback, curses, Mana Burn, block, armour wear and life.
// A monster's curse as it takes hold: its auralen, dimvision's and terror's
// over AiCurseDivisor, 25 frames at least; skill -1 for none.
auto Fight::monster_curse(const Foe::Cast& cursed, std::uint32_t now_ms) -> Hero::Curse {
        const auto* skill = game_data->skills.get(cursed.skill);
        if (!skill) return {};
        int length = calc(*skill, skill->auralen, cursed.level);
        if (const int divisor = game_data->curse_divisor[std::size_t(std::clamp(game_difficulty, 0, 2))]; divisor != 0 && (skill->auratarget == "dimvision" || skill->auratarget == "terror"))
            length /= divisor;
        return { skill->id, cursed.level, now_ms + std::uint32_t(std::max(length, 25)) * 40 };
    }

auto Fight::hurt_side(std::span<Foe> side, std::uint32_t now_ms) -> void {
        side[1].damage += std::exchange(actor->merc_hurt, 0);      // its swings' thorns (merc_strike), its poison (apply_regen)
        if (side[1].poison > 0 && side[1].poison_ticks > 0) {       // one at a time: a new one at least as strong replaces it (FUN_0057ac50)
            const double rate = side[1].poison / 256.0 / 40.0;      // its life is whole points, the poison 256ths a frame
            if (now_ms >= actor->merc_poison_until || rate >= actor->merc_poison_rate) {
                actor->merc_poison_rate = rate;
                actor->merc_poison_until = now_ms + std::uint32_t(side[1].poison_ticks) * 40;
            }
        }
        if (actor->merc && actor->merc->mode != "DT")           // Crushing Blows, after the hits (monprop_hit)
            for (const int divisor : side[1].crushed) {
                side[1].damage += int(crushing_blow(std::max(actor->merc->hit_points - side[1].damage, 0), divisor, side[1].pick.damage_resist));
            }
        if (actor->merc && side[1].knocked_from && actor->merc->hit_points > side[1].damage && actor->merc->mode != "DT")
            knock_back(*actor->merc, side[1].knocked_from->first, side[1].knocked_from->second, now_ms);   // as a monster's, before its get-hit
        // FUN_0057cb00's size test (rules::get_hit) once per landed blow, as game.exe tests one damage
        // record at a time (FUN_0057c6c0 then FUN_0057cee0 per hit): the divisor by that blow's hit class
        // (16 for every Act 1 melee class and any blow with an element), 37.5 % from that share of max
        // life, 75 % from twice it, sure from four times; every blow rolls, a get-hit restarts.
        // What the unit's states absorbed (Bone Armor, Cyclone Armor, Energy Shield: absorb) is off the
        // blows already: an armor's from each blow's own part (rules::absorb_blows).
        if (actor->merc && side[1].damage > 0 && actor->merc->mode != "DT") {
            actor->merc->hit_points -= side[1].damage;
            auto& unit = actor->merc->unit;
            const auto mode = actor->merc->hit_points <= 0                                     ? std::string_view("DT")
                            : actor->merc->mode != "KB" && d2d::rules::struck_big(side[1].blows, std::int64_t(merc_max_life()) << 8, actor->merc->seed) ? std::string_view("GH")
                                                                                                   : actor->merc->mode;
            if (mode != actor->merc->mode) {
                actor->merc->mode = mode; unit.mode_ms = now_ms; unit.walking = false; unit.path.clear();
                actor->merc->mode_until = now_ms + (mode == "GH" ? d2d::game::get_hit_ms(game_data->npc_timing(actor->merc->npc, mode), merc_fighter().fhr) : game_data->npc_timing(actor->merc->npc, mode).length_ms());
            }
        }
        auto& foe = side[0];
        // The last landed blow's hit class (unit +0xb0), before a knockback's GH plays its impact.
        // The client gets it on every landed hit, a get-hit or not: FUN_0057cee0 copies it with
        // result flag 1, and a hit too small for a GH sets unit flag 0x8000, so FUN_00597cf0 sends
        // it in S->C 0x0c. A later block plays its class.
        // ponytail: state 0x36 (uninterruptable) drops result flag 4, so no 0x0c; d2d has no such state.
        if (foe.hits > 0) actor->struck_class = actor->shown_class = foe.hit_class;
        // Poison works on the player over its ticks (a negative potion).
        // One poison at a time: a new one at least as strong replaces it
        // (and its length), a weaker one is ignored (FUN_0057ac50).
        if (foe.poison > 0 && foe.poison_ticks > 0) {
            const Regen poison{ -foe.poison / 40.0, 0, now_ms + std::uint32_t(foe.poison_ticks) * 40, true };
            const auto old = std::ranges::find_if(actor->regen, [](const Regen& regen_entry) { return regen_entry.poison; });
            if (old == actor->regen.end()) actor->regen.push_back(poison);
            else if (poison.life <= old->life) *old = poison;
        }
        if (foe.chill_ticks > 0) actor->chilled = std::max(actor->chilled, now_ms + std::uint32_t(foe.chill_ticks) * 40);   // only ever lengthened (FUN_0057af80)
        if (foe.stun_ticks > 0) actor->stunned = now_ms + std::uint32_t(std::min(foe.stun_ticks, 250)) * 40;          // state 21, a new one's length (FUN_0057aae0)
        // Knockback (result flag 8: FUN_0057cee0's player branch, mode 0x13,
        // FUN_0057f090): path type 8, 5 subtiles on from the attacker;
        // with no room it stays (FUN_0057f020, NU).
        // The KB mode is drawn as GH (PlrMode.txt "Knock back": GH), the
        // player sliding there over it.
        // ponytail: an even slide over the GH length (FHR's); the path's own
        // pace (FUN_00554ca0) isn't traced.
        if (actor->leap) {                                          // Leap's flight, then its landing's knockback (FUN_005da120, FUN_0056bad0)
            const float done = std::clamp(float(now_ms - actor->leap->start) / float(std::max<std::uint32_t>(actor->leap->lands - actor->leap->start, 1)), 0.f, 1.f);
            actor->player.x = actor->leap->from_x + (actor->leap->to_x - actor->leap->from_x) * done; actor->player.y = actor->leap->from_y + (actor->leap->to_y - actor->leap->from_y) * done;
            actor->player.goal_x = actor->player.x; actor->player.goal_y = actor->player.y;
            if (now_ms >= actor->leap->lands) {
                if (const auto* skill = game_data->skills.get(actor->leap->skill)) {
                    const float radius = float(calc(*skill, skill->calc[0], actor->leap->level));
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - actor->player.x, monsters[j].unit.y - actor->player.y) * 5 <= radius) {
                            d2d::rules::Blow blow{ .hit = true };
                            blow.knockback = true;
                            land(j, blow, true, now_ms, player_foe_id());
                        }
                }
                if (actor->character.character_class == 4) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "barbarian_leap_land", now_ms, actor->player.x, actor->player.y);
                actor->leap.reset();
            }
        }
        if (actor->knock) {
            if (actor->pmode == kModeGH) {
                const float done = std::clamp(float(now_ms - actor->player.mode_ms) / float(std::max<std::uint32_t>(actor->pmode_until - actor->player.mode_ms, 1)), 0.f, 1.f);
                actor->player.x = (*actor->knock)[0] + ((*actor->knock)[2] - (*actor->knock)[0]) * done; actor->player.y = (*actor->knock)[1] + ((*actor->knock)[3] - (*actor->knock)[1]) * done;
            }
            if (actor->pmode != kModeGH || now_ms >= actor->pmode_until) actor->knock.reset();   // done, or cut short
        }
        if (foe.knocked_from && !attack_mode(actor->pmode) && actor->pmode != kModeBL) {
            const auto [from_x, from_y] = *foe.knocked_from;
            const float dx = actor->player.x - from_x, dy = actor->player.y - from_y, distance = std::max(std::hypot(dx, dy), 0.01f);
            if (const float next_x = actor->player.x + dx / distance, next_y = actor->player.y + dy / distance; !level->unit_blocked(next_x, next_y, actor->player.shape)) {
                actor->knock = std::array{ actor->player.x, actor->player.y, next_x, next_y };
                set_pmode(kModeGH, now_ms);
            }
        }
        for (std::size_t k = 0; k < 2; ++k)
            if (const auto curse = monster_curse(side[k].cursed, now_ms); curse.skill >= 0) {
                actor->curses[k] = curse;
                d2d::log::info("{} cursed: skill {} level {}", k ? "the merc" : "the player", curse.skill, curse.level);
            }
        for (std::size_t k = 0; k < 2; ++k)
            if (const auto* skill = game_data->skills.get(side[k].marked.skill)) {
                if (now_ms >= actor->marks[k].until) d2d::log::info("{} marked: {}", k ? "the merc" : "the player", skill->auratarget);
                actor->marks[k] = { skill->id, side[k].marked.level, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, side[k].marked.level), 5)) * 40 };
            }
        if (foe.mana_burn > 0) {                             // Mana Burn
            actor->character.stats.values[d2d::d2s::kMana] = std::max<std::int64_t>(actor->character.stats.values[d2d::d2s::kMana] - (std::int64_t(foe.mana_burn) << 8), 0);
            d2d::log::info("mana burn: -{} mana", foe.mana_burn);
        }
        if (foe.blocked && actor->pmode < 0) set_pmode(kModeBL, now_ms);   // a block plays out (FBR)
        for (int landed = 0; landed < foe.hits; ++landed) wear_armor(now_ms);
        foe.damage = absorb(foe, now_ms);
        foe.damage += int(actor->self_hurt >> 8);
        actor->self_hurt &= 255;
        using namespace d2d::d2s;
        bool died = false;
        if (foe.damage > 0 && !dead()) {                    // a dead player takes no more (no second death)
            actor->character.stats.values[kLife] -= std::int64_t(foe.damage) << 8;
            if (actor->character.stats.values[kLife] <= 0) { die(now_ms); died = true; }
        }
        for (const int divisor : foe.crushed) {             // Crushing Blows, after the hits (crushing_blow, FUN_005bffc0)
            if (died || dead()) break;
            auto& life = actor->character.stats.values[kLife];
            const std::int64_t crush = crushing_blow(life, divisor, foe.pick.damage_resist);
            life = std::max<std::int64_t>(life - crush, 0);
            d2d::log::info("crushing blow: -{} life", crush >> 8);
            if (life <= 0) { die(now_ms); died = true; }
        }
        // Stunned, every landed hit is a get-hit, however small, even one
        // absorbed to nothing (FUN_0057cee0: state 21 skips FUN_0057cb00's
        // too-small test; result flag 4 is the hit landing, FUN_0057ec10,
        // before any damage). That's all a stun does to a player: no input
        // is refused.
        // Else the size test per landed blow (rules::struck_big).
        // ponytail: a blow's damage is its whole points; the rolls take d2d's rng, not the player's own seed.
        const bool big = d2d::rules::struck_big(foe.blows, actor->character.stats.get(kMaxLife), rng);
        if (!died && (big || (foe.hits > 0 && now_ms < actor->stunned)) && !attack_mode(actor->pmode) && actor->pmode != kModeBL) set_pmode(kModeGH, now_ms);
    }

auto Fight::monster_auras(std::vector<Foe>& foes, std::uint32_t now_ms) -> void {
        for (auto& monster : monsters) monster.aura_dmg = monster.aura_th = 0;
        for (auto& aura_monster : monsters) {
            if (!aura_monster.alive() || !aura_monster.aura || !near_players(aura_monster.unit.x, aura_monster.unit.y)) continue;
            const auto* skill = game_data->skills.get(aura_monster.aura);
            if (!skill) continue;
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, aura_monster.stats.level, &rng };
            auto val = [&](const d2d::rules::Calc& calc_row) { return d2d::rules::eval_calc(game_data->skills, calc_row, env, skill->id, aura_monster.aura_lvl); };
            const float radius = float(val(skill->aurarange)) / 5;                     // subtiles -> cells
            auto in_range = [&](float x, float y) { return std::hypot(x - aura_monster.unit.x, y - aura_monster.unit.y) <= radius; };
            if (skill->srvdofunc == ServerDoFunction::kFriendlyAura) {
                for (auto& monster : monsters)
                    if (monster.alive() && in_range(monster.unit.x, monster.unit.y))
                        for (std::size_t i = 0; i < skill->aurastat.size(); ++i) {
                            if (skill->aurastat[i] == 25) monster.aura_dmg += val(skill->aura_calc[i]);     // damagepercent
                            if (skill->aurastat[i] == 119) monster.aura_th += val(skill->aura_calc[i]);     // item_tohit_percent
                        }
                continue;
            }
            for (auto& foe : foes) {
                if (!foe.alive || !in_range(foe.x, foe.y)) continue;
                for (std::size_t i = 0; i < skill->aurastat.size(); ++i) {
                    const int value = val(skill->aura_calc[i]);
                    switch (skill->aurastat[i]) {
                        case 39: foe.fighter.res[0] += value; break;                         // fireresist
                        case 41: foe.fighter.res[1] += value; break;                         // lightresist
                        case 43: foe.fighter.res[2] += value; break;                         // coldresist
                        case 171: foe.fighter.defense += foe.fighter.defense * value / 100; break;   // skill_armor_percent
                        default: break;
                    }
                }
            }
            if (skill->etype < 0 || skill->etype > 2 || now_ms < aura_monster.aura_next) continue;
            aura_monster.aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
            const int low = d2d::rules::elem_damage(game_data->skills, *skill, env, aura_monster.aura_lvl, false) >> 8;
            const int high = std::max(d2d::rules::elem_damage(game_data->skills, *skill, env, aura_monster.aura_lvl, true) >> 8, low);
            for (auto& foe : foes)
                if (foe.alive && in_range(foe.x, foe.y)) {
                    const int dealt = d2d::rules::resisted(rng.range(low, high), foe.fighter.res[std::size_t(skill->etype)]);
                    foe.damage += dealt; foe.elemental += dealt;
                    foe.blows.push_back({ dealt, 0, true, 0, dealt });
                }
        }
    }

auto Fight::shrine_missiles(int code, float x, float y, int clvl, std::uint32_t now_ms) -> void {
        const int lvl = std::clamp(clvl / 5, 1, 8);
        if (code == 19) {
            const auto found = game_data->missiles.find("fireball");
            const auto found_skill = game_data->skills.by_name.find("Fire Ball");
            if (found == game_data->missiles.end() || found_skill == game_data->skills.by_name.end()) return;
            for (int ring = 1; ring <= 4; ++ring)
                for (const int offset_y : { 5, -10, 15, -20 }) {
                    const float offset_x = float(ring % 2 ? 5 * ring : -5 * ring);
                    launch(found->second, *game_data->skills.get(found_skill->second), lvl, x, y, offset_x / 5, float(offset_y) / 5, found->second.range, now_ms);
                }
            return;
        }
        const auto found = game_data->missiles.find(code == 21 ? "explosivepotion" : "chokinggaspoition");
        if (found == game_data->missiles.end()) return;
        const auto& missile_info = found->second;
        const auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        static constexpr std::pair<int, int> kBurst[] = { { -6, 6 }, { -6, -6 }, { 0, 6 }, { 0, -6 }, { 6, 6 }, { 6, -6 } };
        for (const auto& [dx, dy] : kBurst) {
            const float dir_x = float(dx) / 5, dir_y = float(dy) / 5, speed = missile_speed(missile_info, lvl), distance = std::hypot(dir_x, dir_y);
            Missile missile{ &missile_info, x, y, dir_x / distance * speed, dir_y / distance * speed, missile_heading(dir_x, dir_y), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, {} };
            missile.friendly = true; missile.caster = actor->id; missile.row = damage; missile.burst = missile_info.hit_par1;
            pending.push_back(std::move(missile));
        }
    }

auto Fight::enter(const Level* destination) -> void {
        if (in_town(destination) || destination == mon_level) return;
        if (mon_level) kept[mon_level] = std::move(monsters);
        monsters.clear();
        if (const auto found = kept.find(destination); found != kept.end()) {
            monsters = std::move(found->second);
            kept.erase(found);
        }
        mon_level = destination;
        missiles.clear();
        actor->attack_mon = actor->merc_target = -1;
        for (auto& pet : actor->pets) pet.target = -1;
    }

auto Fight::rooms_up(const Level& here, float x, float y, bool arrived) -> void {
        for (const auto& [grown, from] : player_moved(*game_data, spawning, here, x, y, arrived)) {
            if (remote_monsters) continue;          // the rooms still come up (their objects); the host has the monsters
            const auto& spawns = spawning.levels[grown].spawns;
            const auto& region = std::size_t(grown->id) < spawning.regions.size() ? spawning.regions[std::size_t(grown->id)] : d2d::rules::Region{};
            auto made = spawn_monsters(*game_data, std::span(spawns).subspan(from), region, rng, game_difficulty, int(heroes.size()));
            for (auto& monster : made) add_monster(std::move(monster), grown);
        }
    }

}  // namespace d2d::game
