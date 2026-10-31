// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for fight.hpp: Fight, the combat subsystem the World owns.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <level_ids.hpp>
#include <merc.hpp>
#include <missiles.hpp>
#include <monsters.hpp>
#include <pets.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <shadows.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {

// Up, right, down, left: where a boss's ring of bolts leaves.
constexpr std::pair<float, float> kCompass[] = { { 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } };

// Cloak of Shadows on a monster (FUN_005d6520): its state, with the AI
// toggle at its end whatever the monster, then special AI 10 on one
// swaps_ai lets.
void cloak(const GameData& game_data, Monster& monster, int skill, int lvl, std::uint32_t until, std::uint32_t now_ms) {
    if (put_state(game_data, monster, monster.cry, { skill, lvl, until, true }, now_ms) && swaps_ai(game_data, monster)) ai_toggle(game_data, monster, 10);
}

constexpr int kTerror = 77;                     // Skills.txt Terror: Howl's state (FUN_005ddd00, 0x4d)

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

}  // namespace

// A state's end on a monster (its stat list off, FUN_006269f0: the
// callback; FUN_005c3370 toggles an AI-swapping one's AI back).
void end_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot) {
    if (slot.swaps) ai_toggle(game_data, monster, 0);
    if (monster.own_level > 0 && slot.skill >= 0) {             // Conversion's end (FUN_005d01a0): its level and life back, life in proportion
        if (const auto* skill = game_data.skills.get(slot.skill); skill && skill->auratarget == "conversion") {
            monster.hit_points = std::max(int(std::int64_t(monster.hit_points) * monster.own_max_life / std::max(monster.stats.hit_points, 1)), 1);
            monster.stats.level = monster.own_level; monster.stats.hit_points = monster.own_max_life;
            monster.own_level = monster.own_max_life = 0;
        }
    }
    slot = {};
}

// A timed state put on a monster in its slot (FUN_0056e970): the same
// skill at the same level only runs longer, one at a higher level stays
// (the put fails); else the one there comes off (end_state).
// ponytail: d2d keeps one curse and one other state; game.exe's other
// states don't take each other's place.
bool put_state(const GameData& game_data, Monster& monster, Monster::SkillState& slot, Monster::SkillState next, std::uint32_t now_ms) {
    if (slot.skill >= 0 && now_ms < slot.until && slot.skill == next.skill) {
        if (slot.level == next.level) { slot.until = next.until; return true; }
        if (slot.level > next.level) return false;
    }
    end_state(game_data, monster, slot);
    slot = next;
    return true;
}

auto Fight::monster_index(int id) const -> int {
        if (id < 0) return -1;
        const auto found = std::ranges::find(monsters, id, &Monster::id);
        return found == monsters.end() ? -1 : int(found - monsters.begin());
    }

auto Fight::add_monster(Monster monster) -> void { add_monster(std::move(monster), mon_level); }
auto Fight::add_monster(Monster monster, const Level* where) -> void {
        monster.id = next_id++;
        const auto* made_level = where ? where : mon_level ? mon_level : level;
        if (made_level && (monster.boss == d2d::rules::Boss::unique || monster.boss == d2d::rules::Boss::superunique))
            made_bosses.push_back({ monster.id, monster.type, made_level->id });
        (where == mon_level || !where ? monsters : kept[where]).push_back(std::move(monster));
    }

auto Fight::own_stats() const -> d2d::d2s::Stats {
        using namespace d2d::d2s;
        auto stats = actor->character.stats;
        stats.values[kMaxLife] -= actor->bo_life + actor->item_max[0]; stats.values[kMaxMana] -= actor->bo_mana + actor->item_max[1]; stats.values[kMaxStamina] -= actor->item_max[2];
        return stats;
    }

auto Fight::new_game(int difficulty) -> void {
        game_difficulty = difficulty;
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
        next_id = 1;
        missiles.clear();
        actor->regen.clear();
        actor->merc_hurt = 0; actor->merc_poison_rate = actor->merc_poison_acc = 0; actor->merc_poison_until = 0; actor->merc_heal_rate = actor->merc_heal_acc = 0; actor->merc_heal_until = 0;
        actor->charges.clear();
        actor->self_states.clear();
        actor->boost = {};
        actor->stamina_pot = {};
        actor->potion_states = {};
        actor->pets.clear();
        actor->attack_mon = -1;
        actor->item_charge = actor->attack_charge = {};            // no cast from a charged entry carried in
        actor->pmode = -1;
        actor->struck_class = actor->shown_class = 0;   // a new game's unit: no hit class yet
    }

auto Fight::merc_joins() -> void {
        const auto& header = actor->character.header;
        actor->merc_target = -1; actor->merc_chase = false;
        actor->merc_hurt = 0; actor->merc_poison_rate = actor->merc_poison_acc = 0; actor->merc_poison_until = 0; actor->merc_heal_rate = actor->merc_heal_acc = 0; actor->merc_heal_until = 0;
        actor->merc_growth = 0; actor->merc_aura = -1; actor->merc_skill = -1; actor->merc_seq = nullptr; actor->merc_buff = {}; actor->merc_cry = {}; actor->leap.reset(); actor->knock.reset();
        merc_level_stats();
        if (!actor->merc) return;
        actor->merc->hit_points = merc_max_life();
        actor->merc->mode = "NU"; actor->merc->next_act = 0; actor->merc->move_pct = 0;
        actor->merc->seed = d2d::rules::Rng(header.merc_seed);
        actor->merc->type = actor->merc_npc ? game_data->monsters.row(actor->merc_npc->id) : -1;
    }

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
    }

auto Fight::revive(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        actor->character.stats.values[kLife] = actor->character.stats.values[kMaxLife];
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

auto Fight::drink(int col, std::uint32_t now_ms) -> void {
        if (!dead()) potion(d2d::rules::drink_belt(game_data->rules, actor->character.items, col), now_ms);
    }

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
        if (potion.percent) actor->merc->hit_points = std::min(merc_max_life(), actor->merc->hit_points + merc_max_life() * potion.life / 100);
        else if (potion.life > 0) {
            const int len = std::max(potion.ticks, 1);
            const double left = actor->merc_heal_until > now_ms ? actor->merc_heal_rate * double(actor->merc_heal_until - now_ms) : 0;
            actor->merc_heal_until = std::max(actor->merc_heal_until, now_ms) + std::uint32_t(len) * 40;
            actor->merc_heal_rate = (left + potion.life) / double(actor->merc_heal_until - now_ms);
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
            const int amount = d2d::rules::potion_amount(life ? potion.life : potion.mana, actor->character.character_class, life, stat, rng);
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
        mana += double(d2d::rules::mana_per_frame(actor->character.stats.values[kMaxMana], gains.mana_regen, actor->player_combat.mana_regen, int(actor->psum[kManaRecovery])))
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
        if (monster.mode != monster.last_mode) {
            monster.last_mode = monster.mode;
            monster.sound_ms = now_ms;                  // a mode's sound (unit +0x7c: FUN_004cb6a0, FUN_004cc410, FUN_004cb890)
            // Struck (FUN_004cc410): the hit's impact and its element's layer, but a critter's.
            // ponytail: state 0x92's skip isn't in.
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

auto Fight::floor_material_at(float x, float y) const -> int {
        const int cell_x = int(std::floor(x)), cell_y = int(std::floor(y));
        for (const auto& floors : level->room_floors)
            for (const auto& floor : floors)
                if (floor.x == cell_x && floor.y == cell_y)
                    if (const int material = d2d::rules::sound_ids::floor_material(floor.material); material > 0) return material;
        return level->material;
    }

auto Fight::gfx() const -> const GameData::Appearance& {
        return actor->character.appearance ? *actor->character.appearance : game_data->starting_gear[std::size_t(std::max(actor->character.character_class, 0))];
    }

auto Fight::player_anim(int mode) const -> const GameData::AnimTiming& {
        return game_data->composite_timing(std::max(actor->character.character_class, 0), mode, gfx());
    }

auto Fight::set_pmode(int mode, std::uint32_t now_ms) -> void {
        actor->pmode = mode;
        actor->player.mode_ms = now_ms;
        actor->player.walking = false;
        actor->player.path.clear();
        const auto& anim = player_anim(mode);
        std::uint32_t len = anim.length_ms();
        if (attack_mode(mode) && anim.frames) {
            const auto ticks = d2d::rules::attack_ticks(int(anim.frames), int(anim.speed ? anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, chill_rate(now_ms));
            len = std::uint32_t(ticks) * 40;
            // The swing (FUN_004cc5b0: A1 / A2, a Barbarian's S3, an Assassin's S4): its weapon_* that many frames in.
            // A sequence plays in SQ (0x12), which the dispatcher leaves silent (the table at 0x4cc74c).
            const int cls = actor->character.character_class;
            if (skill_sequence().empty() && (mode == kModeA1 || mode == kModeA2 || (mode == kModeS3 && cls == d2d::d2s::kBarbarian) || (mode == kModeS4 && cls == d2d::d2s::kAssassin))) {
                const auto swing = d2d::rules::sound_ids::kSwing[std::size_t(actor->player_combat.hit_class & 0xf)];
                const int speed = d2d::rules::attack_speed(int(anim.speed ? anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, chill_rate(now_ms));
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), swing.sound, now_ms + std::uint32_t((swing.frames * 256 + 128) / speed) * 40, actor->player.x, actor->player.y);
            }
        } else if (mode == kModeSC && anim.frames) {
            // ponytail: a cast that plays a sequence (Lightning) is timed as a plain one here.
            len = std::uint32_t(d2d::rules::cast_ticks(int(anim.frames), int(anim.speed ? anim.speed : 256), actor->player_combat.fcr)) * 40;
        } else if (mode == kModeGH && anim.frames) {
            // ponytail: prate spreads the frames over the whole hold; game.exe's client (FUN_00463390, tested
            // before and after its step) draws one frame fewer and stands the last tick.
            len = d2d::game::get_hit_ms(anim, actor->player_combat.fhr);
        } else if (mode == kModeBL && anim.frames) {
            const int shown = d2d::rules::speed_frames(int(anim.frames), actor->player_combat.fbr);
            len = std::uint32_t(shown) * anim.ms_per_frame();
        }
        actor->prate = len ? float(anim.length_ms()) / float(len) : 1.f;
        actor->pmode_until = mode == kModeDD ? 0 : now_ms + len;
        // Struck or dying (FUN_004cc410's player branch): the hit's impact, then the class's own cry.
        // ponytail: FUN_004b9e90's check before the GH cry isn't traced.
        if (mode == kModeGH || mode == kModeDT) {
            const auto impact = d2d::rules::sound_ids::impact(actor->struck_class, mode == kModeDT);
            cues.cue_on(unit_key(kSoundOnPlayer, actor->id), impact.impact, now_ms, actor->player.x, actor->player.y);
            cues.cue_on(unit_key(kSoundOnPlayer, actor->id), impact.element, now_ms, actor->player.x, actor->player.y);
            actor->struck_class = 0;
            if (const int cls = actor->character.character_class; cls >= 0 && cls < 7) {
                const auto& voice = d2d::rules::sound_ids::kPlayerVoice[std::size_t(cls)];
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), mode == kModeDT ? voice.death : voice.hit, now_ms, actor->player.x, actor->player.y);
            }
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
        // them back the swing's frames less 3 ticks (FUN_004ca500).
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && (attack_mode(mode) || mode == kModeSC)) {
            static constexpr std::array<std::string_view, 7> kClassCode = { "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
            const auto swing = d2d::rules::sound_ids::kSwing[std::size_t(actor->player_combat.hit_class & 0xf)];
            const int rate = int(anim.speed ? anim.speed : 256);
            const int speed = attack_mode(mode) ? d2d::rules::attack_speed(rate, actor->player_combat.ias, actor->player_combat.wsm, chill_rate(now_ms)) : rate;
            const int swing_ticks = (swing.frames * 256 + 128) / speed;
            const std::uint32_t start_at = now_ms + (skill->st_sound_delay ? std::uint32_t(std::max(swing_ticks - 3, 0)) * 40 : 0);
            const bool item = (actor->item_cast || from_charges(skill->id)) && skill->item_cast_sound > 0;   // a scroll, or a charged entry (its owner's guid, FUN_004c6140)
            cues.cue_on(unit_key(kSoundOnPlayer, actor->id), item ? skill->item_cast_sound : skill->st_sound, start_at, actor->player.x, actor->player.y);
            if (skill->weapon_sound) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), swing.sound, now_ms + std::uint32_t(swing_ticks) * 40, actor->player.x, actor->player.y);
            const int cls = actor->character.character_class;
            if (!item && cls >= 0 && std::size_t(cls) < kClassCode.size() && skill->cls == kClassCode[std::size_t(cls)])
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), skill->st_sound_class, start_at, actor->player.x, actor->player.y);
        }
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

auto Fight::die(std::uint32_t now_ms) -> void {
    actor->character.stats.values[d2d::d2s::kLife] = 0;
    set_pmode(kModeDT, now_ms);
    actor->attack_mon = -1;
    actor->smove = {};           // a Whirlwind / Charge / Leap cut short isn't resumed by the next attack mode
    actor->kicks_left = 0;
    d2d::log::info("the player died");
}

auto Fight::player_fighter(d2d::rules::Fighter* kick ,
                                                     const d2d::rules::StatSum* states ,
                                                     const std::vector<d2d::rules::PassiveStat>* passives ,
                                                     d2d::rules::StatSum* sum_out ) const -> d2d::rules::Fighter {
        using namespace d2d::d2s;
        d2d::rules::StatSum sum = states ? *states : d2d::rules::StatSum{}, weapon_sum{};
        const d2d::d2s::Item *weapon = nullptr, *shield = nullptr, *boots = nullptr;
        auto add = [](d2d::rules::StatSum& into, const std::vector<d2d::d2s::ItemProp>& props) {
            for (const auto& prop : props) if (prop.stat >= 0 && std::size_t(prop.stat) < into.size()) into[std::size_t(prop.stat)] += prop.value;
        };
        for (const auto& item : actor->character.items) {
            const bool worn = item.location == d2d::d2s::item_location::kEquipped && item.slot >= d2d::d2s::body_location::kFirst && item.slot <= d2d::d2s::body_location::kLast;
            const bool charm = item.location == d2d::d2s::item_location::kStored && item.panel == d2d::d2s::item_panel::kInventory && (item.code == "cm1" || item.code == "cm2" || item.code == "cm3");
            if (!worn && !charm) continue;
            add(sum, item.props);
            for (const auto& socketed : item.socketed_items) add(sum, socket_props(*game_data, item, socketed));
            add(sum, set_bonus_props(*game_data, actor->character.items, item));
            if (worn && item.slot == d2d::d2s::body_location::kFeet) boots = &item;
            if (!worn || (item.slot != d2d::d2s::body_location::kRightArm && item.slot != d2d::d2s::body_location::kLeftArm)) continue;
            const auto found = game_data->rules.item_base.find(item.code);
            const auto info = game_data->rules.item_info.find(item.code);
            if (found == game_data->rules.item_base.end() || info == game_data->rules.item_info.end()) continue;
            if (info->second.kind == 2 && (!weapon || item.slot == d2d::d2s::body_location::kRightArm)) weapon = &item;
            if (info->second.kind == 1 && found->second.block > 0) shield = &item;
        }
        if (passives) {
            const auto type_of = [&](const d2d::d2s::Item& item) {
                const auto found = game_data->rules.item_info.find(item.code);
                return found == game_data->rules.item_info.end() ? std::string{} : found->second.type;
            };
            std::vector<std::string> hands;                  // the types in slots 4 / 5
            for (const auto& item : actor->character.items)
                if (item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm)) hands.push_back(type_of(item));
            const auto is_type = [&](const std::string& type, const std::string& want) { return d2d::rules::type_is(game_data->rules, type, want); };
            const bool claws2 = hands.size() == 2 && is_type(hands[0], "h2h") && is_type(hands[1], "h2h");
            std::array<std::int64_t, 4> best{};              // 342, 343, 344, 348
            for (const auto& passive : *passives) {
                if (passive.stat < 0 || std::size_t(passive.stat) >= sum.size()) continue;
                if (passive.itype.empty()) { sum[std::size_t(passive.stat)] += passive.value; continue; }
                const std::size_t mastery = passive.stat >= kPassiveMasteryMeleeToHit && passive.stat <= kPassiveMasteryMeleeCritical
                                            ? std::size_t(passive.stat - kPassiveMasteryMeleeToHit)
                                            : passive.stat == kPassiveWeaponBlock ? 3 : 4;
                if (mastery == 4) continue;
                const bool fits = mastery < 3 ? weapon && is_type(type_of(*weapon), passive.itype)
                                        : claws2 && std::ranges::any_of(hands, [&](const auto& hand_type) { return is_type(hand_type, passive.itype); });
                if (fits) best[mastery] = std::max<std::int64_t>(best[mastery], passive.value);
            }
            for (std::size_t k = 0; k < 3; ++k) sum[kPassiveMasteryMeleeToHit + k] += best[k];
            sum[kPassiveWeaponBlock] += best[3];
        }
        if (weapon) {                                        // its own enhanced damage (op 13), sockets included
            add(weapon_sum, weapon->props);
            for (const auto& item : weapon->socketed_items) add(weapon_sum, socket_props(*game_data, *weapon, item));
        }
        if (sum_out) *sum_out = sum;
        const auto& resistances = actor->character.panel.res;                        // panel: fire, cold, lightning, poison
        const auto& gains = game_data->class_gains[std::size_t(std::max(actor->character.character_class, 0))];
        const std::array<int, 4> res{ int(resistances[0]), int(resistances[2]), int(resistances[1]), int(resistances[3]) };
        auto fighter = d2d::rules::make_fighter(game_data->rules, weapon, shield, sum, weapon_sum, actor->character.stats, gains,
                                          int(actor->character.panel.defense), res, boots);
        {                                                    // poison length reduction: the panel's difficulty penalty, no max stat so at most 75 (FUN_0057be00)
            const int diff = actor->character.header.active_difficulty();
            const std::int64_t penalty = actor->character.header.expansion() ? game_data->resist_penalty[std::size_t(diff)] : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(diff)];
            fighter.plr = int(std::clamp<std::int64_t>(sum[kPoisonLengthReduction] + penalty, -100, 75));
        }
        fighter.half_freeze = sum[kHalfFreezeDuration] > 0; fighter.cannot_freeze = sum[kCannotBeFrozen] > 0;
        if (kick) {                                          // the weapon's stats off, its attack rating kept
            auto bare = sum;
            for (std::size_t i = 0; i < bare.size(); ++i) bare[i] -= weapon_sum[i];
            *kick = d2d::rules::make_fighter(game_data->rules, nullptr, shield, bare, {}, actor->character.stats, gains,
                                             int(actor->character.panel.defense), res, boots);
            kick->ar_base = fighter.ar_base; kick->ar_pct = fighter.ar_pct; kick->attack_rating = fighter.attack_rating;
            kick->kick_pct = fighter.kick_pct;
        }
        return fighter;
    }

auto Fight::update_fighters(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        std::erase_if(actor->self_states, [&](const SelfState& state) { return now_ms >= state.until; });
        d2d::rules::StatSum st_sum{};
        const auto env = calc_env();
        const auto passives = d2d::rules::passive_stats(game_data->skills, env);
        actor->character.panel = panel_stats(*game_data, actor->character.header, actor->character.items, actor->character.stats, &passives);
        auto states = actor->self_states;
        if (const auto* skill = game_data->skills.get(actor->aura); skill && skill->srvdofunc == ServerDoFunction::kFriendlyAura) states.push_back({ actor->aura, skill_level ? skill_level(actor->aura) : 1, ~0u });
        // A totem's aura (Oak Sage, Heart of Wolverine, Spirit of Barbs: do
        // 65) while the player is within its aurarange of it.
        for (const auto& pet : actor->pets)
            if (const auto* skill = game_data->skills.get(pet.aura); skill && skill->srvdofunc == ServerDoFunction::kFriendlyAura && pet.monster.alive() && pet.where == level
                && std::hypot(pet.monster.unit.x - actor->player.x, pet.monster.unit.y - actor->player.y) * 5 <= float(calc(*skill, skill->aurarange, pet.aura_level)))
                states.push_back({ pet.aura, pet.aura_level, ~0u });
        if (now_ms < actor->curses[0].until) states.push_back({ actor->curses[0].skill, actor->curses[0].level, actor->curses[0].until });   // a monster's curse on the player
        if (now_ms < actor->marks[0].until) states.push_back({ actor->marks[0].skill, actor->marks[0].level, actor->marks[0].until });
        for (const auto& state : states) {
            const auto* skill = game_data->skills.get(state.skill);
            for (std::size_t i = 0; skill && i < skill->aurastat.size(); ++i)
                if (const int id = skill->aurastat[i]; id >= 0 && id != kLife && std::size_t(id) < st_sum.size())
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], env, skill->id, state.level);
        }
        if (now_ms < actor->stamina_pot.until) st_sum[d2d::d2s::kStaminaRecoveryBonus] += actor->stamina_pot.bonus;
        for (const auto& state : actor->potion_states)
            if (now_ms < state.until)
                for (const auto& [id, value] : state.stats) if (std::size_t(id) < st_sum.size()) st_sum[std::size_t(id)] += value;
        if (now_ms < actor->boost.until)
            for (const auto& [id, value] : actor->boost.stats) if (std::size_t(id) < st_sum.size()) st_sum[std::size_t(id)] += value;
        // Max life / mana % (item_maxhp_percent 76, item_maxmana_percent 77;
        // op 11 in FUN_00626200): the items' and Battle Orders' % summed, of
        // the raw maximum (own + the items' flat +life / +mana), not of the
        // attributes' op 8 share. Battle Orders' part raises life and mana
        // with it; the items' maxima follow what's worn, life and mana only
        // stay under them.
        {
            constexpr std::array<std::pair<int, int>, 3> kMax{ { { kMaxLife, kLife }, { kMaxMana, kMana }, { kMaxStamina, kStamina } } };
            constexpr std::array<std::size_t, 3> kBonus{ kMaxLife, kMaxMana, kMaxStamina };
            constexpr std::array<std::size_t, 2> kPercent{ kMaxLifePercent, kMaxManaPercent };
            std::array<std::int64_t*, 2> bo_share{ &actor->bo_life, &actor->bo_mana };
            for (std::size_t k = 0; k < 3; ++k) {
                auto& maximum = actor->character.stats.values[std::size_t(kMax[k].first)];
                auto& current = actor->character.stats.values[std::size_t(kMax[k].second)];
                std::int64_t want = actor->character.panel.bonus[kBonus[k]] * 256;
                if (k < 2) {                              // Miriam: (409 + 466) mana +42% + 54 = the host's 1296
                    const std::int64_t raw = maximum - actor->item_max[k] - *bo_share[k] + actor->character.panel.max_flat[k] * 256;
                    const std::int64_t item_share = raw * actor->character.panel.max_percent[k] / 100;
                    const std::int64_t orders = raw * (actor->character.panel.max_percent[k] + st_sum[kPercent[k]]) / 100 - item_share;
                    maximum += orders - *bo_share[k];
                    current = std::min(current + std::max<std::int64_t>(orders - *bo_share[k], 0), maximum);
                    *bo_share[k] = orders;
                    want += item_share;
                }
                maximum += want - actor->item_max[k];
                current = std::min(current, maximum);
                actor->item_max[k] = want;
            }
        }
        actor->player_combat = player_fighter(&actor->pf_kick, &st_sum, &passives, &actor->psum);
        constexpr int kRes[4] = { kFireResist, kLightningResist, kColdResist, kPoisonResist };          // Fighter::res order: fire, lightning, cold, poison
        constexpr std::size_t kPanelRes[4] = { 0, 2, 1, 3 };  // PanelStats::res_cap order: fire, cold, lightning, poison
        // A resist aura's, shrine's or potion's on top of the panel's (Salvation,
        // Resist Fire, an antidote, ...), to its cap: 75 + every max resist (the
        // stat after each), at most 95.
        for (std::size_t k = 0; k < 4; ++k)
            if (const auto value = st_sum[std::size_t(kRes[k])], more = st_sum[std::size_t(kRes[k] + 1)]; value != 0 || more != 0)
                actor->player_combat.res[k] = int(std::min<std::int64_t>(actor->player_combat.res[k] + value, std::min<std::int64_t>(actor->character.panel.res_cap[kPanelRes[k]] + more, 95)));
        actor->player_combat.ias += int(st_sum[kAttackRate]);
        actor->player_combat.frw += int(st_sum[kVelocityPercent]);
        for (const auto& passive : passives) if (passive.stat == kVelocityPercent && passive.itype.empty()) actor->player_combat.frw += passive.value;   // Increased Speed
        actor->player_combat.defense += int(actor->player_combat.defense * st_sum[kSkillArmorPercent] / 100);
        actor->player_combat.defense = std::max(int(actor->player_combat.defense + actor->player_combat.defense * st_sum[kArmorOverridePercent] / 100), 0);
        const std::uint32_t chosen[2] = { actor->character.header.left_skill, actor->character.header.right_skill };
        for (std::size_t k = 0; k < 2; ++k)
            if (const auto* skill = game_data->skills.get(int(chosen[k])))
                actor->character.panel.attack[k] = d2d::rules::attack_line(game_data->skills, *skill, env, skill_level ? skill_level(skill->id) : 1,
                                                                    actor->player_combat, int(actor->psum[kPassiveMasteryMeleeToHit]));
    }

auto Fight::calc_env() -> d2d::rules::CalcEnv {
        return { skill_base, skill_level, [this](int id) { return id >= 0 && std::size_t(id) < actor->psum.size() ? int(actor->psum[std::size_t(id)]) : 0; },
                 int(actor->character.stats.get(d2d::d2s::kLevel)), &rng };
    }

auto Fight::merc_calc_env() -> d2d::rules::CalcEnv {
        return { [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, &rng };
    }

auto Fight::need_mana(std::uint32_t now_ms) -> void {
        static constexpr std::array<const char*, 7> kNeedMana{ "amazon_needmana_1", "sorceress_needmana_1", "necromancer_needmana_1",
            "paladin_needmana_1", "barbarian_needmana_1", "druid_needmana_1", "assassin_needmana_1" };
        if (now_ms < actor->need_mana_until) return;
        actor->need_mana_until = now_ms + 75 * 40;
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), kNeedMana[std::size_t(std::clamp(actor->character.character_class, 0, 6))], now_ms, actor->player.x, actor->player.y);
    }

auto Fight::impossible(std::uint32_t now_ms) -> void {
        static constexpr std::array<const char*, 7> kImpossible{ "amazon_impossible_1", "sorceress_impossible_1", "necromancer_impossible_1",
            "paladin_impossible_1", "barbarian_impossible_1", "druid_impossible_1", "assassin_impossible_1" };
        if (now_ms < actor->impossible_until) return;
        actor->impossible_until = now_ms + 75 * 40;
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), kImpossible[std::size_t(std::clamp(actor->character.character_class, 0, 6))], now_ms, actor->player.x, actor->player.y);
    }

auto Fight::start_swing(std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        actor->swing_skill = 0;
        actor->kicks_left = 0;
        actor->item_charge = actor->attack_charge;            // the queued attack's entry (World::apply), if any
        const auto* skill = game_data->skills.get(actor->attack_skill);
        if (skill && actor->attack_skill != 0) {
            if (!skill_built(*skill)) {
                if (std::ranges::find(told, actor->attack_skill) == told.end()) {
                    told.push_back(actor->attack_skill);
                    d2d::log::info("not implemented: skill {} (srvstfunc {}, srvdofunc {}) - a plain attack for now",
                                   skill->name, int(skill->srvstfunc), int(skill->srvdofunc));
                }
            } else {
                const int lvl = cast_level(actor->attack_skill);
                const int cost = cost_of(*skill, lvl);
                if (lvl > 0 && actor->character.stats.values[kMana] >= cost) {
                    pay(*skill, cost);
                    actor->swing_skill = actor->attack_skill;
                    if (skill->srvstfunc == ServerStartFunction::kDragonTalon || skill->srvstfunc == ServerStartFunction::kRepeatedHit || skill->srvstfunc == ServerStartFunction::kFend) {   // Talon's kicks (FUN_005d5970), Zeal's hits (FUN_005daf40)
                        const auto env = calc_env();
                        int value = d2d::rules::eval_calc(game_data->skills, skill->calc[0], env, skill->id, lvl);
                        if (skill->srvstfunc == ServerStartFunction::kFend) value = std::min(value, int(in_reach().size()));   // Fend (FUN_005dae30): one per enemy in reach (FUN_0056bc80), at most calc1
                        actor->kicks_left = std::max(value, 1) - 1;
                    }
                } else if (!skill->attack_no_mana) {
                    actor->attack_mon = -1;                         // can't pay, won't swing
                    if (lvl > 0) need_mana(now_ms);
                    return false;
                }
            }
        }
        const auto* used = game_data->skills.get(actor->swing_skill);
        set_pmode(swing_mode(), now_ms);
        start_sequence(now_ms);
        if (used && actor->swing_skill != 0 && used->srvdofunc == ServerDoFunction::kResolveHit && used->aurastat[0] >= 0) self_state(*used, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::start_sequence(std::uint32_t now_ms) -> void {
        actor->seq = {};
        actor->seq_struck = 0;
        actor->seq_sounded = 0;
        actor->seq = skill_sequence();
        if (actor->seq.empty()) return;
        const auto& attack_anim = player_anim(kModeA1);
        start_move(*game_data->skills.get(actor->swing_skill), now_ms);
        const auto ticks = d2d::rules::attack_ticks(int(actor->seq.size()), int(attack_anim.speed ? attack_anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, chill_rate(now_ms));
        actor->pmode_until = now_ms + std::uint32_t(ticks) * 40;
        actor->seq_frame_ms = std::max<std::uint32_t>(std::uint32_t(ticks) * 40 / std::uint32_t(actor->seq.size()), 1);
        actor->prate = 1.f;
    }

auto Fight::skill_sequence() const -> std::span<const d2d::rules::SeqFrame> {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (!skill || actor->swing_skill == 0 || skill->anim != "SQ" || skill->seqnum <= 0) return {};
        const auto& attack_anim = player_anim(kModeA1);
        std::string weapon_class = attack_anim.name.size() >= 7 ? attack_anim.name.substr(4) : "hth";
        for (auto& letter : weapon_class) letter = char(std::tolower((unsigned char)letter));
        return d2d::rules::sequence(skill->seqnum, weapon_class);
    }

auto Fight::start_move(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        actor->smove = {};
        actor->seq_loop = false;
        const auto cls = std::size_t(std::max(actor->character.character_class, 0));
        if (skill.srvdofunc == ServerDoFunction::kWhirlwind) {
            actor->smove = { actor->move_x, actor->move_y, cells_per_sec(float(game_data->walk_velocity[cls])), true, false };
            actor->seq_loop = true;
            actor->pmode_until = ~0u;
            actor->whirl_next = now_ms;
        } else if (skill.srvdofunc == ServerDoFunction::kCharge) {
            actor->smove = { actor->move_x, actor->move_y, cells_per_sec(float(game_data->run_velocity[cls])) * float(std::max(100, 50) + skill.par[0]) / 100.f, true, false };
            actor->seq_loop = true;                                 // the run starts over until it reaches (FUN_005cf900)
            actor->pmode_until = ~0u;
        }
    }

auto Fight::seq_at(std::uint32_t now_ms) const -> std::size_t {
        const auto frame_index = std::size_t((now_ms - actor->player.mode_ms) / actor->seq_frame_ms);
        return actor->seq_loop ? frame_index % actor->seq.size() : std::min(frame_index, actor->seq.size() - 1);
    }

auto Fight::seq_events(std::uint32_t now_ms, int event) const -> int {
        const auto frame_index = std::size_t((now_ms - actor->player.mode_ms) / actor->seq_frame_ms);
        const auto per = std::ranges::count(actor->seq, event, &d2d::rules::SeqFrame::event);
        const auto last = actor->seq_loop ? frame_index : std::min(frame_index, actor->seq.size() - 1);
        int events = actor->seq_loop ? int(last / actor->seq.size() * std::size_t(per)) : 0;
        for (std::size_t k = 0; k <= last % (actor->seq_loop ? actor->seq.size() : ~std::size_t{ 0 }) && k < actor->seq.size(); ++k) events += actor->seq[k].event == event;
        return events;
    }

auto Fight::seq_view(std::uint32_t now_ms) const -> std::pair<int, std::uint32_t> {
        const auto& frame = actor->seq[seq_at(now_ms)];
        const auto mpf = player_anim(frame.mode).ms_per_frame();
        return { frame.mode, now_ms - mpf * frame.frame - mpf / 2 };
    }

auto Fight::swing_mode() const -> int {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (!skill || actor->swing_skill == 0) return kModeA1;
        // FUN_004cc5b0 dispatches S3 / S4 player attacks too (Barbarian / Assassin);
        // Skills.txt also uses S3 for the Druid's Rabies and Hunger attacks.
        return skill->anim == "KK" ? kModeKK : skill->anim == "S1" ? kModeS1 : skill->anim == "S3" ? kModeS3 : skill->anim == "S4" ? kModeS4 : kModeA1;
    }

auto Fight::swing() -> d2d::rules::Swing {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (!skill || actor->swing_skill == 0) return {};
        return skill_swing(*skill, skill_level ? skill_level(actor->swing_skill) : 1, calc_env(), true);
    }

auto Fight::skill_swing(const d2d::rules::Skill& skill_row, int lvl, const d2d::rules::CalcEnv& env, bool by_player) -> d2d::rules::Swing {
        d2d::rules::Swing swing;
        const auto* skill = &skill_row;
        const auto& skill_tables = game_data->skills;
        swing.ar_pct = d2d::rules::skill_tohit(skill_tables, *skill, env, lvl);
        swing.hit_class = skill->hit_class;
        if (skill->srvstfunc == ServerStartFunction::kChargeUp) return swing;                   // a charge-up: a plain hit at its to-hit (FUN_005d3490)
        if (skill->srvstfunc == ServerStartFunction::kDragonTail) {                            // Dragon Tail: a kick (FUN_005d7090 -> FUN_005d54b0)
            swing.kick = true;
            return swing;
        }
        if (skill->srvstfunc == ServerStartFunction::kDragonTalon || skill->srvdofunc == ServerDoFunction::kDragonFlight) {
            // Dragon Flight's kick (do 52, FUN_005d7850) builds as a Dragon
            // Talon kick (FUN_005d54b0, ED ln12), without its knockback.
            swing.kick = true;
            swing.ed_pct = d2d::rules::calc_ln(skill->par[0], skill->par[1], lvl);
            swing.skill_lo = d2d::rules::skill_phys(skill_tables, *skill, env, lvl, false);
            swing.skill_hi = d2d::rules::skill_phys(skill_tables, *skill, env, lvl, true);
            swing.knockback = skill->srvdofunc != ServerDoFunction::kDragonFlight && (!by_player || actor->kicks_left == 0);
        } else if (skill->srvdofunc == ServerDoFunction::kSmite) {                    // Smite (FUN_005ce9f0): calc1 ED, calc2 stun
            swing.ar_pct = 0;
            swing.smite = true;
            // With Holy Shield up (state 0x65), its damage (MinDam..MaxDam
            // by level) joins the shield's.
            for (const auto& state : actor->self_states)
                if (const auto* self_skill = skill_tables.get(state.skill); by_player && self_skill && self_cast(*self_skill)) {
                    swing.skill_lo = d2d::rules::skill_phys(skill_tables, *self_skill, env, state.level, false);
                    swing.skill_hi = d2d::rules::skill_phys(skill_tables, *self_skill, env, state.level, true);
                }
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            swing.stun_ticks = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            swing.knockback = (skill->result_flags & 8) != 0;
        } else if (skill->srvdofunc == ServerDoFunction::kRepeatedHit) {                     // Zeal's hit (FUN_005dbc60): calc2 ED, no ResultFlags
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            if (skill->etype >= 0 && skill->etype < 5)
                if (const int converted = d2d::rules::eval_calc(skill_tables, skill->calc[3], env, skill->id, lvl); converted > 0) { swing.conv_type = skill->etype; swing.conv_pct = converted; }
        } else if (skill->srvstfunc == ServerStartFunction::kSacrifice) {                     // Sacrifice (FUN_005ce790): calc1 ED on the weapon's physical
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            swing.srcdam = skill->srcdam;
            swing.knockback = (skill->result_flags & 8) != 0;
        } else if (skill->srvstfunc == ServerStartFunction::kVengeance) {                     // Vengeance (FUN_005cfe10)
            swing.fire_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            swing.cold_pct = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            swing.ltng_pct = d2d::rules::eval_calc(skill_tables, skill->calc[2], env, skill->id, lvl);
            swing.cold_len = d2d::rules::elem_length(skill_tables, *skill, env, lvl);
        } else {                                             // Bash's family (32), Power Strike (6), Berserk (39)
            // Jab (FUN_005db2d0), Dragon Claw (FUN_005d6200), Frenzy
            // (FUN_005d8b10) and Double Swing (Bash's FUN_005d7ea0) build
            // the same way on each hit.
            // Power Strike's start (FUN_005da940) passes no to-hit bonus and
            // no ResultFlags; only Bash's adds calc2 after the damage
            // (Berserk's calc2 is its state's length).
            if (skill->srvstfunc == ServerStartFunction::kElementalStrike) swing.ar_pct = 0;
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            if (skill->srvstfunc == ServerStartFunction::kBuildHit || skill->srvdofunc == ServerDoFunction::kDoubleSwing) swing.flat = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            swing.srcdam = skill->srcdam;
            swing.knockback = (skill->srvstfunc != ServerStartFunction::kElementalStrike && (skill->result_flags & 8) != 0) || skill->srvdofunc == ServerDoFunction::kCharge || skill->srvdofunc == ServerDoFunction::kLeapAttack;
            // FUN_005d7ea0 on a hit: EType stun stands the target for the
            // skill's elemental length (FUN_0056e0c0 -> FUN_0056c8e0 case
            // 9); another element with calc4 > 0 takes calc4 % of the
            // physical (Concentrate: Berserk's level to magic).
            if (skill->etype == 5) swing.stun_ticks = d2d::rules::elem_length(skill_tables, *skill, env, lvl);
            else if (skill->etype >= 0)
                if (const int converted = d2d::rules::eval_calc(skill_tables, skill->calc[3], env, skill->id, lvl); converted > 0) { swing.conv_type = skill->etype; swing.conv_pct = converted; }
        }
        return swing;
    }

auto Fight::land(std::size_t monster_index, const d2d::rules::Blow& blow, bool by_player, std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        auto& target = monsters[monster_index];
        if (!target.targetable()) return false;                     // under the sand, aloft: nothing reaches it
        if (target.align == 2) return false;                        // converted: no foe of the player's side (FUN_00554200)
        if (blow.blocked) { block_anim(*game_data, target, now_ms); return false; }
        if (!blow.hit) return false;
        target.hit_class = blow.hit_class;
        if (by_player) {
            auto& life = actor->character.stats.values[kLife];
            auto& mana = actor->character.stats.values[kMana];
            const auto life_before = life, mana_before = mana;
            life = std::min(actor->character.stats.values[kMaxLife], life + (std::int64_t(blow.life) << 8));
            mana = std::min(actor->character.stats.values[kMaxMana], mana + (std::int64_t(blow.mana) << 8));
            if (life > life_before) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "impact_steal_life", now_ms, actor->player.x, actor->player.y);
            if (mana > mana_before) cues.cue_on(unit_key(kSoundOnPlayer, actor->id), "impact_steal_mana", now_ms, actor->player.x, actor->player.y);
        }
        // One poison at a time, the stronger wins (FUN_0057ac50); at 0 life
        // the monster dies to it, the kill the poisoner's (FUN_005a6920).
        if (const double rate = double(blow.poison) / (std::max(blow.poison_ticks, 1) * 40.0);
            blow.poison > 0 && (now_ms >= target.poison_until || rate >= target.poison_rate)) {
            target.poison_rate = rate;
            target.poison_until = now_ms + std::uint32_t(blow.poison_ticks) * 40;
        }
        // Chilled (state 11, FUN_0057af80): not at coldeffect 0, the length
        // over MonsterColdDivisor when it's negative, at least a tick, only
        // ever lengthened.
        // ponytail: the 20 % roll for state 107 isn't kept.
        if (const int cold_effect = target.type >= 0 ? game_data->monsters.types[std::size_t(target.type)].diff[std::size_t(target.difficulty)].cold_effect : 0; blow.chill_ticks > 0 && cold_effect != 0) {
            const int divisor = game_data->cold_divisor[std::size_t(target.difficulty)];
            const int ticks = std::max(cold_effect < 0 && divisor != 0 ? blow.chill_ticks / divisor : blow.chill_ticks, 1);
            target.chill_until = std::max(target.chill_until, now_ms + std::uint32_t(ticks) * 40);
        }
        // Stunned (state 21, FUN_0057aae0): it stands until the stun ends, a
        // new stun resetting the length. A special monster (champion, unique,
        // superunique: monster data +0x16 & 8, FUN_005a0180) shrugs it off
        // 90 % of the time (the attacker's seed); MonStats boss (+0xc & 0x40,
        // FUN_0063e9f0) or Velocity 0 (+0x32) can't be stunned; 250 frames at
        // most (the hirelings' 13, FUN_0063ee90, is for mercs struck).
        // ponytail: the item stun length (stat 66) isn't added.
        if (const auto* stun_type = target.type >= 0 ? &game_data->monsters.types[std::size_t(target.type)] : nullptr;
            blow.stun_ticks > 0 && (!stun_type || (!stun_type->boss_column && stun_type->velocity != 0))) {
            const bool special = target.boss != d2d::rules::Boss::none && target.boss != d2d::rules::Boss::minion;
            if (!special || rng(100) >= 90) target.stun_until = now_ms + std::uint32_t(std::min(blow.stun_ticks, 250)) * 40;
        }
        if (blow.bleed) {
            target.bleed_rate = d2d::rules::open_wounds_per_sec(int(actor->character.stats.get(kLevel))) / 1000.0;
            target.bleed_until = now_ms + 8000;
        }
        // Life Tap (damagedinmelee / damagedbymissile, func 5): calc1 % of the
        // damage heals whoever dealt it.
        if (const auto* curse = target.curse.skill >= 0 ? game_data->skills.get(target.curse.skill) : nullptr; by_player && curse && curse->auratarget == "lifetap")
            actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife]
                                         + ((std::int64_t(blow.damage) * calc(*curse, curse->calc[0], target.curse.level) / 100) << 8));
        // ponytail: when soak (Bone Armor) takes part of the hit, blow.frac is
        // dropped: the 256ths aren't applied to the monster's life then.
        // game.exe's absorb (FUN_0057bf10) works on the fine value; that
        // partial case isn't modelled.
        const int taken = soak(target, d2d::rules::resisted(blow.phys, target_of(monster_index).res[0]), blow.damage);
        if (hurt(*game_data, target, taken, now_ms, taken == blow.damage ? blow.frac : 0)) { killed(monster_index, now_ms); return true; }
        // Knockback (result flag 8): a monster with GH and KB modes
        // (MonStats2, FUN_0046c140 bits 3 / 13; else it's a GH) goes into
        // KB (FUN_005a77d0): path type 8, 5 subtiles on from the attacker,
        // a Sand Leaper (class 0x4e) 10.
        // It slides there over the KB mode (FUN_005a8630: along the path till
        // the mode's end), then plays GH (FUN_005a8520; a Sand Leaper thinks 15
        // frames on, 45 stunned): monster_update.
        // ponytail: an even slide over KB's length; the path's own pace
        // (FUN_00554ca0) isn't traced.
        if (blow.knockback) knock_back(target, actor->player.x, actor->player.y, now_ms);
        return true;
    }

