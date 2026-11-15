// SPDX-License-Identifier: GPL-3.0-or-later
// Touch adapter in logical 800x600 coordinates. No SDL, game rules or saves.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace d2d::client::touch {

enum class Phase { Down, Move, Up, Cancel };
struct Box {
    float x, y, width, height;
    /**
     * Test logical touch coordinates against this box, including its top/left edges and excluding
     * its bottom/right edges.
     */
    [[nodiscard]] bool contains(float point_x, float point_y) const { return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height; }
};

// Extra inset within the centre 800x600 touch band (main.cpp from SDL safe area).
struct SafeInset { float left = 0, top = 0, right = 0, bottom = 0; };
inline SafeInset safe_inset;

inline bool g_touch_classic_hud = false;
inline constexpr int kTouchTreeRight = 488;
inline constexpr int kTouchInvPanelX = 12, kTouchInvPanelY = 48;
/** Scales corner chips and field controls (deviation candidate: touch_ui_scale). */
inline constexpr float kUiScale = 1.2f;
inline constexpr int kNpcLinePad = 14;

// Nominal sizes (positions filled in layout_boxes).
inline constexpr float kChipSize = 48.f, kChipGap = 8.f;
inline constexpr float kBeltSize = 48.f, kSkillSize = 72.f, kAttackSize = 96.f, kPageSize = 40.f;
inline constexpr int kTouchStorePanelX = 400;
inline constexpr float kMoveHitW = 200.f, kMoveHitH = 220.f, kMoveVisualW = 100.f, kMoveVisualH = 110.f;
inline constexpr Box kCancel{ 260, 140, 280, 64 };
inline constexpr Box kMenu{ 0, 0, 48, 48 }, kSkillsBar{ 0, 52, 48, 48 }, kDone{ 0, 0, 52, 48 }, kMap{ 0, 56, 52, 48 };
inline constexpr Box kQuestTrack{ 0, 132, 172, 148 };

struct Layout {
    Box move, move_visual, attack, page, cancel, menu, skills_bar, quest, bind, map, done, interact, quest_track;
    std::array<Box, 4> skills{}, belts{};
};

/** Panel open: move corner chips off the left inventory column. */
inline float panel_chrome_x() { return float(kTouchTreeRight) + 8.f + safe_inset.left; }

inline float ui_scale(float value) { return value * kUiScale; }

inline Box anchor_box(float width, float height, bool left, bool top) {
    constexpr float kPad = 6.f;
    Box box{ 0, 0, width, height };
    if (left) box.x = safe_inset.left + kPad;
    else box.x = 800.f - safe_inset.right - width - kPad;
    if (top) box.y = safe_inset.top + kPad;
    else box.y = 600.f - safe_inset.bottom - height - kPad;
    return box;
}

inline Box placed(Box box) {
    constexpr float kEdge = 24.f;
    if (box.x < kEdge) box.x += safe_inset.left;
    if (box.y < kEdge) box.y += safe_inset.top;
    if (box.x + box.width > 800.f - kEdge) box.x -= safe_inset.right;
    if (box.y + box.height > 600.f - kEdge) box.y -= safe_inset.bottom;
    return box;
}

/** Immortal-style thumb layout with edge anchoring. */
inline Layout layout_boxes() {
    Layout out;
    const float belt = ui_scale(kBeltSize), skill = ui_scale(kSkillSize), attack = ui_scale(kAttackSize), page = ui_scale(kPageSize);
    out.move = anchor_box(ui_scale(kMoveHitW), ui_scale(kMoveHitH), true, false);
    out.move_visual = { out.move.x + (out.move.width - ui_scale(kMoveVisualW)) * .5f,
                        out.move.y + (out.move.height - ui_scale(kMoveVisualH)) * .5f,
                        ui_scale(kMoveVisualW), ui_scale(kMoveVisualH) };
    out.attack = anchor_box(attack, attack, false, false);
    const float ax = out.attack.x, ay = out.attack.y;
    // Page on the attack button's top, not tucked into its lower-left corner.
    out.page = { ax + ui_scale(28.f), ay - ui_scale(52.f), page, page };
    const float chip = ui_scale(kChipSize), gap = ui_scale(kChipGap);
    out.menu = anchor_box(chip, chip, true, true);
    out.skills_bar = { out.menu.x, out.menu.y + out.menu.height + gap, chip, chip };
    out.quest = { out.menu.x, out.skills_bar.y + out.skills_bar.height + gap, chip, chip };
    out.bind = { out.menu.x, out.quest.y + out.quest.height + gap, chip, chip };
    out.done = anchor_box(ui_scale(52.f), ui_scale(48.f), false, true);
    out.map = { out.done.x, out.done.y + ui_scale(56.f), ui_scale(52.f), ui_scale(48.f) };
    const float track_y = out.bind.y + out.bind.height + gap;
    out.quest_track = { safe_inset.left + 6.f, track_y, ui_scale(172.f), 88.f };
    out.cancel = placed(kCancel);
    // Column sits fully above the skill arc (its bottom clears skill 3's top).
    const float pot_x = ax - ui_scale(98.f);
    for (int index = 0; index < 4; ++index)
        out.belts[std::size_t(index)] = { pot_x, ay - ui_scale(414.f) + float(index) * (belt + ui_scale(10.f)), belt, belt };
    // Quarter arc left and above the attack. Centres stay about a diameter apart.
    out.skills[0] = { ax - ui_scale(103.f), ay + ui_scale(7.f), skill, skill };
    out.skills[1] = { ax - ui_scale(186.f), ay - ui_scale(64.f), skill, skill };
    out.skills[2] = { ax - ui_scale(104.f), ay - ui_scale(134.f), skill, skill };
    out.skills[3] = { ax - ui_scale(24.f), ay - ui_scale(174.f), skill, skill };
    out.interact = { ax - ui_scale(268.f), ay - ui_scale(48.f), ui_scale(56.f), ui_scale(56.f) };
    const float move_right = std::min(out.interact.x + out.interact.width + 16.f, out.skills[2].x - 8.f);
    out.move.width = std::max(out.move.width, move_right - out.move.x);
    out.move.height = std::max(out.move.height, 600.f - safe_inset.bottom - out.move.y);
    return out;
}

