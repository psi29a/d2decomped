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
#include <missiles.hpp>
#include <monsters.hpp>
#include <pets.hpp>
#include <rules.hpp>
#include <shatter.hpp>
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

// FUN_0064d870 with pattern 1 and mask 0x3c01 at the unit's subtile (FUN_005a3910's spot test, FUN_0064d100):
// the subtile and its four neighbours clear of the level's walls (the five terrain probes) and of every other
// unit's stamp (one plus-shaped crowd test at the corpse's subtile, as the grid holds every unit's stamp).
bool rise_spot_blocked(const Level* level, const Crowd& crowd, std::span<const Foe> foes, const UnitState& self) {
    const int sub_x = int(std::floor(self.x * 5)), sub_y = int(std::floor(self.y * 5));
    for (const auto& [off_x, off_y] : { std::pair{ 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } })
        if (level && level->blocked((float(sub_x + off_x) + 0.5f) / 5, (float(sub_y + off_y) + 0.5f) / 5, 0x0c01)) return true;
    constexpr UnitShape kPlus{ 1, 2, 0x3c01 };
    if (crowd.at((float(sub_x) + 0.5f) / 5, (float(sub_y) + 0.5f) / 5, kPlus, &self)) return true;
    return std::ranges::any_of(foes, [&](const Foe& foe) {
        return foe.alive && !foe.of && std::abs(int(std::floor(foe.x * 5)) - sub_x) + std::abs(int(std::floor(foe.y * 5)) - sub_y) <= 1;
    });
}

}  // namespace

// Dragon Tail's kick hit: fire, (calc1 + fire mastery) % of the kick's
// physical damage, on every monster within aurarangecalc subtiles of
// the target, less fire resistance (FUN_005d7180 -> FUN_0056bad0); the
// mastery is the player's stat 329 (psum: items, passives).

// The blast is centred on the unit kicked (FUN_00553540's, esi at
// 0x5d71f7), so it's in it.
auto Fight::dragon_tail(const d2d::rules::Skill& skill, std::size_t target, int phys, std::uint32_t now_ms) -> void {
        const int lvl = skill_level ? skill_level(skill.id) : 1;
        const auto env = calc_env();
        const int mastery = env.stat ? env.stat(d2d::d2s::kPassiveFireMastery) : 0;   // stat 0x149 (FUN_005d7180)
        const int fire = phys * (d2d::rules::eval_calc(game_data->skills, skill.calc[0], env, skill.id, lvl) + mastery) / 100;
        const float radius = float(d2d::rules::eval_calc(game_data->skills, skill.aurarange, env, skill.id, lvl));
        const float center_x = monsters[target].unit.x, center_y = monsters[target].unit.y;
        for (std::size_t i = 0; i < monsters.size() && fire > 0; ++i) {
            auto& monster = monsters[i];
            if (!monster.targetable() || std::hypot(monster.unit.x - center_x, monster.unit.y - center_y) > radius) continue;
            if (hurt(*game_data, monster, d2d::rules::resisted(fire, target_of(i).res[2]), now_ms)) killed(i, now_ms);
        }
    }

// A unique's mods in the fight (uniques.hpp, monsters.md "Boss mods in
// the fight"), each tick: Lightning Enchanted answers a drop in its life
// with 8 charged bolts (FUN_005a29a0: 4 ways, 2 each, level mlvl / 2,
// wandering as Charged Bolt's) at
// most every 10 frames; 4 frames after it dies (the event 7 its death
// queues), Fire Enchanted's blast and Cold Enchanted's nova
// (FUN_005a2bd0: coldunique, level mlvl / 2).

