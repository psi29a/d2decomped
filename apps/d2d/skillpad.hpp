// SPDX-License-Identifier: GPL-3.0-or-later
// Per-character skill buttons (opt-in pad mode): which skill is on each
// slot, which casts as a left-hand swing, and the wheel's stick angle.
// The pad's buttons stay in gamepad_map.hpp. This file does not write a
// .d2s or a .key.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace d2d::client::skillpad {

[[nodiscard]] inline bool portal_name(std::string_view name) {
    return name == "Book of Townportal" || name == "Town Portal" || name == "Scroll of Townportal";
}

// Modern assignment excludes inventory Identify actions and generic Kick.
[[nodiscard]] inline bool assignable_name(std::string_view name) {
    const auto matches = [&](std::string_view excluded) {
        return name.size() == excluded.size() && std::equal(name.begin(), name.end(), excluded.begin(), [](char left, char right) {
            const auto lower = [](char value) { return value >= 'A' && value <= 'Z' ? char(value + ('a' - 'A')) : value; };
            return lower(left) == lower(right);
        });
    };
    return !matches("Kick") && !matches("Identify") && !matches("Scroll of Identify") && !matches("Book of Identify");
}

inline constexpr int kSlots = 6;          // A B X Y LB RB, each with a second set
inline constexpr int kEmpty = -1;         // the slot casts nothing
inline constexpr int kUnset = -2;         // the main skill has never been saved: seed it
inline constexpr int kInteract = -3;      // the slot talks, uses, or picks up, as the right stick does
inline constexpr int kShowItems = -4;     // the slot holds Show Items, as Alt does

inline constexpr int kInventory = -5;
inline constexpr int kCharacter = -6;
inline constexpr int kSkillTree = -7;
inline constexpr int kAutomap = -8;
inline constexpr int kQuests = -9;
inline constexpr std::array<int, 8> kActions = {
    kEmpty, kInteract, kShowItems, kInventory, kCharacter, kSkillTree, kAutomap, kQuests };

// The same panel indices as the Start list below.
[[nodiscard]] inline int panel_index(int id) {
    switch (id) {
        case kCharacter: return 0;
        case kInventory: return 1;
        case kSkillTree: return 2;
        case kAutomap: return 3;
        case kQuests: return 4;
        default: return -1;
    }
}
[[nodiscard]] inline bool binds_action(int id) { return id == kInteract || id == kShowItems || panel_index(id) >= 0; }
// `full` is the wheel's highlighted name. The tray uses the short one.
[[nodiscard]] inline std::string_view action_label(int id, bool full) {
    if (id == kEmpty) return "Clear";
    if (id == kInteract) return full ? "Interact" : "Use";
    if (id == kShowItems) return full ? "Show Items" : "Items";
    if (id == kInventory) return full ? "Inventory" : "Inv";
    if (id == kCharacter) return full ? "Character" : "Char";
    if (id == kSkillTree) return full ? "Skill Tree" : "Skills";
    if (id == kAutomap) return "Map";
    if (id == kQuests) return "Quests";
    return {};
}

// A skill casts as the left button (a swing that wants a foe) when Skills.txt
// allows it on the left, it fires no missile, and its range is hand to hand.
// Everything else casts as the right button, which fires even with nothing
// in reach (buffs, teleport, missiles).
struct Hand { bool leftskill = false; bool missile = false; std::string_view range; };
[[nodiscard]] inline bool swings(Hand hand) {
    return hand.leftskill && !hand.missile && hand.range == "h2h";
}

struct Sheet {
    int main = kUnset;                              // RT, not part of either set
    int alt_main = kUnset;                          // LT+RT. Unset: use main (an older file). Empty is a saved clear.
    std::array<int, kSlots * 2> slots{};            // set 0, then set 1
    Sheet() { slots.fill(kEmpty); }
    [[nodiscard]] int slot(bool alternate, int index) const { return slots[std::size_t(index + (alternate ? kSlots : 0))]; }
    void set_slot(bool alternate, int index, int skill) { slots[std::size_t(index + (alternate ? kSlots : 0))] = skill; }
    // The skill id on RT, or on LT+RT when `alternate`. An unset alternate is main.
    [[nodiscard]] int trigger(bool alternate) const { return alternate && alt_main != kUnset ? alt_main : main; }
    bool operator==(const Sheet&) const = default;
};

