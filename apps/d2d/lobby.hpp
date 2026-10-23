// SPDX-License-Identifier: GPL-3.0-or-later
// Local co-op's lobby (docs/design/local-coop.md "The lobby"): on
// character select each pad past the first (player 1, who alone starts the
// game) presses A for a roster and picks the character it will play; the
// game starts with them in it.
#pragma once

#include "gamepad_map.hpp"
#include "roster.hpp"
#include "scene.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::client {

struct Lobby {
    struct Seat {
        int pad = 0, seat = 0;                // the pad's id, its seat (1..3)
        pad::Mapper mapper;
        std::optional<Roster> roster;         // open while choosing
        std::optional<RosterChoice> choice;   // what it plays
        std::string chosen;                   // its name, for the line
    };
    std::vector<Seat> seats;
    int lead_save = -2;                       // player 1's pick the rosters were opened against

    // The pads past player 1's (`partners`, in seat order) and player 1's
    // pick (Scene::saves' index): seats come and go with the pads; a
    // different pick from player 1 drops the choices made against it.
    void sync(const std::vector<int>& partners, int lead_save_now, const pad::Config& config);
    // A seat's frame: A opens its roster, the roster picks, B drops a pick.
    void input(Seat& seat, const pad::Sample& sample, float seconds, const Scene& scene);
    void draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene) const;
    [[nodiscard]] bool typing() const;
    void text(std::string_view typed, bool backspace);
    // The keyboard's Enter (Steam's keyboard on a Deck) on the name being
    // typed: the new character is made, as the pad's A.
    void enter(const Scene& scene);
    [[nodiscard]] std::optional<RosterChoice> choice_of(int seat) const;

private:
    void act(Seat& seat, const pad::Actions& actions, const Scene& scene);
};

}  // namespace d2d::client
