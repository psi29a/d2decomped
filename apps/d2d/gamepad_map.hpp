// SPDX-License-Identifier: GPL-3.0-or-later
// Gamepad -> named actions, with no SDL in it (gamepad.hpp feeds it): the
// sticks, triggers and buttons of one frame in, what the game does out. The
// default layout uses SDL's positional buttons (south/east/west/north), so an
// Xbox, PlayStation, Switch or Deck pad lands the same. See
// docs/design/controller-plan.md.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace d2d::client::pad {

// The triggers count as buttons past a press point (Mapper's hysteresis); a
// Sample's own `held` has them off.
enum class Button { South, East, West, North, Back, Start, LeftShoulder, RightShoulder, LeftStick, RightStick,
                    DpadUp, DpadDown, DpadLeft, DpadRight, LeftTrigger, RightTrigger, Count };

// What the screen the pad plays is: the title (B is not Esc there: that quits),
// the other menus (the d-pad hops the cursor between buttons), or the game:
// the field, a panel open (the d-pad hops the cursor between items), a store
// (X trades, the shoulders switch tabs, B leaves), or a dialog: an NPC's menu
// or speech, the game menu, a waypoint (B leaves, the d-pad steps the choices).
enum class Context { Title, Menu, Game, Panel, Shop, Dialog };

// Keys the pad presses on the game's behalf: the adapter turns them into SDL
// key events, so every screen's own key handling runs as for the keyboard.
enum class Key { Escape, Return, Up, Down, Left, Right, Inventory, Character, SkillTree, QuestLog, Automap, ToggleRun,
                 Belt1, Belt2, Belt3, Belt4, SwapWeapons };

struct Sample {                       // one frame of a pad: sticks -1..1, triggers 0..1
    float lx = 0, ly = 0, rx = 0, ry = 0, left_trigger = 0, right_trigger = 0;
    std::array<bool, std::size_t(Button::Count)> held{};
    [[nodiscard]] bool operator[](Button which) const { return held[std::size_t(which)]; }
};

// What a button does in the game, rebindable (Bindings). Each one works in
// some of the game's contexts (kWhere): two actions may share a button only
// where they never both work. The menus outside the game keep a fixed
// layout: the confirm button clicks, the other one backs out, the d-pad steps.
enum class Action { Primary, Secondary, Back, GameMenu, Inventory, Character, SkillTree, QuestLog, Automap, ToggleRun,
                    SwapWeapons, SkillPicker, Recentre, Belt1, Belt2, Belt3, Belt4, Trade, TabPrev, TabNext, Count };

namespace where {
inline constexpr unsigned kField = 1, kPanel = 2, kShop = 4, kDialog = 8, kAll = 15;
}
// The contexts each Action works in, by Action.
inline constexpr std::array<unsigned, std::size_t(Action::Count)> kWhere = {
    where::kAll,                                   // Primary: the left click
    where::kField | where::kPanel,                 // Secondary: the right click / right-hand skill
    where::kShop | where::kDialog,                 // Back: Esc out of a store or a dialog
    where::kAll,                                   // GameMenu (Esc)
    where::kAll, where::kField | where::kPanel | where::kDialog, where::kField | where::kPanel | where::kDialog,   // Inventory, Character, SkillTree
    where::kAll, where::kField | where::kPanel | where::kDialog,                     // QuestLog, Automap
    where::kAll, where::kAll,                      // ToggleRun, SwapWeapons
    where::kField,                                 // SkillPicker
    where::kPanel | where::kShop | where::kDialog, // Recentre: the cursor to the screen's middle
    where::kField, where::kField, where::kField, where::kField,   // Belt1..4
    where::kShop, where::kShop, where::kShop,      // Trade, TabPrev, TabNext
};
// Where the d-pad is the cursor's hops, so no action can have it.
inline constexpr unsigned kDpadHops = where::kPanel | where::kShop | where::kDialog;

inline constexpr std::array<std::string_view, std::size_t(Button::Count)> kButtonNames = {
    "south", "east", "west", "north", "back", "start", "left_shoulder", "right_shoulder", "left_stick", "right_stick",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "left_trigger", "right_trigger" };
inline constexpr std::array<std::string_view, std::size_t(Action::Count)> kActionNames = {
    "primary", "secondary", "back", "game_menu", "inventory", "character", "skill_tree", "quest_log", "automap", "toggle_run",
    "swap_weapons", "skill_picker", "recentre", "belt_1", "belt_2", "belt_3", "belt_4", "trade", "tab_prev", "tab_next" };

