// SPDX-License-Identifier: GPL-3.0-or-later
// Touch adapter in logical 800x600 coordinates. No SDL, game rules or saves.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
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

/** Last physical device that drove this seat. Touch HUD shows only for Touch. */
enum class LastDevice { Gamepad, MouseKeyboard, Touch };

[[nodiscard]] inline bool touch_hud_visible(bool enabled, LastDevice last, bool classic_hud = false) {
    return enabled && last == LastDevice::Touch && !classic_hud;
}

[[nodiscard]] inline bool touch_owns_locomotion(LastDevice last, int fingers, bool item_drag) {
    return last == LastDevice::Touch && (fingers > 0 || item_drag);
}
/** Unused vendor-split leftover; trade/repair and inventory use classic panel X. */
inline constexpr int kTouchInvPanelX = 12, kTouchInvPanelY = 48;
/** Scales corner chips and field controls (deviation candidate: touch_ui_scale). */
inline constexpr float kUiScale = 1.2f;
inline constexpr int kNpcLinePad = 14;

// Nominal sizes (positions filled in layout_boxes).
inline constexpr float kChipSize = 48.f, kChipGap = 8.f;
inline constexpr float kBeltSize = 48.f, kBeltGap = 6.f, kSkillSize = 72.f, kAttackSize = 96.f, kPageSize = 40.f;
/** Life/mana sit this far under the 2x2 chip block, as a fraction of chip height (¼–½). */
inline constexpr float kBarGapChip = 0.35f, kBarBlockH = 48.f;
inline constexpr int kTouchStorePanelX = 400;
inline constexpr float kMoveHitW = 200.f, kMoveHitH = 220.f, kMoveVisualW = 100.f, kMoveVisualH = 110.f;
/** Finger movement that turns an item tap into a drag-and-drop pickup. */
inline constexpr float kItemDragSlop = 14.f;
inline constexpr std::uint32_t kBeltDoubleTapMs = 280;
inline constexpr std::uint32_t kMoveDoubleTapMs = 320, kTreeDoubleTapMs = 320;
inline constexpr float kMoveTapSlop = 18.f;
inline constexpr Box kTreeLearnConfirm{ 16.f, 490.f, 110.f, 44.f }, kTreeLearnCancel{ 136.f, 490.f, 110.f, 44.f };
inline constexpr Box kTreeAssign{ 16.f, 545.f, 240.f, 50.f };

/** Two taps of the same control: close in time and in place, not a drag. */
[[nodiscard]] inline bool deliberate_double_tap(float x0, float y0, float x1, float y1, std::uint32_t t0, std::uint32_t t1,
                                                float slop = kMoveTapSlop, std::uint32_t window = kMoveDoubleTapMs) {
    if (t1 < t0 || t1 - t0 > window) return false;
    return std::hypot(x1 - x0, y1 - y0) <= slop;
}
inline constexpr Box kMenu{ 0, 0, 48, 48 }, kSkillsBar{ 0, 52, 48, 48 }, kDone{ 0, 0, 52, 48 }, kMap{ 0, 56, 52, 48 };
inline constexpr Box kQuestTrack{ 0, 132, 172, 148 };

struct Layout {
    Box move, move_visual, attack, page, menu, skills_bar, quest, bind, map, done, interact, quest_track;
    std::array<Box, 4> skills{}, belts{};
};

inline float ui_scale(float value) { return value * kUiScale; }
inline float bar_gap() { return kBarGapChip * ui_scale(kChipSize); }

/** Top-left for a frame centred in `box` (belt and skill icons). */
inline std::pair<float, float> centred_blit_origin(Box box, float frame_w, float frame_h) {
    return { box.x + (box.width - frame_w) * .5f, box.y + (box.height - frame_h) * .5f };
}

/** Scale that fills `box` with an inner margin (Attack). Belt uses 1 unless the frame clips the ring. */
inline float fit_icon_scale(float box_w, float box_h, float frame_w, float frame_h, float margin = 0.10f) {
    if (frame_w <= 0.f || frame_h <= 0.f) return 1.f;
    const float inner_w = box_w * (1.f - 2.f * margin), inner_h = box_h * (1.f - 2.f * margin);
    return std::min(inner_w / frame_w, inner_h / frame_h);
}

enum class ItemAction { None, Use, Sell, Buy, Fill, Confirm, Cancel };

