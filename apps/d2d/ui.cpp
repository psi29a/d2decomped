// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for ui.hpp: buttons, NPC menus and speech.
#include "ui.hpp"

#include "common.hpp"
#include "scene.hpp"

#include <dc6.hpp>
#include <hire_list.hpp>
#include <monster_ids.hpp>
#include <palette.hpp>
#include <quests.hpp>
#include <quests_act5.hpp>
#include <rules.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace d2d::client {

void blit_button_chrome(std::vector<std::uint8_t>& framebuffer,
                        const d2d::palette::Palette& pal,
                        const d2d::dc6::Sprite& chrome,
                        int x, int y, bool pressed) {
    const auto frames = chrome.frames_per_direction();
    if (frames == 0) return;
    if (frames == 2) {
        const auto& frame = chrome.frame(0, pressed ? 1 : 0);
        blit_sprite(framebuffer, frame, pal, x, y);
        return;
    }
    // 4-frame split. First half goes at x; second half right after it.
    const auto base = pressed ? 2u : 0u;
    if (frames > base) {
        const auto& left = chrome.frame(0, base);
        blit_sprite(framebuffer, left, pal, x, y);
        if (frames > base + 1) {
            const auto& right = chrome.frame(0, base + 1);
            blit_sprite(framebuffer, right, pal, x + int(left.width), y);
        }
    }
}

NpcMenuState open_npc_menu(const Scene& scene, const Level& level, int npc, int screen_x, int screen_y, int clvl , int unidentified ,
                           bool respec, bool east, bool service, const std::string& resurrect) {
    NpcMenuState menu;
    const auto& npc_info = level.npcs[std::size_t(npc)];
    const auto found = std::ranges::find_if(kNpcMenus, [&](const NpcMenu& entry) { return entry.hc_idx == npc_info.hc_idx; });
    if (found == kNpcMenus.end()) return menu;
    menu.npc = npc;
    menu.lines.push_back({ npc_info.name, 21, 0, 0, true });
    auto entries = found->entries;
    namespace monster_ids = d2d::rules::monster_ids;
    if (npc_info.hc_idx == monster_ids::kKashya && clvl > 7) entries = { 0xd35, 0xd45 };
    if (npc_info.hc_idx == monster_ids::kAkara && respec) entries[2] = 0x2ba0;
    if (npc_info.hc_idx == monster_ids::kWarriv && east) entries = { 0xd35, 0xd36 };   // FUN_004b65f0: go east
    if (npc_info.hc_idx == monster_ids::kMeshif && east) entries = { 0xd35, 0xd38 };   // FUN_004b6620: sail east
    if (npc_info.hc_idx == monster_ids::kCharsi && service) entries = { 0xd35, 0xd06, 0xfb1 };
    if (npc_info.hc_idx == monster_ids::kLarzuk && service) entries = { 0xd35, 0xd06, 0x58dc };          // 0x4b3730: socket item
    if (npc_info.hc_idx == d2d::rules::BetrayalQuest::kAnya && service) entries = { 0xd35, 0xd44, 0xd46, 0x58dd };   // 0x4b3760: personalize
    if (!resurrect.empty() && std::ranges::find(d2d::rules::kMercNpcs, npc_info.hc_idx) != d2d::rules::kMercNpcs.end()) {
        auto slot = std::ranges::find(entries, 0xd45);
        if (slot == entries.end()) slot = std::ranges::find(entries, 0);
        if (slot != entries.end()) { std::shift_right(slot, entries.end(), 1); *slot = 0x1507; }
    }
    for (const auto id : entries)
        if (id && !(id == 0xfb4 && unidentified == 0))
            menu.lines.push_back({ id == 0x1507 ? resurrect : string_id(scene, id), 15, 0, 0, false,
                                id == 0xd35 ? NpcMenuState::kTalk
                                : id == 0xd44 || id == 0xd06 ? NpcMenuState::kTrade
                                : id == 0xd46 ? NpcMenuState::kGamble
                                : id == 0xd45 ? NpcMenuState::kHire
                                : id == 0xfb4 ? NpcMenuState::kIdentify
                                : id == 0x2ba0 ? NpcMenuState::kRespec
                                : id >= 0xd36 && id <= 0xd39 ? NpcMenuState::kCaravan
                                : id == 0xfb1 || id == 0x58dc || id == 0x58dd ? NpcMenuState::kImbue
                                : id == 0x1507 ? NpcMenuState::kResurrectMerc : NpcMenuState::kClose });
    menu.lines.push_back({ string_id(scene, 0x102e), 15 });
    layout_npc_menu(scene, menu, screen_x, screen_y);
    return menu;
}

