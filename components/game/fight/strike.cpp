// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): fighters, swings and blows: who hits whom, for what, and the skill
// sequences. The rest is in this directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <leap.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <sequences.hpp>
#include <shrines.hpp>
#include <skills.hpp>
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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

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
            if (!gear_active(item)) continue;
            const bool worn = item.location == d2d::d2s::item_location::kEquipped;
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
        add(sum, set_props(*game_data, actor->character.items));
        // Throwing (FUN_00645720): a throwable in hand and the skill in use ranged
        // (range 2) with an itypea1 of thro (type 0x30).
        const auto* in_use = game_data->skills.get(actor->attack_skill);
        const bool throwing = weapon && throwable(*game_data, weapon->code) && in_use && in_use->range == "rng" && !in_use->itypea1.empty()
                           && d2d::rules::type_is(game_data->rules, in_use->itypea1, "thro");
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
            // Throwing, the throw masteries (345..347) stand in for the melee ones,
            // the best whose type fits.
            const int first = throwing ? kPassiveMasteryThrowToHit : kPassiveMasteryMeleeToHit;
            std::array<std::int64_t, 4> best{};              // 342 (345), 343 (346), 344 (347), 348
            for (const auto& passive : *passives) {
                if (passive.stat < 0 || std::size_t(passive.stat) >= sum.size()) continue;
                if (passive.itype.empty()) { sum[std::size_t(passive.stat)] += passive.value; continue; }
                const std::size_t mastery = passive.stat >= first && passive.stat <= first + 2 ? std::size_t(passive.stat - first)
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
                                          int(actor->character.panel.defense), res, boots, throwing);
        {   // The swing starts by the weapon in hand: slot 4 else slot 5, a weapon, not broken and its requirements met (FUN_0063c9b0 -> FUN_0062a4e0,
            // the `weapon` chosen above), else nothing.
            const auto base = weapon ? game_data->rules.item_base.find(weapon->code) : game_data->rules.item_base.end();
            if (!weapon || base != game_data->rules.item_base.end())
                fighter.attack_start = d2d::rules::attack_start_frame(actor->character.character_class, weapon ? std::string_view(base->second.wclass) : std::string_view{});
        }
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
        if (now_ms < actor->marks[0].until) states.push_back({ actor->marks[0].skill, actor->marks[0].level, actor->marks[0].until });
        for (const auto& state : states) {
            const auto* skill = game_data->skills.get(state.skill);
            for (std::size_t i = 0; skill && i < skill->aurastat.size(); ++i)
                if (const int id = skill->aurastat[i]; id >= 0 && id != kLife && std::size_t(id) < st_sum.size())
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], env, skill->id, state.level);
        }
        // A boss's enemy aura over the player (Aura Enchanted's Holy Freeze):
        // its velocitypercent and attackrate as stats; resists and defense go
        // on the foe (monster_auras).
        for (const auto& aura_monster : monsters) {
            const auto* skill = aura_monster.alive() && aura_monster.aura ? game_data->skills.get(aura_monster.aura) : nullptr;
            if (!skill || (skill->srvdofunc != ServerDoFunction::kEnemyAura && skill->srvdofunc != ServerDoFunction::kHolyFreeze)) continue;
            const d2d::rules::CalcEnv boss_env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, aura_monster.stats.level, nullptr };
            if (std::hypot(aura_monster.unit.x - actor->player.x, aura_monster.unit.y - actor->player.y) * 5 > float(d2d::rules::eval_calc(game_data->skills, skill->aurarange, boss_env, skill->id, aura_monster.aura_lvl))) continue;
            for (std::size_t i = 0; i < skill->aurastat.size(); ++i)
                if (const int id = skill->aurastat[i]; id == kVelocityPercent || id == kAttackRate)
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], boss_env, skill->id, aura_monster.aura_lvl);
        }
        for (const auto& aura : friendly_auras(actor->player.x, actor->player.y))
            for (std::size_t i = 0; i < aura.skill->aurastat.size(); ++i)
                if (const int id = aura.skill->aurastat[i]; id >= 0 && id != kLife && std::size_t(id) < st_sum.size())
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(game_data->skills, aura.skill->aura_calc[i], aura.env, aura.skill->id, aura.level);
        if (now_ms < actor->curses[0].until)                    // a monster's curse on the player
            for (const auto& [id, value] : curse_stats(actor->curses[0], env))
                if (id != kLife && std::size_t(id) < st_sum.size()) st_sum[std::size_t(id)] += value;
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
        actor->player_combat.sias += int(st_sum[kAttackRate]);   // SIAS, raw (FUN_00623f50 adds stat 68 as it is)
        actor->player_combat.velocity_pct += int(st_sum[kVelocityPercent]);
        for (const auto& passive : passives) if (passive.stat == kVelocityPercent && passive.itype.empty()) actor->player_combat.velocity_pct += passive.value;   // Increased Speed
        actor->player_combat.defense += int(actor->player_combat.defense * st_sum[kSkillArmorPercent] / 100);
        actor->player_combat.defense = std::max(int(actor->player_combat.defense + actor->player_combat.defense * st_sum[kArmorOverridePercent] / 100), 0);
        // The char panel shows the unit's full stats (FUN_004a7d00 reads them
        // through FUN_00625480): the states' resistances (to their cap, at
        // least -100) and defense, their attributes, Battle Orders' share of
        // the maxima for the blue; and each state's States.txt colours.
        auto& panel = actor->character.panel;
        for (std::size_t k = 0; k < 4; ++k) panel.res[kPanelRes[k]] = std::max(actor->player_combat.res[k], -100);
        panel.defense = actor->player_combat.defense;
        for (const std::size_t attribute : { kStr, kEne, kDex, kVit }) panel.bonus[attribute] += st_sum[attribute];
        panel.bonus[kMaxLife] += actor->bo_life >> 8; panel.bonus[kMaxMana] += actor->bo_mana >> 8;
        std::vector<std::string_view> active;
        for (const auto& state : states) if (const auto* skill = game_data->skills.get(state.skill)) active.push_back(skill->aurastate);
        for (const auto& aura : friendly_auras(actor->player.x, actor->player.y)) active.push_back(aura.skill->aurastate);
        if (const auto* curse = game_data->skills.get(actor->curses[0].skill); curse && now_ms < actor->curses[0].until) active.push_back(curse->auratarget);
        if (now_ms < actor->potion_states[0].until) active.push_back("antidote");
        if (now_ms < actor->potion_states[1].until) active.push_back("thawing");
        if (const int row = actor->boost.shrine; now_ms < actor->boost.until && row > 0 && std::size_t(row) < game_data->shrines.size())
            active.push_back(d2d::rules::shrine_state(game_data->shrines[std::size_t(row)].code));
        for (const auto name : active)
            if (const auto found = game_data->states.find(std::string(name)); found != game_data->states.end()) {
                panel.blue |= found->second.panel_blue;
                panel.red |= found->second.panel_red;
            }
        if (std::ranges::contains(active, std::string_view("holyshield"))) panel.blue |= d2d::rules::kPanelDefense;   // FUN_004a7d00: state 0x65 with a shield
        const std::uint32_t chosen[2] = { actor->character.header.left_skill, actor->character.header.right_skill };
        for (std::size_t k = 0; k < 2; ++k)
            if (const auto* skill = game_data->skills.get(int(chosen[k])))
                panel.attack[k] = d2d::rules::attack_line(game_data->skills, *skill, env, skill_level ? skill_level(skill->id) : 1,
                                                          actor->player_combat, int(actor->psum[kPassiveMasteryMeleeToHit]), panel.blue, panel.red,
                                                          skill_status(skill->id, now_ms));   // FUN_004d9fc0: 0, 1, 6, 8 draw
    }