auto Fight::knock_back(Monster& target, float from_x, float from_y, std::uint32_t now_ms) -> void {
        if (game_data->npc_timing(target.npc, "KB").directions <= 0 || game_data->npc_timing(target.npc, "GH").directions <= 0) return;
        const float reach = game_data->monsters.types[std::size_t(target.type)].base == 0x4e ? 2.f : 1.f;
        const float dx = target.unit.x - from_x, dy = target.unit.y - from_y, distance = std::max(std::hypot(dx, dy), 0.01f);
        const float next_x = target.unit.x + dx / distance * reach, next_y = target.unit.y + dy / distance * reach;
        if (!level->unit_blocked(next_x, next_y, target.unit.shape)) {
            target.leap_from_x = target.unit.x; target.leap_from_y = target.unit.y; target.leap_to_x = next_x; target.leap_to_y = next_y;
            target.leaping = false;
            set_mode(*game_data, target, "KB", now_ms);
        }
    }

auto Fight::monster_dots(std::uint32_t now_ms, float elapsed) -> void {
        for (std::size_t i = 0; i < monsters.size(); ++i) {
            auto& monster = monsters[i];
            if (!monster.alive() || (now_ms >= monster.poison_until && now_ms >= monster.bleed_until)) continue;
            monster.dot_acc += ((now_ms < monster.poison_until ? monster.poison_rate : 0) + (now_ms < monster.bleed_until ? monster.bleed_rate : 0)) * elapsed * 1000;
            const int whole = int(monster.dot_acc);
            if (whole <= 0) continue;
            monster.dot_acc -= whole;
            monster.hit_points -= whole;
            if (!monster.alive()) { set_mode(*game_data, monster, "DT", now_ms); killed(i, now_ms); }
        }
    }

auto Fight::strike(std::uint32_t now_ms) -> void {
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && missile_skill(*skill)) {   // Magic Arrow: the bow's action frame
            if (!actor->pstruck && now_ms >= actor->player.mode_ms + std::uint32_t(float(player_anim(actor->pmode).action_ms()) / actor->prate)) { actor->pstruck = true; do_sounds(now_ms); fire(*skill, now_ms); }
            return;
        }
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && (skill->srvdofunc == ServerDoFunction::kThrow || skill->srvdofunc == ServerDoFunction::kDoubleThrow)) {   // the throws (TH)
            if (!actor->pstruck && now_ms >= actor->player.mode_ms + std::uint32_t(float(player_anim(actor->pmode).action_ms()) / actor->prate)) { actor->pstruck = true; do_sounds(now_ms); spot(*skill, now_ms); }
            return;
        }
        if (!actor->seq.empty()) {
            // The client runs the skill's do on each event 1..4 frame (LAB_004af4d0 ->
            // FUN_004c68f0); Jab's (cltdofunc 16, FUN_004f4590) plays dosound a on the
            // event-3 ones, before some of its thrusts.
            if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && skill->clt_do_func == 16)
                for (const int sounds = seq_events(now_ms, 3); actor->seq_sounded < sounds; ++actor->seq_sounded)
                    cues.cue_on(unit_key(kSoundOnPlayer, actor->id), skill->do_sound_a, now_ms, actor->player.x, actor->player.y);
            const int due = seq_events(now_ms);
            while (actor->seq_struck < due) {
                ++actor->seq_struck;
                // Frenzy's and Double Swing's second hand looks for another
                // target (FUN_0056bd10 on the odd frame, FUN_005d8e00 /
                // FUN_005d8470).
                if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && actor->seq_struck % 2 == 0 && (skill->srvdofunc == ServerDoFunction::kFrenzy || skill->srvdofunc == ServerDoFunction::kDoubleSwing))
                    other_target();
                if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && moving_skill(*skill)) move_event(*skill, now_ms);
                else hit(now_ms);
            }
            return;
        }
        if (actor->pstruck || actor->attack_mon < 0
            || now_ms < actor->player.mode_ms + std::uint32_t(float(player_anim(actor->pmode).action_ms()) / actor->prate)) return;
        actor->pstruck = true;
        do_sounds(now_ms);
        hit(now_ms);
    }

auto Fight::hit(std::uint32_t now_ms, float reach ) -> void {
        if (actor->attack_mon < 0) return;
        auto& target_monster = monsters[std::size_t(actor->attack_mon)];
        // Converted while the swing was on: no foe of the player's side (FUN_00554200, as land),
        // so no blow, and no charge, Frenzy or wear off it.
        if (!target_monster.targetable() || target_monster.align == 2
            || std::hypot(target_monster.unit.x - actor->player.x, target_monster.unit.y - actor->player.y) > reach) return;
        auto current_swing = swing();
        auto fighter = current_swing.kick ? actor->pf_kick : actor->player_combat;
        const auto* skill = game_data->skills.get(actor->swing_skill);
        const bool charging = skill && skill->srvstfunc == ServerStartFunction::kChargeUp, finishing = finisher(skill);
        std::erase_if(actor->charges, [&](const Charge& charge) { return now_ms >= charge.until; });
        if (finishing) add_charges(fighter, current_swing);
        if (skill && skill->srvstfunc != ServerStartFunction::kVengeance && std::ranges::contains(std::array{ ServerDoFunction::kResolveHit, ServerDoFunction::kRepeatedHit, ServerDoFunction::kSacrifice, ServerDoFunction::kJab, ServerDoFunction::kDragonClaw,
                ServerDoFunction::kFrenzy, ServerDoFunction::kDoubleSwing, ServerDoFunction::kWhirlwind, ServerDoFunction::kCharge, ServerDoFunction::kLeapAttack,
                ServerDoFunction::kPoisonDagger, ServerDoFunction::kChargedStrike, ServerDoFunction::kLightningStrike, ServerDoFunction::kRabies }, skill->srvdofunc)) skill_element(fighter, *skill);
        // Hunger (do 122, FUN_005c7f10): calc1 damage %, calc2 / calc3 life /
        // mana stolen.
        if (skill && skill->srvdofunc == ServerDoFunction::kHunger) {
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            current_swing.ed_pct += calc(*skill, skill->calc[0], lvl);
            fighter.life_steal += calc(*skill, skill->calc[1], lvl); fighter.mana_steal += calc(*skill, skill->calc[2], lvl);
        }
        const int hp_before = target_monster.hit_points;
        const auto target = std::size_t(actor->attack_mon);
        const auto blow = d2d::rules::player_blow(fighter, target_of(target), int(actor->character.stats.get(d2d::d2s::kLevel)), rng, current_swing);
        if (land(target, blow, true, now_ms)) player_thorns(target, blow);
        if (blow.hit && charging) charge(*skill, now_ms);
        if (blow.hit && finishing) release(target, now_ms);
        if (blow.hit && skill && skill->srvdofunc == ServerDoFunction::kDragonTail) dragon_tail(*skill, target, blow.phys, now_ms);
        if (blow.hit && skill && (skill->srvdofunc == ServerDoFunction::kFrenzy || skill->srvdofunc == ServerDoFunction::kStackingStrike)) frenzy(*skill, now_ms);   // Feral Rage / Maul stack as Frenzy
        if (blow.hit && skill && skill->srvstfunc == ServerStartFunction::kImpale) impale_wear(*skill, now_ms);
        if (blow.hit) wear_weapon(now_ms);
        if (blow.hit && skill && (skill->srvdofunc == ServerDoFunction::kChargedStrike || skill->srvdofunc == ServerDoFunction::kLightningStrike)) strike_bolts(*skill, target, now_ms);
        // Conversion (do 79, FUN_005d0350): on a hit, an evil monster no one
        // owns that swaps_ai lets (FUN_005d02b0: FUN_006259b0, FUN_0056e2f0)
        // turns at calc1 % for auralen frames: alignment 2 (FUN_005543b0) in
        // the caster's list (FUN_005b1900), its target and flag-8 states
        // dropped. One above the caster's level comes down to it, its life in
        // proportion (state 0x6d keeps its own, stats 0xb0 / 0xb1).
        // ponytail: FUN_00483360's operands (the decompiler drops them) read
        // as life x clvl / mlvl.
        if (blow.hit && skill && skill->srvdofunc == ServerDoFunction::kConversion && monsters[target].alive() && monsters[target].align == 0
            && swaps_ai(*game_data, monsters[target])) {
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            auto& turned = monsters[target];
            if (int(rng(100)) < calc(*skill, skill->calc[0], lvl)
                && put_state(*game_data, turned, turned.cry, { skill->id, lvl, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, lvl), 1)) * 40 }, now_ms)) {
                turned.align = 2; turned.set_kind = 0; turned.chase = -1;
                if (const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel)); turned.stats.level > clvl && turned.own_level == 0) {
                    turned.own_level = turned.stats.level; turned.own_max_life = turned.stats.hit_points;
                    turned.stats.hit_points = std::max(int(std::int64_t(turned.stats.hit_points) * clvl / turned.own_level), 1);
                    turned.hit_points = std::clamp(int(std::int64_t(turned.hit_points) * clvl / turned.own_level), 1, turned.stats.hit_points);
                    turned.stats.level = clvl;
                }
            }
        }
        // Sacrifice's price (FUN_005ce8e0): calc2 % of the physical dealt,
        // no more than the target had left, off the player's life.
        if (blow.hit && skill && skill->srvdofunc == ServerDoFunction::kSacrifice) {
            const int dealt = std::min(d2d::rules::resisted(blow.phys, target_of(target).res[0]), std::max(hp_before, 0));
            const auto env = calc_env();
            actor->self_hurt += (std::int64_t(dealt) << 8) * d2d::rules::eval_calc(game_data->skills, skill->calc[1], env, skill->id,
                                                                            skill_level ? skill_level(skill->id) : 1) / 100;
        }
        if (!monsters[target].alive()) actor->attack_mon = -1;
    }

auto Fight::strike_bolts(const d2d::rules::Skill& skill, std::size_t monster_index, std::uint32_t now_ms) -> void {
        const auto found = game_data->missiles.find(skill.srvmissilea);
        if (found == game_data->missiles.end()) return;
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const float x = monsters[monster_index].unit.x, y = monsters[monster_index].unit.y, dx = x - actor->player.x, dy = y - actor->player.y;
        if (skill.srvdofunc == ServerDoFunction::kChargedStrike) {
            for (int k = 0; k < std::max(calc(skill, skill.calc[0], lvl), 1); ++k) {
                const float angle = (float(rng(81)) - 40) * 3.14159265f / 180;
                launch(found->second, skill, lvl, x, y, dx * std::cos(angle) - dy * std::sin(angle), dx * std::sin(angle) + dy * std::cos(angle),
                       found->second.range + found->second.lev_range * lvl, now_ms).struck->push_back(int(monster_index));
            }
            return;
        }
        const int reach = calc(skill, skill.calc[0], lvl);
        int best = -1; float best_distance = 1e9f;
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (const float distance = std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y); j != monster_index && monsters[j].targetable() && distance * 5 <= float(reach) && distance < best_distance) {
                best_distance = distance; best = int(j);
            }
        if (best < 0) return;
        const auto& next_target = monsters[std::size_t(best)];
        launch(found->second, skill, lvl, x, y, next_target.unit.x - x, next_target.unit.y - y, found->second.range, now_ms).hops = calc(skill, skill.calc[1], lvl);
    }

auto Fight::corpse_blast(const d2d::rules::Skill& skill, int lvl, std::size_t corpse_index, std::uint32_t now_ms) -> void {
        auto& corpse = monsters[corpse_index];
        corpse.corpse_used = true;
        const int low = calc(skill, skill.calc[0], lvl), high = std::max(calc(skill, skill.calc[1], lvl), low);
        const int dmg = int(std::int64_t(corpse.stats.hit_points) * rng.range(low, high) / 100);
        const int radius = std::max(calc(skill, skill.aurarange, lvl) / 2, 1);
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - corpse.unit.x, monsters[j].unit.y - corpse.unit.y) * 5 <= float(radius)) {
                const auto target = target_of(j);
                d2d::rules::Blow blow{ .hit = true };
                blow.damage = d2d::rules::resisted(dmg / 2, target.res[0]) + d2d::rules::resisted(dmg - dmg / 2, target.res[2]);
                land(j, blow, true, now_ms);
            }
    }

auto Fight::spot_skill(const d2d::rules::Skill& skill) -> bool {
        return std::ranges::contains(std::array{ ServerDoFunction::kPsychicHammer, ServerDoFunction::kMindBlast, ServerDoFunction::kStaticField, ServerDoFunction::kCorpseExplosion,
            ServerDoFunction::kPoisonExplosion, ServerDoFunction::kTeleport, ServerDoFunction::kCurse, ServerDoFunction::kStateAroundCaster,
            ServerDoFunction::kConfuse, ServerDoFunction::kAttract, ServerDoFunction::kTaunt, ServerDoFunction::kFindPotion,
            ServerDoFunction::kFindItem, ServerDoFunction::kLeap, ServerDoFunction::kTelekinesis, ServerDoFunction::kDragonFlight,
            ServerDoFunction::kDoubleThrow, ServerDoFunction::kThrow }, skill.srvdofunc);
    }

auto Fight::player_caster(int skill) const -> Caster {
        return { actor->player.x, actor->player.y, skill_level ? skill_level(skill) : 1, actor->attack_mon, actor->cast_x, actor->cast_y, false };
    }

auto Fight::spot(const d2d::rules::Skill& skill, std::uint32_t now_ms, const Caster& caster) -> void {
        const int lvl = caster.level;
        const auto env = calc_env();
        const auto damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
        auto within = [&](float x, float y, int radius, auto&& each) {
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(radius)) each(j);
        };
        switch (skill.srvdofunc) {
            case ServerDoFunction::kPsychicHammer:
                if (caster.target >= 0 && monsters[std::size_t(caster.target)].alive()) {
                    const auto monster_index = std::size_t(caster.target);
                    auto blow = d2d::rules::missile_blow(damage, target_of(monster_index), pierce(), rng);
                    blow.knockback = int(rng(100)) < calc(skill, skill.calc[0], lvl);
                    land(monster_index, blow, true, now_ms);
                }
                break;
            case ServerDoFunction::kMindBlast:
                within(caster.to_x, caster.to_y, calc(skill, skill.aurarange, lvl), [&](std::size_t monster_index) { land(monster_index, d2d::rules::missile_blow(damage, target_of(monster_index), pierce(), rng), true, now_ms); });
                break;
            case ServerDoFunction::kStaticField: {
                const int pct = calc(skill, skill.calc[0], lvl);
                within(actor->player.x, actor->player.y, calc(skill, skill.aurarange, lvl), [&](std::size_t monster_index) {
                    d2d::rules::Blow blow{ .hit = true };
                    blow.damage = d2d::rules::resisted(monsters[monster_index].hit_points * pct / 100, target_of(monster_index).res[3]);
                    land(monster_index, blow, true, now_ms);
                });
                break;
            }
            case ServerDoFunction::kCorpseExplosion: case ServerDoFunction::kPoisonExplosion: {
                const int corpse_index = corpse_near(actor->cast_x, actor->cast_y);
                if (corpse_index < 0) break;
                auto& corpse = monsters[std::size_t(corpse_index)];
                corpse.corpse_used = true;
                if (skill.srvdofunc == ServerDoFunction::kPoisonExplosion) {
                    if (const auto found = game_data->missiles.find(skill.srvmissilea); found != game_data->missiles.end())
                        launch(found->second, skill, lvl, corpse.unit.x, corpse.unit.y, 0, 0, found->second.range, now_ms);
                    break;
                }
                corpse_blast(skill, lvl, std::size_t(corpse_index), now_ms);
                break;
            }
            case ServerDoFunction::kTeleport: case ServerDoFunction::kLeap: case ServerDoFunction::kDragonFlight: {
                // Teleport; Dragon Flight (start 12, FUN_005c9030: a target
                // within aurarange, out of town; do 52, FUN_005d7850: the
                // flight to the free spot nearest it at once, FUN_00554ea0,
                // then the kick). The level's Teleport (Levels.txt, read by do
                // 52 through FUN_0061db70): 0 none, 2 only in a clear line
                // (FUN_006229f0, 0x804). Leap (start 40, FUN_005d9d50; do 77,
                // FUN_005da370): the point pulled back to aurarange subtiles
                // (FUN_005d9a10), a free spot (mask 0x1c09) reached in a line,
                // state 0x12; it flies there (path type 9 at the class's run
                // velocity, FUN_005d9f70), lands (FUN_005da120) and knocks back
                // every monster within calc1 subtiles (result 9, FUN_0056bad0):
                // Fight::leap.
                // ponytail: the line tests sample the way at a fifth of a cell;
                // FUN_005d9a10's two fallback spots aren't tried.
                float target_x = actor->cast_x, target_y = actor->cast_y;
                const int tgt = actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive() ? actor->attack_mon : -1;
                const auto clear_line = [&](float to_x, float to_y) {
                    const float span = std::hypot(to_x - actor->player.x, to_y - actor->player.y);
                    for (float along = 0.2f; along < span; along += 0.2f)
                        if (level->unit_blocked(actor->player.x + (to_x - actor->player.x) * along / span, actor->player.y + (to_y - actor->player.y) * along / span)) return false;
                    return true;
                };
                if (skill.srvdofunc == ServerDoFunction::kDragonFlight) {
                    if (tgt < 0) break;
                    const auto& target = monsters[std::size_t(tgt)];
                    if (std::hypot(target.unit.x - actor->player.x, target.unit.y - actor->player.y) * 5 > float(calc(skill, skill.aurarange, lvl))) break;
                    const float dx = actor->player.x - target.unit.x, dy = actor->player.y - target.unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                    target_x = target.unit.x + dx / distance * 0.8f; target_y = target.unit.y + dy / distance * 0.8f;
                }
                if (skill.srvdofunc == ServerDoFunction::kLeap) {
                    const float reach = float(calc(skill, skill.aurarange, lvl)) / 5, span = std::hypot(target_x - actor->player.x, target_y - actor->player.y);
                    if (span > reach) { target_x = actor->player.x + (target_x - actor->player.x) * reach / span; target_y = actor->player.y + (target_y - actor->player.y) * reach / span; }
                    std::tie(target_x, target_y) = level->nearest_free(target_x, target_y);
                    if (in_town(level) || std::hypot(target_x - actor->player.x, target_y - actor->player.y) > reach + 0.2f || !clear_line(target_x, target_y)) break;
                    const float speed = cells_per_sec(float(game_data->run_velocity[std::size_t(std::max(actor->character.character_class, 0))]));
                    const auto flight = std::uint32_t(std::hypot(target_x - actor->player.x, target_y - actor->player.y) / std::max(speed, 0.1f) * 1000);
                    actor->leap = Leap{ actor->player.x, actor->player.y, target_x, target_y, now_ms, now_ms + flight, skill.id, lvl };
                    actor->player.path.clear(); actor->player.walking = false;
                    break;
                }
                if (in_town(level) || level->unit_blocked(target_x, target_y) || level->teleport == 0 || (level->teleport == 2 && !clear_line(target_x, target_y))) break;
                actor->player.x = target_x; actor->player.y = target_y;
                actor->player.path.clear(); actor->player.walking = false;
                actor->player.goal_x = target_x; actor->player.goal_y = target_y;
                actor->reassigned = true;                                           // FUN_00554ea0's 0x10000: the client's 0x15
                if (skill.srvdofunc == ServerDoFunction::kDragonFlight) {             // the kick, then its charges released, hit or not (FUN_005d5220)
                    const auto kick = d2d::rules::player_blow(actor->pf_kick, target_of(std::size_t(tgt)), int(actor->character.stats.get(d2d::d2s::kLevel)), rng,
                                                              skill_swing(skill, lvl, env, true));
                    if (land(std::size_t(tgt), kick, true, now_ms)) player_thorns(std::size_t(tgt), kick);   // a melee blow like any kick
                    release(std::size_t(tgt), now_ms);
                }
                break;
            }
            case ServerDoFunction::kTelekinesis:                                         // Telekinesis (do 21): its damage and a knockback on the monster
                if (actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive()) {
                    auto blow = d2d::rules::missile_blow(damage, target_of(std::size_t(actor->attack_mon)), pierce(), rng);
                    blow.knockback = true;
                    land(std::size_t(actor->attack_mon), blow, true, now_ms);
                }
                break;
            // Curses (do 30, FUN_005c37c0; 61 Confuse, FUN_005c3f20): the
            // auratargetstate on every monster within aurarange of the point
            // for auralen frames (FUN_0056e780), one curse at a time. Inner
            // Sight / Slow Missiles (do 6, FUN_005db1c0) round the caster;
            // Attract (59, FUN_005c3b90) and Taunt (71, FUN_005d8570) on the
            // monster.
            // Confuse (FUN_005c3de0) takes evil monsters only, makes them
            // neutral (FUN_005543b0, list 9) and, with SwitchAI (FUN_00573090),
            // sets their target to kind 3 until its end (event 10). Attract
            // makes its evil monster neutral, and each evil one within
            // aurarange of it (FUN_0056e780, FUN_0056d2c0: the caster's
            // target; callback 0x5c3b30) gets that monster as its kind 2
            // target until the same end.
            // The length is auralen; Confuse, Attract (FUN_005c3bcc) and the
            // curses whose state is dimvision (23) or terror (56)
            // (FUN_005c3400) divide it by DifficultyLevels AiCurseDivisor
            // (FUN_005c37a0, +0x1c).
            // Dim Vision and Terror (FUN_005c35c0 with FUN_005c3400's flag)
            // land only on a monster swaps_ai lets, not neutral
            // (FUN_006259b0), and put it on special AI 10 / 11
            // (FUN_005c34b0); their end toggles it back (FUN_005c3370).
            case ServerDoFunction::kCurse: case ServerDoFunction::kConfuse: case ServerDoFunction::kStateAroundCaster: case ServerDoFunction::kAttract: case ServerDoFunction::kTaunt: {
                int length = calc(skill, skill.auralen, lvl);
                if (const int divisor = game_data->curse_divisor[std::size_t(std::clamp(game_difficulty, 0, 2))];
                    divisor != 0 && (skill.srvdofunc == ServerDoFunction::kConfuse || skill.srvdofunc == ServerDoFunction::kAttract
                                     || (skill.srvdofunc == ServerDoFunction::kCurse && (skill.auratarget == "dimvision" || skill.auratarget == "terror"))))
                    length /= divisor;
                const std::uint32_t until = now_ms + std::uint32_t(std::max(length, 25)) * 40;
                const bool curse = skill.srvdofunc == ServerDoFunction::kCurse || skill.srvdofunc == ServerDoFunction::kConfuse || skill.srvdofunc == ServerDoFunction::kAttract;
                const auto set = [&](Monster& monster, int kind, int id) {
                    if (game_data->monsters.types[std::size_t(monster.type)].switch_ai) monster.set_kind = kind, monster.set_id = id, monster.set_until = until;
                };
                const int special = skill.srvdofunc != ServerDoFunction::kCurse ? 0 : skill.auratarget == "dimvision" ? 10 : skill.auratarget == "terror" ? 11 : 0;
                auto put = [&](std::size_t monster_index) {
                    auto& monster = monsters[monster_index];
                    // Confuse (FUN_005c3d50): evil, hostile, alive, and FUN_0056e2f0's test.
                    if (skill.srvdofunc == ServerDoFunction::kConfuse && (monster.align || !monster.alive() || !swaps_ai(*game_data, monster))) return;
                    if (special && (monster.align == 1 || !swaps_ai(*game_data, monster))) return;
                    if (!put_state(*game_data, monster, curse ? monster.curse : monster.cry, { skill.id, lvl, until, special != 0 }, now_ms)) return;
                    if (special) ai_toggle(*game_data, monster, special);
                    if (skill.srvdofunc == ServerDoFunction::kConfuse) { monster.align = 1; set(monster, 3, 0); }
                };
                if (skill.srvdofunc == ServerDoFunction::kAttract || skill.srvdofunc == ServerDoFunction::kTaunt) {
                    const bool lured = actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive() && (skill.srvdofunc == ServerDoFunction::kTaunt || !monsters[std::size_t(actor->attack_mon)].align);
                    if (lured) put(std::size_t(actor->attack_mon));
                    if (lured && skill.srvdofunc == ServerDoFunction::kAttract) {
                        auto& lure = monsters[std::size_t(actor->attack_mon)];
                        lure.align = 1;
                        within(lure.unit.x, lure.unit.y, calc(skill, skill.aurarange, lvl), [&](std::size_t index) { if (!monsters[index].align) set(monsters[index], 2, lure.id); });
                    }
                } else {
                    within(skill.srvdofunc == ServerDoFunction::kStateAroundCaster ? caster.x : caster.to_x, skill.srvdofunc == ServerDoFunction::kStateAroundCaster ? caster.y : caster.to_y, calc(skill, skill.aurarange, lvl), put);
                }
                break;
            }
            // Double Throw (do 74, FUN_005d88b0): each hand's throwing weapon
            // flies at the target as its missile (weapons.txt missiletype),
            // with calc1 damage % (stat 25).
            // ponytail: toht (stat 19) isn't added; no ammo is spent.
            // Throw (do 3, FUN_0056f460): the throwable in hand flies as its
            // missile (weapons.txt missiletype) and one of it is spent.
            // ponytail: FUN_0056c600's to-hit / damage % from the skill aren't
            // added; the shot's damage is the attack's, not the weapon's
            // throw damage (minmisdam..).
            case ServerDoFunction::kThrow:
                if (const int index = ammo_index(); index >= 0)
                    if (const auto found_thrown = game_data->thrown.find(actor->character.items[std::size_t(index)].code); found_thrown != game_data->thrown.end())
                        if (const auto found = game_data->missiles.find(found_thrown->second); found != game_data->missiles.end()) {
                            launch(found->second, skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, found->second.range + found->second.lev_range * lvl, now_ms);
                            spend_ammo();
                        }
                break;
            case ServerDoFunction::kDoubleThrow:
                for (const auto& item : actor->character.items)
                    if (item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm))
                        if (const auto found_thrown = game_data->thrown.find(item.code); found_thrown != game_data->thrown.end())
                            if (const auto found = game_data->missiles.find(found_thrown->second); found != game_data->missiles.end())
                                launch(found->second, skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y,
                                       found->second.range + found->second.lev_range * lvl, now_ms).ed_pct = calc(skill, skill.calc[0], lvl);
                break;
            // Find Potion (do 69, FUN_005d81c0) / Find Item (72, FUN_005d8780):
            // a corpse not yet searched (state 0x76), at calc1 %: a potion
            // (FUN_005d8100), or the monster's treasure again (FUN_005a8000,
            // a tier by Param1..4); the potion by the act and difficulty
            // (rules::find_potion).
            // ponytail: Find Item rolls the monster's own class.
            case ServerDoFunction::kFindPotion: case ServerDoFunction::kFindItem: {
                const int corpse_index = corpse_near(actor->cast_x, actor->cast_y);
                if (corpse_index < 0) break;
                auto& corpse = monsters[std::size_t(corpse_index)];
                corpse.corpse_used = true;
                if (int(rng(100)) >= calc(skill, skill.calc[0], lvl)) break;
                if (skill.srvdofunc == ServerDoFunction::kFindItem) { const auto [players, nearby] = drop_counts(); loot.drop(corpse, spawning.game, now_ms, players, nearby); break; }
                const std::string code = d2d::rules::find_potion(level ? level->id : 1, game_difficulty, int(rng(100)), skill.par[2], skill.par[3]);
                loot.put({ code, 2, 0 }, corpse.unit.x, corpse.unit.y, corpse.stats.level, spawning.game, now_ms);
                break;
            }
            default: break;
        }
    }