// A hit taken in GH fires the bolts 2 frames on (event 7,
// FUN_005a37d0).
// Each monster's mod hooks in the fight (monsters.md "Boss mods in the fight"), incl. mod 34's FUN_005a3840 / FUN_005a3910.
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
            // Their 8 charged bolts (FUN_005a29a0 / FUN_005a2ee0): 2 at the subtile
            // beside it each way (0x6e2188 / 0x6e2178), wandering as Charged
            // Bolt's (flags 0x21, FUN_005c9290 with 0 and 1).
            auto charged_bolts = [&](const char* name, int bolt_level) {
                const auto ring = std::make_shared<std::vector<int>>();
                for (const auto& [dir_x, dir_y] : kCompass)
                    for (int k = 0; k < 2; ++k) {
                        const std::size_t made = pending.size();
                        boss_missile(monster, name, dir_x, dir_y, ring, now_ms, bolt_level);
                        if (pending.size() == made) continue;
                        auto& bolt = pending.back();
                        bolt.to = { (std::floor(monster.unit.x * 5) + dir_x + 0.5f) / 5, (std::floor(monster.unit.y * 5) + dir_y + 0.5f) / 5 };
                        bolt.bolt = k;
                    }
            };
            // Scarab (FUN_005a2eb0 / FUN_005a2ee0): into GH or DT, 2 frames on,
            // 8 buglightning (4 ways x 2) at its level, steered as lightning enchanted's.
            if (scarab && monster.mode != monster.fx_mode && (monster.mode == "GH" || monster.mode == "DT")) monster.scarab_due.push_back(now_ms + 2 * 40);
            const bool questcomplete = std::ranges::contains(monster.mods, d2d::rules::umod::questcomplete) || std::ranges::contains(family, d2d::rules::umod::questcomplete);
            if (questcomplete && monster.mode != monster.fx_mode && monster.mode == "DT") quest_death(monster, now_ms);
            // Shatter_on_death (MonUMod 35, every frozenhorror1 by FUN_005b21b0):
            // hook 1 0x5a3a80 puts state 107 (shatter) on it as it dies (mode
            // 0), whatever the kill: no corpse to raise or blow up.
            if (std::ranges::contains(family, d2d::rules::umod::shatter) && monster.mode != monster.fx_mode && monster.mode == "DT") monster.shatter = true;
            // Ai_after_death (MonUMod 34, every reanimatedhorde1 by FUN_005b21b0). Hook 1 (FUN_005a3840) is in
            // set_mode, as it dies. Hook 2 (FUN_005a3910, event 7, `rise_at`): a corpse in DD, no unique, not
            // good, its spot free around the corpse (every hero's side), fewer than 2 rises (AI scratch +0x14),
            // takes skill 0x125 in S1: life full, flags 0x4020000 off, scratch + 1, its think 0x33 frames on
            // (`next_act`); a blocked spot tries again aip1 x 10 + 1 frames on. Srvstfunc 61 (0x5d1bf0)
            // takes the unit flags 0xe off (`rising`); its think (FUN_005e1540) puts them back.
            if (std::ranges::contains(family, d2d::rules::umod::ai_after_death) && monster.type >= 0) {
                const auto& type_info = game_data->monsters.types[std::size_t(monster.type)];
                const auto& per_difficulty = type_info.diff[std::size_t(std::clamp(monster.difficulty, 0, 2))];
                // A death animation that ran out while no hero was near (monster_update, which ends it, is not
                // called out of range) is over before event 7 is delivered: the corpse is in DD, as FUN_005a3910 finds it.
                if (monster.mode == "DT" && !monster.alive() && now_ms >= monster.mode_until) set_mode(*game_data, monster, "DD", now_ms);
                if (monster.rise_at && now_ms >= monster.rise_at) {
                    monster.rise_at = 0;
                    Crowd crowd_now;
                    crowd_around(crowd_now, monster.unit.x, monster.unit.y);
                    const bool corpse = monster.mode == "DD" && !monster.corpse_used && !monster.shatter;
                    const auto rise = d2d::rules::ai_after_death_rise({
                        .dead = monster.mode == "DT" || monster.mode == "DD", .hidden = monster.corpse_used || monster.shatter,
                        .unique = monster.boss == Boss::unique || monster.boss == Boss::superunique,
                        .blocked = rise_spot_blocked(mon_level ? mon_level : level, crowd_now, foes, monster.unit),
                        .can_raise = corpse && type_info.corpse_sel && type_info.velocity != 0,
                        .alignment = monster.align, .scratch = monster.ai_state, .aip1 = per_difficulty.aip[0] });
                    if (rise.kind == d2d::rules::RiseKind::again) monster.rise_at = now_ms + std::uint32_t(rise.frames) * 40;
                    if (rise.kind == d2d::rules::RiseKind::rise) {
                        d2d::log::info("{} rises again", monster.npc.name);
                        monster.hit_points = monster.stats.hit_points;
                        monster.life_frac = monster.max_life_frac;
                        monster.no_exp = monster.no_drop = false;
                        ++monster.ai_state;
                        monster.rising = true;
                        set_mode(*game_data, monster, "S1", now_ms);
                        monster.thought_ms = now_ms;                       // its think (event 2): the rise's 0x33 frames on
                        monster.next_act = now_ms + std::uint32_t(rise.frames) * 40;
                    }
                }
            }
            // Partydead (MonUMod 15, boneprison1..4 by FUN_005b21b0): hook 1 FUN_005a2d10 as it dies,
            // with an owner (FUN_0058f0d0): each of the owner's minions leaves the group and dies
            // (FUN_0058f380 -> FUN_005a2cc0: FUN_005a7e60 mode 0, no credit), then the owner; the
            // first bone has none, so it falls alone.
            if (std::ranges::contains(family, d2d::rules::umod::partydead) && monster.mode != monster.fx_mode && monster.mode == "DT" && monster.party_owner >= 0) {
                const int owner = std::exchange(monster.party_owner, -1);
                for (auto& other : monsters)
                    if ((other.party_owner == owner || other.id == owner) && &other != &monster) {
                        if (other.party_owner == owner) other.party_owner = -1;
                        if (other.alive()) { other.hit_points = 0; set_mode(*game_data, other, "DT", now_ms); }
                    }
            }
            // A shattered death (the client's unit command 8, FUN_004aff60 from LAB_004b053e):
            // freeze (1) and shatter (107) carry States.txt hide and shatter, so the corpse is
            // hidden and can't be picked (FUN_0063a320: corpse_used), its hit class's element
            // becomes 0xa0, and FUN_004afc10 breaks it into ice (rules::shatter_rows) from it.
            // Each stands on the corpse unless its row has a speed (rules::shatter_row_made),
            // for its Range + LevRange at level 1, facing a draw below 64 (FUN_004cd540, flags 0x2020).
            // The draw is on the missile's own client seed (+0x20): its room's client stream stepped once,
            // {low, 666} (FUN_00465fd0, client_unit_seed), each made in turn.
            if (monster.mode != monster.fx_mode && monster.mode == "DT" && (monster.shatter || now_ms < monster.frozen_until)) {
                monster.corpse_used = true;
                monster.hit_class = (monster.hit_class & 0xf) | 0xa0;
                const auto rows = d2d::rules::shatter_rows(1, monster.npc.height, false, now_ms < monster.frozen_until);
                for (const int row : { rows.first, rows.second })
                    if (const auto found = game_data->missiles.find(std::string(d2d::rules::shatter_row_name(row)));
                        found != game_data->missiles.end() && d2d::rules::shatter_row_made(found->second.vel, found->second.vel_lev)) {
                        const int ice_frames = d2d::rules::missile_range(found->second.range, found->second.lev_range, 1);
                        auto client_seed = level ? client_unit_seed(*level, room_of(*level, monster.unit.x, monster.unit.y)) : d2d::rules::Rng{ 1 };
                        const int facing = client_seed(64);
                        Missile ice{ &found->second, monster.unit.x, monster.unit.y, 0, 0, facing, now_ms, now_ms + std::uint32_t(std::max(ice_frames, 1)) * 40, {} };
                        ice.visual_only = true;
                        ice.client_seed = client_seed;
                        ice.client_seeded = true;
                        pending.push_back(std::move(ice));
                    }
            }
            // Killself's event (FUN_005a3aa0): alive, 3 frames on while
            // uninterruptable (state 0x36), else into death (FUN_005a7c20 mode 0:
            // no credit). Its player-owner branch (FUN_005750e0) needs a pet,
            // which FUN_005dfbf0's evil-only pick never gives it.
            if (monster.killself_at && now_ms >= monster.killself_at) {
                monster.killself_at = 0;
                if (monster.uninterruptable()) monster.killself_at = now_ms + 3 * 40;
                else if (monster.alive()) {
                    monster.hit_points = 0;
                    set_mode(*game_data, monster, "DT", now_ms);
                    killed(std::size_t(&monster - monsters.data()), now_ms, false);
                }
            }
            const bool into_gh = monster.mode != monster.fx_mode && monster.mode == "GH";
            monster.fx_mode = monster.mode;
            while (!monster.scarab_due.empty() && now_ms >= monster.scarab_due.front()) {
                monster.scarab_due.erase(monster.scarab_due.begin());
                d2d::log::info("{} lets off its bugs' lightning", monster.npc.name);
                charged_bolts("buglightning", mlvl);
            }
            if (monster.alive()) {
                // Into GH, hook 1 (FUN_005a37d0, after the mode change) queues event 7
                // two frames on. Struck otherwise (event 4: FUN_0057cee0's soft path, a
                // burner's soft hit too), hook 4 (FUN_005a2ba0) fires at once, but not
                // in GH: it tests mode 3 (FUN_0057cee0 sets GH before its event 4).
                // Event 4 comes of result flag 4 (a landed hit) or 0x4000, its mode the
                // one at the hit (Fight::land, Fight::burn); a burn's or a poison's drop
                // in life raises none.
                bool fire = false;
                if (has(d2d::rules::umod::lightning)) {
                    if (into_gh) monster.bolts_due = now_ms + 2 * 40;
                    if (monster.struck_event) fire = true;
                }
                if (monster.bolts_due && now_ms >= monster.bolts_due) { monster.bolts_due = 0; fire = true; }
                if (fire && now_ms - monster.bolts_at >= 10 * 40) {
                    monster.bolts_at = now_ms;
                    d2d::log::info("{} lets off charged bolts", monster.npc.name);
                    charged_bolts("lightunique", 0);
                }
            } else if ((boss || mummy || suicide) && !monster.fx_done) {
                if (!monster.fx_at) monster.fx_at = now_ms + 4 * 40;
                else if (now_ms >= monster.fx_at) {
                    monster.fx_done = true;
                    // Poisondead (FUN_005a3800 / FUN_005a2c20): corpsepoisoncloud (155) on its spot at its level,
                    // a standing cloud (srvdofunc 3, CollideKill 0): its Size 1 footprint, one foe a frame, never
                    // the one it struck last (missiles_update's last_unit_rule; tools/emu/standing_rehit.py).
                    if (mummy) {
                        d2d::log::info("{} leaves a poison cloud", monster.npc.name);
                        boss_missile(monster, "corpsepoisoncloud", 0, 0, std::make_shared<std::vector<int>>(), now_ms, mlvl);
                    }
                    if (has(d2d::rules::umod::fire)) fire_blast(monster, foes, now_ms);
                    if (suicide) suicide_blast(monster, foes, now_ms);
                    if (has(d2d::rules::umod::cold)) {
                        const auto ring = std::make_shared<std::vector<int>>();
                        for (int k = 0; k < 64; ++k) {
                            const auto [aim_x, aim_y] = d2d::rules::kNovaRing[std::size_t(k)];
                            const std::size_t had = pending.size();
                            boss_missile(monster, "coldunique", float(aim_x), float(aim_y), ring, now_ms);
                            if (pending.size() > had) pending.back().to = offset_target(monster.unit.x, monster.unit.y, aim_x, aim_y);   // FUN_0056d400
                        }
                    }
                }
            }
            monster.struck_event = false;
        }
    }

