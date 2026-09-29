// Definitions for fight.hpp: Fight, the combat subsystem the World owns.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s_items.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <sequences.hpp>
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
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

auto Fight::monster_index(int id) const -> int {
        if (id < 0) return -1;
        const auto found = std::ranges::find(monsters, id, &Monster::id);
        return found == monsters.end() ? -1 : int(found - monsters.begin());
    }

auto Fight::add_monster(Monster monster) -> void { monster.id = next_id++; monsters.push_back(std::move(monster)); }

auto Fight::own_stats() const -> d2d::d2s::Stats {
        using namespace d2d::d2s;
        auto stats = character.stats;
        stats.values[kMaxLife] -= bo_life + item_max[0]; stats.values[kMaxMana] -= bo_mana + item_max[1]; stats.values[kMaxStamina] -= item_max[2];
        return stats;
    }

auto Fight::new_game(int difficulty) -> void {
        game_difficulty = difficulty;
        spawning = start_spawning(*game_data, difficulty);
        monsters.clear();
        mon_level = nullptr;
        amplified = {};
        kept.clear();
        next_id = 1;
        missiles.clear();
        regen.clear();
        charges.clear();
        self_states.clear();
        boost = {};
        pets.clear();
        attack_mon = -1;
        pmode = -1;
    }

auto Fight::merc_joins() -> void {
        const auto& header = character.header;
        merc_st = d2d::rules::merc_stats(game_data->rules, header.merc_type, header.merc_exp);
        merc_life = merc_st.life;
        merc_mode = "NU"; merc_target = -1;
    }

auto Fight::revive(std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        character.stats.values[kLife] = character.stats.values[kMaxLife];
        regen.clear();
        charges.clear();
        self_states.clear();
        pmode = -1; player.mode_ms = now_ms; attack_mon = -1;
        if (merc) { merc->x = player.x + 1; merc->y = player.y + 1; merc->path.clear(); merc_mode = "NU"; merc_target = -1; }
    }

auto Fight::drink(int col, std::uint32_t now_ms) -> void {
        if (!dead()) potion(d2d::rules::drink_belt(game_data->rules, character.items, col), now_ms);
    }

auto Fight::drink_item(int id, std::uint32_t now_ms) -> void {
        if (!dead()) potion(d2d::rules::drink_item(game_data->rules, character.items, id), now_ms);
    }

auto Fight::potion(const std::string& code, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        if (code.empty()) return;
        const auto& potion = game_data->rules.potions.at(code);
        if (potion.percent) {
            character.stats.values[kLife] = std::min(character.stats.values[kMaxLife], character.stats.values[kLife] + character.stats.values[kMaxLife] * potion.life / 100);
            character.stats.values[kMana] = std::min(character.stats.values[kMaxMana], character.stats.values[kMana] + character.stats.values[kMaxMana] * potion.mana / 100);
        } else {
            const double len = std::max(potion.ticks, 1) * 40.0;
            regen.push_back({ potion.life * 256.0 / len, potion.mana * 256.0 / len, now_ms + std::uint32_t(len) });
        }
        cues.cue("item_potion_drink", now_ms, player.x, player.y);
    }

auto Fight::apply_regen(std::uint32_t now_ms, std::uint32_t last_ms) -> void {
        using namespace d2d::d2s;
        double life = 0, mana = 0;
        for (const auto& regen_entry : regen) {
            const double duration = double(std::min(now_ms, regen_entry.until) - std::min(last_ms, regen_entry.until));
            life += regen_entry.life * duration; mana += regen_entry.mana * duration;
        }
        std::erase_if(regen, [&](const Regen& regen_entry) { return now_ms >= regen_entry.until; });
        const double elapsed = double(now_ms - last_ms);
        life += player_combat.life_regen * 256.0 / 256.0 * elapsed / 40.0;
        mana += double(character.stats.values[kMaxMana]) / 120000.0 * (100 + player_combat.mana_regen) / 100.0 * elapsed;
        regen_acc_life += life; regen_acc_mana += mana;
        const auto life_gain = std::int64_t(regen_acc_life), mana_gain = std::int64_t(regen_acc_mana);
        regen_acc_life -= double(life_gain); regen_acc_mana -= double(mana_gain);
        auto& life_stat = character.stats.values[kLife];
        if (life_gain < 0) life_stat = std::max<std::int64_t>(std::min<std::int64_t>(life_stat, 256), life_stat + life_gain);
        else life_stat = std::max(life_stat, std::min(character.stats.values[kMaxLife], life_stat + life_gain));
        character.stats.values[kMana] = std::max(character.stats.values[kMana], std::min(character.stats.values[kMaxMana], character.stats.values[kMana] + mana_gain));
    }

auto Fight::monster_sounds(Monster& monster, std::uint32_t now_ms) -> void {
        if (monster.mode == monster.last_mode) return;
        monster.last_mode = monster.mode;
        const auto found = game_data->mon_sounds.find(game_data->monsters.types[std::size_t(monster.type)].sound);
        if (found == game_data->mon_sounds.end()) return;
        const auto& sounds = found->second;
        if (monster.mode == "A1" || monster.mode == "A2") {
            const std::size_t attack_index = monster.mode == "A2";
            if (rng(100) < sounds.att_prb[attack_index]) cues.cue(sounds.attack[attack_index], now_ms + std::uint32_t(sounds.att_del[attack_index]) * 40, monster.unit.x, monster.unit.y);
            cues.cue(sounds.weapon[attack_index], now_ms + std::uint32_t(sounds.wea_del[attack_index]) * 40, monster.unit.x, monster.unit.y);
        } else if (monster.mode == "GH") {
            cues.cue(sounds.hit, now_ms + std::uint32_t(sounds.hit_del) * 40, monster.unit.x, monster.unit.y);
        } else if (monster.mode == "DT") {
            cues.cue(sounds.death, now_ms + std::uint32_t(sounds.death_del) * 40, monster.unit.x, monster.unit.y);
        }
    }

auto Fight::gfx() const -> const GameData::Appearance& {
        return character.appearance ? *character.appearance : game_data->starting_gear[std::size_t(std::max(character.character_class, 0))];
    }

auto Fight::player_anim(int mode) const -> const GameData::AnimTiming& {
        return game_data->composite_timing(std::max(character.character_class, 0), mode, gfx());
    }

auto Fight::set_pmode(int mode, std::uint32_t now_ms) -> void {
        pmode = mode;
        player.mode_ms = now_ms;
        player.walking = false;
        player.path.clear();
        const auto& anim = player_anim(mode);
        std::uint32_t len = anim.length_ms();
        if (attack_mode(mode) && anim.frames) {
            const auto ticks = d2d::rules::attack_ticks(int(anim.frames), int(anim.speed ? anim.speed : 256), player_combat.ias, player_combat.wsm);
            len = std::uint32_t(ticks) * 40;
        } else if (mode == kModeGH || mode == kModeBL || mode == kModeSC) {
            const int frame_rate = mode == kModeGH ? player_combat.fhr : mode == kModeBL ? player_combat.fbr : player_combat.fcr;
            len = len * 100 / std::uint32_t(100 + d2d::rules::effective_speed(frame_rate));
        }
        prate = len ? float(anim.length_ms()) / float(len) : 1.f;
        pmode_until = mode == kModeDD ? 0 : now_ms + len;
    }

auto Fight::dead() const -> bool { return pmode == kModeDT || pmode == kModeDD; }

auto Fight::player_fighter(d2d::rules::Fighter* kick ,
                                                     const d2d::rules::StatSum* states ,
                                                     const std::vector<d2d::rules::PassiveStat>* passives ,
                                                     d2d::rules::StatSum* sum_out ) const -> d2d::rules::Fighter {
        d2d::rules::StatSum sum = states ? *states : d2d::rules::StatSum{}, weapon_sum{};
        const d2d::d2s::Item *weapon = nullptr, *shield = nullptr, *boots = nullptr;
        auto add = [](d2d::rules::StatSum& into, const std::vector<d2d::d2s::ItemProp>& props) {
            for (const auto& prop : props) if (prop.stat >= 0 && std::size_t(prop.stat) < into.size()) into[std::size_t(prop.stat)] += prop.value;
        };
        for (const auto& item : character.items) {
            const bool worn = item.location == 1 && item.slot >= 1 && item.slot <= 10;
            const bool charm = item.location == 0 && item.panel == 1 && (item.code == "cm1" || item.code == "cm2" || item.code == "cm3");
            if (!worn && !charm) continue;
            add(sum, item.props);
            for (const auto& socketed : item.socketed_items) add(sum, socket_props(*game_data, item, socketed));
            if (worn && item.slot == 9) boots = &item;
            if (!worn || (item.slot != 4 && item.slot != 5)) continue;
            const auto found = game_data->rules.item_base.find(item.code);
            const auto info = game_data->rules.item_info.find(item.code);
            if (found == game_data->rules.item_base.end() || info == game_data->rules.item_info.end()) continue;
            if (info->second.kind == 2 && (!weapon || item.slot == 4)) weapon = &item;
            if (info->second.kind == 1 && found->second.block > 0) shield = &item;
        }
        if (passives) {
            const auto type_of = [&](const d2d::d2s::Item& item) {
                const auto found = game_data->rules.item_info.find(item.code);
                return found == game_data->rules.item_info.end() ? std::string{} : found->second.type;
            };
            std::vector<std::string> hands;                  // the types in slots 4 / 5
            for (const auto& item : character.items)
                if (item.location == 1 && (item.slot == 4 || item.slot == 5)) hands.push_back(type_of(item));
            const auto is_type = [&](const std::string& type, const std::string& want) { return d2d::rules::type_is(game_data->rules, type, want); };
            const bool claws2 = hands.size() == 2 && is_type(hands[0], "h2h") && is_type(hands[1], "h2h");
            std::array<std::int64_t, 4> best{};              // 342, 343, 344, 348
            for (const auto& passive : *passives) {
                if (passive.stat < 0 || std::size_t(passive.stat) >= sum.size()) continue;
                if (passive.itype.empty()) { sum[std::size_t(passive.stat)] += passive.value; continue; }
                const std::size_t mastery = passive.stat >= 342 && passive.stat <= 344 ? std::size_t(passive.stat - 342) : passive.stat == 348 ? 3 : 4;
                if (mastery == 4) continue;
                const bool fits = mastery < 3 ? weapon && is_type(type_of(*weapon), passive.itype)
                                        : claws2 && std::ranges::any_of(hands, [&](const auto& hand_type) { return is_type(hand_type, passive.itype); });
                if (fits) best[mastery] = std::max<std::int64_t>(best[mastery], passive.value);
            }
            for (std::size_t k = 0; k < 3; ++k) sum[342 + k] += best[k];
            sum[348] += best[3];
        }
        if (weapon) {                                        // its own enhanced damage (op 13), sockets included
            add(weapon_sum, weapon->props);
            for (const auto& item : weapon->socketed_items) add(weapon_sum, socket_props(*game_data, *weapon, item));
        }
        if (sum_out) *sum_out = sum;
        const auto& resistances = character.panel.res;                        // panel: fire, cold, lightning, poison
        const auto& gains = game_data->class_gains[std::size_t(std::max(character.character_class, 0))];
        const std::array<int, 4> res{ int(resistances[0]), int(resistances[2]), int(resistances[1]), int(resistances[3]) };
        auto fighter = d2d::rules::make_fighter(game_data->rules, weapon, shield, sum, weapon_sum, character.stats, gains,
                                          int(character.panel.defense), res, boots);
        if (kick) {                                          // the weapon's stats off, its attack rating kept
            auto bare = sum;
            for (std::size_t i = 0; i < bare.size(); ++i) bare[i] -= weapon_sum[i];
            *kick = d2d::rules::make_fighter(game_data->rules, nullptr, shield, bare, {}, character.stats, gains,
                                             int(character.panel.defense), res, boots);
            kick->ar_base = fighter.ar_base; kick->ar_pct = fighter.ar_pct; kick->attack_rating = fighter.attack_rating;
            kick->kick_pct = fighter.kick_pct;
        }
        return fighter;
    }

auto Fight::update_fighters(std::uint32_t now_ms) -> void {
        std::erase_if(self_states, [&](const SelfState& state) { return now_ms >= state.until; });
        d2d::rules::StatSum st_sum{};
        const auto env = calc_env();
        const auto passives = d2d::rules::passive_stats(game_data->skills, env);
        character.panel = panel_stats(*game_data, character.header, character.items, character.stats, &passives);
        auto states = self_states;
        if (const auto* skill = game_data->skills.get(aura); skill && skill->srvdofunc == 65) states.push_back({ aura, skill_level ? skill_level(aura) : 1, ~0u });
        // A totem's aura (Oak Sage, Heart of Wolverine, Spirit of Barbs: do
        // 65) while the player is within its aurarange of it.
        for (const auto& pet : pets)
            if (const auto* skill = game_data->skills.get(pet.aura); skill && skill->srvdofunc == 65 && pet.monster.alive() && pet.where == level
                && std::hypot(pet.monster.unit.x - player.x, pet.monster.unit.y - player.y) * 5 <= float(calc(*skill, skill->aurarange, pet.aura_level)))
                states.push_back({ pet.aura, pet.aura_level, ~0u });
        for (const auto& state : states) {
            const auto* skill = game_data->skills.get(state.skill);
            for (std::size_t i = 0; skill && i < skill->aurastat.size(); ++i)
                if (const int id = skill->aurastat[i]; id >= 0 && id != 6 && std::size_t(id) < st_sum.size())
                    st_sum[std::size_t(id)] += d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], env, skill->id, state.level);
        }
        if (now_ms < boost.until)
            for (const auto& [id, value] : boost.stats) if (std::size_t(id) < st_sum.size()) st_sum[std::size_t(id)] += value;
        // Battle Orders (item_maxhp_percent 76, item_maxmana_percent 77): the
        // maxima up while it lasts, life and mana with them.
        {
            using namespace d2d::d2s;
            const std::int64_t base_life = character.stats.values[kMaxLife] - bo_life, base_mana = character.stats.values[kMaxMana] - bo_mana;
            const std::int64_t want_life = base_life * st_sum[76] / 100, want_mana = base_mana * st_sum[77] / 100;
            character.stats.values[kMaxLife] += want_life - bo_life; character.stats.values[kLife] = std::min(character.stats.values[kLife] + std::max<std::int64_t>(want_life - bo_life, 0), character.stats.values[kMaxLife]);
            character.stats.values[kMaxMana] += want_mana - bo_mana; character.stats.values[kMana] = std::min(character.stats.values[kMana] + std::max<std::int64_t>(want_mana - bo_mana, 0), character.stats.values[kMaxMana]);
            bo_life = want_life; bo_mana = want_mana;
            // Items' +life / mana / stamina (and their attributes' share):
            // the maxima follow what's worn; life and mana don't rise with
            // them, only stay under them.
            constexpr std::array<std::pair<int, int>, 3> kMax{ { { kMaxLife, kLife }, { kMaxMana, kMana }, { kMaxStamina, kStamina } } };
            constexpr std::array<std::size_t, 3> kBonus{ 7, 9, 11 };
            for (std::size_t k = 0; k < 3; ++k) {
                const std::int64_t want = character.panel.bonus[kBonus[k]] * 256;
                auto& maximum = character.stats.values[std::size_t(kMax[k].first)];
                maximum += want - item_max[k];
                character.stats.values[std::size_t(kMax[k].second)] = std::min(character.stats.values[std::size_t(kMax[k].second)], maximum);
                item_max[k] = want;
            }
        }
        player_combat = player_fighter(&pf_kick, &st_sum, &passives, &psum);
        constexpr int kRes[4] = { 39, 41, 43, 45 };          // Fighter::res order: fire, lightning, cold, poison
        for (std::size_t k = 0; k < 4; ++k)                  // a resist aura's on top of the panel's (Salvation, Resist Fire, ...)
            if (const auto value = st_sum[std::size_t(kRes[k])]; value != 0) player_combat.res[k] = int(std::min<std::int64_t>(player_combat.res[k] + value, 95));
        player_combat.ias += int(st_sum[68]);
        player_combat.frw += int(st_sum[67]);
        for (const auto& passive : passives) if (passive.stat == 67 && passive.itype.empty()) player_combat.frw += passive.value;   // Increased Speed
        player_combat.defense += int(player_combat.defense * st_sum[171] / 100);
        player_combat.defense = std::max(int(player_combat.defense + player_combat.defense * st_sum[182] / 100), 0);
    }

