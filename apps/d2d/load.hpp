// Asset loading: composites, NPCs, saves, load_scene, DS1/DT1 world.
#pragma once

#include "ui.hpp"

#include <atomic>
#include <future>
#include <mutex>
#include <thread>

namespace d2d::app {

// Forward decl — full body lives after Scene{} construction so it can use
// the same members without repeating field types.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path);

// Act 1's layout from the map seed picks the town's DS1 (the side the
// Blood Moor went) and where both sit.
void place_act1(Scene& scene, d2d::mpq::Stack& mpqs, const d2d::drlg::OutdoorAssets& act1, std::uint32_t map_seed);


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






// Monster tables (MonStats, MonStats2, MonLvl), the Blood Moor's Levels.txt
// monster columns, and its rooms populated (components/rules/monsters.hpp).
// ponytail: every room at load, in cell order, normal difficulty — game.exe
// populates a room when it first activates (so the game seed's order
// follows the player) and knows the game's difficulty; the game seed is
// the map seed here.
void load_monsters(Scene& scene, const d2d::mpq::Stack& mpqs);


void load_skills(Scene& scene, const d2d::mpq::Stack& mpqs);

// Act 1 town NPCs from the DS1's type-1 objects: id -> MonPreset.txt
// (Act 1 rows) Place -> MonStats row (by Id) -> its MonStatsEx's MonStats2
// row (monster_npc).
// Positions are in subtiles; a unit stands at its subtile's centre.
// ponytail: act 1 only, NU idle only; "place_*" spawn markers skipped.

void load_npcs(Scene& scene, const d2d::mpq::Stack& mpqs);

// Excel tables + the derived composite data: the component table and each
// class's starting-gear appearance (CharStats.txt item1..: "rarm" item in
// the right hand, a "larm" shield on the shield layer; body parts "lit").
void load_composite_data(Scene& scene, const d2d::mpq::Stack& mpqs);

// Draws a composite frame, feet at the anchor (defined with the other
// DCC blitters below).
void draw_composite(std::vector<std::uint8_t>& fb, const Scene::PlayerAnim& p,
                    const d2d::palette::Palette& pal, int dir_want,
                    std::uint32_t elapsed_ms, int anchor_x, int anchor_y);

// Headers (and items) of every valid .d2s in `dir`, most recently played first. Bad files are
// logged and skipped — saves are user-supplied.
void load_saves(Scene& scene, const fs::path& dir);

std::optional<Scene> load_scene(const fs::path& data_dir, const fs::path& patch_installer, std::uint32_t map_seed);



// Load one DS1 + every DT1 it references (silently skips missing ones —
// some rogue-camp DS1s reference .tg1 tile-group files, which aren't
// present in 1.14d). Populates the level's ds1, dt1s, tile_lookup, walk
// and act1_pal on the scene. Idempotent, called once during load_scene.
void load_world(Scene& scene, d2d::mpq::Stack& mpqs, const char* ds1_path);

// A game on another map seed: act 1 laid out again, the camp rebuilt with
// its units, the other levels dropped (they build again when wanted). The
// World and its Town must enter afterwards; nothing may point into the
// old levels.
void set_map_seed(Scene& scene, std::uint32_t seed);

}  // namespace d2d::app
