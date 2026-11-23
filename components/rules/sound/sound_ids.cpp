// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of sound_ids.hpp's functions, with the research behind each.
#include "sound_ids.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace d2d::rules::sound_ids {

int hit_class(std::string_view code) {
    for (std::size_t i = 1; i < kHitClassCodes.size(); ++i) if (code == kHitClassCodes[i]) return int(i);
    return 0;
}

const Greeting* greeting_of(int monster_class) {
    const auto found = std::ranges::find(kGreetings, monster_class, &Greeting::monster_class);
    return found != kGreetings.end() ? &*found : nullptr;
}

// A footstep's row (FUN_004caeb0): its first, + 4 a floor material past dirt
// (1 dirt, 2 indoor stone, 3 outdoor stone, 4 sand, 5 snow, 6 wood), + 24 running.
int footstep(int first, int material, bool running) {
    return first <= 0 ? 0 : first + 4 * (std::clamp(material, 1, 6) - 1) + (running ? 24 : 0);
}

// A floor tile's material by its DT1 material flags (FUN_004cad60), 0 none:
// the level's SoundEnviron Material 1 then (FUN_004cadb0).
int floor_material(unsigned flags) {
    return flags & 0x20 ? 1 : flags & 0x08 ? 2 : flags & 0x10 ? 3 : flags & 0x40 ? 4 : flags & 0x80 ? 6 : flags & 0x400 ? 5 : 0;
}

// A step plays on the tick whose animation position `pos` (256ths of a frame,
// `rate` a tick) is nearer a footfall than the ticks either side (FUN_004caf60):
// footfalls `offset` + k x `length` / `count` apart, distances round the
// period (FUN_004e4180).
int around(int from, int point, int period) {
    if ((period & (period - 1)) == 0) { from &= period - 1; point &= period - 1; }
    else { from %= period; point %= period; }
    const int apart = point - from;
    return std::min({ std::abs(apart), std::abs(apart - period), std::abs(-apart - period) });
}

bool footfall(int pos, int rate, int length, int count, int offset) {
    if (rate <= 0 || count <= 0 || length / count <= 0) return false;
    const int period = length / count, here = around(pos, offset, period);
    return here < around(pos + rate, offset, period) && here < around(pos - rate, offset, period);
}

Impact impact(int hit_class, bool dying) {
    Impact out;
    switch (hit_class & 0xf) {
    case 1: out.impact = 321; break;                   // impact_punch
    case 2: case 3: case 4: case 5: out.impact = 341; break;   // impact_blade_swing
    case 6: case 7: out.impact = 335; break;           // impact_blade_thrust
    case 8: case 9: out.impact = 329; break;           // impact_blunt
    case 10: case 11: out.impact = 347; break;         // impact_arrow
    case 12: out.impact = 325; break;                  // impact_claw
    default: break;
    }
    switch (hit_class & 0xf0) {
    case 0x10: if (out.impact) out.element = 0x17b; break;
    case 0x20: out.element = out.impact ? 0x164 : 0x161; break;   // impact_fire_layer / impact_fire
    case 0x30: if (!out.impact) out.element = 0x167; else if (!dying) out.element = 0x16a; break;
    case 0x40: out.element = out.impact ? 0x174 : 0x170; break;
    case 0x50: if (!out.impact || !dying) out.element = 0x178; break;
    case 0x60: out.element = 0x17e; break;
    case 0x70: out.element = 0x18b; break;
    case 0x80: out.element = 0x956; break;
    case 0xb0: out.element = 0x187; break;
    default: break;
    }
    return out;
}

int block(int hit_class) {
    const int low = hit_class & 0xf;
    return low >= 1 && low <= 9 ? kBlockWeapon : low == 10 || low == 11 ? kBlockArrow : 0;
}

std::array<StartCue, 3> skill_start(const SkillStart& skill, int unit_type, int unit_class, int hit_class, int rate, bool from_item, bool refused) {
    std::array<StartCue, 3> cues{};
    if (refused) return cues;
    const auto& swing = kSwing[std::size_t(hit_class & 0xf) < kSwing.size() ? std::size_t(hit_class & 0xf) : 0];
    const int swing_ticks = (swing.frames * 256 + 128) / (rate != 0 ? rate : 1);
    const int held = skill.delay ? swing_ticks - 3 : 0;
    const bool item = from_item && skill.item_cast_sound > 0;
    if (const int sound = item ? skill.item_cast_sound : skill.st_sound; sound > 0) cues[0] = { sound, held };
    if (skill.weapon && swing.sound != 0) cues[1] = { swing.sound, swing_ticks };
    if (!item && skill.st_sound_class > 0 && unit_type == 0 && unit_class == skill.char_class) cues[2] = { skill.st_sound_class, held };
    return cues;
}

std::optional<int> charge_up(const ChargeUp& skill) {
    if (skill.untargeted) return std::nullopt;
    const bool counted = skill.progressive && skill.state >= 0 && skill.state < skill.states && skill.stat >= 0 && skill.stat < skill.stats && skill.listed;
    return skill.prg_sound + (counted ? std::clamp(skill.count, 1, 3) - 1 : 0);
}



}