auto Fight::calc_env() -> d2d::rules::CalcEnv {
        return { skill_base, skill_level, [this](int id) { return id >= 0 && std::size_t(id) < psum.size() ? int(psum[std::size_t(id)]) : 0; },
                 int(character.stats.get(d2d::d2s::kLevel)), &rng };
    }

auto Fight::start_swing(std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        swing_skill = 0;
        kicks_left = 0;
        const auto* skill = game_data->skills.get(attack_skill);
        if (skill && attack_skill != 0) {
            if (!skill_built(*skill)) {
                if (std::ranges::find(told, attack_skill) == told.end()) {
                    told.push_back(attack_skill);
                    d2d::log::info("not implemented: skill {} (srvstfunc {}, srvdofunc {}) - a plain attack for now",
                                   skill->name, skill->srvstfunc, skill->srvdofunc);
                }
            } else {
                const int lvl = skill_level ? skill_level(attack_skill) : 0;
                const int cost = d2d::rules::mana_cost(*skill, lvl);
                if (lvl > 0 && character.stats.values[kMana] >= cost) {
                    character.stats.values[kMana] -= cost;
                    swing_skill = attack_skill;
                    if (skill->srvstfunc == 24 || skill->srvstfunc == 37 || skill->srvstfunc == 9) {   // Talon's kicks (FUN_005d5970), Zeal's hits (FUN_005daf40)
                        const auto env = calc_env();
                        int value = d2d::rules::eval_calc(game_data->skills, skill->calc[0], env, skill->id, lvl);
                        if (skill->srvstfunc == 9) value = std::min(value, int(in_reach().size()));   // Fend (FUN_005dae30): one per enemy in reach (FUN_0056bc80), at most calc1
                        kicks_left = std::max(value, 1) - 1;
                    }
                } else if (!skill->attack_no_mana) {
                    attack_mon = -1;                         // can't pay, won't swing
                    return false;
                }
            }
        }
        const auto* used = game_data->skills.get(swing_skill);
        set_pmode(swing_mode(), now_ms);
        start_sequence(now_ms);
        if (used && swing_skill != 0 && used->srvdofunc == 2 && used->aurastat[0] >= 0) self_state(*used, now_ms);
        pstruck = false;
        return true;
    }

auto Fight::start_sequence(std::uint32_t now_ms) -> void {
        seq = {};
        seq_struck = 0;
        const auto* skill = game_data->skills.get(swing_skill);
        if (!skill || swing_skill == 0 || skill->anim != "SQ" || skill->seqnum <= 0) return;
        const auto& attack_anim = player_anim(kModeA1);
        std::string weapon_class = attack_anim.name.size() >= 7 ? attack_anim.name.substr(4) : "hth";
        for (auto& letter : weapon_class) letter = char(std::tolower((unsigned char)letter));
        seq = d2d::rules::sequence(skill->seqnum, weapon_class);
        if (seq.empty()) return;
        start_move(*skill, now_ms);
        const auto ticks = d2d::rules::attack_ticks(int(seq.size()), int(attack_anim.speed ? attack_anim.speed : 256), player_combat.ias, player_combat.wsm);
        pmode_until = now_ms + std::uint32_t(ticks) * 40;
        seq_frame_ms = std::max<std::uint32_t>(std::uint32_t(ticks) * 40 / std::uint32_t(seq.size()), 1);
        prate = 1.f;
    }

auto Fight::start_move(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        smove = {};
        seq_loop = false;
        const auto cls = std::size_t(std::max(character.character_class, 0));
        if (skill.srvdofunc == 76) {
            smove = { move_x, move_y, cells_per_sec(float(game_data->walk_velocity[cls])), true, false };
            seq_loop = true;
            pmode_until = ~0u;
            whirl_next = now_ms;
        } else if (skill.srvdofunc == 67) {
            smove = { move_x, move_y, cells_per_sec(float(game_data->run_velocity[cls])) * float(std::max(100, 50) + skill.par[0]) / 100.f, true, false };
            seq_loop = true;                                 // the run starts over until it reaches (FUN_005cf900)
            pmode_until = ~0u;
        }
    }

auto Fight::seq_at(std::uint32_t now_ms) const -> std::size_t {
        const auto frame_index = std::size_t((now_ms - player.mode_ms) / seq_frame_ms);
        return seq_loop ? frame_index % seq.size() : std::min(frame_index, seq.size() - 1);
    }

auto Fight::seq_events(std::uint32_t now_ms) const -> int {
        const auto frame_index = std::size_t((now_ms - player.mode_ms) / seq_frame_ms);
        const auto per = std::ranges::count(seq, 1, &d2d::rules::SeqFrame::event);
        const auto last = seq_loop ? frame_index : std::min(frame_index, seq.size() - 1);
        int events = seq_loop ? int(last / seq.size() * std::size_t(per)) : 0;
        for (std::size_t k = 0; k <= last % (seq_loop ? seq.size() : ~std::size_t{ 0 }) && k < seq.size(); ++k) events += seq[k].event == 1;
        return events;
    }

auto Fight::seq_view(std::uint32_t now_ms) const -> std::pair<int, std::uint32_t> {
        const auto& frame = seq[seq_at(now_ms)];
        const auto mpf = player_anim(frame.mode).ms_per_frame();
        return { frame.mode, now_ms - mpf * frame.frame - mpf / 2 };
    }

auto Fight::swing_mode() const -> int {
        const auto* skill = game_data->skills.get(swing_skill);
        if (!skill || swing_skill == 0) return kModeA1;
        return skill->anim == "KK" ? kModeKK : skill->anim == "S1" ? kModeS1 : kModeA1;
    }

auto Fight::swing() -> d2d::rules::Swing {
        d2d::rules::Swing swing;
        const auto* skill = game_data->skills.get(swing_skill);
        if (!skill || swing_skill == 0) return swing;
        const int lvl = skill_level ? skill_level(swing_skill) : 1;
        const auto env = calc_env();
        const auto& skill_tables = game_data->skills;
        swing.ar_pct = d2d::rules::skill_tohit(skill_tables, *skill, env, lvl);
        if (skill->srvstfunc == 23) return swing;                   // a charge-up: a plain hit at its to-hit (FUN_005d3490)
        if (skill->srvstfunc == 27) {                            // Dragon Tail: a kick (FUN_005d7090 -> FUN_005d54b0)
            swing.kick = true;
            return swing;
        }
        if (skill->srvstfunc == 24) {
            swing.kick = true;
            swing.ed_pct = d2d::rules::calc_ln(skill->par[0], skill->par[1], lvl);
            swing.skill_lo = d2d::rules::skill_phys(skill_tables, *skill, env, lvl, false);
            swing.skill_hi = d2d::rules::skill_phys(skill_tables, *skill, env, lvl, true);
            swing.knockback = kicks_left == 0;
        } else if (skill->srvdofunc == 150) {                    // Smite (FUN_005ce9f0): calc1 ED, calc2 stun
            swing.ar_pct = 0;
            swing.smite = true;
            // With Holy Shield up (state 0x65), its damage (MinDam..MaxDam
            // by level) joins the shield's.
            for (const auto& state : self_states)
                if (const auto* self_skill = skill_tables.get(state.skill); self_skill && self_cast(*self_skill)) {
                    swing.skill_lo = d2d::rules::skill_phys(skill_tables, *self_skill, env, state.level, false);
                    swing.skill_hi = d2d::rules::skill_phys(skill_tables, *self_skill, env, state.level, true);
                }
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            swing.stun_ticks = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            swing.knockback = (skill->result_flags & 8) != 0;
        } else if (skill->srvdofunc == 13) {                     // Zeal's hit (FUN_005dbc60): calc2 ED, no ResultFlags
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            if (skill->etype >= 0 && skill->etype < 5)
                if (const int converted = d2d::rules::eval_calc(skill_tables, skill->calc[3], env, skill->id, lvl); converted > 0) { swing.conv_type = skill->etype; swing.conv_pct = converted; }
        } else if (skill->srvstfunc == 29) {                     // Sacrifice (FUN_005ce790): calc1 ED on the weapon's physical
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            swing.srcdam = skill->srcdam;
            swing.knockback = (skill->result_flags & 8) != 0;
        } else if (skill->srvstfunc == 35) {                     // Vengeance (FUN_005cfe10)
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
            if (skill->srvstfunc == 6) swing.ar_pct = 0;
            swing.ed_pct = d2d::rules::eval_calc(skill_tables, skill->calc[0], env, skill->id, lvl);
            if (skill->srvstfunc == 32 || skill->srvdofunc == 70) swing.flat = d2d::rules::eval_calc(skill_tables, skill->calc[1], env, skill->id, lvl);
            swing.srcdam = skill->srcdam;
            swing.knockback = (skill->srvstfunc != 6 && (skill->result_flags & 8) != 0) || skill->srvdofunc == 67 || skill->srvdofunc == 78;
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

auto Fight::land(std::size_t monster_index, const d2d::rules::Blow& blow, bool by_player, std::uint32_t now_ms) -> void {
        using namespace d2d::d2s;
        auto& target = monsters[monster_index];
        if (blow.blocked) { block_anim(*game_data, target, now_ms); return; }
        if (!blow.hit) return;
        if (by_player) {
            character.stats.values[kLife] = std::min(character.stats.values[kMaxLife], character.stats.values[kLife] + (std::int64_t(blow.life) << 8));
            character.stats.values[kMana] = std::min(character.stats.values[kMaxMana], character.stats.values[kMana] + (std::int64_t(blow.mana) << 8));
        }
        // One poison at a time, the stronger wins (FUN_0057ac50); at 0 life
        // the monster dies to it, the kill the poisoner's (FUN_005a6920).
        if (const double rate = double(blow.poison) / (std::max(blow.poison_ticks, 1) * 40.0);
            blow.poison > 0 && (now_ms >= target.poison_until || rate >= target.poison_rate)) {
            target.poison_rate = rate;
            target.poison_until = now_ms + std::uint32_t(blow.poison_ticks) * 40;
        }
        if (blow.chill_ticks > 0) target.chill_until = now_ms + std::uint32_t(blow.chill_ticks) * 40;
        // Stunned (state 21, FUN_0057aae0): it stands until the stun ends, a
        // new stun resetting the length.
        // ponytail: its guards aren't applied: special monsters' 90 % to
        // shrug it off (FUN_005a0180), the MonStats flag immunity and the
        // act bosses' 13-frame cap; the item stun length (stat 66) isn't added.
        if (blow.stun_ticks > 0) target.stun_until = now_ms + std::uint32_t(blow.stun_ticks) * 40;
        if (blow.bleed) {
            target.bleed_rate = d2d::rules::open_wounds_per_sec(int(character.stats.get(kLevel))) / 1000.0;
            target.bleed_until = now_ms + 8000;
        }
        // Life Tap (damagedinmelee / damagedbymissile, func 5): calc1 % of the
        // damage heals whoever dealt it.
        if (const auto* curse = target.curse.skill >= 0 ? game_data->skills.get(target.curse.skill) : nullptr; by_player && curse && curse->auratarget == "lifetap")
            character.stats.values[kLife] = std::min(character.stats.values[kMaxLife], character.stats.values[kLife]
                                         + ((std::int64_t(blow.damage) * calc(*curse, curse->calc[0], target.curse.level) / 100) << 8));
        if (hurt(*game_data, target, blow.damage, now_ms)) { killed(monster_index, now_ms); return; }
        if (blow.knockback) {                                   // a step straight back, if there's room
            const float dx = target.unit.x - player.x, dy = target.unit.y - player.y, distance = std::max(std::hypot(dx, dy), 0.01f);
            const float next_x = target.unit.x + dx / distance * 0.6f, next_y = target.unit.y + dy / distance * 0.6f;
            if (!level->unit_blocked(next_x, next_y)) { target.unit.x = next_x; target.unit.y = next_y; }
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
        if (const auto* skill = game_data->skills.get(swing_skill); skill && missile_skill(*skill)) {   // Magic Arrow: the bow's action frame
            if (!pstruck && now_ms >= player.mode_ms + std::uint32_t(float(player_anim(pmode).action_ms()) / prate)) { pstruck = true; fire(*skill, now_ms); }
            return;
        }
        if (!seq.empty()) {
            const int due = seq_events(now_ms);
            while (seq_struck < due) {
                ++seq_struck;
                // Frenzy's and Double Swing's second hand looks for another
                // target (FUN_0056bd10 on the odd frame, FUN_005d8e00 /
                // FUN_005d8470).
                if (const auto* skill = game_data->skills.get(swing_skill); skill && seq_struck % 2 == 0 && (skill->srvdofunc == 9 || skill->srvdofunc == 70))
                    other_target();
                if (const auto* skill = game_data->skills.get(swing_skill); skill && moving_skill(*skill)) move_event(*skill, now_ms);
                else hit(now_ms);
            }
            return;
        }
        if (pstruck || attack_mon < 0
            || now_ms < player.mode_ms + std::uint32_t(float(player_anim(pmode).action_ms()) / prate)) return;
        pstruck = true;
        hit(now_ms);
    }

auto Fight::hit(std::uint32_t now_ms, float reach ) -> void {
        if (attack_mon < 0) return;
        auto& target_monster = monsters[std::size_t(attack_mon)];
        if (!target_monster.alive() || std::hypot(target_monster.unit.x - player.x, target_monster.unit.y - player.y) > reach) return;
        auto current_swing = swing();
        auto fighter = current_swing.kick ? pf_kick : player_combat;
        const auto* skill = game_data->skills.get(swing_skill);
        const bool charging = skill && skill->srvstfunc == 23, finishing = finisher(skill);
        std::erase_if(charges, [&](const Charge& charge) { return now_ms >= charge.until; });
        if (finishing) add_charges(fighter, current_swing);
        if (skill && skill->srvstfunc != 35 && std::ranges::contains(std::array{ 2, 13, 64, 7, 46, 9, 70, 76, 67, 78, 32, 11, 14, 121 }, skill->srvdofunc)) skill_element(fighter, *skill);
        // Hunger (do 122, FUN_005c7f10): calc1 damage %, calc2 / calc3 life /
        // mana stolen.
        if (skill && skill->srvdofunc == 122) {
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            current_swing.ed_pct += calc(*skill, skill->calc[0], lvl);
            fighter.life_steal += calc(*skill, skill->calc[1], lvl); fighter.mana_steal += calc(*skill, skill->calc[2], lvl);
        }
        const int hp_before = target_monster.hit_points;
        const auto target = std::size_t(attack_mon);
        const auto blow = d2d::rules::player_blow(fighter, target_of(target), int(character.stats.get(d2d::d2s::kLevel)), rng, current_swing);
        land(target, blow, true, now_ms);
        if (blow.hit && charging) charge(*skill, now_ms);
        if (blow.hit && finishing) release(target, now_ms);
        if (blow.hit && skill && skill->srvdofunc == 50) dragon_tail(*skill, target, blow.phys, now_ms);
        if (blow.hit && skill && (skill->srvdofunc == 9 || skill->srvdofunc == 120)) frenzy(*skill, now_ms);   // Feral Rage / Maul stack as Frenzy
        if (blow.hit && skill && skill->srvstfunc == 7) impale_wear(*skill);
        if (blow.hit && skill && (skill->srvdofunc == 11 || skill->srvdofunc == 14)) strike_bolts(*skill, target, now_ms);
        // Conversion (do 79): at calc1 % the monster turns for auralen frames.
        // ponytail: it doesn't fight for the player; it stops seeing the
        // player's side (as Confuse here).
        if (blow.hit && skill && skill->srvdofunc == 79 && monsters[target].alive()) {
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            if (int(rng(100)) < calc(*skill, skill->calc[0], lvl))
                monsters[target].cry = { skill->id, lvl, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, lvl), 25)) * 40 };
        }
        // Sacrifice's price (FUN_005ce8e0): calc2 % of the physical dealt,
        // no more than the target had left, off the player's life.
        if (blow.hit && skill && skill->srvdofunc == 64) {
            const int dealt = std::min(d2d::rules::resisted(blow.phys, target_of(target).res[0]), std::max(hp_before, 0));
            const auto env = calc_env();
            self_hurt += (std::int64_t(dealt) << 8) * d2d::rules::eval_calc(game_data->skills, skill->calc[1], env, skill->id,
                                                                            skill_level ? skill_level(skill->id) : 1) / 100;
        }
        if (!monsters[target].alive()) attack_mon = -1;
    }