// The pad's family, for the labels printed on its buttons.
enum class Family { Xbox, PlayStation, Nintendo, Deck };
// What `button` is called on a `family` pad. Nintendo's face letters sit
// mirrored (A east, B south); PlayStation's are shapes, spelled out (the
// game's fonts have none).
[[nodiscard]] inline std::string_view label(Family family, Button button) {
    static constexpr std::array<std::array<std::string_view, std::size_t(Button::Count)>, 4> kLabels = { {
        { "A", "B", "X", "Y", "View", "Menu", "LB", "RB", "LS", "RS", "Up", "Down", "Left", "Right", "LT", "RT" },
        { "Cross", "Circle", "Square", "Triangle", "Share", "Options", "L1", "R1", "L3", "R3", "Up", "Down", "Left", "Right", "L2", "R2" },
        { "B", "A", "Y", "X", "-", "+", "L", "R", "LS", "RS", "Up", "Down", "Left", "Right", "ZL", "ZR" },
        { "A", "B", "X", "Y", "View", "Menu", "L1", "R1", "L3", "R3", "Up", "Down", "Left", "Right", "L2", "R2" },
    } };
    return button < Button::Count ? kLabels[std::size_t(family)][std::size_t(button)] : std::string_view("-");
}

// Each action's buttons: up to two (Button::Count: none).
struct Bindings {
    static constexpr Button kNone = Button::Count;
    using Pair = std::array<Button, 2>;
    std::array<Pair, std::size_t(Action::Count)> buttons{};
    Bindings() {                                   // the default layout (apps/d2d/README.md)
        using enum Button;
        const auto put = [&](Action action, Button first, Button second = kNone) { buttons[std::size_t(action)] = { first, second }; };
        put(Action::Primary, South, RightTrigger);  put(Action::Secondary, East);       put(Action::Back, East);
        put(Action::GameMenu, Start);               put(Action::Inventory, North);      put(Action::Character, West);
        put(Action::SkillTree, LeftShoulder);       put(Action::QuestLog, Back);        put(Action::Automap, RightShoulder);
        put(Action::ToggleRun, LeftStick);          put(Action::SwapWeapons, LeftTrigger);
        put(Action::SkillPicker, RightStick);       put(Action::Recentre, RightStick);
        put(Action::Belt1, DpadUp);                 put(Action::Belt2, DpadDown);       put(Action::Belt3, DpadLeft);
        put(Action::Belt4, DpadRight);              put(Action::Trade, West);
        put(Action::TabPrev, LeftShoulder);         put(Action::TabNext, RightShoulder);
    }
    [[nodiscard]] const Pair& operator[](Action action) const { return buttons[std::size_t(action)]; }
    Pair& operator[](Action action) { return buttons[std::size_t(action)]; }
    bool operator==(const Bindings&) const = default;
};

[[nodiscard]] inline bool is_dpad(Button button) { return button >= Button::DpadUp && button <= Button::DpadRight; }

// The actions that clash: a button two actions share where both work, or a
// d-pad direction where it hops. Each clash once, as (action, other); other is
// Action::Count for the d-pad's hops.
[[nodiscard]] inline std::vector<std::pair<Action, Action>> clashes(const Bindings& bindings) {
    std::vector<std::pair<Action, Action>> out;
    for (std::size_t first = 0; first < std::size_t(Action::Count); ++first)
        for (const Button button : bindings.buttons[first]) {
            if (button == Bindings::kNone) continue;
            if (is_dpad(button) && (kWhere[first] & kDpadHops) != 0) out.emplace_back(Action(first), Action::Count);
            for (std::size_t second = first + 1; second < std::size_t(Action::Count); ++second)
                if ((kWhere[first] & kWhere[second]) != 0 && std::ranges::contains(bindings.buttons[second], button))
                    out.emplace_back(Action(first), Action(second));
        }
    return out;
}

struct Config {
    float deadzone = 0.15f;           // radial, both sticks
    float cursor_speed = 650.f;       // game pixels per second at full tilt
    float trigger_press = 0.5f, trigger_release = 0.3f;
    float walk_reach = 6.f;           // tiles ahead a held stick walks towards
    bool  swap_sticks = false;        // left-handed: the right stick walks, the left one aims and moves the cursor
    bool  swap_confirm = false;       // the menus' confirm on east, back on south (a Nintendo pad's A / B)
    Bindings bindings;
    bool operator==(const Config&) const = default;
};