auto Fight::skill_element(d2d::rules::Fighter& fighter, const d2d::rules::Skill& skill) -> void {
        if (skill.etype < 0 || skill.etype >= 5) return;
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        auto& [low, high] = fighter.elem[std::size_t(skill.etype)];
        const int len = d2d::rules::elem_length(game_data->skills, skill, env, lvl);
        const std::int64_t elo = d2d::rules::elem_damage(game_data->skills, skill, env, lvl, false, 0, true),
                           ehi = d2d::rules::elem_damage(game_data->skills, skill, env, lvl, true, 0, true);
        if (skill.etype == 3) {                                  // poison: 256ths a tick over its length
            low += int(elo * std::max(len, 1) >> 8); high += int(ehi * std::max(len, 1) >> 8);
            fighter.poison_len = std::max(fighter.poison_len, len);
            return;
        }
        low += int(elo >> 8); high += int(ehi >> 8);
        if (skill.etype == 2) fighter.cold_len = std::max(fighter.cold_len, len);
    }

auto Fight::self_state(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        std::uint32_t until = actor->pmode_until;
        if (skill.srvstfunc == ServerStartFunction::kBerserk) {
            const auto env = calc_env();
            const int ticks = d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl);
            until = now_ms + std::uint32_t(ticks > 0 ? ticks : 10) * 40;
        }
        std::erase_if(actor->self_states, [&](const SelfState& state) { return state.skill == skill.id; });
        actor->self_states.push_back({ skill.id, lvl, until });
    }

auto Fight::start_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms) -> void {
        const int len = calc(skill, skill.auralen, lvl);
        const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
        std::erase_if(actor->self_states, [&](const SelfState& state) { return state.skill == skill.id; });
        actor->self_states.push_back({ skill.id, lvl, until });
        for (std::size_t k = 0; k < skill.aurastat.size(); ++k)
            if (skill.aurastat[k] == d2d::d2s::kBoneArmor) actor->absorb_pool = calc(skill, skill.aura_calc[k], lvl) >> 8, actor->absorb_skill = skill.id;
        if (skill.srvdofunc == ServerDoFunction::kCloakOfShadows)
            for (auto& monster : monsters)
                if (monster.targetable() && std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(calc(skill, skill.aurarange, lvl)))
                    cloak(*game_data, monster, skill.id, lvl, until, now_ms);
    }

auto Fight::state_of(std::string_view name) const -> const SelfState* {
        for (const auto& state : actor->self_states)
            if (const auto* skill = game_data->skills.get(state.skill); skill && skill->aurastate == name) return &state;
        return nullptr;
    }

auto Fight::absorb(const Foe& foe, std::uint32_t now_ms) -> int {
        using namespace d2d::d2s;
        int damage = foe.damage;
        if (damage <= 0) return 0;
        // Bone Armor (event 22, FUN_005c6370) takes from the physical part
        // (the hit's +8), Cyclone Armor (25, FUN_005c87a0) from fire,
        // lightning and cold (+0x10, +0x1c, +0x24).
        if (actor->absorb_pool > 0 && std::ranges::contains(actor->self_states, actor->absorb_skill, &SelfState::skill)) {
            const auto* pool_skill = game_data->skills.get(actor->absorb_skill);
            const int part = pool_skill && pool_skill->aurastate == "cyclonearmor" ? foe.elemental : foe.physical;
            const int absorbed = std::min({ actor->absorb_pool, part, damage });
            actor->absorb_pool -= absorbed; damage -= absorbed;
            if (actor->absorb_pool <= 0) std::erase_if(actor->self_states, [&](const SelfState& state) { return state.skill == actor->absorb_skill; });
        }
        if (const auto* energy_shield = state_of("energyshield"); energy_shield && damage > 0) {
            const auto* skill = game_data->skills.get(energy_shield->skill);
            const int part = damage * std::clamp(calc(*skill, skill->calc[0], energy_shield->level), 0, 95) / 100;
            const int per16 = std::max(calc(*skill, skill->calc[1], energy_shield->level), 1);
            const int can = int(std::min<std::int64_t>(part, (actor->character.stats.values[kMana] >> 8) * 16 / per16));
            actor->character.stats.values[kMana] -= (std::int64_t(can) * per16 / 16) << 8;
            damage -= can;
            if (can > 0 && actor->character.character_class == 1) {
                static constexpr std::array<std::string_view, 5> kEnergyShieldHits = { "sorceress_energyshield_hit_1", "sorceress_energyshield_hit_2",
                    "sorceress_energyshield_hit_3", "sorceress_energyshield_hit_4", "sorceress_energyshield_hit_5" };
                cues.cue_on(unit_key(kSoundOnPlayer, actor->id), kEnergyShieldHits[std::size_t(sound_rng(int(kEnergyShieldHits.size())))], now_ms, actor->player.x, actor->player.y);
            }
        }
        return damage;
    }

auto Fight::buff_events(Foe& foe, std::uint32_t now_ms) -> void {
        // Frozen Armor (event 2, FUN_005cac50, damagedinmelee): calc1 frames'
        // freeze on a striker whose blow did damage; Shiver Armor (3,
        // FUN_005cad40, attackedinmelee): its element (FUN_0056e0c0) on any.
        for (const int striker : foe.melee_by) {
            const int found = this->monster_index(striker);
            if (found < 0 || !monsters[std::size_t(found)].alive()) continue;
            const auto monster_index = std::size_t(found);
            if (const auto* frozen_armor = state_of("frozenarmor"); frozen_armor && std::ranges::contains(foe.damaged_by, striker)) {
                const auto* skill = game_data->skills.get(frozen_armor->skill);
                const int frames = std::max(calc(*skill, skill->calc[0], frozen_armor->level), 1);
                monsters[monster_index].stun_until = std::max(monsters[monster_index].stun_until, now_ms + std::uint32_t(frames) * 40);
                monsters[monster_index].chill_until = std::max(monsters[monster_index].chill_until, now_ms + std::uint32_t(frames) * 40);
            }
            if (const auto* shiver_armor = state_of("shiverarmor")) {
                const auto* skill = game_data->skills.get(shiver_armor->skill);
                land(monster_index, d2d::rules::missile_blow(d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), shiver_armor->level), target_of(monster_index), pierce(), rng),
                     true, now_ms);
            }
        }
    }

auto Fight::return_fire(const Foe& foe, std::uint32_t now_ms) -> void {
        // Chilling Armor (event 1, FUN_005cab40): a ReturnFire missile's
        // owner, alive and hostile, gets the skill's srvmissilea (+0x48)
        // back from the wearer at where it stands.
        if (const auto* chilling_armor = state_of("chillingarmor"))
            for (const int owner : foe.fired_by) {
                const auto* skill = game_data->skills.get(chilling_armor->skill);
                const auto found = game_data->missiles.find(skill->srvmissilea);
                const int shooter = monster_index(owner);
                if (shooter < 0 || !monsters[std::size_t(shooter)].alive() || found == game_data->missiles.end()) continue;
                const auto& shooter_unit = monsters[std::size_t(shooter)].unit;
                launch(found->second, *skill, chilling_armor->level, actor->player.x, actor->player.y, shooter_unit.x - actor->player.x, shooter_unit.y - actor->player.y, found->second.range, now_ms);
            }
    }

auto Fight::merc_buff_events(Foe& foe, std::uint32_t now_ms) -> void {
        const auto* skill = actor->merc_buff.skill >= 0 && now_ms < actor->merc_buff.until ? game_data->skills.get(actor->merc_buff.skill) : nullptr;
        if (!skill || skill->aurastate != "frozenarmor") return;
        const int frames = std::max(d2d::rules::eval_calc(game_data->skills, skill->calc[0], merc_calc_env(), skill->id, actor->merc_buff.level), 1);
        for (const int striker : foe.damaged_by) {                    // as the player's (buff_events): its strikers freeze
            const int found = this->monster_index(striker);
            if (found < 0 || !monsters[std::size_t(found)].alive()) continue;
            const auto monster_index = std::size_t(found);
            monsters[monster_index].stun_until = std::max(monsters[monster_index].stun_until, now_ms + std::uint32_t(frames) * 40);
            monsters[monster_index].chill_until = std::max(monsters[monster_index].chill_until, now_ms + std::uint32_t(frames) * 40);
        }
    }

auto Fight::buff_tick(std::uint32_t now_ms) -> void {
        if (const auto* blaze = state_of("blaze"); blaze && actor->player.walking && now_ms / 40 != actor->blaze_frame) {
            actor->blaze_frame = now_ms / 40;
            const auto* skill = game_data->skills.get(blaze->skill);
            if (const auto found = game_data->missiles.find(skill->srvmissilea); found != game_data->missiles.end())
                launch(found->second, *skill, blaze->level, actor->player.x, actor->player.y, 0, 0, found->second.range + found->second.lev_range * blaze->level, now_ms);
        }
        // Armageddon / Hurricane (do 124, FUN_005c8190: the state and its
        // timer event, FUN_005417d0(5, ...)): Armageddon drops its control
        // row (hit 56) on a spot within aurarange every 5 frames; Hurricane
        // strikes what's within aurarange with its cold once a second.
        // ponytail: the timer's period (FUN_004efc80) and the events aren't
        // traced: the paces are by eye (level 24 / 30 skills: past Act 1).
        // Blade Shield: blade_shield, every perdelay frames.
        if (const auto* armageddon = state_of("armageddon"); armageddon && (now_ms / 40) % 5 == 0 && now_ms / 40 != actor->storm_frame) {
            const auto* skill = game_data->skills.get(armageddon->skill);
            if (const auto found = game_data->missiles.find(skill->srvmissilea); found != game_data->missiles.end()) {
                const int radius = std::max(calc(*skill, skill->aurarange, armageddon->level), 1);
                launch(found->second, *skill, armageddon->level, actor->player.x + float(int(rng(2 * radius + 1)) - radius) / 5, actor->player.y + float(int(rng(2 * radius + 1)) - radius) / 5,
                       0, 0, found->second.range, now_ms);
            }
        }
        if (const auto* state = state_of("hurricane"); state && now_ms / 40 != actor->storm_frame && (now_ms / 40) % 25 == 0) {
            const auto* skill = game_data->skills.get(state->skill);
            const auto damage = d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), state->level);
            const int radius = std::max(calc(*skill, skill->aurarange, state->level), 1);
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - actor->player.x, monsters[j].unit.y - actor->player.y) * 5 <= float(radius))
                    land(j, d2d::rules::missile_blow(damage, target_of(j), pierce(), rng), true, now_ms);
        }
        if (const auto* state = state_of("bladeshield"); state && now_ms >= actor->blade_next) {
            const auto* skill = game_data->skills.get(state->skill);
            actor->blade_next = now_ms + std::uint32_t(std::max(calc(*skill, skill->perdelay_calc, state->level), 1)) * 40;
            blade_shield(*skill, state->level, actor->player.x, actor->player.y, actor->player_combat, true, now_ms);
        }
        actor->storm_frame = now_ms / 40;
        // Thunder Storm (do 29, FUN_005ca4d0): a periodic skill (stats 0x15e /
        // 0x15f), every perdelay frames while its state lasts. The bolt
        // (srvmissilea, hit at once: FUN_005adf10) goes at the monster within
        // Param7 subtiles (FUN_005c8bc0, record +0x160) whose unit id comes
        // next after the last one struck, round to the lowest (FUN_0056bd10's
        // callback 0x56bcd0): it takes them in turn.
        if (const auto* thunderstorm = state_of("thunderstorm"); thunderstorm && now_ms >= actor->storm_next) {
            const auto* skill = game_data->skills.get(thunderstorm->skill);
            actor->storm_next = now_ms + std::uint32_t(std::max(calc(*skill, skill->perdelay_calc, thunderstorm->level), 1)) * 40;
            int best = -1, lowest = -1;
            for (std::size_t j = 0; j < monsters.size(); ++j) {
                const auto& monster = monsters[j];
                if (!monster.targetable() || monster.align == 2 || std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 > float(skill->par[6])) continue;
                if (lowest < 0 || monster.id < monsters[std::size_t(lowest)].id) lowest = int(j);
                if (monster.id > actor->storm_last && (best < 0 || monster.id < monsters[std::size_t(best)].id)) best = int(j);
            }
            if (best < 0) best = lowest;
            if (best >= 0) { actor->storm_last = monsters[std::size_t(best)].id; actor->storm_struck = true; }
            if (best >= 0)
                land(std::size_t(best), d2d::rules::missile_blow(d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), thunderstorm->level),
                                                                 target_of(std::size_t(best)), pierce(), rng), true, now_ms);
        }
    }

auto Fight::monster_states(std::uint32_t now_ms) -> void {
        for (auto& monster : monsters) {
            monster.dmg_pct = monster.speed_pct = monster.reflect_pct = monster.anim_pct = 0;
            monster.align = 0;                                // Confuse's / Attract's end (FUN_005c3db0 / 0x5c3b00), or another curse in its place
            monster.afraid = false;
            for (auto* effect : { &monster.curse, &monster.cry, &monster.frenzy }) {
                if (effect->skill < 0) continue;
                if (now_ms >= effect->until) { end_state(*game_data, monster, *effect); continue; }
                const auto* skill = game_data->skills.get(effect->skill);
                if (!skill) continue;
                for (std::size_t aura_stat = 0; aura_stat < skill->aurastat.size(); ++aura_stat) {
                    if (skill->aurastat[aura_stat] == 25) monster.dmg_pct += calc(*skill, skill->aura_calc[aura_stat], effect->level);
                    if (skill->aurastat[aura_stat] == 67) monster.speed_pct += calc(*skill, skill->aura_calc[aura_stat], effect->level);
                    if (skill->aurastat[aura_stat] == 68 || skill->aurastat[aura_stat] == 69)   // attackrate, other_animrate: the same rate (MonFrenzy)
                        monster.anim_pct = std::max(monster.anim_pct, calc(*skill, skill->aura_calc[aura_stat], effect->level));
                }
                const auto& targets = skill->auratarget;
                if (targets == "ironmaiden") monster.reflect_pct += calc(*skill, skill->calc[0], effect->level);
                if (targets == "terror") monster.afraid = true;
                if (targets == "conversion") monster.align = 2;
                if (targets == "confuse" || targets == "attract") monster.align = 1;
            }
            if (monster.set_kind && now_ms >= monster.set_until) monster.set_kind = 0;   // event 10 (0x5a7f70 -> FUN_00573120)
        }
    }

auto Fight::charge(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void { charge(actor->charges, skill, skill_level ? skill_level(skill.id) : 1, now_ms); }

// A charge on `held` (the player's, or a Shadow's), at most 3, auralen on.
auto Fight::charge(std::vector<Charge>& held, const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms) -> void {
        auto found = std::ranges::find(held, skill.id, &Charge::skill);
        if (found == held.end()) found = held.insert(held.end(), Charge{ skill.id });
        found->level = std::max(found->level, lvl);
        found->count = std::min(found->count + 1, 3);
        const auto env = calc_env();
        found->until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data->skills, skill.auralen, env, skill.id, lvl), 1)) * 40;
    }

auto Fight::add_charges(const std::vector<Charge>& held, d2d::rules::Fighter& fighter, d2d::rules::Swing& swing, bool player_levels) -> void {
        const auto env = calc_env();
        for (const auto& charge : held) {
            const auto* skill = game_data->skills.get(charge.skill);
            if (!skill) continue;
            const int lvl = std::max(charge.level, player_levels && skill_level ? skill_level(charge.skill) : 0);
            const auto bonus = d2d::rules::charge_bonus(game_data->skills, *skill, env, lvl, charge.count);
            swing.ed_pct += bonus.ed_pct;
            fighter.life_steal += bonus.life_steal;
            fighter.mana_steal += bonus.mana_steal;
            if (bonus.etype >= 0 && bonus.etype < 5) {
                auto& [low, high] = fighter.elem[std::size_t(bonus.etype)];
                low += bonus.elem_lo; high += bonus.elem_hi;
                if (bonus.etype == 2) fighter.cold_len = std::max(fighter.cold_len, bonus.elem_len);
            }
        }
    }

auto Fight::row_damage(const GameData::MissileInfo& missile_info, int lvl, const d2d::rules::CalcEnv& env, int skill_id) const -> d2d::rules::MissileDamage {
        auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        if (damage.etype >= 0 && lvl >= 1 && !missile_info.edmg_sym.empty()) {   // FUN_0064b100 / FUN_0064b1d0
            damage.elo = missile_elem(*game_data, missile_info, env, skill_id, lvl, false);
            damage.ehi = std::max(missile_elem(*game_data, missile_info, env, skill_id, lvl, true), damage.elo);
        }
        damage.srcdam = missile_info.src_damage;
        return damage;
    }

auto Fight::release(std::vector<Charge>& held, std::size_t monster_index, std::uint32_t now_ms, bool player_levels) -> void {
        const float target_x = monsters[monster_index].unit.x, target_y = monsters[monster_index].unit.y;
        const auto released = held;
        held.clear();
        for (const auto& charge : released) {
            const auto* skill = game_data->skills.get(charge.skill);
            if (!skill) continue;
            const int lvl = std::max(charge.level, player_levels && skill_level ? skill_level(skill->id) : 0);
            const int count = std::clamp(charge.count, 1, 3);
            for (int k = skill->prgstack ? 1 : count; k <= count; ++k) prg(*skill, skill->prgfunc[std::size_t(k - 1)], k, lvl, target_x, target_y, now_ms);
        }
    }

auto Fight::prg(const d2d::rules::Skill& skill, ServerDoFunction func, int count, int lvl, float target_x, float target_y, std::uint32_t now_ms) -> void {
        if (int(func) <= 0) return;
        const auto env = calc_env();
        const auto& mname = count <= 1 ? skill.srvmissilea : count == 2 ? skill.srvmissileb : skill.srvmissilec;
        const auto mit = game_data->missiles.find(mname);
        const GameData::MissileInfo* missile_info = mit == game_data->missiles.end() ? nullptr : &mit->second;
        int radius = d2d::rules::eval_calc(game_data->skills, skill.prgcalc[std::size_t(count - 1)], env, skill.id, lvl);
        if (radius == 0) radius = d2d::rules::eval_calc(game_data->skills, skill.prgcalc[0], env, skill.id, lvl);
        if (func == ServerDoFunction::kReleaseAreaHit) {
            d2d::rules::MissileDamage damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
            damage.srcdam = 0;
            const std::array<int, 4> pierce{ int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) };
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - target_x, monsters[i].unit.y - target_y) * 5 <= float(std::max(radius, 1)))
                    land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms);
            return;
        }
        if ((func == ServerDoFunction::kReleaseNova || func == ServerDoFunction::kReleaseScatter) && missile_info) {
            auto put = [&](float x, float y, float velocity_x, float velocity_y, int range) {
                Missile missile{ missile_info, x, y, velocity_x, velocity_y, missile_heading(velocity_x, velocity_y), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
                missile.friendly = true; missile.caster = actor->id; missile.skill = skill.id; missile.level = lvl;
                missiles.push_back(std::move(missile));
                return missiles.size() - 1;
            };
            if (func == ServerDoFunction::kReleaseNova) {
                const auto shared = std::make_shared<std::vector<int>>();
                const float speed = missile_speed(*missile_info, lvl);
                const int range = missile_info->range + missile_info->lev_range * lvl + d2d::rules::eval_calc(game_data->skills, skill.calc[0], env, skill.id, lvl);
                for (int k = 0; k < 64; ++k) {
                    const float angle = float(k) * 2 * 3.14159265f / 64;
                    missiles[put(target_x, target_y, std::cos(angle) * speed, std::sin(angle) * speed, range)].struck = shared;
                }
            } else {
                for (int k = 0; k < radius * radius; ++k) {
                    const int offset_x = radius - int(rng(2 * radius + 1)), offset_y = radius - int(rng(2 * radius + 1));
                    if (offset_x * offset_x + offset_y * offset_y > radius * radius) continue;
                    const float x = target_x + float(offset_x) / 5, y = target_y + float(offset_y) / 5;
                    if (!level->blocked(x, y, 0x04)) put(x, y, 0, 0, missile_info->range + missile_info->lev_range * lvl);
                }
            }
            return;
        }
        // 40 (FUN_005d5010, Royal Strike's first): the count's missile standing
        // at the target (FUN_0056ede0: its meteor). 41 (FUN_005d5080, its
        // third): prgcalc of them from the target toward random points
        // round it. 37 / 143 (FUN_005d4e70 -> FUN_005d4150, FUN_005d4f40 ->
        // FUN_005d4870: Claws of Thunder's third, Royal Strike's second):
        // a scan of 64 directions (0x100 / 4) for the units within prgcalc
        // (else aurarange) subtiles, a missile at each carrying
        // FUN_004efc20 + 1 hops.
        // ponytail: the scan's pick (FUN_005d40f0 / FUN_005d4680) and the
        // hop count read as: every monster in reach, the level's hops; 41's
        // spread as ±20 subtiles.
        if (!missile_info) return;
        if (func == ServerDoFunction::kReleaseAtTarget) { launch(*missile_info, skill, lvl, target_x, target_y, 0, 0, missile_info->range, now_ms); return; }
        if (func == ServerDoFunction::kReleaseSpread) {
            for (int k = 0; k < std::max(radius, 1); ++k) {
                const float dx = float(int(rng(41)) - 20) / 5, dy = float(int(rng(41)) - 20) / 5;
                launch(*missile_info, skill, lvl, target_x, target_y, dx, dy, missile_info->range + missile_info->lev_range * lvl, now_ms);
            }
            return;
        }
        if (func == ServerDoFunction::kReleaseMissileAtUnits || func == ServerDoFunction::kReleaseMissileAtUnitsSecondScan) {
            const int reach = radius > 0 ? radius : d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - target_x, monsters[i].unit.y - target_y) * 5 <= float(std::max(reach, 1)))
                    launch(*missile_info, skill, lvl, target_x, target_y, monsters[i].unit.x - target_x + 0.01f, monsters[i].unit.y - target_y, missile_info->range + missile_info->lev_range * lvl, now_ms).hops = lvl + 1;
            return;
        }
        if (std::ranges::find(told, -1000 - int(func)) != told.end()) return;
        told.push_back(-1000 - int(func));
        d2d::log::info("not implemented: {}'s release srvprgfunc {} ({} charges)", skill.name, int(func), count);
    }

auto Fight::dragon_tail(const d2d::rules::Skill& skill, std::size_t target, int phys, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const int fire = phys * d2d::rules::eval_calc(game_data->skills, skill.calc[0], env, skill.id, lvl) / 100;
        const float radius = float(d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl));
        const float center_x = monsters[target].unit.x, center_y = monsters[target].unit.y;
        for (std::size_t i = 0; i < monsters.size() && fire > 0; ++i) {
            auto& monster = monsters[i];
            if (!monster.targetable() || std::hypot(monster.unit.x - center_x, monster.unit.y - center_y) > radius) continue;
            if (hurt(*game_data, monster, d2d::rules::resisted(fire, target_of(i).res[2]), now_ms)) killed(i, now_ms);
        }
    }

auto Fight::boss_events(std::span<Foe> foes, std::uint32_t now_ms) -> void {
        using d2d::rules::Boss;
        for (auto& monster : monsters) {
            const bool boss = monster.boss == Boss::unique || monster.boss == Boss::superunique;
            const auto has = [&](int id) { return boss && std::ranges::contains(monster.mods, id); };
            const auto family = d2d::rules::family_mods(monster.type >= 0 ? game_data->monsters.types[std::size_t(monster.type)].base : -1);
            const bool scarab = std::ranges::contains(family, d2d::rules::umod::scarab), mummy = std::ranges::contains(family, d2d::rules::umod::poisondead);
            // Suicideminion (umod 33, every BaseId 0x1cd by FUN_005b21b0, a whipped
            // minion by its sumumod): hook 1 (FUN_005a3e70) puts it from GH
            // straight into death (FUN_005a7e60 mode 0: no credit); dead, its
            // event 7 four frames on is the blast (FUN_005a3ef0).
            const bool suicide = std::ranges::contains(family, d2d::rules::umod::suicideminion) || std::ranges::contains(monster.mods, d2d::rules::umod::suicideminion);
            if (suicide && monster.alive() && monster.mode == "GH") { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); }
            const int mlvl = std::max(monster.stats.level, 1);
            // Scarab (FUN_005a2eb0 / FUN_005a2ee0): into GH or DT, 2 frames on,
            // 8 buglightning (4 ways x 2) at its level, steered as lightning enchanted's.
            if (scarab && monster.mode != monster.fx_mode && (monster.mode == "GH" || monster.mode == "DT")) monster.scarab_due.push_back(now_ms + 2 * 40);
            monster.fx_mode = monster.mode;
            while (!monster.scarab_due.empty() && now_ms >= monster.scarab_due.front()) {
                monster.scarab_due.erase(monster.scarab_due.begin());
                d2d::log::info("{} lets off its bugs' lightning", monster.npc.name);
                const auto ring = std::make_shared<std::vector<int>>();
                for (const auto& [dir_x, dir_y] : kCompass)
                    for (int k = 0; k < 2; ++k) {
                        const float angle = float(rng(51) - 25) * 3.14159265f / 180, cosine = std::cos(angle), sine = std::sin(angle);
                        boss_missile(monster, "buglightning", dir_x * cosine - dir_y * sine, dir_x * sine + dir_y * cosine, ring, now_ms, mlvl);
                    }
            }
            if (monster.alive()) {
                if (has(d2d::rules::umod::lightning) && monster.hit_points < monster.last_hp && now_ms - monster.bolts_at >= 10 * 40) {
                    monster.bolts_at = now_ms;
                    d2d::log::info("{} lets off charged bolts", monster.npc.name);
                    const auto ring = std::make_shared<std::vector<int>>();
                    for (const auto& [dir_x, dir_y] : kCompass)
                        for (int k = 0; k < 2; ++k) {
                            const float angle = float(rng(51) - 25) * 3.14159265f / 180, cosine = std::cos(angle), sine = std::sin(angle);
                            boss_missile(monster, "lightunique", dir_x * cosine - dir_y * sine, dir_x * sine + dir_y * cosine, ring, now_ms);
                        }
                }
            } else if ((boss || mummy || suicide) && !monster.fx_done) {
                if (!monster.fx_at) monster.fx_at = now_ms + 4 * 40;
                else if (now_ms >= monster.fx_at) {
                    monster.fx_done = true;
                    // Poisondead (FUN_005a3800 / FUN_005a2c20): corpsepoisoncloud (155) on its spot at its level.
                    // ponytail: poisons each foe within 0.4 cells once; srvdofunc 3's own area and ticks untraced.
                    if (mummy) {
                        d2d::log::info("{} leaves a poison cloud", monster.npc.name);
                        boss_missile(monster, "corpsepoisoncloud", 0, 0, std::make_shared<std::vector<int>>(), now_ms, mlvl);
                    }
                    if (has(d2d::rules::umod::fire)) fire_blast(monster, foes, now_ms);
                    if (suicide) suicide_blast(monster, foes, now_ms);
                    if (has(d2d::rules::umod::cold)) {
                        const auto ring = std::make_shared<std::vector<int>>();
                        for (int k = 0; k < 64; ++k) {
                            const float angle = float(k) * 6.2831853f / 64;
                            boss_missile(monster, "coldunique", std::cos(angle), std::sin(angle), ring, now_ms);
                        }
                    }
                }
            }
            monster.last_hp = monster.hit_points;
        }
    }

auto Fight::boss_missile(const Monster& monster, const char* name, float dx, float dy, const std::shared_ptr<std::vector<int>>& ring, std::uint32_t now_ms, int missile_level) -> void {
        const auto found = game_data->missiles.find(name);
        if (found == game_data->missiles.end()) return;
        const auto& missile_info = found->second;
        const int lvl = missile_level > 0 ? missile_level : std::max(monster.stats.level / 2, 1);   // the enchanted mods' mlvl / 2 by default
        const auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        d2d::rules::MonStats stats;
        stats.level = monster.stats.level;
        stats.to_hit = 1 << 20;                                     // ToHit 0: always hits
        // Poison (3) is kept as monster poison is: x 10 makes it 256ths a frame
        // (combat.hpp, FUN_005a4f50's stats 0x39 / 0x3a); the rest in points.
        if (damage.etype == 3) stats.elements[0] = { damage.etype, 100, damage.elo / 10, std::max(damage.ehi / 10, 1), damage.elen, "A2" };
        else if (damage.etype >= 0) stats.elements[0] = { damage.etype, 100, damage.elo >> 8, std::max(damage.ehi >> 8, 1), damage.elen, "A2" };
        const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(dx, dy), 0.01f);
        Missile x{ &missile_info, monster.unit.x, monster.unit.y, dx / distance * speed, dy / distance * speed, missile_heading(dx, dy), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats };
        x.struck = ring; x.level = lvl; x.owner = monster.id;
        pending.push_back(std::move(x));
    }

// Umod 33's blast (FUN_005a3ef0): its A1 damage (FUN_006538a0 kind 8:
// MonLvl's A1MinD / A1MaxD), min + rand(max - min) on its seed, as physical
// and, by its aip1 (MonStats +0x56), fire (1) or cold for aip1 frames (2
// up); suicidecorpseexplode (426) on its spot (FUN_0056ecb0); everyone
// within its aip4 (+0x68) subtiles (FUN_0057e090, flags 0x581).
// ponytail: FUN_0057e090's own shape is the fire blast's circle.
auto Fight::suicide_blast(Monster& monster, std::span<Foe> foes, std::uint32_t now_ms) -> void {
        const auto& aip = game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))].aip;
        const int low = monster.stats.a1_min, high = monster.stats.a1_max;
        const int pts = high > low ? low + monster.seed(high - low) : low;
        const int element = aip[0];
        const float radius = float(aip[3]) / 5;
        for (auto& foe : foes) {
            if (!foe.alive || std::hypot(foe.x - monster.unit.x, foe.y - monster.unit.y) > radius) continue;
            d2d::rules::Taken taken;
            taken.hit = true;
            taken.damage = std::max(pts * (100 - foe.fighter.dr_pct) / 100 - foe.fighter.dr_flat, 0);
            if (element == 1) taken.damage += d2d::rules::resisted(pts, foe.fighter.res[0]);
            else if (element >= 2) {
                const int cold = d2d::rules::resisted(pts, foe.fighter.res[2]);
                taken.damage += cold;
                if (cold > 0) taken.chill_ticks = d2d::rules::resisted(element, foe.fighter.res[2]);
            }
            foe.take(taken);
        }
        if (const auto found = game_data->missiles.find("suicidecorpseexplode"); found != game_data->missiles.end()) {
            Missile x{ &found->second, monster.unit.x, monster.unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(found->second.range, 1)) * 40, {} };
            x.visual_only = true;
            pending.push_back(std::move(x));
        }
        d2d::log::info("{} blows up ({} physical{})", monster.npc.name, pts, element == 1 ? " + fire" : element >= 2 ? " + cold" : "");
    }

auto Fight::fire_blast(const Monster& monster, std::span<Foe> foes, std::uint32_t now_ms) -> void {
        const auto [low, high] = d2d::rules::fire_blast(monster.stats.hit_points, monster.difficulty);
        const int pts = (low + rng(std::max(high - low, 1))) * 64 / 256;
        const float radius = float(monster.difficulty + 4) / 5;
        for (auto& foe : foes) {
            if (!foe.alive || std::hypot(foe.x - monster.unit.x, foe.y - monster.unit.y) > radius) continue;
            d2d::rules::Taken taken;
            taken.hit = true;
            taken.damage = std::max(pts * (100 - foe.fighter.dr_pct) / 100 - foe.fighter.dr_flat, 0) + d2d::rules::resisted(pts, foe.fighter.res[0]);
            foe.take(taken);
        }
        if (const auto found = game_data->missiles.find("monstercorpseexplode"); found != game_data->missiles.end()) {
            Missile x{ &found->second, monster.unit.x, monster.unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(found->second.range, 1)) * 40, {} };
            x.visual_only = true;
            pending.push_back(std::move(x));
        }
        d2d::log::info("{} explodes ({} fire, {} physical)", monster.npc.name, pts, pts);
    }

auto Fight::killed(std::size_t monster_index, std::uint32_t now_ms, bool credit) -> void {
        auto& monster = monsters[monster_index];
        const auto [players, nearby] = drop_counts();
        if (!credit) {                                      // a monster's kill (FUN_0057e7b0: no player behind it)
            if (!monster.no_drop) loot.drop(monster, spawning.game, now_ms, players, nearby);
            kills.push_back({ monster.type, monster.unit.x, monster.unit.y, monster.stats.level, monster.super, monster.id, 0 });
            return;
        }
        // Who gets the experience (FUN_0057e990): alone, the killer; in a
        // party (local co-op: everyone), its members alive within 80
        // subtiles of the dead (dx² + dy² <= 0x1900, FUN_0057e5a0) share it
        // by level (rules::party_shares) — one of them leaves it all the
        // killer's, none nobody's.
        Hero& killer = *actor;
        if (!monster.no_exp) merc_share(monster);
        if (monster.no_exp) {
        } else if (heroes.size() == 1) {
            gain_exp(monster.stats.exp, monster, now_ms);
        } else {
            std::vector<Hero*> members;
            std::vector<int> levels;
            const auto subtile = [](float value) { return int(std::floor(value * 5)); };
            for (Hero* hero : heroes) {
                act_as(*hero);
                const int dx = subtile(monster.unit.x) - subtile(hero->player.x), dy = subtile(monster.unit.y) - subtile(hero->player.y);
                if (dead() || std::uint32_t(dx * dx + dy * dy) > 0x1900u || members.size() >= 8) continue;
                members.push_back(hero);
                levels.push_back(int(hero->character.stats.get(d2d::d2s::kLevel)));
            }
            if (members.size() == 1) members = { &killer };
            const auto shares = d2d::rules::party_shares(monster.stats.exp, levels);
            for (std::size_t i = 0; i < members.size(); ++i) {
                act_as(*members[i]);
                gain_exp(shares[i], monster, now_ms);
            }
            act_as(killer);
        }
        if (!monster.no_drop) {
            const auto merc_sum = merc_blow ? merc_gear().sum : d2d::rules::StatSum{};
            loot.drop(monster, spawning.game, now_ms, players, nearby, int(merc_sum[std::size_t(d2d::d2s::kMagicFind)]), int(merc_sum[std::size_t(d2d::d2s::kGoldFind)]));
        }
        kills.push_back({ monster.type, monster.unit.x, monster.unit.y, monster.stats.level, monster.super, monster.id, killer.id });
    }

auto Fight::merc_share(const Monster& monster) -> void {
        auto& header = actor->character.header;
        if (!actor->merc || !actor->merc->alive() || monster.stats.exp <= 0) return;
        const int merc_level = actor->merc_st.level;
        auto award = d2d::rules::kill_award(int(std::min<std::int64_t>(monster.stats.exp, 0x7fffffff)), merc_level, monster.stats.level,
                                            int(merc_gear().sum[std::size_t(d2d::d2s::kAddExperience)]),
                                            game_data->exp_ratio, game_data->exp_ratio_shift, game_data->max_level);
        if (const auto* row = d2d::rules::hireling_row(game_data->rules, header.merc_type, merc_level)) {   // FUN_0057e3f0: a level's span / 64 at most
            const auto need = [&](std::int64_t at_level) { return std::uint32_t((at_level + 1) * at_level * at_level * row->exp_per_level); };
            award = std::min<std::int64_t>(award, (need(merc_level + 1) - need(merc_level)) >> 6);
        }
        header.merc_exp = d2d::rules::merc_kill_exp(header.merc_exp, award, merc_blow, merc_level,
                                                    int(actor->character.stats.get(d2d::d2s::kLevel)));
        merc_level_stats();
        if (actor->merc_st.level > merc_level) d2d::log::info("{}'s mercenary reaches level {}", actor->character.name, actor->merc_st.level);
    }

auto Fight::drop_counts() -> std::pair<int, int> {
        if (heroes.size() == 1) return { 1, 1 };
        Hero& was = *actor;
        int alive = 0;
        for (Hero* hero : heroes) { act_as(*hero); alive += dead() ? 0 : 1; }
        act_as(was);
        return { int(heroes.size()), std::max(alive, 1) };
    }

