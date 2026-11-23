// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of pets.hpp's functions; the older ones' notes are in the header, newer ones' above their definitions.
#include "pets.hpp"

#include "montypes.hpp"
#include "points.hpp"
#include "uniques.hpp"

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

// Clay Golem's slow (damagedinmelee, aura event func 27, FUN_005c6760):
// FUN_0057c6c0 raises it on the golem once a melee hit on it has landed
// (record +4 & 1, not & 0x20; 0x57c7a5..0x57c7f3), the striker the other
// unit. The golem's item_slow (stat 0x96, layer 0; Clay Golem's aurastat
// dm34) is held to 50 against a player (type 0) and a champion, unique or
// superunique (FUN_005a0180 mask 0xc), to 90 against another monster; other
// types and a 0 give none. Returns the slow, 0 for none: state 0x18 on the
// striker for 0x2ee frames with velocitypercent, attackrate and
// other_animrate at -slow (FUN_0056e970, FUN_006270b0 0x5c67cf..0x5c6840).
int clay_slow(int slow, int unit_type, bool special) noexcept {
    if (slow == 0 || (unit_type != 0 && unit_type != 1)) return 0;
    return std::min(slow, unit_type == 0 || special ? 50 : 90);
}

// item_slow's own event (ItemStatCost domeleedamage / domissiledamage, func 19,
// FUN_005c0310), on the unit it strikes: on gear's wearer, and on a summon whose
// aurastat it is (FUN_005c4470 0x5c4686..0x5c46f2: Clay Golem's), raised as its
// hit lands (FUN_0057c6c0 event 5, 0x57c7d8..0x57c7e3). Held to 50 against a
// player, and against a monster that is a champion, unique or superunique
// (`special`, +0x16 & 0xc), a MonStats boss (FUN_0063e9f0) or a hireling
// (FUN_0063ee90); to 75 against one with the superunique flag (+0x16 & 2)
// alone; to 90 against another monster; other types and a 0 give none
// (0x5c0347..0x5c03c9). The same state 0x18 as clay_slow's.
int item_slow(int slow, int unit_type, bool special, bool boss, bool hireling, bool superunique) noexcept {
    if (slow == 0 || (unit_type != 0 && unit_type != 1)) return 0;
    return std::min(slow, unit_type == 0 || special || boss || hireling ? 50 : superunique ? 75 : 90);
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
// FUN_0056e2f0(unit, 7): ai_swappable.
bool revive_takes(const MonType& type, const CorpseState& corpse) {
    return type.revive && (corpse.dying || corpse.mode == 0 || corpse.mode == 0xc) && corpse_raisable(type, corpse) && ai_swappable(type, corpse);
}

// FUN_0056e2f0 -> FUN_005dd480, the same for every argument its eight
// callers pass (7 Revive FUN_005c3300; 0xa or 0xc FUN_005c35c0; 0xa
// FUN_005d6520; 0xb Confuse FUN_005c3d50, Conversion FUN_005d02b0,
// FUN_005d72b0; 0xc FUN_005d8510; 0x13 FUN_005c31f0): not an imp
// (BaseId 0x1ec) attached (state 0x8f), not uninterruptable (state 0x36),
// FUN_00623470 (MonStats2 mWL, not a MonStats boss +0xc & 0x40, switchai
// +0xe & 1), targetable (+0xc4 & 4) or dead or dying (FUN_005541b0: +0xc6 & 1,
// mode 0 or 0xc), and monster data +0x16 & 0xa clear (0x5dd4ce, before the
// argument's FUN_005b0da0 test, which adds nothing to these): no champion,
// unique or superunique.
bool ai_swappable(const MonType& type, const CorpseState& unit) {
    const bool dying = unit.dying || unit.mode == 0 || unit.mode == 0xc;
    return !(type.base == 0x1ec && unit.attached) && !unit.uninterruptable && type.can_walk && !type.boss_column && type.switch_ai
        && (unit.targetable || dying) && (unit.flags & 0xa) == 0;
}

// FUN_005a48c0 makes a champion: FUN_005a0320 sets 8 (0x5a0375), then | 5
// (0x5a4905); FUN_005a4940 a unique: | 1 (0x5a4967), then FUN_005a0320. A
// superunique carries 2 (FUN_005a0180's other tests). Bits 1 / 4 / 0x10.. aren't
// read here.
int monster_data_flags(Boss boss) {
    return boss == Boss::champion ? 0xd : boss == Boss::unique ? 9 : boss == Boss::superunique ? 0xb : 0;
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