// d2d.cfg's `pad_*` keys. Names it doesn't know, or bindings that clash, are
// put in `problems` and left at their defaults.
[[nodiscard]] inline std::string button_list(const Bindings::Pair& pair) {
    std::string out;
    for (const Button button : pair)
        if (button != Bindings::kNone) out += (out.empty() ? "" : ",") + std::string(kButtonNames[std::size_t(button)]);
    if (out.empty()) return "none";
    return out;
}
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> save_config(const Config& config) {
    std::vector<std::pair<std::string, std::string>> out = {
        { "pad_deadzone", std::to_string(int(std::lround(config.deadzone * 100))) },
        { "pad_cursor_speed", std::to_string(int(std::lround(config.cursor_speed))) },
        { "pad_swap_sticks", config.swap_sticks ? "1" : "0" },
        { "pad_swap_confirm", config.swap_confirm ? "1" : "0" } };
    for (std::size_t action = 0; action < std::size_t(Action::Count); ++action)
        out.emplace_back("pad_" + std::string(kActionNames[action]), button_list(config.bindings.buttons[action]));
    return out;
}
// `lookup(key)`: the cfg's value or nullptr.
template <class Lookup>
[[nodiscard]] Config load_config(const Lookup& lookup, std::vector<std::string>& problems) {
    Config config;
    const auto number = [&](const char* key, int low, int high, auto apply) {
        const std::string* value = lookup(key);
        if (!value) return;
        char* end = nullptr;
        const long parsed = std::strtol(value->c_str(), &end, 10);
        if (value->empty() || *end != 0 || parsed < low || parsed > high) problems.push_back(std::string(key) + " = " + *value + ": not " + std::to_string(low) + ".." + std::to_string(high));
        else apply(int(parsed));
    };
    number("pad_deadzone", 0, 90, [&](int value) { config.deadzone = float(value) / 100; });
    number("pad_cursor_speed", 50, 3000, [&](int value) { config.cursor_speed = float(value); });
    number("pad_swap_sticks", 0, 1, [&](int value) { config.swap_sticks = value != 0; });
    number("pad_swap_confirm", 0, 1, [&](int value) { config.swap_confirm = value != 0; });
    const Bindings defaults;
    for (std::size_t action = 0; action < std::size_t(Action::Count); ++action) {
        const std::string key = "pad_" + std::string(kActionNames[action]);
        const std::string* value = lookup(key.c_str());
        if (!value) continue;
        Bindings::Pair pair = { Bindings::kNone, Bindings::kNone };
        std::size_t count = 0;
        bool bad = false;
        for (std::size_t start = 0; *value != "none" && start <= value->size();) {
            const std::size_t comma = std::min(value->find(',', start), value->size());
            const auto found = std::ranges::find(kButtonNames, std::string_view(*value).substr(start, comma - start));
            if (found == kButtonNames.end() || count == pair.size()) { bad = true; break; }
            pair[count++] = Button(found - kButtonNames.begin());
            start = comma + 1;
        }
        if (bad) problems.push_back(key + " = " + *value + ": not up to two of " + button_list({ Button::South, Button::East }) + ", ...");
        else config.bindings.buttons[action] = pair;
    }
    if (const auto clashing = clashes(config.bindings); !clashing.empty()) {
        problems.push_back(std::string("pad bindings clash (") + std::string(kActionNames[std::size_t(clashing.front().first)]) + "): the default layout instead");
        config.bindings = defaults;
    }
    return config;
}

struct Actions {
    float cursor_dx = 0, cursor_dy = 0;            // pixels to move the cursor this frame
    bool  click_down = false, click_press = false, click_release = false;   // the left button
    bool  rclick_press = false;
    bool  recentre_cursor = false;
    bool  skill_picker = false;                    // the field's right stick click: open the skill picker
    bool  aim_active = false;                      // the field's aiming stick turns the facing...
    float aim_x = 0, aim_y = 0;                    // ...to this screen direction (unit length)
    bool  move_active = false;                     // the left stick walks the hero...
    float move_x = 0, move_y = 0;                  // ...towards this screen direction (unit length); walk or run is the run toggle's
    int   snap_x = 0, snap_y = 0;                  // a panel's d-pad press: hop the cursor this way (-1..1 each)
    bool  trade_press = false;                     // a store's X: buy or sell what's under the cursor
    int   tab_step = 0;                            // a store's shoulders: the tab before (-1) or after (1)
    int   focus_step = 0;                          // the menus' shoulders: the screen's button before (-1) or after (1)
    std::vector<Key> keys;
};

