// SPDX-License-Identifier: GPL-3.0-or-later
// corpse_select.cpp: Skills.txt SelectProc at the client cursor hit test.
#include "corpse_select.hpp"

#include <ai/units.hpp>
#include <skills.hpp>
#include <uniques.hpp>

namespace d2d::client {

// FUN_00464310 runs the left then right skill's SelectProc through
// FUN_004642b0. Stock Skills rows select the proc by id below. A zero proc
// uses the unit's ordinary corpse flags, represented by usable_corpse.
bool corpse_cursor_takes(const d2d::rules::Skill* left, const d2d::rules::Skill* right,
                         const d2d::game::Monster& corpse,
                         const d2d::rules::MonType& type, bool town) {
    if (!corpse.usable_corpse()) return false;
    const auto select_proc = [](const d2d::rules::Skill* skill) {
        if (!skill) return 0;
        switch (skill->id) {
            case 70: return 2;   // Raise Skeleton
            case 74: return 1;   // Corpse Explosion
            case 80: return 2;   // Raise Skeletal Mage
            case 83: return 1;   // Poison Explosion
            case 95: return 3;   // Revive
            case 131: return 4;
            case 142: return 5;
            case 150: return 6;
            case 351: return 1;
            default: return 0;
        }
    };
    const auto proc_takes = [&](int proc) {
        const bool corpse_sel = !town && type.corpse_sel;
        if (proc == 1 || proc == 5) return corpse_sel;
        if (proc == 2) return corpse_sel && type.velocity != 0;
        if (proc == 3)
            return !town && type.corpse_sel && type.velocity != 0 && type.can_walk
                && !type.boss_column && type.switch_ai
                && corpse.boss != d2d::rules::Boss::champion
                && corpse.boss != d2d::rules::Boss::unique
                && corpse.boss != d2d::rules::Boss::superunique;
        // Procs 4 and 6 additionally need MonStats2 soft, which d2d does not
        // retain; keep their traced common gate and narrow the caller's gap.
        if (proc == 4 || proc == 6) return corpse_sel;
        return false;
    };
    const int left_proc = select_proc(left);
    const int right_proc = select_proc(right);
    if (left_proc == 0 && right_proc == 0) return true;
    return proc_takes(left_proc) || proc_takes(right_proc);
}

}  // namespace d2d::client
