// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for roster.hpp.
#include "roster.hpp"

#include "common.hpp"
#include "frontend.hpp"
#include "gamepad_map.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <d2s.hpp>
#include <join.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client {

namespace {
constexpr int kBoxWidth = 260, kLineHeight = 18, kTop = 90;
int box_x(int seat) { return 15 + std::clamp(seat - 1, 0, 2) * (kBoxWidth + 2); }   // three across 800 (seat 0, a hand-over's, takes the first)
// The lines the stage shows (title first).
std::vector<std::pair<std::string, bool>> lines(const Scene& scene, const Roster& roster) {
    std::vector<std::pair<std::string, bool>> out;
    if (roster.stage == Roster::Stage::character)
        for (const auto& entry : roster.entries) out.emplace_back(entry.detail.empty() ? entry.name : entry.name + " - " + entry.detail, entry.usable);
    else if (roster.stage == Roster::Stage::cls)
        for (int cls = 0; cls < (roster.expansion ? 7 : 5); ++cls) out.emplace_back(class_name(scene, cls), true);
    else
        out.emplace_back("Name: " + roster.name + "_", true);
    return out;
}
}

Roster open_roster(const Scene& scene, std::span<const std::string> playing, const d2d::d2s::Header& lead) {
    Roster roster;
    roster.expansion = lead.expansion();
    for (std::size_t i = 0; i < scene.saves.size(); ++i) {
        const auto& save = scene.saves[i];
        const bool usable = !std::ranges::contains(playing, save.name) && save.hardcore() == lead.hardcore() && save.expansion() == lead.expansion()
                            && top_difficulty(save) >= lead.active_difficulty();
        roster.entries.push_back({ save.name, level_class_line(scene, save), int(i), usable });
    }
    roster.entries.push_back({ "New character", {}, -1, lead.active_difficulty() == 0 });   // a new one plays Normal only
    const auto first = std::ranges::find_if(roster.entries, &Roster::Entry::usable);
    roster.pick = first == roster.entries.end() ? 0 : int(first - roster.entries.begin());   // none usable: the first, greyed
    return roster;
}

std::optional<RosterChoice> roster_input(Roster& roster, const pad::Actions& actions, const Scene& scene) {
    const int count = int(lines(scene, roster).size());
    int step = 0;
    for (const auto key : actions.keys) step += key == pad::Key::Down ? 1 : key == pad::Key::Up ? -1 : 0;
    roster.travel += actions.cursor_dy;
    if (std::abs(roster.travel) >= kLineHeight) { step += roster.travel > 0 ? 1 : -1; roster.travel = 0; }
    if (step != 0 && roster.stage != Roster::Stage::name) roster.pick = std::clamp(roster.pick + step, 0, count - 1);
    const bool back = std::ranges::contains(actions.keys, pad::Key::Escape);
    const bool take = actions.click_press || std::ranges::contains(actions.keys, pad::Key::Return);
    if (back && roster.stage != Roster::Stage::character) {
        roster.stage = roster.stage == Roster::Stage::name ? Roster::Stage::cls : Roster::Stage::character;
        roster.pick = roster.stage == Roster::Stage::cls ? roster.cls : int(roster.entries.size()) - 1;
        return std::nullopt;
    }
    if (!take) return std::nullopt;
    switch (roster.stage) {
        case Roster::Stage::character: {
            const auto& entry = roster.entries[std::size_t(roster.pick)];
            if (!entry.usable) return std::nullopt;
            if (entry.save >= 0) return RosterChoice{ entry.save, 0, {} };
            roster.stage = Roster::Stage::cls;
            roster.pick = 0;
            return std::nullopt;
        }
        case Roster::Stage::cls:
            roster.cls = roster.pick;
            roster.stage = Roster::Stage::name;
            roster.pick = 0;
            return std::nullopt;
        case Roster::Stage::name: {
            const bool taken = std::ranges::any_of(scene.saves, [&](const auto& save) {   // case-blind: the files may be
                return std::ranges::equal(save.name, roster.name, {}, [](char letter) { return std::tolower(static_cast<unsigned char>(letter)); },
                                          [](char letter) { return std::tolower(static_cast<unsigned char>(letter)); });
            });
            if (!d2d::net::valid_join_name(roster.name) || taken) return std::nullopt;   // a name D2 takes, not a save's
            return RosterChoice{ -1, roster.cls, roster.name };
        }
    }
    return std::nullopt;
}

void roster_text(Roster& roster, std::string_view text, bool backspace) {
    if (!roster_typing(roster)) return;
    if (backspace && !roster.name.empty()) roster.name.pop_back();
    for (const char letter : text)
        if (roster.name.size() < 15 && (std::isalpha(static_cast<unsigned char>(letter)) || letter == '_' || letter == '-')) roster.name += letter;
}

void draw_roster(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Roster& roster, int seat, int colour_index) {
    (void)draw_roster_at(framebuffer, scene, roster, box_x(seat), kTop, colour_index);
}

int draw_roster_at(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Roster& roster, int x, int top, int colour_index) {
    const auto shown = lines(scene, roster);
    const int height = (int(shown.size()) + 2) * kLineHeight + 8;
    shade_box(framebuffer, x, top, kBoxWidth, height);
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto& colour = kSeatColours[std::size_t(std::clamp(colour_index, 0, 3))];
    const int cell = scene.font.sheet().frames_per_direction() > 0 ? int(scene.font.sheet().frame(0, 0).height) : 16;
    const char* title = roster.stage == Roster::Stage::character ? "choose a character" : roster.stage == Roster::Stage::cls ? "choose a class" : "type a name";
    int base = top + kLineHeight;
    scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x + 10, base - cell + 1, std::format("Player {}: {}", colour_index + 1, title), colour[0], colour[1], colour[2]);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        base += kLineHeight;
        const auto& [text, usable] = shown[i];
        const int text_x = x + 40;
        if (usable) scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, pal, text_x, base - cell + 1, text);
        else scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, text_x, base - cell + 1, text, 100, 100, 100);
        if (int(i) == roster.pick && roster.stage != Roster::Stage::name && scene.cursor.frames_per_direction() > 0) {
            // The seat's cursor on its pick: the hand's tip (its anchor, as the mouse's) at the line's top left.
            const auto& frame = scene.cursor.frame(0, 0);
            const int tip_x = x + 12, tip_y = base - cell + 4;
            blit_sprite(framebuffer, frame, tinted(pal, colour[0], colour[1], colour[2]), tip_x + frame.offset_x, tip_y + frame.offset_y - int(frame.height) + 1);
        }
    }
    return height;
}

}  // namespace d2d::client