// A screen direction as a world one: the inverse of the isometric projection
// the cursor's click uses (Town::input), normalised, times `reach` tiles.
struct WorldStep { float x = 0, y = 0; };
[[nodiscard]] inline WorldStep world_step(float screen_x, float screen_y, int iso_w, int iso_h, float reach) {
    const float iso_u = screen_x / (float(iso_w) / 2), iso_v = screen_y / (float(iso_h) / 2);
    const float x = (iso_u + iso_v) / 2, y = (iso_v - iso_u) / 2;
    const float length = std::hypot(x, y);
    return length > 0 ? WorldStep{ x / length * reach, y / length * reach } : WorldStep{};
}

// Soft targeting in the field, where the pad shows no cursor: the thing to hit
// is the nearest one inside `reach` tiles and a cone round the facing, the
// target already held (`sticky`) staying while it is in `sticky_reach`.
struct Candidate { int id = -1; float x = 0, y = 0; };
struct Aim {
    float reach = 2.5f;               // tiles from the hero
    float sticky_reach = 4.f;         // a held target is let go beyond this
    float cone_cos = 0.35f;           // cos of the half angle (~70 degrees) either side of the facing
};
// Index into `candidates` or -1. `face` is a world direction (need not be unit; zero = any way).
[[nodiscard]] inline int pick_target(float from_x, float from_y, float face_x, float face_y, std::span<const Candidate> candidates,
                                     int sticky, const Aim& aim = {}) {
    const float face_length = std::hypot(face_x, face_y);
    int best = -1;
    float best_distance = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const float dx = candidates[i].x - from_x, dy = candidates[i].y - from_y;
        const float distance = std::hypot(dx, dy);
        if (candidates[i].id == sticky && distance <= aim.sticky_reach) return int(i);
        if (distance > aim.reach) continue;
        if (face_length > 0 && distance > 0.3f && (dx * face_x + dy * face_y) / (distance * face_length) < aim.cone_cos) continue;
        if (best < 0 || distance < best_distance) { best = int(i); best_distance = distance; }
    }
    return best;
}

// The cursor's hop in a panel: the spot `points` nearest in the d-pad's
// direction, a spot off to the side counting double, so a press goes along
// a grid's row or column before it leaves it. Index or -1.
struct Point { int x = 0, y = 0; };
[[nodiscard]] inline int snap_pick(int from_x, int from_y, int dir_x, int dir_y, std::span<const Point> points) {
    int best = -1, best_score = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const int dx = points[i].x - from_x, dy = points[i].y - from_y;
        const int along = dx * dir_x + dy * dir_y, across = std::abs(dx * dir_y - dy * dir_x);
        if (along < 4) continue;                   // behind, or the spot the cursor's on
        if (const int score = along + 2 * across; best < 0 || score < best_score) { best = int(i); best_score = score; }
    }
    return best;
}

class Mapper {
public:
    explicit Mapper(const Config& config = {}) : config_(config) {}

