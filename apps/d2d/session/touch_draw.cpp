// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in touch prototype: separate per-character destinations, shared skill
// eligibility, targeting and gameplay commands. No platform code in the World.
#include "panels/panels.hpp"
#include "session.hpp"

#include "common.hpp"
#include "input/touch/binding.hpp"
#include "input/touch/feedback.hpp"
#include "input/touch/glyph.hpp"
#include "input/touch/icon.hpp"
#include "input/touch/input.hpp"
#include "input/touch/orb.hpp"
#include "input/touch/potions.hpp"
#include "input/touch/theme.hpp"
#include "scene.hpp"
#include "skills/pad.hpp"
#include "wear.hpp"



#include <d2s_items.hpp>
#include <dc6.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <hud_globe.hpp>
#include <palette.hpp>
#include <skills.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace d2d::client {

namespace {

template <typename Items>
touch::PotionSummary potion_summary(const Items& items) {
    std::vector<touch::PotionEntry> entries;
    entries.reserve(items.size());
    for (const auto& item : items)
        entries.push_back({ item.code, item.location == d2d::d2s::item_location::kBelt ? touch::PotionPlace::Belt
                                                                                        : touch::PotionPlace::Other,
                            item.column });
    return touch::summarize_touch_potions(entries);
}

/** True when the contextual interact chrome should accept touches and block field taps. */
bool touch_interact_visible(const ClientSession::PadPick& pick) {
    return pick.kind == ClientSession::PadPick::kItem || pick.kind == ClientSession::PadPick::kThing;
}

/** Short Immortal-style label for the contextual interact button. */
std::string touch_interact_label(const ClientSession& town, const ClientSession::PadPick& pick) {
    if (pick.kind == ClientSession::PadPick::kItem) return "Get";
    if (pick.kind != ClientSession::PadPick::kThing) return {};
    if (pick.id <= -3000) return "Get";
    if (pick.id < 0) return "Use";
    if (town.level && std::size_t(pick.id) < town.level->npcs.size())
        return town.level->npcs[std::size_t(pick.id)].root == "objects" ? "Use" : "Talk";
    return "Use";
}

} // namespace

/**
 * Draw the optional touch controls, assignment destinations and aim/cancel feedback. Reuse the
 * skill preview and current item hints without committing gameplay actions.
 */
