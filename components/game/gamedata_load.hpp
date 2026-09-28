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

// A game on another map seed: act 1 laid out again, the camp rebuilt with
// its units, the other levels dropped (they build again when wanted). The
// World and its Town must enter afterwards; nothing may point into the
// old levels.
void set_map_seed(GameData& data, std::uint32_t seed);

}  // namespace d2d::game
