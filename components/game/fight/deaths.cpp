// SPDX-License-Identifier: GPL-3.0-or-later
// Fight (fight.hpp): bosses' events, deaths, experience and thorns. The rest is in this
// directory.
#include "fight.hpp"

#include "ai.hpp"
#include "character.hpp"
#include "cues.hpp"
#include "gamedata.hpp"
#include "log.hpp"

#include <combat.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <merc.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <sound_ids.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::game {

namespace {

// Up, right, down, left: where a boss's ring of bolts leaves.
constexpr std::pair<float, float> kCompass[] = { { 0.f, -1.f }, { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f } };

}  // namespace

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
        const bool landed = land(target, blow, false, now_ms, merc_foe_id());
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
        const bool landed = land(target, blow, false, now_ms, pet.monster.id);
        // A MonProp thorns monster's flat value back on the pet (FUN_005bf670),
        // a monster striker: through its own damage resist, as a monster's hit on a monster.
        if (landed && thorns > 0) pet_hurt(pet, d2d::rules::resisted(thorns, pet.monster.target(*game_data).res[0]), now_ms);
    }

}  // namespace d2d::game