auto Fight::strike_bolts(const d2d::rules::Skill& skill, std::size_t monster_index, std::uint32_t now_ms) -> void {
        const auto found = game_data->missiles.find(skill.srvmissilea);
        if (found == game_data->missiles.end()) return;
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const float x = monsters[monster_index].unit.x, y = monsters[monster_index].unit.y, dx = x - player.x, dy = y - player.y;
        if (skill.srvdofunc == 11) {
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
            if (const float distance = std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y); j != monster_index && monsters[j].alive() && distance * 5 <= float(reach) && distance < best_distance) {
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
            if (monsters[j].alive() && std::hypot(monsters[j].unit.x - corpse.unit.x, monsters[j].unit.y - corpse.unit.y) * 5 <= float(radius)) {
                const auto target = target_of(j);
                d2d::rules::Blow blow{ .hit = true };
                blow.damage = d2d::rules::resisted(dmg / 2, target.res[0]) + d2d::rules::resisted(dmg - dmg / 2, target.res[2]);
                land(j, blow, true, now_ms);
            }
    }

auto Fight::spot_skill(const d2d::rules::Skill& skill) -> bool {
        return std::ranges::contains(std::array{ 33, 51, 20, 55, 63, 27, 30, 6, 61, 59, 71, 69, 72, 77, 21, 52, 74 }, skill.srvdofunc);
    }

auto Fight::spot(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const auto damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
        auto within = [&](float x, float y, int radius, auto&& each) {
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (monsters[j].alive() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(radius)) each(j);
        };
        switch (skill.srvdofunc) {
            case 33:
                if (attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive()) {
                    const auto monster_index = std::size_t(attack_mon);
                    auto blow = d2d::rules::missile_blow(damage, target_of(monster_index), pierce(), rng);
                    blow.knockback = int(rng(100)) < calc(skill, skill.calc[0], lvl);
                    land(monster_index, blow, true, now_ms);
                }
                break;
            case 51:
                within(cast_x, cast_y, calc(skill, skill.aurarange, lvl), [&](std::size_t monster_index) { land(monster_index, d2d::rules::missile_blow(damage, target_of(monster_index), pierce(), rng), true, now_ms); });
                break;
            case 20: {
                const int pct = calc(skill, skill.calc[0], lvl);
                within(player.x, player.y, calc(skill, skill.aurarange, lvl), [&](std::size_t monster_index) {
                    d2d::rules::Blow blow{ .hit = true };
                    blow.damage = d2d::rules::resisted(monsters[monster_index].hit_points * pct / 100, target_of(monster_index).res[3]);
                    land(monster_index, blow, true, now_ms);
                });
                break;
            }
            case 55: case 63: {
                const int corpse_index = corpse_near(cast_x, cast_y);
                if (corpse_index < 0) break;
                auto& corpse = monsters[std::size_t(corpse_index)];
                corpse.corpse_used = true;
                if (skill.srvdofunc == 63) {
                    if (const auto found = game_data->missiles.find(skill.srvmissilea); found != game_data->missiles.end())
                        launch(found->second, skill, lvl, corpse.unit.x, corpse.unit.y, 0, 0, found->second.range, now_ms);
                    break;
                }
                corpse_blast(skill, lvl, std::size_t(corpse_index), now_ms);
                break;
            }
            case 27: case 77: case 52: {
                // Teleport; Leap (FUN_... do 77: the jump, then knockback
                // round the landing within aurarange); Dragon Flight (do 52:
                // next to the monster, then a kick).
                // ponytail: Leap and Dragon Flight arrive at once (no jump,
                // no flight); Leap's range (calc1) isn't checked.
                float target_x = cast_x, target_y = cast_y;
                const int tgt = attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive() ? attack_mon : -1;
                if (skill.srvdofunc == 52) {
                    if (tgt < 0) break;
                    const auto& target = monsters[std::size_t(tgt)];
                    const float dx = player.x - target.unit.x, dy = player.y - target.unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                    target_x = target.unit.x + dx / distance * 0.8f; target_y = target.unit.y + dy / distance * 0.8f;
                }
                if (level == &game_data->town || level->unit_blocked(target_x, target_y)) break;
                player.x = target_x; player.y = target_y;
                player.path.clear(); player.walking = false;
                player.goal_x = target_x; player.goal_y = target_y;
                if (skill.srvdofunc == 77)
                    within(target_x, target_y, calc(skill, skill.aurarange, lvl), [&](std::size_t monster_index) {
                        d2d::rules::Blow blow{ .hit = true };
                        blow.knockback = true;
                        land(monster_index, blow, true, now_ms);
                    });
                if (skill.srvdofunc == 52) {
                    d2d::rules::Swing swing;
                    swing.kick = true;
                    land(std::size_t(tgt), d2d::rules::player_blow(pf_kick, target_of(std::size_t(tgt)), int(character.stats.get(d2d::d2s::kLevel)), rng, swing),
                         true, now_ms);
                }
                break;
            }
            case 21:                                         // Telekinesis (do 21): its damage and a knockback on the monster
                if (attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive()) {
                    auto blow = d2d::rules::missile_blow(damage, target_of(std::size_t(attack_mon)), pierce(), rng);
                    blow.knockback = true;
                    land(std::size_t(attack_mon), blow, true, now_ms);
                }
                break;
            // Curses (do 30, FUN_005c37c0; 61 Confuse, FUN_005c3f20): the
            // auratargetstate on every monster within aurarange of the point
            // for auralen frames (FUN_0056e780), one curse at a time. Inner
            // Sight / Slow Missiles (do 6, FUN_005db1c0) round the caster;
            // Attract (59, FUN_005c3b90) and Taunt (71, FUN_005d8570) on the
            // monster.
            case 30: case 61: case 6: case 59: case 71: {
                const std::uint32_t until = now_ms + std::uint32_t(std::max(calc(skill, skill.auralen, lvl), 25)) * 40;
                const bool curse = skill.srvdofunc == 30 || skill.srvdofunc == 61 || skill.srvdofunc == 59;
                auto put = [&](std::size_t monster_index) { (curse ? monsters[monster_index].curse : monsters[monster_index].cry) = { skill.id, lvl, until }; };
                if (skill.srvdofunc == 59 || skill.srvdofunc == 71) {
                    if (attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive()) put(std::size_t(attack_mon));
                } else {
                    within(skill.srvdofunc == 6 ? player.x : cast_x, skill.srvdofunc == 6 ? player.y : cast_y, calc(skill, skill.aurarange, lvl), put);
                }
                break;
            }
            // Double Throw (do 74, FUN_005d88b0): each hand's throwing weapon
            // flies at the target as its missile (weapons.txt missiletype),
            // with calc1 damage % (stat 25).
            // ponytail: toht (stat 19) isn't added; no ammo is spent.
            case 74:
                for (const auto& item : character.items)
                    if (item.location == 1 && (item.slot == 4 || item.slot == 5))
                        if (const auto found_thrown = game_data->thrown.find(item.code); found_thrown != game_data->thrown.end())
                            if (const auto found = game_data->missiles.find(found_thrown->second); found != game_data->missiles.end())
                                launch(found->second, skill, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y,
                                       found->second.range + found->second.lev_range * lvl, now_ms).ed_pct = calc(skill, skill.calc[0], lvl);
                break;
            // Find Potion (do 69, FUN_005d81c0) / Find Item (72, FUN_005d8780):
            // a corpse not yet searched (state 0x76), at calc1 %: a potion
            // (FUN_005d8100), or the monster's treasure again (FUN_005a8000,
            // a tier by Param1..4).
            // ponytail: the potion by the character's level (FUN_005d8100's
            // table isn't traced); Find Item rolls the monster's own class.
            case 69: case 72: {
                const int corpse_index = corpse_near(cast_x, cast_y);
                if (corpse_index < 0) break;
                auto& corpse = monsters[std::size_t(corpse_index)];
                corpse.corpse_used = true;
                if (int(rng(100)) >= calc(skill, skill.calc[0], lvl)) break;
                if (skill.srvdofunc == 72) { loot.drop(corpse, now_ms); break; }
                const int tier = std::clamp(1 + int(character.stats.get(d2d::d2s::kLevel)) / 12, 1, 5);
                const std::string code = rng(20) == 0 ? "rvs" : (rng(2) ? "hp" : "mp") + std::to_string(tier);
                loot.put({ code, 2, 0 }, corpse.unit.x, corpse.unit.y, corpse.stats.level, now_ms);
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
        std::uint32_t until = pmode_until;
        if (skill.srvstfunc == 39) {
            const auto env = calc_env();
            const int ticks = d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl);
            until = now_ms + std::uint32_t(ticks > 0 ? ticks : 10) * 40;
        }
        std::erase_if(self_states, [&](const SelfState& state) { return state.skill == skill.id; });
        self_states.push_back({ skill.id, lvl, until });
    }

auto Fight::start_state(const d2d::rules::Skill& skill, int lvl, std::uint32_t now_ms) -> void {
        const int len = calc(skill, skill.auralen, lvl);
        const std::uint32_t until = len > 0 ? now_ms + std::uint32_t(len) * 40 : ~0u;
        std::erase_if(self_states, [&](const SelfState& state) { return state.skill == skill.id; });
        self_states.push_back({ skill.id, lvl, until });
        for (std::size_t k = 0; k < skill.aurastat.size(); ++k)
            if (skill.aurastat[k] == kBoneArmor) absorb_pool = calc(skill, skill.aura_calc[k], lvl) >> 8, absorb_skill = skill.id;
        if (skill.srvdofunc == 47)
            for (auto& monster : monsters)
                if (monster.alive() && std::hypot(monster.unit.x - player.x, monster.unit.y - player.y) * 5 <= float(calc(skill, skill.aurarange, lvl)))
                    monster.cry = { skill.id, lvl, until };
    }

auto Fight::state_of(std::string_view name) const -> const SelfState* {
        for (const auto& state : self_states)
            if (const auto* skill = game_data->skills.get(state.skill); skill && skill->aurastate == name) return &state;
        return nullptr;
    }

auto Fight::absorb(int damage) -> int {
        using namespace d2d::d2s;
        if (damage <= 0) return 0;
        if (absorb_pool > 0 && std::ranges::contains(self_states, absorb_skill, &SelfState::skill)) {
            const int absorbed = std::min(absorb_pool, damage);
            absorb_pool -= absorbed; damage -= absorbed;
            if (absorb_pool <= 0) std::erase_if(self_states, [&](const SelfState& state) { return state.skill == absorb_skill; });
        }
        if (const auto* energy_shield = state_of("energyshield"); energy_shield && damage > 0) {
            const auto* skill = game_data->skills.get(energy_shield->skill);
            const int part = damage * std::clamp(calc(*skill, skill->calc[0], energy_shield->level), 0, 95) / 100;
            const int per16 = std::max(calc(*skill, skill->calc[1], energy_shield->level), 1);
            const int can = int(std::min<std::int64_t>(part, (character.stats.values[kMana] >> 8) * 16 / per16));
            character.stats.values[kMana] -= (std::int64_t(can) * per16 / 16) << 8;
            damage -= can;
        }
        return damage;
    }

auto Fight::buff_events(Foe& foe, std::uint32_t now_ms) -> void {
        for (const auto* monster : foe.melee_by) {
            const auto monster_index = std::size_t(monster - monsters.data());
            if (monster_index >= monsters.size() || !monsters[monster_index].alive()) continue;
            if (const auto* frozen_armor = state_of("frozenarmor")) {
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
        if (const auto* chilling_armor = state_of("chillingarmor"); chilling_armor && foe.missile_hits > 0) {
            const auto* skill = game_data->skills.get(chilling_armor->skill);
            const auto found = game_data->missiles.find(skill->srvmissilea);
            int best = -1; float best_distance = 12.f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float distance = std::hypot(monsters[j].unit.x - player.x, monsters[j].unit.y - player.y); monsters[j].alive() && distance < best_distance) { best_distance = distance; best = int(j); }
            if (best >= 0 && found != game_data->missiles.end()) {
                const auto& nearest = monsters[std::size_t(best)];
                launch(found->second, *skill, chilling_armor->level, player.x, player.y, nearest.unit.x - player.x, nearest.unit.y - player.y, found->second.range, now_ms);
            }
        }
    }

auto Fight::buff_tick(std::uint32_t now_ms) -> void {
        if (const auto* blaze = state_of("blaze"); blaze && player.walking && now_ms / 40 != blaze_frame) {
            blaze_frame = now_ms / 40;
            const auto* skill = game_data->skills.get(blaze->skill);
            if (const auto found = game_data->missiles.find(skill->srvmissilea); found != game_data->missiles.end())
                launch(found->second, *skill, blaze->level, player.x, player.y, 0, 0, found->second.range + found->second.lev_range * blaze->level, now_ms);
        }
        // Armageddon / Hurricane (do 124, FUN_005c8190: the state and its
        // timer event, FUN_005417d0(5, ...)): Armageddon drops its control
        // row (hit 56) on a spot within aurarange every 5 frames; Hurricane
        // strikes what's within aurarange with its cold once a second. Blade
        // Shield (do 54, FUN_005d7e10 -> FUN_005d7ce0) strikes one monster
        // within aurarange every 10 frames.
        // ponytail: the timer's period (FUN_004efc80) and the events aren't
        // traced: the paces are by eye.
        if (const auto* armageddon = state_of("armageddon"); armageddon && (now_ms / 40) % 5 == 0 && now_ms / 40 != storm_frame) {
            const auto* skill = game_data->skills.get(armageddon->skill);
            if (const auto found = game_data->missiles.find(skill->srvmissilea); found != game_data->missiles.end()) {
                const int radius = std::max(calc(*skill, skill->aurarange, armageddon->level), 1);
                launch(found->second, *skill, armageddon->level, player.x + float(int(rng(2 * radius + 1)) - radius) / 5, player.y + float(int(rng(2 * radius + 1)) - radius) / 5,
                       0, 0, found->second.range, now_ms);
            }
        }
        for (const char* name : { "hurricane", "bladeshield" })
            if (const auto* state = state_of(name); state && now_ms / 40 != storm_frame && (now_ms / 40) % (name[0] == 'h' ? 25 : 10) == 0) {
                const auto* skill = game_data->skills.get(state->skill);
                const auto damage = d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), state->level);
                const int radius = std::max(calc(*skill, skill->aurarange, state->level), 1);
                for (std::size_t j = 0; j < monsters.size(); ++j)
                    if (monsters[j].alive() && std::hypot(monsters[j].unit.x - player.x, monsters[j].unit.y - player.y) * 5 <= float(radius)) {
                        land(j, d2d::rules::missile_blow(damage, target_of(j), pierce(), rng), true, now_ms);
                        if (name[0] == 'b') break;
                    }
            }
        storm_frame = now_ms / 40;
        if (const auto* thunderstorm = state_of("thunderstorm"); thunderstorm && now_ms >= storm_next) {
            storm_next = now_ms + 1600;
            const auto* skill = game_data->skills.get(thunderstorm->skill);
            int best = -1; float best_distance = 5.f;
            for (std::size_t j = 0; j < monsters.size(); ++j)
                if (const float distance = std::hypot(monsters[j].unit.x - player.x, monsters[j].unit.y - player.y); monsters[j].alive() && distance < best_distance) { best_distance = distance; best = int(j); }
            if (best >= 0)
                land(std::size_t(best), d2d::rules::missile_blow(d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), thunderstorm->level),
                                                                 target_of(std::size_t(best)), pierce(), rng), true, now_ms);
        }
    }

auto Fight::monster_states(std::uint32_t now_ms) -> void {
        for (auto& monster : monsters) {
            monster.dmg_pct = monster.speed_pct = monster.reflect_pct = 0;
            for (auto* effect : { &monster.curse, &monster.cry }) {
                if (effect->skill < 0) continue;
                if (now_ms >= effect->until) { *effect = {}; continue; }
                const auto* skill = game_data->skills.get(effect->skill);
                if (!skill) continue;
                for (std::size_t aura_stat = 0; aura_stat < skill->aurastat.size(); ++aura_stat) {
                    if (skill->aurastat[aura_stat] == 25) monster.dmg_pct += calc(*skill, skill->aura_calc[aura_stat], effect->level);
                    if (skill->aurastat[aura_stat] == 67) monster.speed_pct += calc(*skill, skill->aura_calc[aura_stat], effect->level);
                }
                const auto& targets = skill->auratarget;
                if (targets == "ironmaiden") monster.reflect_pct += calc(*skill, skill->calc[0], effect->level);
                if (targets == "terror") monster.flee_until = std::max(monster.flee_until, effect->until);
                if (targets == "dimvision" || targets == "cloaked" || targets == "confuse" || targets == "attract" || targets == "conversion") monster.blind_until = effect->until;
            }
        }
    }

