// SPDX-License-Identifier: GPL-3.0-or-later
// Sounds.txt rows (Index) the code plays by number, by their Sound name;
// the rest are looked up by name (GameData::sound_index).
#pragma once

namespace d2d::rules::sound_ids {

inline constexpr int kCursorPass = 1;          // a menu choice or slider moves
inline constexpr int kCursorSelect = 2;        // a menu row taken
inline constexpr int kCursorButtonClick = 4;   // a HUD / mini-panel button pressed
inline constexpr int kCursorLevelUp = 7;       // S→C 0x2C event 2
inline constexpr int kCursorQuestDone = 14;
inline constexpr int kSceneRain = 64;

}  // namespace d2d::rules::sound_ids