    // `seconds` since the last frame. A pad that went away is a default
    // Sample: every button lets go, so nothing stays held.
    Actions step(const Sample& sample, Context context, float seconds) {
        Actions out;
        left_trigger_ = hysteresis(sample.left_trigger, left_trigger_);
        right_trigger_ = hysteresis(sample.right_trigger, right_trigger_);
        auto held = sample.held;
        held[std::size_t(Button::LeftTrigger)] = left_trigger_;
        held[std::size_t(Button::RightTrigger)] = right_trigger_;
        const auto pressed = [&](Button which) { return held[std::size_t(which)] && !prev_[std::size_t(which)]; };
        const auto snap = [&] {
            out.snap_x = int(pressed(Button::DpadRight)) - int(pressed(Button::DpadLeft));
            out.snap_y = int(pressed(Button::DpadDown)) - int(pressed(Button::DpadUp));
        };
        const auto dpad_keys = [&] {
            if (pressed(Button::DpadUp)) out.keys.push_back(Key::Up);
            if (pressed(Button::DpadDown)) out.keys.push_back(Key::Down);
            if (pressed(Button::DpadLeft)) out.keys.push_back(Key::Left);
            if (pressed(Button::DpadRight)) out.keys.push_back(Key::Right);
        };
        const unsigned here = context == Context::Game ? where::kField : context == Context::Panel ? where::kPanel
                            : context == Context::Shop ? where::kShop : context == Context::Dialog ? where::kDialog : 0;
        const auto& bindings = config_.bindings;
        // An action is on here, and held / just pressed on one of its buttons.
        const auto works_here = [&](Action action) { return (kWhere[std::size_t(action)] & here) != 0; };
        const auto down = [&](Action action) {
            return works_here(action) && std::ranges::any_of(bindings[action], [&](Button button) { return button != Bindings::kNone && held[std::size_t(button)]; });
        };
        const auto hit = [&](Action action) {
            return works_here(action) && std::ranges::any_of(bindings[action], [&](Button button) { return button != Bindings::kNone && pressed(button); });
        };
        const auto key = [&](Action action, Key press) { if (hit(action)) out.keys.push_back(press); };
        const bool game = here != 0;
        const Button confirm = config_.swap_confirm ? Button::East : Button::South, cancel = config_.swap_confirm ? Button::South : Button::East;

        // The left button: Primary in the game; the confirm button or the right trigger in the menus.
        out.click_down = game ? down(Action::Primary) : held[std::size_t(confirm)] || right_trigger_;
        out.click_press = out.click_down && !click_prev_;
        out.click_release = !out.click_down && click_prev_;
        click_prev_ = out.click_down;
        const bool secondary = down(Action::Secondary);
        out.rclick_press = secondary && !secondary_prev_;
        secondary_prev_ = secondary;

        // The aiming stick (the right one; the left one when swapped): the cursor. In the menus the other one moves it too.
        const float walk_x = config_.swap_sticks ? sample.rx : sample.lx, walk_y = config_.swap_sticks ? sample.ry : sample.ly;
        const float aim_x = config_.swap_sticks ? sample.lx : sample.rx, aim_y = config_.swap_sticks ? sample.ly : sample.ry;
        float cursor_x = aim_x, cursor_y = aim_y;
        if (!game) { cursor_x += walk_x; cursor_y += walk_y; }
        const auto [tilt_x, tilt_y, tilt] = dead(cursor_x, cursor_y);
        const float speed = config_.cursor_speed * tilt * tilt;   // gentle near the centre, quick at the edge
        out.cursor_dx = tilt_x * speed * seconds;
        out.cursor_dy = tilt_y * speed * seconds;
        out.skill_picker = hit(Action::SkillPicker);
        out.recentre_cursor = game ? hit(Action::Recentre) : pressed(Button::RightStick);

        if (game) {
            // The walking stick walks; in the field, where no cursor shows, the aiming one turns the facing.
            const auto [dir_x, dir_y, tilt_move] = dead(walk_x, walk_y);
            out.move_active = tilt_move > 0;
            out.move_x = dir_x; out.move_y = dir_y;
            const auto [face_x, face_y, tilt_aim] = dead(aim_x, aim_y);
            out.aim_active = context == Context::Game && tilt_aim > 0;
            out.aim_x = face_x; out.aim_y = face_y;

            key(Action::GameMenu, Key::Escape);
            key(Action::Back, Key::Escape);
            key(Action::Inventory, Key::Inventory);
            key(Action::QuestLog, Key::QuestLog);
            key(Action::ToggleRun, Key::ToggleRun);
            key(Action::SwapWeapons, Key::SwapWeapons);
            key(Action::Character, Key::Character);
            key(Action::SkillTree, Key::SkillTree);
            key(Action::Automap, Key::Automap);
            key(Action::Belt1, Key::Belt1);
            key(Action::Belt2, Key::Belt2);
            key(Action::Belt3, Key::Belt3);
            key(Action::Belt4, Key::Belt4);
            out.trade_press = hit(Action::Trade);
            out.tab_step = int(hit(Action::TabNext)) - int(hit(Action::TabPrev));
            if (here & kDpadHops) {
                snap();
                if (context == Context::Dialog) dpad_keys();   // the game menu steps its rows and sliders by key
            }
        } else {
            if (pressed(Button::Start)) out.keys.push_back(Key::Return);
            out.focus_step = int(pressed(Button::RightShoulder)) - int(pressed(Button::LeftShoulder));
            if (context == Context::Menu && pressed(cancel)) out.keys.push_back(Key::Escape);
            snap();
            dpad_keys();                             // char select steps its roster by key
        }
        prev_ = held;
        return out;
    }

