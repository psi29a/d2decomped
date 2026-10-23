// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for party.hpp.
#include "party.hpp"

#include "common.hpp"
#include "scene.hpp"
#include "ui.hpp"

#include <dc6.hpp>
#include <font.hpp>
#include <palette.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace d2d::client {

namespace {

constexpr int kLeft = 0x50, kTop = 0x3c;                         // the left-panel spot (0x7a2858, -0x7a285c)
constexpr int kFirstRow = 0x5a, kRowStep = 0x26, kMostRows = 9;   // FUN_0049a640
constexpr int kCloseX = 0x116, kCloseBottom = 0x1a5;              // FUN_0049a950: buysellbtn 10 / 11
constexpr int kLeaveX = 0xe3, kLeaveBottom = 0x23;                // FUN_0049a510: partybuttons over its "LEAVE"
// The boxes (FUN_0049a090): x, the bottom's offset from the row's, the
// flag, the frames off / on.
struct Box { int x, dy, flag, off, on; };
constexpr std::array<Box, 4> kBoxes = { { { 0xe, 7, 8, 4, 6 }, { 0xf5, 0, 1, 0xc, 0xe }, { 0x10a, 0, 2, 0, 2 }, { 0x11f, 0, 4, 8, 10 } } };
constexpr std::array<std::uint16_t, 7> kClassName = { 0x103a, 0x103b, 0x103d, 0x103e, 0x103c, 0x2771, 0x2772 };
constexpr std::array<std::array<std::uint8_t, 3>, 9> kRgb{ { { 255, 255, 255 }, { 255, 77, 77 }, { 0, 255, 0 }, { 105, 105, 255 }, { 199, 179, 119 },
                                                            { 105, 105, 105 }, { 255, 255, 255 }, { 255, 255, 255 }, { 255, 168, 0 } } };

bool inside(int mouse_x, int mouse_y, int x, int bottom, int width, int height) {    // FUN_0049aa50
    return mouse_x >= kLeft + x && mouse_x <= kLeft + x + width && mouse_y >= kTop + bottom - height && mouse_y <= kTop + bottom;
}

// Hostility needs both players at level 9 and the player in town
// (FUN_0049bb60's checks).
bool hostile_allowed(const PartyPanel::Row& row, const PartyPanel::Own& own) { return row.level >= 9 && own.level >= 9 && own.in_town; }

}  // namespace

PartyPanel::Click PartyPanel::input(const Mouse& mouse, const std::vector<Row>& rows, const Own& own) {
    if (mouse.release_this_frame && close_down) {
        close_down = false;
        if (inside(mouse.x, mouse.y, kCloseX, kCloseBottom, 0x20, 0x20)) { open = false; return { Click::kClose }; }
    }
    if (!mouse.press_this_frame) return {};
    if (inside(mouse.x, mouse.y, kCloseX, kCloseBottom, 0x20, 0x20)) { close_down = true; return {}; }
    if (own.fixed) return {};
    if (own.party != 0xffff && inside(mouse.x, mouse.y, kLeaveX, kLeaveBottom, 0x35, 0xb)) return { Click::kLeave };
    for (std::size_t i = 0; i < rows.size() && int(i) < kMostRows; ++i) {
        const int bottom = kFirstRow + kRowStep * int(i);
        const auto& row = rows[i];
        if (row.relation != 1 && inside(mouse.x, mouse.y, 0xbe, bottom, 0x35, 0x14)) return { Click::kButton, row.id };
        for (const auto& box : kBoxes) {
            if (!inside(mouse.x, mouse.y, box.x, bottom + box.dy, 0x14, 0x14)) continue;
            if (box.flag == 8 && !hostile_allowed(row, own)) return {};
            return { Click::kFlag, row.id, box.flag };
        }
    }
    return {};
}