// FUN_005dfbf0: each monster the unit finder takes within radius subtiles
// of the boss (flags 0x583: alive, unit +0xc4 & 4 and & 8, d2d's
// targetable(); not in town, which no boss here is), evil (FUN_006259b0)
// and not diabloclone (0x14d), undead (FUN_0063e990) when the hook asks
// (Blood Raven's), gets killself (MonUMod 21, FUN_005a4850(0x15, 0)), its
// event 7 first + rand(end - first) frames on. Mephisto's
// (FUN_005dfdb0) is mephistodeathcontrol (299) on himself; Radament's also
// radamentdeath (347) at his spot (FUN_0056ede0).
// The finder is FUN_0065a950 / FUN_0065ac70. FUN_0065a6b0 keeps the boss's
// own room when the square of the radius lies inside it; otherwise the
// near list, and FUN_0065a710 lets every room of that list through
// (bugs.md #23). A unit in none of those rooms is missed, however close.
// FUN_0065ac70 walks each room's unit list from its head (room +0x74, next
// at +0xe8). FUN_0064c2c0 inserts at the head, so each room's draws run
// newest first. monsters is oldest first.
// Radament's radamentdeath at his spot is drawn too: its client do 32 (missile_flight.hpp) lays the
// Hands of God.
// ponytail: Mephisto's mephistodeathcontrol (client do 26: fire wall makers and flying rocks, each a
// client do of its own) isn't made.
auto Fight::quest_death(Monster& boss, std::uint32_t now_ms) -> void {
        const auto row = [&](int bin_row) { return std::size_t(bin_row) < game_data->mon_bin.size() ? int(game_data->mon_bin[std::size_t(bin_row)]) : -1; };
        const auto radament = game_data->missiles.find("radamentdeath");
        if (const auto hands = game_data->missiles.find("radamenthandofgod");
            boss.type == row(0xe5) && radament != game_data->missiles.end() && hands != game_data->missiles.end()) {
            Missile death{ &radament->second, boss.unit.x, boss.unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(radament->second.range, 1)) * 40, {} };
            death.visual_only = true;                               // a server do 19 / client do 32 row with no cel of its own: what shows is the client's
            death.sub = &hands->second;
            death.owner = boss.id;
            missiles.push_back(std::move(death));
        }
        const auto subtile = [](float value) { return int(std::floor(value * 5)); };
        for (const auto& hook : d2d::rules::killself_hooks(radament != game_data->missiles.end() ? radament->second.range : 0)) {
            if (boss.type != row(hook.monstats_row) || !level) continue;
            const int from_x = subtile(boss.unit.x), from_y = subtile(boss.unit.y);
            const int own_room = room_of(*level, boss.unit.x, boss.unit.y);
            std::vector<int> rooms;
            if (const auto room_rect = own_room >= 0 ? level_room(*level, std::size_t(own_room)) : std::nullopt;
                room_rect && room_rect->x * 5 < from_x - hook.radius && room_rect->y * 5 < from_y - hook.radius
                    && from_x + hook.radius < (room_rect->x + room_rect->width) * 5 && from_y + hook.radius < (room_rect->y + room_rect->height) * 5)
                rooms.push_back(own_room);
            else {
                for (const auto& [room_level, near_room] : near_rooms(*game_data, *level, boss.unit.x, boss.unit.y))
                    if (room_level == level) rooms.push_back(near_room);
            }
            restamp();                                               // the lists as they stand now (FUN_0064c2c0 re-files a unit as it changes rooms)
            std::vector<d2d::rules::RoomUnit> units;
            units.reserve(monsters.size());
            for (const auto& other : monsters) {
                const int off_x = subtile(other.unit.x) - from_x, off_y = subtile(other.unit.y) - from_y;
                const int other_room = room_of(*level, other.unit.x, other.unit.y);
                const bool undead = other.type >= 0 && std::size_t(other.type) < game_data->monsters.types.size() && game_data->monsters.types[std::size_t(other.type)].undead;
                const bool take = std::ranges::contains(rooms, other_room) && other.targetable() && other.align == 0 && other.type != row(0x14d)
                    && off_x * off_x + off_y * off_y <= hook.radius * hook.radius && (!hook.undead_only || undead);
                units.push_back({ other_room, take, other.unit.room_stamp });
            }
            for (const int index : d2d::rules::room_order(units, rooms)) {
                auto& other = monsters[std::size_t(index)];
                const std::uint32_t due = now_ms + std::uint32_t(hook.first + boss.seed(hook.end - hook.first)) * 40;
                if (!other.killself_at || due < other.killself_at) other.killself_at = due;
            }
            d2d::log::info("{} is dead: those about it die with it", boss.npc.name);
        }
    }