// First activation only: current hands first, then the player's saved hotkeys.
// Callers filter eligibility and charged entries before passing these IDs.
[[nodiscard]] inline Sheet initial_sheet(int left, int right, std::span<const int> hotkeys, const std::array<bool, 7>& enabled) {
    Sheet sheet;
    sheet.slots.fill(kEmpty);
    sheet.main = enabled[6] ? left : kEmpty;
    sheet.set_slot(false, 5, enabled[5] ? right : kEmpty);
    std::array<int, 11> destinations{ 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11 };
    std::size_t next = 0;
    for (const int skill : hotkeys) {
        if (skill < 0 || skill == sheet.main || std::ranges::contains(sheet.slots, skill)) continue;
        while (next < destinations.size() && !enabled[std::size_t(destinations[next] % kSlots)]) ++next;
        if (next == destinations.size()) break;
        sheet.slots[std::size_t(destinations[next++])] = skill;
    }
    return sheet;
}

// Screen axes: +x right, +y down. Wedge 0 is centred half a step clockwise
// from straight up, then clockwise. The same centre is what the wheel draws.
inline constexpr float kTau = 6.2831853f;
inline constexpr float kHalfPi = 1.5707963f;
inline constexpr int kWheelShown = 8;       // icons on the ring at once; the rest follow the highlight

[[nodiscard]] inline float wedge_center(int index, int count) {
    return (float(index) + 0.5f) / float(count) * kTau - kHalfPi;
}

// -1 when the stick is inside `deadzone` or there is nothing to pick.
// A stick aimed at a wedge's centre returns that wedge.
[[nodiscard]] inline int wedge(float x, float y, int count, float deadzone) {
    if (count <= 0 || std::hypot(x, y) <= deadzone) return -1;
    float scaled = (std::atan2(y, x) + kHalfPi) / kTau * float(count) - 0.5f;
    scaled = std::fmod(scaled, float(count));
    if (scaled < 0.f) scaled += float(count);
    int index = int(std::floor(scaled + 1e-4f));
    if (index >= count) index = 0;
    if (index < 0) index = 0;
    return index;
}

// Modern radial selection: centre hold and a small angular boundary margin.
[[nodiscard]] inline int stable_wedge(float x, float y, int count, int current) {
    if (count <= 0 || std::hypot(x, y) <= .30f) return current;
    const float angle = std::atan2(y, x);
    if (current >= 0 && current < count) {
        const float delta = std::remainder(angle - wedge_center(current, count), kTau);
        if (std::abs(delta) <= kTau / float(count) / 2 + .08f) return current;
    }
    float turn = std::fmod(angle + kHalfPi + kTau, kTau);
    return std::min(count - 1, int(turn / kTau * float(count)));
}
[[nodiscard]] inline int next_page(int highlight, int count, int step) {
    if (count <= 0) return 0;
    const int pages = (count + kWheelShown - 1) / kWheelShown;
    const int page = (highlight / kWheelShown + (step > 0 ? 1 : pages - 1)) % pages;
    return std::min(count - 1, page * kWheelShown + highlight % kWheelShown);
}

// The icons actually drawn. `start` is the previous window. The highlight
// stays inside it; a highlight that walks off the end pulls the window along.
struct WheelPage { int start = 0, shown = 0; };
[[nodiscard]] inline WheelPage wheel_page(int count, int highlight, int limit, int start) {
    WheelPage page;
    if (count <= 0 || limit <= 0) return page;
    page.shown = std::min(count, limit);
    if (highlight < 0) highlight = 0;
    if (highlight >= count) highlight = count - 1;
    if (start < 0) start = 0;
    if (start > count - page.shown) start = std::max(0, count - page.shown);
    if (highlight < start) start = highlight;
    if (highlight >= start + page.shown) start = highlight - page.shown + 1;
    page.start = start;
    return page;
}

// The set latched when a slot was pressed. Holding the modifier afterwards
// does not retarget that hold. -1 when the button is up.
struct HoldSet {
    std::array<int, kSlots> latched{};
    HoldSet() { latched.fill(-1); }
    int set_for(int index, bool press, bool held, bool alternate) {
        if (index < 0 || index >= kSlots) return -1;
        auto& kept = latched[std::size_t(index)];
        if (press) kept = alternate ? 1 : 0;
        else if (!held) kept = -1;
        return kept;
    }
};

