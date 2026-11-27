// SPDX-License-Identifier: GPL-3.0-or-later
// corpse_select.cpp: Skills.txt SelectProc at the client cursor hit test.
#include "corpse_select.hpp"

#include <ai/units.hpp>
#include <skills.hpp>
#include <uniques.hpp>

namespace d2d::client {

// FUN_00464310 runs the left then right skill's SelectProc (Skills.txt, record
// +0x15) through FUN_004642b0; a proc of 0 passes nothing. FUN_00466870 then
// takes a unit whose +0xc4 bit 1 (targetable) is set, which a corpse's isn't,
// so a corpse needs a passing proc and its corpse flags (usable_corpse).
bool corpse_cursor_takes(const d2d::rules::Skill* left, const d2d::rules::Skill* right,
                         const d2d::game::Monster& corpse,
                         const d2d::rules::MonType& type, bool town) {
    if (!corpse.usable_corpse()) return false;
    const auto proc_takes = [&](const d2d::rules::Skill* skill) {
        const int proc = skill ? skill->select_proc : 0;
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
    return proc_takes(left) || proc_takes(right);
}

}  // namespace d2d::client