auto Fight::charge(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        auto found = std::ranges::find(charges, skill.id, &Charge::skill);
        if (found == charges.end()) found = charges.insert(charges.end(), Charge{ skill.id });
        found->level = std::max(found->level, lvl);
        found->count = std::min(found->count + 1, 3);
        const auto env = calc_env();
        found->until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data->skills, skill.auralen, env, skill.id, lvl), 1)) * 40;
    }

auto Fight::add_charges(d2d::rules::Fighter& fighter, d2d::rules::Swing& swing) -> void {
        const auto env = calc_env();
        for (const auto& charge : charges) {
            const auto* skill = game_data->skills.get(charge.skill);
            if (!skill) continue;
            const int lvl = std::max(charge.level, skill_level ? skill_level(charge.skill) : 0);
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

auto Fight::row_damage(const GameData::MissileInfo& missile_info, int lvl) -> d2d::rules::MissileDamage {
        auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        damage.srcdam = missile_info.src_damage;
        return damage;
    }

auto Fight::release(std::size_t monster_index, std::uint32_t now_ms) -> void {
        const float target_x = monsters[monster_index].unit.x, target_y = monsters[monster_index].unit.y;
        const auto held = charges;
        charges.clear();
        for (const auto& charge : held) {
            const auto* skill = game_data->skills.get(charge.skill);
            if (!skill) continue;
            const int lvl = std::max(charge.level, skill_level ? skill_level(skill->id) : 0);
            const int count = std::clamp(charge.count, 1, 3);
            for (int k = skill->prgstack ? 1 : count; k <= count; ++k) prg(*skill, skill->prgfunc[std::size_t(k - 1)], k, lvl, target_x, target_y, now_ms);
        }
    }

auto Fight::prg(const d2d::rules::Skill& skill, int func, int count, int lvl, float target_x, float target_y, std::uint32_t now_ms) -> void {
        if (func <= 0) return;
        const auto env = calc_env();
        const auto& mname = count <= 1 ? skill.srvmissilea : count == 2 ? skill.srvmissileb : skill.srvmissilec;
        const auto mit = game_data->missiles.find(mname);
        const GameData::MissileInfo* missile_info = mit == game_data->missiles.end() ? nullptr : &mit->second;
        int radius = d2d::rules::eval_calc(game_data->skills, skill.prgcalc[std::size_t(count - 1)], env, skill.id, lvl);
        if (radius == 0) radius = d2d::rules::eval_calc(game_data->skills, skill.prgcalc[0], env, skill.id, lvl);
        if (func == 38) {
            d2d::rules::MissileDamage damage = d2d::rules::missile_damage(game_data->skills, skill, env, lvl);
            damage.srcdam = 0;
            const std::array<int, 4> pierce{ int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) };
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].alive() && std::hypot(monsters[i].unit.x - target_x, monsters[i].unit.y - target_y) * 5 <= float(std::max(radius, 1)))
                    land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms);
            return;
        }
        if ((func == 36 || func == 39) && missile_info) {
            auto put = [&](float x, float y, float velocity_x, float velocity_y, int range) {
                Missile missile{ missile_info, x, y, velocity_x, velocity_y, direction32(velocity_x, velocity_y), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
                missile.friendly = true; missile.skill = skill.id; missile.level = lvl;
                missiles.push_back(missile);
                return missiles.size() - 1;
            };
            if (func == 36) {
                const auto shared = std::make_shared<std::vector<int>>();
                const float speed = cells_per_sec(float(missile_info->vel));
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
        if (func == 40) { launch(*missile_info, skill, lvl, target_x, target_y, 0, 0, missile_info->range, now_ms); return; }
        if (func == 41) {
            for (int k = 0; k < std::max(radius, 1); ++k) {
                const float dx = float(int(rng(41)) - 20) / 5, dy = float(int(rng(41)) - 20) / 5;
                launch(*missile_info, skill, lvl, target_x, target_y, dx, dy, missile_info->range + missile_info->lev_range * lvl, now_ms);
            }
            return;
        }
        if (func == 37 || func == 143) {
            const int reach = radius > 0 ? radius : d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (monsters[i].alive() && std::hypot(monsters[i].unit.x - target_x, monsters[i].unit.y - target_y) * 5 <= float(std::max(reach, 1)))
                    launch(*missile_info, skill, lvl, target_x, target_y, monsters[i].unit.x - target_x + 0.01f, monsters[i].unit.y - target_y, missile_info->range + missile_info->lev_range * lvl, now_ms).hops = lvl + 1;
            return;
        }
        if (std::ranges::find(told, -1000 - func) != told.end()) return;
        told.push_back(-1000 - func);
        d2d::log::info("not implemented: {}'s release srvprgfunc {} ({} charges)", skill.name, func, count);
    }

auto Fight::dragon_tail(const d2d::rules::Skill& skill, std::size_t target, int phys, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const int fire = phys * d2d::rules::eval_calc(game_data->skills, skill.calc[0], env, skill.id, lvl) / 100;
        const float radius = float(d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl));
        const float center_x = monsters[target].unit.x, center_y = monsters[target].unit.y;
        for (std::size_t i = 0; i < monsters.size() && fire > 0; ++i) {
            auto& monster = monsters[i];
            if (!monster.alive() || std::hypot(monster.unit.x - center_x, monster.unit.y - center_y) > radius) continue;
            if (hurt(*game_data, monster, d2d::rules::resisted(fire, target_of(i).res[2]), now_ms)) killed(i, now_ms);
        }
    }

auto Fight::boss_events(std::span<Foe> foes, std::uint32_t now_ms) -> void {
        using d2d::rules::Boss;
        for (auto& monster : monsters) {
            const bool boss = monster.boss == Boss::unique || monster.boss == Boss::superunique;
            const auto has = [&](int id) { return boss && std::ranges::contains(monster.mods, id); };
            if (monster.alive()) {
                if (has(d2d::rules::umod::lightning) && monster.hit_points < monster.last_hp && now_ms - monster.bolts_at >= 10 * 40) {
                    monster.bolts_at = now_ms;
                    d2d::log::info("{} lets off charged bolts", monster.npc.name);
                    const auto ring = std::make_shared<std::vector<int>>();
                    for (const auto& [dir_x, dir_y] : { std::pair{ 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } })
                        for (int k = 0; k < 2; ++k) {
                            const float angle = float(rng(51) - 25) * 3.14159265f / 180, cosine = std::cos(angle), sine = std::sin(angle);
                            boss_missile(monster, "lightunique", dir_x * cosine - dir_y * sine, dir_x * sine + dir_y * cosine, ring, now_ms);
                        }
                }
            } else if (boss && !monster.fx_done) {
                if (!monster.fx_at) monster.fx_at = now_ms + 4 * 40;
                else if (now_ms >= monster.fx_at) {
                    monster.fx_done = true;
                    if (has(d2d::rules::umod::fire)) fire_blast(monster, foes, now_ms);
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

auto Fight::boss_missile(const Monster& monster, const char* name, float dx, float dy, const std::shared_ptr<std::vector<int>>& ring, std::uint32_t now_ms) -> void {
        const auto found = game_data->missiles.find(name);
        if (found == game_data->missiles.end()) return;
        const auto& missile_info = found->second;
        const int lvl = std::max(monster.stats.level / 2, 1);
        const auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        d2d::rules::MonStats stats;
        stats.level = monster.stats.level;
        stats.to_hit = 1 << 20;                                     // ToHit 0: always hits
        if (damage.etype >= 0) stats.elements[0] = { damage.etype, 100, damage.elo >> 8, std::max(damage.ehi >> 8, 1), damage.elen, "A2" };
        const float speed = cells_per_sec(float(missile_info.vel)), distance = std::max(std::hypot(dx, dy), 0.01f);
        Missile x{ &missile_info, monster.unit.x, monster.unit.y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, stats };
        x.struck = ring;
        pending.push_back(std::move(x));
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

auto Fight::killed(std::size_t monster_index, std::uint32_t now_ms) -> void {
        const auto& monster = monsters[monster_index];
        const auto save_class = std::size_t(std::max(character.character_class, 0));
        auto exp = d2d::rules::kill_exp(monster.stats.exp, int(character.stats.get(d2d::d2s::kLevel)), monster.stats.level);
        exp += exp * int(psum[85]) / 100;                   // item_addexperience (the experience shrine)
        const int levels_gained = d2d::rules::gain_exp(character.stats, exp, game_data->exp_next, game_data->class_gains[save_class]);
        d2d::log::info("killed {} (+{} exp){}", monster.npc.name, exp, levels_gained ? std::format(", level {}", character.stats.get(d2d::d2s::kLevel)) : "");
        if (levels_gained) character.panel = panel_stats(*game_data, character.header, character.items, character.stats);
        fallen_scatter(*game_data, monsters, monster_index, rng, now_ms);
        loot.drop(monster, now_ms);
    }

auto Fight::merc_fighter() const -> d2d::rules::Fighter {
        return d2d::rules::simple_fighter(merc_st.dmg_min, merc_st.dmg_max, merc_st.attack_rating, merc_st.def);
    }

auto Fight::merc_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        auto& unit = *merc;
        const auto set = [&](std::string_view mode) {
            merc_mode = mode; unit.mode_ms = now_ms; unit.walking = mode == "WL";
            unit.path.clear();
            merc_until = mode == "NU" || mode == "WL" ? 0 : now_ms + game_data->npc_timing(*merc_npc, mode).length_ms();
        };
        if (merc_mode == "DT") {
            if (now_ms >= merc_until) { merc.reset(); character.header.merc_dead = true; d2d::log::info("the merc died"); }
            return;
        }
        if (merc_mode == "GH") { if (now_ms < merc_until) return; set("NU"); }
        const bool archer = merc_npc && merc_npc->id == "roguehire";
        const float reach = archer ? 6.f : kMeleeReach;
        if (merc_mode == "A1") {
            if (!merc_struck && merc_target >= 0 && now_ms >= unit.mode_ms + game_data->npc_timing(*merc_npc, "A1").action_ms()) {
                merc_struck = true;
                auto& target = monsters[std::size_t(merc_target)];
                const float dx = target.unit.x - unit.x, dy = target.unit.y - unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                if (archer && game_data->missiles.contains("arrow")) {
                    const auto& missile_info = game_data->missiles.at("arrow");
                    const float speed = cells_per_sec(float(missile_info.vel));
                    Missile missile{ &missile_info, unit.x, unit.y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms, now_ms + std::uint32_t(missile_info.range) * 40, {} };
                    missile.min = merc_st.dmg_min; missile.max = merc_st.dmg_max; missile.attack_rating = merc_st.attack_rating; missile.level = merc_st.level; missile.friendly = true;
                    missiles.push_back(missile);
                } else if (target.alive() && distance <= kMeleeReach + 0.3f) {
                    land(std::size_t(merc_target), d2d::rules::player_blow(merc_fighter(), target_of(std::size_t(merc_target)), merc_st.level, rng), false, now_ms);
                }
            }
            if (now_ms < merc_until) return;
            set("NU");
        }
        // Pick a target.
        if (merc_target >= 0 && !monsters[std::size_t(merc_target)].alive()) merc_target = -1;
        if (merc_target < 0) {
            float best = 1e9f;
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                const auto& monster = monsters[i];
                if (!monster.alive()) continue;
                const float to_player = std::hypot(monster.unit.x - player.x, monster.unit.y - player.y), to_merc = std::hypot(monster.unit.x - unit.x, monster.unit.y - unit.y);
                if (((monster.aware && to_player < 6) || to_merc < 3) && to_merc < best) { best = to_merc; merc_target = int(i); }
            }
        }
        const auto save_class = std::size_t(std::max(character.character_class, 0));
        const float speed = cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f;
        if (merc_target >= 0 && std::hypot(unit.x - player.x, unit.y - player.y) < 10) {
            const auto& target = monsters[std::size_t(merc_target)];
            const float dx = target.unit.x - unit.x, dy = target.unit.y - unit.y, distance = std::hypot(dx, dy);
            if (distance <= reach) {
                unit.dir = direction16(dx, dy);
                set("A1");
                merc_struck = false;
                return;
            }
            if (merc_mode != "WL") set("WL");
            if (unit.path.empty() || std::hypot(unit.goal_x - target.unit.x, unit.goal_y - target.unit.y) > 1.f) {
                unit.path = walk_path(*level, unit.x, unit.y, target.unit.x, target.unit.y, crowd, &unit);
                unit.goal_x = target.unit.x; unit.goal_y = target.unit.y;
            }
            if (!follow_path(*level, unit, speed * elapsed, crowd)) merc_target = -1;
            return;
        }
        merc_target = -1;
        merc_follow(*level, unit, player.x, player.y, speed, now_ms, elapsed, crowd);
        merc_mode = unit.walking ? "WL" : "NU";
    }

auto Fight::player_modes(bool held, std::uint32_t now_ms, float elapsed) -> bool {
        if (dead()) {
            if (pmode == kModeDT && now_ms >= pmode_until) set_pmode(kModeDD, now_ms);
            return pmode == kModeDD;
        }
        if (attack_mode(pmode)) {
            strike(now_ms);
            if (smove.active) skill_step(now_ms, elapsed);
            if (now_ms >= pmode_until) {
                if (kicks_left > 0 && next_target()) {
                    --kicks_left;                            // Talon's next kick (FUN_005d5a30), Zeal's next hit (FUN_005dbc60)
                    set_pmode(swing_mode(), now_ms);
                    pstruck = false;
                    return false;
                }
                kicks_left = 0;
                seq = {};
                smove = {};
                seq_loop = false;
                pmode = -1; player.mode_ms = now_ms;
                if (!held) attack_mon = -1;
            }
        } else if (pmode == kModeSC) {                       // a self cast: its state on the action frame
            if (!pstruck && now_ms >= player.mode_ms + player_anim(kModeSC).action_ms()) {
                pstruck = true;
                if (const auto* skill = game_data->skills.get(swing_skill); skill && skill->srvdofunc == 113) {
                    portal_due = true;                         // Town Portal (FUN_005bf3d0): the World opens it
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
            if (now_ms >= pmode_until) { pmode = -1; player.mode_ms = now_ms; if (!held) attack_mon = -1; }
        } else if ((pmode == kModeGH || pmode == kModeBL) && now_ms >= pmode_until) {
            pmode = -1; player.mode_ms = now_ms;
        }
        return false;
    }

auto Fight::skill_step(std::uint32_t now_ms, float elapsed) -> void {
        const auto* skill = game_data->skills.get(swing_skill);
        if (skill && skill->srvdofunc == 67 && attack_mon >= 0) {
            smove.target_x = monsters[std::size_t(attack_mon)].unit.x; smove.target_y = monsters[std::size_t(attack_mon)].unit.y;
        }
        const float dx = smove.target_x - player.x, dy = smove.target_y - player.y, distance = std::hypot(dx, dy);
        const bool whirl = skill && skill->srvdofunc == 76;
        if (distance < 0.05f || (!whirl && !smove.fly && distance <= kMeleeReach)) { if (whirl) pmode_until = now_ms; return; }
        player.dir = direction16(dx, dy);
        const float step = std::min(smove.speed * elapsed, distance), next_x = player.x + dx / distance * step, next_y = player.y + dy / distance * step;
        if (!smove.fly && level->unit_blocked(next_x, next_y)) { if (whirl) pmode_until = now_ms; return; }
        player.x = next_x; player.y = next_y;
    }

auto Fight::move_event(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        if (skill.srvdofunc == 76) {
            if (now_ms < whirl_next) return;
            const auto& attack_anim = player_anim(kModeA1);
            const int ticks = d2d::rules::attack_ticks(int(attack_anim.frames ? attack_anim.frames : 16), int(attack_anim.speed ? attack_anim.speed : 256), player_combat.ias, player_combat.wsm);
            whirl_next = now_ms + std::uint32_t(d2d::rules::whirlwind_gap(ticks)) * 40;
            const int hands = int(std::ranges::count_if(character.items, [&](const d2d::d2s::Item& item) {
                return item.location == 1 && (item.slot == 4 || item.slot == 5) && game_data->rules.item_info.contains(item.code)
                    && game_data->rules.item_info.at(item.code).kind == 2; }));
            for (int hand = 0; hand < (hands >= 2 ? 2 : 1); ++hand) {
                std::vector<int> nearby;
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].unit.x - player.x, monsters[i].unit.y - player.y) <= 5.f) nearby.push_back(int(i));
                if (nearby.empty()) return;
                const auto next = std::ranges::upper_bound(nearby, attack_mon);
                attack_mon = next != nearby.end() ? *next : nearby.front();
                hit(now_ms, 5.f);
            }
            return;
        }
        if (skill.srvdofunc == 67) {
            const auto frame_index = seq_at(now_ms);
            const auto run_end = std::size_t(std::ranges::find_if(seq, [](const auto& frame) { return frame.mode != kModeRN; }) - seq.begin());
            if (frame_index < run_end) {                               // running: in reach, on to the attack frames
                if (attack_mon >= 0 && std::hypot(monsters[std::size_t(attack_mon)].unit.x - player.x,
                                                  monsters[std::size_t(attack_mon)].unit.y - player.y) <= kMeleeReach + 0.3f) {
                    seq_loop = false;
                    smove.active = false;
                    player.mode_ms = now_ms - std::uint32_t(run_end) * seq_frame_ms;
                    seq_struck = seq_events(now_ms);
                    pmode_until = now_ms + std::uint32_t(seq.size() - run_end) * seq_frame_ms;
                }
                return;
            }
            hit(now_ms);
            return;
        }
        // Leap Attack: the first event takes off, the second lands, the third strikes.
        if (seq_struck == 1 && attack_mon >= 0) {
            const auto& target = monsters[std::size_t(attack_mon)].unit;
            const float dx = target.x - player.x, dy = target.y - player.y, distance = std::max(std::hypot(dx, dy), 0.01f);
            const float land = std::max(distance - kMeleeReach * 0.8f, 0.f);
            std::uint32_t air = seq_frame_ms;
            for (std::size_t k = seq_at(now_ms) + 1; k < seq.size() && seq[k].event != 1; ++k) air += seq_frame_ms;
            smove = { player.x + dx / distance * land, player.y + dy / distance * land, land / (float(air) / 1000.f), land > 0.f, true };
            player.dir = direction16(dx, dy);
        } else if (seq_struck == 2) {
            if (smove.active) { player.x = smove.target_x; player.y = smove.target_y; }
            smove = {};
        } else if (seq_struck >= 3) {
            hit(now_ms);
        }
    }

auto Fight::other_target() -> void {
        const auto reach = in_reach();
        if (reach.empty()) { attack_mon = -1; return; }
        const auto next = std::ranges::upper_bound(reach, attack_mon);
        attack_mon = next != reach.end() ? *next : reach.front();
        player.dir = direction16(monsters[std::size_t(attack_mon)].unit.x - player.x, monsters[std::size_t(attack_mon)].unit.y - player.y);
    }

auto Fight::impale_wear(const d2d::rules::Skill& skill) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        if (int(rng(100)) >= d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl)) return;
        for (auto& item : character.items)
            if (item.location == 1 && (item.slot == 4 || item.slot == 5) && game_data->rules.item_info.contains(item.code)
                && game_data->rules.item_info.at(item.code).kind == 2) {
                const auto found = game_data->rules.item_base.find(item.code);
                if (found != game_data->rules.item_base.end() && found->second.stackable) item.quantity = std::max(int(item.quantity) - 1, 0);
                else if (item.max_durability > 0 && !d2d::rules::indestructible(item))
                    item.durability = std::max(int(item.durability) - d2d::rules::eval_calc(game_data->skills, skill.calc[2], env, skill.id, lvl), 0);
                return;
            }
    }

auto Fight::skill_missile(const d2d::rules::Skill& skill, bool any_owner ) const -> const GameData::MissileInfo* {
        static constexpr int kDo[] = { 8, 17, 22, 10, 12, 26, 28, 24, 19, 73, 80, 117, 118, 123, 43, 48, 68, 75, 44, 125, 95 };
        static constexpr int kSt[] = { 0, 4, 8, 11, 26, 33 };
        const bool plain = (skill.srvstfunc == 0 || skill.srvstfunc == 4) && skill.srvdofunc == 0;
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
        if (!skill_row || dead() || pmode >= 0 || (!missile_skill(*skill_row) && !spot_skill(*skill_row))) return false;
        const int lvl = skill_level ? skill_level(skill) : 0;
        const int cost = d2d::rules::mana_cost(*skill_row, lvl);
        if (lvl <= 0 || character.stats.values[kMana] < cost) { attack_mon = -1; return false; }
        character.stats.values[kMana] -= cost;
        swing_skill = skill;
        cast_x = target_x; cast_y = target_y;
        player.dir = direction16(target_x - player.x, target_y - player.y);
        set_pmode(skill_row->anim == "A1" || skill_row->anim == "TH" ? kModeA1 : kModeSC, now_ms);
        pstruck = false;
        return true;
    }

auto Fight::launch(const GameData::MissileInfo& missile_info, const d2d::rules::Skill& skill, int lvl, float x, float y, float dx, float dy,
                    int range, std::uint32_t now_ms) -> Missile& {
        const float speed = cells_per_sec(float(missile_info.vel)), distance = std::max(std::hypot(dx, dy), 0.01f);
        Missile missile{ &missile_info, x, y, dx / distance * speed, dy / distance * speed, direction32(dx, dy), now_ms, now_ms + std::uint32_t(std::max(range, 1)) * 40, {} };
        missile.friendly = true; missile.skill = skill.id; missile.level = lvl;
        pending.push_back(missile);
        return pending.back();
    }

auto Fight::calc(const d2d::rules::Skill& skill, const d2d::rules::Calc& calc_row, int lvl) -> int {
        return d2d::rules::eval_calc(game_data->skills, calc_row, calc_env(), skill.id, lvl);
    }

auto Fight::fire(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const auto& missile_info = *skill_missile(skill);
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const int calc1 = calc(skill, skill.calc[0], lvl);
        const auto shared = std::make_shared<std::vector<int>>();
        const float dx = cast_x - player.x, dy = cast_y - player.y;
        const int range = missile_info.range + missile_info.lev_range * lvl;
        const int target = attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive() ? attack_mon : -1;
        auto send = [&](float ddx, float ddy, int reach) -> Missile& {
            auto& missile = launch(missile_info, skill, lvl, player.x, player.y, ddx, ddy, reach, now_ms);
            missile.target_x = cast_x; missile.target_y = cast_y;                    // where it was sent (Molten Boulder's roll)
            return missile;
        };
        if (skill.srvdofunc == 8) {
            const int count = std::max(calc1, 1);
            float side_x = dy * 5, side_y = -dx * 5;                 // subtiles, turned a right angle
            while (side_x * side_x + side_y * side_y > 3) { side_x = std::trunc(side_x / 2); side_y = std::trunc(side_y / 2); }
            if (side_x == 0 && side_y == 0) side_x = 1;
            for (int k = 0; k < count; ++k) {
                const float spread = float(k) - float(count) / 2;
                send(dx + spread * side_x / 5, dy + spread * side_y / 5, range);
            }
        } else if (skill.srvdofunc == 17) {
            for (int k = 0; k < std::max(calc1, 1); ++k) {
                const float angle = (float(rng(81)) - 40) * 3.14159265f / 180;
                send(dx * std::cos(angle) - dy * std::sin(angle), dx * std::sin(angle) + dy * std::cos(angle), range);
            }
        } else if (skill.srvdofunc == 22 || skill.srvdofunc == 68) {
            // A war cry (do 68, FUN_005d83e0): the nova of its missile (range
            // its row's), then its state on the caster (FUN_005d8290).
            for (int k = 0; k < 64; ++k) {
                const float angle = float(k) * 2 * 3.14159265f / 64;
                send(std::cos(angle), std::sin(angle), skill.srvdofunc == 68 ? missile_info.range : missile_info.range + calc1).struck = shared;
            }
            if (skill.srvdofunc == 68 && !skill.aurastate.empty()) start_state(skill, lvl, now_ms);
        } else if (skill.srvdofunc == 10) {
            auto& missile = send(dx, dy, range);
            missile.target = target; missile.ed_pct = calc1;
        } else if (skill.srvdofunc == 12) {
            const int radius = calc(skill, skill.aurarange, lvl);
            std::vector<int> nearby;
            if (target >= 0) nearby.push_back(target);
            for (std::size_t i = 0; i < monsters.size(); ++i)
                if (int(i) != target && monsters[i].alive()
                    && std::hypot(monsters[i].unit.x - player.x, monsters[i].unit.y - player.y) * 5 <= float(radius)) nearby.push_back(int(i));
            const int high = calc1, low = std::min(calc(skill, skill.calc[2], lvl), high);
            const int count = std::clamp(int(nearby.size()), low, high);
            for (int k = 0; k < count; ++k)
                strafe.push_back({ nearby.empty() ? -1 : nearby[std::size_t(k) % nearby.size()], now_ms + std::uint32_t(k) * 120 });
        } else if (skill.srvdofunc == 26) {
            send(dx, dy, range).hops = calc1;
        } else if (skill.srvdofunc == 28) {
            launch(missile_info, skill, lvl, cast_x, cast_y, 0, 0, range, now_ms);
        } else if (skill.srvdofunc == 24) {
            const float around_x = target >= 0 ? monsters[std::size_t(target)].unit.x : cast_x;
            const float around_y = target >= 0 ? monsters[std::size_t(target)].unit.y : cast_y;
            const float perp_x = around_y - player.y, perp_y = player.x - around_x;
            launch(missile_info, skill, lvl, around_x, around_y, perp_x, perp_y, range, now_ms);
            launch(missile_info, skill, lvl, around_x, around_y, -perp_x, -perp_y, range, now_ms);
            if (const auto found = game_data->missiles.find(skill.srvmissileb); found != game_data->missiles.end())
                launch(found->second, skill, lvl, around_x, around_y, 0, 0, found->second.range + found->second.lev_range * lvl, now_ms);
        } else if (skill.srvdofunc == 19 || skill.srvdofunc == 48) {
            channel = skill.id;
        } else if (skill.srvdofunc == 73) {                      // Blessed Hammer (FUN_005d0040): path 14, a spiral out
            auto& missile = send(dx, dy, range);
            missile.target_x = player.x; missile.target_y = player.y;
            missile.turn = int(std::atan2(dy, dx) * 1000);
        } else if (skill.srvdofunc == 80) {                      // Fist of the Heavens (FUN_005d0670): on the target
            if (target < 0) return;
            const auto& target_monster = monsters[std::size_t(target)];
            launch(missile_info, skill, lvl, target_monster.unit.x, target_monster.unit.y, 0, 0, range, now_ms).target = target;
        } else if (skill.srvdofunc == 117 || skill.srvdofunc == 118) {   // Firestorm (FUN_005c7160), Twister / Tornado (FUN_005c72f0)
            for (int k = 0; k < std::max(calc1, 1); ++k) {
                const float angle = k == 0 ? 0.f : (float(rng(41)) - 20) * 3.14159265f / 180;
                send(dx * std::cos(angle) - dy * std::sin(angle), dx * std::sin(angle) + dy * std::cos(angle), range);
            }
        } else if (skill.srvdofunc == 44) {                      // Blade Sentinel: out to the point and back, Param1 frames
            // ponytail: do 44 (FUN_005d6020) moves a trap monster between
            // the caster and the point; here its missile row does, hitting
            // as it passes (NextHit).
            auto& missile = send(dx, dy, std::max(range, skill.par[0]));
            missile.origin_x = player.x; missile.origin_y = player.y;
            const float distance = std::max(std::hypot(dx, dy), 0.01f), speed = cells_per_sec(10.f);   // bladecreeper's Velocity
            missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed;
        } else if (skill.srvdofunc == 75) {                      // Grim Ward: its start row at the point
            launch(missile_info, skill, lvl, cast_x, cast_y, 0, 0, missile_info.range, now_ms);
        } else if (skill.srvdofunc == 123) {                     // Volcano (FUN_005c8080): at the target point
            launch(missile_info, skill, lvl, cast_x, cast_y, 0, 0, range, now_ms);
        } else if (skill.srvdofunc == 43) {                      // Shock Web (FUN_005d5d70 -> FUN_005d5bf0): scattered round the target
            const int count = std::max(calc(skill, skill.prgcalc[0], lvl), 1), radius = std::max(calc(skill, skill.aurarange, lvl), 1);
            for (int k = 0; k < count; ++k) {
                const float target_x = cast_x + float(int(rng(2 * radius + 1)) - radius) / 5, target_y = cast_y + float(int(rng(2 * radius + 1)) - radius) / 5;
                send(target_x - player.x, target_y - player.y, land_range(missile_info, target_x - player.x, target_y - player.y));
            }
        } else {
            send(dx, dy, missile_info.hit_func == 36 ? land_range(missile_info, dx, dy) : range);
        }
    }

auto Fight::land_range(const GameData::MissileInfo& missile_info, float dx, float dy) -> int {
        const float per_frame = cells_per_sec(float(std::max(missile_info.vel, 1))) * 0.04f;
        return std::max(int(std::hypot(dx, dy) / per_frame), 1);
    }

auto Fight::missile_tick(std::uint32_t now_ms) -> void {
        const int frame = int(now_ms / 40);
        if (frame == last_frame) return;
        last_frame = frame;
        burned.clear();
        std::erase_if(strafe, [&](const std::pair<int, std::uint32_t>& strafed) {
            if (now_ms < strafed.second) return false;
            const auto* skill = game_data->skills.get(swing_skill);
            if (!skill || skill->srvdofunc != 12) return true;
            const auto& missile_info = *skill_missile(*skill);
            const int lvl = skill_level ? skill_level(skill->id) : 1;
            float target_x = cast_x, target_y = cast_y;
            if (strafed.first >= 0 && monsters[std::size_t(strafed.first)].alive()) { target_x = monsters[std::size_t(strafed.first)].unit.x; target_y = monsters[std::size_t(strafed.first)].unit.y; }
            launch(missile_info, *skill, lvl, player.x, player.y, target_x - player.x, target_y - player.y, missile_info.range + missile_info.lev_range * lvl, now_ms).ed_pct
                = calc(*skill, skill->calc[1], lvl);
            return true;
        });
        if (channel >= 0) {
            const auto* skill = game_data->skills.get(channel);
            if (!skill || pmode != kModeSC || swing_skill != channel) channel = -1;
            else {
                const int lvl = skill_level ? skill_level(skill->id) : 1;
                const auto& missile_info = *skill_missile(*skill);
                if (skill->srvdofunc == 19)
                    launch(missile_info, *skill, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y, std::max(calc(*skill, skill->calc[0], lvl), 1), now_ms);
                else if (frame % 3 == 0)                     // Blade Fury (FUN_005d68a0): one per attack frame
                    launch(missile_info, *skill, lvl, player.x, player.y, cast_x - player.x, cast_y - player.y, missile_info.range, now_ms);
            }
        }
        for (std::size_t k = 0; k < missiles.size(); ++k) {
            auto& missile = missiles[k];
            if (missile.skill < 0 || !missile.info) continue;
            const auto* skill = game_data->skills.get(missile.skill);
            if (!skill) continue;
            const int age = int(now_ms - missile.born) / 40;
            const auto& missile_info = *missile.info;
            if (missile_info.srv_do == 7 && age % std::max(missile_info.param1, 1) == 0) {
                if (missile.target < 0 && skill->srvstfunc == 0) {
                    float best = 1e9f;
                    for (std::size_t i = 0; i < monsters.size(); ++i)
                        if (const float distance = std::hypot(monsters[i].unit.x - cast_x, monsters[i].unit.y - cast_y); monsters[i].alive() && distance < best) {
                            best = distance; missile.target = int(i);
                        }
                }
                if (missile.target >= 0 && monsters[std::size_t(missile.target)].alive()) {
                    const auto& target = monsters[std::size_t(missile.target)];
                    const float speed = std::hypot(missile.velocity_x, missile.velocity_y), dx = target.unit.x - missile.x, dy = target.unit.y - missile.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                    missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed; missile.dir = direction32(dx, dy);
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
                const auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = 0, .elo = missile.fixed, .ehi = missile.fixed }
                                             : d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), missile.level);
                for (std::size_t i = 0; i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].unit.x - missile.x, monsters[i].unit.y - missile.y) <= 0.5f
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
            // ponytail: missile do 14 isn't traced; Param1 / aurarange read so.
            if (missile_info.srv_do == 14 && age % std::max(missile_info.param1, 1) == 0)
                for (auto& monster : monsters)
                    if (monster.alive() && std::hypot(monster.unit.x - missile.x, monster.unit.y - missile.y) * 5 <= float(std::max(calc(*skill, skill->aurarange, missile.level), 1)))
                        monster.flee_until = std::max(monster.flee_until, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, missile.level), 1)) * 40);
            // 20 (Blade Sentinel): back and forth between the caster's spot and
            // where it was sent.
            if (missile_info.srv_do == 20 && std::hypot(missile.target_x - missile.x, missile.target_y - missile.y) < 0.3f) {
                std::swap(missile.target_x, missile.origin_x); std::swap(missile.target_y, missile.origin_y);
                const float speed = std::hypot(missile.velocity_x, missile.velocity_y), dx = missile.target_x - missile.x, dy = missile.target_y - missile.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                missile.velocity_x = dx / distance * speed; missile.velocity_y = dy / distance * speed; missile.dir = direction32(dx, dy);
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
            // ponytail: its Param3 / Param4 frame window isn't applied.
            if (missile_info.srv_do == 28 && sub != game_data->missiles.end()
                && age % std::max(missile_info.param1 > 0 ? missile_info.param1 : calc(*skill, skill->calc[3], missile.level), 1) == 0) {
                const int radius = std::max(missile_info.param2 > 0 ? missile_info.param2 : calc(*skill, skill->aurarange, missile.level), 1);
                const float offset_x = float(int(rng(2 * radius + 1)) - radius) / 5, offset_y = float(int(rng(2 * radius + 1)) - radius) / 5;
                launch(sub->second, *skill, missile.level, missile.x, missile.y, offset_x, offset_y, land_range(sub->second, offset_x, offset_y), now_ms);
            }
            // Blessed Hammer (do 73's path 14): round its caster's spot and out.
            // ponytail: the client path's shape isn't traced: a turn each 1.6
            // s, out at a fifth of its speed.
            if (skill->srvdofunc == 73 && missile_info.srv_do <= 1) {
                const float seconds = float(now_ms - missile.born) / 1000, radius = seconds * cells_per_sec(float(missile_info.vel)) * 0.2f;
                const float ang = float(missile.turn) / 1000 + seconds * 2 * 3.14159265f / 1.6f;
                missile.x = missile.target_x + radius * std::cos(ang); missile.y = missile.target_y + radius * std::sin(ang);
                missile.velocity_x = missile.velocity_y = 0;
            }
        }
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

