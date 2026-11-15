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

// Immortal-inspired HUD: thumb corners, clear center (x roughly 220–560, y 120–380).
inline constexpr Box kMove{ 12, 392, 152, 176 }, kAttack{ 672, 468, 108, 108 }, kPage{ 588, 308, 72, 40 },
    kCancel{ 260, 140, 280, 64 }, kMenu{ 12, 12, 48, 48 }, kDone{ 684, 12, 56, 48 }, kMap{ 740, 12, 48, 48 },
    kSkillsBar{ 12, 64, 48, 48 };
inline constexpr Box kSkills[4] = { { 592, 488, 76, 76 }, { 568, 412, 76, 76 }, { 600, 332, 76, 76 }, { 672, 288, 76, 76 } },
    kInteract{ 540, 368, 64, 64 };
inline constexpr Box kBelts[4] = { { 12, 252, 52, 42 }, { 68, 252, 52, 42 }, { 12, 300, 52, 42 }, { 68, 300, 52, 42 } };
inline constexpr int kTouchInvPanelX = 12, kTouchInvPanelY = 48;

// Extra inset, in logical pixels, past the 12px margin the boxes already have.
// Zero leaves every box where it is. main.cpp fills this from SDL_GetWindowSafeArea.
struct SafeInset { float left = 0, top = 0, right = 0, bottom = 0; };
inline SafeInset safe_inset;

// Off (default): hide the PC bottom bar on the touch field and draw compact vitals.
// On: --toggle touch_classic_hud=on keeps game.exe's bar under touch chrome.
inline bool g_touch_classic_hud = false;

inline constexpr int kTouchTreeRight = 488;

struct Layout {
    Box move, attack, page, cancel, menu, skills_bar, map, done, interact;
    std::array<Box, 4> skills{}, belts{};
};

inline Box placed(Box box) {
    constexpr float kEdge = 24.f;
    if (box.x < kEdge) box.x += safe_inset.left;
    if (box.y < kEdge) box.y += safe_inset.top;
    if (box.x + box.width > 800.f - kEdge) box.x -= safe_inset.right;
    if (box.y + box.height > 600.f - kEdge) box.y -= safe_inset.bottom;
    return box;
}

/** Placed touch regions for draw, hit tests and chrome. */
inline Layout layout_boxes() {
    Layout out;
    out.move = placed(kMove);
    out.attack = placed(kAttack);
    out.page = placed(kPage);
    out.cancel = placed(kCancel);
    out.menu = placed(kMenu);
    out.skills_bar = placed(kSkillsBar);
    out.map = placed(kMap);
    out.done = placed(kDone);
    out.interact = placed(kInteract);
    for (int index = 0; index < 4; ++index) {
        out.skills[std::size_t(index)] = placed(kSkills[index]);
        out.belts[std::size_t(index)] = placed(kBelts[index]);
    }
    return out;
}

inline constexpr float kStickOuter = 76.f, kStickInner = 26.f, kSkillRing = 38.f;

/** True when logical coordinates hit a fixed touch chrome region (not the free field). */
inline bool chrome_hit(float x, float y, bool interact_chrome = false) {
    const auto chrome = layout_boxes();
    if (chrome.done.contains(x, y) || chrome.map.contains(x, y) || chrome.menu.contains(x, y)) return true;
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

    /**
     * Cancel pending touch ownership and queued actions without committing a cast. Preserve the
     * selected skill page while clearing movement, attack and editor gestures.
     */
    void clear() {
        movement_ = skill_ = attack_ = -1;
        move_x = move_y = 0;
        aiming = dragged = attack_down = attack_press = false;
        casts.clear();
        belt = destination = -1;
        menu = done = false;
    }
    /**
     * Clear one-frame touch outputs after consumption while retaining held fingers, movement and
     * the selected page.
     */
    void frame() { attack_press = menu = done = false; belt = destination = -1; casts.clear(); }
    // Self casts and auras are stationary taps; all casts commit on a valid
    // lift, so cancellation never spends mana. Aimed skills enqueue the visible preview.
    /**
     * Update one finger in logical viewport coordinates without affecting another finger owner.
     * Only a valid release queues the visible skill preview; cancellation never queues a cast.
     * Editor taps choose destinations while page taps preserve an already-held skill target.
     */
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