void PartyPanel::draw(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const std::vector<Row>& rows, const Own& own, int mouse_x, int mouse_y) const {
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto cel = [&](const d2d::dc6::Sprite& sprite, int frame, int left, int bottom) {   // FUN_004f6480, draw mode 5
        if (frame < 0 || std::uint32_t(frame) >= sprite.frames_per_direction()) return;
        const auto& image = sprite.frame(0, std::uint32_t(frame));
        blit_sprite(framebuffer, image, pal, kLeft + left, kTop + bottom - int(image.height));
    };
    const auto& font16 = scene.font;
    const auto& font8 = scene.font_small.line_height() > 0 ? scene.font_small : scene.font;
    const auto put = [&](const d2d::font::Font& font, const std::string& words, int x, int baseline, int colour, bool centred) {
        const auto& rgb = kRgb[std::size_t(std::clamp(colour, 0, 8))];
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, kLeft + (centred ? x - font.measure(words) / 2 : x),
                         kTop + glyph_top(font, baseline), words, rgb[0], rgb[1], rgb[2]);
    };
    const auto cut = [&](const d2d::font::Font& font, std::string words, int width) {    // FUN_0049a2c0 / FUN_0049a830: "..." past the width
        if (font.measure(words) <= width) return words;
        while (!words.empty() && font.measure(words + "...") > width) words.pop_back();
        return words + "...";
    };
    // FUN_00499e50: Menu\party's four frames.
    const int height = int(kScreenHeight);
    cel(scene.party_panel, 0, 0, height - 0xe0 - kTop * 2);
    cel(scene.party_panel, 1, 0x100, height - 0xe0 - kTop * 2);
    cel(scene.party_panel, 2, 0, height - 0x30 - kTop * 2);
    cel(scene.party_panel, 3, 0x100, height - 0x30 - kTop * 2);
    // FUN_0049a580: the player's name, centred at 0x69; FUN_0049a510: LEAVE in a party.
    put(font16, own.name, 0x69, 0x1f, 0, true);
    if (own.party != 0xffff && !own.fixed) {
        cel(scene.party_buttons, 0, kLeaveX, kLeaveBottom);
        put(font8, string_id(scene, 0x1038), kLeaveX + 0x1b, 0x21, 2, true);
    }
    // FUN_0049c270: each row in font8 — the name (green: our party, orange:
    // another, red: hostile), class, "Lvl: n", a member's area; FUN_0049a690:
    // the party button with its word and the four boxes, red over the
    // hostile one where it can't be toggled; FUN_0049a9b0: partybar
    // between parties.
    std::string tip_first, tip_second;
    int tip_x = 0;
    for (std::size_t i = 0; i < rows.size() && int(i) < kMostRows; ++i) {
        const auto& row = rows[i];
        const int bottom = kFirstRow + kRowStep * int(i);
        int colour = 0;
        if (row.party != 0xffff) colour = row.party == own.party ? 2 : 8;
        if (row.hostile) colour = 1;
        put(font8, cut(font8, row.name, 100), 0x24, bottom - 2, colour, false);
        put(font8, string_id(scene, kClassName[std::size_t(std::clamp(row.cls, 0, 6))]), 0x24, bottom + 12, colour, false);
        put(font8, string_id(scene, 0xd28) + string_id(scene, 0xf9d) + string_id(scene, 0xf9b) + std::to_string(row.level), 0x10a, bottom + 12, colour, false);
        if (!row.area.empty() && row.party != 0xffff && row.party == own.party) put(font8, cut(font8, row.area, 0x8c), 0x7e, bottom + 12, colour, false);
        if (own.fixed) continue;
        if (row.relation != 1) {
            cel(scene.party_buttons, 0, 0xbe, bottom);
            const std::uint16_t word = row.relation == 0 ? 0x1036 : row.relation == 2 ? 0x1037 : row.relation == 3 ? 0x1038 : 0;
            if (word) put(font8, string_id(scene, word), 0xbe + 0x1b, bottom - 2, 0, true);
        }
        for (const auto& box : kBoxes) {
            cel(scene.party_boxes, (row.flags & box.flag) ? box.on : box.off, box.x, bottom + box.dy);
            if (box.flag == 8 && !hostile_allowed(row, own))
                for (int y = kTop + bottom + box.dy - 0x14; y < kTop + bottom + box.dy && y < height; ++y)
                    for (int x = kLeft + box.x; x < kLeft + box.x + 0x14; ++x) {
                        auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
                        pixel[0] = std::uint8_t((pixel[0] + 255) / 2); pixel[1] = std::uint8_t(pixel[1] / 2); pixel[2] = std::uint8_t(pixel[2] / 2);
                    }
            // FUN_0049b330's tooltips.
            if (inside(mouse_x, mouse_y, box.x, bottom + box.dy, 0x14, 0x14)) {
                const bool set = (row.flags & box.flag) != 0;
                std::uint16_t first = 0;
                if (box.flag == 8) first = row.level < 9 || own.level < 9 ? 0x101a : !own.in_town ? 0x1019 : set ? 0x100c : 0x100a;
                if (box.flag == 1) first = set ? 0x1010 : 0x100e;
                if (box.flag == 2) first = set ? 0x1012 : 0x1014;
                if (box.flag == 4) first = set ? 0x1017 : 0x1016;
                tip_first = string_id(scene, first);
                tip_second = first == 0x1016 || first == 0x1017 || first == 0x1019 || first == 0x101a ? "" : string_id(scene, std::uint16_t(first + 1));
                tip_x = box.flag == 8 ? (first == 0x1019 ? 0x6e : 0xa0) : box.flag == 1 ? 0xff : box.flag == 2 ? 0x114 : 0x128;   // FUN_0049b330's centres
            }
        }
        if (row.relation != 1 && inside(mouse_x, mouse_y, 0xbe, bottom, 0x35, 0x14)) {
            const std::uint16_t first = row.relation == 0 ? 0x1004 : row.relation == 2 ? 0x1006 : row.relation == 4 ? 0x1008 : 0x1018;
            tip_first = string_id(scene, first);
            tip_second = first == 0x1018 ? "" : string_id(scene, std::uint16_t(first + 1));
            tip_x = 0xda;
        }
        if (i > 0 && rows[i - 1].party != row.party) {
            cel(scene.party_bar, 0, 0xc, bottom - 23);
            cel(scene.party_bar, 1, 0x10b, bottom - 23);
        }
    }
    cel(scene.store_buttons, close_down ? 11 : 10, kCloseX, kCloseBottom);
    if (own.party != 0xffff && !own.fixed && inside(mouse_x, mouse_y, kLeaveX, kLeaveBottom, 0x35, 0xb)) { tip_first = string_id(scene, 0x1018); tip_second.clear(); tip_x = 0xd5; }
    if (!tip_first.empty()) {                                       // FUN_0049aaf0: a dark box, the lines centred
        const int width = std::max(font8.measure(tip_first), font8.measure(tip_second)) + 10, lines = tip_second.empty() ? 1 : 2;
        const int box_top = kTop + 0x32 - 12, box_left = kLeft + tip_x - width / 2;
        for (int y = std::max(box_top, 0); y < box_top + 15 * lines && y < height; ++y)
            for (int x = std::max(box_left, 0); x < box_left + width && x < int(kScreenWidth); ++x)
                for (int channel = 0; channel < 3; ++channel) {
                    auto& value = framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4 + std::size_t(channel)];
                    value = std::uint8_t(value / 4);
                }
        put(font8, tip_first, tip_x, lines == 2 ? 0x32 : 0x37, 0, true);
        if (lines == 2) put(font8, tip_second, tip_x, 0x41, 0, true);
    }
}

}  // namespace d2d::client