auto Fight::pierce() const -> std::array<int, 4> { return { int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) }; }

auto Fight::skill_missile_hits(Missile& missile, std::size_t monster_index, std::uint32_t now_ms) -> bool {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill) return true;
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
        // ponytail: Howl's level sum (FUN_004efc20) read as calc1 + level.
        if (missile_info.hit_func == 18) return false;
        if (missile_info.hit_func == 21) {
            if (monsters[monster_index].cry.skill != skill->id || now_ms >= monsters[monster_index].cry.until)
                monsters[monster_index].cry = { skill->id, missile.level, now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, missile.level), 1)) * 40 };
            return false;
        }
        if (missile_info.hit_func == 17) {
            const int clvl = int(character.stats.get(d2d::d2s::kLevel));
            if (monsters[monster_index].stats.level < clvl + calc(*skill, skill->calc[0], missile.level) + missile.level)
                monsters[monster_index].flee_until = std::max(monsters[monster_index].flee_until, now_ms + std::uint32_t(std::max(skill->par[2] + (missile.level - 1) * skill->par[3], 25)) * 40);
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
                    j != monster_index && monsters[j].alive() && distance * 5 <= float(radius) && distance < best_distance) { best_distance = distance; best = int(j); }
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
        return missile_info.collide_kill && !(missile_info.pierce && int(rng(100)) < int(psum[328]));
    }