std::string resurrect_line(const Scene& scene, int merc_type, int name_index, int cost) {
    const auto merc = scene.mercs.find(merc_type);
    std::string line = string_id(scene, 0x58a8);
    if (const auto found_at = line.find("%s"); found_at != line.npos) line.replace(found_at, 2, merc != scene.mercs.end() ? merc_name(scene, merc->second, name_index) : std::string("?"));
    if (const auto found_at = line.find("%d"); found_at != line.npos) line.replace(found_at, 2, std::to_string(cost));
    return line;
}

NpcMenuState open_respec_menu(const Scene& scene, int npc, int screen_x, int screen_y) {
    NpcMenuState menu;
    menu.npc = npc;
    menu.lines.push_back({ string_id(scene, 0x2ba0), 21, 0, 0, true });
    menu.lines.push_back({ string_id(scene, 0xd49), 15, 0, 0, false, NpcMenuState::kRespecOk });
    menu.lines.push_back({ string_id(scene, 0xd48), 15 });
    layout_npc_menu(scene, menu, screen_x, screen_y);
    return menu;
}

NpcMenuState open_trade_refused(const Scene& scene, int npc, d2d::rules::Trade result, int mouse_x, int mouse_y) {
    NpcMenuState menu;
    menu.npc = npc;
    const auto text = std::uint16_t(result == d2d::rules::Trade::refused ? 0xd21 : result == d2d::rules::Trade::no_room ? 0xd1c : 0xd22);
    menu.lines.push_back({ string_id(scene, text), 21, 0, 0, true, NpcMenuState::kClose, -1, true });
    menu.lines.push_back({ string_id(scene, 0xd4b), 15 });
    layout_npc_menu(scene, menu, mouse_x, mouse_y + 150);   // at the mouse itself, not 150 up as an NPC's
    return menu;
}

NpcMenuState open_talk_menu(const Scene& scene, const Level& level, int npc, int screen_x, int screen_y,
                            const std::vector<d2d::rules::QuestMsg>& quest) {
    NpcMenuState menu;
    const auto& npc_info = level.npcs[std::size_t(npc)];
    const auto found = std::ranges::find_if(kNpcTalk, [&](const NpcTalk& entry) { return entry.hc_idx == npc_info.hc_idx; });
    menu.npc = npc;
    menu.lines.push_back({ string_id(scene, 0xd35), 21, 0, 0, true });
    if (found != kNpcTalk.end() && !found->topics.empty()) {
        if (!found->no_intro) menu.lines.push_back({ string_id(scene, 0xd47), 15, 0, 0, false, NpcMenuState::kIntro });
        menu.lines.push_back({ string_id(scene, 0xd43), 15, 0, 0, false, NpcMenuState::kGossip });
    }
    for (const auto& message : quest)
        if (!message.greet)
            menu.lines.push_back({ string_id(scene, std::uint16_t(d2d::rules::quest_name(message.string))), 15, 0, 0, false, NpcMenuState::kQuest, message.string });
    menu.lines.push_back({ string_id(scene, 0xd48), 15 });
    layout_npc_menu(scene, menu, screen_x, screen_y);
    return menu;
}

void layout_npc_menu(const Scene& scene, NpcMenuState& menu, int screen_x, int screen_y) {
    int text_width = 0;
    for (auto& line : menu.lines) { line.width = scene.font.measure(line.text); text_width = std::max(text_width, line.width); menu.box_height += line.height; }
    menu.box_width = text_width + 20;
    menu.box_height += 15;
    for (auto& line : menu.lines) line.x = line.width < menu.box_width ? (menu.box_width - line.width + 1) / 2 + 1 : 0;
    const int anchor_x = screen_x, anchor_y = std::max(screen_y - 150, 20);
    menu.x = anchor_x - menu.box_width / 2;
    menu.y = anchor_y - menu.box_height / 3;
    if (menu.x + menu.box_width > int(kScreenWidth) - 10) menu.x = int(kScreenWidth) - menu.box_width;
    if (menu.y + menu.box_height > int(kScreenHeight) - 0x3a) menu.y = int(kScreenHeight) - menu.box_height - 0x30;
    if (menu.x < 11) menu.x = 10;
    if (menu.y < 11) menu.y = 10;
}

