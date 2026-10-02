// SPDX-License-Identifier: GPL-3.0-or-later
// The game's base: the `fs` alias and the iso geometry. Every file
// includes what else it uses itself.
#pragma once

#include <filesystem>   // IWYU pragma: keep (namespace fs)

namespace d2d::game {

namespace fs = std::filesystem;

// D2 iso-diamond tile dimensions. Each cell footprint = 160x80; each
// step in x moves (+80, +40) on screen, each step in y moves (-80, +40).
// unverified (source: OpenDiablo2's mapengine): the projection; the data
// agrees (DT1 floor tiles are 160x80 diamonds and the camp's tiles meet).
constexpr int kIsoW = 160;
constexpr int kIsoH = 80;

}  // namespace d2d::game
