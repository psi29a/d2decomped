// SPDX-License-Identifier: GPL-3.0-or-later
// Where a Sounds.txt file is looked up. Music Vol rows start in the music
// archive (docs/research/re/sound.md). A name the sfx folders do not have
// is tried there too: the Den's jingle is Music Vol off and lives only in
// d2music.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace d2d::client {

// `from_music`: the d2music / d2xmusic stack. Otherwise the game's stack.
struct SoundRead {
    bool from_music = false;
    std::string path;
};

[[nodiscard]] inline std::vector<SoundRead> sound_reads(std::string_view file, bool music_vol) {
    const std::string music = std::string(R"(data\global\music\)") + std::string(file);
    const std::string effects = std::string(R"(data\global\sfx\)") + std::string(file);
    const std::string speech = std::string(R"(data\local\sfx\)") + std::string(file);
    std::vector<SoundRead> reads;
    if (music_vol) {
        reads.push_back({ false, music });
        reads.push_back({ true, music });
    }
    reads.push_back({ false, effects });
    reads.push_back({ false, speech });
    if (!music_vol) reads.push_back({ true, music });
    return reads;
}

}  // namespace d2d::client
