// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of pets.hpp's functions; the older ones' notes are in the header, newer ones' above their definitions.
#include "pets.hpp"

#include "montypes.hpp"
#include "rules.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace d2d::rules {

int run_percent(int walk_velocity, int run_velocity) {
    return walk_velocity > 0 ? run_velocity * 100 / walk_velocity - 100 : 0;
}

int heal_to_max(int& life, int max_life, int amount) {   // FUN_005c5f10: what it took
    if (amount <= 0) return 0;
    int healed = int(std::uint32_t(life) + std::uint32_t(amount));
    if (healed > max_life) { amount += max_life - healed; healed = max_life; }
    life = healed;
    return amount;
}

GolemSteal blood_golem_steal(bool monster, int drain, bool evil, int phys_fine, int target_life, int steal_pct, int owner_pct,
                             bool owner, GolemSteal lives, int owner_max, int golem_max) {
    if ((monster && (drain & 0xff) == 0) || !evil) return lives;
    int amount = monster && (drain & 0xff) != 100 ? mul_div(phys_fine, drain & 0xff, 100) : phys_fine;
    if (amount >= target_life) amount = target_life;
    if (steal_pct <= 0) return lives;
    int heal = mul_div(amount, steal_pct, 100);
    if (heal <= 0) return lives;
    if (owner && owner_pct > 0) heal -= heal_to_max(lives.owner_life, owner_max, mul_div(heal, owner_pct, 100));
    heal -= heal_to_max(lives.golem_life, golem_max, heal);
    if (owner && heal > 0) heal_to_max(lives.owner_life, owner_max, heal);
    return lives;
}

// The corpses the Necromancer's corpse skills take, by their server start
// functions on the target. Monster data +0x16: a champion's 0xd (FUN_005a48c0:
// FUN_005a0320 sets 8 at 0x5a0375, then | 5 at 0x5a4905), a unique's 9
// (FUN_005a4940: 1 at 0x5a4967, then FUN_005a0320).
namespace {

bool corpse_raisable(const MonType& type, const CorpseState& corpse) {
    return corpse.mode == 0xc && !corpse.hidden && type.corpse_sel && type.velocity != 0;
}

}  // namespace

// Raise Skeleton / Skeletal Mage (srvstfunc 15, LAB_005c3070): not in a town
// (FUN_0061ab00), and FUN_00645510: a monster in mode 0xc (DD), not hidden
// (FUN_0063a770), MonStats2 corpseSel (FUN_004638a0 bit 7), MonStats
// Velocity (+0x32) not 0.
bool raise_takes(const MonType& type, const CorpseState& corpse) { return !corpse.town && corpse_raisable(type, corpse); }
// Corpse / Poison Explosion (srvstfunc 17, LAB_005c31c0): not in a town, and
// FUN_00645680: mode 0xc, not hidden, corpseSel.
bool blast_takes(const MonType& type, const CorpseState& corpse) {
    return !corpse.town && corpse.mode == 0xc && !corpse.hidden && type.corpse_sel;
}
// Revive (srvstfunc 21, LAB_005c3350 -> FUN_005c3300; a town too): MonStats2
// revive (bit 8), dead or dying (FUN_005541b0), FUN_00645510, and
// FUN_0056e2f0 -> FUN_005dd480: not an imp (BaseId 0x1ec) attached (state
// 0x8f), not uninterruptable (state 0x36), MonStats2 mWL, not a MonStats boss
// (+0xc & 0x40), switchai (+0xe & 1), targetable or dying, not a champion,
// unique or superunique (+0x16 & 0xa).
bool revive_takes(const MonType& type, const CorpseState& corpse) {
    const bool dying = corpse.dying || corpse.mode == 0 || corpse.mode == 0xc;
    return type.revive && dying && corpse_raisable(type, corpse) && !(type.base == 0x1ec && corpse.attached) && !corpse.uninterruptable && type.can_walk
        && !type.boss_column && type.switch_ai && (corpse.targetable || dying) && (corpse.flags & 0xa) == 0;
}

BoneWallMakers bone_wall_makers(int caster_x, int caster_y, int piece_x, int piece_y, int calc2, bool town) {
    const int count = calc2 / 2;
    if (town || count <= 1) return {};
    int across = caster_x - piece_x;
    const int down = caster_y - piece_y;
    if (across == 0 && down == 0) across = 1;
    return { count, { std::pair{ piece_x - down, piece_y + across }, std::pair{ piece_x + down, piece_y - across } } };
}

bool traced_pet_ai(std::string_view ai_name) {
    static constexpr std::array<std::string_view, 11> kTraced{ "Hydra", "AssassinSentry", "DeathSentry", "Raven", "DruidWolf", "Totem", "Vines", "CycleOfLife", "DruidBear",
                                                               "ShadowWarrior", "ShadowMaster" };
    return std::ranges::contains(kTraced, ai_name);
}

}