    [[nodiscard]] const Config& config() const { return config_; }

private:
    struct Tilt { float x, y, magnitude; };
    // The stick past the radial dead zone: its direction and a 0..1 magnitude.
    Tilt dead(float x, float y) const {
        const float length = std::hypot(x, y);
        if (length <= config_.deadzone) return {};
        const float scaled = std::min((length - config_.deadzone) / (1.f - config_.deadzone), 1.f);
        return { x / length, y / length, scaled };
    }
    bool hysteresis(float value, bool down) const { return down ? value > config_.trigger_release : value >= config_.trigger_press; }

    Config config_;
    std::array<bool, std::size_t(Button::Count)> prev_{};
    bool click_prev_ = false, secondary_prev_ = false, left_trigger_ = false, right_trigger_ = false;
};

// Local co-op's seats (docs/design/local-coop.md): pads take seats 0..3 in
// the order they come (seat 0 is the first player), each by its id while
// it's there. A pad that goes keeps its seat for kHoldMs: a pad of the same
// key (model and serial: the same pad, or one just like it) coming back
// takes it again; after that the seat is free and its player leaves.
class Seats {
public:
    static constexpr std::uint32_t kHoldMs = 60'000;
    static constexpr int kMost = 4;
    // Pad `pad` came: its seat (a held one of its key first), or -1 when all are taken.
    int connect(const std::string& key, int pad) {
        for (std::size_t seat = 0; seat < seats_.size(); ++seat)
            if (seats_[seat].used && seats_[seat].pad < 0 && seats_[seat].key == key) { seats_[seat].pad = pad; seats_[seat].gone_at = 0; return int(seat); }
        for (std::size_t seat = 0; seat < seats_.size(); ++seat)
            if (!seats_[seat].used) { seats_[seat] = { key, pad, true, 0, int(seat) }; return int(seat); }
        return -1;
    }
    // Pad `pad` went: its seat, held from now (-1: it had none).
    int disconnect(int pad, std::uint32_t now_ms) {
        const int seat = seat_of(pad);
        if (seat >= 0) { seats_[std::size_t(seat)].pad = -1; seats_[std::size_t(seat)].gone_at = std::max<std::uint32_t>(now_ms, 1); }
        return seat;
    }
    // The seats held too long, freed now (their players leave).
    std::vector<int> expire(std::uint32_t now_ms) {
        std::vector<int> freed;
        for (std::size_t seat = 0; seat < seats_.size(); ++seat)
            if (seats_[seat].used && seats_[seat].pad < 0 && now_ms - seats_[seat].gone_at >= kHoldMs) { seats_[seat] = {}; freed.push_back(int(seat)); }
        return freed;
    }
    [[nodiscard]] int seat_of(int pad) const {
        for (std::size_t seat = 0; seat < seats_.size(); ++seat) if (seats_[seat].used && seats_[seat].pad == pad) return int(seat);
        return -1;
    }
    [[nodiscard]] int pad(int seat) const { return seat >= 0 && seat < kMost && seats_[std::size_t(seat)].used ? seats_[std::size_t(seat)].pad : -1; }
    [[nodiscard]] bool present(int seat) const { return pad(seat) >= 0; }
    [[nodiscard]] bool used(int seat) const { return seat >= 0 && seat < kMost && seats_[std::size_t(seat)].used; }
    // The seat's colour (0..3: red, green, blue, magenta): its first seat's,
    // kept as the pad moves seat.
    [[nodiscard]] int colour(int seat) const { return seat >= 0 && seat < kMost && seats_[std::size_t(seat)].used ? seats_[std::size_t(seat)].colour : std::clamp(seat, 0, kMost - 1); }
    // Seats `first` and `second` change pads (a lead's hand-over: the next
    // player's pad to seat 0).
    void swap(int first, int second) { std::swap(seats_[std::size_t(first)], seats_[std::size_t(second)]); }
    void release(int seat) { if (seat >= 0 && seat < kMost) seats_[std::size_t(seat)] = {}; }
    void clear() { seats_ = {}; }

private:
    struct Seat { std::string key; int pad = -1; bool used = false; std::uint32_t gone_at = 0; int colour = 0; };
    std::array<Seat, kMost> seats_{};
};

}  // namespace d2d::client::pad
