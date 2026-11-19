// SPDX-License-Identifier: GPL-3.0-or-later
// iOS document picker for the user's own MPQ folder. UIKit stays in import.mm.
#pragma once

#include <filesystem>

namespace d2d::ios {

// When `dest` already contains d2data.mpq, write `data` and `patch` into
// `cfg_file` without opening the picker (sim reinstalls keep Documents/gamedata).
bool adopt_game_data_if_present(const std::filesystem::path& dest, const std::filesystem::path& cfg_file);

// Copy a folder the user picks into `dest`. Require d2data.mpq. Write `data`
// and, when present, `patch` (patch_d2.mpq or LODPatch_114d.exe) into `cfg_file`.
// False when the user cancels, the copy fails, or d2data.mpq is not in the folder.
bool import_game_data(const std::filesystem::path& dest, const std::filesystem::path& cfg_file);

}  // namespace d2d::ios