auto Fight::gain_exp(std::int64_t share, const Monster& monster, std::uint32_t now_ms) -> void {
        const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
        const auto exp = d2d::rules::kill_award(int(std::min<std::int64_t>(share, 0x7fffffff)), int(actor->character.stats.get(d2d::d2s::kLevel)), monster.stats.level,
                                                int(actor->psum[d2d::d2s::kAddExperience]), game_data->exp_ratio, game_data->exp_ratio_shift, game_data->max_level);   // item_addexperience: the experience shrine
        const int levels_gained = d2d::rules::gain_exp(actor->character.stats, exp, game_data->exp_next, game_data->class_gains[save_class]);
        d2d::log::info("{} killed {} (+{} exp){}", actor->character.name, monster.npc.name, exp, levels_gained ? std::format(", level {}", actor->character.stats.get(d2d::d2s::kLevel)) : "");
        if (levels_gained) {
            actor->character.panel = panel_stats(*game_data, actor->character.header, actor->character.items, actor->character.stats);
            // The server's unit sound event 2 (FUN_00570880 → FUN_00553380),
            // sent to the player alone as S→C 0x2C; the client plays
            // Sounds.txt 7, cursor_level_up, not placed (FUN_004cb9c0).
            cues.cue(d2d::rules::sound_ids::kCursorLevelUp, now_ms, actor->player.x, actor->player.y);
        }
    }

auto Fight::merc_strike(std::size_t target, const d2d::rules::Swing& swing, std::uint32_t now_ms) -> void {
        const auto fighter = merc_fighter();
        const int thorns = monsters[target].props.thorns;
        const auto blow = d2d::rules::player_blow(fighter, target_of(target), actor->merc_st.level, rng, swing);
        merc_blow = true;
        const bool landed = land(target, blow, false, now_ms);
        merc_blow = false;
        // A MonProp thorns monster's flat value back on the merc, a melee
        // striker as the player is (FUN_005bf670), through its damage reduction.
        if (landed && thorns > 0) actor->merc_hurt += std::max(thorns * (100 - fighter.dr_pct) / 100 - fighter.dr_flat, 0);
    }

// A MonProp thorns monster (stat 78, damagedinmelee func 6, FUN_005bf670):
// its flat value back on the player's melee blow as physical damage, through
// the striker's damage reduction (FUN_0057c6c0 -> FUN_0057c1e0, as a
// monster's blow). The main strike and Dragon Flight's kick. FUN_0057c1e0
// reads the unit's full stats, so a kick's thorns go through the weapon's
// damage reduction too: player_combat, not pf_kick.
auto Fight::player_thorns(std::size_t target, const d2d::rules::Blow& blow) -> void {
        const auto& hero = actor->player_combat;
        if (const int thorns = monsters[target].props.thorns; blow.hit && thorns > 0)
            actor->self_hurt += std::int64_t(std::max(thorns * (100 - hero.dr_pct) / 100 - hero.dr_flat, 0)) << 8;
}

auto Fight::pet_strike(Pet& pet, std::size_t target, const d2d::rules::Blow& struck, std::uint32_t now_ms) -> void {
        const int thorns = monsters[target].props.thorns;
        auto blow = struck;
        if (const int percent = pet_buff_stat(pet, d2d::d2s::kDamagePercent); percent != 0 && blow.hit)   // a curse's (Weaken) on its physical part
            blow.damage = std::max(blow.damage + d2d::rules::resisted(blow.phys * percent / 100, target_of(target).res[0]), 0);
        const bool landed = land(target, blow, false, now_ms);
        // A MonProp thorns monster's flat value back on the pet (FUN_005bf670),
        // a monster striker: through its own damage resist, as a monster's hit on a monster.
        if (landed && thorns > 0) pet_hurt(pet, d2d::rules::resisted(thorns, pet.monster.target(*game_data).res[0]), now_ms);
    }

auto Fight::merc_fighter() const -> d2d::rules::Fighter {
        using namespace d2d::d2s;
        // Its stats as a unit's (FUN_00572840's base, its worn items'
        // props, sockets and set bonuses summed, as a player's are), read as
        // game.exe reads any unit's:
        // - damage (FUN_0057b420): a two-handed weapon reads stats 0x17 /
        //   0x18, where the level-up put its base damage; a one-handed one or
        //   none 0x15 / 0x16, which it doesn't (1 / 2 at least); the weapon's
        //   own damage and enhanced damage as make_fighter has them;
        // - attack rating (combat.md, a monster's): stat 19 + dex x 5,
        //   x (100 + stat 119) / 100;
        // - defence (FUN_006223f0): stat 31 (its base, its items' own
        //   defence after their enhanced defence) + dex / 4, + skill_armor_percent
        //   (Frozen Armor);
        // - resistances: its base + its items', less the difficulty's penalty
        //   (FUN_0057be00 takes it off a player or a hireling: ctx +0x14 0),
        //   DifficultyLevels ResistPenalty in LoD, -100 .. 75 + max-resist
        //   (95 at most).
        // - block (FUN_00622720 for a monster): stat 20 (MonStats ToBlock,
        //   none for a hireling, its shield's block and its items' to-block),
        //   75 at most, only with a shield (FUN_006225f0: never for act3hire,
        //   0x167; no other merc can hold one).
        const auto gear = merc_gear();
        auto sum = gear.sum;
        const auto& weapon_sum = gear.weapon_sum;
        const Item *weapon = gear.weapon, *shield = gear.shield;
        const std::int64_t item_def = gear.item_def, per_level = gear.per_level;
        const auto weapon_info = weapon ? game_data->rules.item_info.find(weapon->code) : game_data->rules.item_info.end();
        if (weapon_info != game_data->rules.item_info.end() && weapon_info->second.two_handed) {
            sum[kMinDamage] += actor->merc_st.dmg_min;                           // stats 0x17 / 0x18
            sum[kMaxDamage] += actor->merc_st.dmg_max;
        }
        sum[kToHit] += actor->merc_st.attack_rating;
        // A monster's curse on it (Amplify Damage, Weaken): its aurastats
        // join its stats (merc_foe drops a spent one).
        if (const auto* skill = actor->curses[1].skill >= 0 ? game_data->skills.get(actor->curses[1].skill) : nullptr) {
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, nullptr };
            for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                if (skill->aurastat[k] >= 0 && std::size_t(skill->aurastat[k]) < sum.size())
                    sum[std::size_t(skill->aurastat[k])] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, actor->curses[1].level);
        }
        Stats stats;
        stats.values[kLevel] = actor->merc_st.level;
        stats.values[kStr] = actor->merc_st.str + sum[kStr];
        stats.values[kDex] = actor->merc_st.dex + sum[kDex];
        d2d::rules::ClassGains gains;
        gains.to_hit = 35;                                                // make_fighter's (dex - 7) x 5: a monster's dex x 5
        std::int64_t defense = actor->merc_st.def + item_def + sum[kArmorClass] + per_level * actor->merc_st.level / 8 + stats.values[kDex] / 4;
        if (const auto* skill = actor->merc_buff.skill >= 0 ? game_data->skills.get(actor->merc_buff.skill) : nullptr) {
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, nullptr };
            for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                if (skill->aurastat[k] == kSkillArmorPercent)
                    defense += defense * d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, actor->merc_buff.level) / 100;
        }
        defense += defense * merc_cry_stat(kSkillArmorPercent) / 100;   // Shout's
        const int difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        const auto penalty = actor->character.header.expansion() ? game_data->resist_penalty[std::size_t(difficulty)] : std::array<std::int64_t, 3>{ 0, -20, -50 }[std::size_t(difficulty)];
        std::array<int, 4> res{};
        constexpr std::array<int, 4> kResStat{ 39, 41, 43, 45 };          // fire, lightning, cold, poison (+1: its max)
        for (std::size_t k = 0; k < 4; ++k)
            res[k] = int(std::clamp<std::int64_t>(actor->merc_st.resist + sum[std::size_t(kResStat[k])] + penalty, -100, std::min<std::int64_t>(75 + sum[std::size_t(kResStat[k] + 1)], 95)));
        auto fighter = d2d::rules::make_fighter(game_data->rules, weapon, shield, sum, weapon_sum, stats, gains, int(defense), res);
        fighter.block = 0;
        if (const auto found = shield && actor->merc_npc && actor->merc_npc->hc_idx != 0x167 ? game_data->rules.item_base.find(shield->code) : game_data->rules.item_base.end();
            found != game_data->rules.item_base.end())
            fighter.block = int(std::clamp<std::int64_t>(found->second.block + sum[kToBlock], 0, 75));
        return fighter;
    }

auto Fight::merc_gear() const -> MercGear {
        using namespace d2d::d2s;
        MercGear gear;
        auto add = [](d2d::rules::StatSum& into, const std::vector<ItemProp>& props) {
            for (const auto& prop : props) if (prop.stat >= 0 && std::size_t(prop.stat) < into.size()) into[std::size_t(prop.stat)] += prop.value;
        };
        for (const auto& item : actor->character.merc_items) {
            if (item.location != item_location::kEquipped) continue;
            add(gear.sum, item.props);
            for (const auto& socketed : item.socketed_items) add(gear.sum, socket_props(*game_data, item, socketed));
            add(gear.sum, set_bonus_props(*game_data, actor->character.merc_items, item));
            std::int64_t enhanced_defense = 0;
            for (const auto& prop : item.props) {
                if (prop.stat == kArmorPercent) enhanced_defense += prop.value;
                if (prop.stat == kArmorPerLevel) gear.per_level += prop.value;
            }
            if (item.defense > 0) gear.item_def += item.defense * (100 + enhanced_defense) / 100;
            if (item.slot != body_location::kRightArm && item.slot != body_location::kLeftArm) continue;
            const auto info = game_data->rules.item_info.find(item.code);
            const auto found = game_data->rules.item_base.find(item.code);
            if (info == game_data->rules.item_info.end() || found == game_data->rules.item_base.end()) continue;
            if (info->second.kind == 2 && (!gear.weapon || item.slot == body_location::kRightArm)) gear.weapon = &item;
            if (info->second.kind == 1 && found->second.block > 0) gear.shield = &item;
        }
        if (gear.weapon) {
            add(gear.weapon_sum, gear.weapon->props);
            for (const auto& socketed : gear.weapon->socketed_items) add(gear.weapon_sum, socket_props(*game_data, *gear.weapon, socketed));
        }
        return gear;
    }

auto Fight::merc_cry_stat(int stat) const -> int {
        const auto* skill = actor->merc_cry.skill >= 0 ? game_data->skills.get(actor->merc_cry.skill) : nullptr;
        if (!skill) return 0;
        const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, nullptr };
        int sum = 0;
        for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
            if (skill->aurastat[k] == stat) sum += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, actor->merc_cry.level);
        return sum;
    }

auto Fight::merc_max_life() const -> int {
        // Its level-up life (stat 7) + its items' maxhp and hp per level (216,
        // an eighth a level), the items' maxhp percent (76) on the level-up's.
        // ponytail: vitality's share isn't given (a hireling has no CharStats
        // row); its items' mana goes nowhere (d2d keeps no merc mana).
        const auto gear = merc_gear();
        return int(std::max<std::int64_t>(actor->merc_st.life + gear.sum[d2d::d2s::kMaxLife] + gear.sum[216] * actor->merc_st.level / 8
                                              + std::int64_t(actor->merc_st.life) * (gear.sum[d2d::d2s::kMaxLifePercent] + merc_cry_stat(d2d::d2s::kMaxLifePercent)) / 100, 1));
    }

// A think's move as a path by its type (FUN_00649970) from subtile
// (from_x, from_y): the cells to walk, empty when none.
namespace {
auto move_route(const Level& level, const UnitState& unit, int from_x, int from_y, const d2d::rules::MercMove& move, const Crowd& crowd)
    -> std::vector<std::pair<float, float>> {
    auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    auto blocked = [&](int x, int y) { return level.unit_blocked(centre(x), centre(y), unit.shape) || crowd.at(centre(x), centre(y), &unit); };
    const int steps = move.steps ? move.steps : 5;
    std::vector<std::pair<int, int>> points;
    if (move.type == 7) points = d2d::rules::player_path(from_x, from_y, move.x, move.y, 0, false, blocked, steps);
    else if (move.type == 1) points = d2d::rules::search_path(from_x, from_y, move.x, move.y, false, blocked);
    else if (move.type == 0xf) points = d2d::rules::wall_path(from_x, from_y, move.x, move.y, steps, false, blocked);
    else points = d2d::rules::monster_path(from_x, from_y, move.x, move.y, false, blocked, steps);
    std::vector<std::pair<float, float>> route;
    std::pair last{ from_x, from_y };
    for (const auto& point : points)
        if (point != last) { route.emplace_back(centre(point.first), centre(point.second)); last = point; }
    return route;
}
// A unit put at the free spot nearest (x, y) cells (FUN_00554ea0 ->
// FUN_0064e7b0, its size, mask 0x1c09); none: it stays.
void put_near(const Level& level, const Crowd& crowd, UnitState& unit, float x, float y) {
    if (const auto spot = free_spot_near(level, crowd, &unit, int(std::floor(x * 5)), int(std::floor(y * 5)), unit.shape.size, 0x1c09)) {
        unit.x = (float(spot->first) + 0.5f) / 5; unit.y = (float(spot->second) + 0.5f) / 5;
    }
}
// A pet's or the merc's think mode 3 (FUN_005e3930): a spot in the owner's
// room (FUN_0054dc40: up to 20 tries inside its rect less a subtile each
// side, on the room's seed, each where MonStats class 0x16b fits:
// FUN_005b2f20 radius -1, test only, spawn_spot), then put_near it.
// False when no try fits.
bool room_teleport(const GameData& game_data, const Level& level, const Crowd& crowd, Spawning& spawning, UnitState& unit) {
    if (spawning.room_level != &level) return false;
    auto& in_play = spawning.levels[spawning.room_level];
    if (spawning.room < 0 || std::size_t(spawning.room) >= in_play.room_seeds.size() || std::size_t(spawning.room) >= level.rooms.size()) return false;
    const auto& room = level.rooms[std::size_t(spawning.room)];
    for (int tries = 0; tries < 20; ++tries) {
        auto& seed = in_play.room_seeds[std::size_t(spawning.room)];
        const int x = room.x * 5 + 1 + seed(room.width * 5 - 1), y = room.y * 5 + 1 + seed(room.height * 5 - 1);
        if (int fit_x = 0, fit_y = 0; !spawn_spot(game_data, level, crowd, &in_play, 0x16b, x, y, -1, spawning.room, fit_x, fit_y)) continue;
        put_near(level, crowd, unit, (float(x) + 0.5f) / 5, (float(y) + 0.5f) / 5);
        return true;
    }
    return false;
}
// A monster mode's number (MonMode: DT 0 .. RN 15); a MonSeq sequence
// (seq_swtigerfist) or Skills.txt monanim xx plays SQ (14).
int monster_mode(std::string_view token) {
    static constexpr std::array<std::string_view, 16> kMonModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
    const auto found = std::ranges::find(kMonModes, token);
    return found == kMonModes.end() ? 14 : int(found - kMonModes.begin());
}
// FUN_00645460: a skill's kind, Skills.txt range — none 0, h2h 1, rng 2,
// both 1 (2 with a missile weapon).
// ponytail: a Shadow with its owner's bow would count both as 2.
int skill_kind(const d2d::rules::Skill& skill) { return skill.range == "h2h" || skill.range == "both" ? 1 : skill.range == "rng" ? 2 : 0; }
// Skills.txt charclass as FUN_00645040's class (7: none).
int skill_class(const d2d::rules::Skill& skill) {
    static constexpr std::array<std::string_view, 7> kClasses{ "ama", "sor", "nec", "pal", "bar", "dru", "ass" };
    const auto found = std::ranges::find(kClasses, std::string_view(skill.cls));
    return found == kClasses.end() ? 7 : int(found - kClasses.begin());
}
// FUN_005eab20 by name: may a Shadow (MonStats row `type_info`, raised by
// `summoning`) have the skill? One that summons nothing, or neither this
// monster nor the Shadow's pettype.
bool shadow_may_have(const d2d::rules::Skill* skill, const d2d::rules::MonType& type_info, const d2d::rules::Skill& summoning) {
    if (!skill) return false;
    if (skill->summon.empty()) return true;
    if (skill->summon == type_info.id) return false;
    return skill->pettype.empty() || skill->pettype != summoning.pettype;
}
}  // namespace

auto Fight::merc_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd, bool town) -> void {
        auto& unit = actor->merc->unit;
        const auto set = [&](std::string_view mode) {
            actor->merc->mode = mode; unit.mode_ms = now_ms; unit.walking = mode == "WL" || mode == "RN";
            if (!unit.walking) unit.path.clear();
            // ponytail: the merc's FHR is its items' (merc_fighter); a state's stat 99 isn't kept, as a monster's.
            actor->merc->mode_until = mode == "NU" || unit.walking ? 0
                : mode == "GH"                                  ? now_ms + d2d::game::get_hit_ms(game_data->npc_timing(*actor->merc_npc, mode), merc_fighter().fhr)
                                                                : now_ms + game_data->npc_timing(*actor->merc_npc, mode).length_ms();
        };
        if (actor->merc->mode == "GH" || actor->merc->mode == "DT") actor->merc_seq = nullptr;   // struck or dying: the sequence is cut
        if (actor->merc_buff.skill >= 0 && now_ms >= actor->merc_buff.until) actor->merc_buff = {};
        if (actor->merc_cry.skill >= 0 && now_ms >= actor->merc_cry.until) { actor->merc_cry = {}; actor->merc->hit_points = std::min(actor->merc->hit_points, merc_max_life()); }
        if (actor->merc->mode == "DT") {
            if (now_ms >= actor->merc->mode_until) { actor->merc.reset(); actor->character.header.merc_dead = true; d2d::log::info("the merc died"); }
            return;
        }
        if (actor->merc->mode == "KB") { if (d2d::game::knock_slide(*actor->merc, now_ms)) return; set("GH"); }   // knocked back, then its get-hit (as a monster's)
        if (actor->merc->mode == "GH") { if (now_ms < actor->merc->mode_until) return; set("NU"); actor->merc->next_act = now_ms; }
        const bool archer = actor->merc_npc && actor->merc_npc->id == "roguehire";
        const float reach = archer ? 6.f : kMeleeReach;
        if (actor->merc_seq && actor->merc->mode != "NU" && actor->merc->mode != "WL" && actor->merc->mode != "RN") {   // an SQ skill: a hit at each event-1 frame
            const auto frame_ms = std::max<std::uint32_t>(game_data->npc_timing(*actor->merc_npc, actor->merc->mode).ms_per_frame(), 1);
            const int reached = std::min(int((now_ms - unit.mode_ms) / frame_ms), int(actor->merc_seq->size()) - 1);
            for (; actor->merc_seq_done <= reached; ++actor->merc_seq_done) {
                if ((*actor->merc_seq)[std::size_t(actor->merc_seq_done)].event != 1 || actor->merc_target < 0) continue;
                auto& target = monsters[std::size_t(actor->merc_target)];
                const auto* skill = actor->merc_skill >= 0 ? game_data->skills.get(actor->merc_skill) : nullptr;
                if (target.alive() && std::hypot(target.unit.x - unit.x, target.unit.y - unit.y) <= kMeleeReach + 0.3f) {
                    const auto swing_with = skill ? skill_swing(*skill, std::max(actor->merc_skill_level, 1), merc_calc_env(), false) : d2d::rules::Swing{};
                    merc_strike(std::size_t(actor->merc_target), swing_with, now_ms);
                }
            }
            if (now_ms < actor->merc->mode_until) return;
            actor->merc_seq = nullptr;
            set("NU");
            actor->merc->next_act = now_ms;
        }
        if (actor->merc->mode != "NU" && actor->merc->mode != "WL" && actor->merc->mode != "RN") {   // an attack: its skill (or blow) on the action frame
            if (!actor->merc->struck && actor->merc_target >= 0 && now_ms >= unit.mode_ms + game_data->npc_timing(*actor->merc_npc, actor->merc->mode).action_ms()) {
                actor->merc->struck = true;
                auto& target = monsters[std::size_t(actor->merc_target)];
                const auto* skill = actor->merc_skill >= 0 ? game_data->skills.get(actor->merc_skill) : nullptr;
                const Caster caster{ unit.x, unit.y, std::max(actor->merc_skill_level, 1), actor->merc_target, target.unit.x, target.unit.y, true };
                if (skill && skill->srvdofunc != ServerDoFunction::kAttack && missile_skill(*skill)) fire(*skill, now_ms, caster);
                else if (skill && spot_skill(*skill)) spot(*skill, now_ms, caster);
                else if (skill && self_cast(*skill)) {                    // Frozen Armor: its state on the merc for auralen frames
                    const int length = d2d::rules::eval_calc(game_data->skills, skill->auralen, merc_calc_env(), skill->id, caster.level);
                    actor->merc_buff = { skill->id, caster.level, length > 0 ? now_ms + std::uint32_t(length) * 40 : ~0u };
                }
                else {
                const float dx = target.unit.x - unit.x, dy = target.unit.y - unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                if (!skill && archer && game_data->missiles.contains("arrow")) {
                    const auto& missile_info = game_data->missiles.at("arrow");
                    const float speed = missile_speed(missile_info, actor->merc_st.level);
                    Missile missile{ &missile_info, unit.x, unit.y, dx / distance * speed, dy / distance * speed, missile_facing(unit.x, unit.y, dx, dy), now_ms, now_ms + std::uint32_t(missile_info.range) * 40, {} };
                    missile.to = { target.unit.x, target.unit.y };
                    const auto fighter = merc_fighter();                    // its bow's damage and attack rating with its gear
                    missile.min = fighter.min; missile.max = fighter.max; missile.attack_rating = fighter.attack_rating; missile.level = actor->merc_st.level; missile.friendly = true; missile.caster = actor->id; missile.by_merc = true;
                    missiles.push_back(std::move(missile));
                } else if (target.alive() && distance <= kMeleeReach + 0.3f) {
                    // A melee skill (Jab, Bash, Stun) builds its hit as the
                    // player's do (skill_swing) at the merc's skill level.
                    // ponytail: Jab's SQ hits come as one.
                    const auto swing_with = skill ? skill_swing(*skill, std::max(actor->merc_skill_level, 1), merc_calc_env(), false) : d2d::rules::Swing{};
                    merc_strike(std::size_t(actor->merc_target), swing_with, now_ms);
                }
                }
            }
            if (now_ms < actor->merc->mode_until) return;
            set("NU");
            actor->merc->next_act = now_ms;
        }
        const auto* type_info = actor->merc->type >= 0 ? &game_data->monsters.types[std::size_t(actor->merc->type)] : nullptr;
        if ((actor->merc->mode == "WL" || actor->merc->mode == "RN") && actor->merc_chase) {       // at a foe: in reach (or it's gone), think again
            const bool gone = actor->merc_target < 0 || !monsters[std::size_t(actor->merc_target)].alive();
            if (gone || std::hypot(monsters[std::size_t(actor->merc_target)].unit.x - unit.x, monsters[std::size_t(actor->merc_target)].unit.y - unit.y) <= reach) {
                set("NU");
                actor->merc_chase = false;
                actor->merc->next_act = now_ms;
            }
        }
        if (actor->merc->mode == "WL" || actor->merc->mode == "RN") {                    // velocitypercent: the move's pace
            const int velocity = type_info ? (actor->merc->mode == "RN" ? type_info->run : type_info->velocity) : 6;
            if (walk_on(*level, unit, cells_per_sec(float(velocity)) * float(std::max(100 + actor->merc->move_pct, 10)) / 100 * elapsed, crowd)) return;
            set("NU");
            actor->merc_chase = false;
            actor->merc->next_act = now_ms;                                      // a walk's end thinks at once (FUN_005a8030)
        }
        if (actor->merc->mode != "NU" || now_ms < actor->merc->next_act) return;
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int merc_x = sub(unit.x), merc_y = sub(unit.y), size = actor->merc_npc->size_x > 0 ? actor->merc_npc->size_x : 1;
        // The foe (FUN_005ddc30 -> rules::search_sight / sight_choice): the
        // near rooms' monsters (FUN_005dce60: the merc's room's near list,
        // each room's units newest first), not good, alive, out of town,
        // their MonStats threat; sight from them to it; FUN_005dd510's path
        // test a type 2 (toward) path to the primary.
        // ponytail: no monster here is ever in state 0x92 (invis).
        std::vector<d2d::rules::SightFoe> sight_foes;
        std::vector<int> sight_index;
        const auto wall = [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x04); };
        for (const auto& [room_level, room] : near_rooms(*game_data, *level, unit.x, unit.y)) {
            if (room_level != level) continue;
            for (std::size_t i = monsters.size(); i-- > 0;) {
                const auto& monster = monsters[i];
                if (room_of(*level, monster.unit.x, monster.unit.y) != room) continue;
                const auto& foe_type = game_data->monsters.types[std::size_t(monster.type)];
                const int foe_x = sub(monster.unit.x), foe_y = sub(monster.unit.y);
                auto& entry = sight_foes.emplace_back(d2d::rules::SightFoe{ d2d::rules::near_distance(foe_x - merc_x, foe_y - merc_y, foe_type.size), foe_type.threat,
                                                                            monster.targetable() && !town && !d2d::rules::friends(2, monster.align) });
                entry.blocked = entry.enemy && entry.distance < 0x31 && d2d::rules::sight_blocked(foe_x, foe_y, foe_type.size, merc_x, merc_y, size, wall);
                sight_index.push_back(int(i));
            }
        }
        int target_gap = 0x7fffffff;
        const auto path_to_primary = [&](int pick) {
            const auto& foe = monsters[std::size_t(sight_index[std::size_t(pick)])];
            const int foe_x = sub(foe.unit.x), foe_y = sub(foe.unit.y);
            if ((foe_x == merc_x && foe_y == merc_y) || std::abs(foe_x - merc_x) > 100 || std::abs(foe_y - merc_y) > 100) return false;   // FUN_00649970
            const auto points = d2d::rules::toward_path(merc_x, merc_y, foe_x, foe_y, 5, 1, [&](int at_x, int at_y) {
                return level->unit_blocked(centre(at_x), centre(at_y), unit.shape) || crowd.at(centre(at_x), centre(at_y), &unit);
            });
            return std::ranges::any_of(points, [&](const std::pair<int, int>& point) { return point != std::pair(merc_x, merc_y); });
        };
        const int chosen = d2d::rules::sight_choice(sight_foes, d2d::rules::search_sight(sight_foes), path_to_primary, target_gap);
        actor->merc_target = chosen >= 0 ? sight_index[std::size_t(chosen)] : -1;
        if (actor->merc_target < 0) target_gap = -1;
        d2d::rules::MercView view{ .cls = actor->merc_npc->hc_idx, .x = merc_x, .y = merc_y, .size = size, .mode = d2d::rules::kMonsterNeutral,
                                   .owner_x = sub(actor->player.x), .owner_y = sub(actor->player.y), .owner_mode = actor->owner_mode,
                                   .end_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.back().first),
                                   .end_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.back().second),
                                   .ring = actor->footsteps, .cursor = actor->footstep_cursor,
                                   .on_bit40 = level->blocked(unit.x, unit.y, 0x40), .town = town, .target = target_gap };
        auto area_at = [&](int from_x, int from_y, int x, int y) { return int(area_id_at(*game_data, *level, centre(from_x), centre(from_y), centre(x), centre(y))); };   // FUN_0061b130
        std::vector<std::pair<float, float>> route;
        auto try_move = [&](const d2d::rules::MercMove& move) {
            route = move_route(*level, unit, merc_x, merc_y, move, crowd);
            return !route.empty();
        };
        using Kind = d2d::rules::MercAct::Kind;
        const auto act = d2d::rules::hireable_think(view, actor->merc->seed, area_at, try_move);
        const int aidel = type_info && type_info->diff[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))].aidel > 0
                              ? type_info->diff[std::size_t(std::clamp(actor->character.header.active_difficulty(), 0, 2))].aidel : 15;
        actor->merc_chase = false;
        if (act.kind == Kind::moved) {
            set(act.move.mode == d2d::rules::kMonsterRun ? "RN" : "WL");
            unit.path = route; unit.goal_x = centre(act.move.x); unit.goal_y = centre(act.move.y);
            unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false;
            actor->merc->move_pct = act.move.pct;
        } else if (act.kind == Kind::stand) {
            actor->merc->next_act = now_ms + std::uint32_t(act.frames) * 40;
        } else if (act.kind == Kind::failed) {
            actor->merc->next_act = now_ms + std::uint32_t(aidel) * 40;
        } else if (act.kind == Kind::teleport) {
            // FUN_005e3930 mode 3: room_teleport, then it stands 5.
            room_teleport(*game_data, *level, crowd, spawning, unit);
            actor->merc->next_act = now_ms + 5 * 40;
        } else if (act.kind == Kind::attack && actor->merc_target >= 0 && type_info) {
            // The attack think (FUN_005e5050) and its skill pick (FUN_005e4d30).
            // ponytail: the 0x29 distance is the gap + 1, not FUN_006416d0;
            // a skill's own mode token falls back to A1 without frames.
            const auto& target = monsters[std::size_t(actor->merc_target)];
            const int foe_x = sub(target.unit.x), foe_y = sub(target.unit.y), difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
            static constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
            const auto* skill1 = skill_named(type_info->skill[0]);
            const auto sk1mode = std::ranges::find(kModes, std::string_view(type_info->sk_mode[0]));
            d2d::rules::MercFightView fight_view{ .cls = actor->merc_npc->hc_idx, .level = actor->merc_st.level, .aip1 = type_info->diff[std::size_t(difficulty)].aip[0],
                .gap = d2d::rules::merc_gap(merc_x, merc_y, size, foe_x, foe_y), .distance = target_gap + 1,
                .melee = d2d::rules::unit_distance(foe_x - merc_x, foe_y - merc_y, type_info->size, game_data->monsters.types[std::size_t(target.type)].size) <= type_info->melee_rng + 1,
                .row = d2d::rules::hireling_row(game_data->rules, actor->character.header.merc_type, actor->merc_st.level), .ids = actor->merc_ids, .levels = actor->merc_levels,
                .skill1 = skill1 ? skill1->id : -1, .sk1mode = sk1mode == kModes.end() ? 4 : int(sk1mode - kModes.begin()) };
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k) {
                const auto* skill = game_data->skills.get(actor->merc_ids[k]);
                fight_view.aura[k] = skill && skill->aura;
                fight_view.running[k] = actor->merc_ids[k] > 0 && (actor->merc_ids[k] == actor->merc_aura || (actor->merc_ids[k] == actor->merc_buff.skill && now_ms < actor->merc_buff.until));
            }
            auto move = [&](int kind, int x, int y) {
                if (kind == 2) {                                      // FUN_005ded00: a run at the foe
                    if (!set_off(*level, unit, target.unit.x, target.unit.y, true, crowd)) return false;
                    set("RN");                                        // a run keeps its path
                    actor->merc->move_pct = 0; actor->merc_chase = true;
                    return true;
                }
                if (!try_move({ .x = x, .y = y })) return false;
                set("WL");
                unit.path = route; unit.goal_x = centre(x); unit.goal_y = centre(y);
                unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false;
                actor->merc->move_pct = 0;
                return true;
            };
            using AttackKind = d2d::rules::MercAttack::Kind;
            const auto attack = d2d::rules::merc_attack(fight_view, actor->merc_growth, merc_x, merc_y, foe_x, foe_y, actor->merc->seed, move);
            if (attack.kind == AttackKind::failed) actor->merc->next_act = now_ms + std::uint32_t(aidel) * 40;
            else if (attack.kind == AttackKind::stand) actor->merc->next_act = now_ms + std::uint32_t(attack.frames) * 40;
            else if (attack.kind == AttackKind::aura) {
                actor->merc_aura = attack.skill;
                d2d::log::info("the merc starts its aura {}", attack.skill);
                actor->merc->next_act = now_ms + 10 * 40;
            } else if (attack.kind == AttackKind::skill || attack.kind == AttackKind::swing) {
                actor->merc_skill = attack.kind == AttackKind::skill ? attack.skill : -1;
                actor->merc_skill_level = 0;
                for (std::size_t k = 0; k < actor->merc_ids.size(); ++k) if (actor->merc_ids[k] == actor->merc_skill) actor->merc_skill_level = actor->merc_levels[k];
                if (skill1 && actor->merc_skill == skill1->id && actor->merc_skill_level == 0) actor->merc_skill_level = type_info->sk_lvl[0];
                const auto mode = attack.kind == AttackKind::swing || attack.mode < 0 || attack.mode >= 16 ? std::string_view("A1") : kModes[std::size_t(attack.mode)];
                unit.dir = direction16(target.unit.x - unit.x, target.unit.y - unit.y);
                actor->merc_seq = nullptr;
                if (attack.kind == AttackKind::skill && attack.mode == 14)   // SQ: the sequence its MonStats slot for the skill names
                    for (std::size_t slot = 0; slot < type_info->skill.size(); ++slot)
                        if (const auto* slot_skill = skill_named(type_info->skill[slot]); slot_skill && slot_skill->id == actor->merc_skill)
                            if (const auto found = game_data->mon_seqs.find(type_info->sk_mode[slot]); found != game_data->mon_seqs.end() && !found->second.empty()) {
                                actor->merc_seq = &found->second;
                                actor->merc_seq_done = 0;
                            }
                const std::string_view played = actor->merc_seq ? std::string_view(actor->merc_seq->front().mode) : mode;
                set(game_data->npc_timing(*actor->merc_npc, played).directions ? played : std::string_view("A1"));
                if (actor->merc_seq) actor->merc->mode_until = now_ms + std::uint32_t(actor->merc_seq->size()) * game_data->npc_timing(*actor->merc_npc, actor->merc->mode).ms_per_frame();
                actor->merc->struck = false;
            }
        }
    }

auto Fight::player_modes(bool held, std::uint32_t now_ms, float elapsed) -> bool {
        if (dead()) {
            if (actor->pmode == kModeDT && now_ms >= actor->pmode_until) set_pmode(kModeDD, now_ms);
            return actor->pmode == kModeDD;
        }
        if (attack_mode(actor->pmode)) {
            strike(now_ms);
            if (actor->smove.active) skill_step(now_ms, elapsed);
            if (now_ms >= actor->pmode_until) {
                if (actor->kicks_left > 0 && next_target()) {
                    --actor->kicks_left;                            // Talon's next kick (FUN_005d5a30), Zeal's next hit (FUN_005dbc60)
                    set_pmode(swing_mode(), now_ms);
                    actor->pstruck = false;
                    return false;
                }
                actor->kicks_left = 0;
                actor->seq = {};
                actor->smove = {};
                actor->seq_loop = false;
                actor->pmode = -1; actor->player.mode_ms = now_ms;
                actor->item_charge = {};                            // the swing's over: skill_level is the character's again
                if (!held) actor->attack_mon = -1;
            }
        } else if (actor->pmode == kModeSC) {                       // a self cast: its state on the action frame
            if (!actor->pstruck && now_ms >= actor->player.mode_ms + std::uint32_t(float(player_anim(kModeSC).action_ms()) / actor->prate)) {
                actor->pstruck = true;
                do_sounds(now_ms);
                if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && skill->srvdofunc == ServerDoFunction::kBookOrScroll) {
                    actor->portal_due = true;                         // Town Portal (FUN_005bf3d0): the World opens it
                } else if (skill && missile_skill(*skill)) {
                    fire(*skill, now_ms);
                } else if (skill && spot_skill(*skill)) {
                    spot(*skill, now_ms);
                } else if (skill && summon_skill(*skill)) {
                    summon(*skill, now_ms);
                } else if (skill) {
                    start_state(*skill, skill_level ? skill_level(skill->id) : 1, now_ms);
                }
            }
            if (now_ms >= actor->pmode_until) { actor->pmode = -1; actor->player.mode_ms = now_ms; actor->item_charge = {}; if (!held) actor->attack_mon = -1; }
        } else if ((actor->pmode == kModeGH || actor->pmode == kModeBL) && now_ms >= actor->pmode_until) {
            actor->pmode = -1; actor->player.mode_ms = now_ms;
        }
        return false;
    }

