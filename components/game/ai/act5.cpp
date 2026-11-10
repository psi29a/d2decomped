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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace d2d::game {

namespace {

struct Act5Preps : ThinkPrep {
    // The Minion's command's unit (its in reach by melee) and its last
    // frame: only Cry Help sends a minion one.
    void minion() {
        if (!cried_at) return;
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
};

constexpr auto kRows = std::to_array<PrepRow>({
    { "SiegeBeast", &prep_as<Act5Preps, &Act5Preps::siege_beast> },
    { "Minion", &prep_as<Act5Preps, &Act5Preps::minion> },
    { "Succubus", &prep_as<Act5Preps, &Act5Preps::succubus> },
    { "SuccubusWitch", &prep_as<Act5Preps, &Act5Preps::succubus> },
    { "Overseer", &prep_as<Act5Preps, &Act5Preps::overseer> },
    { "MinionSpawner", &prep_as<Act5Preps, &Act5Preps::minion_spawner> },
    { "SiegeTower", &prep_as<Act5Preps, &Act5Preps::siege_tower> },
    { "Imp", &prep_as<Act5Preps, &Act5Preps::imp> },
    { "PutridDefiler", &prep_as<Act5Preps, &Act5Preps::putrid_defiler> },
    { "BaalTentacle", &prep_as<ThinkPrep, &ThinkPrep::baal_rider> },
    { "BaalTaunt", &prep_as<Act5Preps, &Act5Preps::baal_taunt> },
    { "Ancient", &prep_as<Act5Preps, &Act5Preps::ancient> },
    { "Nihlathak", &prep_as<Act5Preps, &Act5Preps::nihlathak> },
    { "ClawViperEx", &prep_as<ThinkPrep, &ThinkPrep::charge_line> },
    { "Sarcophagus", &prep_as<ThinkPrep, &ThinkPrep::nest> },
    { "Trap-RightArrow", &prep_as<Act5Preps, &Act5Preps::right_arrow> },
    { "Trap-LeftArrow", &prep_as<Act5Preps, &Act5Preps::left_arrow> },
    { "DesertTurret", &prep_as<Act5Preps, &Act5Preps::desert_turret> },
});

}  // namespace

const std::span<const PrepRow> kAct5Preps{ kRows };

}  // namespace d2d::game