// A boss's missile from where it stands, at level mlvl / 2 (at least
// 1), the row's own element (FUN_0064b100 ..).
auto Fight::boss_missile(const Monster& monster, const char* name, float dx, float dy, const std::shared_ptr<std::vector<int>>& ring, std::uint32_t now_ms, int missile_level) -> void {
        const auto found = game_data->missiles.find(name);
        if (found == game_data->missiles.end()) return;
        const auto& missile_info = found->second;
        const int lvl = missile_level > 0 ? missile_level : std::max(monster.stats.level / 2, 1);   // the enchanted mods' mlvl / 2 by default
        const auto damage = d2d::rules::row_damage(missile_info.etype, missile_info.emin, missile_info.emax, missile_info.emin_lev, missile_info.emax_lev, missile_info.hitshift, missile_info.elen, missile_info.elen_lev, lvl);
        d2d::rules::MonStats stats;
        stats.level = monster.stats.level;
        stats.to_hit = monster.stats.to_hit;
        stats.no_hit_roll = !missile_info.to_hit;                   // ToHit 0: no roll (FUN_005adf10)
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
// up); on its spot (FUN_0056ecb0) suicidefireexplode (427) for fire,
// suicideiceexplode (428) for cold, else suicidecorpseexplode (426)
// (0x5a4002..0x5a414b); everyone within its aip4 (+0x68) subtiles
// (FUN_0057e090, flags 0x581). Every suicideminion row has aip1 15: cold.
auto Fight::suicide_blast(Monster& monster, std::span<Foe> foes, std::uint32_t now_ms) -> void {
        const auto& aip = game_data->monsters.types[std::size_t(monster.type)].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))].aip;
        const int low = monster.stats.a1_min, high = monster.stats.a1_max;
        const int pts = high > low ? low + monster.seed(high - low) : low;
        const int element = aip[0];
        for (auto& foe : foes) {
            if (!blast_reaches(monster, foe, aip[3])) continue;
            d2d::rules::Taken taken;
            taken.hit = true;
            taken.damage = std::max(pts * (100 - foe.fighter.dr_pct) / 100 - foe.fighter.dr_flat, 0);
            if (element == 1) taken.damage += d2d::rules::resisted(pts, foe.fighter.res[0]);
            else if (element >= 2) {
                const int cold = d2d::rules::resisted(pts, foe.fighter.res[2]);
                taken.damage += cold;
                if (cold > 0) taken.chill_ticks = d2d::rules::resisted(element, foe.fighter.res[2]);
            }
            foe.take(taken, monster.id);
        }
        const char* const look = element == 1 ? "suicidefireexplode" : element >= 2 ? "suicideiceexplode" : "suicidecorpseexplode";
        if (const auto found = game_data->missiles.find(look); found != game_data->missiles.end()) {
            Missile x{ &found->second, monster.unit.x, monster.unit.y, 0, 0, 0, now_ms, now_ms + std::uint32_t(std::max(found->second.range, 1)) * 40, {} };
            x.visual_only = true;
            pending.push_back(std::move(x));
        }
        d2d::log::info("{} blows up ({} physical{})", monster.npc.name, pts, element == 1 ? " + fire" : element >= 2 ? " + cold" : "");
    }

