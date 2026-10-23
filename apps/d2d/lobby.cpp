// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for lobby.hpp.
#include "lobby.hpp"

#include "common.hpp"
#include "gamepad_map.hpp"
#include "roster.hpp"
#include "scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2d::client {

void Lobby::sync(const std::vector<int>& partners, int lead_save_now, const pad::Config& config) {
    std::erase_if(seats, [&](const Seat& seat) { return !std::ranges::contains(partners, seat.pad); });
    for (std::size_t i = 0; i < partners.size() && i < 3; ++i) {
        auto found = std::ranges::find(seats, partners[i], &Seat::pad);
        if (found == seats.end()) { seats.push_back({ partners[i], 0, pad::Mapper{ config } }); found = seats.end() - 1; }
        found->seat = int(i) + 1;
    }
    if (lead_save_now != lead_save) {                 // player 1 picked another: the choices were against the old one
        lead_save = lead_save_now;
        for (auto& seat : seats) { seat.roster.reset(); seat.choice.reset(); seat.chosen.clear(); }
    }
}

void Lobby::input(Seat& seat, const pad::Sample& sample, float seconds, const Scene& scene) {
    act(seat, seat.mapper.step(sample, pad::Context::Menu, seconds), scene);
}

void Lobby::enter(const Scene& scene) {
    for (auto& seat : seats)
        if (seat.roster && roster_typing(*seat.roster)) { act(seat, pad::Actions{ .keys = { pad::Key::Return } }, scene); return; }
}

void Lobby::act(Seat& seat, const pad::Actions& actions, const Scene& scene) {
    const bool back = std::ranges::contains(actions.keys, pad::Key::Escape);
    if (seat.choice) {                                // ready: B takes the pick back
        if (back) { seat.choice.reset(); seat.chosen.clear(); }
        return;
    }
    if (!seat.roster) {                               // A (or Start) opens the roster
        if ((actions.click_press || std::ranges::contains(actions.keys, pad::Key::Return)) && lead_save >= 0 && std::size_t(lead_save) < scene.saves.size()) {
            const std::vector<std::string> playing = { scene.saves[std::size_t(lead_save)].name };
            seat.roster = open_roster(scene, playing, scene.saves[std::size_t(lead_save)]);
        }
        return;
    }
    if (back && seat.roster->stage == Roster::Stage::character) { seat.roster.reset(); return; }
    if (const auto picked = roster_input(*seat.roster, actions, scene)) {
        // A save another seat already took can't be taken twice.
        if (picked->save >= 0 && std::ranges::any_of(seats, [&](const Seat& other) { return &other != &seat && other.choice && other.choice->save == picked->save; })) return;
        seat.choice = *picked;
        seat.chosen = picked->save >= 0 && std::size_t(picked->save) < scene.saves.size() ? scene.saves[std::size_t(picked->save)].name : picked->name;
        seat.roster.reset();
    }
}

void Lobby::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene) const {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    // Each partner's state right of character select's panel (x 612), two lines a seat.
    for (const auto& seat : seats) {
        const auto& colour = kSeatColours[std::size_t(std::clamp(seat.seat, 0, 3))];
        const std::string first = seat.choice ? std::format("Player {}: {}", seat.seat + 1, seat.chosen) : std::format("Player {}", seat.seat + 1);
        const std::string second = seat.choice ? "ready (B: change)" : seat.roster ? "choosing" : "press A to join";
        const int top = 100 + 44 * (seat.seat - 1);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 612, top, first, colour[0], colour[1], colour[2]);
        scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, 612, top + 18, second, colour[0], colour[1], colour[2]);
        if (seat.roster) draw_roster(framebuffer, scene, *seat.roster, seat.seat, seat.seat);
    }
}

bool Lobby::typing() const { return std::ranges::any_of(seats, [](const Seat& seat) { return seat.roster && roster_typing(*seat.roster); }); }

void Lobby::text(std::string_view typed, bool backspace) {
    for (auto& seat : seats)
        if (seat.roster && roster_typing(*seat.roster)) { roster_text(*seat.roster, typed, backspace); return; }
}

std::optional<RosterChoice> Lobby::choice_of(int seat_index) const {
    const auto found = std::ranges::find(seats, seat_index, &Seat::seat);
    return found != seats.end() ? found->choice : std::nullopt;
}

}  // namespace d2d::client
