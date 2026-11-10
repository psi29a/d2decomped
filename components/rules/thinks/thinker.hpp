// SPDX-License-Identifier: GPL-3.0-or-later
// What every MonAI think shares (thinks.hpp): its inputs, the unit's seed,
// the back-off, and the acts the thinks build from them. Each act's thinks
// (act1.hpp .. act5.hpp) are members of a struct built on Thinker, a row
// each in that act's table: the MonAI table (0x73ca18 + AI x 16, the think
// at +8) by its MonStats AI name.
#pragma once

#include "monsters.hpp"
#include "rules.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>

namespace d2d::rules {

// `away(n, run)` backs off n subtiles from the target (FUN_005defe0 walking,
// FUN_005df140 running) and says whether it set off.
using ThinkAway = std::function<bool(int, bool)>;

struct Thinker {
    std::string_view ai_name;
    const ThinkIn& input;
    Rng& rng;
    const ThinkAway& away;
    const std::array<int, 8>& aip = input.aip;
    const Think idle2{ MonAct::idle, aip[1] }, walk{ MonAct::walk, 7 };
    const Think walk0{ MonAct::walk, 0 };
    const Think walk_in{ MonAct::walk, 0 };                                                       // FUN_005dec80(target, 0)
    bool set_off_unused = false;                        // FUN_005df7d0's answer: Act 4's thinks return either way

    int roll() { return rng(100); }
    Think a1_or_a2(int chance) { return Think{ roll() < chance ? MonAct::a1 : MonAct::a2 }; }
    // An act at the second target (FUN_005ddc30), a back-off from it.
    static Think at_second(Think out) { out.unit = ThinkUnit::second; return out; }
    bool away_second(int subtiles, bool running) {
        if (input.away_from) *input.away_from = ThinkUnit::second;
        const bool set_off = away(subtiles, running);
        if (input.away_from) *input.away_from = ThinkUnit::target;
        return set_off;
    }
    void effect(ThinkEffect::Kind kind, int id = 0, int value = 0) { if (input.effects) input.effects->push_back({ kind, id, value }); }
    void set_pace(int pct, int steps) { if (pct) *input.pace = pct; if (steps && input.steps) *input.steps = steps; }
    bool skill_set(int slot) { return slot < 3 ? input.skill[std::size_t(slot)] : input.skill45[std::size_t(slot - 3)]; }
    static Think cast(int slot, ThinkUnit unit = ThinkUnit::target) { Think out{ MonAct::skill, slot }; out.unit = unit; return out; }
    static Think aimed(Think out, ThinkUnit unit) { out.unit = unit; return out; }
    static Think aimed_at(Think out, ThinkUnit unit) { out.unit = unit; return out; }
    // A circle (FUN_005df7d0) says whether it set off, as `away` does.
    Think circle(int count, int flag, bool& set_off) { auto out = think_circle(rng, count); out.y = flag; set_off = away(count, false); return out; }
    static int squared(int from_x, int from_y, int to_x, int to_y) { return (from_x - to_x) * (from_x - to_x) + (from_y - to_y) * (from_y - to_y); }   // FUN_005b0bd0
    static Think keep_off(int most, int want) { return Think{ MonAct::keep, most, want, 2 }; }   // FUN_005de6d0 -> FUN_005de4e0(.., 2, ..)
    Think imp_teleport() {                                          // FUN_005df850: Skill1 at a point within a2 of itself
        const int radius = input.imp_aip[0][1];
        Think out = cast(0, ThinkUnit::none);
        out.x = rng(2 * radius) - radius + input.self_x;
        out.y = rng(2 * radius) - radius + input.self_y;
        return out;
    }
};

// A row of an act's table: the AI's name, its think.
using ThinkFn = Think (*)(std::string_view ai_name, const ThinkIn& input, Rng& rng, const ThinkAway& away);
struct ThinkRow {
    std::string_view ai_name;
    ThinkFn think;
};

// An act's think, member `act_think` of its struct, as a ThinkFn.
template <class Act, Think (Act::*act_think)()>
Think think_as(std::string_view ai_name, const ThinkIn& input, Rng& rng, const ThinkAway& away) {
    Act act{ { ai_name, input, rng, away } };
    return (act.*act_think)();
}

}  // namespace d2d::rules