// FUN_0057e090's area: FUN_0065aa40 takes a unit whose subtile is within
// `radius` (dx² + dy² <= r²), then FUN_00645950 the bare line from the
// blast to it (FUN_0064e260) clear of mask 0x805.
auto Fight::blast_reaches(const Monster& monster, const Foe& foe, int radius) const -> bool {
        if (!foe.alive) return false;
        const int from_x = int(std::floor(monster.unit.x * 5)), from_y = int(std::floor(monster.unit.y * 5));
        const int to_x = int(std::floor(foe.x * 5)), to_y = int(std::floor(foe.y * 5));
        if ((to_x - from_x) * (to_x - from_x) + (to_y - from_y) * (to_y - from_y) > radius * radius) return false;
        return !mon_level || !d2d::rules::sight_blocked(from_x, from_y, 0, to_x, to_y, 0, [&](int at_x, int at_y) {
            return mon_level->blocked((float(at_x) + 0.5f) / 5, (float(at_y) + 0.5f) / 5, 0x805);
        });
    }

// Fire Enchanted's death blast (uniques.hpp fire_blast): physical and
// fire, each a 64th of the roll in 256ths, on everyone within
// difficulty + 4 subtiles; the corpse's guts (monstercorpseexplode).
auto Fight::fire_blast(const Monster& monster, std::span<Foe> foes, std::uint32_t now_ms) -> void {
        const auto [low, high] = d2d::rules::fire_blast(monster.stats.hit_points, monster.difficulty);
        const int pts = (low + rng(std::max(high - low, 1))) * 64 / 256;
        for (auto& foe : foes) {
            if (!blast_reaches(monster, foe, monster.difficulty + 4)) continue;
            d2d::rules::Taken taken;
            taken.hit = true;
            taken.damage = std::max(pts * (100 - foe.fighter.dr_pct) / 100 - foe.fighter.dr_flat, 0) + d2d::rules::resisted(pts, foe.fighter.res[0]);
            foe.take(taken, monster.id);
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
        if (actor->merc_st.level > merc_level) {
            // FUN_0057e860 -> FUN_00572840: the level-up sets its base max life (stat 7, 0x5729d1)
            // and its life (stat 6, 0x5729dc) to the same value, its items' max life left out.
            actor->merc->set_life_fine(actor->merc_st.life * 256);
            d2d::log::info("{}'s mercenary reaches level {}", actor->character.name, actor->merc_st.level);
        }
    }

// The players in the game (game_players: the /players count when it's
// larger) and those of them alive (the party on the level: one screen),
// for NoDrop (Loot::drop); alone, game_players() and 1.
auto Fight::drop_counts() -> std::pair<int, int> {
        if (heroes.size() == 1) return { game_players(), 1 };
        Hero& was = *actor;
        int alive = 0;
        for (Hero* hero : heroes) { act_as(*hero); alive += dead() ? 0 : 1; }
        act_as(was);
        return { game_players(), std::max(alive, 1) };
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
        // No steal divisor: FUN_0057c420 divides a player's leech by DifficultyLevels' LifeStealDivisor / ManaStealDivisor
        // (0x57c49e..0x57c4ce), a hireling's not (it joins that path at 0x57c4d1); land() heals the merc.
        auto struck = merc_target(target);
        struck.life_steal_divisor = struck.mana_steal_divisor = 1;
        const auto blow = d2d::rules::player_blow(fighter, struck, actor->merc_st.level, rng, swing);
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

// A curse's damagepercent (Weaken) is in the blow before its roll: pet_swing's, or a Shadow's shadow_fighter.
auto Fight::pet_strike(Pet& pet, std::size_t target, const d2d::rules::Blow& blow, std::uint32_t now_ms) -> void {
        const int thorns = monsters[target].props.thorns;
        const int target_life = monsters[target].life_fine();               // as FUN_005c6530 reads it: before the blow's loss
        const bool landed = land(target, blow, false, now_ms, pet.monster.id);
        // Blood Golem's life steal (its skill's domeleedamage event, func 23: rules::blood_golem_steal), on the
        // golem before the struck one's thorns come back (event 5 before event 1 / 2, FUN_0057c6c0).
        if (const auto* skill = game_data->skills.get(pet.skill); landed && skill && !dead()) {   // an owner dying: none at all (0x5c65d4)
            for (std::size_t event = 0; event < 3; ++event) {
                if (skill->aura_event[event] != "domeleedamage" || skill->aura_event_func[event] != 23) continue;
                const auto& victim = monsters[target];
                const int drain = game_data->monsters.types[std::size_t(victim.type)].diff[std::size_t(victim.difficulty)].drain;
                const int owner_max = int(actor->character.stats.values[d2d::d2s::kMaxLife]);
                // The steal reads blow.phys_fine, the swing's physical with any Weaken in it (pet_swing, FUN_0057b420).
                const auto lives = d2d::rules::blood_golem_steal(true, drain, victim.align == 0, blow.phys_fine, target_life,
                                                                 d2d::rules::calc_dm(skill->par[0], skill->par[1], pet.level), skill->par[2], true,
                                                                 { int(actor->character.stats.values[d2d::d2s::kLife]), pet.monster.life_fine() }, owner_max,
                                                                 pet.monster.max_life_fine());
                actor->character.stats.values[d2d::d2s::kLife] = lives.owner_life;
                pet.monster.set_life_fine(lives.golem_life);
            }
        }
        // Its item_slow's domeleedamage (func 19, FUN_005c0310; Clay Golem's aurastat, hooked by FUN_005c4470
        // 0x5c4686..0x5c46f2): the struck monster slowed (rules::item_slow, put_slow).
        if (auto& victim = monsters[target]; landed && pet.slow != 0 && victim.type >= 0) {
            const bool boss = game_data->monsters.types[std::size_t(victim.type)].boss_column;
            if (const int slow = d2d::rules::item_slow(pet.slow, 1, victim.target(*game_data).special, boss, false, victim.boss == d2d::rules::Boss::superunique))
                put_slow(*game_data, victim, slow, now_ms);
        }
        // A MonProp thorns monster's flat value back on the pet (FUN_005bf670),
        // a monster striker: through its own damage resist, as a monster's hit on a monster.
        if (landed && thorns > 0) pet_hurt(pet, d2d::rules::resisted(thorns, pet.monster.target(*game_data).res[0]), now_ms);
    }

}  // namespace d2d::game
