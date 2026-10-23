// SPDX-License-Identifier: GPL-3.0-or-later
// Local co-op's roster (docs/design/local-coop.md): a pad that joins picks
// the character it plays, in a box over the game, while the others play on.
// A save, or a new character: its class, then its name (typing opens Steam's
// on-screen keyboard on a Deck).
#pragma once

#include "gamepad_map.hpp"
#include "scene.hpp"

#include <d2s.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::client {

// A seat's colour (0..3): its cursor, its roster's title.
inline constexpr std::array<std::array<std::uint8_t, 3>, 4> kSeatColours{ { { 230, 60, 60 }, { 70, 210, 70 }, { 80, 130, 255 }, { 225, 70, 225 } } };

struct Roster {
    // A line: a save (Scene::saves' index; -1 "New character"); greyed when
    // it can't join (playing, the other hardcore or expansion, the game's
    // difficulty not open to it).
    struct Entry { std::string name, detail; int save = -1; bool usable = true; };
    std::vector<Entry> entries;
    enum class Stage { character, cls, name } stage = Stage::character;
    int pick = 0;                 // the line the cursor is on
    int cls = 0;                  // a new character's class (the save's order)
    std::string name;             // a new character's name, as typed
    float travel = 0;             // the stick's travel since the last step (pixels)
    bool expansion = true;        // the game's: the classes a new character may be
};

// The saves for a joining player: `playing` the names in the game, `lead`
// the first player's header (hardcore, expansion, difficulty).
[[nodiscard]] Roster open_roster(const Scene& scene, std::span<const std::string> playing, const d2d::d2s::Header& lead);

// The choice made: a save to play, or a new character of that class and name.
struct RosterChoice { int save = -1; int cls = 0; std::string name; };
// A pad's frame on the roster (its actions in Context::Menu): the d-pad and
// stick step the lines, A takes one, B goes back a stage; nullopt until a
// choice is made. A new character's name comes by roster_text.
[[nodiscard]] std::optional<RosterChoice> roster_input(Roster& roster, const pad::Actions& actions, const Scene& scene);
void roster_text(Roster& roster, std::string_view text, bool backspace);
[[nodiscard]] inline bool roster_typing(const Roster& roster) { return roster.stage == Roster::Stage::name; }

// Seat `seat`'s box (0..3 across the screen), its pick marked by the
// seat's cursor in its colour (0..3, kSeatColours).
void draw_roster(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Roster& roster, int seat, int colour);
// The same box with its top left at `x`, `top`; its height.
int draw_roster_at(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Roster& roster, int x, int top, int colour);

}  // namespace d2d::client
