// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.hpp"
#include "input/gamepad_map.hpp"
#include "input/loot_chooser.hpp"
#include "input/loot_radial.hpp"
#include "input/touch/input.hpp"
#include "scene.hpp"
#include "session.hpp"
#include <font.hpp>
#include <items/loot.hpp>
#include <protocol.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

// Freeze eligible IDs and labels at the dedicated pickup action.
auto ClientSession::open_loot_chooser(const PadPlayer& player_pad, const UnitState& unit, bool for_touch) -> bool {
    std::vector<LootChoice> choices;
    for (const auto& item : view.ground) {
        const std::array<pad::Candidate, 1> candidate{ pad::Candidate{ item.id, item.x, item.y } };
        if (pad::pick_target(unit.x, unit.y, player_pad.face_x, player_pad.face_y, candidate, -1, pad::Aim{ 3.5f, 3.5f, -.2f }) >= 0)
            choices.push_back({ item.id, item.label, item.rgb });
    }
    if (choices.size() < 2) return false;
    loot_chooser.start(std::move(choices), for_touch);
    touch_input.clear();
    touch_ground_finger = -1;
    touch_fingers = 0;
    pad.teleport.clear();
    return true;
}

void ClientSession::commit_loot(int item_id) {
    const auto found = std::ranges::find(view.ground, item_id, &Loot::GroundItem::id);
    const auto& owner = loot_chooser.touch ? touch_player : pad;
    const auto& unit = view.player;
    if (found != view.ground.end() && pad_field() && !view.dead) {
        const std::array<pad::Candidate, 1> candidate{ pad::Candidate{ found->id, found->x, found->y } };
        if (pad::pick_target(unit.x, unit.y, owner.face_x, owner.face_y, candidate, -1, pad::Aim{ 3.5f, 3.5f, -.2f }) >= 0)
            send(cmd::Pickup{ item_id });
    }
    loot_chooser.clear();
}

void ClientSession::loot_pad_event(const pad::Actions& actions) {
    if (!loot_chooser.open || loot_chooser.touch) return;
    if (!pad_field() || view.dead || actions.loot_cancel || actions.cancel || std::ranges::find(actions.keys, pad::Key::Escape) != actions.keys.end()) {
        loot_chooser.clear(); return;
    }
    if (actions.loot_page) loot_chooser.radial.change_page(actions.loot_page);
    const float x = actions.aim_x * actions.aim_magnitude, y = actions.aim_y * actions.aim_magnitude;
    if (!loot_chooser.ready) { if (std::hypot(x, y) <= .3f) loot_chooser.ready = true; return; }
    if (const int picked = loot_chooser.radial.step(x, y); picked >= 0) commit_loot(picked);
}


void ClientSession::draw_loot(std::vector<std::uint8_t>& framebuffer) {
    if (!loot_chooser.open || !scene) return;
    if (last_device == touch::LastDevice::MouseKeyboard || !pad_field() || view.dead) { loot_chooser.clear(); return; }
    const int width = fb_width(framebuffer), height = int(kScreenHeight);
    const auto& font = scene->font_small;
    const auto& palette = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto rect = [&](int x, int y, int rect_width, int rect_height, bool selected) {
        for (int pixel_y = std::max(0, y); pixel_y < std::min(height, y + rect_height); ++pixel_y)
            for (int pixel_x = std::max(0, x); pixel_x < std::min(width, x + rect_width); ++pixel_x) {
                auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(width) + std::size_t(pixel_x)) * 4];
                const bool border = pixel_y == y || pixel_y == y + rect_height - 1 || pixel_x == x || pixel_x == x + rect_width - 1;
                if (border || selected) { pixel[0] = selected ? 100 : 150; pixel[1] = selected ? 80 : 130; pixel[2] = 50; }
                else { pixel[0] /= 3; pixel[1] /= 3; pixel[2] /= 3; }
            }
    };
    const auto label = [&](const LootChoice& choice, int x, int y, int max_width) {
        std::string remaining = choice.name.empty() ? "Item" : choice.name;
        for (int line = 0; !remaining.empty() && line < 3; ++line) {
            std::size_t end = remaining.size();
            while (end > 1 && font.measure(remaining.substr(0, end)) > max_width) --end;
            if (end < remaining.size()) {
                const auto space = remaining.rfind(' ', end);
                if (space != std::string::npos && space > 0) end = space;
            }
            font.draw_tinted(framebuffer, width, height, palette, x, y + line * 12, remaining.substr(0, end), choice.rgb[0], choice.rgb[1], choice.rgb[2]);
            remaining.erase(0, end);
            while (!remaining.empty() && remaining.front() == ' ') remaining.erase(remaining.begin());
        }
    };
    if (loot_chooser.touch) {
        auto box = LootChooser::list_box();
        box.x += float((width - int(kScreenWidth)) / 2);
        rect(int(box.x + box.width - 70.f), int(box.y - 48.f), 70, 44, false);
        font.draw(framebuffer, width, height, palette, int(box.x + box.width - 64.f), int(box.y - 34.f), "Close");
        if (loot_chooser.choices.size() > 6) font.draw(framebuffer, width, height, palette, int(box.x), int(box.y - 28.f), "Swipe to scroll");
        for (int row = 0; row < 6; ++row) {
            const auto index = loot_chooser.scroll + std::size_t(row);
            if (index >= loot_chooser.choices.size()) break;
            const int y = int(box.y) + row * 48;
            rect(int(box.x), y, int(box.width), 46, false);
            label(loot_chooser.choices[index], int(box.x) + 8, y + 7, int(box.width) - 16);
        }
        return;
    }
    const int center_x = width / 2 - 170, center_y = height / 2;
    const auto& radial = loot_chooser.radial;
    const auto count = radial.page_size();
    font.draw(framebuffer, width, height, palette, center_x - 60, center_y - 8, "Flick, then release");
    font.draw(framebuffer, width, height, palette, center_x - 50, center_y + 10, "Back: close");
    if (radial.page_count() > 1) font.draw(framebuffer, width, height, palette, center_x - 65, center_y + 28, "D-pad: page " + std::to_string(radial.page() + 1) + "/" + std::to_string(radial.page_count()));
    for (std::size_t sector = 0; sector < count; ++sector) {
        const float angle = float(sector) * 2.f * loot_radial::kPi / float(count) - loot_radial::kPi / 2.f;
        const int x = center_x + int(std::cos(angle) * 115.f) - 14;
        const int y = center_y + int(std::sin(angle) * 105.f) - 14;
        const bool selected = int(sector) == radial.selected_sector();
        rect(x, y, 28, 28, selected);
        font.draw(framebuffer, width, height, palette, x + 9, y + 7, std::to_string(sector + 1));
        const int row_x = width / 2 - 5, row_y = 100 + int(sector) * 48;
        rect(row_x, row_y, 320, 46, selected);
        font.draw(framebuffer, width, height, palette, row_x + 5, row_y + 7, std::to_string(sector + 1));
        label(loot_chooser.choices[radial.page() * loot_radial::kSectorsPerPage + sector], row_x + 24, row_y + 5, 288);
    }
}


} // namespace d2d::client
