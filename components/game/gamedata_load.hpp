// SPDX-License-Identifier: GPL-3.0-or-later
// Loading GameData from the MPQs: the strings, the tables the rules read,
// Act 1 laid out from the map seed, the camp and its units. Nothing here
// draws: the client adds its sprites on top (apps/d2d load.hpp).
#pragma once

#include "gamedata.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace d2d::game {

// The MPQs in data_dir (the 1.14d patch: patch_d2.mpq, else the
// LODPatch_114d.exe installer at patch_installer or next to the MPQs),
// every table and string, and Act 1 for map_seed. nullopt: no d2data.mpq
// there, or a file failed to load (logged).
// tile_pixels: decode the levels' tile graphics (GameData::tile_pixels).
std::optional<GameData> load_game_data(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed,
                                       bool tile_pixels = false);
// load_game_data in steps, so the menus can come up first: the MPQs and the
// strings; then every table; then Act 1 for map_seed with its units,
// monsters and skills (after the tables: a save's seed needs them read).
std::optional<GameData> open_game_data(const fs::path& data_dir, const fs::path& patch_installer, bool tile_pixels = false);
void load_game_tables(GameData& data, const d2d::mpq::Stack& mpqs);
void load_game_world(GameData& data, d2d::mpq::Stack& mpqs, std::uint32_t map_seed);

// A game on another map seed: act 1 laid out again, the camp rebuilt with
// its units, the other levels dropped (they build again when wanted). The
// World and its Town must enter afterwards; nothing may point into the
// old levels.
void set_map_seed(GameData& data, std::uint32_t seed);

}  // namespace d2d::game
