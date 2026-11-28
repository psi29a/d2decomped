// SPDX-License-Identifier: GPL-3.0-or-later
#include "fight.hpp"

#include "gamedata.hpp"

#include <skills.hpp>

#include <cstdint>

namespace d2d::game {

// Optional direct-input policy, not a game.exe parity claim. The ordinary
// start retains mana, readiness, equipment and animation checks.
bool Fight::start_direct_swing(std::uint32_t now_ms) {
    if (actor->pmode >= 0 || dead() || !start_swing(now_ms)) return false;
    actor->direct_swing = true;
    const auto* skill = game_data->skills.get(actor->swing_skill);
    if (skill && moving_skill(*skill)) {
        actor->smove.at_point = true;
        if (skill->srvdofunc == d2d::rules::ServerDoFunction::kCharge && actor->smove.active)
            actor->pmode_until = ~0u;
    }
    return true;
}

} // namespace d2d::game