auto Fight::skill_step(std::uint32_t now_ms, float elapsed) -> void {
        const auto* skill = game_data->skills.get(actor->swing_skill);
        if (skill && skill->srvdofunc == ServerDoFunction::kCharge && actor->attack_mon >= 0) {
            actor->smove.target_x = monsters[std::size_t(actor->attack_mon)].unit.x; actor->smove.target_y = monsters[std::size_t(actor->attack_mon)].unit.y;
        }
        const float dx = actor->smove.target_x - actor->player.x, dy = actor->smove.target_y - actor->player.y, distance = std::hypot(dx, dy);
        const bool whirl = skill && skill->srvdofunc == ServerDoFunction::kWhirlwind;
        if (distance < 0.05f || (!whirl && !actor->smove.fly && distance <= kMeleeReach)) { if (whirl) actor->pmode_until = now_ms; return; }
        actor->player.dir = direction16(dx, dy);
        const float step = std::min(actor->smove.speed * elapsed, distance), next_x = actor->player.x + dx / distance * step, next_y = actor->player.y + dy / distance * step;
        if (!actor->smove.fly && level->unit_blocked(next_x, next_y)) { if (whirl) actor->pmode_until = now_ms; return; }
        actor->player.x = next_x; actor->player.y = next_y;
    }

auto Fight::move_event(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        if (skill.srvdofunc == ServerDoFunction::kWhirlwind) {
            if (now_ms < actor->whirl_next) return;
            const auto& attack_anim = player_anim(kModeA1);
            const int ticks = d2d::rules::attack_ticks(int(attack_anim.frames ? attack_anim.frames : 16), int(attack_anim.speed ? attack_anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, chill_rate(now_ms));
            actor->whirl_next = now_ms + std::uint32_t(d2d::rules::whirlwind_gap(ticks)) * 40;
            const int hands = int(std::ranges::count_if(actor->character.items, [&](const d2d::d2s::Item& item) {
                return item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm) && game_data->rules.item_info.contains(item.code)
                    && game_data->rules.item_info.at(item.code).kind == 2; }));
            for (int hand = 0; hand < (hands >= 2 ? 2 : 1); ++hand) {
                std::vector<int> nearby;
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - actor->player.x, monsters[i].unit.y - actor->player.y) <= 5.f) nearby.push_back(int(i));
                if (nearby.empty()) return;
                const auto next = std::ranges::upper_bound(nearby, actor->attack_mon);
                actor->attack_mon = next != nearby.end() ? *next : nearby.front();
                hit(now_ms, 5.f);
            }
            return;
        }
        if (skill.srvdofunc == ServerDoFunction::kCharge) {
            const auto frame_index = seq_at(now_ms);
            const auto run_end = std::size_t(std::ranges::find_if(actor->seq, [](const auto& frame) { return frame.mode != kModeRN; }) - actor->seq.begin());
            if (frame_index < run_end) {                               // running: in reach, on to the attack frames
                if (actor->attack_mon >= 0 && std::hypot(monsters[std::size_t(actor->attack_mon)].unit.x - actor->player.x,
                                                  monsters[std::size_t(actor->attack_mon)].unit.y - actor->player.y) <= kMeleeReach + 0.3f) {
                    actor->seq_loop = false;
                    actor->smove.active = false;
                    actor->player.mode_ms = now_ms - std::uint32_t(run_end) * actor->seq_frame_ms;
                    actor->seq_struck = seq_events(now_ms);
                    actor->seq_sounded = seq_events(now_ms, 3);
                    actor->pmode_until = now_ms + std::uint32_t(actor->seq.size() - run_end) * actor->seq_frame_ms;
                }
                return;
            }
            hit(now_ms);
            return;
        }
        // Leap Attack: the first event takes off, the second lands, the third strikes.
        if (actor->seq_struck == 1 && actor->attack_mon >= 0) {
            const auto& target = monsters[std::size_t(actor->attack_mon)].unit;
            const float dx = target.x - actor->player.x, dy = target.y - actor->player.y, distance = std::max(std::hypot(dx, dy), 0.01f);
            const float land = std::max(distance - kMeleeReach * 0.8f, 0.f);
            std::uint32_t air = actor->seq_frame_ms;
            for (std::size_t k = seq_at(now_ms) + 1; k < actor->seq.size() && actor->seq[k].event != 1; ++k) air += actor->seq_frame_ms;
            actor->smove = { actor->player.x + dx / distance * land, actor->player.y + dy / distance * land, land / (float(air) / 1000.f), land > 0.f, true };
            actor->player.dir = direction16(dx, dy);
        } else if (actor->seq_struck == 2) {
            if (actor->smove.active) { actor->player.x = actor->smove.target_x; actor->player.y = actor->smove.target_y; }
            actor->smove = {};
        } else if (actor->seq_struck >= 3) {
            hit(now_ms);
        }
    }

auto Fight::other_target() -> void {
        const auto reach = in_reach();
        if (reach.empty()) { actor->attack_mon = -1; return; }
        const auto next = std::ranges::upper_bound(reach, actor->attack_mon);
        actor->attack_mon = next != reach.end() ? *next : reach.front();
        actor->player.dir = direction16(monsters[std::size_t(actor->attack_mon)].unit.x - actor->player.x, monsters[std::size_t(actor->attack_mon)].unit.y - actor->player.y);
    }

auto Fight::ammo_index() const -> int {
        using namespace d2d::d2s;
        const auto& tables = game_data->rules;
        auto arm = [&](int slot) -> int {
            for (std::size_t i = 0; i < actor->character.items.size(); ++i)
                if (actor->character.items[i].location == item_location::kEquipped && actor->character.items[i].slot == slot) return int(i);
            return -1;
        };
        const int right = arm(body_location::kRightArm), left = arm(body_location::kLeftArm);
        auto type_of = [&](int index) -> std::string {
            const auto found = index >= 0 ? tables.item_info.find(actor->character.items[std::size_t(index)].code) : tables.item_info.end();
            return found != tables.item_info.end() ? found->second.type : std::string{};
        };
        auto bow = [&](int index) { return d2d::rules::type_is(tables, type_of(index), "bow") || d2d::rules::type_is(tables, type_of(index), "xbow"); };
        auto ammo = [&](int index) { return index >= 0 && (d2d::rules::type_is(tables, type_of(index), "misl") || throwable(*game_data, actor->character.items[std::size_t(index)].code)); };
        int found = -1;
        if (bow(right) || bow(left)) found = ammo(right) && !bow(right) ? right : ammo(left) && !bow(left) ? left : -1;
        else found = right >= 0 && throwable(*game_data, actor->character.items[std::size_t(right)].code) ? right
                   : left >= 0 && throwable(*game_data, actor->character.items[std::size_t(left)].code) ? left : -1;
        return found >= 0 && actor->character.items[std::size_t(found)].quantity > 0 ? found : -1;
    }

auto Fight::weapon_missile() const -> const GameData::MissileInfo* {
        using namespace d2d::d2s;
        const auto& tables = game_data->rules;
        bool crossbow = false, bow = false;
        for (const auto& item : actor->character.items)
            if (item.location == item_location::kEquipped && (item.slot == body_location::kRightArm || item.slot == body_location::kLeftArm))
                if (const auto found = tables.item_info.find(item.code); found != tables.item_info.end()) {
                    crossbow = crossbow || d2d::rules::type_is(tables, found->second.type, "xbow");
                    bow = bow || d2d::rules::type_is(tables, found->second.type, "bow");
                }
        if (!bow && !crossbow) return nullptr;
        int magic = 0, exploding = 0;
        for (const auto& prop : gear_props(*game_data, actor->character.items)) {
            if (prop.stat == kItemMagicArrow) magic += prop.value;
            if (prop.stat == kItemExplosiveArrow) exploding += prop.value;
        }
        const char* name = magic > 0 ? "magicarrow" : exploding > 0 ? "explodingarrow" : crossbow ? "bolt" : "arrow";
        const auto found = game_data->missiles.find(name);
        return found != game_data->missiles.end() ? &found->second : nullptr;
    }

auto Fight::needs_ammo(const d2d::rules::Skill& skill) const -> bool {
        if (skill.srvdofunc == ServerDoFunction::kAttack) {
            const auto* shot = weapon_missile();
            return shot && shot != &game_data->missiles.at("magicarrow");
        }
        return skill.decquant || skill.srvdofunc == ServerDoFunction::kThrow || skill.srvstfunc == ServerStartFunction::kCheckAmmo;
    }

auto Fight::spend_ammo() -> void {
        const int index = ammo_index();
        if (index < 0) return;
        if (--actor->character.items[std::size_t(index)].quantity <= 0) out_of_ammo(std::size_t(index));
    }

auto Fight::out_of_ammo(std::size_t index) -> void {
        using namespace d2d::d2s;
        auto& item = actor->character.items[index];
        item.quantity = 0;
        const auto info = game_data->rules.item_info.find(item.code);
        if (info != game_data->rules.item_info.end() && info->second.kind == 2 && item.quality >= 4 && item.quality <= 9) {
            item.flags |= 0x100u;
            item.durability = 0;
            return;
        }
        const std::string code = item.code;
        const auto slot = item.slot;
        actor->character.items.erase(actor->character.items.begin() + std::ptrdiff_t(index));
        const auto next = std::ranges::find_if(actor->character.items, [&](const Item& other) {
            return other.code == code && other.location == item_location::kStored && other.panel == item_panel::kInventory && !d2d::rules::broken(other); });
        if (next != actor->character.items.end()) { next->location = item_location::kEquipped; next->slot = slot; next->panel = 0; }
    }

auto Fight::wear(std::size_t index, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& item = actor->character.items[index];
        const auto& tables = game_data->rules;
        const auto info = tables.item_info.find(item.code);
        if (info == tables.item_info.end()) return;
        const bool armor = d2d::rules::type_is(tables, info->second.type, "armo"), weapon = d2d::rules::type_is(tables, info->second.type, "weap");
        if ((!armor && !weapon) || item.max_durability <= 0 || d2d::rules::indestructible(item) || d2d::rules::broken(item)) return;
        const int chance = armor || throwable(*game_data, item.code) ? 10 : 4;
        if (int(rng(100)) >= chance) return;
        if (item.durability - 1 >= 1) { item.durability = std::min(item.durability - 1, d2d::rules::max_durability(item)); return; }
        if (armor) { item.flags |= 0x100u; item.durability = 0; cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y); return; }
        const auto base = tables.item_base.find(item.code);
        if (base == tables.item_base.end() || !base->second.stackable) {
            item.durability = 0;
            cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y);
            return;
        }
        if (item.quantity < 2) { out_of_ammo(index); return; }
        --item.quantity;
        item.durability = d2d::rules::max_durability(item);
    }

auto Fight::wear_weapon(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        for (std::size_t i = 0; i < actor->character.items.size(); ++i)
            if (actor->character.items[i].location == item_location::kEquipped && (actor->character.items[i].slot == body_location::kRightArm || actor->character.items[i].slot == body_location::kLeftArm)
                && game_data->rules.item_info.contains(actor->character.items[i].code) && game_data->rules.item_info.at(actor->character.items[i].code).kind == 2) { wear(i, now_ms); return; }
    }

auto Fight::wear_armor(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        static constexpr std::array<std::pair<int, int>, 7> kSlots{ { { body_location::kHead, 3 }, { body_location::kTorso, 5 }, { body_location::kRightArm, 4 },
            { body_location::kLeftArm, 4 }, { body_location::kBelt, 2 }, { body_location::kFeet, 2 }, { body_location::kGloves, 2 } } };
        std::array<int, 7> worn{};
        int total = 0;
        for (std::size_t slot_index = 0; slot_index < kSlots.size(); ++slot_index) {
            worn[slot_index] = -1;
            for (std::size_t i = 0; i < actor->character.items.size(); ++i)
                if (const auto& item = actor->character.items[i]; item.location == item_location::kEquipped && item.slot == kSlots[slot_index].first && game_data->rules.item_info.contains(item.code)
                    && d2d::rules::type_is(game_data->rules, game_data->rules.item_info.at(item.code).type, "armo")) { worn[slot_index] = int(i); total += kSlots[slot_index].second; }
        }
        if (total <= 0) return;
        auto slot_index = std::size_t(rng(int(kSlots.size())));
        int pick = rng(total);
        for (;; slot_index = (slot_index + 1) % kSlots.size()) {
            if (worn[slot_index] < 0) continue;
            if (pick < kSlots[slot_index].second) { wear(std::size_t(worn[slot_index]), now_ms); return; }
            pick -= kSlots[slot_index].second;
        }
    }

auto Fight::impale_wear(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        if (int(rng(100)) >= d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl)) return;
        for (std::size_t i = 0; i < actor->character.items.size(); ++i)
            if (auto& item = actor->character.items[i]; item.location == d2d::d2s::item_location::kEquipped && (item.slot == d2d::d2s::body_location::kRightArm || item.slot == d2d::d2s::body_location::kLeftArm)
                && game_data->rules.item_info.contains(item.code) && game_data->rules.item_info.at(item.code).kind == 2) {
                const auto found = game_data->rules.item_base.find(item.code);
                if (found != game_data->rules.item_base.end() && found->second.stackable) { if (--item.quantity <= 0) out_of_ammo(i); }
                else if (item.max_durability > 0 && !d2d::rules::indestructible(item)) {
                    item.durability -= d2d::rules::eval_calc(game_data->skills, skill.calc[2], env, skill.id, lvl);
                    if (item.durability <= 0) {
                        item.durability = 0; item.flags |= 0x100u;             // FUN_0055f850: it breaks
                        cues.cue(d2d::rules::sound_ids::kCursorDurabilityBreak, now_ms, actor->player.x, actor->player.y);
                    }
                }
                return;
            }
    }

auto Fight::skill_missile(const d2d::rules::Skill& skill, bool any_owner ) const -> const GameData::MissileInfo* {
        static constexpr ServerDoFunction kDo[] = { ServerDoFunction::kMissileFan, ServerDoFunction::kScatteredBolts, ServerDoFunction::kNova, ServerDoFunction::kGuidedMissile,
            ServerDoFunction::kStrafe, ServerDoFunction::kChainLightning, ServerDoFunction::kMissileAtTarget, ServerDoFunction::kMissileWall,
            ServerDoFunction::kBreath, ServerDoFunction::kBlessedHammer, ServerDoFunction::kFistOfTheHeavens, ServerDoFunction::kFirestorm,
            ServerDoFunction::kWindMissile, ServerDoFunction::kVolcano, ServerDoFunction::kShockField, ServerDoFunction::kBladeFury,
            ServerDoFunction::kWarCry, ServerDoFunction::kGrimWard, ServerDoFunction::kBladeSentinel, ServerDoFunction::kWakeOfDestruction,
            ServerDoFunction::kMonsterBreath };
        static constexpr ServerStartFunction kSt[] = { ServerStartFunction::kNone, ServerStartFunction::kCheckAmmo, ServerStartFunction::kStrafe,
            ServerStartFunction::kCheckMana, ServerStartFunction::kBladeFury, ServerStartFunction::kTargetCorpse };
        if (skill.srvdofunc == ServerDoFunction::kAttack) return weapon_missile();
        const bool plain = (skill.srvstfunc == ServerStartFunction::kNone || skill.srvstfunc == ServerStartFunction::kCheckAmmo) && skill.srvdofunc == ServerDoFunction::kNone;
        const bool multi = std::ranges::contains(kSt, skill.srvstfunc) && std::ranges::contains(kDo, skill.srvdofunc);
        const auto& name = plain ? skill.srvmissile : skill.srvmissilea;
        if ((!plain && !multi) || name.empty()) return nullptr;
        const auto found = game_data->missiles.find(name);
        static constexpr int kHit[] = { 0, 1, 2, 3, 4, 7, 9, 10, 12, 13, 14, 17, 18, 20, 21, 22, 26, 29, 36, 37, 47, 48, 56 };
        if (found == game_data->missiles.end() || !std::ranges::contains(kHit, found->second.hit_func)) return nullptr;
        return any_owner || found->second.skill == skill.name || found->second.skill.empty() ? &found->second : nullptr;
    }

auto Fight::missile_skill(const d2d::rules::Skill& skill) const -> bool { return skill_missile(skill) != nullptr; }

auto Fight::cast_missile(int skill, float target_x, float target_y, std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || (!missile_skill(*skill_row) && !spot_skill(*skill_row))) return false;
        // Throw is a spot skill: a held one (engage) whose last one's gone, or the weapons
        // swapped since the command, stops here (FUN_004610c0's status 2).
        if (skill_refused(*game_data, actor->character, skill)) { actor->attack_mon = -1; impossible(now_ms); return false; }
        const int lvl = cast_level(skill);
        const int cost = cost_of(*skill_row, lvl);
        if (lvl > 0 && actor->character.stats.values[kMana] < cost) need_mana(now_ms);
        if (lvl <= 0 || actor->character.stats.values[kMana] < cost || (needs_ammo(*skill_row) && ammo_index() < 0)) { actor->attack_mon = -1; return false; }
        pay(*skill_row, cost);
        actor->swing_skill = skill;
        actor->cast_x = target_x; actor->cast_y = target_y;
        actor->player.dir = direction16(target_x - actor->player.x, target_y - actor->player.y);
        set_pmode(skill_row->anim == "A1" || skill_row->anim == "TH" ? kModeA1 : kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::launch(const GameData::MissileInfo& missile_info, const d2d::rules::Skill& skill, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t now_ms) -> Missile& {
        const float speed = missile_speed(missile_info, lvl), distance = std::max(std::hypot(dx, dy), 0.01f);
        Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, missile_facing(x, y, dx, dy), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
        missile.friendly = true; missile.caster = actor->id; missile.skill = skill.id; missile.level = lvl; missile.by_merc = casting_merc;
        pending.push_back(std::move(missile));
        return pending.back();
    }

auto Fight::calc(const d2d::rules::Skill& skill, const d2d::rules::Calc& calc_row, int lvl) -> int {
        return d2d::rules::eval_calc(game_data->skills, calc_row, calc_env(), skill.id, lvl);
    }

auto Fight::fire(const d2d::rules::Skill& skill, std::uint32_t now_ms, const Caster& caster) -> void {
        const auto& missile_info = *skill_missile(skill);
        const int lvl = caster.level;
        casting_merc = caster.merc;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{ casting_merc };
        const int calc1 = calc(skill, skill.calc[0], lvl);
        const auto shared = std::make_shared<std::vector<int>>();
        const float dx = caster.to_x - caster.x, dy = caster.to_y - caster.y;
        const int range = missile_info.range + missile_info.lev_range * lvl;
        const int target = caster.target >= 0 && monsters[std::size_t(caster.target)].alive() ? caster.target : -1;
        auto send = [&](float ddx, float ddy, int reach) -> Missile& {
            auto& missile = launch(missile_info, skill, lvl, caster.x, caster.y, ddx, ddy, reach, now_ms);
            missile.target_x = caster.to_x; missile.target_y = caster.to_y;                    // where it was sent (Molten Boulder's roll)
            missile.to = { caster.x + ddx, caster.y + ddy };                         // its flight's aim (FUN_0059fa30)
            return missile;
        };
        if (skill.srvdofunc == ServerDoFunction::kMissileFan) {
            const int count = std::max(calc1, 1);
            float side_x = dy * 5, side_y = -dx * 5;                 // subtiles, turned a right angle
            while (side_x * side_x + side_y * side_y > 3) { side_x = std::trunc(side_x / 2); side_y = std::trunc(side_y / 2); }
            if (side_x == 0 && side_y == 0) side_x = 1;
            for (int k = 0; k < count; ++k) {
                const float spread = float(k) - float(count) / 2;
                send(dx + spread * side_x / 5, dy + spread * side_y / 5, range);
            }
        } else if (skill.srvdofunc == ServerDoFunction::kScatteredBolts) {
            for (int k = 0; k < std::max(calc1, 1); ++k) send(dx, dy, range).bolt = k;   // each wandering (FUN_005c9290)
        } else if (skill.srvdofunc == ServerDoFunction::kNova || skill.srvdofunc == ServerDoFunction::kWarCry) {
            // A war cry (do 68, FUN_005d83e0): the nova of its missile (range
            // its row's), then its state on the caster (FUN_005d8290).
            for (int k = 0; k < 64; ++k) {
                const float angle = float(k) * 2 * 3.14159265f / 64;
                auto& shot = send(std::cos(angle), std::sin(angle), skill.srvdofunc == ServerDoFunction::kWarCry ? missile_info.range : missile_info.range + calc1);
                shot.struck = shared; shot.to.reset();                              // a way, not a point
            }
            if (skill.srvdofunc == ServerDoFunction::kWarCry && !skill.aurastate.empty()) {
                start_state(skill, lvl, now_ms);
                // The nova reaching an ally (hit 18, FUN_005ab0b0: FUN_00554de0)
                // gives it the state too, the caster's auralen and level
                // (FUN_005d8290, aurastats FUN_005c6cc0): the merc, the pets.
                // ponytail: an ally within the missile's reach (speed x Range)
                // when it's cast, not as the nova passes.
                const float reach = missile_speed(missile_info, lvl) * float(missile_info.range) * 0.04f;
                const int len = calc(skill, skill.auralen, lvl);
                const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
                if (actor->merc && actor->merc->mode != "DT" && std::hypot(actor->merc->unit.x - actor->player.x, actor->merc->unit.y - actor->player.y) <= reach) {
                    const int before = merc_max_life();
                    actor->merc_cry = { skill.id, lvl, until };
                    actor->merc->hit_points += std::max(merc_max_life() - before, 0);
                }
                for (auto& pet : actor->pets)
                    if (pet.monster.alive() && std::hypot(pet.monster.unit.x - actor->player.x, pet.monster.unit.y - actor->player.y) <= reach) {
                        std::erase_if(pet.buffs, [&](const SelfState& buff) { return buff.skill == skill.id; });
                        pet.buffs.push_back({ skill.id, lvl, until });
                        pet_buffs(pet);
                    }
            }
        } else if (skill.srvdofunc == ServerDoFunction::kGuidedMissile) {
            auto& missile = send(dx, dy, range);
            missile.target = target; missile.ed_pct = calc1;
        } else if (skill.srvdofunc == ServerDoFunction::kStrafe) {
            const int radius = calc(skill, skill.aurarange, lvl);
            std::vector<int> nearby;
            if (target >= 0) nearby.push_back(target);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (int(i) != target && monsters[i].targetable()
                    && std::hypot(monsters[i].unit.x - caster.x, monsters[i].unit.y - caster.y) * 5 <= float(radius)) nearby.push_back(int(i));
            const int high = calc1, low = std::min(calc(skill, skill.calc[2], lvl), high);
            const int count = std::clamp(int(nearby.size()), low, high);
            for (int k = 0; k < count; ++k)
                actor->strafe.push_back({ nearby.empty() ? -1 : nearby[std::size_t(k) % nearby.size()], now_ms + std::uint32_t(k) * 120 });
        } else if (skill.srvdofunc == ServerDoFunction::kChainLightning) {
            send(dx, dy, range).hops = calc1;
        } else if (skill.srvdofunc == ServerDoFunction::kMissileAtTarget) {
            launch(missile_info, skill, lvl, caster.to_x, caster.to_y, 0, 0, range, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kMissileWall) {
            const float around_x = target >= 0 ? monsters[std::size_t(target)].unit.x : caster.to_x;
            const float around_y = target >= 0 ? monsters[std::size_t(target)].unit.y : caster.to_y;
            const float perp_x = around_y - caster.y, perp_y = caster.x - around_x;
            launch(missile_info, skill, lvl, around_x, around_y, perp_x, perp_y, range, now_ms);
            launch(missile_info, skill, lvl, around_x, around_y, -perp_x, -perp_y, range, now_ms);
            if (const auto found = game_data->missiles.find(skill.srvmissileb); found != game_data->missiles.end())
                launch(found->second, skill, lvl, around_x, around_y, 0, 0, found->second.range + found->second.lev_range * lvl, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kBreath || skill.srvdofunc == ServerDoFunction::kBladeFury) {
            if (caster.merc || caster.pet) send(dx, dy, range);            // one a cast: the channel is the player's (missile_tick)
            else actor->channel = skill.id;
        } else if (skill.srvdofunc == ServerDoFunction::kBlessedHammer) {                      // Blessed Hammer (FUN_005d0040): path 14, a spiral out
            auto& missile = send(dx, dy, range);
            missile.target_x = caster.x; missile.target_y = caster.y;
            missile.turn = int(std::atan2(dy, dx) * 1000);
        } else if (skill.srvdofunc == ServerDoFunction::kFistOfTheHeavens) {                      // Fist of the Heavens (FUN_005d0670): on the target
            if (target < 0) return;
            const auto& target_monster = monsters[std::size_t(target)];
            launch(missile_info, skill, lvl, target_monster.unit.x, target_monster.unit.y, 0, 0, range, now_ms).target = target;
        } else if (skill.srvdofunc == ServerDoFunction::kFirestorm || skill.srvdofunc == ServerDoFunction::kWindMissile) {   // Firestorm (FUN_005c7160), Twister / Tornado (FUN_005c72f0)
            for (int k = 0; k < std::max(calc1, 1); ++k) {
                const float angle = k == 0 ? 0.f : (float(rng(41)) - 20) * 3.14159265f / 180;
                send(dx * std::cos(angle) - dy * std::sin(angle), dx * std::sin(angle) + dy * std::cos(angle), range);
            }
        } else if (skill.srvdofunc == ServerDoFunction::kBladeSentinel) {                      // Blade Sentinel: out to the point and back, Param1 frames
            // ponytail: do 44 (FUN_005d6020) moves a trap monster between
            // the caster and the point; here its missile row does, hitting
            // as it passes (NextHit).
            auto& missile = send(dx, dy, std::max(range, skill.par[0]));
            missile.origin_x = caster.x; missile.origin_y = caster.y;
            const float distance = std::max(std::hypot(dx, dy), 0.01f), speed = cells_per_sec(10.f);   // bladecreeper's Velocity
            missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed;
        } else if (skill.srvdofunc == ServerDoFunction::kGrimWard) {                      // Grim Ward: its start row at the point
            launch(missile_info, skill, lvl, caster.to_x, caster.to_y, 0, 0, missile_info.range, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kVolcano) {                     // Volcano (FUN_005c8080): at the target point
            launch(missile_info, skill, lvl, caster.to_x, caster.to_y, 0, 0, range, now_ms);
        } else if (skill.srvdofunc == ServerDoFunction::kShockField) {                      // Shock Web (FUN_005d5d70 -> FUN_005d5bf0): scattered round the target
            const int count = std::max(calc(skill, skill.prgcalc[0], lvl), 1), radius = std::max(calc(skill, skill.aurarange, lvl), 1);
            for (int k = 0; k < count; ++k) {
                const float target_x = caster.to_x + float(int(rng(2 * radius + 1)) - radius) / 5, target_y = caster.to_y + float(int(rng(2 * radius + 1)) - radius) / 5;
                send(target_x - caster.x, target_y - caster.y, land_range(missile_info, target_x - caster.x, target_y - caster.y));
            }
        } else {
            send(dx, dy, missile_info.hit_func == 36 ? land_range(missile_info, dx, dy) : range);
        }
    }

auto Fight::land_range(const GameData::MissileInfo& missile_info, float dx, float dy) -> int {
        const float per_frame = missile_speed(std::max(d2d::rules::missile_velocity(missile_info.vel, missile_info.vel_lev, 1), 1)) * 0.04f;
        return std::max(int(std::hypot(dx, dy) / per_frame), 1);
    }

auto Fight::player_shots(std::uint32_t now_ms) -> void {
        const int frame = int(now_ms / 40);
        if (frame == actor->last_frame) return;
        actor->last_frame = frame;
        std::erase_if(actor->strafe, [&](const std::pair<int, std::uint32_t>& strafed) {
            if (now_ms < strafed.second) return false;
            const auto* skill = game_data->skills.get(actor->swing_skill);
            if (!skill || skill->srvdofunc != ServerDoFunction::kStrafe) return true;
            const auto& missile_info = *skill_missile(*skill);
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            float target_x = actor->cast_x, target_y = actor->cast_y;
            if (strafed.first >= 0 && monsters[std::size_t(strafed.first)].alive()) { target_x = monsters[std::size_t(strafed.first)].unit.x; target_y = monsters[std::size_t(strafed.first)].unit.y; }
            launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, target_x - actor->player.x, target_y - actor->player.y, missile_info.range + missile_info.lev_range * lvl, now_ms).ed_pct
                = calc(*skill, skill->calc[1], lvl);
            return true;
        });
        if (actor->channel >= 0) {
            const auto* skill = game_data->skills.get(actor->channel);
            if (!skill || actor->pmode != kModeSC || actor->swing_skill != actor->channel) actor->channel = -1;
            else {
                const int lvl = skill_level ? skill_level(skill->id) : 1;
                const auto& missile_info = *skill_missile(*skill);
                if (skill->srvdofunc == ServerDoFunction::kBreath)
                    launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, std::max(calc(*skill, skill->calc[0], lvl), 1), now_ms);
                else if (frame % 3 == 0)                     // Blade Fury (FUN_005d68a0): one per attack frame
                    launch(missile_info, *skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, missile_info.range, now_ms);
            }
        }
    }

auto Fight::missile_tick(std::uint32_t now_ms) -> void {
        const int frame = int(now_ms / 40);
        if (frame == missile_frame) return;
        missile_frame = frame;
        burned.clear();
        for (std::size_t k = 0; k < missiles.size(); ++k) {
            auto& missile = missiles[k];
            if (!missile.friendly || missile.skill < 0 || !missile.info) continue;   // the player's side's (a monster's carries its skill too)
            if (Hero* caster = hero_of(missile.caster)) act_as(*caster);              // its caster's skills, credit and leech
            merc_blow = casting_merc = missile.by_merc;                              // a merc's burst kills and fires as the merc's
            const auto* skill = game_data->skills.get(missile.skill);
            if (!skill) continue;
            const int age = int(now_ms - missile.born) / 40;
            const auto& missile_info = *missile.info;
            if (missile_info.srv_do == 7 && age % std::max(missile_info.param1, 1) == 0) {
                if (missile.target < 0 && skill->srvstfunc == ServerStartFunction::kNone) {
                    float best = 1e9f;
                    for (std::size_t i = 0; i < monsters.size(); ++i)
                        if (const float distance = std::hypot(monsters[i].unit.x - actor->cast_x, monsters[i].unit.y - actor->cast_y); monsters[i].targetable() && distance < best) {
                            best = distance; missile.target = int(i);
                        }
                }
                if (missile.target >= 0 && monsters[std::size_t(missile.target)].alive()) {
                    const auto& target = monsters[std::size_t(missile.target)];
                    const float speed = std::hypot(missile.velocity_x, missile.velocity_y), dx = target.unit.x - missile.x, dy = target.unit.y - missile.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                    missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed; missile.dir = missile_facing(missile.x, missile.y, dx, dy);
                }
            }
            const auto sub = missile_info.sub.empty() ? game_data->missiles.end() : game_data->missiles.find(missile_info.sub);
            if (missile_info.srv_do == 6 && sub != game_data->missiles.end())
                launch(sub->second, *skill, missile.level, missile.x, missile.y, 0, 0, sub->second.range + sub->second.lev_range * missile.level, now_ms);
            if ((missile_info.srv_do == 10 || missile_info.srv_do == 25) && sub != game_data->missiles.end()) {
                const int every = std::max(calc(*skill, skill->calc[1], missile.level), 1), radius = std::max(calc(*skill, skill->calc[0], missile.level), 1);
                if (age % every == 0) {
                    const float offset_x = float(int(rng(2 * radius - 1)) - (radius - 1)) / 5, offset_y = float(int(rng(2 * radius - 1)) - (radius - 1)) / 5;
                    launch(sub->second, *skill, missile.level, missile.x + offset_x, missile.y + offset_y, 0, 0, sub->second.range, now_ms);
                }
            }
            if (missile_info.srv_do == 5) {
                // A row with no Skill (Meteor's, Immolation Arrow's fire) burns its own element with its synergy.
                const auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = 0, .elo = missile.fixed, .ehi = missile.fixed }
                                  : missile_info.skill.empty() ? row_damage(missile_info, missile.level, calc_env(), missile.skill)
                                                               : d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), missile.level);
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - missile.x, monsters[i].unit.y - missile.y) <= 0.5f
                        && !std::ranges::contains(burned, std::pair{ int(i), missile.skill })) {
                        burned.emplace_back(int(i), missile.skill);
                        burn(i, damage, now_ms);
                    }
            }
            // The Range runs out: the hit function goes off with no unit.
            static constexpr int kEnd[] = { 1, 3, 4, 9, 13, 14, 20, 22, 26, 29, 36, 48, 56 };
            if (now_ms + 40 >= missile.dies && missile.frame < 0 && std::ranges::contains(kEnd, missile_info.hit_func)) burst(missile, now_ms);
            // 15 (FUN_005af030, Frozen Orb): every Param1 frames SubMissile1
            // toward direction `turn` of 64 (0x6e2b78 / 0x6e2a78), turning
            // Param2 on.
            if (missile_info.srv_do == 15 && sub != game_data->missiles.end() && age % std::max(missile_info.param1, 1) == 0) {
                const float angle = float(missile.turn & 63) * 2 * 3.14159265f / 64;
                launch(sub->second, *skill, missile.level, missile.x, missile.y, std::cos(angle), std::sin(angle), sub->second.range, now_ms);
                missile.turn += missile_info.param2;
            }
            // 14 (Grim Ward's totem): every Param1 frames the monsters within
            // aurarange of it run for auralen frames.
            // 14 (FUN_005aef70, Grim Ward's totem): every Param1 frames skill
            // srvdofunc Param2 (FUN_0056d810: 30, the curse) at the missile's
            // skill and level, the missile its caster and its spot the point
            // (FUN_0056d2c0, no target unit): Terror round it, aurarangecalc
            // wide, auralencalc / AiCurseDivisor long, the AI swap with it.
            if (missile_info.srv_do == 14 && age % std::max(missile_info.param1, 1) == 0 && missile_info.param2 == int(ServerDoFunction::kCurse)) {
                auto curse = *skill;
                curse.srvdofunc = ServerDoFunction::kCurse;
                spot(curse, now_ms, Caster{ .x = missile.x, .y = missile.y, .level = missile.level, .to_x = missile.x, .to_y = missile.y });
            }
            // 20 (Blade Sentinel): back and forth between the caster's spot and
            // where it was sent.
            if (missile_info.srv_do == 20 && std::hypot(missile.target_x - missile.x, missile.target_y - missile.y) < 0.3f) {
                std::swap(missile.target_x, missile.origin_x); std::swap(missile.target_y, missile.origin_y);
                const float speed = std::hypot(missile.velocity_x, missile.velocity_y), dx = missile.target_x - missile.x, dy = missile.target_y - missile.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed; missile.dir = missile_facing(missile.x, missile.y, dx, dy);
                missile.hit_at.clear();
            }
            // 31 (Wake of Fire's maker): its fire where it goes.
            if (missile_info.srv_do == 31 && sub != game_data->missiles.end())
                launch(sub->second, *skill, missile.level, missile.x, missile.y, missile.velocity_x, missile.velocity_y, sub->second.range, now_ms);
            // 23 (FUN_005af790, Firestorm): SubMissile1 where it is, each frame.
            if (missile_info.srv_do == 23 && sub != game_data->missiles.end())
                launch(sub->second, *skill, missile.level, missile.x, missile.y, 0, 0, sub->second.range, now_ms);
            // 27 (FUN_005afa30, Tornado): every Param1 (else calc4) frames its
            // damage within Param2 (else aurarange) subtiles.
            if (missile_info.srv_do == 27 && age % std::max(missile_info.param1 > 0 ? missile_info.param1 : calc(*skill, skill->calc[3], missile.level), 1) == 0)
                area(missile, missile.x, missile.y, missile_info.param2 > 0 ? missile_info.param2 : calc(*skill, skill->aurarange, missile.level), now_ms);
            // 28 (FUN_005afb80, Volcano): every Param1 (else calc4) frames
            // SubMissile1 thrown at a point within Param2 (else aurarange).
            // ponytail: its Param3 / Param4 frame window isn't applied (Volcano,
            // level 24: past Act 1).
            if (missile_info.srv_do == 28 && sub != game_data->missiles.end()
                && age % std::max(missile_info.param1 > 0 ? missile_info.param1 : calc(*skill, skill->calc[3], missile.level), 1) == 0) {
                const int radius = std::max(missile_info.param2 > 0 ? missile_info.param2 : calc(*skill, skill->aurarange, missile.level), 1);
                const float offset_x = float(int(rng(2 * radius + 1)) - radius) / 5, offset_y = float(int(rng(2 * radius + 1)) - radius) / 5;
                launch(sub->second, *skill, missile.level, missile.x, missile.y, offset_x, offset_y, land_range(sub->second, offset_x, offset_y), now_ms);
            }
            // Blessed Hammer (do 73's path 14): round its caster's spot and out.
            // ponytail: the client path's shape isn't traced: a turn each 1.6
            // s, out at a fifth of its speed.
            if (skill->srvdofunc == ServerDoFunction::kBlessedHammer && missile_info.srv_do <= 1) {
                const float seconds = float(now_ms - missile.born) / 1000, radius = seconds * missile_speed(missile_info, missile.level) * 0.2f;
                const float ang = float(missile.turn) / 1000 + seconds * 2 * 3.14159265f / 1.6f;
                missile.x = missile.target_x + radius * std::cos(ang); missile.y = missile.target_y + radius * std::sin(ang);
                missile.velocity_x = missile.velocity_y = 0;
            }
        }
        merc_blow = casting_merc = false;
    }

