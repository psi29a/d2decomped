// SPDX-License-Identifier: GPL-3.0-or-later
// Act 5's think inputs (rules/thinks/act5.hpp reads them), the traps' and
// the rest the levels place.
#include "ai.hpp"

#include "gamedata.hpp"
#include "internal.hpp"

#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace d2d::game {

namespace {

struct Act5Ai : ThinkCtx {
    // The Minion's command's unit (its in reach by melee) and its last
    // frame: only Cry Help sends a minion one.
    void minion() {
        if (monster.ai_command != 1 || monster.command_foe < 0) return;   // not under Cry Help
        input.command_unit = std::ranges::any_of(foes, [&](const Foe& sent) { return sent.id == monster.command_foe && sent.alive; });
        input.command_until = monster.command_until;
    }
    void succubus() { input.target_player = !target->pet && !target->of; }
    // The Overseer's search (FUN_005e26d0; Nihlathak's too, FUN_005ee5d0) over
    // the near order: friendly (FUN_00554200) live minions (base 0x1c5) not in
    // Bloodlust (state 0x8d, the whip's), counted; the first under half life
    // within 24 (squared 0x240; Nihlathak's 0), the first not special (data
    // +0x16 & 8, FUN_005a0180) within 20 (0x190; his 25, 0x271).
    void minion_search(int hurt_reach, int free_reach) {
        const int minion = int(game_data.mon_bin[0x1c5]);
        const auto lusted = [&](const Monster& other) {
            const auto* lust = game_data.skills.get(other.cry.skill);
            return lust && lust->srvdofunc == d2d::rules::ServerDoFunction::kOverseerWhip && now_ms < other.cry.until;
        };
        for (const int found : near_order([&](const Monster& other) {
                 return &other != &monster && game_data.monsters.types[std::size_t(other.type)].base == minion && other.alive() && d2d::rules::friends(monster.align, other.align) && !lusted(other);
             })) {
            const auto& other = pack[std::size_t(found)];
            ++input.young;
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y, squared = off_x * off_x + off_y * off_y;
            const int life = int(std::int64_t(other.hit_points) * 100 / std::max(other.stats.hit_points, 1));
            if (kin < 0 && squared <= hurt_reach && life < 50) kin = found;
            if (aid < 0 && squared <= free_reach && (other.boss == d2d::rules::Boss::none || other.boss == d2d::rules::Boss::minion)) aid = found;
        }
        input.hurt_minion = kin >= 0; input.free_minion = aid >= 0;
    }
    // The Overseer's warcry goes at the unit that last struck at it
    // (FUN_005de830: unit +0xa0, not dying or dead, its enemy).
    void overseer() {
        minion_search(0x240, 0x190);
        const auto struck = std::ranges::find(foes, monster.struck_by, &Foe::id);
        if (monster.struck_by != -1 && struck != foes.end() && struck->alive && !d2d::rules::friends(monster.align, struck->of ? struck->of->align : 2)) {
            input.attacker = true;
            second = &*struck;
        }
    }
    // Nihlathak's Corpse Explosion (FUN_0056e390): the first unused,
    // corpseSel DD corpse within 10, squared subtile distance <= 100.
    void nihlathak() {
        minion_search(0, 0x271);
        corpse = first_near([&](const Monster& other) {
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
            return other.usable_corpse()
                && game_data.monsters.types[std::size_t(other.type)].corpse_sel && off_x * off_x + off_y * off_y <= 100;
        });
        input.corpse = corpse >= 0;
    }
    // The spawner's (FUN_005e2b70) counts live minions and suicide minions
    // (0x1cd) not under Confuse / Attract / Conversion (FUN_006259b0 0).
    void minion_spawner() {
        const int minion = int(game_data.mon_bin[0x1c5]), suicide = int(game_data.mon_bin[0x1cd]);
        const auto base_of = [&](const Monster& other) { return game_data.monsters.types[std::size_t(other.type)].base; };
        input.young = int(std::ranges::count_if(pack, [&](const Monster& other) {
            return &other != &monster && other.alive() && (base_of(other) == minion || base_of(other) == suicide) && other.align == 0;
        }));
    }
    // The Ancients: Madawc's Shout on him; which by class, a foe picked (the
    // target), awake (a5q5's record, set by World::act5_update); Talic's state 0x36.
    void ancient() {
        input.skill_state = monster.cry.skill >= 0 && monster.cry.skill == skill_id(game_data, type_info.skill[0]) && now_ms < monster.cry.until;
        input.asleep = monster.asleep;
        input.state36 = monster.uninterruptable();
        input.ancient = type_info.id == "ancientbarb2" ? 1 : type_info.id == "ancientbarb3" ? 2 : 0;
        input.picked = targeted;
        input.self_size = type_info.size; input.target_size = target->size;
    }
    void baal_taunt() { input.target_still = target->neutral; }
    // The putrid defiler's find (FUN_005dd0b0 mode 1 with FUN_005efa50):
    // the first monster Impregnate takes (FUN_005d2140) within 25
    // (FUN_005dc380), in near-room then newest-unit order; in reach
    // (FUN_00622c40) it casts at it.
    void putrid_defiler() {
        kin = first_near([&](const Monster& host) {
            return &host != &monster && impregnable(game_data, host)
                && d2d::rules::near_distance(subtile(host.unit.x) - x, subtile(host.unit.y) - y, game_data.monsters.types[std::size_t(host.type)].size) <= 25;
        });
        input.defile = kin >= 0;
        if (kin >= 0) {
            const auto& host = pack[std::size_t(kin)];
            input.reach = d2d::rules::unit_distance(subtile(host.unit.x) - x, subtile(host.unit.y) - y, type_info.size, game_data.monsters.types[std::size_t(host.type)].size) <= type_info.melee_rng + 1;
        }
    }
    // The Imp reads imp1..imp4's aips (MonStats 0x1ec..0x1ef). Its ride
    // word (+0x14) names the siege beast or tower that took it (FUN_005e17e0):
    // found (FUN_00552f60), dead (FUN_005541b0), ridden (FUN_00552fd0), its
    // alignment (FUN_006259b0), its spot; its own alignment first. Riding,
    // its mount found, dead or its alignment (FUN_005e2d80: one branch).
    void imp() {
        if (monster.ai_state == 0 && monster.special != 16) monster.ai_state = -1;   // its init, FUN_005e2fd0 (rules::ai_toggle)
        for (std::size_t k = 0; k < 4; ++k)
            input.imp_aip[k] = game_data.monsters.types[std::size_t(game_data.mon_bin[0x1ec + k])].diff[std::size_t(std::clamp(monster.difficulty, 0, 2))].aip;
        int mount = -1;
        if (monster.special == 16) mount = monster.bound >= 0 && std::size_t(monster.bound) < pack.size() ? monster.bound : -1;
        else if (const auto found = std::ranges::find(pack, monster.ai_state, &Monster::id); monster.ai_state != -1 && found != pack.end()) mount = int(found - pack.begin());
        if (mount >= 0) {
            const auto& ride = pack[std::size_t(mount)];
            input.command_unit = true;
            input.command_busy = !ride.alive() || ride.align != 0;
            input.rider = monster.special == 16 || ride.bound >= 0;
            kin = mount;
            input.kin_x = subtile(ride.unit.x); input.kin_y = subtile(ride.unit.y);
        }
        input.riding = monster.riding;
        input.state69 = monster.special != 16 && monster.align != 0;
    }
    // The siege beast's and the barricade tower's rider (FUN_00552fd0) and
    // whether it's dead (FUN_005541b0); the first imp to take on in mode
    // 1's order (FUN_005e1720: alive, BaseId imp1, its evil friend, ride
    // word -1, within the beast's aip1 / the tower's 20, squared <=).
    void mount_search(int reach) {
        const Monster* rider = monster.bound >= 0 && std::size_t(monster.bound) < pack.size() ? &pack[std::size_t(monster.bound)] : nullptr;
        input.rider = rider != nullptr;
        input.command_busy = rider && !rider->alive();
        imp_found = first_near([&](const Monster& other) {
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
            return &other != &monster && other.alive() && game_data.monsters.types[std::size_t(other.type)].base == 0x1ec
                && other.align == 0 && other.ai_state == -1 && off_x * off_x + off_y * off_y <= reach * reach;
        });
        input.imp_near = imp_found >= 0;
    }
    // The siege beast's own alignment (FUN_006259b0: not 0 under Confuse /
    // Attract / Conversion, Monster::align).
    void siege_beast() {
        input.state69 = monster.align != 0;
        mount_search(input.aip[0]);
    }
    void siege_tower() { mount_search(20); }
    // The arrow traps' level kind is drawn when one first fires there (in
    // range, lined up, past its frame; FUN_005fb650): Act 1's (levels below
    // 40) the spike, else rand(3) on that trap's seed.
    void arrow(int off) {
        if (level.trap_kind < 0 && input.aip[0] <= input.dist && input.dist <= input.aip[1] && std::abs(off) < 3 && monster.ai_state < frame)
            level.trap_kind = level.id < 40 ? 0 : monster.seed(3);
        input.trap_kind = level.trap_kind;
    }
    void right_arrow() { arrow(target_x - x); }
    void left_arrow() { arrow(target_y - y); }
    // The desert turret's lines (FUN_005fd470 skill 0xcb: FUN_00645910 from
    // it, mask 0x805, no size) to each step's point and to the target.
    void desert_turret() {
        for (std::size_t step = 0; step < input.ray_clear.size(); ++step) {
            const auto& [step_x, step_y] = d2d::rules::kTurretStep[step];
            input.ray_clear[step] = !d2d::rules::sight_blocked(x, y, 0, x + step_x * input.aip[4], y + step_y * input.aip[4], 0, [this](int at_x, int at_y) { return wall805(at_x, at_y); });
        }
        input.usable = !d2d::rules::sight_blocked(x, y, 0, target_x, target_y, 0, [this](int at_x, int at_y) { return wall805(at_x, at_y); });
    }
    // A minion under Cry Help (AI command 1, FUN_0058f730): the unit it's sent
    // at, while that lives, is its target (the Minion think, command kind 1).
    void cry_help_search() {
        if (monster.ai_command != 1 || monster.command_foe < 0) return;
        if (const auto sent = std::ranges::find(foes, monster.command_foe, &Foe::id); sent != foes.end() && sent->alive) {
            target = &*sent;
            best = d2d::rules::ai_distance(int(std::floor(sent->x * 5)) - int(std::floor(unit.x * 5)), int(std::floor(sent->y * 5)) - int(std::floor(unit.y * 5)));
            monster.aware = true;
        }
    }
    // The catapult spotter's strike: its raw skill id in A1 at a point.
    bool catapult_spotter_skill() {
        if (act.act != d2d::rules::MonAct::skill) return false;
        use("A1", act.n);
        monster.skill_x = act.x; monster.skill_y = act.y; monster.at_point = true;
        return true;
    }
    void imp_aim() {
        aim_map_point();
        aim_kin();
    }
    // Wussie (FUN_005ee3c0; a5q2's prisoners): no target. Its cage's portal
    // open: unless one of its cage has gone through, 4 or more off it it
    // walks at it 70 % (FUN_005dec80 flags 7: one that can't set off has
    // sight forced, then walk_failed), else rand(100) < 10 wanders 4, else
    // stands 10; there (or after the first) it's gone through. Else a
    // player within 16: its door broken, the portal opens (FUN_005888d0),
    // still caged it cries (FUN_00588dd0; rand(1000) < 100 its help-me
    // line, RescueQuest::kHelpMe). Then it stands 25.
    // With its door broken it goes into list 8 (FUN_005b1990, while +0xd0
    // is 0xb: in none), so evil monsters can find it. The portal stands
    // still, so a walk at it is a spot move pathed as at a unit (near 1).
    bool wussie() {
        auto& view = monster.wussie;
        if (view.portal) {
            if (!view.cage_left && d2d::rules::ai_distance(view.portal_x - x, view.portal_y - y) >= 4) {   // FUN_005dc5c0
                if (monster.seed(100) < 70) {
                    unit.goal_x = (float(view.portal_x) + 0.5f) / 5; unit.goal_y = (float(view.portal_y) + 0.5f) / 5;
                    if (set_off(false, nullptr, true)) return true;
                    monster.force_sight = true;
                    const auto failed = d2d::rules::walk_failed(monster.seed);
                    if (failed.act == d2d::rules::MonAct::idle) idle(failed.n); else walk_to(failed.x, failed.y, false);
                    return true;
                }
                if (monster.seed(100) < 10) { const auto wander = d2d::rules::think_wander(monster.seed, 4); walk_to(wander.x, wander.y, false); return true; }
                idle(10);
                return true;
            }
            view.left = true;
            monster.gone = true;
            return true;
        }
        if (view.player) {
            if (view.door_broken) { view.open_portal = true; monster.listed = true; }
            else { view.cried = true; monster.help_sound = monster.seed(1000) < 100; }
        }
        idle(25);
        return true;
    }
    // BaalThrone (134, FUN_005ef320; AI control +0x14 the waves sent, +0x18
    // flags, +0x1c the frame of the next think): till then it stands. With
    // no other living monster within 64 subtiles (FUN_005dd0b0 kind 3):
    // flag 1 set, the next of five waves (FUN_005ef210: skill 286 in S3 at
    // 13 subtiles south, its Subject the wave's superunique 61..65; 100
    // frames on, flag 2, flag 1 off), or after the fifth he turns into
    // baalcrabstairs (FUN_00574370(0x22f), think BaalToStairs) and stands 5;
    // flag 1 unset, skill 285 (Baal Corpse Explode) in S3, flag 1, 250
    // frames on, and unit event 0x10 (FUN_00553380: the client plays its
    // Taunt). Before that cast the client is sent the next wave's class
    // (S->C 0xa4, FUN_00571c00: the superunique's +4; after fallenshaman5
    // and unraveler5 fallen5 and skmage_cold3 too) to load its art: d2d
    // loads a unit's when it comes. With others near: Skill1 (Decrepify) in
    // Sk1mode at the target, unless he's in its aurastate (+0x80, none) or
    // the target in its auratargetstate (+0x82), on seed(100) < aip1; else
    // stands 10. Its srvdo 141 (FUN_005d2e80): each corpse
    // within Param5 (40) + (lvl - 1) x Param6 (0) subtiles (FUN_0056dcc0,
    // flags 0x3002) goes up as Corpse Explosion (FUN_005c50b0 ->
    // FUN_005c4df0), whose damage is calc1..calc2 % of its life: the row
    // has neither, so it hurts no one (bugs.md); the corpses are spent.
    // The corpses are FUN_0056dcc0's (flags 0x3002, radius 40): baal_corpse_spent.
    // They're spent on S3's action frame (FUN_0056f7f0 runs the do-func).
    bool baal_throne() {
        if (frame < monster.ai_state3) { idle(monster.ai_state3 - frame); return true; }
        const bool others = std::ranges::any_of(pack, [&](const Monster& other) {
            return &other != &monster && other.alive() && other.align == 0 && d2d::rules::ai_distance(subtile(other.unit.x) - x, subtile(other.unit.y) - y) < 0x40; });
        if (others) {
            const int curse = skill_id(game_data, type_info.skill[0]);
            const auto* row = game_data.skills.get(curse);
            if (!target || !row || (!row->auratarget.empty() && std::ranges::contains(target->states, row->auratarget)) || monster.seed(100) >= per_difficulty.aip[0]) {
                idle(10);
                return true;
            }
            monster.chase = int(target - foes.data());
            unit.dir = direction16(target->x - unit.x, target->y - unit.y);
            monster.skill = curse;
            set_mode(game_data, monster, type_info.sk_mode[0], now_ms);
            monster.skill_x = subtile(target->x); monster.skill_y = subtile(target->y);
            return true;
        }
        if (monster.ai_state2 & 1) {
            if (monster.ai_state >= 5) { monster.baal_leaves = true; idle(5); return true; }
            monster.baal_wave = 61 + monster.ai_state;
            set_mode(game_data, monster, "S3", now_ms);
            monster.skill = -1;
            monster.next_act = monster.mode_until;                 // the cast plays out before the next think
            ++monster.ai_state;
            monster.ai_state3 = frame + 100;
            monster.ai_state2 = (monster.ai_state2 | 2) & ~1;
            return true;
        }
        set_mode(game_data, monster, "S3", now_ms);
        monster.skill = skill_id(game_data, "Baal Corpse Explode");   // 285
        monster.next_act = monster.mode_until;
        monster.ai_state2 |= 1;
        monster.ai_state3 = frame + 250;
        monster.taunt_sound = true;
        return true;
    }
    // BaalToStairs (138, FUN_005ef620): the Chamber's portal (object 563)
    // within 25 (FUN_005ef5e0), a straight walk to it (FUN_005deca0); within
    // aip1 (FUN_006416d0) he's gone in (FUN_0058e600, state 0x92). None:
    // stands 25.
    bool baal_to_stairs() {
        if (!monster.chamber_portal) { idle(25); return true; }
        const auto [portal_x, portal_y] = *monster.chamber_portal;
        const int dist = d2d::rules::ai_distance(portal_x - x, portal_y - y);
        if (dist >= 25) { idle(25); return true; }
        if (dist < per_difficulty.aip[0]) { monster.baal_in = true; monster.gone = true; return true; }
        walk_to(portal_x - x, portal_y - y, false);
        return true;
    }
};

constexpr auto kRows = std::to_array<AiRow>({
    { .ai_name = "SiegeBeast", .prep = &part_as<Act5Ai, &Act5Ai::siege_beast> },
    { .ai_name = "Minion", .search = &part_as<Act5Ai, &Act5Ai::cry_help_search>, .prep = &part_as<Act5Ai, &Act5Ai::minion> },
    { .ai_name = "Succubus", .prep = &part_as<Act5Ai, &Act5Ai::succubus> },
    { .ai_name = "SuccubusWitch", .prep = &part_as<Act5Ai, &Act5Ai::succubus> },
    { .ai_name = "Overseer", .prep = &part_as<Act5Ai, &Act5Ai::overseer> },
    { .ai_name = "MinionSpawner", .prep = &part_as<Act5Ai, &Act5Ai::minion_spawner> },
    { .ai_name = "SiegeTower", .prep = &part_as<Act5Ai, &Act5Ai::siege_tower> },
    { .ai_name = "Imp", .prep = &part_as<Act5Ai, &Act5Ai::imp>, .aim = &part_as<Act5Ai, &Act5Ai::imp_aim> },
    { .ai_name = "PutridDefiler", .prep = &part_as<Act5Ai, &Act5Ai::putrid_defiler>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_kin> },
    { .ai_name = "BaalTentacle", .prep = &part_as<ThinkCtx, &ThinkCtx::baal_rider> },
    { .ai_name = "BaalTaunt", .prep = &part_as<Act5Ai, &Act5Ai::baal_taunt> },
    { .ai_name = "Ancient", .thinks_alone = true, .prep = &part_as<Act5Ai, &Act5Ai::ancient> },
    { .ai_name = "Nihlathak", .thinks_alone = true, .prep = &part_as<Act5Ai, &Act5Ai::nihlathak>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_map_point> },
    { .ai_name = "ClawViperEx", .prep = &part_as<ThinkCtx, &ThinkCtx::charge_line> },
    { .ai_name = "Sarcophagus", .prep = &part_as<ThinkCtx, &ThinkCtx::nest> },
    { .ai_name = "Trap-RightArrow", .prep = &part_as<Act5Ai, &Act5Ai::right_arrow> },
    { .ai_name = "Trap-LeftArrow", .prep = &part_as<Act5Ai, &Act5Ai::left_arrow> },
    { .ai_name = "DesertTurret", .prep = &part_as<Act5Ai, &Act5Ai::desert_turret>, .aim = &part_as<ThinkCtx, &ThinkCtx::aim_ground> },
    { .ai_name = "NpcBarb", .thinks_alone = true },
    { .ai_name = "HellMeteor", .aim = &part_as<ThinkCtx, &ThinkCtx::aim_ground> },
    { .ai_name = "CatapultSpotter", .skill = &part_as<Act5Ai, &Act5Ai::catapult_spotter_skill> },
    { .ai_name = "Wussie", .think = &part_as<Act5Ai, &Act5Ai::wussie> },
    { .ai_name = "BaalThrone", .think = &part_as<Act5Ai, &Act5Ai::baal_throne> },
    { .ai_name = "BaalToStairs", .think = &part_as<Act5Ai, &Act5Ai::baal_to_stairs> },
});

}  // namespace

const std::span<const AiRow> kAct5Ais{ kRows };

}  // namespace d2d::game