auto Fight::calc_env() -> d2d::rules::CalcEnv {
        return { skill_base, skill_level, [this](int id) { return id >= 0 && std::size_t(id) < actor->psum.size() ? int(actor->psum[std::size_t(id)]) : 0; },
                 int(actor->character.stats.get(d2d::d2s::kLevel)), &rng };
    }

auto Fight::friendly_auras(float x, float y) const -> std::vector<AuraOn> { return auras_at(x, y, false); }
auto Fight::enemy_auras(const Monster& monster) const -> std::vector<AuraOn> { return auras_at(monster.unit.x, monster.unit.y, true, &monster); }

auto Fight::auras_at(float x, float y, bool enemy, const Monster* taker) const -> std::vector<AuraOn> {
        using namespace d2d::d2s;
        std::vector<AuraOn> out;
        const auto add = [&](int skill_id, int lvl, d2d::rules::CalcEnv env, float from_x, float from_y) {
            const auto* skill = game_data->skills.get(skill_id);
            const bool kind = enemy ? skill && skill->aura && (skill->srvdofunc == ServerDoFunction::kEnemyAura || skill->srvdofunc == ServerDoFunction::kHolyFreeze)
                                    : skill && skill->srvdofunc == ServerDoFunction::kFriendlyAura;
            if (!kind || lvl < 1) return;
            if (std::hypot(from_x - x, from_y - y) * 5 > float(d2d::rules::eval_calc(game_data->skills, skill->aurarange, env, skill->id, lvl))) return;
            if (enemy && taker && !aura_takes(*skill, *taker, from_x, from_y)) return;
            out.push_back({ skill, lvl, std::move(env) });
        };
        const auto no_skills = [](int) { return 0; };
        add(actor->aura, skill_level ? skill_level(actor->aura) : 1,
            { skill_base, skill_level, [this](int id) { return id >= 0 && std::size_t(id) < actor->psum.size() ? int(actor->psum[std::size_t(id)]) : 0; }, int(actor->character.stats.get(kLevel)), nullptr },
            actor->player.x, actor->player.y);
        // The merc's (its AI's pick, merc_turn), at its level for that skill (mercs have no synergies).
        if (actor->merc && actor->merc->alive() && actor->merc->mode != "DT")
            for (std::size_t k = 0; k < actor->merc_ids.size(); ++k)
                if (actor->merc_ids[k] == actor->merc_aura && actor->merc_aura > 0)
                    add(actor->merc_aura, actor->merc_levels[k], { no_skills, no_skills, no_skills, actor->merc_st.level, nullptr }, actor->merc->unit.x, actor->merc->unit.y);
        // A totem's (Oak Sage, Heart of Wolverine, Spirit of Barbs).
        for (const auto& pet : actor->pets)
            if (pet.monster.alive() && pet.where == level)
                add(pet.aura, pet.aura_level, { skill_base, skill_level, no_skills, int(actor->character.stats.get(kLevel)), nullptr }, pet.monster.unit.x, pet.monster.unit.y);
        return out;
    }

