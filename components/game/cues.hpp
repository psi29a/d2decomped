// SPDX-License-Identifier: GPL-3.0-or-later
// World sounds due later (a monster's cry after its delay, an item landing),
// from a place. The World queues them; the client plays them when due
// (audio.hpp play_cues).
#pragma once

#include "gamedata.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::game {

// A sound's unit: D2's unit type (0 player, 1 monster, 2 object, 3 missile)
// and its id, never 0.
enum : int { kSoundOnPlayer = 0, kSoundOnMonster = 1, kSoundOnObject = 2, kSoundOnMissile = 3 };
constexpr std::uint64_t unit_key(int type, int id) { return std::uint64_t(type + 1) << 32 | std::uint32_t(id); }

struct Cues {
    const GameData* game_data = nullptr;
    // `owner`: a missile's sound id (Missile::sound): the sound stops when it's gone (a TravelSound's loop).
    // `unit`: the unit it plays on (unit_key), 0 for none: Defer / Stop Inst and a unit's one line
    // at a time go by it (rules/sound_start.hpp). `hush`: dropped while one of its unit's speech
    // lines plays (FUN_004b9e90: a player's get-hit cry, its class lines 0x13..0x18); kClassLine
    // also skips the last class line again within 75 client frames (repeat_line).
    enum : std::uint8_t { kHush = 1, kClassLine = 2 };
    struct Cue { std::uint32_t when_ms = 0; int sound = 0; float x = 0, y = 0; int owner = 0; std::uint64_t unit = 0; std::uint8_t hush = 0; };
    std::vector<Cue> due;
    // The last class line played and when (FUN_004cb9c0: DAT_007c88c8, DAT_007c88c4 in client
    // frames, FUN_0044db00). A line the hush drops returns before it's kept (0x4cbc6d), so only
    // one that plays counts: true for kClassLine's same line under 75 frames on, else kept.
    int last_line = 0;
    std::uint32_t last_line_ms = 0;
    bool repeat_line(const Cue& cue, std::uint32_t now_ms) {
        if (cue.hush != kClassLine) return false;
        if (cue.sound == last_line && now_ms - last_line_ms < 75 * 40) return true;
        last_line = cue.sound;
        last_line_ms = now_ms;
        return false;
    }
    void cue(int sound, std::uint32_t when_ms, float x, float y, int owner = 0) { if (sound > 0) due.push_back({ when_ms, sound, x, y, owner, 0 }); }
    void cue(std::string_view name, std::uint32_t when_ms, float x, float y) { cue_on(0, name, when_ms, x, y); }
    void cue_on(std::uint64_t unit, int sound, std::uint32_t when_ms, float x, float y, int owner = 0, std::uint8_t hush = 0) {
        if (sound > 0) due.push_back({ when_ms, sound, x, y, owner, unit, hush });
    }
    void cue_on(std::uint64_t unit, std::string_view name, std::uint32_t when_ms, float x, float y, std::uint8_t hush = 0) {
        if (const auto found = game_data->sound_index.find(std::string(name)); found != game_data->sound_index.end()) cue_on(unit, found->second, when_ms, x, y, 0, hush);
    }
};

}  // namespace d2d::game