int talk_topic(const NpcTalk& talk, bool intro, int cls, d2d::rules::Rng& rng,
               const std::function<bool(int quest)>& quest_done) {
    if (intro) return talk.topics.size() > 1 && int(talk.topics[1].cls) == cls ? 1 : 0;
    const int count = int(talk.topics.size());
    for (int tries = 10; tries > 0 && count > 0; --tries) {
        const int pick = rng(count);
        if (pick < 2) continue;
        const auto& topic = talk.topics[std::size_t(pick)];
        if (topic.cls != 7 && int(topic.cls) != cls) continue;
        if (topic.quest_gated && quest_done(int(topic.quest)) != (topic.quest_state != 0)) continue;
        return pick;
    }
    return std::min(2, count - 1);
}

Speech start_speech(const Scene& scene, int npc, std::uint16_t string, std::uint32_t now_ms) {
    Speech speech;
    speech.npc = npc;
    speech.start_ms = now_ms;
    speech.string = string;
    const std::string text = string_id(scene, string);
    std::size_t start = 0;
    bool first = true;
    while (start <= text.size()) {
        const auto newline = text.find('\n', start);
        const std::string line = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        if (first) {
            first = false;
            const bool num = !line.empty() && std::ranges::all_of(line, [](char letter) { return letter >= '0' && letter <= '9'; });
            speech.rate = num ? std::atoi(line.c_str()) : 8;
            if (!num) speech.lines.push_back(line);
        } else {
            speech.lines.push_back(line);
        }
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    return speech;
}

void draw_speech(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Speech& speech, std::uint32_t now_ms) {
    if (speech.npc < 0) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const int box_x = (int(kScreenWidth) - 0x145) / 2, top = 12;
    for (int y = std::max(0, top - 5); y < top - 5 + 0x7a; ++y)
        for (int x = box_x; x < box_x + 0x145; ++x) {
            auto* pixel = &framebuffer[(std::size_t(y) * kScreenWidth + std::size_t(x)) * 4];
            pixel[0] = std::uint8_t(pixel[0] / 2); pixel[1] = std::uint8_t(pixel[1] / 2); pixel[2] = std::uint8_t(pixel[2] / 2);
        }
    const auto& font = scene.font_formal11.line_height() > 0 ? scene.font_formal11 : scene.font;
    const int cell = font.sheet().frames_per_direction() > 0 ? int(font.sheet().frame(0, 0).height) : 16;
    const int off = speech.offset_px(now_ms);
    for (std::size_t i = 0; i < speech.lines.size(); ++i) {
        const int base = top + 0x70 - off + int(i) * 18;
        if (!speech_line_visible(base, cell)) continue;
        font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, box_x + 16, base - cell + 1, speech.lines[i], 255, 255, 255,
                      kSpeechBoxTop, kSpeechBoxBottom);
    }
}

void draw_npc_menu(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const NpcMenuState& menu,
                   int mouse_x, int mouse_y, std::uint32_t now_ms) {
    if (menu.npc < 0) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    shade_box(framebuffer, menu.x, menu.y, menu.box_width, menu.box_height);
    const int hover = menu.line_at(mouse_x, mouse_y);
    const int cell = scene.font.sheet().frames_per_direction() > 0 ? int(scene.font.sheet().frame(0, 0).height) : 16;
    int base = menu.y;
    const auto& blue = scene.act_text_blue.entries().empty() ? pal : scene.act_text_blue;
    for (std::size_t i = 0; i < menu.lines.size(); ++i) {
        const auto& line = menu.lines[i];
        base += line.height;
        const int x = menu.x + line.x;
        if (line.header && line.red) scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, base - cell + 1, line.text, 255, 77, 77);
        else if (line.header) scene.font.draw_tinted(framebuffer, kScreenWidth, kScreenHeight, pal, x, base - cell + 1, line.text, 199, 179, 119);
        else          scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, int(i) == hover && menu.blue_hover ? blue : pal, x, base - cell + 1, line.text);
        if (int(i) == hover && !menu.blue_hover) draw_focus(framebuffer, scene, x, base, line.width, now_ms);
    }
    // The hire window's list box (FUN_004bee90): the rows that fit from the
    // first shown, each line at x + 0x20 on rules::list_line_y, white, the
    // row under the mouse blue; a line wider than width - 0x32 loses its
    // last letters till it fits (FUN_004be410).
    if (!menu.list) return;
    const auto& list = *menu.list;
    const auto lines = menu.list_lines();
    // The scroll bar (textslid, each cel by its bottom-left): the up arrow
    // (9, 11 pressed), the track's cells (13), the down arrow (8, 10 pressed)
    // on the last of them; the thumb after the rows (rules::list_thumb_cels).
    const auto cel = [&](int frame_index, int left, int bottom) {
        if (std::uint32_t(frame_index) >= scene.textslid.frames_per_direction()) return;
        const auto& image = scene.textslid.frame(0, std::uint32_t(frame_index));
        blit_sprite(framebuffer, image, pal, left, bottom - int(image.height));
    };
    const int scroll_x = d2d::rules::list_scroll_x(list.box), scroll_y = d2d::rules::list_scroll_y(list.box), cells = d2d::rules::list_track_cells(list.box);
    cel(list.part == d2d::rules::ListPart::kUp ? 11 : 9, scroll_x, scroll_y);
    for (int track = 0; track < cells; ++track) cel(0xd, scroll_x, scroll_y + 12 * (track + 1));
    cel(list.part == d2d::rules::ListPart::kDown ? 10 : 8, scroll_x, scroll_y + 12 * cells);
    const int shown = d2d::rules::list_rows_shown(list.box, lines, menu.scroll);
    int rows_before = 0, lines_before = 0;
    for (int row = menu.scroll; row < menu.scroll + shown && row < int(list.rows.size()); ++row, ++rows_before)
        for (const auto& text : list.rows[std::size_t(row)].lines) {
            if (text.empty()) continue;
            std::string cut = text;
            while (!cut.empty() && scene.font.measure(cut) > list.box.width - 0x32) cut.pop_back();
            const int baseline = d2d::rules::list_line_y(list.box, rows_before, lines_before++);
            scene.font.draw(framebuffer, kScreenWidth, kScreenHeight, row == list.hover ? blue : pal, list.box.x + 0x20, baseline - cell + 1, cut);
        }
    if (const auto thumb = d2d::rules::list_thumb(list.box, lines, menu.scroll); thumb.shown)
        d2d::rules::list_thumb_cels(list.box, thumb, [&](d2d::rules::ListCel piece) { cel(piece.frame, scroll_x - 1, piece.bottom); });
}

