// Loading the Scene: composites, saves, and load_scene.
#pragma once

#include "common.hpp"
#include "scene.hpp"

#include <atomic>
#include <future>
#include <mutex>
#include <thread>

namespace d2d::client {

// Load one composite: COF <CC><mode><wclass>, then per COF layer the DCC
// <CC><LY><component><mode><layer wclass>. The weapon class comes from
// the hand/shield bytes (compcode::weapon_class, falling back to hth when
// D2 would reject the combination). Empty body layers wear "lit" (a bare
// head under a circlet, say); empty RH/LH/SH draw nothing.
// Each layer's tint (gfx[16 + layer]) maps its pixels through the item
// colormap first (compcode.md "Tints"). A dead hardcore character's ghost
// is monsters\RH (frontend.hpp).
Scene::PlayerAnim load_composite(const d2d::mpq::Stack& mpqs,
                                 const std::vector<d2d::compcode::Entry>& comp,
                                 const std::array<std::vector<std::uint8_t>, 9>& colormaps,
                                 int cls, int mode, const Scene::Appearance& gfx);

// Load an NPC/object composite: COF <root>\<code>\COF\<code><mode><BaseW>,
// then per COF layer <root>\<code>\<LY>\<code><LY><comp><mode><wclass>
// with the recipe's component for that layer ("lit" when blank).
Scene::PlayerAnim load_npc_composite(const d2d::mpq::Stack& mpqs, const Npc& n,
                                     const std::string& mode);

// Headers (and items) of every valid .d2s in `dir`, most recently played first. Bad files are
// logged and skipped — saves are user-supplied.
void load_saves(Scene& scene, const fs::path& dir);

// GameData (load_game_data), then the frontend's, the panels' and the
// world's sprites, fonts and palettes. nullopt: no game data.
std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed);

}  // namespace d2d::client
