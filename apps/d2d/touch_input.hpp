// SPDX-License-Identifier: GPL-3.0-or-later
// Touch adapter in logical 800x600 coordinates. No SDL, game rules or saves.
#pragma once

#include <algorithm>
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
inline constexpr Box kMove{ 12, 330, 270, 260 }, kAttack{ 680, 438, 108, 100 }, kPage{ 650, 265, 138, 55 },
    kCancel{ 540, 180, 240, 65 }, kMenu{ 12, 12, 95, 48 }, kDone{ 650, 12, 138, 48 };
inline constexpr Box kSkills[4] = { { 540, 445, 112, 75 }, { 550, 350, 112, 75 }, { 675, 350, 112, 75 }, { 415, 445, 112, 75 } };
inline constexpr Box kBelts[4] = { { 12, 260, 62, 55 }, { 80, 260, 62, 55 }, { 148, 260, 62, 55 }, { 216, 260, 62, 55 } };
// Extra inset, in logical pixels, past the 12px margin the boxes already have.
// Zero leaves every box where it is. main.cpp fills this from SDL_GetWindowSafeArea.
struct SafeInset { float left = 0, top = 0, right = 0, bottom = 0; };
inline SafeInset safe_inset;
inline Box placed(Box box) {
    constexpr float kEdge = 24.f;
    if (box.x < kEdge) box.x += safe_inset.left;
    if (box.y < kEdge) box.y += safe_inset.top;
    if (box.x + box.width > 800.f - kEdge) box.x -= safe_inset.right;
    if (box.y + box.height > 600.f - kEdge) box.y -= safe_inset.bottom;
    return box;
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
        if (phase == Phase::Cancel) {
            if (finger == movement_) { movement_ = -1; move_x = move_y = 0; }
            if (finger == attack_) { attack_ = -1; attack_down = false; }
            if (finger == skill_) { skill_ = -1; aiming = false; }
            return;
        }
        if (phase == Phase::Down) {
            if (placed(kDone).contains(x, y)) { done = true; return; }
            if (placed(kPage).contains(x, y)) { alternate = !alternate; return; }
            if (editor) {
                for (int index = 0; index < 4; ++index)
                    if (placed(kSkills[index]).contains(x, y)) { destination = index; return; }
                return;
            }
            if (placed(kMenu).contains(x, y)) { menu = true; return; }
            for (int index = 0; index < 4; ++index)
                if (placed(kBelts[index]).contains(x, y)) { belt = index; return; }
            if (placed(kMove).contains(x, y) && movement_ < 0) { movement_ = finger; move_origin_x_ = x; move_origin_y_ = y; }
            else if (placed(kAttack).contains(x, y) && attack_ < 0) { attack_ = finger; attack_press = attack_down = true; }
            else if (skill_ < 0)
                for (const auto& box : kSkills)
                    if (placed(box).contains(x, y)) {
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
                move_x = length > 8 ? dx / std::max(length, 60.f) : 0;
                move_y = length > 8 ? dy / std::max(length, 60.f) : 0;
            }
        }
        if (finger == attack_ && phase == Phase::Up) { attack_ = -1; attack_down = false; }
        if (finger == skill_) {
            aim_x = x; aim_y = y;
            dragged = dragged || (!stationary && std::hypot(x - origin_x, y - origin_y) > 10);
            if (phase == Phase::Up) {
                if (!placed(kCancel).contains(x, y) && x >= 0 && x < 800 && y >= 0 && y < 600 && preview.valid) casts.push_back(preview);
                skill_ = -1; aiming = false;
            }
        }
    }
private:
    std::int64_t movement_ = -1, skill_ = -1, attack_ = -1;
    float move_origin_x_ = 0, move_origin_y_ = 0;
};

} // namespace d2d::client::touch