auto Fight::merc_calc_env() -> d2d::rules::CalcEnv {
        return { [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, actor->merc_st.level, &rng };
    }

auto Fight::need_mana(std::uint32_t now_ms) -> void {
        static constexpr std::array<const char*, 7> kNeedMana{ "amazon_needmana_1", "sorceress_needmana_1", "necromancer_needmana_1",
            "paladin_needmana_1", "barbarian_needmana_1", "druid_needmana_1", "assassin_needmana_1" };
        class_voice(kNeedMana[std::size_t(std::clamp(actor->character.character_class, 0, 6))], now_ms);
    }

auto Fight::class_voice(std::string_view name, std::uint32_t now_ms) -> void {
        cues.cue_on(unit_key(kSoundOnPlayer, actor->id), name, now_ms, actor->player.x, actor->player.y, Cues::kClassLine);
    }

auto Fight::impossible(std::uint32_t now_ms) -> void {
        static constexpr std::array<const char*, 7> kImpossible{ "amazon_impossible_1", "sorceress_impossible_1", "necromancer_impossible_1",
            "paladin_impossible_1", "barbarian_impossible_1", "druid_impossible_1", "assassin_impossible_1" };
        class_voice(kImpossible[std::size_t(std::clamp(actor->character.character_class, 0, 6))], now_ms);
    }

auto Fight::start_swing(std::uint32_t now_ms, bool shown) -> bool {
        actor->swing_skill = 0;
        actor->kicks_left = 0;
        actor->item_charge = actor->attack_charge;            // the queued attack's entry (World::apply), if any
        const auto* skill = game_data->skills.get(actor->attack_skill);
        if (skill && actor->attack_skill != 0) {
            // The walk's end asks again (FUN_00461250 commands 0x15 / 0x16 -> FUN_00461180 -> FUN_004610c0).
            const int asked = shown ? actor->attack_skill : ask(actor->attack_skill, now_ms);
            if (asked < 0) { actor->attack_mon = -1; return false; }
            if (!skill_built(*skill)) {
                if (std::ranges::find(told, actor->attack_skill) == told.end()) {
                    told.push_back(actor->attack_skill);
                    d2d::log::info("not implemented: skill {} (srvstfunc {}, srvdofunc {}) - a plain attack for now",
                                   skill->name, int(skill->srvstfunc), int(skill->srvdofunc));
                }
            } else if (asked == actor->attack_skill) {
                const int lvl = cast_level(asked);
                pay(*skill, lvl);
                actor->swing_skill = actor->attack_skill;
                if (skill->srvstfunc == ServerStartFunction::kDragonTalon || skill->srvstfunc == ServerStartFunction::kRepeatedHit || skill->srvstfunc == ServerStartFunction::kFend) {   // Talon's kicks (FUN_005d5970), Zeal's hits (FUN_005daf40)
                    const auto env = calc_env();
                    int value = d2d::rules::eval_calc(game_data->skills, skill->calc[0], env, skill->id, lvl);
                    if (skill->srvstfunc == ServerStartFunction::kFend) value = std::min(value, int(in_reach().size()));   // Fend (FUN_005dae30): one per enemy in reach (FUN_0056bc80), at most calc1
                    actor->kicks_left = std::max(value, 1) - 1;
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
        const auto ticks = d2d::rules::attack_ticks(int(actor->seq.size()), int(attack_anim.speed ? attack_anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, actor->player_combat.sias + chill_rate(now_ms));
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
        actor->whirl_next = 0;
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

auto Fight::land(std::size_t monster_index, const d2d::rules::Blow& blow, bool by_player, std::uint32_t now_ms, int striker) -> bool {
        using namespace d2d::d2s;
        auto& target = monsters[monster_index];
        if (!target.targetable()) return false;                     // under the sand, aloft: nothing reaches it
        if (target.align == 2) return false;                        // converted: no foe of the player's side (FUN_00554200)
        if (target.alive()) target.struck_by = striker;             // FUN_00621d50: not once it's dying (FUN_005541b0)
        // Prevent Monster Heal (FUN_0057d810, run as the to-hit roll passes):
        // a player's only, not on the ubers (hcIdx 704..709); state 0x34
        // for 120 000 frames.
        if (blow.prevent_heal && striker == player_foe_id() && (target.type < 704 || target.type > 709)) target.no_heal_until = now_ms + 120'000u * 40;
        if (blow.blocked && !blow.hit) { block_anim(*game_data, target, now_ms); return false; }
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
        // ever lengthened. Then rand % 100 < 20 on the monster's seed puts state
        // 107 (shatter) on, else off; never on a deadCol monster (MonStats2 flag 19).
        // A death under it shatters (boss_events, rules::shatter_rows).
        if (const int cold_effect = target.type >= 0 ? game_data->monsters.types[std::size_t(target.type)].diff[std::size_t(target.difficulty)].cold_effect : 0; blow.chill_ticks > 0 && cold_effect != 0) {
            const int divisor = game_data->cold_divisor[std::size_t(target.difficulty)];
            const int ticks = std::max(cold_effect < 0 && divisor != 0 ? blow.chill_ticks / divisor : blow.chill_ticks, 1);
            target.chill_until = std::max(target.chill_until, now_ms + std::uint32_t(ticks) * 40);
            target.shatter = target.seed.next() % 100 < 20 && !target.npc.dead_col;
        }
        // Stunned (state 21, FUN_0057aae0): it stands until the stun ends, a
        // new stun resetting the length. A special monster (champion, unique,
        // superunique: monster data +0x16 & 8, FUN_005a0180) shrugs it off
        // 90 % of the time (the attacker's seed); MonStats boss (+0xc & 0x40,
        // FUN_0063e9f0) or Velocity 0 (+0x32) can't be stunned; 250 frames at
        // most (the hirelings' 13, FUN_0063ee90, is for mercs struck). The
        // gear's stun length (stat 66) comes in the blow (rules::player_blow).
        if (const auto* stun_type = target.type >= 0 ? &game_data->monsters.types[std::size_t(target.type)] : nullptr;
            blow.stun_ticks > 0 && (!stun_type || (!stun_type->boss_column && stun_type->velocity != 0))) {
            const bool special = target.boss != d2d::rules::Boss::none && target.boss != d2d::rules::Boss::minion;
            if (!special || rng(100) >= 90) target.stun_until = target.stunned_until = now_ms + std::uint32_t(std::min(blow.stun_ticks, 250)) * 40;
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
        // Bone Armor's pool takes the record's fine physical (FUN_005c6370 on +8).
        const int taken = soak(target, blow.phys_fine, (blow.damage << 8) + blow.frac);
        if (hurt(*game_data, target, taken >> 8, now_ms, taken & 255, blow.hit_class, blow.elemental || blow.deadly)) { killed(monster_index, now_ms); return true; }
        // Knockback (result flag 8): a monster with GH and KB modes
        // (MonStats2, FUN_0046c140 bits 3 / 13; else it's a GH) goes into
        // KB (LAB_005a77d0): path type 8, 5 subtiles on from the attacker,
        // a Sand Leaper (class 0x4e) 10.
        // It slides there a subtile a frame (knock_slide: FUN_00623f50's KB
        // velocity 0x1000) till the path's end or the KB mode's (LAB_005a8630),
        // then plays GH (FUN_005a8520; a Sand Leaper thinks 15 frames on, 45
        // stunned): monster_update.
        if (blow.knockback) knock_back(target, actor->player.x, actor->player.y, now_ms);
        return true;
    }

auto Fight::knock_back(Monster& target, float from_x, float from_y, std::uint32_t now_ms) -> void {
        const auto& type_info = game_data->monsters.types[std::size_t(target.type)];
        if (!type_info.can_get_hit || !type_info.can_knock_back) return;   // LAB_005a77d0: FUN_0046c140 bits 3 and 13
        const float reach = type_info.base == 0x4e ? 2.f : 1.f;
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
            if (!monster.alive()) continue;
            // Life's one rate (FUN_005a6920, every frame): its regen (stat 0x4a)
            // less poison and bleeding; a gain held back under state 0x34 and at full life.
            const double net = (now_ms < monster.poison_until ? monster.poison_rate : 0) + (now_ms < monster.bleed_until ? monster.bleed_rate : 0)
                             - monster.stats.regen / (256.0 * 40);
            const bool full = monster.hit_points == monster.stats.hit_points && monster.life_frac == monster.max_life_frac;
            if (net <= 0 && (now_ms < monster.no_heal_until || full)) { monster.dot_acc = 0; continue; }
            monster.dot_acc += net * elapsed * 1000;
            const int whole = int(monster.dot_acc);
            if (whole == 0) continue;
            monster.dot_acc -= whole;
            if (whole < 0) heal(monster, -whole);
            else monster.hit_points -= whole;
            if (!monster.alive()) { set_mode(*game_data, monster, "DT", now_ms); killed(i, now_ms); }
        }
    }

auto Fight::strike(std::uint32_t now_ms) -> void {
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && missile_skill(*skill)) {   // Magic Arrow: the bow's action frame
            if (!actor->pstruck && now_ms >= actor->player.mode_ms + action_at()) { actor->pstruck = true; settle_cast(); do_sounds(now_ms); fire(*skill, now_ms); start_delay(now_ms); }
            return;
        }
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && (skill->srvdofunc == ServerDoFunction::kThrow || skill->srvdofunc == ServerDoFunction::kDoubleThrow)) {   // the throws (TH)
            if (!actor->pstruck && now_ms >= actor->player.mode_ms + action_at()) { actor->pstruck = true; do_sounds(now_ms); spot(*skill, now_ms); start_delay(now_ms); }
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
            || now_ms < actor->player.mode_ms + action_at()) return;
        actor->pstruck = true;
        do_sounds(now_ms);
        const int struck = actor->attack_mon;
        hit(now_ms);
        start_delay(now_ms);
        if (const auto* skill = game_data->skills.get(actor->swing_skill); skill && actor->swing_skill != 0 && skill->srvdofunc == ServerDoFunction::kRepeatedHit) repeat_hit(struck, now_ms);
    }

auto Fight::repeat_hit(int struck, std::uint32_t now_ms) -> void {
        if (actor->kicks_left <= 0) return;
        --actor->kicks_left;
        actor->attack_mon = struck;
        other_target();
        if (actor->attack_mon < 0) { actor->kicks_left = 0; return; }
        const auto* skill = game_data->skills.get(actor->swing_skill);
        const auto played = (now_ms - actor->player.mode_ms) / 40;   // game frames into the swing
        const auto frames_kept = played * std::uint32_t(std::clamp(100 - skill->par[1], 0, 100)) / 100;   // the animation's frames kept
        set_pmode(swing_mode(), now_ms);
        const auto& anim = player_anim(actor->pmode);
        const auto back = anim.frames ? frames_kept * (actor->pmode_until - now_ms) / anim.frames : 0;
        actor->player.mode_ms -= back;
        actor->pmode_until -= back;
        actor->pstruck = false;
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
        const auto target_view = target_of(target);
        auto blow = d2d::rules::player_blow(fighter, target_view, int(actor->character.stats.get(d2d::d2s::kLevel)), rng, current_swing);
        const auto frozen = freeze_on(target, target_view, blow, current_swing.freeze_len);
        if (land(target, blow, true, now_ms, player_foe_id())) player_thorns(target, blow);
        stand(target, frozen, now_ms);
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
        // proportion (state 0x6d keeps its own, stats 0xb0 / 0xb1): MulDiv of
        // the whole points by clvl / mlvl (FUN_00483360: ECX life, EDX clvl,
        // mlvl on the stack; 0x5d05ed / 0x5d0600), back in 256ths (the part of
        // a point dropped), life 1..max; d2d applies a minimum of one whole point during Conversion.
        if (blow.hit && skill && skill->srvdofunc == ServerDoFunction::kConversion && monsters[target].alive() && monsters[target].align == 0
            && swaps_ai(*game_data, monsters[target])) {
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            auto& turned = monsters[target];
            if (int(rng(100)) < calc(*skill, skill->calc[0], lvl)
                && put_state(*game_data, turned, turned.cry, { skill->id, lvl, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, lvl), 1)) * 40 }, now_ms)) {
                turned.align = 2; turned.set_kind = 0; turned.chase = -1;
                if (const int clvl = int(actor->character.stats.get(d2d::d2s::kLevel)); turned.stats.level > clvl && turned.own_level == 0) {
                    turned.own_level = turned.stats.level; turned.own_max_life = turned.stats.hit_points;
                    turned.own_max_life_frac = turned.max_life_frac;
                    turned.stats.hit_points = std::max(d2d::rules::mul_div(turned.max_life_fine() >> 8, clvl, turned.own_level), 1);
                    turned.hit_points = std::clamp(d2d::rules::mul_div(turned.hit_points - (turned.life_frac > 0), clvl, turned.own_level), 1, turned.stats.hit_points);
                    turned.max_life_frac = turned.life_frac = 0;
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
        const float x = monsters[monster_index].unit.x, y = monsters[monster_index].unit.y;
        if (skill.srvdofunc == ServerDoFunction::kChargedStrike) {
            // Charged Bolt's (flags 0x21, FUN_005c9290 with each one's index) at the
            // subtile twice the monster's less the player's.
            const auto subtile = [](float cells) { return int(std::floor(cells * 5)); };
            const float to_x = (float(2 * subtile(x) - subtile(actor->player.x)) + 0.5f) / 5, to_y = (float(2 * subtile(y) - subtile(actor->player.y)) + 0.5f) / 5;
            for (int k = 0; k < calc(skill, skill.calc[0], lvl); ++k) {
                auto& bolt = launch(found->second, skill, lvl, x, y, to_x - x, to_y - y, found->second.range + found->second.lev_range * lvl, now_ms);
                bolt.to = { to_x, to_y }; bolt.bolt = k;
                bolt.struck->push_back(int(monster_index));
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
                land(j, blow, true, now_ms, player_foe_id());
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
                    land(monster_index, blow, true, now_ms, striker_of(caster));
                }
                break;
            case ServerDoFunction::kMindBlast:
                within(caster.to_x, caster.to_y, calc(skill, skill.aurarange, lvl), [&](std::size_t monster_index) { land(monster_index, d2d::rules::missile_blow(damage, target_of(monster_index), pierce(), rng), true, now_ms, striker_of(caster)); });
                break;
            case ServerDoFunction::kStaticField: {
                // FUN_005c9800: calc1 %, calc2 the least (256ths), the floor
                // only in an expansion game; FUN_005c96a0 on each unit, its
                // record through FUN_0057c6c0 with the attacker's pierce.
                // Its walk (FUN_0056b7e0, the caster in EDX, x = y = 0: the
                // caster's spot) takes Skills.txt's aurafilter (+0x50, 0x8783):
                // a monster behind a wall (0x200, mask 4) isn't struck.
                const int pct = calc(skill, skill.calc[0], lvl), low = calc(skill, skill.calc[1], lvl);
                const int floor_pct = actor->character.header.expansion() ? game_data->static_field_min[std::size_t(std::clamp(game_difficulty, 0, 2))] : 0;
                const int ltng_pierce = pierce()[1];
                const float radius = float(calc(skill, skill.aurarange, lvl));
                for (std::size_t monster_index = 0; monster_index < monsters.size(); ++monster_index) {
                    const auto& struck = monsters[monster_index];
                    if (std::hypot(struck.unit.x - caster.x, struck.unit.y - caster.y) * 5 > radius || !filter_takes(skill.aura_filter, struck, caster.x, caster.y)) continue;
                    const int res = target_of(monster_index).res[3];
                    const int fine = d2d::rules::static_field_fine(struck.life_fine(), struck.max_life_fine(), pct, low, floor_pct, res);
                    if (fine < 0) continue;
                    const int got = d2d::rules::resisted_fine(fine, res < 100 ? std::max(res - ltng_pierce, -100) : res);
                    d2d::rules::Blow blow{ .hit = true };
                    blow.damage = got >> 8;
                    blow.frac = got & 255;
                    blow.elemental = got > 0;
                    land(monster_index, blow, true, now_ms, striker_of(caster));
                }
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
                // Fight::leap. Leap's clear line is FUN_0064e260's from the
                // caster's subtile (mask 0x804, no sizes); Teleport 2's is
                // FUN_006229f0's: FUN_00622920 with the caster's size
                // (FUN_00620510: a player's 2) and 2 at the point (0x622a80).
                float target_x = actor->cast_x, target_y = actor->cast_y;
                const int tgt = actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive() ? actor->attack_mon : -1;
                const int from_x = int(std::floor(actor->player.x * 5)), from_y = int(std::floor(actor->player.y * 5));
                const auto wall = [&](int at_x, int at_y) { return level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x804); };
                const auto clear_to = [&](int to_x, int to_y) { return !d2d::rules::sight_blocked(from_x, from_y, 0, to_x, to_y, 0, wall); };
                const auto clear_line = [&](float to_x, float to_y) {
                    return !d2d::rules::sight_blocked(from_x, from_y, 2, int(std::floor(to_x * 5)), int(std::floor(to_y * 5)), 2, wall);
                };
                if (skill.srvdofunc == ServerDoFunction::kDragonFlight) {
                    if (tgt < 0) break;
                    const auto& target = monsters[std::size_t(tgt)];
                    if (std::hypot(target.unit.x - actor->player.x, target.unit.y - actor->player.y) * 5 > float(calc(skill, skill.aurarange, lvl))) break;
                    const float dx = actor->player.x - target.unit.x, dy = actor->player.y - target.unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                    target_x = target.unit.x + dx / distance * 0.8f; target_y = target.unit.y + dy / distance * 0.8f;
                }
                if (skill.srvdofunc == ServerDoFunction::kLeap) {
                    if (in_town(level)) break;
                    Crowd crowd_now;
                    crowd(crowd_now);
                    const auto landing = d2d::rules::leap_landing(from_x, from_y, int(std::floor(target_x * 5)), int(std::floor(target_y * 5)), calc(skill, skill.aurarange, lvl),
                        [&](int at_x, int at_y) { return free_spot_near(*level, crowd_now, &actor->player, at_x, at_y, 2, 0x1c09); }, clear_to);
                    if (!landing) break;
                    target_x = (float(landing->first) + 0.5f) / 5; target_y = (float(landing->second) + 0.5f) / 5;
                    const float speed = cells_per_sec(float(game_data->run_velocity[std::size_t(std::max(actor->character.character_class, 0))]));
                    const auto flight = std::uint32_t(std::hypot(target_x - actor->player.x, target_y - actor->player.y) / std::max(speed, 0.1f) * 1000);
                    actor->leap = Leap{ actor->player.x, actor->player.y, target_x, target_y, now_ms, now_ms + flight, skill.id, lvl };
                    actor->player.path.clear(); actor->player.walking = false;
                    break;
                }
                const bool teleporting = skill.srvdofunc == ServerDoFunction::kTeleport;
                if (in_town(level) || (!teleporting && level->unit_blocked(target_x, target_y)) || level->teleport == 0 || (level->teleport == 2 && !clear_line(target_x, target_y))) break;
                // Teleport (do 27, FUN_005ca360 -> FUN_00554ea0 -> FUN_00463740): only a point in the caster's room or its near list.
                if (teleporting) {
                    const int sub_x = level->world_x * 5 + int(std::floor(target_x * 5)), sub_y = level->world_y * 5 + int(std::floor(target_y * 5));
                    const auto rects = missile_rooms(*game_data, *level, actor->player.x, actor->player.y);
                    if (!std::ranges::any_of(rects, [&](const std::array<int, 4>& rect) {
                            return sub_x >= rect[0] && sub_y >= rect[1] && sub_x < rect[0] + rect[2] && sub_y < rect[1] + rect[3]; })) break;
                    // Then the nearest free spot to it (FUN_0064e7b0), the point itself when it is free.
                    const int spot_x = int(std::floor(target_x * 5)), spot_y = int(std::floor(target_y * 5));
                    const auto spot = level->warp_spot(spot_x, spot_y);
                    if (!spot) break;
                    if (*spot != std::pair{ spot_x, spot_y }) { target_x = (float(spot->first) + 0.5f) / 5; target_y = (float(spot->second) + 0.5f) / 5; }
                }
                actor->player.x = target_x; actor->player.y = target_y;
                actor->player.path.clear(); actor->player.walking = false;
                actor->player.goal_x = target_x; actor->player.goal_y = target_y;
                actor->reassigned = true;                                           // FUN_00554ea0's 0x10000: the client's 0x15
                if (skill.srvdofunc == ServerDoFunction::kDragonFlight) {             // the kick, then its charges released, hit or not (FUN_005d5220)
                    auto flight_swing = skill_swing(skill, lvl, env, true);
                    flight_swing.ar_pct += progressive_tohit();                     // FUN_005d7850: FUN_006449f0 + stat 0x145
                    const auto kick = d2d::rules::player_blow(actor->pf_kick, target_of(std::size_t(tgt)), int(actor->character.stats.get(d2d::d2s::kLevel)), rng, flight_swing);
                    if (land(std::size_t(tgt), kick, true, now_ms, striker_of(caster))) player_thorns(std::size_t(tgt), kick);   // a melee blow like any kick
                    release(std::size_t(tgt), now_ms);
                }
                break;
            }
            case ServerDoFunction::kTelekinesis:                                         // Telekinesis (do 21): its damage and a knockback on the monster
                if (actor->attack_mon >= 0 && monsters[std::size_t(actor->attack_mon)].alive()) {
                    auto blow = d2d::rules::missile_blow(damage, target_of(std::size_t(actor->attack_mon)), pierce(), rng);
                    blow.knockback = true;
                    land(std::size_t(actor->attack_mon), blow, true, now_ms, striker_of(caster));
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
            // with calc1 damage % (stat 25), spending one of it (FUN_0056ecb0's
            // last argument 1 -> FUN_0056c3f0) and toht (stat 19, FUN_006449f0);
            // none left, none flies.
            // ponytail: both hands throw at once, not one an SQ event (FUN_00535bc0's hand).
            // Throw (do 3, FUN_0056f460): the item rules::throw_hand takes (the
            // wielded weapon, or the other hand's) flies as its missile
            // (weapons.txt missiletype) and one of it is spent. Its
            // FUN_0056c600 puts the throw mastery on the missile (stat 0x13 +
            // FUN_00645720 kind 0, stat 0x19 = kind 1), no skill bonus: here the
            // player's fighter carries it while throwing (player_fighter), as it
            // does the weapon's throw damage (minmisdam / maxmisdam).
            case ServerDoFunction::kThrow:
                if (const int index = d2d::rules::throw_hand(game_data->rules, actor->character.items); index >= 0 && actor->character.items[std::size_t(index)].quantity > 0)
                    if (const auto found_thrown = game_data->thrown.find(actor->character.items[std::size_t(index)].code); found_thrown != game_data->thrown.end())
                        if (const auto found = game_data->missiles.find(found_thrown->second); found != game_data->missiles.end()) {
                            launch(found->second, skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y, found->second.range + found->second.lev_range * lvl, now_ms);
                            spend_ammo(index);
                        }
                break;
            case ServerDoFunction::kDoubleThrow:
                for (const int slot : { d2d::d2s::body_location::kRightArm, d2d::d2s::body_location::kLeftArm }) {
                    auto& items = actor->character.items;
                    const auto hand = std::ranges::find_if(items, [&](const d2d::d2s::Item& item) { return item.location == d2d::d2s::item_location::kEquipped && item.slot == slot; });
                    if (hand == items.end() || hand->quantity < 1) continue;
                    const auto found_thrown = game_data->thrown.find(hand->code);
                    const auto found = found_thrown != game_data->thrown.end() ? game_data->missiles.find(found_thrown->second) : game_data->missiles.end();
                    if (found == game_data->missiles.end()) continue;
                    auto& thrown = launch(found->second, skill, lvl, actor->player.x, actor->player.y, actor->cast_x - actor->player.x, actor->cast_y - actor->player.y,
                                          found->second.range + found->second.lev_range * lvl, now_ms);
                    thrown.ed_pct = calc(skill, skill.calc[0], lvl);
                    thrown.ar_pct = d2d::rules::skill_tohit(game_data->skills, skill, calc_env(), lvl);
                    const auto index = std::size_t(hand - items.begin());
                    if (--items[index].quantity <= 0) out_of_ammo(index);
                }
                break;
            // Find Potion (do 69, FUN_005d81c0) / Find Item (72, FUN_005d8780):
            // a corpse not yet searched (state 0x76), at calc1 %: a potion
            // (FUN_005d8100), or the monster's treasure again (FUN_005a8000,
            // a tier by Param1..4, which FUN_005a8000 never reads: the monster's
            // own class, bugs.md #44); the potion by the act and difficulty
            // (rules::find_potion).
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
                    --actor->kicks_left;                            // Talon's next kick (FUN_005d5a30); do 13's go on the action frame (repeat_hit)
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
            if (!actor->pstruck && now_ms >= actor->player.mode_ms + actor->action_ms) {
                actor->pstruck = true;
                const auto* skill = game_data->skills.get(actor->swing_skill);
                // A shift back's do gives 0, so with no srvmissile FUN_0056f7f0 stops
                // before the cost; FUN_0056f020 still starts the delay.
                const bool shifted_back = skill && skill->srvdofunc == ServerDoFunction::kShapeShift && shift_back(*skill);
                if (shifted_back) actor->owed = {};
                else settle_cast();
                do_sounds(now_ms);
                if (shifted_back) {
                    // nothing goes on
                } else if (skill && skill->srvdofunc == ServerDoFunction::kBookOrScroll) {
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
                start_delay(now_ms);
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
            const int ticks = d2d::rules::attack_ticks(int(attack_anim.frames ? attack_anim.frames : 16), int(attack_anim.speed ? attack_anim.speed : 256), actor->player_combat.ias, actor->player_combat.wsm, actor->player_combat.sias + chill_rate(now_ms));
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

}  // namespace d2d::game