struct ItemSheet {
    bool open = false, confirming = false, show_use = false, show_sell = false, show_buy = false, show_fill = false;
    bool identifying_target = false;
    int belt_index = -1;
    ItemAction chosen = ItemAction::None;
    Box use{}, sell{}, buy{}, fill{}, confirm{}, cancel{};
    [[nodiscard]] bool contains(float x, float y) const {
        return open && (use.contains(x, y) || sell.contains(x, y) || buy.contains(x, y) || fill.contains(x, y)
                        || confirm.contains(x, y) || cancel.contains(x, y));
    }
    [[nodiscard]] ItemAction hit(float x, float y) const {
        if (!open) return ItemAction::None;
        if (use.contains(x, y)) return ItemAction::Use;
        if (sell.contains(x, y)) return ItemAction::Sell;
        if (buy.contains(x, y)) return ItemAction::Buy;
        if (fill.contains(x, y)) return ItemAction::Fill;
        if (confirm.contains(x, y)) return ItemAction::Confirm;
        if (cancel.contains(x, y)) return ItemAction::Cancel;
        return ItemAction::None;
    }
};

[[nodiscard]] inline bool item_dragged(float origin_x, float origin_y, float x, float y) {
    return std::hypot(x - origin_x, y - origin_y) > kItemDragSlop;
}

/** Inventory/stash/cube grid for nearest-cell snap (same origin as item_cursor_command). */
struct SnapGrid {
    float x = 0, y = 0, box_w = 29, box_h = 29;
    int cols = 0, rows = 0, item_w = 1, item_h = 1;
};

[[nodiscard]] inline bool snap_grid_cell(const SnapGrid& grid, float mouse_x, float mouse_y, int& col, int& row) {
    if (grid.cols <= 0 || grid.rows <= 0 || grid.box_w <= 0.f || grid.box_h <= 0.f) return false;
    const float adj_x = mouse_x - grid.x - float((grid.item_w - 1) * grid.box_w) / 2.f;
    const float adj_y = mouse_y - grid.y - float((grid.item_h - 1) * grid.box_h) / 2.f;
    col = int(std::floor(adj_x / grid.box_w));
    row = int(std::floor(adj_y / grid.box_h));
    const float left = grid.x, top = grid.y;
    const float right = grid.x + float(grid.cols) * grid.box_w, bottom = grid.y + float(grid.rows) * grid.box_h;
    const float dx = mouse_x < left ? left - mouse_x : (mouse_x > right ? mouse_x - right : 0.f);
    const float dy = mouse_y < top ? top - mouse_y : (mouse_y > bottom ? mouse_y - bottom : 0.f);
    if (dx > grid.box_w || dy > grid.box_h) return false;
    const int max_col = std::max(0, grid.cols - std::max(grid.item_w, 1));
    const int max_row = std::max(0, grid.rows - std::max(grid.item_h, 1));
    col = std::clamp(col, 0, max_col);
    row = std::clamp(row, 0, max_row);
    return true;
}

inline std::pair<float, float> snap_grid_point(const SnapGrid& grid, int col, int row) {
    return { grid.x + float((grid.item_w - 1) * grid.box_w) / 2.f + (float(col) + 0.5f) * grid.box_w,
             grid.y + float((grid.item_h - 1) * grid.box_h) / 2.f + (float(row) + 0.5f) * grid.box_h };
}

inline ItemSheet make_item_sheet(bool can_use, bool can_sell, bool can_buy, bool can_fill = false) {
    ItemSheet sheet;
    sheet.open = can_use || can_sell || can_buy || can_fill;
    sheet.show_use = can_use;
    sheet.show_sell = can_sell;
    sheet.show_buy = can_buy;
    sheet.show_fill = can_fill;
    constexpr float kWidth = 220.f, kHeight = 40.f, kGap = 8.f;
    float left = 290.f, top = 188.f;
    const auto place = [&](Box& box, bool shown) {
        if (!shown) { box = {}; return; }
        box = { left, top, kWidth, kHeight };
        top += kHeight + kGap;
    };
    place(sheet.use, can_use);
    place(sheet.sell, can_sell);
    place(sheet.buy, can_buy);
    place(sheet.fill, can_fill);
    top += 6.f;
    sheet.confirm = { left, top, 100.f, kHeight };
    sheet.cancel = { left + 120.f, top, 100.f, kHeight };
    return sheet;
}