auto Fight::burn(std::size_t monster_index, const d2d::rules::MissileDamage& damage, std::uint32_t now_ms) -> void {
        static constexpr int kRes[5] = { 2, 3, 4, 5, 1 };
        if (damage.etype < 0 || damage.etype > 4 || damage.ehi <= 0) return;
        auto& monster = monsters[monster_index];
        const auto target = target_of(monster_index);
        int res = target.res[std::size_t(kRes[damage.etype])];
        if (damage.etype < 4 && res < 100) res = std::max(res - pierce()[std::size_t(damage.etype)], -100);
        monster.dot_acc += double(d2d::rules::resisted(rng.range(damage.elo, damage.ehi), res)) / 256;
        const int whole = int(monster.dot_acc);
        if (whole <= 0) return;
        monster.dot_acc -= whole;
        monster.hit_points -= whole;
        monster.aware = true;
        if (!monster.alive()) { set_mode(*game_data, monster, "DT", now_ms); killed(monster_index, now_ms); }
    }

auto Fight::pierce() const -> std::array<int, 4> { return { int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) }; }

auto Fight::skill_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms) -> bool {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || !missile.info) return true;
        const auto& missile_info = *missile.info;
        if (missile_info.srv_do == 5) return false;                    // a burner: missile_tick's
        // Hit function 10 (FUN_005aa650): a guided one passes by all but its target.
        if (missile_info.hit_func == 10 && missile.target >= 0 && missile.target != int(monster_index) && monsters[std::size_t(missile.target)].alive()) return false;
        // 36 (FUN_005abf70: Fire Blast, Shock Web in the air), 14 / 22 / 29 /
        // 48 (Meteor, Fist of the Heavens, Frozen Orb, Molten Boulder rising)
        // pass over units and act where they come down.
        if (missile_info.hit_func == 36 || missile_info.hit_func == 14 || missile_info.hit_func == 22 || missile_info.hit_func == 29 || missile_info.hit_func == 48) return false;
        // 7 (FUN_005a9fb0: Holy Bolt, the Fist's bolts): sHitPar2 1 strikes
        // the undead only (FUN_0063e990), 2 demons (FUN_0063e940); the rest
        // it passes (4). ponytail: it doesn't heal the player's side (calc1).
        if (missile_info.hit_func == 7) {
            const auto& type_info = game_data->monsters.types[std::size_t(monsters[monster_index].type)];
            if ((missile_info.hit_par2 == 1 && !type_info.undead) || (missile_info.hit_par2 == 2 && !type_info.demon)) return false;
        }
        // 18 (FUN_005ab0b0: Shout, Battle Orders, Battle Command) is for
        // allies; 21 (FUN_005ab500, Battle Cry) puts its auratargetstate on
        // the monster for auralen frames; 17 (FUN_005aafb0, Howl) sends one
        // below the caster's level + calc1 + the skill level running for
        // Param3 + Param4 x (level - 1) frames (FUN_004cc7c0); neither hurts.
        // Howl's run (FUN_005ddd00): on a monster swaps_ai lets, alive,
        // Terror's state (0x38, skill 77 level 1) for those frames with no
        // AI toggle at its end (FUN_0056e900), special AI 11 (FUN_005b0e00)
        // and a think next frame; Terror's think toggles back once it's
        // gone.
        // ponytail: Howl's level sum (FUN_004efc20) read as calc1 + level.
        if (missile_info.hit_func == 18) return false;
        if (missile_info.hit_func == 21) {
            if (monsters[monster_index].cry.skill != skill->id || now_ms >= monsters[monster_index].cry.until)
                put_state(*game_data, monsters[monster_index], monsters[monster_index].cry,
                          { skill->id, missile.level, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, missile.level), 1)) * 40 }, now_ms);
            return false;
        }
        if (missile_info.hit_func == 17) {
            const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
            if (auto& howled = monsters[monster_index]; howled.stats.level < clvl + calc(*skill, skill->calc[0], missile.level) + missile.level && howled.alive() && swaps_ai(*game_data, howled)) {
                put_state(*game_data, howled, howled.curse, { kTerror, 1, now_ms + std::uint32_t(std::max(skill->par[2] + (missile.level - 1) * skill->par[3], 25)) * 40 }, now_ms);
                ai_toggle(*game_data, howled, 11);
                howled.next_act = now_ms + 40;
            }
            missile.struck->push_back(int(monster_index));
            return false;
        }
        if (missile_info.hit_func == 26) return false;                 // Grim Ward's start: it lands (burst)
        // NextHit rows strike a monster again NextDelay frames on.
        if (missile_info.next_hit) {
            auto found = std::ranges::find(missile.hit_at, int(monster_index), &std::pair<int, std::uint32_t>::first);
            if (found != missile.hit_at.end() && now_ms < found->second + std::uint32_t(std::max(missile_info.next_delay, 1)) * 40) return false;
            if (found == missile.hit_at.end()) missile.hit_at.emplace_back(int(monster_index), now_ms); else found->second = now_ms;
        }
        // 1 (FUN_005a9a70) and 13 (FUN_005aa8b0) go off here, the area taking in the unit.
        if (missile_info.hit_func == 1 || missile_info.hit_func == 13) { burst(missile, now_ms); return true; }
        strike(missile, monster_index, now_ms);
        if (!missile_info.next_hit) missile.struck->push_back(int(monster_index));
        // Hit function 12 (FUN_005aa730, Chain Lightning): with hops left, on
        // from here at another monster within sHitPar1 (else aurarange)
        // subtiles (FUN_0056bd10), a hop fewer.
        // ponytail: the nearest one, not FUN_0056bd10's pick.
        if (missile_info.hit_func == 12 && missile.hops > 1) {
            const int radius = missile_info.hit_par1 > 0 ? missile_info.hit_par1 : calc(*skill, skill->aurarange, missile.level);
            int best = -1; float best_distance = 1e9f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float distance = std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y);
                    j != monster_index && monsters[j].targetable() && distance * 5 <= float(radius) && distance < best_distance) { best_distance = distance; best = int(j); }
            if (best >= 0) {
                const auto& next_target = monsters[std::size_t(best)];
                launch(missile_info, *skill, missile.level, missile.x, missile.y, next_target.unit.x - missile.x, next_target.unit.y - missile.y, missile_info.range, now_ms).hops = missile.hops - 1;
            }
        }
        if (missile_info.hit_func == 2 || missile_info.hit_func == 4 || missile_info.hit_func == 9 || missile_info.hit_func == 20 || missile_info.hit_func == 47) {
            missile.target = int(monster_index);
            burst(missile, now_ms);
            if (missile_info.hit_func == 47) return false;             // the boulder rolls on (FUN_005ac550 returns 1)
            return true;
        }
        return missile_info.collide_kill && !(missile_info.pierce && int(rng(100)) < int(actor->psum[d2d::d2s::kPierceIndex]));
    }

auto Fight::strike(const Missile& missile, std::size_t monster_index, std::uint32_t now_ms, int freeze ) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || !monsters[monster_index].alive()) return;
        const auto target = target_of(monster_index);
        // A merc's skill missile (Fire Arrow): its weapon, attack rating and
        // level; its skill's calcs see no synergies (mercs have none).
        const int clvl = missile.by_merc ? actor->merc_st.level : int(actor->character.stats.get(d2d::d2s::kLevel));
        const auto fighter = missile.by_merc ? merc_fighter() : actor->player_combat;
        const auto env = missile.by_merc ? merc_calc_env() : calc_env();
        auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = missile.info->etype < 0 ? 0 : missile.info->etype, .elo = missile.fixed, .ehi = missile.fixed }
                : missile.info->skill.empty() ? row_damage(*missile.info, missile.level, env, missile.skill)
                                        : d2d::rules::missile_damage(game_data->skills, *skill, env, missile.level);
        d2d::rules::Blow blow{ .hit = true };
        if (damage.srcdam > 0) {
            d2d::rules::Swing swing;
            swing.srcdam = damage.srcdam;
            swing.ed_pct = missile.ed_pct;
            blow = d2d::rules::player_blow(fighter, target, clvl, rng, swing);
        } else if (missile.info->to_hit) {
            blow.hit = int(rng(100)) < d2d::rules::hit_chance(fighter.attack_rating, target.armor_class, clvl, target.level);
        }
        if (blow.hit) blow = d2d::rules::missile_blow(damage, target, pierce(), rng, blow);
        blow.hit_class = missile.info->hit_class | skill->hit_class;
        if (monsters[monster_index].half_freeze) freeze /= 2;          // stat 0x76 (the Countess)
        if (blow.hit && freeze > 0) blow.chill_ticks = std::max(blow.chill_ticks, freeze);
        land(monster_index, blow, true, now_ms);
        // A freeze stands it as a stun does, without the stun's guards.
        if (blow.hit && freeze > 0) monsters[monster_index].stun_until = std::max(monsters[monster_index].stun_until, now_ms + std::uint32_t(freeze) * 40);
    }

auto Fight::area(const Missile& missile, float x, float y, int radius, std::uint32_t now_ms, int freeze ) -> void {
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(std::max(radius, 1))) strike(missile, j, now_ms, freeze);
    }

auto Fight::burst(Missile& missile, std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || missile.frame >= 0) return;
        missile.frame = int(now_ms / 40);
        const auto& missile_info = *missile.info;
        const auto hit_sub = missile_info.hit_sub.empty() ? game_data->missiles.end() : game_data->missiles.find(missile_info.hit_sub);
        const bool sub = hit_sub != game_data->missiles.end();
        // `frames` > 0: the sub-missile's Range set outright (create flag 0x8000, FUN_0059fa30).
        auto sub_at = [&](float x, float y, float dx, float dy, int frames = 0) -> Missile& {
            return launch(hit_sub->second, *skill, missile.level, x, y, dx, dy, frames > 0 ? frames : hit_sub->second.range + hit_sub->second.lev_range * missile.level, now_ms);
        };
        auto radius = [&](int par, const d2d::rules::Calc& radius_calc) { return par > 0 ? par : std::max(calc(*skill, radius_calc, missile.level), 1); };
        static constexpr int kSpreadX[18] = { 2, -2, 0, 0, -3, 0, 3, -1, 1, -1, 2, -4, -3, -1, 0, 1, 3, 4 };
        static constexpr int kSpreadY[18] = { -2, -2, 2, 5, 3, 3, 3, 2, 1, -1, -1, -2, -2, -3, -4, -3, -3, -2 };
        switch (missile_info.hit_func) {
            case 1: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->calc[0]), now_ms); break;
            case 3: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms); break;
            case 13: area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms, radius(missile_info.hit_par2, skill->auralen)); break;
            case 2: case 4: case 26: case 36: case 51: if (sub) sub_at(missile.x, missile.y, 0, 0); break;
            case 9: {
                // Immolation Arrow (FUN_005aa250 -> FUN_005a9530): its fire lasts SHitCalc1 frames.
                const int burst_radius = radius(missile_info.hit_par1, skill->calc[0]);
                const int frames = missile_info.hit_calc.empty() ? 0 : calc(*skill, missile_info.hit_calc, missile.level);
                area(missile, missile.x, missile.y, burst_radius, now_ms);
                if (sub)
                    for (int offset_x = -burst_radius; offset_x <= burst_radius; ++offset_x)
                        for (int offset_y = -burst_radius; offset_y <= burst_radius; ++offset_y)
                            if (offset_x * offset_x + offset_y * offset_y <= burst_radius * burst_radius) sub_at(missile.x + float(offset_x) / 5, missile.y + float(offset_y) / 5, 0, 0, frames);
                break;
            }
            case 14: case 47: case 56: {
                area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms);
                // Meteor's (FUN_005aabb0 -> FUN_005aaa90): its fire lasts FUN_004cc7c0's Param3 + (lvl - 1) x Param4 frames.
                // ponytail: 47 / 56 (untraced) keep the sub-row's EMin a frame.
                const int frames = missile_info.hit_func == 14 ? skill->par[2] + (missile.level - 1) * skill->par[3] : 0;
                if (sub)
                    for (std::size_t j = 0; j < 18; j += std::size_t(std::max(missile_info.hit_par2, 1))) {
                        auto& fire = sub_at(missile.x + float(kSpreadX[j]) / 5, missile.y + float(kSpreadY[j]) / 5, 0, 0, frames);
                        if (missile_info.hit_func != 14) fire.fixed = hit_sub->second.emin > 0 ? hit_sub->second.emin : -1;
                    }
                break;
            }
            case 20: {
                if (!sub) break;
                const int burst_radius = radius(missile_info.hit_par1, skill->aurarange);
                int count = radius(missile_info.hit_par2, skill->calc[0]);
                for (std::size_t j = 0; j < monsters.size() && count > 0; ++j)
                    if (int(j) != missile.target && monsters[j].targetable() && std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y) * 5 <= float(burst_radius)) {
                        sub_at(missile.x, missile.y, monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y);
                        --count;
                    }
                break;
            }
            case 22: {
                const float x = missile.target >= 0 ? monsters[std::size_t(missile.target)].unit.x : missile.x;
                const float y = missile.target >= 0 ? monsters[std::size_t(missile.target)].unit.y : missile.y;
                const int burst_radius = radius(missile_info.hit_par1, skill->aurarange);
                area(missile, x, y, burst_radius, now_ms);
                if (sub)
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].targetable() && game_data->monsters.types[std::size_t(monsters[j].type)].undead
                            && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(burst_radius))
                            sub_at(x, y, monsters[j].unit.x - x, monsters[j].unit.y - y);
                break;
            }
            case 29:
                if (sub)
                    for (int k = 0; k < 64; k += std::max(missile_info.hit_par1, 1)) {
                        const float angle = float(k) * 2 * 3.14159265f / 64;
                        sub_at(missile.x, missile.y, std::cos(angle), std::sin(angle));
                    }
                break;
            case 48: if (sub) sub_at(missile.x, missile.y, missile.target_x - missile.x, missile.target_y - missile.y); break;
            default: break;
        }
    }

auto Fight::aura_pulse(std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || !skill->aura || now_ms < actor->aura_next || dead()) return;
        const int lvl = skill_level ? skill_level(actor->aura) : 1;
        if (lvl < 1) return;
        const auto env = calc_env();
        actor->aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        if (skill->srvdofunc == ServerDoFunction::kFriendlyAura) {
            for (std::size_t i = 0; i < skill->aurastat.size(); ++i)
                if (skill->aurastat[i] == 6) {
                    using namespace d2d::d2s;
                    const auto heal = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], env, skill->id, lvl);
                    actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife] + heal);
                }
            return;
        }
        // Redemption (do 82): a corpse within aurarange redeemed at calc1 %,
        // calc2 life and mana back.
        // ponytail: its callback isn't traced; one corpse a pulse (Redemption,
        // level 30: past Act 1).
        if (skill->srvdofunc == ServerDoFunction::kRedemption) {
            using namespace d2d::d2s;
            const int radius = calc(*skill, skill->aurarange, lvl);
            for (auto& monster : monsters)
                if (!monster.alive() && !monster.corpse_used && monster.mode == "DD" && std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(radius)) {
                    if (int(rng(100)) < calc(*skill, skill->calc[0], lvl)) {
                        monster.corpse_used = true;
                        const std::int64_t gain = std::int64_t(calc(*skill, skill->calc[1], lvl)) << 8;
                        actor->character.stats.values[kLife] = std::min(actor->character.stats.values[kMaxLife], actor->character.stats.values[kLife] + gain);
                        actor->character.stats.values[kMana] = std::min(actor->character.stats.values[kMaxMana], actor->character.stats.values[kMana] + gain);
                    }
                    break;
                }
            return;
        }
        if ((skill->srvdofunc != ServerDoFunction::kEnemyAura && skill->srvdofunc != ServerDoFunction::kHolyFreeze) || skill->etype < 0 || skill->etype > 4) return;
        const auto damage = d2d::rules::MissileDamage{ .etype = skill->etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, *skill, env, lvl, false, 0, true),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, *skill, env, lvl, true, 0, true),
                                                   .elen = d2d::rules::elem_length(game_data->skills, *skill, env, lvl) };
        const std::array<int, 4> pierce{ int(actor->psum[d2d::d2s::kPassiveFirePierce]), int(actor->psum[d2d::d2s::kPassiveLightningPierce]), int(actor->psum[d2d::d2s::kPassiveColdPierce]), int(actor->psum[d2d::d2s::kPassivePoisonPierce]) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && in_aura(monsters[i]))
                land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms);
    }

auto Fight::in_aura(const Monster& monster) -> bool {
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || !skill->aura) return false;
        const int radius = d2d::rules::eval_calc(game_data->skills, skill->aurarange, calc_env(), skill->id, skill_level ? skill_level(actor->aura) : 1);
        return std::hypot(monster.unit.x - actor->player.x, monster.unit.y - actor->player.y) * 5 <= float(radius);
    }

auto Fight::target_of(std::size_t monster_index) -> d2d::rules::Target {
        auto target = monsters[monster_index].target(*game_data);
        // Its curse and cry (Amplify Damage, Lower Resist, Decrepify, Battle
        // Cry, Inner Sight, Cloak of Shadows): their aurastats as the aura's.
        for (const auto* effect : { &monsters[monster_index].curse, &monsters[monster_index].cry })
            if (const auto* skill = effect->skill >= 0 ? game_data->skills.get(effect->skill) : nullptr)
                apply_target_stats(*skill, effect->level, target);
        const auto* skill = game_data->skills.get(actor->aura);
        if (!skill || (skill->srvdofunc != ServerDoFunction::kEnemyAura && skill->srvdofunc != ServerDoFunction::kHolyFreeze) || !in_aura(monsters[monster_index])) return target;
        apply_target_stats(*skill, skill_level ? skill_level(actor->aura) : 1, target);
        return target;
    }

auto Fight::apply_target_stats(const d2d::rules::Skill& skill_row, int lvl, d2d::rules::Target& target) -> void {
        const auto* skill = &skill_row;
        const auto env = calc_env();
        for (std::size_t k = 0; k < skill->aurastat.size(); ++k) {
            const int value = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, lvl);
            switch (skill->aurastat[k]) {
                case 36: target.res[0] += value; break;  case 37: target.res[1] += value; break;
                case 39: target.res[2] += value; break;  case 41: target.res[3] += value; break;
                case 43: target.res[4] += value; break;  case 45: target.res[5] += value; break;
                case 171: target.armor_class += target.armor_class * value / 100; break;
                case 31: target.armor_class = std::max(target.armor_class + value, 0); break;   // Inner Sight's armorclass
                default: break;
            }
        }
    }

auto Fight::summon_skill(const d2d::rules::Skill& skill) const -> bool {
        if (skill.srvdofunc == ServerDoFunction::kRevive) return true;                  // Revive: the corpse's own monster
        return !skill.summon.empty() && game_data->monsters.row(skill.summon) >= 0
            && (skill.srvdofunc == ServerDoFunction::kGolem || skill.srvdofunc == ServerDoFunction::kIronGolem || skill.srvdofunc == ServerDoFunction::kRaiseSkeleton || skill.srvdofunc == ServerDoFunction::kValkyrie || skill.srvdofunc == ServerDoFunction::kRaven
                || skill.srvdofunc == ServerDoFunction::kHydra || skill.srvdofunc == ServerDoFunction::kDecoy || skill.srvdofunc == ServerDoFunction::kMirrorImage || skill.srvdofunc == ServerDoFunction::kBoneWall || skill.srvdofunc == ServerDoFunction::kBonePrison
                || skill.srvdofunc == ServerDoFunction::kVine
                || (skill.srvdofunc == ServerDoFunction::kDruidSummon && (skill.pettype == "spiritwolf" || skill.pettype == "fenris" || skill.pettype == "grizzly" || skill.pettype == "totem"))
                || (skill.srvdofunc == ServerDoFunction::kTrap && trap_shot(skill) >= 0));
    }

auto Fight::trap_shot(const d2d::rules::Skill& skill) const -> int {
        for (const auto& name : skill.sumskill)
            if (const auto found = game_data->skills.by_name.find(name); !name.empty() && found != game_data->skills.by_name.end())
                if (const auto* trap_skill = game_data->skills.get(found->second); trap_skill && skill_missile(*trap_skill, true)) return trap_skill->id;
        return -1;
    }

auto Fight::cast_summon(int skill, float target_x, float target_y, std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || actor->pmode >= 0 || !summon_skill(*skill_row)) return false;
        if (skill_row->target_corpse && corpse_near(target_x, target_y) < 0) return false;
        const int lvl = cast_level(skill);
        const int cost = cost_of(*skill_row, lvl);
        if (lvl > 0 && actor->character.stats.values[kMana] < cost) need_mana(now_ms);
        if (lvl <= 0 || actor->character.stats.values[kMana] < cost) return false;
        pay(*skill_row, cost);
        actor->swing_skill = skill;
        actor->cast_x = target_x; actor->cast_y = target_y;
        actor->attack_mon = -1;
        actor->player.dir = direction16(target_x - actor->player.x, target_y - actor->player.y);
        set_pmode(kModeSC, now_ms);
        actor->pstruck = false;
        return true;
    }

auto Fight::corpse_near(float x, float y) const -> int {
        int best = -1; float best_distance = 2.f;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const auto& corpse = monsters[i]; !corpse.alive() && !corpse.corpse_used && corpse.mode == "DD")
                if (const float distance = std::hypot(corpse.unit.x - x, corpse.unit.y - y); distance < best_distance) { best_distance = distance; best = int(i); }
        return best;
    }

auto Fight::skill_named(std::string_view name) const -> const d2d::rules::Skill* {
        if (name.empty()) return nullptr;
        if (const auto found = game_data->skills.by_name.find(std::string(name)); found != game_data->skills.by_name.end()) return game_data->skills.get(found->second);
        for (const auto& [skill_name, id] : game_data->skills.by_name)
            if (std::ranges::equal(skill_name, name, [](char left, char right) { return std::tolower(std::uint8_t(left)) == std::tolower(std::uint8_t(right)); }))
                return game_data->skills.get(id);
        return nullptr;
    }

auto Fight::pet_missile(const d2d::rules::Skill& skill, int variant ) const -> const GameData::MissileInfo* {
        std::string name = skill.srvmissile.empty() ? skill.srvmissilea : skill.srvmissile;
        if (skill.srvdofunc == ServerDoFunction::kVariantMissile && !name.empty() && name.back() == '1') name.back() = char('1' + std::clamp(variant, 0, 3));
        const auto found = game_data->missiles.find(name);
        return name.empty() || found == game_data->missiles.end() ? nullptr : &found->second;
    }

auto Fight::summon(const d2d::rules::Skill& skill, int lvl, float x, float y, std::uint32_t now_ms) -> void {
        int type = game_data->monsters.row(skill.summon);
        const auto env = calc_env();
        if (skill.target_corpse) {
            const int corpse = corpse_near(x, y);
            if (corpse < 0) return;
            monsters[std::size_t(corpse)].corpse_used = true;
            x = monsters[std::size_t(corpse)].unit.x; y = monsters[std::size_t(corpse)].unit.y;
            if (skill.srvdofunc == ServerDoFunction::kRevive) type = monsters[std::size_t(corpse)].type;
        }
        if (type < 0) return;
        // Bone Wall (do 60, FUN_005c58b0): one on the point, then its makers
        // (srvmissilea, missile do 13) lay calc2 / 2 more each way across
        // the caster's line. Bone Prison (62, FUN_005c5d00): twelve round the
        // point at 0x6e304c / 0x6e307c.
        // ponytail: the makers are laid at once, a subtile apart.
        if (skill.srvdofunc == ServerDoFunction::kBoneWall) {
            const float perp_x = y - actor->player.y, perp_y = actor->player.x - x, distance = std::max(std::hypot(perp_x, perp_y), 0.01f);
            const int count = std::max(calc(skill, skill.calc[1], lvl), 2) / 2;
            for (int k = -count; k <= count; ++k) summon_one(skill, type, lvl, env, x + perp_x / distance * float(k) * 0.4f, y + perp_y / distance * float(k) * 0.4f, now_ms);
            return;
        }
        if (skill.srvdofunc == ServerDoFunction::kBonePrison) {
            static constexpr int kRingX[12] = { -1, 1, 3, 4, 4, 3, -1, 1, -3, -4, -4, -3 };
            static constexpr int kRingY[12] = { -4, -4, -3, -1, 1, 3, 4, 4, 3, -1, 1, -3 };
            for (std::size_t k = 0; k < 12; ++k) summon_one(skill, type, lvl, env, x + float(kRingX[k]) / 5, y + float(kRingY[k]) / 5, now_ms);
            return;
        }
        // Hydra (do 144, FUN_005ca910): three at once.
        for (int k = skill.srvdofunc == ServerDoFunction::kHydra ? 3 : 1; k > 0; --k) summon_one(skill, type, lvl, env, x + float(k - 1) * 0.6f, y, now_ms);
    }

auto Fight::summon_one(const d2d::rules::Skill& skill, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t now_ms) -> void {
        const int max = std::max(d2d::rules::eval_calc(game_data->skills, skill.petmax, env, skill.id, lvl), 1);
        auto same = [&](const Pet& pet) { return game_data->skills.get(pet.skill) && game_data->skills.get(pet.skill)->pettype == skill.pettype; };
        while (skill.pettype != "none" && std::ranges::count_if(actor->pets, same) >= max) actor->pets.erase(std::ranges::find_if(actor->pets, same));
        const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel));
        const int bonus = skill.srvdofunc == ServerDoFunction::kRaven || skill.srvdofunc == ServerDoFunction::kDruidSummon ? d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl) : 0;
        const int plvl = std::max(std::min(clvl, clvl * 3 / 4 + bonus), 1);
        Pet pet;
        auto& monster = pet.monster;
        monster.type = type;
        monster.npc = game_data->mon_npc[std::size_t(type)];
        monster.unit.shape = monster.npc.shape.owned();   // a pet: +0xc4 bit 31 (FUN_0056d8d0)
        const auto& type_info = game_data->monsters.types[std::size_t(type)];
        for (std::size_t layer = 0; layer < 16; ++layer)
            if (!type_info.parts[layer].empty()) monster.npc.comp[layer] = type_info.parts[layer].front();
        monster.difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        monster.stats = d2d::rules::monster_stats(game_data->monsters, type, monster.difficulty, rng);   // spawned at its own level (FUN_005b2f20)
        // Its life and damage straight from MonStats' columns (a Raise
        // Skeleton skeleton: 21 life, 1-2 damage in Normal; 30 / 42 in
        // Nightmare / Hell), not scaled by MonLvl.
        // ponytail: FUN_005b2f20's pet branch isn't traced; the published
        // pet values are the raw columns.
        // Revive (do 58, FUN_005c56c0) raises the monster as it was: its own
        // stats at its level, life and level brought down to the owner's
        // when it's above it.
        if (skill.srvdofunc != ServerDoFunction::kRevive) {
            const auto& per_difficulty = game_data->monsters.types[std::size_t(type)].diff[std::size_t(monster.difficulty)];
            monster.stats.hit_points = std::max(rng.range(per_difficulty.min_hp, std::max(per_difficulty.max_hp, per_difficulty.min_hp)), 1);
            monster.stats.a1_min = per_difficulty.a1_min; monster.stats.a1_max = std::max(per_difficulty.a1_max, per_difficulty.a1_min);
            monster.stats.a2_min = per_difficulty.a2_min; monster.stats.a2_max = std::max(per_difficulty.a2_max, per_difficulty.a2_min);
        }
        // The Druid's and Raven's hit with the skill's physical damage (their
        // MonStats rows have none).
        // ponytail: where game.exe hands the skill's damage to the pet isn't
        // traced.
        if (monster.stats.a1_max == 0 && skill.maxdam > 0) {
            const auto damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
            monster.stats.a1_min = damage.phys_lo >> 8; monster.stats.a1_max = std::max(damage.phys_hi >> 8, monster.stats.a1_min);
        }
        monster.stats.level = skill.srvdofunc == ServerDoFunction::kRevive ? std::min(monster.stats.level, clvl) : skill.srvdofunc == ServerDoFunction::kMirrorImage || skill.srvdofunc == ServerDoFunction::kDecoy ? clvl : plvl;
        if (skill.srvdofunc != ServerDoFunction::kRevive && !game_data->monsters.lvl.empty()) {
            const auto& level_row = game_data->monsters.lvl[std::min<std::size_t>(std::size_t(plvl), game_data->monsters.lvl.size() - 1)];
            monster.stats.armor_class = level_row.armor_class[std::size_t(monster.difficulty)];
            monster.stats.to_hit = level_row.to_hit[std::size_t(monster.difficulty)];
        }
        // FUN_005c4470: the skill's passive stats (base) and aurastats (its
        // aurastate) onto the pet, at the skill's level through the owner's
        // calcs — Skeleton Mastery, Golem Mastery and the golems' synergies
        // come in that way — then life (with maxhp) + calc1 %, then its
        // sumskills at their sumsk calcs.
        std::unordered_map<int, int> pet_stats;
        for (std::size_t k = 0; k < 5; ++k)
            if (skill.passive_stat[k] >= 0) pet_stats[skill.passive_stat[k]] += calc(skill, skill.passive_calc[k], lvl);
        for (std::size_t k = 0; k < 6; ++k)
            if (skill.aurastat[k] >= 0) pet_stats[skill.aurastat[k]] += calc(skill, skill.aura_calc[k], lvl);
        auto stat = [&](int id) { const auto found = pet_stats.find(id); return found == pet_stats.end() ? 0 : found->second; };
        using namespace d2d::d2s;
        monster.stats.hit_points += stat(kMaxLife) >> 8;                                               // maxhp, 256ths
        monster.stats.hit_points += int(std::int64_t(monster.stats.hit_points) * calc(skill, skill.calc[0], lvl) / 100);
        monster.hit_points = monster.stats.hit_points = std::max(monster.stats.hit_points, 1);
        for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max }) { *damage += stat(kNormalDamage); *damage += *damage * stat(kDamagePercent) / 100; }   // item_normaldamage, damagepercent
        monster.stats.to_hit += stat(kToHit); monster.stats.to_hit += monster.stats.to_hit * stat(kToHitPercent) / 100;              // tohit, item_tohit_percent
        monster.stats.armor_class += stat(kArmorClass); monster.stats.armor_class += monster.stats.armor_class * (stat(kArmorPercent) + stat(kSkillArmorPercent)) / 100;   // armorclass, item / skill_armor_percent
        const auto& base_res = type_info.diff[std::size_t(monster.difficulty)].res;
        pet.res = { base_res[2] + stat(kFireResist), base_res[3] + stat(kLightningResist), base_res[4] + stat(kColdResist), base_res[5] + stat(kPoisonResist) };
        pet.thorns = stat(kThornsPercent); pet.fire_lo = stat(kFireMinDamage); pet.fire_hi = std::max(stat(kFireMaxDamage), pet.fire_lo); pet.speed_pct = stat(kVelocityPercent);
        for (std::size_t k = 0; k < 5; ++k) {
            const auto* k_s = skill_named(skill.sumskill[k]);
            const int skill_level_value = k_s ? calc(skill, skill.sumsk_calc[k], lvl) : 0;
            if (!k_s || skill_level_value < 1) continue;
            if (k_s->aura) { pet.aura = k_s->id; pet.aura_level = skill_level_value; }
            else if (pet_missile(*k_s) && skill.srvdofunc != ServerDoFunction::kVine && skill.srvdofunc != ServerDoFunction::kTrap) { pet.ranged = k_s->id; pet.ranged_level = skill_level_value; }
        }
        monster.unit.x = monster.home_x = x; monster.unit.y = monster.home_y = y;
        monster.unit.dir = actor->player.dir;
        pet.skill = skill.id;
        pet.where = level;
        pet.variant = int(rng(4));
        switch (skill.srvdofunc) {
            case ServerDoFunction::kRevive: pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40; break;   // Revive: calc2 frames
            case ServerDoFunction::kHydra:                                                                                   // Hydra: Param1 frames, it stands
                pet.until = now_ms + std::uint32_t(std::max(skill.par[0], 1)) * 40;
                if (const auto* hydra = skill_named("HydraMissile")) { pet.ranged = hydra->id; pet.ranged_level = lvl; }
                break;
            case ServerDoFunction::kDecoy:                                  // Decoy (FUN_005dc000): owner's life x calc3 %, calc2 frames, it stands
                monster.hit_points = monster.stats.hit_points = std::max(int(actor->character.stats.fixed(d2d::d2s::kMaxLife) * calc(skill, skill.calc[2], lvl) / 100), 1);
                pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40;
                pet.idle = true;
                break;
            case ServerDoFunction::kMirrorImage: pet.mirror = true; break;          // Shadow Warrior / Master (FUN_005d6e70): the owner's level and gear
            case ServerDoFunction::kRaven: pet.hits = std::max(skill.par[4], 1); break;   // Raven: its attacks
            case ServerDoFunction::kBoneWall: case ServerDoFunction::kBonePrison: pet.idle = true; pet.until = now_ms + 24000; break;   // bone: it stands (24 s: ponytail, the published time)
            case ServerDoFunction::kVine: monster.stats.level = std::max(calc(skill, skill.calc[1], lvl), 1); break;   // Vines (FUN_005c6a80): calc2 is its level
            default: break;
        }
        if (skill.srvdofunc == ServerDoFunction::kTrap) {                             // a trap (FUN_005d6170 -> FUN_005d5e10)
            pet.shot_skill = trap_shot(skill);
            for (std::size_t k = 0; k < 5; ++k)
                if (game_data->skills.by_name.contains(skill.sumskill[k]) && game_data->skills.by_name.at(skill.sumskill[k]) == pet.shot_skill)
                    pet.shot_level = std::max(d2d::rules::eval_calc(game_data->skills, skill.sumsk_calc[k], env, skill.id, lvl), 1);
            const int calc4 = d2d::rules::eval_calc(game_data->skills, skill.calc[3], env, skill.id, lvl);
            pet.shots = calc4 > 0 ? calc4 : std::max(skill.par[0], 1);
        }
        // The AI control a traced think starts from (the AIs' inits,
        // FUN_005ea510 / FUN_005eaf50; the Hydra's last frame, the Raven's
        // hits, a trap's shots as d2d counts them).
        if (const auto& ai_name = game_data->monsters.types[std::size_t(monster.type)].ai_name; ai_name == "Hydra") pet.ctrl[0] = int(pet.until / 40);
        else if (ai_name == "Raven") pet.ctrl[0] = pet.hits;
        else if (ai_name == "AssassinSentry" || ai_name == "DeathSentry") pet.ctrl = { -1, pet.shots, lvl };
        // The Shadows' skills: their MonStats Skill1..8 at Sk*lvl +
        // MonsterSkillBonus, in Sk*mode (FUN_004ae8d0), then the Warrior's
        // init (FUN_005eb490: Attack, the owner's class skills it may have)
        // or the Master's (FUN_005ecb70, no skills).
        if (skill.srvdofunc == ServerDoFunction::kMirrorImage) {
            for (std::size_t slot = 0; slot < type_info.skill.size(); ++slot)
                if (const auto* listed = skill_named(type_info.skill[slot]); listed && type_info.sk_lvl[slot] > 0) {
                    shadow_give(pet, listed->id, d2d::rules::monster_skill_level(type_info.sk_lvl[slot], monster.difficulty));
                    std::ranges::find(pet.skill_list, listed->id, &d2d::rules::ShadowListed::id)->mode = monster_mode(type_info.sk_mode[slot]);
                }
            if (type_info.ai_name == "ShadowWarrior") {
                const auto& class_ids = game_data->skills.class_ids[std::size_t(std::clamp(actor->character.character_class, 0, 6))];
                const bool has_attack = std::ranges::contains(pet.skill_list, 0, &d2d::rules::ShadowListed::id);
                const auto gifts = d2d::rules::shadow_warrior_init(
                    pet.ctrl, true, lvl, has_attack, class_ids, [&](int id) { return shadow_may_have(game_data->skills.get(id), type_info, skill); },
                    [&](int id) { return skill_base ? skill_base(id) : 0; });
                for (const auto& gift : gifts) shadow_give(pet, gift.skill, gift.level);
                pet.left_skill = 0;
            } else pet.ctrl = d2d::rules::shadow_master_init(type_info.diff[std::size_t(monster.difficulty)].aip[2], monster.seed);
        }
        set_mode(*game_data, monster, "NU", now_ms);
        pet.monster.id = next_id++;                              // a unit id as a monster's (a guided missile's hold on it)
        actor->pets.push_back(std::move(pet));
    }

