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

struct Cues {
    const GameData* game_data = nullptr;
    struct Cue { std::uint32_t when_ms = 0; int sound = 0; float x = 0, y = 0; };
    std::vector<Cue> due;
    void cue(int sound, std::uint32_t when_ms, float x, float y) { if (sound > 0) due.push_back({ when_ms, sound, x, y }); }
    void cue(std::string_view name, std::uint32_t when_ms, float x, float y) {
        if (const auto found = game_data->sound_index.find(std::string(name)); found != game_data->sound_index.end()) cue(found->second, when_ms, x, y);
    }
};

}  // namespace d2d::game