// RT's set, latched the same way as a slot: the set at the press, until RT is up.
struct TriggerHold {
    int latched = -1;
    int set_for(bool press, bool held, bool alternate) {
        if (press) latched = alternate ? 1 : 0;
        else if (!held) latched = -1;
        return latched;
    }
};

// The assigned-slot bar. It stays up in the field and while the wheel is open.
// The Start list has the screen instead.
[[nodiscard]] inline bool tray_visible(bool legend, bool strip, bool wheel, int picking) {
    return legend && !strip && (picking == 0 || wheel);
}

// Escape respawns once the death screen is up. Skill buttons use Start for
// the panel list, so that press respawns instead. A ghost with a partner
// still alive keeps the list.
[[nodiscard]] inline bool start_respawns(bool start_pressed, bool dead, bool ghost, bool all_dead) {
    return start_pressed && dead && (!ghost || all_dead);
}

// What the right-hand skill becomes when an aura button is pressed.
// `active` is the aura the world is running (0 none). `pressed` is an aura.
// The same aura toggles off, back to the non-aura skill remembered earlier.
// A different aura switches. The remembered skill is never itself an aura.
struct AuraChoice { int next = 0; int remembered = 0; };
[[nodiscard]] inline AuraChoice aura_choice(int active, int pressed, int current, bool current_is_aura, int remembered) {
    const int memory = current_is_aura ? remembered : current;
    if (active == pressed) return { memory, memory };
    return { pressed, memory };
}

// South confirmed the Start list. Drop pad clicks while that button is down,
// and on the frame it goes up, so the release does not activate the screen
// the list just opened. The next press, after it has been up, is a real click.
struct MenuHandoff {
    bool armed = false;
    bool drop(bool south_down) {
        if (!armed) return false;
        if (!south_down) armed = false;
        return true;
    }
};

inline constexpr std::array<std::string_view, 9> kPanels = {
    "Character", "Inventory", "Skill Tree", "Automap", "Quests", "Party", "Hireling", "Help", "Menu" };

// One line each: "main <id>", "amain <id>" when LT+RT was saved, "s0".."s5"
// the first set, "a0".."a5" the second. A missing main stays unset. A missing
// amain means "use main". A missing slot stays empty. -1 is an explicit clear.
[[nodiscard]] inline std::string write(const Sheet& sheet) {
    std::string out = "initialized 1\n";
    if (sheet.main != kUnset) out += "main " + std::to_string(sheet.main) + "\n";
    if (sheet.alt_main != kUnset) out += "amain " + std::to_string(sheet.alt_main) + "\n";
    for (int index = 0; index < kSlots; ++index) {
        out += "s" + std::to_string(index) + " " + std::to_string(sheet.slot(false, index)) + "\n";
        out += "a" + std::to_string(index) + " " + std::to_string(sheet.slot(true, index)) + "\n";
    }
    return out;
}

[[nodiscard]] inline Sheet read(std::string_view text) {
    Sheet sheet;
    sheet.slots.fill(kEmpty);  // existing files keep missing and deliberately empty slots empty
    while (!text.empty()) {
        const auto split = text.find('\n');
        const auto line = text.substr(0, split);
        text.remove_prefix(split == std::string_view::npos ? text.size() : split + 1);
        if (line.empty()) continue;
        const auto space = line.find(' ');
        if (space == std::string_view::npos || space + 1 >= line.size()) continue;
        const auto key = line.substr(0, space);
        const auto value = line.substr(space + 1);
        int skill = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), skill);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) continue;
        if (key == "main") sheet.main = skill;
        else if (key == "amain") sheet.alt_main = skill;
        else if (key.size() == 2 && key[0] == 's' && key[1] >= '0' && key[1] < '0' + kSlots) sheet.set_slot(false, key[1] - '0', skill);
        else if (key.size() == 2 && key[0] == 'a' && key[1] >= '0' && key[1] < '0' + kSlots) sheet.set_slot(true, key[1] - '0', skill);
    }
    return sheet;
}

}  // namespace d2d::client::skillpad