void ClientSession::draw_touch(std::vector<std::uint8_t>& framebuffer) {
    if (!touch::touch_interface_visible(touch_enabled, last_device) || !scene || !have_world) return;
    touch::Layout chrome = touch::layout_boxes();
    const int screen_w = fb_width(framebuffer);
    const auto& font = scene->font_small;
    const auto& palette = scene->act_pal.entries().empty() ? scene->pal : scene->act_pal;
    const auto darken = [&](int pixel_x, int pixel_y, std::uint8_t alpha) {
        if (pixel_x < 0 || pixel_x >= screen_w || pixel_y < 0 || pixel_y >= int(kScreenHeight)) return;
        auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4];
        const float scale = 1.f - alpha / 255.f * .72f;
        pixel[0] = std::uint8_t(pixel[0] * scale);
        pixel[1] = std::uint8_t(pixel[1] * scale);
        pixel[2] = std::uint8_t(pixel[2] * scale);
    };
    const auto halo = [&](float centre_x, float centre_y, float radius, std::uint8_t alpha = 16) {
        for (int pixel_y = int(centre_y - radius); pixel_y <= int(centre_y + radius); ++pixel_y)
            for (int pixel_x = int(centre_x - radius); pixel_x <= int(centre_x + radius); ++pixel_x) {
                if (pixel_x < 0 || pixel_x >= screen_w || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                if (std::hypot(float(pixel_x) - centre_x, float(pixel_y) - centre_y) > radius) continue;
                darken(pixel_x, pixel_y, alpha);
            }
    };
    const auto ring = [&](float centre_x, float centre_y, float radius, bool faint = false) {
        for (int pixel_y = int(centre_y - radius - 1); pixel_y <= int(centre_y + radius + 1); ++pixel_y)
            for (int pixel_x = int(centre_x - radius - 1); pixel_x <= int(centre_x + radius + 1); ++pixel_x)
                if (pixel_x >= 0 && pixel_x < screen_w && pixel_y >= 0 && pixel_y < int(kScreenHeight) &&
                    std::abs(std::hypot(float(pixel_x) - centre_x, float(pixel_y) - centre_y) - radius) < 1.5f) {
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4];
                    pixel[0] = faint ? 120 : 199; pixel[1] = faint ? 108 : 179; pixel[2] = faint ? 72 : 119;
                }
    };
    const auto button = [&](touch::Box box, const std::string& text, bool active = false, bool round = false, bool gold = false, bool label = true) {
        if (round) {
            touch::draw_button_face(framebuffer, screen_w, int(kScreenHeight), box.x, box.y, box.width, box.height,
                                    active ? touch::ButtonState::Active : touch::ButtonState::Normal);
        } else {
            for (int pixel_y = int(box.y); pixel_y < int(box.y + box.height); ++pixel_y)
                for (int pixel_x = int(box.x); pixel_x < int(box.x + box.width); ++pixel_x) {
                    if (pixel_x < 0 || pixel_x >= screen_w || pixel_y < 0 || pixel_y >= int(kScreenHeight)) continue;
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4];
                    const bool edge = pixel_x == int(box.x) || pixel_y == int(box.y) || pixel_x == int(box.x + box.width) - 1 || pixel_y == int(box.y + box.height) - 1;
                    if (edge) { pixel[0] = 146; pixel[1] = 113; pixel[2] = 68; }
                    else { pixel[0] = std::uint8_t(pixel[0] * .5f); pixel[1] = std::uint8_t(pixel[1] * .5f); pixel[2] = std::uint8_t(pixel[2] * .5f); }
                }
        }
        if (!label || text.empty()) return;
        std::string clipped = text;
        while (!clipped.empty() && font.measure(clipped) > int(box.width) - 8) clipped.pop_back();
        const int label_x = round ? int(box.x + (box.width - float(font.measure(clipped))) / 2) : int(box.x) + 4;
        const int label_y = round ? int(box.y + box.height * .62f) - 6 : int(box.y) + 12;
        font.draw_tinted(framebuffer, screen_w, kScreenHeight, palette, label_x, label_y, clipped,
                         gold ? 255 : active ? 255 : 199, gold ? 220 : active ? 220 : 179, gold ? 100 : 119);
    };
    const auto glyph_button = [&](touch::Box box, touch::Glyph glyph, const std::string& caption,
                                  bool active = false, bool available = false, bool plus = true) {
        button(box, "", active || available, true, false, false);
        std::array<int, 3> notice_colour{};
        if (available && scene->level_button.frames_per_direction() > 0) {
            const auto& frame = scene->level_button.frame(0, 0);
            notice_colour = touch::notification_colour(std::span<const std::uint8_t>(frame.pixels), palette.entries());
            touch::tint_notification_face(framebuffer, screen_w, int(kScreenHeight), box.x, box.y,
                                          box.width, box.height, notice_colour);
        }
        const bool notice = available && notice_colour != std::array<int, 3>{};
        static const std::array glyphs{
            touch::glyph_shape(touch::Glyph::Inventory), touch::glyph_shape(touch::Glyph::Skills),
            touch::glyph_shape(touch::Glyph::Quest), touch::glyph_shape(touch::Glyph::Bind),
            touch::glyph_shape(touch::Glyph::Map), touch::glyph_shape(touch::Glyph::More),
            touch::glyph_shape(touch::Glyph::Hand), touch::glyph_shape(touch::Glyph::Talk),
            touch::glyph_shape(touch::Glyph::Back), touch::glyph_shape(touch::Glyph::Character),
            touch::glyph_shape(touch::Glyph::Belt)
        };
        const auto& shape = glyphs[std::size_t(glyph)];
        const float icon_size = std::min(box.width, box.height) * .58f;
        const float icon_x = box.x + (box.width - icon_size) * .5f;
        const float icon_y = box.y + 2.f;
        const auto pixel = [&](int x, int y) {
            if (x < 0 || x >= screen_w || y < 0 || y >= int(kScreenHeight) ||
                !touch::inside_icon_circle(x, y, box.x, box.y, box.width, box.height)) return;
            auto* rgba = &framebuffer[(std::size_t(y) * std::size_t(screen_w) + std::size_t(x)) * 4];
            rgba[0] = std::uint8_t(notice ? notice_colour[0] : active ? 220 : 199);
            rgba[1] = std::uint8_t(notice ? notice_colour[1] : active ? 185 : 179);
            rgba[2] = std::uint8_t(notice ? notice_colour[2] : active ? 72 : 119);
        };
        for (const auto& stroke : shape.strokes) {
            const float from_x = icon_x + stroke.from.x * icon_size;
            const float from_y = icon_y + stroke.from.y * icon_size;
            const float to_x = icon_x + stroke.to.x * icon_size;
            const float to_y = icon_y + stroke.to.y * icon_size;
            const int steps = std::max(1, int(std::ceil(std::hypot(to_x - from_x, to_y - from_y) * 1.5f)));
            for (int step = 0; step <= steps; ++step) {
                const float fraction = float(step) / float(steps);
                pixel(int(std::lround(from_x + (to_x - from_x) * fraction)),
                      int(std::lround(from_y + (to_y - from_y) * fraction)));
            }
        }
        for (const auto dot : shape.dots) {
            const float centre_x = icon_x + dot.x * icon_size;
            const float centre_y = icon_y + dot.y * icon_size;
            for (int y = int(centre_y) - 1; y <= int(centre_y) + 1; ++y)
                for (int x = int(centre_x) - 1; x <= int(centre_x) + 1; ++x)
                    if (std::hypot(float(x) - centre_x, float(y) - centre_y) <= 1.f) pixel(x, y);
        }
        if (available && plus) {
            // Keep the points plus inside the rim.
            const int plus_x = int(box.x + box.width * .75f), plus_y = int(box.y + box.height * .27f);
            for (int offset = -3; offset <= 3; ++offset) {
                pixel(plus_x + offset, plus_y); pixel(plus_x, plus_y + offset);
            }
        }
        if (!caption.empty()) {
            const int caption_width = font.measure(caption);
            font.draw_tinted(framebuffer, screen_w, kScreenHeight, palette,
                             int(box.x + (box.width - float(caption_width)) * .5f), int(box.y + box.height * .70f), caption,
                             std::uint8_t(notice ? notice_colour[0] : active ? 255 : 199),
                             std::uint8_t(notice ? notice_colour[1] : active ? 220 : 179),
                             std::uint8_t(notice ? notice_colour[2] : active ? 100 : 119));
        }
    };
    const auto blit_skill_icon = [&](int skill_id, int pixel_x, int pixel_y) {
        const auto* info = scene->skills.get(skill_id);
        if (!info) return;
        int icon_class = -1;
        for (int column = 0; column < 7; ++column)
            if (info->cls == d2d::rules::kClassCode[std::size_t(column)]) icon_class = column;
        const auto& art = icon_class >= 0 ? scene->skill_icons[std::size_t(icon_class)] : scene->generic_skill_icons;
        if (info->icon >= 0 && std::uint32_t(info->icon) < art.frames_per_direction())
            blit_sprite(framebuffer, art.frame(0, std::uint32_t(info->icon)), palette, pixel_x, pixel_y);
    };
    const auto blit_frame_scaled = [&](const touch::Box& box, const d2d::dc6::Frame& frame, const d2d::palette::Palette& pal, float scale) {
        if (scale <= 0.f || frame.width == 0 || frame.height == 0) return;
        const int dest_w = std::max(1, int(std::lround(float(frame.width) * scale)));
        const int dest_h = std::max(1, int(std::lround(float(frame.height) * scale)));
        const int dst_x = int(box.x + (box.width - float(dest_w)) * .5f);
        const int dst_y = int(box.y + (box.height - float(dest_h)) * .5f);
        for (int pixel_y = 0; pixel_y < dest_h; ++pixel_y) {
            const int dy = dst_y + pixel_y;
            if (dy < 0 || dy >= int(kScreenHeight)) continue;
            const int src_y = std::min(int(frame.height) - 1, int(float(pixel_y) / scale));
            for (int pixel_x = 0; pixel_x < dest_w; ++pixel_x) {
                const int dx = dst_x + pixel_x;
                if (dx < 0 || dx >= screen_w) continue;
                const int src_x = std::min(int(frame.width) - 1, int(float(pixel_x) / scale));
                const auto idx = frame.pixels[std::size_t(src_y) * frame.width + std::size_t(src_x)];
                if (idx == 0) continue;
                const auto colour = pal[idx];
                auto* pixel = &framebuffer[(std::size_t(dy) * std::size_t(screen_w) + std::size_t(dx)) * 4];
                pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; pixel[3] = colour.a;
            }
        }
    };
    const auto blit_skill_icon_centered = [&](const touch::Box& box, int skill_id, float rim = 3.f) {
        const auto* info = scene->skills.get(skill_id);
        if (!info || info->icon < 0) return;
        int icon_class = -1;
        for (int column = 0; column < 7; ++column)
            if (info->cls == d2d::rules::kClassCode[std::size_t(column)]) icon_class = column;
        const auto& art = icon_class >= 0 ? scene->skill_icons[std::size_t(icon_class)] : scene->generic_skill_icons;
        if (std::uint32_t(info->icon) >= art.frames_per_direction()) return;
        const auto& frame = art.frame(0, std::uint32_t(info->icon));
        const auto destination = touch::circular_icon_rect(box.x, box.y, box.width, box.height,
                                                           float(frame.width), float(frame.height), rim);
        if (destination.scale <= 0.f) return;
        for (int pixel_y = 0; pixel_y < destination.height; ++pixel_y) {
            const int dy = destination.y + pixel_y;
            if (dy < 0 || dy >= int(kScreenHeight)) continue;
            const int src_y = std::min(int(frame.height) - 1, int(float(pixel_y) / destination.scale));
            for (int pixel_x = 0; pixel_x < destination.width; ++pixel_x) {
                const int dx = destination.x + pixel_x;
                if (dx < 0 || dx >= screen_w ||
                    !touch::inside_icon_circle(dx, dy, box.x, box.y, box.width, box.height, rim)) continue;
                const int src_x = std::min(int(frame.width) - 1, int(float(pixel_x) / destination.scale));
                const auto index = frame.pixels[std::size_t(src_y) * frame.width + std::size_t(src_x)];
                if (index == 0) continue;
                const auto colour = palette[index];
                auto* pixel = &framebuffer[(std::size_t(dy) * std::size_t(screen_w) + std::size_t(dx)) * 4];
                pixel[0] = colour.r; pixel[1] = colour.g; pixel[2] = colour.b; pixel[3] = colour.a;
            }
        }
    };
    const auto draw_feedback = [&] {
        const std::array<touch::Box, 17> boxes{ chrome.skills[0], chrome.skills[1], chrome.skills[2], chrome.skills[3],
            chrome.attack, chrome.healing, chrome.mana, chrome.menu, chrome.skills_bar, chrome.stats,
            chrome.quest, chrome.bind, chrome.done, chrome.map, chrome.page, chrome.belt_more, chrome.interact };
        static_assert(std::tuple_size_v<decltype(boxes)> == touch::FeedbackState::kButtons);
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            if (store.npc >= 0 && index >= 7 && index < 12) continue;
            if (index >= 15 && !touch_shows_field_controls()) continue;
            if (index == 14 && !touch_shows_field_controls() && touch_bind == TouchBindPhase::Off
                && touch_assign == skillpad::kUnset) continue;
            const auto sample = touch_input.feedback.sample(index, now_ms);
            if (!sample.active) continue;
            const auto box = boxes[index];
            const float centre_x = box.x + box.width / 2, centre_y = box.y + box.height / 2;
            const float radius = box.width / 2 + 2.f + (sample.kind == touch::FeedbackKind::Pressed ? 0.f : sample.phase * 5.f);
            const float strength = sample.strength;
            for (int pixel_y = int(centre_y - radius - 1); pixel_y <= int(centre_y + radius + 1); ++pixel_y)
                for (int pixel_x = int(centre_x - radius - 1); pixel_x <= int(centre_x + radius + 1); ++pixel_x) {
                    if (pixel_x < 0 || pixel_x >= screen_w || pixel_y < 0 || pixel_y >= int(kScreenHeight)
                        || std::abs(std::hypot(float(pixel_x) - centre_x, float(pixel_y) - centre_y) - radius) > 1.5f) continue;
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4];
                    const bool rejected = sample.kind == touch::FeedbackKind::Rejected;
                    const bool cancelled = sample.kind == touch::FeedbackKind::Cancelled;
                    const float red = cancelled ? 135.f : rejected ? 155.f : 215.f;
                    const float green = cancelled ? 135.f : rejected ? 100.f : 185.f;
                    const float blue = cancelled ? 135.f : rejected ? 85.f : 115.f;
                    pixel[0] = std::uint8_t(pixel[0] * (1.f - strength) + red * strength);
                    pixel[1] = std::uint8_t(pixel[1] * (1.f - strength) + green * strength);
                    pixel[2] = std::uint8_t(pixel[2] * (1.f - strength) + blue * strength);
                }
        }
    };
    const auto draw_left_chips = [&] {
        glyph_button(chrome.menu, touch::Glyph::Inventory, "Inv", inv_open);
        glyph_button(chrome.skills_bar, touch::Glyph::Skills, "Skl", tree_open,
                     touch::points_available(character.stats.get(d2d::d2s::kSkillPts)));
        glyph_button(chrome.stats, touch::Glyph::Character, "Stats", char_open,
                     touch::points_available(character.stats.get(d2d::d2s::kStatPts)));
        glyph_button(chrome.quest, touch::Glyph::Quest, "Quest", quest_log.open, quest_log.touch_notice.active(), false);
        glyph_button(chrome.bind, touch::Glyph::Bind, touch_bind != TouchBindPhase::Off ? "Done" : "Bind",
                     touch_bind != TouchBindPhase::Off);
    };
    const auto draw_sheet = [&] {
        if (!touch_sheet.open) return;
        if (touch_sheet.show_use) button(touch::translate_legacy_box(touch_sheet.use), "Use", touch_sheet.chosen == touch::ItemAction::Use);
        if (touch_sheet.show_sell) button(touch::translate_legacy_box(touch_sheet.sell), "Sell", touch_sheet.trade_action == touch::ItemAction::Sell);
        if (touch_sheet.show_buy) button(touch::translate_legacy_box(touch_sheet.buy), "Buy", touch_sheet.trade_action == touch::ItemAction::Buy);
        if (touch_sheet.show_fill) button(touch::translate_legacy_box(touch_sheet.fill), "Fill", touch_sheet.chosen == touch::ItemAction::Fill);
        if (touch_sheet.show_confirm())
            button(touch::translate_legacy_box(touch_sheet.confirm), touch_sheet.identifying_target ? "Identify" : "Confirm", touch_sheet.confirming);
        button(touch::translate_legacy_box(touch_sheet.cancel), "Cancel");
    };
    if (pad_field() && touch_bind == TouchBindPhase::Off && touch_assign == skillpad::kUnset && skillbar.picking != 3)
        glyph_button(chrome.done, touch::Glyph::More, "...");
    else
        glyph_button(chrome.done, touch::Glyph::Back,
                     touch_bind != TouchBindPhase::Off || touch_assign != skillpad::kUnset || skillbar.picking == 3 ? "Done" : "Back");
    if (touch_bind != TouchBindPhase::Off) {
        draw_left_chips();
        glyph_button(chrome.map, touch::Glyph::Map, "Map", automap.open);
        for (int index = 0; index < 4; ++index) {
            const touch::Box& skill_box = chrome.skills[std::size_t(index)];
            const float centre_x = skill_box.x + skill_box.width * .5f, centre_y = skill_box.y + skill_box.height * .5f;
            halo(centre_x, centre_y, skill_box.width * .56f, 18);
            ring(centre_x, centre_y, skill_box.width * .52f, index == touch_bind_slot);
            const int id = touch_bound_skill(index);
            const auto* info = scene->skills.get(id);
            if (info && info->icon >= 0) { button(skill_box, "", index == touch_bind_slot, true); blit_skill_icon_centered(skill_box, id, 4.f); }
            else button(skill_box, skillpad::binds_action(id) ? std::string(skillpad::action_label(id, false)) : std::to_string(index + 1),
                        index == touch_bind_slot, true);
        }
        {
            const touch::Box& attack_box = chrome.attack;
            const float centre_x = attack_box.x + attack_box.width * .5f, centre_y = attack_box.y + attack_box.height * .5f;
            halo(centre_x, centre_y, attack_box.width * .56f, 18);
            ring(centre_x, centre_y, attack_box.width * .52f, touch_bind_slot == touch::kAttackBind);
            const int id = touch_bound_skill(touch::kAttackBind);
            const auto* info = scene->skills.get(id);
            if (info && info->icon >= 0) { button(attack_box, "", touch_bind_slot == touch::kAttackBind, true); blit_skill_icon_centered(attack_box, id, 4.f); }
            else button(attack_box, skillpad::binds_action(id) ? std::string(skillpad::action_label(id, false)) : "-",
                        touch_bind_slot == touch::kAttackBind, true);
        }
        if (touch_bind == TouchBindPhase::PickSkill && !touch_bind_ids.empty()) {
            const float wheel_cx = touch::to_viewport_x(380.f), wheel_cy = 270.f;
            const touch::BindingPicker touch_binding{ touch_bind_wheel_page, touch_bind_wheel_at };
            const auto page = touch_binding.visible(int(touch_bind_ids.size()));
            for (int slot = 0; slot < page.shown; ++slot) {
                const int index = std::clamp(page.start + slot, 0, int(touch_bind_ids.size()) - 1);
                const float angle = skillpad::wedge_center(slot, page.shown);
                const int icon_x = int(wheel_cx + std::cos(angle) * skillpad::kBindWheelRx);
                const int icon_y = int(wheel_cy + std::sin(angle) * skillpad::kBindWheelRy);
                const int id = touch_bind_ids[std::size_t(index)];
                if (index == touch_binding.selected) ring(float(icon_x), float(icon_y), 22.f, false);
                if (id < 0) {
                    const std::string label(skillpad::action_label(id, true));
                    font.draw(framebuffer, screen_w, kScreenHeight, palette, icon_x - font.measure(label) / 2, icon_y - 6, label);
                } else blit_skill_icon(id, icon_x - 12, icon_y - 12);
            }
            ring(wheel_cx, wheel_cy, 18.f, true);
            const int pages = touch::BindingPicker::pages(int(touch_bind_ids.size()));
            if (pages > 1) {
                button(touch::translate_legacy_box(touch::kBindingPrevious), "Prev", false, true);
                button(touch::translate_legacy_box(touch::kBindingNext), "Next", false, true);
            }
            const std::string page_label = std::to_string(touch_binding.page + 1) + "/" + std::to_string(pages);
            font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(322.f)) - font.measure(page_label) / 2, 474, page_label);
        }
        button(chrome.page, touch_input.alternate ? "II" : "I", false, true);
        font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(280.f)), 90, touch_feedback);
        draw_feedback();
        return;
    }
    if (touch_assign != skillpad::kUnset) {
        button(chrome.menu, "Assign Inventory");
        button(chrome.page, touch_input.alternate ? "II" : "I", false, true);
        const auto found = touch_sheets.find(sheet_key(character.name));
        for (int index = 0; index < 4; ++index) {
            const int id = found == touch_sheets.end() ? -1 : found->second.slot(touch_input.alternate, index);
            const auto* info = scene->skills.get(id);
            const touch::Box& skill_box = chrome.skills[std::size_t(index)];
            const float centre_x = skill_box.x + skill_box.width * .5f, centre_y = skill_box.y + skill_box.height * .5f;
            halo(centre_x, centre_y, skill_box.width * .56f, 22);
            ring(centre_x, centre_y, skill_box.width * .52f, false);
            button(skill_box, std::to_string(index + 1) + ": " + (info ? info->name : "Empty"), false, true, true);
            if (info) blit_skill_icon_centered(skill_box, id);
        }
        {
            const int id = found == touch_sheets.end() ? skillpad::kUnset : found->second.trigger(touch_input.alternate);
            const auto* info = scene->skills.get(id);
            const touch::Box& attack_box = chrome.attack;
            halo(attack_box.x + attack_box.width * .5f, attack_box.y + attack_box.height * .5f, attack_box.width * .56f, 22);
            ring(attack_box.x + attack_box.width * .5f, attack_box.y + attack_box.height * .5f, attack_box.width * .52f, false);
            button(attack_box, info ? info->name : "Attack", false, true, true);
            if (info) blit_skill_icon_centered(attack_box, id);
        }
        const auto* info = scene->skills.get(touch_assign);
        font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(310.f)), 170, "Assign: " + (info ? info->name : std::string(skillpad::action_label(touch_assign, true))));
        font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(310.f)), 195, touch_feedback);
        draw_feedback();
        return;
    }
    if (tree_open) {
        if (touch_hides_classic_hud()) {
            draw_left_chips();
            glyph_button(chrome.map, touch::Glyph::Map, "Map", automap.open);
        }
        font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(12.f)), 118, "Skills . Done closes");
        if (touch_learn_skill >= 0) {
            button(touch::translate_legacy_box(touch::kTreeLearnConfirm), "Confirm");
            button(touch::translate_legacy_box(touch::kTreeLearnCancel), "Cancel");
            font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(16.f)), 472, "Learn this skill?");
        }
        if (touch_selected >= 0) {
            button(touch::translate_legacy_box(touch::kTreeAssign), "Assign to touch button");
        }
        draw_feedback();
        return;
    }
    if (!pad_field()) {
        draw_touch_trade_hint(framebuffer);
        if (inv_open) font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(12.f)), 36, "Inventory . Done closes");
        const auto drop = touch_trade_drop();
        std::string_view hint = touch_item_hint;
        if (identify_with >= 0) hint = "Tap an unidentified item . Back cancels";
        if (drop.width > 0.f) {
            const bool inside = drop.contains(touch_pointer.x, touch_pointer.y);
            const bool buy = touch_stock.tab >= 0;
            hint = inside ? (buy ? "Release to buy" : "Release to sell")
                          : (buy ? "Drag to the inventory grid" : "Drag to the vendor grid");
            const auto screen_drop = touch::translate_legacy_box(drop);
            const int left = int(screen_drop.x), right = left + int(screen_drop.width);
            const int top = int(screen_drop.y), bottom = top + int(screen_drop.height);
            const int stroke = inside ? 4 : 2;
            for (int yy = std::max(top, 0); yy < std::min(bottom, int(kScreenHeight)); ++yy)
                for (int xx = std::max(left, 0); xx < std::min(right, screen_w); ++xx) {
                    if (xx >= left + stroke && xx < right - stroke && yy >= top + stroke && yy < bottom - stroke) continue;
                    auto* pixel = &framebuffer[(std::size_t(yy) * std::size_t(screen_w) + std::size_t(xx)) * 4];
                    pixel[0] = inside ? 110 : 145;
                    pixel[1] = inside ? 242 : 137;
                    pixel[2] = inside ? 151 : 116;
                }
        }
        font.draw(framebuffer, screen_w, kScreenHeight, palette, screen_w / 2 - font.measure(hint) / 2, 500, hint);
        if (touch_hides_classic_hud()) {
            if (store.npc < 0) draw_left_chips();
            glyph_button(chrome.map, touch::Glyph::Map, "Map", automap.open);
        }
        draw_sheet();
        if (touch_hold_fraction > 0.f && touch_item_finger != -1)
            ring(touch::to_viewport_x(touch_item_x), touch_item_y, 22.f + 18.f * touch_hold_fraction, false);
        draw_feedback();
        return;
    }
    const auto found = touch_sheets.find(sheet_key(character.name));
    for (int index = 0; index < 4; ++index) {
        const int skill = found == touch_sheets.end() ? skillpad::kEmpty : found->second.slot(touch_input.alternate, index);
        touch_previews[std::size_t(index)] = resolve_skill(touch_player, skill);
    }
    if (touch_hides_classic_hud()) {
        using namespace d2d::d2s;
        const auto& stats = character.stats;
        const int stamina = int(stats.values[kStamina] >> 8), max_stamina = std::max(1, int(stats.values[kMaxStamina] >> 8));
        const touch::Box stamina_bar = touch::stamina_bar_rect(chrome);
        const int bar_x = int(stamina_bar.x), bar_y = int(stamina_bar.y);
        const int half = int(stamina_bar.width), bar_h = int(stamina_bar.height);
        constexpr int kOrbSize = 56;
        const auto orb = [&](touch::Box box, bool mana) {
            const int panel_frame = mana ? 5 : 0;
            if (scene->ctrl_panel.frames_per_direction() > std::uint32_t(panel_frame)) {
                const auto& panel = scene->ctrl_panel.frame(0, std::uint32_t(panel_frame));
                blit_frame_scaled(box, panel, palette, std::min(box.width / float(std::max(1u, panel.width)), box.height / float(std::max(1u, panel.height))));
            }
            const int fill_frame = mana ? 1 : view.poisoned ? 2 : 0;
            const int glass_frame = mana ? 1 : 0;
            if (scene->globes.frames_per_direction() <= std::uint32_t(fill_frame)
                || scene->globe_glass.frames_per_direction() <= std::uint32_t(glass_frame)) return;
            const int stat = mana ? kMana : kLife, max_stat = mana ? kMaxMana : kMaxLife;
            const auto maximum = stats.values[std::size_t(max_stat)];
            const auto shown = stats.values[std::size_t(stat)];
            int rows = maximum > 0 ? int(std::clamp<std::int64_t>(shown * 80 / maximum, 0, 80)) : 0;
            if (!mana && rows == 1) rows = 2;
            const auto frame = [](const d2d::dc6::Frame& source) {
                return touch::OrbFrame{ source.pixels, int(source.width), int(source.height) };
            };
            const auto& fill = scene->globes.frame(0, std::uint32_t(fill_frame));
            const auto& preview = scene->globes.frame(0, mana ? 1u : 0u);
            const auto& glass = scene->globe_glass.frame(0, std::uint32_t(glass_frame));
            touch::draw_indexed_orb(framebuffer, screen_w, int(kScreenHeight), screen_w * 4, int(box.x + (box.width - kOrbSize) * .5f), int(box.y + box.height - kOrbSize - 4.f), kOrbSize,
                frame(fill), rows, frame(preview), d2d::rules::globe_preview_rows(mana ? view.mana_preview : view.life_preview), frame(glass),
                [&](std::uint8_t index) { const auto colour = palette[index]; return touch::OrbColour{ colour.r, colour.g, colour.b, colour.a }; });
        };
        orb(chrome.life_orb, false);
        orb(chrome.mana_orb, true);
        const auto bar = [&](int x, int y, int width, int fill, std::string_view label, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            const int used = std::clamp(fill, 0, width);
            for (int pixel_y = y; pixel_y < y + bar_h; ++pixel_y)
                for (int pixel_x = x; pixel_x < x + width; ++pixel_x) {
                    if (pixel_x < 0 || pixel_y < 0 || pixel_x >= screen_w || pixel_y >= int(kScreenHeight)) continue;
                    auto* pixel = &framebuffer[(std::size_t(pixel_y) * std::size_t(screen_w) + std::size_t(pixel_x)) * 4];
                    const bool edge = pixel_x == x || pixel_y == y || pixel_x == x + width - 1 || pixel_y == y + bar_h - 1;
                    const bool full = pixel_x < x + used;
                    if (edge) {
                        pixel[0] = 199; pixel[1] = 179; pixel[2] = 119;
                    } else if (full) {
                        pixel[0] = red; pixel[1] = green; pixel[2] = blue;
                    } else {
                        pixel[0] = std::uint8_t(pixel[0] * .25f); pixel[1] = std::uint8_t(pixel[1] * .25f); pixel[2] = std::uint8_t(pixel[2] * .25f);
                    }
                }
            if (!label.empty()) font.draw(framebuffer, screen_w, kScreenHeight, palette, x, y - 12, std::string(label));
        };
        const int row_y = bar_y;
        const auto requested = touch::stamina_colour(stats.values[kStamina], stats.values[kMaxStamina], view.stamina_blue);
        int best_distance = 1 << 30;
        std::uint8_t nearest = 0;
        const auto colours = palette.entries();
        for (std::size_t index = 0; index < 256 && index < colours.size(); ++index) {
            const auto& colour = colours[index];
            const int red = colour.r - requested[0], green = colour.g - requested[1], blue = colour.b - requested[2];
            const int distance = red * red + green * green + blue * blue;
            if (distance < best_distance) { best_distance = distance; nearest = std::uint8_t(index); }
        }
        const auto stamina_rgb = palette[nearest];
        // Keep a sliver visible at zero so an empty stamina bar is still identifiable.
        bar(bar_x, row_y, half, std::max(2, stamina * half / max_stamina),
            "", stamina_rgb.r, stamina_rgb.g, stamina_rgb.b);
        int xp_fill = 0;
        const int lvl = int(stats.get(kLevel));
        if (lvl >= 1 && std::size_t(lvl) < scene->exp_next.size()) {
            const auto next = scene->exp_next[std::size_t(lvl)], prev = scene->exp_next[std::size_t(lvl - 1)];
            if (next > prev) {
                const std::int64_t num = stats.values[kExp] > prev ? stats.values[kExp] - prev : 0;
                const std::int64_t den = next - prev;
                xp_fill = int(num * half / den);
                if (xp_fill > half) xp_fill = half;
            }
        }
        const auto xp_colour = palette[0xff];
        bar(bar_x + half + 4, row_y, half, xp_fill, "", xp_colour.r, xp_colour.g, xp_colour.b);
        if (!quest_log.open) {
            struct QuestCard { std::string title, line; };
            std::array<QuestCard, 2> cards{};
            int count = 0;
            const int act = view_act();
            for (const auto& entry : kQuestLog) {
                if (entry.act != act || count >= 2) continue;
                const auto text = quest_text(quest_bits(), entry.quest, quest_state());
                if (text.shown != 3 && text.shown != 1) continue;
                QuestCard& card = cards[std::size_t(count++)];
                card.title = string_id(*scene, std::uint16_t(entry.name));
                if (text.string) {
                    const std::string body = string_id(*scene, std::uint16_t(text.string));
                    card.line = body.size() > 42 ? body.substr(0, 41) + "." : body;
                }
            }
            if (count > 0) {
                for (int pixel_y = int(chrome.quest_track.y); pixel_y < int(chrome.quest_track.y + chrome.quest_track.height); ++pixel_y)
                    for (int pixel_x = int(chrome.quest_track.x); pixel_x < int(chrome.quest_track.x + chrome.quest_track.width); ++pixel_x)
                        if (pixel_x >= 0 && pixel_y >= 0 && pixel_x < screen_w && pixel_y < int(kScreenHeight)) darken(pixel_x, pixel_y, 120);
                float card_y = chrome.quest_track.y + 6.f;
                for (int index = 0; index < count; ++index) {
                    font.draw(framebuffer, screen_w, kScreenHeight, palette, int(chrome.quest_track.x) + 6, int(card_y), cards[std::size_t(index)].title);
                    if (!cards[std::size_t(index)].line.empty())
                        font.draw(framebuffer, screen_w, kScreenHeight, palette, int(chrome.quest_track.x) + 6, int(card_y) + 16, cards[std::size_t(index)].line);
                    card_y += 72.f;
                }
            }
        }
    }
    if (!touch_shows_field_controls()) {
        if (touch_hides_classic_hud()) draw_left_chips();
        glyph_button(chrome.map, touch::Glyph::Map, "Map", automap.open);
        draw_feedback();
        return;
    }
    draw_left_chips();
    glyph_button(chrome.map, touch::Glyph::Map, "Map", automap.open);
    const touch::Box& move_pad = chrome.move_visual;
    const float stick_x = move_pad.x + move_pad.width * .5f;
    const float stick_y = move_pad.y + move_pad.height * .55f;
    if (touch_input.move_held()) {
        halo(stick_x, stick_y, touch::kStickOuter + 4, 6);
        ring(stick_x, stick_y, touch::kStickOuter, true);
        ring(stick_x, stick_y, touch::kStickInner, true);
        ring(stick_x + touch_input.move_x * 32, stick_y + touch_input.move_y * 32, 16);
    }
    const auto potions = potion_summary(character.items);
    const auto potion_button = [&](touch::Box box, std::optional<int> slot, int count) {
        touch::draw_button_face(framebuffer, screen_w, int(kScreenHeight), box.x, box.y, box.width, box.height,
                                slot ? touch::ButtonState::Normal : touch::ButtonState::Disabled);
        if (slot) for (const auto& item : character.items) {
            if (item.location != d2d::d2s::item_location::kBelt || item.column != *slot) continue;
            if (const auto* sprite = scene->item_sprite(item); sprite && sprite->frames_per_direction() > 0) {
                const auto& frame = sprite->frame(0, 0);
                blit_frame_scaled(box, frame, scene->item_pal(item, palette),
                    touch::fit_icon_scale(box.width, box.height, float(frame.width), float(frame.height), .18f));
            }
            break;
        }
        const auto text = std::to_string(count);
        font.draw(framebuffer, screen_w, kScreenHeight, palette, int(box.x + box.width - float(font.measure(text)) - 4.f), int(box.y + box.height - 12.f), text);
    };
    potion_button(chrome.healing, potions.strongest_healing_slot, potions.healing);
    potion_button(chrome.mana, potions.strongest_mana_slot, potions.mana);
    glyph_button(chrome.belt_more, touch::Glyph::Belt, touch_input.belt_expanded ? "Close" : "Belt", touch_input.belt_expanded);
    if (touch_input.belt_expanded) for (int index = 0; index < 4; ++index) {
        const touch::Box belt = chrome.belts[std::size_t(index)];
        const d2d::d2s::Item* belt_item = nullptr;
        for (const auto& item : character.items)
            if (item.location == d2d::d2s::item_location::kBelt && item.column == index) { belt_item = &item; break; }
        halo(belt.x + belt.width * .5f, belt.y + belt.height * .5f, belt.width * .48f, 10);
        const bool has_icon = belt_item && scene->item_sprite(*belt_item);
        button(belt, has_icon ? "" : std::to_string(index + 1), false, true, false, !has_icon);
        if (belt_item) {
            if (const auto* spr = scene->item_sprite(*belt_item); spr && spr->frames_per_direction() > 0) {
                const auto& frame = spr->frame(0, 0);
                const auto& item_pal = scene->item_pal(*belt_item, palette);
                const float max_w = belt.width * .80f, max_h = belt.height * .80f;
                if (float(frame.width) > max_w || float(frame.height) > max_h)
                    blit_frame_scaled(belt, frame, item_pal, std::min(max_w / float(std::max(1u, frame.width)), max_h / float(std::max(1u, frame.height))));
                else {
                    const auto origin = touch::centred_blit_origin(belt, float(frame.width), float(frame.height));
                    blit_sprite(framebuffer, frame, item_pal, int(origin.first), int(origin.second));
                }
            }
        }
    }
    const touch::Box attack_box = chrome.attack;
    halo(attack_box.x + attack_box.width * .5f, attack_box.y + attack_box.height * .5f, attack_box.width * .46f);
    const int attack_skill = touch_bound_skill(touch::kAttackBind);
    const auto attack_label = skillpad::binds_action(attack_skill) ? skillpad::action_label(attack_skill, false)
        : attack_skill == skillpad::kEmpty ? "-" : "";
    button(attack_box, std::string(attack_label), touch_input.attack_down, true);
    blit_skill_icon_centered(attack_box, attack_skill, 4.f);
    if (touch_input.attack_down) touch::shade_button_art(framebuffer, screen_w, int(kScreenHeight), attack_box.x, attack_box.y, attack_box.width, attack_box.height, touch::ButtonState::Pressed);
    button(chrome.page, touch_input.alternate ? "II" : "I", false, true);
    if (touch_interact_visible(touch_interact_pick)) {
        const touch::Box& interact = chrome.interact;
        halo(interact.x + interact.width * .5f, interact.y + interact.height * .5f, interact.width * .62f, 20);
        ring(interact.x + interact.width * .5f, interact.y + interact.height * .5f, interact.width * .48f, true);
        const auto caption = touch_interact_label(*this, touch_interact_pick);
        glyph_button(interact, caption == "Talk" ? touch::Glyph::Talk : touch::Glyph::Hand, caption, true);
    }
    for (int index = 0; index < 4; ++index) {
        const auto& target = touch_previews[std::size_t(index)];
        const auto* info = scene->skills.get(target.skill);
        const touch::Box skill_box = chrome.skills[std::size_t(index)];
        const float skill_cx = skill_box.x + skill_box.width * .5f, skill_cy = skill_box.y + skill_box.height * .5f;
        halo(skill_cx, skill_cy, skill_box.width * .46f, 10);
        ring(skill_cx, skill_cy, skill_box.width * .48f, true);
        const bool has_icon = info && info->icon >= 0;
        if (has_icon) button(skill_box, "", (info && info->id == view.aura) || (touch_input.aiming && touch_input.preview.skill == target.skill), true);
        if (!has_icon) {
            const std::string skill_label = info ? (info->name.size() > 6 ? info->name.substr(0, 5) + "." : info->name) :
                                                  target.skill < -2 ? std::string(skillpad::action_label(target.skill, false)) : "-";
            button(skill_box, skill_label, info && info->id == view.aura, true);
        } else if (info && info->id == view.aura)
            ring(skill_cx, skill_cy, skill_box.width * .48f + 2.f, false);
        if (has_icon) {
            blit_skill_icon_centered(skill_box, target.skill, 4.f);
            if (touch_input.aiming && touch_input.preview.skill == target.skill)
                touch::shade_button_art(framebuffer, screen_w, int(kScreenHeight), skill_box.x, skill_box.y, skill_box.width, skill_box.height, touch::ButtonState::Pressed);
        }
    }
    if (view.aura > 0) {
        const auto* aura = scene->skills.get(view.aura);
        if (aura) font.draw(framebuffer, screen_w, kScreenHeight, palette, int(chrome.page.x), int(chrome.page.y) - 14, aura->name);
    }
    if (touch_attack_pick.kind == PadPick::kMonster) {
        const auto monster = std::ranges::find(view.monsters, touch_attack_pick.id, &Monster::id);
        if (monster != view.monsters.end()) {
            const float monster_x = monster->unit.x, monster_y = monster->unit.y;
            const int screen_x = screen_w / 2 + int((monster_x - cam_x - monster_y + cam_y) * kIsoW / 2);
            const int screen_y = kViewY + int((monster_x - cam_x + monster_y - cam_y) * kIsoH / 2);
            ring(float(screen_x), float(screen_y), 14, false);
        }
    }
    if (touch_input.aiming) {
        const auto& target = touch_input.preview;
        if (target.unit >= 0 && !touch_skill_hold.cancelled) {
            const auto monster = std::ranges::find(view.monsters, target.unit, &Monster::id);
            if (monster != view.monsters.end()) {
                const float unit_x = monster->unit.x, unit_y = monster->unit.y;
                const int mark_x = screen_w / 2 + int((unit_x - cam_x - unit_y + cam_y) * kIsoW / 2);
                const int mark_y = kViewY + int((unit_x - cam_x + unit_y - cam_y) * kIsoH / 2);
                ring(float(mark_x), float(mark_y), 16, false);
            }
        }
        const auto* info = scene->skills.get(target.skill);
        const auto message = (info ? info->name : std::string("Skill"))
            + (touch_skill_hold.cancelled ? " - release to cancel" : target.valid ? " - release to cast" : " - no eligible target");
        if (touch_skill_hold.cancelled)
            font.draw_tinted(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(300.f)), 120, message, 235, 110, 90);
        else
            font.draw(framebuffer, screen_w, kScreenHeight, palette, int(touch::to_viewport_x(300.f)), 120, message);
    }
    draw_sheet();
    draw_feedback();
}

} // namespace d2d::client
