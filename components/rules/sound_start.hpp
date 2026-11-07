// SPDX-License-Identifier: GPL-3.0-or-later
// Whether a sound starts against the ones playing, and the two volume ducks:
// game.exe's FUN_004b9a00 (Compound), FUN_004ba020 at 0x4ba47a (Defer / Stop
// Inst, a unit's speech) and 0x4ba68f (Solo, the game menu). Times are sound
// ticks (DAT_007bc9bc, 40 ms). docs/research/re/sound.md "Starting a sound".
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

#include <sound_mix.hpp>

namespace d2d::rules {

// One playing sound, as the start rules see it.
struct SoundInstance {
    int row = 0;                    // its Sounds.txt row
    int group_base = 0;             // its group's first row (record +0x60)
    std::uint64_t unit = 0;         // the unit it plays on, 0 for none (instance +0x28)
    std::int64_t start_tick = 0;    // instance +0x34
    bool stopping = false;          // told to stop (instance +0x3d)
    // The units a Compound merge added (the list at +0x28 past the first,
    // FUN_004b9760): it counts as playing on them too.
    std::span<const std::uint64_t> merged{};
    // Playing on `on` (FUN_005169d0: a walk of the list).
    [[nodiscard]] bool plays_on(std::uint64_t on) const { return unit == on || std::ranges::find(merged, on) != merged.end(); }
};

// The row being started.
struct SoundRule {
    int row = 0, group_base = 0;
    int compound = 0;               // record +0x45: ticks, < 0 any age
    bool defer = false, stop_inst = false;
};

// Speech rows: one line at a time on a unit (FUN_004b9700).
constexpr bool speech_row(int row) { return row >= 0xb76 && row <= 0x1230; }

// Compound: a live instance of the same group started at most `compound`
// ticks ago (any, when < 0) takes the play instead of a new one
// (FUN_004b9760). Its index, if any.
inline std::optional<std::size_t> compound_into(std::span<const SoundInstance> playing, const SoundRule& rule, std::int64_t now) {
    if (rule.compound == 0) return std::nullopt;
    for (std::size_t k = 0; k < playing.size(); ++k) {
        const auto& other = playing[k];
        if (other.group_base != rule.group_base || other.stopping) continue;
        // unsigned as game.exe compares (jbe): one not started yet never matches
        if (rule.compound < 0 || std::uint64_t(now - other.start_tick) <= std::uint64_t(rule.compound)) return k;
    }
    return std::nullopt;
}

// At the start: the playing instance this one clashes with, and whether the
// new one plays (Stop Inst: the old one stops) or is dropped.
struct SoundClash {
    std::optional<std::size_t> other;   // the clashing instance
    bool play = true;                   // false: the new one is dropped
    bool stop_other = false;
};
inline SoundClash start_clash(std::span<const SoundInstance> playing, const SoundRule& rule, std::uint64_t unit, bool positioned) {
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

// The ducks, percent of a sound's volume, stepped once a tick.
// Solo (DAT_00727568): 2 down to 70 while a Solo row plays and isn't fading
// out, else 2 up to 100; scales every non-Solo row.
constexpr int solo_duck_step(int percent, bool solo_playing) {
    return solo_playing ? std::max(percent - 2, 70) : std::min(percent + 2, 100);
}
// The game menu (DAT_00727564): 5 down to 0 while a local game is paused
// under it, else 5 up to 100; scales every row but the menu-exempt ones.
constexpr int menu_duck_step(int percent, bool paused) {
    return paused ? std::max(percent - 5, 0) : std::min(percent + 5, 100);
}
// Rows the menu doesn't fade (0x4dfcc9): cursor 1..0xf, the scene_*
// ambience 0x34..0x47, music 0x1231..0x125a.
constexpr bool menu_exempt(int row) {
    return (row >= 1 && row <= 0xf) || (row >= 0x34 && row <= 0x47) || (row >= 0x1231 && row <= 0x125a);
}
// The sound ticks a client frame runs (FUN_00482c20: no clock of its own,
// DAT_007bc9bc + 1 a call): one each 40 ms frame in play, at most 100 to
// catch up; under the paused game menu one each pass of the message loop
// (menu.md "The pause"), so one each frame, however short.
constexpr std::uint64_t sound_ticks_due(std::uint64_t elapsed_ms, bool paused) {
    return paused ? 1 : std::min<std::uint64_t>(elapsed_ms / 40, 100);
}
// A sound's 0..255 volume under the master slider (0x4dfca1, FUN_00514cc0:
// whole percent, truncated), before the ducks. Nothing else takes the master:
// a cinematic's sound (Bink) plays at its own level.
constexpr int master_scaled(int volume, int master_percent) { return volume * master_percent / 100; }
// A sound's 0..255 volume under the ducks (0x4dfcb2 / 0x4dfd0b: integer percent).
constexpr int ducked_volume(int volume, int row, bool solo_row, int menu_percent, int solo_percent) {
    if (menu_percent != 100 && !menu_exempt(row)) volume = menu_percent * volume / 100;
    if (solo_percent != 100 && !solo_row) volume = solo_percent * volume / 100;
    return volume;
}
// A world sound's mix under the ducks, in FUN_004dfc20's order: the row's
// 0..255 volume is ducked first (whole percent steps, each truncated), and
// the distance and pan mix (sound_mix_pixels) works on what's left.
inline SoundMix ducked_mix(int volume, int falloff, float screen_x, float screen_y2, int row, bool solo_row, int menu_percent, int solo_percent) {
    return sound_mix_pixels(ducked_volume(volume, row, solo_row, menu_percent, solo_percent), falloff, screen_x, screen_y2);
}

}  // namespace d2d::rules