inline ItemSheet make_confirm_sheet() {
    ItemSheet sheet;
    sheet.open = true;
    sheet.identifying_target = true;
    constexpr float kHeight = 40.f;
    float left = 290.f, top = 188.f;
    sheet.confirm = { left, top, 100.f, kHeight };
    sheet.cancel = { left + 120.f, top, 100.f, kHeight };
    return sheet;
}

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
    const float attack_x = out.attack.x, attack_y = out.attack.y;
    // Page on the attack button's top, not tucked into its lower-left corner.
    out.page = { attack_x + ui_scale(28.f), attack_y - ui_scale(52.f), page, page };
    const float chip = ui_scale(kChipSize), gap = ui_scale(kChipGap);
    out.menu = anchor_box(chip, chip, true, true);
    out.skills_bar = { out.menu.x + chip + gap, out.menu.y, chip, chip };
    out.quest = { out.menu.x, out.menu.y + chip + gap, chip, chip };
    out.bind = { out.skills_bar.x, out.quest.y, chip, chip };
    out.done = anchor_box(ui_scale(52.f), ui_scale(48.f), false, true);
    out.map = { out.done.x, out.done.y + ui_scale(56.f), ui_scale(52.f), ui_scale(48.f) };
    const float track_y = out.bind.y + out.bind.height + bar_gap() + kBarBlockH;
    out.quest_track = { safe_inset.left + 6.f, track_y, ui_scale(172.f), 88.f };
    // Quarter arc left and above the attack. Centres stay about a diameter apart.
    out.skills[0] = { attack_x - ui_scale(103.f), attack_y + ui_scale(7.f), skill, skill };
    out.skills[1] = { attack_x - ui_scale(186.f), attack_y - ui_scale(64.f), skill, skill };
    out.skills[2] = { attack_x - ui_scale(104.f), attack_y - ui_scale(134.f), skill, skill };
    out.skills[3] = { attack_x - ui_scale(24.f), attack_y - ui_scale(174.f), skill, skill };
    float skill_top = out.skills[0].y;
    for (const auto& skill_box : out.skills) skill_top = std::min(skill_top, skill_box.y);
    // 2x2 under Map, shifted one column left so the right pots clear Map.
    const float pot_gap = ui_scale(kBeltGap);
    const float grid_w = 2.f * belt + pot_gap, grid_h = 2.f * belt + pot_gap;
    const float column = belt + pot_gap;
    float pot_x = out.map.x + out.map.width - grid_w - column;
    float pot_y0 = out.map.y + out.map.height + pot_gap;
    const float min_y = safe_inset.top + 6.f;
    const float max_y = 600.f - safe_inset.bottom - 6.f - grid_h;
    pot_y0 = std::clamp(pot_y0, min_y, std::max(min_y, max_y));
    if (pot_y0 + grid_h + 8.f > skill_top)
        pot_y0 = std::clamp(skill_top - grid_h - 8.f, min_y, std::max(min_y, max_y));
    pot_x = std::clamp(pot_x, safe_inset.left + 6.f, 800.f - safe_inset.right - grid_w - 6.f);
    for (int index = 0; index < 4; ++index) {
        const int col = index % 2, row = index / 2;
        out.belts[std::size_t(index)] = { pot_x + float(col) * (belt + pot_gap), pot_y0 + float(row) * (belt + pot_gap), belt, belt };
    }
    out.interact = { attack_x - ui_scale(268.f), attack_y - ui_scale(48.f), ui_scale(56.f), ui_scale(56.f) };
    const float move_right = std::min(out.interact.x + out.interact.width + 16.f, out.skills[2].x - 8.f);
    out.move.width = std::max(out.move.width, move_right - out.move.x);
    out.move.height = std::max(out.move.height, 600.f - safe_inset.bottom - out.move.y);
    return out;
}

inline constexpr float kStickOuter = 52.f, kStickInner = 22.f, kSkillRing = 34.f;

/** Inv/Skl/Quest/Bind stay pinned to the top-left, including while panels are open. */
inline bool chip_hit(float x, float y) {
    const auto chrome = layout_boxes();
    return chrome.menu.contains(x, y) || chrome.skills_bar.contains(x, y) || chrome.quest.contains(x, y) || chrome.bind.contains(x, y);
}

