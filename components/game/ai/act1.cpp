// SPDX-License-Identifier: GPL-3.0-or-later
// Act 1's think inputs (rules/thinks/act1.hpp reads them).
#include "ai.hpp"

#include "internal.hpp"

#include <monsters.hpp>
#include <uniques.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace d2d::game {

namespace {

using d2d::rules::Boss;

struct Act1Preps : ThinkPrep {
    // The Fallen run while a unit dies near (ThinkIn::dying), crying once as they start.
    void fallen() {
        if (!input.dying) monster.fleeing = false;
        else if (!monster.fleeing) { monster.fleeing = true; monster.flee_sound = true; }
    }
    // A Shaman's corpse (FUN_005dd0b0, the last found): a unique's
    // (FUN_005a0180 mask 8, not 4; FUN_005f1380) any Fallen's or Shaman's
    // not unique; else its own group's (FUN_005dcda0) within aip4 (squared,
    // FUN_005dc380). Either way in DD, not yet gone. A unique's (FUN_005f1440) walks the
    // near rooms (FUN_005dd0b0 mode 1: near_order), the rest its group
    // (mode 9: FUN_005dd050 -> FUN_0058f380 on FUN_005dcda0).
    // The group list runs newest first (FUN_0058f100 adds at its head, as
    // each member is made), so its last match is the first in pack order.
    // ponytail: the selectable flag (+0xc4 bit 1) and the udead states but shatter (freeze 1 on a corpse) aren't kept.
    void fallen_shaman() {
        auto unique = [](const Monster& other) { return other.boss == Boss::unique || other.boss == Boss::superunique; };
        const auto corpse_for = [&](const Monster& other) {
            if (!other.usable_corpse()) return false;
            const int off_x = subtile(other.unit.x) - x, off_y = subtile(other.unit.y) - y;
            const int base = game_data.monsters.types[std::size_t(other.type)].base;
            return unique(monster) ? d2d::rules::shaman_corpse_reach(base, other.boss == Boss::champion, unique(other), off_x, off_y, input.aip[3])
                                   : other.leader == int(self) && off_x * off_x + off_y * off_y <= input.aip[3] * input.aip[3];
        };
        if (unique(monster)) {
            if (const auto order = near_order(corpse_for); !order.empty()) corpse = order.back();
        } else
            for (std::size_t i = 0; i < pack.size() && corpse < 0; ++i)
                if (corpse_for(pack[i])) corpse = int(i);
        input.corpse = corpse >= 0;
        corpse_place();
    }
    // A nest's init, taken at its first think: the frame.
    void nest() {
        if (monster.ai_state == 0) monster.ai_state = frame;
    }
    void blood_raven() {      // her think's two distances to home are FUN_005dc480's: each axis less half the unit's size + 1
        input.home_dist = d2d::rules::point_apart(subtile(monster.home_x) - x, subtile(monster.home_y) - y, (type_info.size >> 1) + 1);
        input.target_home_dist = d2d::rules::point_apart(subtile(monster.home_x) - target_x, subtile(monster.home_y) - target_y, (target->size >> 1) + 1);
    }
    // A fetish's 0xe: the corpse its shaman raises, while that unit is
    // still about (FUN_005f53e0: FUN_00552f60 finds it by id; dead or raised).
    void fetish() {
        if (*input.command == 14)
            if (const auto raised = std::ranges::find_if(pack, [&](const Monster& other) { return other.id == monster.command_corpse && !other.gone; }); raised != pack.end()) {
                input.command_unit = true;
                kin = int(raised - pack.begin());
            }
    }
};

constexpr auto kRows = std::to_array<PrepRow>({
    { "Fallen", &prep_as<Act1Preps, &Act1Preps::fallen> },
    { "FallenShaman", &prep_as<Act1Preps, &Act1Preps::fallen_shaman> },
    { "FoulCrowNest", &prep_as<Act1Preps, &Act1Preps::nest> },
    { "BloodRaven", &prep_as<Act1Preps, &Act1Preps::blood_raven> },
    { "GargoyleTrap", &prep_as<Act1Preps, &Act1Preps::nest> },
    { "Fetish", &prep_as<Act1Preps, &Act1Preps::fetish> },
});

}  // namespace

const std::span<const PrepRow> kAct1Preps{ kRows };

}  // namespace d2d::game