auto Fight::trap_turn(Pet& pet, std::uint32_t now_ms) -> void {
        auto& monster = pet.monster;
        if (now_ms < monster.next_act) return;
        const auto& per_difficulty = game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(monster.difficulty)];
        monster.next_act = now_ms + std::uint32_t(std::max(per_difficulty.aidel, 1)) * 40;
        const float range = float(per_difficulty.aip[3] > 0 ? per_difficulty.aip[3] : 25) / 5;
        int best = -1; float best_distance = range;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const float distance = std::hypot(monsters[i].unit.x - monster.unit.x, monsters[i].unit.y - monster.unit.y); monsters[i].targetable() && distance <= best_distance) { best_distance = distance; best = int(i); }
        // Death Sentry's other skill, 'mon death sentry' (do 55): a corpse in
        // its reach goes up.
        if (const auto* pet_skill = game_data->skills.get(pet.skill))
            for (std::size_t summon_index = 0; summon_index < 5; ++summon_index)
                if (const auto* bone_skill = skill_named(pet_skill->sumskill[summon_index]); bone_skill && bone_skill->srvdofunc == ServerDoFunction::kCorpseExplosion) {
                    const int corpse = corpse_near(monster.unit.x, monster.unit.y);
                    if (corpse >= 0) { corpse_blast(*bone_skill, pet.shot_level, std::size_t(corpse), now_ms); break; }
                }
        if (best < 0) return;
        trap_fire(pet, std::size_t(best), now_ms);
        if (--pet.shots <= 0) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); }
    }

// A trap's shot (its shot skill's missile) at monster `target`.
auto Fight::trap_fire(Pet& pet, std::size_t target, std::uint32_t now_ms) -> void {
        auto& monster = pet.monster;
        const auto* skill = game_data->skills.get(pet.shot_skill);
        if (!skill) return;
        const auto& missile_info = *skill_missile(*skill, true);
        const auto owner = game_data->skills.by_name.find(missile_info.skill);
        const float dx = monsters[target].unit.x - monster.unit.x, dy = monsters[target].unit.y - monster.unit.y, dist = std::max(std::hypot(dx, dy), 0.01f);
        const float speed = missile_speed(missile_info, pet.shot_level);
        // Inferno Sentry's (do 95, FUN_005cc4e0) is a stream of flames:
        // here eight at once, their reach staggered.
        const int count = skill->srvdofunc == ServerDoFunction::kScatteredBolts ? std::max(d2d::rules::eval_calc(game_data->skills, skill->calc[0], calc_env(), skill->id, pet.shot_level), 1)
                    : skill->srvdofunc == ServerDoFunction::kMonsterBreath ? 8 : 1;
        for (int j = 0; j < count; ++j) {
            const float angle = count > 1 ? (float(rng(81)) - 40) * 3.14159265f / 180 : 0.f;
            const float aim_x = dx * std::cos(angle) - dy * std::sin(angle), aim_y = dx * std::sin(angle) + dy * std::cos(angle);
            Missile x{ &missile_info, monster.unit.x, monster.unit.y, aim_x / dist * speed, aim_y / dist * speed, missile_facing(monster.unit.x, monster.unit.y, aim_x, aim_y), now_ms,
                       now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, {} };
            x.friendly = true; x.caster = actor->id; x.level = pet.shot_level;
            x.skill = owner != game_data->skills.by_name.end() ? owner->second : skill->id;
            if (skill->srvdofunc == ServerDoFunction::kMonsterBreath) { x.velocity_x = dx / dist * speed; x.velocity_y = dy / dist * speed; x.dies = now_ms + std::uint32_t(4 + 3 * j) * 40; }
            missiles.push_back(std::move(x));
        }
    }

auto Fight::pet_foe(const Pet& pet) const -> Foe {
        auto fighter = d2d::rules::simple_fighter(pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit,
                                                  pet.monster.stats.armor_class * (100 + pet_buff_stat(pet, d2d::d2s::kSkillArmorPercent)) / 100);   // Shout's
        static constexpr std::array<int, 4> kResists{ d2d::d2s::kFireResist, d2d::d2s::kLightningResist, d2d::d2s::kColdResist, d2d::d2s::kPoisonResist };
        for (std::size_t k = 0; k < 4; ++k) fighter.res[k] = std::min(pet.res[k] + pet_buff_stat(pet, kResists[k]), 95);   // Fade's on a Shadow
        fighter.thorns_pct = pet.thorns;                             // Iron Golem's thorns
        fighter.dr_pct = std::min(pet_buff_stat(pet, d2d::d2s::kDamageResist), 50);   // a curse's (Amplify Damage)
        const auto& type_info = game_data->monsters.types[std::size_t(pet.monster.type)];
        const bool still = type_info.velocity == 0 && type_info.run == 0 && pet.ranged >= 0;   // a Hydra, like a trap, isn't there to hit
        Foe foe{ pet.monster.unit.x, pet.monster.unit.y, pet.monster.stats.level, pet.monster.alive() && pet.monster.mode != "DT" && pet.shot_skill < 0 && !still && pet.where == level,
                 pet.monster.unit.walking, fighter };
        foe.pet = true; foe.size = type_info.size; foe.threat = type_info.threat; foe.id = pet.monster.id;
        monster_foe_props(*game_data, pet.monster, foe);
        foe.life_pct = int(std::int64_t(pet.monster.hit_points) * 100 / std::max(pet.monster.stats.hit_points, 1));
        const auto& own = type_info.diff[std::size_t(pet.monster.difficulty)].res;
        std::array<int, 5> res{ own[0] + pet_buff_stat(pet, d2d::d2s::kDamageResist), own[1] };
        for (std::size_t k = 0; k < 3; ++k) res[k + 2] = pet.res[k] + pet_buff_stat(pet, kResists[k]);
        foe.pick = unit_pick(*game_data, res, pet.aura, pet.aura_level, d2d::rules::swing_max_damage(pet.monster.stats, pet.monster.swing_mode));
        return foe;
    }

auto Fight::pet_hurt(Pet& pet, int damage, std::uint32_t now_ms) -> void {
        if (damage > 0 && pet.monster.mode != "DT") (void)hurt(*game_data, pet.monster, damage, now_ms);
    }

auto Fight::pets_cross(const Level* from, const Level* destination, float dx, float dy) -> void {
        for (auto& pet : actor->pets)
            if (pet.where == from && pet.shot_skill < 0 && pet.monster.alive()) {
                pet.where = destination; pet.target = -1;
                pet.monster.unit.x -= dx; pet.monster.unit.y -= dy; pet.monster.unit.goal_x -= dx; pet.monster.unit.goal_y -= dy;
                for (auto& [step_x, step_y] : pet.monster.unit.path) { step_x -= dx; step_y -= dy; }
                pet.monster.home_x -= dx; pet.monster.home_y -= dy;
            }
    }

auto Fight::pet_aura(Pet& pet, std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(pet.aura);
        if (!skill || skill->srvdofunc != ServerDoFunction::kEnemyAura || now_ms < pet.aura_next || skill->etype < 0 || skill->etype > 4) return;
        pet.aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        const int radius = calc(*skill, skill->aurarange, pet.aura_level);
        const auto damage = d2d::rules::MissileDamage{ .etype = skill->etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, *skill, calc_env(), pet.aura_level, false),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, *skill, calc_env(), pet.aura_level, true) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - pet.monster.unit.x, monsters[i].unit.y - pet.monster.unit.y) * 5 <= float(radius))
                land(i, d2d::rules::missile_blow(damage, target_of(i), {}, rng), false, now_ms);
    }

auto Fight::pets_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        std::erase_if(actor->pets, [&](const Pet& pet) { return !pet.monster.alive() && pet.monster.mode == "DT" && now_ms >= pet.monster.mode_until; });
        const auto save_class = std::size_t(std::max(actor->character.character_class, 0));
        for (const auto& made : std::exchange(actor->pet_summons, {}))            // a Shadow's traps and Blade Sentinel from last turn
            if (const auto* skill = game_data->skills.get(made.skill)) summon(*skill, made.level, made.x, made.y, now_ms);
        for (auto& pet : actor->pets) {
            auto& monster = pet.monster;
            auto& unit = monster.unit;
            const bool curse_spent = pet.curse.skill >= 0 && now_ms >= pet.curse.until;
            if (curse_spent) pet.curse = {};
            if (std::erase_if(pet.buffs, [&](const SelfState& buff) { return now_ms >= buff.until; }) > 0 || curse_spent) pet_buffs(pet);
            if (!monster.alive() || pet.where != level) continue;
            for (const auto& buff : pet.buffs)                              // a Shadow's Blade Shield, as the player's
                if (const auto* skill = game_data->skills.get(buff.skill); skill && skill->srvdofunc == ServerDoFunction::kBladeShield && now_ms >= pet.blade_next) {
                    pet.blade_next = now_ms + std::uint32_t(std::max(calc(*skill, skill->perdelay_calc, buff.level), 1)) * 40;
                    blade_shield(*skill, buff.level, unit.x, unit.y, actor->player_combat, false, now_ms);
                }
            if (pet.until && now_ms >= pet.until) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); continue; }
            if (monster.mode == "KB") { if (d2d::game::knock_slide(monster, now_ms)) continue; set_mode(*game_data, monster, "GH", now_ms); }   // knocked back, then its get-hit
            const auto& ai_name = game_data->monsters.types[std::size_t(monster.type)].ai_name;
            const bool traced = d2d::rules::traced_pet_ai(ai_name);
            if (pet.shot_skill >= 0 && !traced) { trap_turn(pet, now_ms); continue; }
            if (pet.aura >= 0) pet_aura(pet, now_ms);
            if (pet.idle) continue;                            // Decoy stands where it was cast
            if (const auto* skill = game_data->skills.get(pet.skill); !traced && skill && skill->srvdofunc == ServerDoFunction::kVine && now_ms >= pet.aura_next) {
                // The vines: Poison Creeper poisons what's within 1.5 cells
                // with the skill's poison; Carrion Vine / Solar Creeper eat a
                // corpse within 5 cells for Param6 seconds, giving Param5 %
                // of max life / mana a second.
                // ponytail: their AI (Vines, CycleOfLife) and Vine Attack /
                // the cyclers (do 130, st 63) aren't traced: the published
                // behaviour.
                pet.aura_next = now_ms + 1000;
                using namespace d2d::d2s;
                if (skill->etype == 3) {
                    const auto damage = d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), pet.monster.stats.level);
                    for (std::size_t j = 0; j < monsters.size(); ++j)
                        if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - unit.x, monsters[j].unit.y - unit.y) < 1.5f)
                            land(j, d2d::rules::missile_blow(damage, target_of(j), pierce(), rng), false, now_ms);
                } else if (pet.hits > 0 || [&] {
                               const int corpse = corpse_near(unit.x, unit.y);
                               if (corpse < 0 || std::hypot(monsters[std::size_t(corpse)].unit.x - unit.x, monsters[std::size_t(corpse)].unit.y - unit.y) > 5) return false;
                               monsters[std::size_t(corpse)].corpse_used = true;
                               pet.hits = std::max(skill->par[5], 1);
                               return true;
                           }()) {
                    --pet.hits;
                    const bool life = skill->name == "Cycle of Life";
                    auto& value = actor->character.stats.values[life ? kLife : kMana];
                    value = std::min(actor->character.stats.values[life ? kMaxLife : kMaxMana], value + actor->character.stats.values[life ? kMaxLife : kMaxMana] * skill->par[4] / 100);
                }
            }
            if (monster.mode != "NU" && monster.mode != "WL" && monster.mode != "RN" && monster.mode != "GH" && monster.mode != "DT") {
                if (!monster.struck && (pet.target >= 0 || pet.cast >= 0) && now_ms >= unit.mode_ms + game_data->npc_timing(monster.npc, monster.mode).action_ms()) {
                    monster.struck = true;
                    const auto monster_index = std::size_t(std::max(pet.target, 0));
                    if (pet.cast >= 0) {
                        pet_cast(pet, now_ms, crowd);
                    } else if (const auto* missile_info = pet.ranged >= 0 ? pet_missile(*game_data->skills.get(pet.ranged), pet.variant) : nullptr; missile_info && monsters[monster_index].alive()) {
                        Missile x{ missile_info, unit.x, unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(missile_info->range, 1)) * 40, {} };
                        const float dx = monsters[monster_index].unit.x - unit.x, dy = monsters[monster_index].unit.y - unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                        x.velocity_x = dx / distance * missile_speed(*missile_info, pet.ranged_level); x.velocity_y = dy / distance * missile_speed(*missile_info, pet.ranged_level);
                        x.dir = missile_facing(unit.x, unit.y, dx, dy); x.to = { monsters[monster_index].unit.x, monsters[monster_index].unit.y };
                        x.friendly = true; x.caster = actor->id; x.level = pet.ranged_level; x.skill = pet.ranged;
                        if (const auto* missile_skill_row = skill_named(missile_info->skill)) x.skill = missile_skill_row->id;   // 'hydra' carries the Hydra skill's damage
                        pending.push_back(std::move(x));
                    } else if (monsters[monster_index].targetable() && std::hypot(monsters[monster_index].unit.x - unit.x, monsters[monster_index].unit.y - unit.y) <= kMeleeReach + 0.3f) {
                        auto target = target_of(monster_index);
                        // A Shadow swings the owner's blow (its copied gear); its skills are shadow_cast's.
                        auto blow = d2d::rules::player_blow(pet.mirror ? actor->player_combat : d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit),
                                                         target, monster.stats.level, rng);
                        if (!traced && pet.hits > 0 && --pet.hits == 0) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); }   // a Raven's last
                        if (blow.hit && pet.fire_hi > 0) blow.damage += d2d::rules::resisted(rng.range(pet.fire_lo, pet.fire_hi), target.res[2]);   // Fire Golem
                        pet_strike(pet, monster_index, blow, now_ms);
                    }
                }
                if (now_ms < monster.mode_until) continue;
                pet.cast = -1;
                set_mode(*game_data, monster, "NU", now_ms);
            }
            if (monster.mode == "GH" && now_ms < monster.mode_until) continue;
            if (ai_name == "NecroPet") { necropet_turn(pet, now_ms, elapsed, crowd); continue; }
            if (traced) { pet_think_turn(pet, now_ms, elapsed, crowd); continue; }
            if (pet.target >= 0 && !monsters[std::size_t(pet.target)].alive()) pet.target = -1;
            if (pet.target < 0) {
                float best = 1e9f;
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    const auto& other = monsters[i];
                    if (!other.alive()) continue;
                    const float to_player = std::hypot(other.unit.x - actor->player.x, other.unit.y - actor->player.y), to_pet = std::hypot(other.unit.x - unit.x, other.unit.y - unit.y);
                    if (((other.aware && to_player < 8) || to_pet < 4) && to_pet < best) { best = to_pet; pet.target = int(i); }
                }
            }
            const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
            const float speed = cells_per_sec(float(std::max(type_info.run, type_info.velocity))) * float(100 + pet.speed_pct) / 100;
            if (speed <= 0) {                                // a Hydra: it stands and shoots what comes in range
                if (pet.target >= 0 && std::hypot(monsters[std::size_t(pet.target)].unit.x - unit.x, monsters[std::size_t(pet.target)].unit.y - unit.y) > 6.f) pet.target = -1;
                for (std::size_t i = 0; pet.target < 0 && i < monsters.size(); ++i)
                    if (monsters[i].targetable() && std::hypot(monsters[i].unit.x - unit.x, monsters[i].unit.y - unit.y) <= 6.f) pet.target = int(i);
                if (pet.target >= 0 && now_ms >= monster.next_act) {
                    const auto& target = monsters[std::size_t(pet.target)];
                    unit.dir = direction16(target.unit.x - unit.x, target.unit.y - unit.y);
                    set_mode(*game_data, monster, game_data->npc_timing(monster.npc, "A1").directions ? "A1" : "NU", now_ms);
                    monster.struck = false;
                    monster.next_act = now_ms + std::uint32_t(std::max(game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(monster.difficulty)].aidel, 15)) * 40;
                }
                continue;
            }
            if (pet.target >= 0) {
                const auto& target = monsters[std::size_t(pet.target)];
                const float dx = target.unit.x - unit.x, dy = target.unit.y - unit.y;
                if (std::hypot(dx, dy) <= (pet.ranged >= 0 ? 6.f : kMeleeReach)) {
                    unit.dir = direction16(dx, dy);
                    unit.path.clear(); unit.walking = false;
                    set_mode(*game_data, monster, "A1", now_ms);
                    monster.struck = false;
                    continue;
                }
                if (unit.path.empty() || std::hypot(unit.goal_x - target.unit.x, unit.goal_y - target.unit.y) > 1.f)
                    set_off(*level, unit, target.unit.x, target.unit.y, true, crowd);
                if (!walk_on(*level, unit, speed * elapsed, crowd)) pet.target = -1;
            } else {
                pet_follow(*level, unit, actor->player.x, actor->player.y, cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f, now_ms, elapsed, crowd);
            }
            const std::string_view want = unit.walking ? "WL" : "NU";
            if (monster.mode != want) set_mode(*game_data, monster, want, now_ms);
        }
    }

// A pet's walk between thinks (a walk at its target ends in reach); true
// while it walks or isn't due a think.
auto Fight::pet_walking(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> bool {
        auto& monster = pet.monster;
        auto& unit = monster.unit;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        if (pet.target >= 0 && !monsters[std::size_t(pet.target)].alive()) { pet.target = -1; pet.chase = false; }
        // A walk at its foe ends in melee as its think tests it (FUN_00622c40:
        // the units' distance within MonStats2 MeleeRng + 1).
        const auto in_melee = [&](const Monster& foe) {
            const auto sub = [](float cells) { return int(std::floor(cells * 5)); };
            return d2d::rules::unit_distance(sub(foe.unit.x) - sub(unit.x), sub(foe.unit.y) - sub(unit.y), std::max(type_info.size, 1),
                                             game_data->monsters.types[std::size_t(foe.type)].size) <= type_info.melee_rng + 1;
        };
        if (unit.walking) {
            if (pet.chase && (pet.target < 0 || in_melee(monsters[std::size_t(pet.target)]))) {
                unit.walking = false; unit.path.clear();
            } else {
                int velocity = monster.mode == "RN" ? type_info.run : type_info.velocity;
                // In a state that looks like a player class (States gfxtype 2:
                // the Shadows' "shadowwarrior", class 6) it moves as that class
                // (FUN_00645270 -> FUN_00621360: CharStats WalkVelocity /
                // RunVelocity); a Shadow's MonStats Velocity is 0.
                if (const auto* summoning = game_data->skills.get(pet.skill); summoning && !summoning->aurastate.empty())
                    if (const auto state = game_data->states.find(summoning->aurastate); state != game_data->states.end() && state->second.gfx_type == 2) {
                        const auto cls = std::size_t(std::clamp(state->second.gfx_class, 0, 6));
                        velocity = monster.mode == "RN" ? game_data->run_velocity[cls] : game_data->walk_velocity[cls];
                    }
                const float pace = float(std::max(100 + monster.move_pct, 10)) * float(100 + pet.speed_pct + pet_buff_stat(pet, d2d::d2s::kVelocityPercent)) / 10000;   // Burst of Speed's on a Shadow
                if (walk_on(*level, unit, cells_per_sec(float(velocity)) * pace * elapsed, crowd)) return true;
            }
            pet.chase = false;
            set_mode(*game_data, monster, "NU", now_ms);
            monster.next_act = now_ms;                                    // a walk's end thinks at once (FUN_005a8030)
        }
        return monster.mode != "NU" || now_ms < monster.next_act;
    }

// A NecroPet's frame outside an attack (the golems, the Valkyrie, the
// skeletons and mages): a walk goes on (a walk at its foe ends in reach),
// then at its think rules::necropet_think. The foe is FUN_005dd7f0's for a
// good monster: mode 5 over the near rooms' monsters (rules::search_near:
// a threat of 2 or more first, else the rest, within 0x23), sight needed
// in a preset room that isn't Outdoors, or once after a walk at a foe found
// no path (flag 0x40); its melee is MeleeRng + 1, its line FUN_005dc640.
// A teleport is room_teleport (mode 3).
// ponytail: the owner's path spot (+0x10) is the player's next path point;
// the crowd counts by merc_gap from this pet's size
// (FUN_005e3900's order of units isn't read); a good monster's own room
// order isn't kept (the search runs over the monsters in fight order).
auto Fight::necropet_turn(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        auto& monster = pet.monster;
        auto& unit = monster.unit;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        const int difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        if (pet_walking(pet, now_ms, elapsed, crowd)) return;
        const int pet_x = sub(unit.x), pet_y = sub(unit.y), size = std::max(type_info.size, 1);
        const bool town = d2d::rules::level_ids::is_town(level->id);
        // The foe (FUN_005dd7f0 -> FUN_005dd0b0 mode 5).
        const int room = room_of(*level, unit.x, unit.y);
        const auto* in_room = room >= 0 ? &level->rooms[std::size_t(room)] : nullptr;
        const bool indoor = in_room && in_room->kind == 2
            && !(std::size_t(in_room->def) < game_data->prest_outdoors.size() && game_data->prest_outdoors[std::size_t(in_room->def)]);
        const bool need_sight = std::exchange(monster.force_sight, false) || indoor;
        const auto wall = [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x04); };
        std::vector<d2d::rules::NearFoe> nears;
        for (const auto& other : monsters) {
            const auto& other_type = game_data->monsters.types[std::size_t(other.type)];
            const int foe_x = sub(other.unit.x), foe_y = sub(other.unit.y);
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ d2d::rules::near_distance(foe_x - pet_x, foe_y - pet_y, other_type.size), other.align, other_type.threat,
                                                                  false, true, !other.targetable(), town });   // FUN_005dca70: no unit flag 0x4, no target
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(foe_x, foe_y, other_type.size, pet_x, pet_y, size, wall);
        }
        const auto pick = d2d::rules::search_near(nears, 2, need_sight);
        const int found = pick.target >= 0 ? pick.target : pick.second;
        d2d::rules::PetView view{ .x = pet_x, .y = pet_y, .size = size, .owner_x = sub(actor->player.x), .owner_y = sub(actor->player.y), .owner_mode = actor->owner_mode,
                                  .cur_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.front().first),
                                  .cur_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.front().second),
                                  .end_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.back().first),
                                  .end_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.back().second),
                                  .ring = actor->footsteps, .cursor = actor->footstep_cursor, .arrive_x = actor->warp_spot.first, .arrive_y = actor->warp_spot.second,
                                  .town = town, .velocity = type_info.velocity, .run = type_info.run };
        view.pets = int(std::ranges::count_if(actor->pets, [](const Pet& other) { return other.monster.alive(); }));
        view.crowd = int(std::ranges::count_if(actor->pets, [&](const Pet& other) {
            return &other != &pet && other.monster.alive() && other.where == level
                && d2d::rules::merc_gap(pet_x, pet_y, size, sub(other.monster.unit.x), sub(other.monster.unit.y)) < 2;
        }));
        if (found >= 0) {
            const auto& foe = monsters[std::size_t(found)];
            const auto& foe_type = game_data->monsters.types[std::size_t(foe.type)];
            view.foe = true;
            view.foe_x = sub(foe.unit.x); view.foe_y = sub(foe.unit.y);
            view.foe_distance = pick.target >= 0 ? pick.best : pick.second_best;
            view.foe_melee = d2d::rules::unit_distance(view.foe_x - pet_x, view.foe_y - pet_y, size, foe_type.size) <= type_info.melee_rng + 1;
            view.clear = d2d::rules::pet_line_clear(pet_x, pet_y, size, view.foe_x, view.foe_y,
                                                    [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x805); });
        }
        auto area_at = [&](int from_x, int from_y, int x, int y) { return int(area_id_at(*game_data, *level, centre(from_x), centre(from_y), centre(x), centre(y))); };   // FUN_0061b130
        std::vector<std::pair<float, float>> route;
        auto try_move = [&](const d2d::rules::MercMove& move) {
            if (move.unit != 0) return found >= 0 && set_off(*level, unit, monsters[std::size_t(found)].unit.x, monsters[std::size_t(found)].unit.y, true, crowd);
            route = move_route(*level, unit, pet_x, pet_y, move, crowd);
            return !route.empty();
        };
        using Kind = d2d::rules::PetAct::Kind;
        const auto act = d2d::rules::necropet_think(view, monster.seed, area_at, try_move);
        monster.force_sight = act.unreachable;
        const int aidel = type_info.diff[std::size_t(difficulty)].aidel > 0 ? type_info.diff[std::size_t(difficulty)].aidel : 15;
        if (act.kind == Kind::moved || act.kind == Kind::chase) {
            set_mode(*game_data, monster, act.move.mode == d2d::rules::kMonsterRun ? "RN" : "WL", now_ms);
            if (act.kind != Kind::chase) unit.path = route;
            unit.walking = true;
            if (act.kind == Kind::moved) { unit.goal_x = centre(act.move.x); unit.goal_y = centre(act.move.y); unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false; }
            monster.move_pct = act.move.pct;
            pet.chase = act.kind == Kind::chase;
            pet.target = pet.chase ? found : -1;
        } else if (act.kind == Kind::swing && found >= 0) {
            pet.target = found;
            unit.dir = direction16(monsters[std::size_t(found)].unit.x - unit.x, monsters[std::size_t(found)].unit.y - unit.y);
            unit.path.clear(); unit.walking = false;
            set_mode(*game_data, monster, "A1", now_ms);
            monster.struck = false;
        } else if (act.kind == Kind::teleport) {
            room_teleport(*game_data, *level, crowd, spawning, unit);
            unit.path.clear(); unit.walking = false;
            set_mode(*game_data, monster, "NU", now_ms);
            monster.next_act = now_ms + 5 * 40;
        } else {
            monster.next_act = now_ms + std::uint32_t(act.kind == Kind::stand ? act.frames : aidel) * 40;
        }
    }

