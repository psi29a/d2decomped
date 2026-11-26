// SPDX-License-Identifier: GPL-3.0-or-later
// The client cursor test for a SearchEnemyNear corpse candidate.
#pragma once

namespace d2d::game {
struct Monster;
}
namespace d2d::rules {
struct MonType;
struct Skill;
}

namespace d2d::client {

// Whether either active hand's SelectProc, or the ordinary corpse flags when
// neither has one, lets the cursor take this corpse.
bool corpse_cursor_takes(const d2d::rules::Skill* left, const d2d::rules::Skill* right,
                         const d2d::game::Monster& corpse,
                         const d2d::rules::MonType& type, bool town);

}  // namespace d2d::client
