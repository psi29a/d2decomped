// SPDX-License-Identifier: GPL-3.0-or-later
// The bodies of sound_start.hpp's functions; their notes are in the header.
#include "sound_start.hpp"

#include "sound_mix.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace d2d::rules {

std::optional<std::size_t> compound_into(std::span<const SoundInstance> playing, const SoundRule& rule, std::int64_t now) {
    if (rule.compound == 0) return std::nullopt;
    for (std::size_t k = 0; k < playing.size(); ++k) {
        const auto& other = playing[k];
        if (other.group_base != rule.group_base || other.stopping) continue;
        // unsigned as game.exe compares (jbe): one not started yet never matches
        if (rule.compound < 0 || std::uint64_t(now - other.start_tick) <= std::uint64_t(rule.compound)) return k;
    }
    return std::nullopt;
}

SoundClash start_clash(std::span<const SoundInstance> playing, const SoundRule& rule, std::uint64_t unit, bool positioned) {
    SoundClash clash;
    if (rule.defer || rule.stop_inst) {   // the oldest of its group, on its unit when it has one (FUN_004b9690)
        for (std::size_t k = 0; k < playing.size(); ++k) {
            const auto& other = playing[k];
            if (other.group_base != rule.group_base || (unit != 0 && !other.plays_on(unit))) continue;
            if (!clash.other || other.start_tick < playing[*clash.other].start_tick) clash.other = k;
        }
    }
    if (unit != 0 && speech_row(rule.row)) {   // a unit's speech: any of its lines playing (FUN_004b9700)
        clash.other.reset();
        for (std::size_t k = 0; k < playing.size() && !clash.other; ++k)
            if (playing[k].plays_on(unit) && speech_row(playing[k].row)) clash.other = k;
    }
    if (rule.defer && unit == 0 && positioned) clash.other.reset();   // plays over whatever plays
    if (clash.other) {
        clash.stop_other = rule.stop_inst;
        clash.play = rule.stop_inst;
    }
    return clash;
}

SoundMix ducked_mix(int volume, int falloff, float screen_x, float screen_y2, int row, bool solo_row, int menu_percent, int solo_percent) {
    return sound_mix_pixels(ducked_volume(volume, row, solo_row, menu_percent, solo_percent), falloff, screen_x, screen_y2);
}

}