inline bool chrome_hit(float x, float y, bool interact_chrome = false, bool field_controls = true) {
    const auto chrome = layout_boxes();
    if (chrome.done.contains(x, y) || chrome.map.contains(x, y) || chip_hit(x, y)) return true;
    if (!field_controls) return false;
    if (interact_chrome && chrome.interact.contains(x, y)) return true;
    if (chrome.page.contains(x, y) || chrome.move.contains(x, y) || chrome.attack.contains(x, y)) return true;
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
    bool menu = false, done = false, belt_quick = false, run_toggle = false;
    int belt = -1, belt_armed = -1, destination = -1;
    float move_x = 0, move_y = 0;
    bool aiming = false, dragged = false, stationary = false;
    float origin_x = 0, origin_y = 0, aim_x = 0, aim_y = 0;
    Target preview;
    std::vector<Target> casts;

    [[nodiscard]] bool move_held() const { return movement_ >= 0; }
    [[nodiscard]] bool owns(std::int64_t finger) const { return finger == movement_ || finger == attack_ || finger == skill_; }

    /** Drop ownership for a finger that lifted (routes that skip event() must call this). */
    void release_finger(std::int64_t finger) {
        if (finger == movement_) { movement_ = -1; move_x = move_y = 0; }
        if (finger == attack_) { attack_ = -1; attack_down = false; }
        if (finger == skill_) { skill_ = -1; aiming = false; }
    }

    void clear() {
        movement_ = skill_ = attack_ = -1;
        move_x = move_y = 0;
        aiming = dragged = attack_down = attack_press = false;
        casts.clear();
        belt = belt_armed = destination = -1;
        belt_quick = menu = done = run_toggle = false;
        belt_last_ = -1; belt_ms_ = 0;
        move_tap_ms_ = 0;
    }
    void frame() { attack_press = menu = done = false; belt = destination = -1; belt_quick = run_toggle = false; casts.clear(); }

    void event(std::int64_t finger, Phase phase, float x, float y, Target target = {}, bool immediate = false, bool editor = false, std::uint32_t now_ms = 0) {
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
                if (chrome.belts[std::size_t(index)].contains(x, y)) {
                    const bool quick = belt_last_ == index && now_ms >= belt_ms_ && now_ms - belt_ms_ <= kBeltDoubleTapMs
                        && !item_dragged(belt_x_, belt_y_, x, y);
                    if (quick) { belt = index; belt_quick = true; belt_armed = -1; }
                    else { belt_armed = index; belt = -1; belt_quick = false; }
                    belt_last_ = index; belt_ms_ = now_ms; belt_x_ = x; belt_y_ = y;
                    return;
                }
            if (chrome.move.contains(x, y) && movement_ < 0) {
                const bool second = now_ms > 0 && move_tap_ms_ > 0
                    && deliberate_double_tap(move_tap_x_, move_tap_y_, x, y, move_tap_ms_, now_ms);
                if (second) { run_toggle = true; move_tap_ms_ = 0; }
                movement_ = finger; move_origin_x_ = x; move_origin_y_ = y; move_down_ms_ = now_ms;
            }
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
            if (phase == Phase::Up) {
                const float dist = std::hypot(x - move_origin_x_, y - move_origin_y_);
                const bool short_tap = dist <= kMoveTapSlop
                    && (now_ms == 0 || move_down_ms_ == 0 || (now_ms >= move_down_ms_ && now_ms - move_down_ms_ <= kMoveDoubleTapMs));
                if (short_tap && now_ms > 0) { move_tap_ms_ = now_ms; move_tap_x_ = x; move_tap_y_ = y; }
                else move_tap_ms_ = 0;
                movement_ = -1; move_x = move_y = 0;
            } else {
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
                if (x >= 0 && x < 800 && y >= 0 && y < 600 && preview.valid) casts.push_back(preview);
                skill_ = -1; aiming = false;
            }
        }
    }
private:
    std::int64_t movement_ = -1, skill_ = -1, attack_ = -1;
    float move_origin_x_ = 0, move_origin_y_ = 0;
    std::uint32_t move_down_ms_ = 0, move_tap_ms_ = 0;
    float move_tap_x_ = 0, move_tap_y_ = 0;
    int belt_last_ = -1;
    std::uint32_t belt_ms_ = 0;
    float belt_x_ = 0, belt_y_ = 0;
};

} // namespace d2d::client::touch