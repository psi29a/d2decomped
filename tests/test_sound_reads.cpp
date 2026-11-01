// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Sounds.txt lookup order (apps/d2d/sound_reads.hpp). No game files.
#include "sound_reads.hpp"

#include <cassert>

int main() {
    using d2d::client::sound_reads;
    const auto den = sound_reads(R"(act1\denofevilaction.wav)", false);
    assert(den.size() == 3);
    assert(!den[0].from_music && den[0].path == R"(data\global\sfx\act1\denofevilaction.wav)");
    assert(!den[1].from_music && den[1].path == R"(data\local\sfx\act1\denofevilaction.wav)");
    assert(den[2].from_music && den[2].path == R"(data\global\music\act1\denofevilaction.wav)");

    const auto song = sound_reads(R"(act1\wild.wav)", true);
    assert(song.size() == 4);
    assert(!song[0].from_music && song[0].path == R"(data\global\music\act1\wild.wav)");
    assert(song[1].from_music && song[1].path == R"(data\global\music\act1\wild.wav)");
    assert(!song[2].from_music && song[2].path == R"(data\global\sfx\act1\wild.wav)");
    assert(!song[3].from_music && song[3].path == R"(data\local\sfx\act1\wild.wav)");
    return 0;
}