// A traced pet's frame outside an attack (rules::traced_pet_ai): a walk
// goes on, then at its think the AI's rules::*_think on a PetScene, its
// follows and follow-or-fight tests by rules::PetFollow as NecroPet's. The
// units: the foe of the mode 5 search (necropet_turn's), the corpse near the
// owner (Fenris: 10 subtiles; the cycle vines: their skill's calc, 5..50)
// or near the Death Sentry. The AI driver's target and the merc-style
// search's are that foe too.
// ponytail: `foe2` is the mode 5 foe, not FUN_005ddc30's; no unit is ever
// poisoned (state 2), slowed (0x3c) or in Fenris's rage (0x8a); the
// owner's life / mana are whole points; Blade Sentinel isn't summoned, so
// its think doesn't run. The Shadows: the Warrior's scene and the Master's
// think below.
auto Fight::pet_think_turn(Pet& pet, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        using d2d::rules::PetUnit;
        auto& monster = pet.monster;
        auto& unit = monster.unit;
        if (pet_walking(pet, now_ms, elapsed, crowd)) return;
        const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
        const int difficulty = std::clamp(actor->character.header.active_difficulty(), 0, 2);
        auto sub = [](float cells) { return int(std::floor(cells * 5)); };
        auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
        const int size = std::max(type_info.size, 1);
        const bool town = d2d::rules::level_ids::is_town(level->id);
        // The foe (as necropet_turn's).
        const int room = room_of(*level, unit.x, unit.y);
        const auto* in_room = room >= 0 ? &level->rooms[std::size_t(room)] : nullptr;
        const bool indoor = in_room && in_room->kind == 2
            && !(std::size_t(in_room->def) < game_data->prest_outdoors.size() && game_data->prest_outdoors[std::size_t(in_room->def)]);
        const bool need_sight = std::exchange(monster.force_sight, false) || indoor;
        const int pet_x = sub(unit.x), pet_y = sub(unit.y);
        const auto wall = [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x04); };
        std::vector<d2d::rules::NearFoe> nears;
        for (const auto& other : monsters) {
            const auto& other_type = game_data->monsters.types[std::size_t(other.type)];
            const int foe_x = sub(other.unit.x), foe_y = sub(other.unit.y);
            auto& entry = nears.emplace_back(d2d::rules::NearFoe{ d2d::rules::near_distance(foe_x - pet_x, foe_y - pet_y, other_type.size), other.align, other_type.threat,
                                                                  false, true, !other.targetable(), town });   // FUN_005dca70: no unit flag 0x4, no target
            entry.blocked = need_sight && entry.distance <= 0x23 && d2d::rules::sight_blocked(foe_x, foe_y, other_type.size, pet_x, pet_y, size, wall);
        }
        const auto pick = d2d::rules::search_near(nears, 2, need_sight);
        const int found = pick.target >= 0 ? pick.target : pick.second;
        // The corpse: near the owner for Fenris (10) and the cycle vines
        // (their skill's calc), near itself for the Death Sentry.
        const auto* summoning = game_data->skills.get(pet.skill);
        const int corpse_reach = summoning && type_info.ai_name != "DruidWolf" ? std::clamp(d2d::rules::eval_calc(game_data->skills, summoning->calc[0], calc_env(), summoning->id, monster.stats.level), 5, 0x32) : 10;
        const bool by_self = type_info.ai_name == "DeathSentry";
        int corpse = corpse_near(by_self ? unit.x : actor->player.x, by_self ? unit.y : actor->player.y);
        if (corpse >= 0 && !by_self && std::hypot(monsters[std::size_t(corpse)].unit.x - actor->player.x, monsters[std::size_t(corpse)].unit.y - actor->player.y) * 5 > float(corpse_reach)) corpse = -1;
        std::array<int, 6> index{ -1, -1, -1, found, found, corpse };   // by PetUnit: a monster
        d2d::rules::PetScene scene;
        scene.cls = monster.npc.hc_idx; scene.owner_id = 1; scene.owner_mode = actor->owner_mode; scene.town = town; scene.frame = int(now_ms / 40);
        std::array<int, 6> sizes{ 0, size, 2, 0, 0, 0 };
        scene.spot[1] = { pet_x, pet_y };
        scene.spot[2] = { sub(actor->player.x), sub(actor->player.y) };
        for (std::size_t k = 3; k < 6; ++k)
            if (index[k] >= 0) {
                const auto& other = monsters[std::size_t(index[k])];
                scene.spot[k] = { sub(other.unit.x), sub(other.unit.y) };
                sizes[k] = game_data->monsters.types[std::size_t(other.type)].size;
            }
        for (std::size_t from = 1; from < 6; ++from)
            for (std::size_t to = 1; to < 6; ++to) {
                const auto [from_x, from_y] = scene.spot[from];
                const auto [to_x, to_y] = scene.spot[to];
                scene.gap[from][to] = d2d::rules::merc_gap(from_x, from_y, sizes[from], to_x, to_y);
                const int shrink = sizes[from] / 2 + sizes[to] / 2;        // FUN_006416d0
                const int across = std::max(std::abs(to_x - from_x) - shrink, 0), down = std::max(std::abs(to_y - from_y) - shrink, 0);
                scene.distance[from][to] = (across <= down ? across + down * 2 : down + across * 2) / 2;
            }
        if (found >= 0) {
            const auto& foe = monsters[std::size_t(found)];
            const auto [foe_x, foe_y] = scene.spot[3];
            scene.foe = scene.foe2 = scene.driver = true;
            scene.foe_distance = scene.foe2_distance = scene.driver_distance = pick.target >= 0 ? pick.best : pick.second_best;
            scene.foe_melee = scene.foe2_melee = scene.driver_melee
                = d2d::rules::unit_distance(foe_x - pet_x, foe_y - pet_y, size, game_data->monsters.types[std::size_t(foe.type)].size) <= type_info.melee_rng + 1;
            scene.clear[3] = scene.clear[4] = d2d::rules::pet_line_clear(pet_x, pet_y, size, foe_x, foe_y,
                                                                         [&](int at_x, int at_y) { return level->blocked(centre(at_x), centre(at_y), 0x805); });
            scene.poison_resist = target_of(std::size_t(found)).res[5];
        }
        for (std::size_t k = 3; k < 6; ++k) {
            if (index[k] < 0) continue;
            const auto& other = monsters[std::size_t(index[k])];
            scene.melee_of[k] = d2d::rules::unit_distance(scene.spot[k].first - pet_x, scene.spot[k].second - pet_y, size, sizes[k]) <= type_info.melee_rng + 1;
            scene.dying[k] = !other.alive();
        }
        scene.nearby = corpse >= 0;
        scene.corpse_id = corpse >= 0 ? monsters[std::size_t(corpse)].id : 0;
        scene.radius = corpse_reach * 2;
        scene.skill_level = std::max(monster.stats.level, 1);
        {
            using namespace d2d::d2s;
            scene.life = int(actor->character.stats.values[kLife] >> 8); scene.max_life = int(actor->character.stats.values[kMaxLife] >> 8);
            scene.mana = int(actor->character.stats.values[kMana] >> 8); scene.max_mana = int(actor->character.stats.values[kMaxMana] >> 8);
        }
        static constexpr std::array<std::string_view, 16> kModes{ "DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC", "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN" };
        auto mode_of = [&](std::string_view token) { const auto found_mode = std::ranges::find(kModes, token); return found_mode == kModes.end() ? 4 : int(found_mode - kModes.begin()); };
        const auto& per_difficulty = type_info.diff[std::size_t(difficulty)];
        for (std::size_t k = 0; k < 5; ++k) scene.aip[k] = per_difficulty.aip[k];
        const auto* skill1 = skill_named(type_info.skill[0]);
        const auto* skill2 = skill_named(type_info.skill[1]);
        scene.skill1 = skill1 ? skill1->id : -1; scene.skill2 = skill2 ? skill2->id : -1;
        scene.mode1 = mode_of(type_info.sk_mode[0]); scene.mode2 = mode_of(type_info.sk_mode[1]);
        scene.skill_mode = scene.mode1;
        scene.velocity = type_info.velocity; scene.run = type_info.run;
        // How it follows: PetFollow on its PetView (as NecroPet's).
        d2d::rules::PetView view{ .x = pet_x, .y = pet_y, .size = size, .owner_x = sub(actor->player.x), .owner_y = sub(actor->player.y), .owner_mode = actor->owner_mode,
                                  .cur_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.front().first),
                                  .cur_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.front().second),
                                  .end_x = sub(actor->player.path.empty() ? actor->player.x : actor->player.path.back().first),
                                  .end_y = sub(actor->player.path.empty() ? actor->player.y : actor->player.path.back().second),
                                  .ring = actor->footsteps, .cursor = actor->footstep_cursor, .arrive_x = actor->warp_spot.first, .arrive_y = actor->warp_spot.second,
                                  .town = town, .velocity = type_info.velocity, .run = type_info.run };
        view.pets = int(std::ranges::count_if(actor->pets, [](const Pet& other) { return other.monster.alive(); }));
        view.crowd = int(std::ranges::count_if(actor->pets, [&](const Pet& other) {
            return &other != &pet && other.monster.alive() && other.where == level
                && d2d::rules::merc_gap(pet_x, pet_y, size, sub(other.monster.unit.x), sub(other.monster.unit.y)) < 2;
        }));
        auto area_under = [&](int from_x, int from_y, int x, int y) { return int(area_id_at(*game_data, *level, centre(from_x), centre(from_y), centre(x), centre(y))); };   // FUN_0061b130
        std::vector<std::pair<float, float>> route;
        int chased = -1;
        auto move_pet = [&](const d2d::rules::MercMove& move) {
            if (move.unit != 0) {
                if (move.unit == int(PetUnit::owner)) return set_off(*level, unit, actor->player.x, actor->player.y, true, crowd);
                const int monster_at = index[std::size_t(move.unit)];
                if (monster_at < 0 || !set_off(*level, unit, monsters[std::size_t(monster_at)].unit.x, monsters[std::size_t(monster_at)].unit.y, true, crowd)) return false;
                chased = monster_at;
                return true;
            }
            route = move_route(*level, unit, pet_x, pet_y, move, crowd);
            return !route.empty();
        };
        struct World {
            decltype(move_pet)& move_to;
            d2d::rules::PetView& view;
            d2d::rules::Rng& seed;
            decltype(area_under)& area_of;
            std::function<void()> teleport_to;
            d2d::rules::PetAct last;
            bool try_move(const d2d::rules::MercMove& move) { return move_to(move); }
            bool follow(int mode, bool run, int pct, int reach) {
                d2d::rules::PetFollow follower(view, seed, area_of, move_to);
                const bool took = follower.follow(mode, run, pct, reach);
                last = follower.act;
                return took;
            }
            bool decide(PetUnit foe, bool melee, bool stay, int reach) {
                d2d::rules::PetFollow follower(view, seed, area_of, move_to);
                const bool took = follower.decide(foe != PetUnit::none, melee, stay, reach);
                last = follower.act;
                return took;
            }
            bool teleport() { teleport_to(); return true; }
            std::function<void()> give;
            void give_hands() { give(); }
            [[nodiscard]] d2d::rules::PetAct followed() const { return last; }
        };
        World world{ move_pet, view, monster.seed, area_under, [&] { room_teleport(*game_data, *level, crowd, spawning, unit); }, {} };
        const auto& ai_name = type_info.ai_name;
        // The Shadow Warrior: the owner's left / right skills (given when its
        // think gets that far, at FUN_005eaf00's level: give_hands), its
        // class, and what Attack and each hand are to its AI (FUN_005ead50 /
        // FUN_005eabf0) once given.
        std::function<void()> give_hands = [] {};
        if (ai_name == "ShadowWarrior") {
            const auto hand = [&](std::uint32_t id) { return game_data->skills.get(int(id)) ? int(id) : -1; };
            scene.left = hand(actor->character.header.left_skill); scene.right = hand(actor->character.header.right_skill);
            scene.owner_class = std::clamp(actor->character.character_class, 0, 6);
            const auto hand_level = [&](int id) { return d2d::rules::shadow_hand_level(pet.ctrl[2], skill_level ? skill_level(id) : 1); };
            give_hands = [this, &pet, hand_level, left = scene.left, right = scene.right] { for (const int id : { left, right }) shadow_give(pet, id, hand_level(id)); };
            scene.pet_attack = std::ranges::contains(pet.skill_list, 0, &d2d::rules::ShadowListed::id);
            scene.aip8_nightmare = type_info.diff[1].aip[7]; scene.aip8_hell = type_info.diff[2].aip[7];
            const std::array<int, 3> hands{ 0, scene.left, scene.right };
            for (std::size_t k = 0; k < hands.size(); ++k) {
                auto& entry = scene.skills[k];
                entry = { .id = hands[k] };
                const auto* row = game_data->skills.get(hands[k]);
                const auto listed = std::ranges::find(pet.skill_list, hands[k], &d2d::rules::ShadowListed::id);
                const bool given = k > 0;                                    // a hand: as it will be once given
                if (!row || (!given && listed == pet.skill_list.end())) continue;
                const int lvl = std::max(given ? hand_level(hands[k]) : listed->level, 1);
                entry.cls = skill_class(*row);
                const auto held = std::ranges::find(pet.charges, hands[k], &Charge::skill);
                const bool buffed = std::ranges::contains(pet.buffs, hands[k], &SelfState::skill);
                const bool charged = !row->aurastate.empty() && (held != pet.charges.end() || buffed);   // in its aurastate: a buff on, or a charge-up's charges
                bool target_in = false;                                      // the driver's target in its auratargetstate (its curse / cry's)
                if (found >= 0 && !row->auratarget.empty())
                    for (const auto* effect : { &monsters[std::size_t(found)].curse, &monsters[std::size_t(found)].cry })
                        if (const auto* effect_skill = game_data->skills.get(effect->skill); effect_skill && effect_skill->auratarget == row->auratarget && now_ms < effect->until) target_in = true;
                entry.ai_ok = d2d::rules::shadow_ai_may_use(shadow_may_have(row, type_info, *game_data->skills.get(pet.skill)), row->aitype, scene.driver_melee, scene.driver,
                                                            charged, target_in, row->progressive, held != pet.charges.end() ? std::optional<int>(held->count) : std::nullopt);
                entry.mana = std::max(((row->mana + row->lvlmana * (lvl - 1)) << (row->manashift & 31)) >> 8, 0);   // FUN_006459f0
                entry.kind = listed != pet.skill_list.end() ? listed->kind : skill_kind(*row);
                entry.mode = listed != pet.skill_list.end() ? listed->mode : monster_mode(row->monanim);
                entry.delay = d2d::rules::eval_calc(game_data->skills, row->delay, calc_env(), row->id, lvl);
            }
            scene.skills[3] = { .id = -2 };
        }
        world.give = std::move(give_hands);
        d2d::rules::PetBrain brain(scene, monster.seed, pet.ctrl, world);
        // The Shadow Master (shadows.hpp) on the units round it: the pet, its
        // owner, the monsters within 64 subtiles of either, its fellow pets
        // (the traps it counts). Its world turns what it does into a PetAct.
        // A monster's states are its curse / cry (their auratargetstates).
        // ponytail: units by that radius, not FUN_005dd0b0's rooms.
        auto master_think = [&] {
            using d2d::rules::ShadowUnit;
            d2d::rules::ShadowMasterScene master;
            auto& units = master.units;
            std::vector<int> monster_of{ -1, -1 };                          // by unit: its monster (-1 the pet and owner, -2 a fellow pet)
            units.push_back({ .type = 1, .x = pet_x, .y = pet_y, .foe = false, .owner = 1 });
            units.push_back({ .type = 0, .x = sub(actor->player.x), .y = sub(actor->player.y), .targetable = false, .foe = false });
            const int owner_x = sub(actor->player.x), owner_y = sub(actor->player.y);
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                const auto& other = monsters[i];
                const int other_x = sub(other.unit.x), other_y = sub(other.unit.y);
                if (std::max(std::abs(other_x - pet_x), std::abs(other_y - pet_y)) > 0x40 && std::max(std::abs(other_x - owner_x), std::abs(other_y - owner_y)) > 0x40) continue;
                if (!other.alive() && other.mode != "DT") continue;
                const auto& other_type = game_data->monsters.types[std::size_t(other.type)];
                const bool special = other.boss == d2d::rules::Boss::champion || other.boss == d2d::rules::Boss::unique || other.boss == d2d::rules::Boss::superunique;
                const auto foe = target_of(i);
                ShadowUnit entry{ .type = 1, .x = other_x, .y = other_y, .targetable = other.alive(), .dying = !other.alive(), .foe = other.align == 0,
                                  .melee = d2d::rules::unit_distance(other_x - pet_x, other_y - pet_y, size, other_type.size) <= type_info.melee_rng + 1,
                                  .worth = d2d::rules::shadow_worth(true, !other.alive(), game_data->mon_is_npc[std::size_t(other.type)], other_type.killable,
                                                                    other_type.boss_column, other_type.prime_evil, special),
                                  .blocked = d2d::rules::sight_blocked(pet_x, pet_y, size, other_x, other_y, other_type.size, wall),
                                  .monster_id = other_type.base >= 0 ? other_type.base : other.type };
                entry.resist = { foe.res[0], foe.res[2], foe.res[3], foe.res[1], foe.res[4], foe.res[5] };   // by EType: none, fire, light, magic, cold, poison
                entry.drain = foe.drain;
                units.push_back(std::move(entry));
                monster_of.push_back(int(i));
            }
            for (const auto& fellow : actor->pets)
                if (&fellow != &pet && fellow.monster.alive() && fellow.where == level) {
                    const auto& fellow_type = game_data->monsters.types[std::size_t(fellow.monster.type)];
                    units.push_back({ .type = 1, .x = sub(fellow.monster.unit.x), .y = sub(fellow.monster.unit.y), .targetable = false, .foe = false, .side = true,
                                      .monster_id = fellow_type.base >= 0 ? fellow_type.base : fellow.monster.type });
                    monster_of.push_back(-2);
                }
            const auto unit_of = [&](int monster_index) {
                const auto slot = monster_index >= 0 ? std::ranges::find(monster_of, monster_index) : monster_of.end();
                return slot == monster_of.end() ? -1 : int(slot - monster_of.begin());
            };
            units[0].target = pet.target >= 0 && monsters[std::size_t(pet.target)].alive() ? unit_of(pet.target) : -1;
            units[1].target = actor->attack_mon >= 0 ? unit_of(actor->attack_mon) : -1;
            master.owner = 1;
            master.driver = found >= 0 ? unit_of(found) : -1;
            master.driver_melee = scene.driver_melee; master.driver_distance = scene.driver_distance;
            master.skills = pet.skill_list;
            for (const auto& listed : pet.skill_list)
                if (const auto* row = game_data->skills.get(listed.id)) {
                    const auto missile = game_data->missiles.find(row->srvmissilea);
                    static constexpr std::array<int, 7> kEType{ 0, 1, 2, 4, 5, 3, 6 };   // d2d's etype (-1 none, fire, light, cold, poison, magic, stun) as EType
                    master.rows[listed.id] = { .aitype = row->aitype, .bonus = row->aibonus, .reqlevel = row->reqlevel,
                                               .etype = kEType[std::size_t(std::clamp(row->etype + 1, 0, 6))],
                                               .state = row->aurastate.empty() ? 0 : 1000 + row->id, .state2 = row->auratarget.empty() ? 0 : 2000 + row->id,
                                               .progressive = row->progressive,
                                               .srvmissile = row->srvmissile.empty() ? -1 : 0, .missile = row->srvmissilea.empty() ? -1 : 0,
                                               .missile_range = missile == game_data->missiles.end() ? -1 : missile->second.range,
                                               .repeat = row->srvdofunc == ServerDoFunction::kBreath };
                }
            for (std::size_t k = 2; k < units.size(); ++k)                  // a monster in a skill's auratargetstate: its curse or cry's
                if (monster_of[k] >= 0)
                    for (const auto* effect : { &monsters[std::size_t(monster_of[k])].curse, &monsters[std::size_t(monster_of[k])].cry })
                        if (const auto* effect_skill = game_data->skills.get(effect->skill); effect_skill && !effect_skill->auratarget.empty() && now_ms < effect->until)
                            for (const auto& [id, row] : master.rows)
                                if (const auto* listed_row = game_data->skills.get(id); row.state2 > 0 && listed_row && listed_row->auratarget == effect_skill->auratarget) units[k].states.push_back(row.state2);
            for (const auto& buff : pet.buffs)                              // its self states' aurastates on it
                if (const auto row = master.rows.find(buff.skill); row != master.rows.end() && row->second.state > 0) units[0].states.push_back(row->second.state);
            for (const auto& held : pet.charges)                            // a charge-up's aurastate on it, its count the aurastat
                if (const auto row = master.rows.find(held.skill); row != master.rows.end() && row->second.state > 0) {
                    units[0].states.push_back(row->second.state);
                    row->second.charges = held.count;
                }
            for (std::size_t k = 0; k < 3; ++k) { master.fixed[k] = type_info.diff[k].aip[0]; master.fixed[k + 3] = type_info.diff[k].aip[1]; }
            master.aip3 = per_difficulty.aip[2];
            master.scan = d2d::rules::shadow_scan(units, 0, 1);
            master.life = monster.hit_points * 100 / std::max(monster.stats.hit_points, 1);
            master.left = pet.left_skill >= 0;
            master.town = town;
            master.charged = !pet.charges.empty();                         // FUN_0063a2b0: a progressive_* state on it
            d2d::rules::PetAct out;
            const auto aim = [&](int target) {
                index[3] = target >= 0 ? monster_of[std::size_t(target)] : -1;
                return index[3] >= 0 ? PetUnit::foe : PetUnit::none;
            };
            struct MasterWorld {
                std::function<bool(int, bool)> decide;
                std::function<bool(int, int, int)> skill;
                std::function<bool(int)> approach;
                std::function<bool(int, int)> away;
                std::function<void(int)> run, set_left, stand;
            } master_world;   // assigned one by one: MSVC can't take these lambdas in a braced init (C2011)
            master_world.decide = [&](int foe, bool melee) {
                const bool took = world.decide(foe >= 0 ? PetUnit::foe : PetUnit::none, melee, false, 6);
                if (took) out = world.followed();
                return took;
            };
            master_world.skill = [&](int mode, int id, int target) {
                out = { .kind = d2d::rules::PetAct::Kind::skill, .unit = aim(target), .skill = id, .mode = mode };
                return true;
            };
            master_world.approach = [&](int target) {                                               // FUN_005ded00(target, 4): a walk at it
                if (aim(target) == PetUnit::none) return false;
                const d2d::rules::MercMove move{ .unit = int(PetUnit::foe) };
                if (!move_pet(move)) return false;
                out = { .kind = d2d::rules::PetAct::Kind::chase, .move = move, .unit = PetUnit::foe };
                return true;
            };
            master_world.away = [&](int x, int y) {                                             // FUN_005df140: 8 away, path type 0xf
                const d2d::rules::MercMove move{ .x = x, .y = y, .type = 0xf, .steps = 8 };
                if (!move_pet(move)) return false;
                out = { .kind = d2d::rules::PetAct::Kind::moved, .move = move };
                return true;
            };
            master_world.run = [&](int) {                                                      // FUN_005ded20: a run at the owner
                const d2d::rules::MercMove move{ .mode = d2d::rules::kMonsterRun, .unit = int(PetUnit::owner) };
                if (move_pet(move)) out = { .kind = d2d::rules::PetAct::Kind::chase, .move = move, .unit = PetUnit::owner };
            };
            master_world.set_left = [&](int id) { pet.left_skill = id; };
            master_world.stand = [&](int frames) { out = { .kind = d2d::rules::PetAct::Kind::stand, .frames = frames }; };
            d2d::rules::shadow_master_think(master, monster.seed, pet.ctrl, master_world);
            return out;
        };
        if (ai_name == "ShadowMaster") brain.act = master_think();
        else if (ai_name == "ShadowWarrior") d2d::rules::shadow_warrior_think(brain);
        else if (ai_name == "Hydra") d2d::rules::hydra_think(brain);
        else if (ai_name == "AssassinSentry") d2d::rules::sentry_think(brain);
        else if (ai_name == "DeathSentry") d2d::rules::death_sentry_think(brain);
        else if (ai_name == "Raven") d2d::rules::raven_think(brain);
        else if (ai_name == "DruidWolf" && monster.npc.hc_idx == 0x1a4) d2d::rules::spirit_wolf_think(brain);
        else if (ai_name == "DruidWolf") d2d::rules::fenris_think(brain);
        else if (ai_name == "Totem") d2d::rules::totem_think(brain);
        else if (ai_name == "Vines") d2d::rules::poison_creeper_think(brain);
        else if (ai_name == "CycleOfLife") d2d::rules::cycle_vine_think(brain);
        else d2d::rules::druid_bear_think(brain);
        const auto& act = brain.act;
        monster.force_sight = act.unreachable;
        using Kind = d2d::rules::PetAct::Kind;
        const int aidel = per_difficulty.aidel > 0 ? per_difficulty.aidel : 15;
        const auto target_of_unit = [&](PetUnit which) { return which == PetUnit::none ? -1 : index[std::size_t(which)]; };
        switch (act.kind) {
        case Kind::moved:
        case Kind::chase: {
            set_mode(*game_data, monster, act.move.mode == d2d::rules::kMonsterRun ? "RN" : "WL", now_ms);
            if (act.kind != Kind::chase) unit.path = route;
            unit.walking = true;
            if (act.kind == Kind::moved) { unit.goal_x = centre(act.move.x); unit.goal_y = centre(act.move.y); unit.budget = 0x14; unit.path_points = int(route.size()); unit.to_unit = false; }
            monster.move_pct = act.move.pct;
            pet.chase = act.kind == Kind::chase && chased >= 0;
            pet.target = pet.chase ? chased : -1;
            break;
        }
        case Kind::swing:
        case Kind::skill:
        case Kind::seq: {
            const int target = target_of_unit(act.unit);
            pet.target = target;
            pet.cast = act.kind == Kind::swing ? -1 : act.skill;
            pet.cast_x = centre(act.x); pet.cast_y = centre(act.y);
            if (target >= 0) unit.dir = direction16(monsters[std::size_t(target)].unit.x - unit.x, monsters[std::size_t(target)].unit.y - unit.y);
            unit.path.clear(); unit.walking = false;
            const std::string_view mode = act.kind == Kind::skill && act.mode < 16 && game_data->npc_timing(monster.npc, kModes[std::size_t(act.mode)]).directions
                                              ? kModes[std::size_t(act.mode)] : "A1";
            monster.next_act = now_ms + std::uint32_t(std::max(act.frames, 0)) * 40;
            if (!game_data->npc_timing(monster.npc, mode).directions) {   // no frames for it (a trap): at once
                if (pet.cast >= 0) pet_cast(pet, now_ms, crowd);
                pet.cast = -1;
                monster.next_act = std::max(monster.next_act, now_ms + 40);
                break;
            }
            set_mode(*game_data, monster, mode, now_ms);
            monster.struck = false;
            break;
        }
        case Kind::die:
            monster.hit_points = 0;
            set_mode(*game_data, monster, "DT", now_ms);
            break;
        case Kind::teleport:                                        // mode 3: room_teleport, stand 5
            room_teleport(*game_data, *level, crowd, spawning, unit);
            unit.path.clear(); unit.walking = false;
            monster.next_act = now_ms + 5 * 40;
            break;
        case Kind::stand:
            monster.next_act = now_ms + std::uint32_t(std::max(act.frames, 1)) * 40;
            break;
        case Kind::failed:
            monster.next_act = now_ms + std::uint32_t(aidel) * 40;
            break;
        }
    }

// A traced pet's skill on its attack mode's action frame (pet.cast at
// pet.target or its spot): a trap's or the Hydra's shot, the Death
// Sentry's corpse blast, Poison Creeper's poison, the cycle vines eating a
// corpse (their skill's Param5 % of the owner's max life / mana), a wolf's
// Teleport, Fenris eating a corpse, the Grizzly's Maul (a blow).
// ponytail: Vine Attack and the cyclers (do 130, st 63) and Fenris's rage
// as their published effects, not traced.
// Blade Shield's strike (do 54, a periodic skill every perdelay frames:
// FUN_005d7e10 -> FUN_005d7ce0, each unit by FUN_005d7c40): every monster
// within aurarange of (x, y), a hit at the skill's to-hit (FUN_006449f0),
// its own damage (FUN_0056e170, MinDam..MaxDam by level) and the weapon's
// at SrcDam (FUN_0057b7d0, 32 / 128).
auto Fight::blade_shield(const d2d::rules::Skill& skill, int lvl, float x, float y, const d2d::rules::Fighter& fighter, bool by_player, std::uint32_t now_ms) -> void {
        const auto env = calc_env();
        d2d::rules::Swing swing;
        swing.ar_pct = d2d::rules::skill_tohit(game_data->skills, skill, env, lvl);
        swing.srcdam = skill.srcdam;
        swing.skill_lo = d2d::rules::skill_phys(game_data->skills, skill, env, lvl, false);
        swing.skill_hi = d2d::rules::skill_phys(game_data->skills, skill, env, lvl, true);
        const int radius = std::max(calc(skill, skill.aurarange, lvl), 1);
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].targetable() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(radius))
                land(j, d2d::rules::player_blow(fighter, target_of(j), int(actor->character.stats.get(d2d::d2s::kLevel)), rng, swing), by_player, now_ms);
    }

// What a Shadow's self states give it: their aurastats at their levels,
// summed again as one starts or ends.
auto Fight::pet_buffs(Pet& pet) -> void {
        pet.buff_stats.clear();
        const auto env = calc_env();
        auto add = [&](int skill_id, int lvl) {
            if (const auto* skill = game_data->skills.get(skill_id))
                for (std::size_t k = 0; k < skill->aurastat.size(); ++k)
                    if (skill->aurastat[k] >= 0) pet.buff_stats[skill->aurastat[k]] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[k], env, skill->id, lvl);
        };
        for (const auto& buff : pet.buffs) add(buff.skill, buff.level);
        if (pet.curse.skill >= 0) add(pet.curse.skill, pet.curse.level);
        // Battle Orders' maxhp percent on its life, as the player's (bo_life).
        auto& stats = pet.monster.stats;
        const int want = int(std::int64_t(stats.hit_points - pet.bo_life) * pet_buff_stat(pet, d2d::d2s::kMaxLifePercent) / 100);
        stats.hit_points += want - pet.bo_life;
        pet.monster.hit_points = std::min(pet.monster.hit_points + std::max(want - pet.bo_life, 0), stats.hit_points);
        pet.bo_life = want;
    }

// FUN_00647280 on a Shadow: a skill it has takes the new level; else it
// joins the list's tail with its monanim mode and kind (FUN_00647110).
auto Fight::shadow_give(Pet& pet, int skill, int lvl) const -> void {
        const auto* row = game_data->skills.get(skill);
        if (!row) return;
        if (const auto found = std::ranges::find(pet.skill_list, skill, &d2d::rules::ShadowListed::id); found != pet.skill_list.end()) { found->level = lvl; return; }
        pet.skill_list.push_back({ .id = skill, .level = lvl, .kind = skill_kind(*row), .mode = monster_mode(row->monanim) });
    }

// A Shadow's skill on its action frame, at its level on the Shadow: Attack
// and the claws and kicks as its owner's blow (it has the owner's gear) with
// the skill's swing; a missile skill fired from it (Blade Fury, Fire Blast,
// Shock Web, ...); Psychic Hammer and Mind Blast at its target.
// Dragon Flight puts it beside the target and kicks. Charge-ups charge it,
// finishers release them, as the player's.
// Its self states, traps and Blade Sentinel as the player's.
// ponytail: Dragon Flight's two events (the flight, then the kick) land
// on one frame.
auto Fight::shadow_cast(Pet& pet, std::uint32_t now_ms, const Crowd& crowd) -> void {
        const auto& monster = pet.monster;
        const auto* skill = game_data->skills.get(pet.cast);
        if (!skill) return;
        const auto listed = std::ranges::find(pet.skill_list, skill->id, &d2d::rules::ShadowListed::id);
        const int lvl = std::max(listed == pet.skill_list.end() ? 1 : listed->level, 1);
        const int target = pet.target >= 0 && monsters[std::size_t(pet.target)].alive() ? pet.target : -1;
        const auto& foe = target >= 0 ? monsters[std::size_t(target)].unit : monster.unit;
        const Caster caster{ monster.unit.x, monster.unit.y, lvl, target, foe.x, foe.y, false, true };
        // Its self states (do 18 Burst of Speed / Fade / Venom, 47 Cloak of
        // Shadows, 54 Blade Shield): auralen frames on it; Cloak blinds the
        // monsters within its aurarange (their cry, as the player's).
        if (skill->srvdofunc == ServerDoFunction::kSelfState || skill->srvdofunc == ServerDoFunction::kCloakOfShadows || skill->srvdofunc == ServerDoFunction::kBladeShield) {
            const int len = calc(*skill, skill->auralen, lvl);
            const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
            std::erase_if(pet.buffs, [&](const SelfState& buff) { return buff.skill == skill->id; });
            pet.buffs.push_back({ skill->id, lvl, until });
            pet_buffs(pet);
            if (skill->srvdofunc == ServerDoFunction::kCloakOfShadows)
                for (auto& other : monsters)
                    if (other.alive() && std::hypot(other.unit.x - monster.unit.x, other.unit.y - monster.unit.y) * 5 <= float(calc(*skill, skill->aurarange, lvl)))
                        cloak(*game_data, other, skill->id, lvl, until, now_ms);
            return;
        }
        // Its traps and Blade Sentinel (do 45 / 44): laid at its target (else
        // where it stands) once the pets have had their turns.
        if (skill->srvdofunc == ServerDoFunction::kTrap || skill->srvdofunc == ServerDoFunction::kBladeSentinel) {
            actor->pet_summons.push_back({ skill->id, lvl, foe.x, foe.y });
            return;
        }
        if (skill->id != 0 && missile_skill(*skill)) { fire(*skill, now_ms, caster); return; }
        if (skill->srvdofunc == ServerDoFunction::kPsychicHammer || skill->srvdofunc == ServerDoFunction::kMindBlast) { spot(*skill, now_ms, caster); return; }
        std::erase_if(pet.charges, [&](const Charge& charge) { return now_ms >= charge.until; });
        if (skill->srvdofunc == ServerDoFunction::kDragonFlight && target >= 0) {   // do 52: beside the target, then the kick (as the owner's)
            if (in_town(level)) return;
            auto& unit = pet.monster.unit;
            put_near(*level, crowd, unit, foe.x, foe.y);
            unit.path.clear(); unit.walking = false;
            auto fighter = actor->pf_kick;
            auto swing = skill_swing(*skill, lvl, calc_env(), false);
            add_charges(pet.charges, fighter, swing, false);
            pet_strike(pet, std::size_t(target), d2d::rules::player_blow(fighter, target_of(std::size_t(target)), monster.stats.level, rng, swing), now_ms);
            release(pet.charges, std::size_t(target), now_ms, false);
            return;
        }
        if (target < 0 || (skill->id != 0 && skill_kind(*skill) != 1)) return;
        if (std::hypot(foe.x - monster.unit.x, foe.y - monster.unit.y) > kMeleeReach + 0.3f) return;
        // As the player's hit: a charge-up's hit adds a charge, a finisher
        // takes the charges' bonus and releases them on a hit.
        auto swing = skill->id == 0 ? d2d::rules::Swing{} : skill_swing(*skill, lvl, calc_env(), false);
        auto fighter = swing.kick ? actor->pf_kick : actor->player_combat;
        if (const int length = pet_buff_stat(pet, d2d::d2s::kPoisonLength); length > 0) {   // Venom: its poison on the blow
            auto& [low, high] = fighter.elem[3];
            low += pet_buff_stat(pet, d2d::d2s::kPoisonMinDamage) * length / 256; high += pet_buff_stat(pet, d2d::d2s::kPoisonMaxDamage) * length / 256;
            fighter.poison_len = std::max(fighter.poison_len, length);
        }
        const bool finishing = finisher(skill);
        if (finishing) add_charges(pet.charges, fighter, swing, false);
        const auto blow = d2d::rules::player_blow(fighter, target_of(std::size_t(target)), monster.stats.level, rng, swing);
        pet_strike(pet, std::size_t(target), blow, now_ms);
        if (blow.hit && skill->srvstfunc == ServerStartFunction::kChargeUp) charge(pet.charges, *skill, lvl, now_ms);
        if (blow.hit && finishing) release(pet.charges, std::size_t(target), now_ms, false);
    }

auto Fight::pet_cast(Pet& pet, std::uint32_t now_ms, const Crowd& crowd) -> void {
        if (pet.mirror) { shadow_cast(pet, now_ms, crowd); return; }
        auto& monster = pet.monster;
        const auto* skill = game_data->skills.get(pet.cast);
        const int target = pet.target;
        const bool alive = target >= 0 && monsters[std::size_t(target)].alive();
        if (!skill) return;
        if (skill->name.starts_with("Teleport")) { put_near(*level, crowd, monster.unit, pet.cast_x, pet.cast_y); return; }   // "Teleport 2": a wolf to its owner
        if (pet.shot_skill >= 0 && alive && skill->srvdofunc != ServerDoFunction::kCorpseExplosion) { trap_fire(pet, std::size_t(target), now_ms); return; }
        if (pet.ranged >= 0 && alive) {
            const auto* missile_info = pet_missile(*game_data->skills.get(pet.ranged), pet.variant);
            if (!missile_info) return;
            auto& foe = monsters[std::size_t(target)];
            Missile x{ missile_info, monster.unit.x, monster.unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(missile_info->range, 1)) * 40, {} };
            const float dx = foe.unit.x - monster.unit.x, dy = foe.unit.y - monster.unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
            x.velocity_x = dx / distance * missile_speed(*missile_info, pet.ranged_level); x.velocity_y = dy / distance * missile_speed(*missile_info, pet.ranged_level);
            x.dir = missile_facing(monster.unit.x, monster.unit.y, dx, dy); x.to = { foe.unit.x, foe.unit.y };
            x.friendly = true; x.caster = actor->id; x.level = pet.ranged_level; x.skill = pet.ranged;
            if (const auto* missile_skill_row = skill_named(missile_info->skill)) x.skill = missile_skill_row->id;
            pending.push_back(std::move(x));
            return;
        }
        if (target < 0) return;
        if (const auto* pet_skill = game_data->skills.get(pet.skill); skill->srvdofunc == ServerDoFunction::kCorpseExplosion || (pet_skill && pet_skill->name == "Death Sentry" && !alive)) {
            if (pet_skill)
                for (std::size_t k = 0; k < 5; ++k)
                    if (const auto* bone_skill = skill_named(pet_skill->sumskill[k]); bone_skill && bone_skill->srvdofunc == ServerDoFunction::kCorpseExplosion && !monsters[std::size_t(target)].corpse_used) {
                        corpse_blast(*bone_skill, pet.shot_level, std::size_t(target), now_ms);
                        break;
                    }
            return;
        }
        if (const auto* pet_skill = game_data->skills.get(pet.skill); pet_skill && pet_skill->srvdofunc == ServerDoFunction::kVine) {
            using namespace d2d::d2s;
            if (pet_skill->etype == 3 && alive) {                      // Poison Creeper
                const auto damage = d2d::rules::missile_damage(game_data->skills, *pet_skill, calc_env(), monster.stats.level);
                land(std::size_t(target), d2d::rules::missile_blow(damage, target_of(std::size_t(target)), pierce(), rng), false, now_ms);
            } else if (!alive && !monsters[std::size_t(target)].corpse_used) {   // Carrion Vine / Solar Creeper
                monsters[std::size_t(target)].corpse_used = true;
                const bool life = pet_skill->name == "Cycle of Life";
                auto& value = actor->character.stats.values[life ? kLife : kMana];
                value = std::min(actor->character.stats.values[life ? kMaxLife : kMaxMana], value + actor->character.stats.values[life ? kMaxLife : kMaxMana] * pet_skill->par[4] / 100);
            }
            return;
        }
        if (!alive) { monsters[std::size_t(target)].corpse_used = true; return; }   // Fenris eats the corpse
        if (std::hypot(monsters[std::size_t(target)].unit.x - monster.unit.x, monsters[std::size_t(target)].unit.y - monster.unit.y) <= kMeleeReach + 0.3f)
            pet_strike(pet, std::size_t(target), d2d::rules::player_blow(d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit),
                                                                         target_of(std::size_t(target)), monster.stats.level, rng), now_ms);
    }

auto Fight::cast_scroll(int skill, std::uint32_t now_ms) -> bool {
        if (dead() || actor->pmode >= 0) return false;
        actor->swing_skill = skill;
        actor->attack_mon = -1;
        actor->item_cast = true;
        set_pmode(kModeSC, now_ms);
        actor->item_cast = false;
        actor->pstruck = false;
        return true;
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

auto Fight::in_reach() const -> std::vector<int> {
        std::vector<int> out;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].targetable() && monsters[i].align != 2   // a converted one is no foe (FUN_00554200)
                && std::hypot(monsters[i].unit.x - actor->player.x, monsters[i].unit.y - actor->player.y) <= kMeleeReach + 0.5f)
                out.push_back(int(i));
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
        foe.id = -2 - 2 * actor->id;                            // its own id below the monsters' (a guided missile finds it again)
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
        foe.id = -3 - 2 * actor->id;                     // as the player's, its own
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
                bool dead = hurt(*game_data, struck, soak(struck, foes[i].physical, foes[i].damage), now_ms);
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
                    pet_hurt(pet, foe.damage, now_ms);
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
                                land(j, d2d::rules::missile_blow(*missile.row, target_of(j), {}, rng), true, now_ms);
                        return true;
                    }
                    if (missile.skill >= 0) {
                        if (std::ranges::contains(*missile.struck, int(i))) continue;
                        if (skill_missile_hits(missile, i, now_ms)) return true;
                        continue;
                    }
                    auto blow = d2d::rules::player_blow(d2d::rules::simple_fighter(missile.min, missile.max, missile.attack_rating), target_of(i), missile.level, rng);
                    if (missile.info) blow.hit_class = missile.info->hit_class;   // an arrow's, not hand to hand's (as a skill's missile, Missiles.txt HitClass)
                    land(i, blow, false, now_ms);
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
        if (actor->merc && side[1].damage > 0 && actor->merc->mode != "DT") {
            actor->merc->hit_points -= side[1].damage;
            auto& unit = actor->merc->unit;
            const auto mode = actor->merc->hit_points <= 0                                     ? std::string_view("DT")
                            : actor->merc->mode != "KB" && side[1].damage * 12 >= merc_max_life() ? std::string_view("GH")
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
                            land(j, blow, true, now_ms);
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
        const bool big = foe.damage > 0 && std::int64_t(foe.damage) * 12 >= actor->character.stats.fixed(kMaxLife);
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