auto Fight::strike(const Missile& missile, std::size_t monster_index, std::uint32_t now_ms, int freeze ) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || !monsters[monster_index].alive()) return;
        const auto target = target_of(monster_index);
        const int clvl = int(character.stats.get(d2d::d2s::kLevel));
        auto damage = missile.fixed >= 0 ? d2d::rules::MissileDamage{ .etype = missile.info->etype < 0 ? 0 : missile.info->etype, .elo = missile.fixed, .ehi = missile.fixed }
                : missile.info->skill.empty() ? row_damage(*missile.info, missile.level)
                                        : d2d::rules::missile_damage(game_data->skills, *skill, calc_env(), missile.level);
        d2d::rules::Blow blow{ .hit = true };
        if (damage.srcdam > 0) {
            d2d::rules::Swing swing;
            swing.srcdam = damage.srcdam;
            swing.ed_pct = missile.ed_pct;
            blow = d2d::rules::player_blow(player_combat, target, clvl, rng, swing);
        } else if (missile.info->to_hit) {
            blow.hit = int(rng(100)) < d2d::rules::hit_chance(player_combat.attack_rating, target.armor_class, clvl, target.level);
        }
        if (blow.hit) blow = d2d::rules::missile_blow(damage, target, pierce(), rng, blow);
        if (blow.hit && freeze > 0) { blow.chill_ticks = std::max(blow.chill_ticks, freeze); blow.stun_ticks = std::max(blow.stun_ticks, freeze); }
        land(monster_index, blow, true, now_ms);
    }

auto Fight::area(const Missile& missile, float x, float y, int radius, std::uint32_t now_ms, int freeze ) -> void {
        for (std::size_t j = 0; j < monsters.size(); ++j)
            if (monsters[j].alive() && std::hypot(monsters[j].unit.x - x, monsters[j].unit.y - y) * 5 <= float(std::max(radius, 1))) strike(missile, j, now_ms, freeze);
    }

auto Fight::burst(Missile& missile, std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(missile.skill);
        if (!skill || missile.frame >= 0) return;
        missile.frame = int(now_ms / 40);
        const auto& missile_info = *missile.info;
        const auto hit_sub = missile_info.hit_sub.empty() ? game_data->missiles.end() : game_data->missiles.find(missile_info.hit_sub);
        const bool sub = hit_sub != game_data->missiles.end();
        auto sub_at = [&](float x, float y, float dx, float dy) -> Missile& {
            return launch(hit_sub->second, *skill, missile.level, x, y, dx, dy, hit_sub->second.range + hit_sub->second.lev_range * missile.level, now_ms);
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
                const int burst_radius = radius(missile_info.hit_par1, skill->calc[0]);
                area(missile, missile.x, missile.y, burst_radius, now_ms);
                if (sub)
                    for (int offset_x = -burst_radius; offset_x <= burst_radius; ++offset_x)
                        for (int offset_y = -burst_radius; offset_y <= burst_radius; ++offset_y)
                            if (offset_x * offset_x + offset_y * offset_y <= burst_radius * burst_radius) sub_at(missile.x + float(offset_x) / 5, missile.y + float(offset_y) / 5, 0, 0).fixed = hit_sub->second.emin;
                break;
            }
            case 14: case 47: case 56: {
                area(missile, missile.x, missile.y, radius(missile_info.hit_par1, skill->aurarange), now_ms);
                const int burn256 = skill->par[2] + (missile.level - 1) * skill->par[3];   // Meteor's (FUN_004cc7c0)
                if (sub)
                    for (std::size_t j = 0; j < 18; j += std::size_t(std::max(missile_info.hit_par2, 1)))
                        sub_at(missile.x + float(kSpreadX[j]) / 5, missile.y + float(kSpreadY[j]) / 5, 0, 0).fixed
                            = missile_info.hit_func == 14 && burn256 > 0 ? burn256 : hit_sub->second.emin > 0 ? hit_sub->second.emin : -1;
                break;
            }
            case 20: {
                if (!sub) break;
                const int burst_radius = radius(missile_info.hit_par1, skill->aurarange);
                int count = radius(missile_info.hit_par2, skill->calc[0]);
                for (std::size_t j = 0; j < monsters.size() && count > 0; ++j)
                    if (int(j) != missile.target && monsters[j].alive() && std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y) * 5 <= float(burst_radius)) {
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
                        if (monsters[j].alive() && game_data->monsters.types[std::size_t(monsters[j].type)].undead
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
        const auto* skill = game_data->skills.get(aura);
        if (!skill || !skill->aura || now_ms < aura_next || dead()) return;
        const int lvl = skill_level ? skill_level(aura) : 1;
        if (lvl < 1) return;
        const auto env = calc_env();
        aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        if (skill->srvdofunc == 65) {
            for (std::size_t i = 0; i < skill->aurastat.size(); ++i)
                if (skill->aurastat[i] == 6) {
                    using namespace d2d::d2s;
                    const auto heal = d2d::rules::eval_calc(game_data->skills, skill->aura_calc[i], env, skill->id, lvl);
                    character.stats.values[kLife] = std::min(character.stats.values[kMaxLife], character.stats.values[kLife] + heal);
                }
            return;
        }
        // Redemption (do 82): a corpse within aurarange redeemed at calc1 %,
        // calc2 life and mana back.
        // ponytail: its callback isn't traced; one corpse a pulse.
        if (skill->srvdofunc == 82) {
            using namespace d2d::d2s;
            const int radius = calc(*skill, skill->aurarange, lvl);
            for (auto& monster : monsters)
                if (!monster.alive() && !monster.corpse_used && monster.mode == "DD" && std::hypot(monster.unit.x - player.x, monster.unit.y - player.y) * 5 <= float(radius)) {
                    if (int(rng(100)) < calc(*skill, skill->calc[0], lvl)) {
                        monster.corpse_used = true;
                        const std::int64_t gain = std::int64_t(calc(*skill, skill->calc[1], lvl)) << 8;
                        character.stats.values[kLife] = std::min(character.stats.values[kMaxLife], character.stats.values[kLife] + gain);
                        character.stats.values[kMana] = std::min(character.stats.values[kMaxMana], character.stats.values[kMana] + gain);
                    }
                    break;
                }
            return;
        }
        if ((skill->srvdofunc != 66 && skill->srvdofunc != 81) || skill->etype < 0 || skill->etype > 4) return;
        const auto damage = d2d::rules::MissileDamage{ .etype = skill->etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, *skill, env, lvl, false, 0, true),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, *skill, env, lvl, true, 0, true),
                                                   .elen = d2d::rules::elem_length(game_data->skills, *skill, env, lvl) };
        const std::array<int, 4> pierce{ int(psum[333]), int(psum[334]), int(psum[335]), int(psum[336]) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && in_aura(monsters[i]))
                land(i, d2d::rules::missile_blow(damage, target_of(i), pierce, rng), true, now_ms);
    }

auto Fight::in_aura(const Monster& monster) -> bool {
        const auto* skill = game_data->skills.get(aura);
        if (!skill || !skill->aura) return false;
        const int radius = d2d::rules::eval_calc(game_data->skills, skill->aurarange, calc_env(), skill->id, skill_level ? skill_level(aura) : 1);
        return std::hypot(monster.unit.x - player.x, monster.unit.y - player.y) * 5 <= float(radius);
    }

auto Fight::target_of(std::size_t monster_index) -> d2d::rules::Target {
        auto target = monsters[monster_index].target(*game_data);
        // Its curse and cry (Amplify Damage, Lower Resist, Decrepify, Battle
        // Cry, Inner Sight, Cloak of Shadows): their aurastats as the aura's.
        for (const auto* effect : { &monsters[monster_index].curse, &monsters[monster_index].cry })
            if (const auto* skill = effect->skill >= 0 ? game_data->skills.get(effect->skill) : nullptr)
                apply_target_stats(*skill, effect->level, target);
        const auto* skill = game_data->skills.get(aura);
        if (!skill || (skill->srvdofunc != 66 && skill->srvdofunc != 81) || !in_aura(monsters[monster_index])) return target;
        apply_target_stats(*skill, skill_level ? skill_level(aura) : 1, target);
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
        if (skill.srvdofunc == 58) return true;                  // Revive: the corpse's own monster
        return !skill.summon.empty() && game_data->monsters.row(skill.summon) >= 0
            && (skill.srvdofunc == 56 || skill.srvdofunc == 57 || skill.srvdofunc == 31 || skill.srvdofunc == 16 || skill.srvdofunc == 114
                || skill.srvdofunc == 144 || skill.srvdofunc == 15 || skill.srvdofunc == 49 || skill.srvdofunc == 60 || skill.srvdofunc == 62
                || skill.srvdofunc == 115
                || (skill.srvdofunc == 119 && (skill.pettype == "spiritwolf" || skill.pettype == "fenris" || skill.pettype == "grizzly" || skill.pettype == "totem"))
                || (skill.srvdofunc == 45 && trap_shot(skill) >= 0));
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
        if (!skill_row || dead() || pmode >= 0 || !summon_skill(*skill_row)) return false;
        if (skill_row->target_corpse && corpse_near(target_x, target_y) < 0) return false;
        const int lvl = skill_level ? skill_level(skill) : 0;
        const int cost = d2d::rules::mana_cost(*skill_row, lvl);
        if (lvl <= 0 || character.stats.values[kMana] < cost) return false;
        character.stats.values[kMana] -= cost;
        swing_skill = skill;
        cast_x = target_x; cast_y = target_y;
        attack_mon = -1;
        player.dir = direction16(target_x - player.x, target_y - player.y);
        set_pmode(kModeSC, now_ms);
        pstruck = false;
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
        if (skill.srvdofunc == 149 && !name.empty() && name.back() == '1') name.back() = char('1' + std::clamp(variant, 0, 3));
        const auto found = game_data->missiles.find(name);
        return name.empty() || found == game_data->missiles.end() ? nullptr : &found->second;
    }

auto Fight::summon(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        int type = game_data->monsters.row(skill.summon);
        const auto env = calc_env();
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        float x = cast_x, y = cast_y;
        if (skill.target_corpse) {
            const int corpse = corpse_near(cast_x, cast_y);
            if (corpse < 0) return;
            monsters[std::size_t(corpse)].corpse_used = true;
            x = monsters[std::size_t(corpse)].unit.x; y = monsters[std::size_t(corpse)].unit.y;
            if (skill.srvdofunc == 58) type = monsters[std::size_t(corpse)].type;
        }
        if (type < 0) return;
        // Bone Wall (do 60, FUN_005c58b0): one on the point, then its makers
        // (srvmissilea, missile do 13) lay calc2 / 2 more each way across
        // the caster's line. Bone Prison (62, FUN_005c5d00): twelve round the
        // point at 0x6e304c / 0x6e307c.
        // ponytail: the makers are laid at once, a subtile apart; calc2's
        // text is 'par34' (Param3 taken).
        if (skill.srvdofunc == 60) {
            const float perp_x = y - player.y, perp_y = player.x - x, distance = std::max(std::hypot(perp_x, perp_y), 0.01f);
            const int count = std::max(skill.par[2], 2) / 2;
            for (int k = -count; k <= count; ++k) summon_one(skill, type, lvl, env, x + perp_x / distance * float(k) * 0.4f, y + perp_y / distance * float(k) * 0.4f, now_ms);
            return;
        }
        if (skill.srvdofunc == 62) {
            static constexpr int kRingX[12] = { -1, 1, 3, 4, 4, 3, -1, 1, -3, -4, -4, -3 };
            static constexpr int kRingY[12] = { -4, -4, -3, -1, 1, 3, 4, 4, 3, -1, 1, -3 };
            for (std::size_t k = 0; k < 12; ++k) summon_one(skill, type, lvl, env, x + float(kRingX[k]) / 5, y + float(kRingY[k]) / 5, now_ms);
            return;
        }
        // Hydra (do 144, FUN_005ca910): three at once.
        for (int k = skill.srvdofunc == 144 ? 3 : 1; k > 0; --k) summon_one(skill, type, lvl, env, x + float(k - 1) * 0.6f, y, now_ms);
    }

