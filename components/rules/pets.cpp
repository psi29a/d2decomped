// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of pets.hpp's functions; their notes are in the header.
#include "pets.hpp"

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