void shade_box(std::vector<std::uint8_t>& framebuffer, int x, int y, int width, int height) {
    for (int row = std::max(0, y); row < std::min(int(kScreenHeight), y + height); ++row)
        for (int column = std::max(0, x); column < std::min(int(kScreenWidth), x + width); ++column) {
            auto* pixel = &framebuffer[(std::size_t(row) * kScreenWidth + std::size_t(column)) * 4];
            pixel[0] = std::uint8_t(pixel[0] / 2); pixel[1] = std::uint8_t(pixel[1] / 2); pixel[2] = std::uint8_t(pixel[2] / 2);
        }
}

void draw_focus(std::vector<std::uint8_t>& framebuffer, const Scene& scene, int x, int base, int width, std::uint32_t now_ms) {
    if (scene.focus16.frames_per_direction() == 0) return;
    const auto& pal = scene.act_pal.entries().empty() ? scene.pal : scene.act_pal;
    const auto& frame = scene.focus16.frame(0, (now_ms / 40) % std::min<std::uint32_t>(7, scene.focus16.frames_per_direction()));
    blit_sprite(framebuffer, frame, pal, x - 24, base + 4 - int(frame.height) + 1);
    blit_sprite(framebuffer, frame, pal, x + width + 2, base + 4 - int(frame.height) + 1);
}

bool update_button(Button& button, const Mouse& mouse, Screen& current_screen,
                   std::atomic<bool>& quit) {
    button.hovered = mouse.x >= button.x && mouse.x < button.x + button.width
             && mouse.y >= button.y && mouse.y < button.y + button.height;
    if (button.hovered && mouse.press_this_frame) {
        button.pressed = true;
        if (g_on_button_press) g_on_button_press();
    }
    if (!mouse.down)                          button.pressed = false;
    if (button.hovered && mouse.release_this_frame) {
        if (button.do_switch) { current_screen = button.goto_screen; return true; }
        if (button.quit)      { quit = true; return true; }
    }
    return false;
}

std::vector<std::string> parse_credits_utf16(std::span<const std::byte> bytes) {
    std::vector<std::string> out;
    std::size_t index = 0;
    // Skip BOM (FF FE) if present.
    if (bytes.size() >= 2 && std::uint8_t(bytes[0]) == 0xFF
                      && std::uint8_t(bytes[1]) == 0xFE) index = 2;
    std::string cur;
    while (index + 1 < bytes.size()) {
        const auto low = std::uint8_t(bytes[index]);
        const auto high = std::uint8_t(bytes[index + 1]);
        index += 2;
        if (high == 0 && low == '\r') continue;         // ignore CR
        if (high == 0 && low == '\n') { out.push_back(std::move(cur)); cur.clear(); continue; }
        // Latin-1 subset: keep low byte, drop chars we can't render.
        if (high == 0 && low >= 32) cur.push_back(char(low));
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

}  // namespace d2d::client