auto Fight::summon_one(const d2d::rules::Skill& skill, int type, int lvl, const d2d::rules::CalcEnv& env, float x, float y, std::uint32_t now_ms) -> void {
        const int max = std::max(d2d::rules::eval_calc(game_data->skills, skill.petmax, env, skill.id, lvl), 1);
        auto same = [&](const Pet& pet) { return game_data->skills.get(pet.skill) && game_data->skills.get(pet.skill)->pettype == skill.pettype; };
        while (skill.pettype != "none" && std::ranges::count_if(pets, same) >= max) pets.erase(std::ranges::find_if(pets, same));
        const int clvl = int(character.stats.get(d2d::d2s::kLevel));
        const int bonus = skill.srvdofunc == 114 || skill.srvdofunc == 119 ? d2d::rules::eval_calc(game_data->skills, skill.calc[1], env, skill.id, lvl) : 0;
        const int plvl = std::max(std::min(clvl, clvl * 3 / 4 + bonus), 1);
        Pet pet;
        auto& monster = pet.monster;
        monster.type = type;
        monster.npc = game_data->mon_npc[std::size_t(type)];
        const auto& type_info = game_data->monsters.types[std::size_t(type)];
        for (std::size_t layer = 0; layer < 16; ++layer)
            if (!type_info.parts[layer].empty()) monster.npc.comp[layer] = type_info.parts[layer].front();
        monster.difficulty = std::clamp(character.header.active_difficulty(), 0, 2);
        monster.stats = d2d::rules::monster_stats(game_data->monsters, type, monster.difficulty, rng);   // spawned at its own level (FUN_005b2f20)
        // Its life and damage straight from MonStats' columns (a Raise
        // Skeleton skeleton: 21 life, 1-2 damage in Normal; 30 / 42 in
        // Nightmare / Hell), not scaled by MonLvl.
        // ponytail: FUN_005b2f20's pet branch isn't traced; the published
        // pet values are the raw columns.
        // Revive (do 58, FUN_005c56c0) raises the monster as it was: its own
        // stats at its level, life and level brought down to the owner's
        // when it's above it.
        if (skill.srvdofunc != 58) {
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
        monster.stats.level = skill.srvdofunc == 58 ? std::min(monster.stats.level, clvl) : skill.srvdofunc == 49 || skill.srvdofunc == 15 ? clvl : plvl;
        if (skill.srvdofunc != 58 && !game_data->monsters.lvl.empty()) {
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
        monster.stats.hit_points += stat(7) >> 8;                                               // maxhp, 256ths
        monster.stats.hit_points += int(std::int64_t(monster.stats.hit_points) * calc(skill, skill.calc[0], lvl) / 100);
        monster.hit_points = monster.stats.hit_points = std::max(monster.stats.hit_points, 1);
        for (int* damage : { &monster.stats.a1_min, &monster.stats.a1_max }) { *damage += stat(111); *damage += *damage * stat(25) / 100; }   // item_normaldamage, damagepercent
        monster.stats.to_hit += stat(19); monster.stats.to_hit += monster.stats.to_hit * stat(119) / 100;              // tohit, item_tohit_percent
        monster.stats.armor_class += stat(31); monster.stats.armor_class += monster.stats.armor_class * (stat(16) + stat(171)) / 100;   // armorclass, item / skill_armor_percent
        const auto& base_res = type_info.diff[std::size_t(monster.difficulty)].res;
        pet.res = { base_res[2] + stat(39), base_res[3] + stat(41), base_res[4] + stat(43), base_res[5] + stat(45) };
        pet.thorns = stat(131); pet.fire_lo = stat(48); pet.fire_hi = std::max(stat(49), pet.fire_lo); pet.speed_pct = stat(67);
        for (std::size_t k = 0; k < 5; ++k) {
            const auto* k_s = skill_named(skill.sumskill[k]);
            const int skill_level_value = k_s ? calc(skill, skill.sumsk_calc[k], lvl) : 0;
            if (!k_s || skill_level_value < 1) continue;
            if (k_s->aura) { pet.aura = k_s->id; pet.aura_level = skill_level_value; }
            else if (pet_missile(*k_s) && skill.srvdofunc != 115 && skill.srvdofunc != 45) { pet.ranged = k_s->id; pet.ranged_level = skill_level_value; }
        }
        monster.unit.x = monster.home_x = x; monster.unit.y = monster.home_y = y;
        monster.unit.dir = player.dir;
        pet.skill = skill.id;
        pet.where = level;
        pet.variant = int(rng(4));
        switch (skill.srvdofunc) {
            case 58: pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40; break;   // Revive: calc2 frames
            case 144:                                                                                   // Hydra: Param1 frames, it stands
                pet.until = now_ms + std::uint32_t(std::max(skill.par[0], 1)) * 40;
                if (const auto* hydra = skill_named("HydraMissile")) { pet.ranged = hydra->id; pet.ranged_level = lvl; }
                break;
            case 15:                                  // Decoy (FUN_005dc000): owner's life x calc3 %, calc2 frames, it stands
                monster.hit_points = monster.stats.hit_points = std::max(int(character.stats.fixed(d2d::d2s::kMaxLife) * calc(skill, skill.calc[2], lvl) / 100), 1);
                pet.until = now_ms + std::uint32_t(std::max(calc(skill, skill.calc[1], lvl), 1)) * 40;
                pet.idle = true;
                break;
            case 49: pet.mirror = true; break;          // Shadow Warrior / Master (FUN_005d6e70): the owner's level and gear
            case 114: pet.hits = std::max(skill.par[4], 1); break;   // Raven: its attacks
            case 60: case 62: pet.idle = true; pet.until = now_ms + 24000; break;   // bone: it stands (24 s: ponytail, the published time)
            case 115: monster.stats.level = std::max(calc(skill, skill.calc[1], lvl), 1); break;   // Vines (FUN_005c6a80): calc2 is its level
            default: break;
        }
        if (skill.srvdofunc == 45) {                             // a trap (FUN_005d6170 -> FUN_005d5e10)
            pet.shot_skill = trap_shot(skill);
            for (std::size_t k = 0; k < 5; ++k)
                if (game_data->skills.by_name.contains(skill.sumskill[k]) && game_data->skills.by_name.at(skill.sumskill[k]) == pet.shot_skill)
                    pet.shot_level = std::max(d2d::rules::eval_calc(game_data->skills, skill.sumsk_calc[k], env, skill.id, lvl), 1);
            const int calc4 = d2d::rules::eval_calc(game_data->skills, skill.calc[3], env, skill.id, lvl);
            pet.shots = calc4 > 0 ? calc4 : std::max(skill.par[0], 1);
        }
        set_mode(*game_data, monster, "NU", now_ms);
        pets.push_back(std::move(pet));
    }

auto Fight::trap_turn(Pet& pet, std::uint32_t now_ms) -> void {
        auto& monster = pet.monster;
        if (now_ms < monster.next_act) return;
        const auto& per_difficulty = game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(monster.difficulty)];
        monster.next_act = now_ms + std::uint32_t(std::max(per_difficulty.aidel, 1)) * 40;
        const float range = float(per_difficulty.aip[3] > 0 ? per_difficulty.aip[3] : 25) / 5;
        int best = -1; float best_distance = range;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (const float distance = std::hypot(monsters[i].unit.x - monster.unit.x, monsters[i].unit.y - monster.unit.y); monsters[i].alive() && distance <= best_distance) { best_distance = distance; best = int(i); }
        // Death Sentry's other skill, 'mon death sentry' (do 55): a corpse in
        // its reach goes up.
        if (const auto* pet_skill = game_data->skills.get(pet.skill))
            for (std::size_t summon_index = 0; summon_index < 5; ++summon_index)
                if (const auto* bone_skill = skill_named(pet_skill->sumskill[summon_index]); bone_skill && bone_skill->srvdofunc == 55) {
                    const int corpse = corpse_near(monster.unit.x, monster.unit.y);
                    if (corpse >= 0) { corpse_blast(*bone_skill, pet.shot_level, std::size_t(corpse), now_ms); break; }
                }
        if (best < 0) return;
        const auto* skill = game_data->skills.get(pet.shot_skill);
        if (!skill) return;
        const auto& missile_info = *skill_missile(*skill, true);
        const auto owner = game_data->skills.by_name.find(missile_info.skill);
        const float dx = monsters[std::size_t(best)].unit.x - monster.unit.x, dy = monsters[std::size_t(best)].unit.y - monster.unit.y, dist = std::max(std::hypot(dx, dy), 0.01f);
        const float speed = cells_per_sec(float(missile_info.vel));
        // Inferno Sentry's (do 95, FUN_005cc4e0) is a stream of flames:
        // here eight at once, their reach staggered.
        const int count = skill->srvdofunc == 17 ? std::max(d2d::rules::eval_calc(game_data->skills, skill->calc[0], calc_env(), skill->id, pet.shot_level), 1)
                    : skill->srvdofunc == 95 ? 8 : 1;
        for (int j = 0; j < count; ++j) {
            const float angle = count > 1 ? (float(rng(81)) - 40) * 3.14159265f / 180 : 0.f;
            const float aim_x = dx * std::cos(angle) - dy * std::sin(angle), aim_y = dx * std::sin(angle) + dy * std::cos(angle);
            Missile x{ &missile_info, monster.unit.x, monster.unit.y, aim_x / dist * speed, aim_y / dist * speed, direction32(aim_x, aim_y), now_ms,
                       now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, {} };
            x.friendly = true; x.level = pet.shot_level;
            x.skill = owner != game_data->skills.by_name.end() ? owner->second : skill->id;
            if (skill->srvdofunc == 95) { x.velocity_x = dx / dist * speed; x.velocity_y = dy / dist * speed; x.dies = now_ms + std::uint32_t(4 + 3 * j) * 40; }
            missiles.push_back(x);
        }
        if (--pet.shots <= 0) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); }
    }

auto Fight::pet_foe(const Pet& pet) const -> Foe {
        auto fighter = d2d::rules::simple_fighter(pet.monster.stats.a1_min, pet.monster.stats.a1_max, pet.monster.stats.to_hit, pet.monster.stats.armor_class);
        for (std::size_t k = 0; k < 4; ++k) fighter.res[k] = std::min(pet.res[k], 95);
        fighter.thorns_pct = pet.thorns;                             // Iron Golem's thorns
        const auto& type_info = game_data->monsters.types[std::size_t(pet.monster.type)];
        const bool still = type_info.velocity == 0 && type_info.run == 0 && pet.ranged >= 0;   // a Hydra, like a trap, isn't there to hit
        return Foe{ pet.monster.unit.x, pet.monster.unit.y, pet.monster.stats.level, pet.monster.alive() && pet.monster.mode != "DT" && pet.shot_skill < 0 && !still && pet.where == level,
                    pet.monster.unit.walking, fighter };
    }

auto Fight::pet_hurt(Pet& pet, int damage, std::uint32_t now_ms) -> void {
        if (damage > 0 && pet.monster.mode != "DT") hurt(*game_data, pet.monster, damage, now_ms);
    }

auto Fight::pets_cross(const Level* from, const Level* destination, float dx, float dy) -> void {
        for (auto& pet : pets)
            if (pet.where == from && pet.shot_skill < 0 && pet.monster.alive()) {
                pet.where = destination; pet.target = -1;
                pet.monster.unit.x -= dx; pet.monster.unit.y -= dy; pet.monster.unit.goal_x -= dx; pet.monster.unit.goal_y -= dy;
                for (auto& [step_x, step_y] : pet.monster.unit.path) { step_x -= dx; step_y -= dy; }
                pet.monster.home_x -= dx; pet.monster.home_y -= dy;
            }
    }

auto Fight::pet_aura(Pet& pet, std::uint32_t now_ms) -> void {
        const auto* skill = game_data->skills.get(pet.aura);
        if (!skill || skill->srvdofunc != 66 || now_ms < pet.aura_next || skill->etype < 0 || skill->etype > 4) return;
        pet.aura_next = now_ms + std::uint32_t(std::max(skill->perdelay, 25)) * 40;
        const int radius = calc(*skill, skill->aurarange, pet.aura_level);
        const auto damage = d2d::rules::MissileDamage{ .etype = skill->etype,
                                                   .elo = d2d::rules::elem_damage(game_data->skills, *skill, calc_env(), pet.aura_level, false),
                                                   .ehi = d2d::rules::elem_damage(game_data->skills, *skill, calc_env(), pet.aura_level, true) };
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && std::hypot(monsters[i].unit.x - pet.monster.unit.x, monsters[i].unit.y - pet.monster.unit.y) * 5 <= float(radius))
                land(i, d2d::rules::missile_blow(damage, target_of(i), {}, rng), false, now_ms);
    }

auto Fight::pets_turn(std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        std::erase_if(pets, [&](const Pet& pet) { return !pet.monster.alive() && pet.monster.mode == "DT" && now_ms >= pet.monster.mode_until; });
        const auto save_class = std::size_t(std::max(character.character_class, 0));
        for (auto& pet : pets) {
            auto& monster = pet.monster;
            auto& unit = monster.unit;
            if (!monster.alive() || pet.where != level) continue;
            if (pet.until && now_ms >= pet.until) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); continue; }
            if (pet.shot_skill >= 0) { trap_turn(pet, now_ms); continue; }
            if (pet.aura >= 0) pet_aura(pet, now_ms);
            if (pet.idle) continue;                            // Decoy stands where it was cast
            if (const auto* skill = game_data->skills.get(pet.skill); skill && skill->srvdofunc == 115 && now_ms >= pet.aura_next) {
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
                        if (monsters[j].alive() && std::hypot(monsters[j].unit.x - unit.x, monsters[j].unit.y - unit.y) < 1.5f)
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
                    auto& value = character.stats.values[life ? kLife : kMana];
                    value = std::min(character.stats.values[life ? kMaxLife : kMaxMana], value + character.stats.values[life ? kMaxLife : kMaxMana] * skill->par[4] / 100);
                }
            }
            if (monster.mode == "A1") {
                if (!monster.struck && pet.target >= 0 && now_ms >= unit.mode_ms + game_data->npc_timing(monster.npc, "A1").action_ms()) {
                    monster.struck = true;
                    const auto monster_index = std::size_t(pet.target);
                    if (const auto* missile_info = pet.ranged >= 0 ? pet_missile(*game_data->skills.get(pet.ranged), pet.variant) : nullptr; missile_info && monsters[monster_index].alive()) {
                        Missile x{ missile_info, unit.x, unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(missile_info->range, 1)) * 40, {} };
                        const float dx = monsters[monster_index].unit.x - unit.x, dy = monsters[monster_index].unit.y - unit.y, distance = std::max(std::hypot(dx, dy), 0.01f);
                        x.velocity_x = dx / distance * cells_per_sec(float(missile_info->vel)); x.velocity_y = dy / distance * cells_per_sec(float(missile_info->vel)); x.dir = direction32(dx, dy);
                        x.friendly = true; x.level = pet.ranged_level; x.skill = pet.ranged;
                        if (const auto* missile_skill_row = skill_named(missile_info->skill)) x.skill = missile_skill_row->id;   // 'hydra' carries the Hydra skill's damage
                        pending.push_back(x);
                    } else if (monsters[monster_index].alive() && std::hypot(monsters[monster_index].unit.x - unit.x, monsters[monster_index].unit.y - unit.y) <= kMeleeReach + 0.3f) {
                        auto target = target_of(monster_index);
                        // A Shadow Warrior swings the owner's blow (its copied gear and level).
                        // ponytail: its own skills (Fists of Fire, Blade Fury, ...) and
                        // FUN_005d6cf0's skill copy aren't built.
                        auto blow = d2d::rules::player_blow(pet.mirror ? player_combat : d2d::rules::simple_fighter(monster.stats.a1_min, monster.stats.a1_max, monster.stats.to_hit),
                                                         target, monster.stats.level, rng);
                        if (pet.hits > 0 && --pet.hits == 0) { monster.hit_points = 0; set_mode(*game_data, monster, "DT", now_ms); }   // a Raven's last
                        if (blow.hit && pet.fire_hi > 0) blow.damage += d2d::rules::resisted(rng.range(pet.fire_lo, pet.fire_hi), target.res[2]);   // Fire Golem
                        land(monster_index, blow, false, now_ms);
                    }
                }
                if (now_ms < monster.mode_until) continue;
                set_mode(*game_data, monster, "NU", now_ms);
            }
            if (monster.mode == "GH" && now_ms < monster.mode_until) continue;
            if (pet.target >= 0 && !monsters[std::size_t(pet.target)].alive()) pet.target = -1;
            if (pet.target < 0) {
                float best = 1e9f;
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    const auto& other = monsters[i];
                    if (!other.alive()) continue;
                    const float to_player = std::hypot(other.unit.x - player.x, other.unit.y - player.y), to_pet = std::hypot(other.unit.x - unit.x, other.unit.y - unit.y);
                    if (((other.aware && to_player < 8) || to_pet < 4) && to_pet < best) { best = to_pet; pet.target = int(i); }
                }
            }
            const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
            const float speed = cells_per_sec(float(std::max(type_info.run, type_info.velocity))) * float(100 + pet.speed_pct) / 100;
            if (speed <= 0) {                                // a Hydra: it stands and shoots what comes in range
                if (pet.target >= 0 && std::hypot(monsters[std::size_t(pet.target)].unit.x - unit.x, monsters[std::size_t(pet.target)].unit.y - unit.y) > 6.f) pet.target = -1;
                for (std::size_t i = 0; pet.target < 0 && i < monsters.size(); ++i)
                    if (monsters[i].alive() && std::hypot(monsters[i].unit.x - unit.x, monsters[i].unit.y - unit.y) <= 6.f) pet.target = int(i);
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
                if (unit.path.empty() || std::hypot(unit.goal_x - target.unit.x, unit.goal_y - target.unit.y) > 1.f) {
                    unit.path = walk_path(*level, unit.x, unit.y, target.unit.x, target.unit.y, crowd, &unit);
                    unit.goal_x = target.unit.x; unit.goal_y = target.unit.y;
                }
                if (!follow_path(*level, unit, speed * elapsed, crowd)) pet.target = -1;
            } else {
                merc_follow(*level, unit, player.x, player.y, cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f, now_ms, elapsed, crowd);
            }
            const std::string_view want = unit.walking ? "WL" : "NU";
            if (monster.mode != want) set_mode(*game_data, monster, want, now_ms);
        }
    }

