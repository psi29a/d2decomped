// Shared includes, screen/iso constants, data-dir lookup, blit helpers.
#pragma once

#include "game.hpp"


#include <mpq.hpp>
#include <cof.hpp>
#include <compcode.hpp>
#include <dc6.hpp>
#include <dcc.hpp>
#include <devctl.hpp>
#include <d2s.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <maze.hpp>
#include <outdoor_data.hpp>
#include <dt1.hpp>
#include <font.hpp>
#include <palette.hpp>
#include <screenshot.hpp>
#include <tbl.hpp>
#include <txt.hpp>
#include <rules.hpp>
#include <monsters.hpp>
#include <combat.hpp>
#include <drops.hpp>
#include <shrines.hpp>
#include <quests.hpp>
#include <light.hpp>
#include <weather.hpp>
#include <skills.hpp>
#include <sequences.hpp>
#include <userdir.hpp>

#include "log.hpp"

#include "npc_menu.hpp"
#include "npc_talk.hpp"
#include "speech_sound.hpp"
#include "video.hpp"
#include <obj_preset.hpp>

#include <SDL3/SDL.h>

#if __has_include(<AL/al.h>)
#  include <AL/al.h>
#  include <AL/alc.h>
#else
#  include <OpenAL/al.h>
#  include <OpenAL/alc.h>
#endif
#include <CLI/CLI.hpp>
#include <csignal>
#include <unordered_map>
#include <format>
#include <unordered_set>
#include <variant>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <fstream>
#include <functional>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

// windef.h (through SDL / OpenAL on Windows) defines near and far as
// empty macros; they'd eat any name spelled so (MSVC C2513).
#ifdef _WIN32
#undef near
#undef far
#endif

namespace fs = std::filesystem;

// Set to 1 by d2d_sigint_handler on SIGINT/SIGTERM; polled each frame.
// Declared at global scope because std::signal handlers must have C
// linkage. Defined further below.
extern volatile std::sig_atomic_t g_sigint_quit;

namespace d2d::app {

// D2 LoD 800×600 mode dimensions — matches TitleScreen.DC6, which ships
// pre-sliced into a 4×3 grid of sub-frames adding up to exactly 800×600
// (columns 256/256/256/32, rows 256/256/88).
constexpr std::uint32_t kW = 800;
constexpr std::uint32_t kH = 600;


// Dev overlay toggled by devctl `debug collision`: blocked subtiles in red.
inline bool g_debug_collision = false;

fs::path default_data_dir(std::string_view cfg_data);

// Palette-lookup blit: index 0 is transparent (skip), all other indices map
// through the supplied palette to real RGBA. Signed dest so negative offsets
// clip cleanly (logo frames have ox down to -180).
// `shade` scales the colour, 256 = as is (skill tree: greyed-out icons).
void blit_sprite(std::vector<std::uint8_t>& fb,
                 const d2d::dc6::Frame& f,
                 const d2d::palette::Palette& pal,
                 int dst_x, int dst_y, int shade = 256);

// Blit a frame at anchor+(frame.offset_x, frame.offset_y - height + 1).
// D2 convention is BOTTOM-LEFT origin — matches DCC's frame-box math (see
// OpenDiablo2/dcc_direction_frame.go: `box.top = y_offset - height + 1`).
// The fire animation confirms this: its oy=132 is constant across frames of
// varying height, so anchoring the BOTTOM keeps the fire base planted while
// the flame top flickers up and down.
void blit_at_anchor(std::vector<std::uint8_t>& fb,
                    const d2d::dc6::Frame& f,
                    const d2d::palette::Palette& pal,
                    int anchor_x, int anchor_y);

// Additive blit — D2's fire assets are drawn with TRANS_ADDITIVE. When a
// PL2 is available we resolve through Blizzard's authored
// MaxComponentBlend[fg][bg] table, which returns the palette index
// Blizzard picked for that combination (palette-preserving, exact match
// against the original renderer). Falls back to plain RGB-clamp additive
// when the caller has only a plain Palette — used by the credits path
// which doesn't need PL2 fidelity.
//
// Reading back the framebuffer to a palette index would require an RGB->
// index reverse lookup we don't have; instead the PL2 path samples the
// UNMODIFIED-so-far fg color for the src and treats the current fb pixel
// as bg by finding its closest palette index. To keep it fast and avoid a
// KD-tree, we short-circuit the common case: when bg is black (index 0,
// i.e. the fb has not been written since the last clear), additive(fg, 0)
// == fg — so we just plot pal[fg]. Otherwise fall back to RGB-clamp add.
// The fire is drawn early in the frame over the char-create bg (which is
// dark/near-black in the campfire pit), so this covers > 95% of pixels.
void blit_additive(std::vector<std::uint8_t>& fb,
                   const d2d::dc6::Frame& f,
                   const d2d::palette::Palette& pal,
                   const d2d::palette::Pl2* pl2,
                   int anchor_x, int anchor_y);

void paint_test_pattern(std::vector<std::uint8_t>& fb);

// A DC6 background is often stored as a grid of sub-frames arranged
// left-to-right, top-to-bottom (D2 splits large images because DC6 frames
// each cap at 256×256 in practice). This lays them out side by side.
void blit_dc6_grid(std::vector<std::uint8_t>& fb,
                   const d2d::dc6::Sprite& spr,
                   const d2d::palette::Palette& pal,
                   int origin_x, int origin_y,
                   int tiles_across);

// D2's base game/anim tick is 25 Hz — every animation rate in AnimData.d2
// is `25 * animRate / 256`. The frontend menu runs at that base rate;
// tying our advance to wall-clock ms keeps playback correct regardless of
// how fast we happen to be rendering (60 Hz, 120 Hz, headless, whatever).
constexpr std::uint32_t kBaseFrameMs = 40;   // 1000 / 25

}  // namespace d2d::app