inline constexpr float kStickOuter = 52.f, kStickInner = 22.f, kSkillRing = 34.f;

inline bool chrome_hit(float x, float y, bool interact_chrome = false) {
    const auto chrome = layout_boxes();
    if (chrome.done.contains(x, y) || chrome.map.contains(x, y) || chrome.menu.contains(x, y)) return true;
    if (chrome.quest.contains(x, y) || chrome.bind.contains(x, y)) return true;
    if (interact_chrome && chrome.interact.contains(x, y)) return true;
    if (chrome.skills_bar.contains(x, y) || chrome.page.contains(x, y) || chrome.move.contains(x, y) || chrome.attack.contains(x, y)) return true;
    for (const auto& box : chrome.skills)
        if (box.contains(x, y)) return true;
    for (const auto& box : chrome.belts)
        if (box.contains(x, y)) return true;
    return false;
}

struct Target { int skill = -1, unit = -1; float x = 0, y = 0; bool left = false, valid = false; };

class Input {
public:
    bool alternate = false, attack_down = false, attack_press = false;
    bool menu = false, done = false;
    int belt = -1, destination = -1;
    float move_x = 0, move_y = 0;
    bool aiming = false, dragged = false, stationary = false;
    float origin_x = 0, origin_y = 0, aim_x = 0, aim_y = 0;
    Target preview;
    std::vector<Target> casts;

    void clear() {
        movement_ = skill_ = attack_ = -1;
        move_x = move_y = 0;
        aiming = dragged = attack_down = attack_press = false;
        casts.clear();
        belt = destination = -1;
        menu = done = false;
    }
    void frame() { attack_press = menu = done = false; belt = destination = -1; casts.clear(); }

    void event(std::int64_t finger, Phase phase, float x, float y, Target target = {}, bool immediate = false, bool editor = false) {
        const auto chrome = layout_boxes();
        if (phase == Phase::Cancel) {
            if (finger == movement_) { movement_ = -1; move_x = move_y = 0; }
            if (finger == attack_) { attack_ = -1; attack_down = false; }
            if (finger == skill_) { skill_ = -1; aiming = false; }
            return;
        }
        if (phase == Phase::Down) {
            if (chrome.done.contains(x, y)) { done = true; return; }
            if (chrome.page.contains(x, y)) { alternate = !alternate; return; }
            if (editor) {
                for (int index = 0; index < 4; ++index)
                    if (chrome.skills[std::size_t(index)].contains(x, y)) { destination = index; return; }
                return;
            }
            if (chrome.menu.contains(x, y)) { menu = true; return; }
            for (int index = 0; index < 4; ++index)
                if (chrome.belts[std::size_t(index)].contains(x, y)) { belt = index; return; }
            if (chrome.move.contains(x, y) && movement_ < 0) { movement_ = finger; move_origin_x_ = x; move_origin_y_ = y; }
            else if (chrome.attack.contains(x, y) && attack_ < 0) { attack_ = finger; attack_press = attack_down = true; }
            else if (skill_ < 0)
                for (const auto& box : chrome.skills)
                    if (box.contains(x, y)) {
                        stationary = immediate;
                        skill_ = finger; aiming = true; dragged = false;
                        origin_x = aim_x = x; origin_y = aim_y = y; preview = target;
                        return;
                    }
        }
        if (finger == movement_) {
            if (phase == Phase::Up) { movement_ = -1; move_x = move_y = 0; }
            else {
                const float dx = x - move_origin_x_, dy = y - move_origin_y_;
                const float length = std::hypot(dx, dy);
                move_x = length > 12 ? dx / std::max(length, 55.f) : 0;
                move_y = length > 12 ? dy / std::max(length, 55.f) : 0;
            }
        }
        if (finger == attack_ && phase == Phase::Up) { attack_ = -1; attack_down = false; }
        if (finger == skill_) {
            aim_x = x; aim_y = y;
            dragged = dragged || (!stationary && std::hypot(x - origin_x, y - origin_y) > 10);
            if (phase == Phase::Up) {
                if (!chrome.cancel.contains(x, y) && x >= 0 && x < 800 && y >= 0 && y < 600 && preview.valid) casts.push_back(preview);
                skill_ = -1; aiming = false;
            }
        }
    }
private:
    std::int64_t movement_ = -1, skill_ = -1, attack_ = -1;
    float move_origin_x_ = 0, move_origin_y_ = 0;
};

} // namespace d2d::client::touch