auto Fight::cast_scroll(int skill, std::uint32_t now_ms) -> bool {
        if (dead() || pmode >= 0) return false;
        swing_skill = skill;
        attack_mon = -1;
        set_pmode(kModeSC, now_ms);
        pstruck = false;
        return true;
    }

auto Fight::cast(int skill, std::uint32_t now_ms) -> bool {
        using namespace d2d::d2s;
        const auto* skill_row = game_data->skills.get(skill);
        if (!skill_row || dead() || pmode >= 0 || !self_cast(*skill_row)) return false;
        const int lvl = skill_level ? skill_level(skill) : 0;
        const int cost = d2d::rules::mana_cost(*skill_row, lvl);
        if (lvl <= 0 || character.stats.values[kMana] < cost) return false;
        character.stats.values[kMana] -= cost;
        swing_skill = skill;
        attack_mon = -1;
        set_pmode(kModeSC, now_ms);
        pstruck = false;
        return true;
    }

auto Fight::frenzy(const d2d::rules::Skill& skill, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const auto found = std::ranges::find(self_states, skill.id, &SelfState::skill);
        const int frenzy_charges = std::min(found == self_states.end() ? 1 : found->level + 1, lvl);
        const auto until = now_ms + std::uint32_t(std::max(d2d::rules::eval_calc(game_data->skills, skill.auralen, env, skill.id, lvl), 1)) * 40;
        if (found == self_states.end()) self_states.push_back({ skill.id, frenzy_charges, until });
        else *found = { skill.id, frenzy_charges, until };
    }

auto Fight::next_target() -> bool {
        const auto* skill = game_data->skills.get(swing_skill);
        if (skill && skill->srvdofunc == 13) { other_target(); return attack_mon >= 0; }
        return attack_mon >= 0 && monsters[std::size_t(attack_mon)].alive();
    }

auto Fight::in_reach() const -> std::vector<int> {
        std::vector<int> out;
        for (std::size_t i = 0; i < monsters.size(); ++i)
            if (monsters[i].alive() && std::hypot(monsters[i].unit.x - player.x, monsters[i].unit.y - player.y) <= kMeleeReach + 0.5f)
                out.push_back(int(i));
        return out;
    }

auto Fight::engage(std::uint32_t now_ms) -> std::optional<std::pair<float, float>> {
        if (attack_mon < 0 || pmode >= 0) return std::nullopt;
        const auto& target = monsters[std::size_t(attack_mon)];
        if (!target.alive()) { attack_mon = -1; return std::nullopt; }
        const auto* skill = game_data->skills.get(attack_skill);
        if (skill && (missile_skill(*skill) || spot_skill(*skill))) {   // from here, at it
            cast_missile(attack_skill, target.unit.x, target.unit.y, now_ms);
            return std::nullopt;
        }
        if (skill && moving_skill(*skill) && (skill->srvdofunc != 67 || std::hypot(target.unit.x - player.x, target.unit.y - player.y) > kMeleeReach)) {
            player.dir = direction16(target.unit.x - player.x, target.unit.y - player.y);
            move_x = target.unit.x; move_y = target.unit.y;                  // Whirlwind to it, Charge and Leap Attack at it
            start_swing(now_ms);
            return std::nullopt;
        }
        if (std::hypot(target.unit.x - player.x, target.unit.y - player.y) <= kMeleeReach) {
            player.dir = direction16(target.unit.x - player.x, target.unit.y - player.y);
            start_swing(now_ms);
            return std::nullopt;
        }
        return std::pair{ target.unit.x, target.unit.y };
    }

auto Fight::crowd(Crowd& crowd_out) -> void {
        for (auto& monster : monsters)
            if (monster.alive() && std::abs(monster.unit.x - player.x) < 12 && std::abs(monster.unit.y - player.y) < 12) crowd_out.units.push_back(&monster.unit);
        for (auto& pet : pets)                                 // pets stand in the way too (Bone Wall)
            if (pet.monster.alive() && pet.where == level) crowd_out.units.push_back(&pet.monster.unit);
    }

auto Fight::world(bool in_moor, std::uint32_t now_ms, float elapsed, const Crowd& crowd) -> void {
        // Monsters think while the player is near (D2 runs the rooms
        // around each player); what they hit comes off the player's life,
        // and a hit of a twelfth of max life or more makes them flinch (GH).
        if (in_moor) {
            std::vector<Foe> foes{ Foe{ player.x, player.y, int(character.stats.get(d2d::d2s::kLevel)), true, player.walking, player_combat },
                                   Foe{ merc ? merc->x : 0, merc ? merc->y : 0, merc_st.level, merc && merc_mode != "DT",
                                        merc && merc->walking, merc_fighter() } };
            for (std::size_t k = 0; k < 2; ++k)                  // Amplify Damage on them: damage reduced -100 %
                if (now_ms < amplified[k]) foes[k].fighter.dr_pct -= 100;
            monster_auras(foes, now_ms);
            for (const auto& pet : pets) foes.push_back(pet_foe(pet));
            for (std::size_t i = 0; i < monsters.size(); ++i) {
                auto& monster = monsters[i];
                if (std::abs(monster.unit.x - player.x) < 30 && std::abs(monster.unit.y - player.y) < 30
                    && monster_update(*game_data, *level, monster, foes, rng, now_ms, elapsed, crowd, missiles))
                    killed(i, now_ms);                           // on the player's thorns
            }
            boss_events(foes, now_ms);
            monster_dots(now_ms, elapsed);
            monster_states(now_ms);
            buff_events(foes[0], now_ms);
            buff_tick(now_ms);
            aura_pulse(now_ms);
            for (auto& monster : monsters) monster.in_aura = monster.alive() && in_aura(monster);   // its auratargetstate shows (states.md)
            for (std::size_t k = 0; k < pets.size(); ++k) pet_hurt(pets[k], foes[2 + k].damage, now_ms);
            pets_turn(now_ms, elapsed, crowd);
            missile_tick(now_ms);
            // The merc's arrows strike the first live monster they reach.
            missiles_update(*level, missiles, foes, rng, now_ms, elapsed, [&](Missile& missile) {
                for (std::size_t i = 0; i < monsters.size(); ++i) {
                    auto& monster = monsters[i];
                    if (!monster.alive() || std::hypot(monster.unit.x - missile.x, monster.unit.y - missile.y) > 0.5f) continue;
                    if (missile.row) {                             // a shrine's potion: everyone in its burst
                        for (std::size_t j = 0; j < monsters.size(); ++j)
                            if (monsters[j].alive() && std::hypot(monsters[j].unit.x - missile.x, monsters[j].unit.y - missile.y) * 5 <= float(std::max(missile.burst, 1)))
                                land(j, d2d::rules::missile_blow(*missile.row, target_of(j), {}, rng), true, now_ms);
                        return true;
                    }
                    if (missile.skill >= 0) {
                        if (std::ranges::contains(*missile.struck, int(i))) continue;
                        if (skill_missile_hits(missile, i, now_ms)) return true;
                        continue;
                    }
                    land(i, d2d::rules::player_blow(d2d::rules::simple_fighter(missile.min, missile.max, missile.attack_rating), target_of(i), missile.level, rng),
                         false, now_ms);
                    return true;
                }
                return false;
            });
            std::ranges::move(pending, std::back_inserter(missiles));
            pending.clear();
            if (merc && foes[1].damage > 0 && merc_mode != "DT") {
                merc_life -= foes[1].damage;
                auto& unit = *merc;
                const auto mode = merc_life <= 0 ? std::string_view("DT") : foes[1].damage * 12 >= merc_st.life ? std::string_view("GH") : merc_mode;
                if (mode != merc_mode) {
                    merc_mode = mode; unit.mode_ms = now_ms; unit.walking = false; unit.path.clear();
                    merc_until = now_ms + game_data->npc_timing(*merc_npc, mode).length_ms();
                }
            }
            for (auto& monster : monsters)
                if (std::abs(monster.unit.x - player.x) < 30 && std::abs(monster.unit.y - player.y) < 30) monster_sounds(monster, now_ms);
            auto& foe = foes[0];
            // Poison works on the player over its ticks (a negative potion).
            // One poison at a time: a new one at least as strong replaces it
            // (and its length), a weaker one is ignored (FUN_0057ac50).
            if (foe.poison > 0) {
                const Regen poison{ -foe.poison * 256.0 / (foe.poison_ticks * 40.0), 0, now_ms + std::uint32_t(foe.poison_ticks) * 40, true };
                const auto old = std::ranges::find_if(regen, [](const Regen& regen_entry) { return regen_entry.poison; });
                if (old == regen.end()) regen.push_back(poison);
                else if (poison.life <= old->life) *old = poison;
            }
            for (std::size_t k = 0; k < 2; ++k)                  // a Cursed boss's Amplify Damage: its auralen
                if (const auto* skill = game_data->skills.get(66); skill && foes[k].amplify > 0) {
                    amplified[k] = now_ms + std::uint32_t(std::max(calc(*skill, skill->auralen, foes[k].amplify), 25)) * 40;
                    d2d::log::info("{} cursed: Amplify Damage level {}", k ? "the merc" : "the player", foes[k].amplify);
                }
            if (foe.mana_burn > 0) {                             // Mana Burn
                character.stats.values[d2d::d2s::kMana] = std::max<std::int64_t>(character.stats.values[d2d::d2s::kMana] - (std::int64_t(foe.mana_burn) << 8), 0);
                d2d::log::info("mana burn: -{} mana", foe.mana_burn);
            }
            if (foe.blocked && pmode < 0) set_pmode(kModeBL, now_ms);   // a block plays out (FBR)
            foe.damage = absorb(foe.damage);
            foe.damage += int(self_hurt >> 8);
            self_hurt &= 255;
            if (foe.damage > 0) {
                using namespace d2d::d2s;
                character.stats.values[kLife] -= std::int64_t(foe.damage) << 8;
                if (character.stats.values[kLife] <= 0) {
                    character.stats.values[kLife] = 0;
                    set_pmode(kModeDT, now_ms);
                    attack_mon = -1;
                    d2d::log::info("the player died");
                } else if (std::int64_t(foe.damage) * 12 >= character.stats.fixed(kMaxLife) && !attack_mode(pmode) && pmode != kModeBL) {
                    set_pmode(kModeGH, now_ms);
                }
            }
        }
        if (!in_moor) {                                      // pets follow the player about camp
            const auto save_class = std::size_t(std::max(character.character_class, 0));
            for (auto& pet : pets)
                if (pet.where == level && pet.monster.alive()) {
                    merc_follow(*level, pet.monster.unit, player.x, player.y, cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f, now_ms, elapsed, crowd);
                    if (const std::string_view want = pet.monster.unit.walking ? "WL" : "NU"; pet.monster.mode != want) set_mode(*game_data, pet.monster, want, now_ms);
                }
        }
        if (merc && merc_npc) {
            if (in_moor) merc_turn(now_ms, elapsed, crowd);
            else {
                const auto save_class = std::size_t(std::max(character.character_class, 0));
                merc_follow(*level, *merc, player.x, player.y, cells_per_sec(float(game_data->run_velocity[save_class])) * 1.1f, now_ms, elapsed, crowd);
                merc_mode = merc->walking ? "WL" : "NU";
            }
        }
    }

auto Fight::monster_auras(std::vector<Foe>& foes, std::uint32_t now_ms) -> void {
        for (auto& monster : monsters) monster.aura_dmg = monster.aura_th = 0;
        for (auto& aura_monster : monsters) {
            if (!aura_monster.alive() || !aura_monster.aura || std::abs(aura_monster.unit.x - player.x) >= 30 || std::abs(aura_monster.unit.y - player.y) >= 30) continue;
            const auto* skill = game_data->skills.get(aura_monster.aura);
            if (!skill) continue;
            const d2d::rules::CalcEnv env{ [](int) { return 0; }, [](int) { return 0; }, [](int) { return 0; }, aura_monster.stats.level, &rng };
            auto val = [&](const d2d::rules::Calc& calc_row) { return d2d::rules::eval_calc(game_data->skills, calc_row, env, skill->id, aura_monster.aura_lvl); };
            const float radius = float(val(skill->aurarange)) / 5;                     // subtiles -> cells
            auto in_range = [&](float x, float y) { return std::hypot(x - aura_monster.unit.x, y - aura_monster.unit.y) <= radius; };
            if (skill->srvdofunc == 65) {
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
                if (foe.alive && in_range(foe.x, foe.y)) foe.damage += d2d::rules::resisted(rng.range(low, high), foe.fighter.res[std::size_t(skill->etype)]);
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
        for (const auto& [dx, dy] : { std::pair{ -6, 6 }, { -6, -6 }, { 0, 6 }, { 0, -6 }, { 6, 6 }, { 6, -6 } }) {
            const float dir_x = float(dx) / 5, dir_y = float(dy) / 5, speed = cells_per_sec(float(missile_info.vel)), distance = std::hypot(dir_x, dir_y);
            Missile missile{ &missile_info, x, y, dir_x / distance * speed, dir_y / distance * speed, direction32(dir_x, dir_y), now_ms, now_ms + std::uint32_t(std::max(missile_info.range, 1)) * 40, {} };
            missile.friendly = true; missile.row = damage; missile.burst = missile_info.hit_par1;
            pending.push_back(missile);
        }
    }

auto Fight::enter(const Level* destination) -> void {
        if (destination == &game_data->town || destination == mon_level) return;
        if (mon_level) kept[mon_level] = std::move(monsters);
        monsters.clear();
        if (const auto found = kept.find(destination); found != kept.end()) {
            monsters = std::move(found->second);
            kept.erase(found);
        }
        mon_level = destination;
        missiles.clear();
        attack_mon = merc_target = -1;
        for (auto& pet : pets) pet.target = -1;
    }

auto Fight::rooms_up(const Level& here, float x, float y, bool arrived) -> void {
        for (const auto& [grown, from] : player_moved(*game_data, spawning, here, x, y, arrived)) {
            const auto& spawns = spawning.levels[grown].spawns;
            const auto& region = std::size_t(grown->id) < spawning.regions.size() ? spawning.regions[std::size_t(grown->id)] : d2d::rules::Region{};
            auto made = spawn_monsters(*game_data, std::span(spawns).subspan(from), region, rng, game_difficulty);
            auto& into = grown == mon_level ? monsters : kept[grown];
            for (auto& monster : made) { monster.id = next_id++; into.push_back(std::move(monster)); }
        }
    }

}  // namespace d2d::game
