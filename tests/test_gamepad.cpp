// SPDX-License-Identifier: GPL-3.0-or-later
// The gamepad's action mapping (apps/d2d/gamepad_map.hpp): dead zones, the
// walk direction, click edges, per-screen keys, panel hops and store
// buttons, and a pad that goes away.
#include "gamepad_map.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

using namespace d2d::client::pad;

namespace {
bool has(const Actions& actions, Key key) { return std::ranges::find(actions.keys, key) != actions.keys.end(); }
Sample with(Button which) { Sample sample; sample.held[std::size_t(which)] = true; return sample; }
}

int main() {
    // Inside the dead zone nothing moves.
    {
        Mapper mapper;
        Sample drift; drift.lx = 0.1f; drift.ly = -0.08f; drift.rx = 0.12f;
        const Actions actions = mapper.step(drift, Context::Game, 0.016f);
        assert(!actions.move_active && actions.cursor_dx == 0 && actions.cursor_dy == 0);
    }
    // The left stick walks in the game, towards its direction, from a light push on.
    {
        Mapper mapper;
        Sample light; light.lx = 0.2f;
        const Actions actions = mapper.step(light, Context::Game, 0.016f);
        assert(actions.move_active && actions.move_x > 0.99f && std::abs(actions.move_y) < 1e-4f);
    }
    // In the menus the left stick is a cursor, not a walk.
    {
        Mapper mapper;
        Sample stick; stick.lx = 1.f;
        const Actions actions = mapper.step(stick, Context::Menu, 0.1f);
        assert(!actions.move_active && actions.cursor_dx > 0);
    }
    // The right stick moves the cursor, quicker the harder it's pushed.
    {
        Mapper mapper;
        Sample half; half.ry = 0.6f;
        Sample full; full.ry = 1.f;
        const float slow = mapper.step(half, Context::Game, 0.1f).cursor_dy;
        const float quick = mapper.step(full, Context::Game, 0.1f).cursor_dy;
        assert(slow > 0 && quick > slow * 2);
    }
    // A click is one press edge, held down, then one release; the right trigger clicks too.
    {
        Mapper mapper;
        Actions actions = mapper.step(with(Button::South), Context::Game, 0.016f);
        assert(actions.click_press && actions.click_down && !actions.click_release);
        actions = mapper.step(with(Button::South), Context::Game, 0.016f);
        assert(!actions.click_press && actions.click_down);
        actions = mapper.step(Sample{}, Context::Game, 0.016f);
        assert(actions.click_release && !actions.click_down);
        Sample trigger; trigger.right_trigger = 0.8f;
        assert(mapper.step(trigger, Context::Game, 0.016f).click_press);
        trigger.right_trigger = 0.4f;   // still held: between the press and release thresholds
        assert(mapper.step(trigger, Context::Game, 0.016f).click_down);
    }
    // A pad that goes away (an empty Sample) lets go of a held click and a walk.
    {
        Mapper mapper;
        Sample held = with(Button::South); held.lx = 1.f;
        assert(mapper.step(held, Context::Game, 0.016f).move_active);
        const Actions gone = mapper.step(Sample{}, Context::Game, 0.016f);
        assert(gone.click_release && !gone.click_down && !gone.move_active);
    }
    // East right-clicks in the game; in a menu it's Esc, except on the title, where Esc quits.
    {
        Mapper mapper;
        Actions actions = mapper.step(with(Button::East), Context::Game, 0.016f);
        assert(actions.rclick_press && actions.keys.empty());
        mapper.step(Sample{}, Context::Game, 0.016f);
        actions = mapper.step(with(Button::East), Context::Menu, 0.016f);
        assert(!actions.rclick_press && has(actions, Key::Escape));
        mapper.step(Sample{}, Context::Menu, 0.016f);
        actions = mapper.step(with(Button::East), Context::Title, 0.016f);
        assert(actions.keys.empty() && !actions.click_press && !actions.rclick_press);
    }
    // A key is sent once per press, not every frame it's held.
    {
        Mapper mapper;
        assert(has(mapper.step(with(Button::North), Context::Game, 0.016f), Key::Inventory));
        assert(mapper.step(with(Button::North), Context::Game, 0.016f).keys.empty());
        mapper.step(Sample{}, Context::Game, 0.016f);
        assert(has(mapper.step(with(Button::North), Context::Game, 0.016f), Key::Inventory));
    }
    // The D-pad: the belt in the game, arrows in the menus; Start is the game menu or Enter.
    {
        Mapper mapper;
        assert(has(mapper.step(with(Button::DpadLeft), Context::Game, 0.016f), Key::Belt3));
        mapper.step(Sample{}, Context::Game, 0.016f);
        assert(has(mapper.step(with(Button::DpadLeft), Context::Menu, 0.016f), Key::Left));
        mapper.step(Sample{}, Context::Menu, 0.016f);
        assert(has(mapper.step(with(Button::Start), Context::Game, 0.016f), Key::Escape));
        mapper.step(Sample{}, Context::Game, 0.016f);
        assert(has(mapper.step(with(Button::Start), Context::Title, 0.016f), Key::Return));
    }
    // Screen directions as world ones (isometric, 160x80 tiles): up the screen is (-x, -y), right is (+x, -y).
    {
        const WorldStep north = world_step(0, -1, 160, 80, 6.f);
        assert(north.x < -4.2f && north.y < -4.2f && std::abs(north.x - north.y) < 1e-4f);
        const WorldStep right = world_step(1, 0, 160, 80, 6.f);
        assert(right.x > 4.2f && right.y < -4.2f);
        assert(std::abs(std::hypot(right.x, right.y) - 6.f) < 1e-4f);
        const WorldStep none = world_step(0, 0, 160, 80, 6.f);
        assert(none.x == 0 && none.y == 0);
    }
    // Soft targeting: the nearest in the facing cone and in reach; the held target sticks.
    {
        const std::vector<Candidate> things{ { 1, 1.f, 0.f }, { 2, -1.f, 0.f }, { 3, 6.f, 0.f }, { 4, 0.f, 2.f } };
        assert(pick_target(0, 0, 1, 0, things, -1) == 0);       // facing +x: the one ahead, not behind (2) or far (3)
        assert(pick_target(0, 0, -1, 0, things, -1) == 1);
        assert(pick_target(0, 0, 0, 0, things, -1) == 0);       // no facing: nearest of any
        assert(pick_target(0, 0, 0, -1, things, -1) == -1);     // nothing that way
        assert(pick_target(0, 0, 1, 0, things, 4) == 3);        // sticky 4 (2 tiles off the cone) stays in sticky reach
        assert(pick_target(0, 0, 1, 0, things, 3) == 0);        // sticky 3 is out of sticky reach: back to the nearest
        assert(pick_target(0, 0, 1, 0, {}, -1) == -1);
    }
    // A panel: the d-pad hops the cursor instead of using the belt; the shoulders still open their panels.
    {
        Mapper mapper;
        Actions actions = mapper.step(with(Button::DpadRight), Context::Panel, 0.016f);
        assert(actions.snap_x == 1 && actions.snap_y == 0 && !has(actions, Key::Belt4));
        mapper.step(Sample{}, Context::Panel, 0.016f);
        actions = mapper.step(with(Button::DpadUp), Context::Panel, 0.016f);
        assert(actions.snap_x == 0 && actions.snap_y == -1);
        mapper.step(Sample{}, Context::Panel, 0.016f);
        assert(has(mapper.step(with(Button::LeftShoulder), Context::Panel, 0.016f), Key::SkillTree));
        assert(mapper.step(with(Button::DpadRight), Context::Game, 0.016f).snap_x == 0);   // the field: the belt
    }
    // A store: X trades (not the character panel), the shoulders step the tabs; each once per press.
    {
        Mapper mapper;
        Actions actions = mapper.step(with(Button::West), Context::Shop, 0.016f);
        assert(actions.trade_press && !has(actions, Key::Character));
        assert(!mapper.step(with(Button::West), Context::Shop, 0.016f).trade_press);
        mapper.step(Sample{}, Context::Shop, 0.016f);
        actions = mapper.step(with(Button::RightShoulder), Context::Shop, 0.016f);
        assert(actions.tab_step == 1 && !has(actions, Key::Automap));
        mapper.step(Sample{}, Context::Shop, 0.016f);
        assert(mapper.step(with(Button::LeftShoulder), Context::Shop, 0.016f).tab_step == -1);
        mapper.step(Sample{}, Context::Shop, 0.016f);
        assert(mapper.step(with(Button::DpadDown), Context::Shop, 0.016f).snap_y == 1);
        mapper.step(Sample{}, Context::Shop, 0.016f);
        actions = mapper.step(with(Button::East), Context::Shop, 0.016f);   // B leaves the store
        assert(has(actions, Key::Escape) && !actions.rclick_press);
    }
    // A dialog (NPC menu, game menu): B leaves, the d-pad both hops the cursor and steps by key.
    {
        Mapper mapper;
        Actions actions = mapper.step(with(Button::East), Context::Dialog, 0.016f);
        assert(has(actions, Key::Escape) && !actions.rclick_press);
        mapper.step(Sample{}, Context::Dialog, 0.016f);
        actions = mapper.step(with(Button::DpadDown), Context::Dialog, 0.016f);
        assert(actions.snap_y == 1 && has(actions, Key::Down) && !has(actions, Key::Belt2));
    }
    // A menu: the d-pad hops the cursor between buttons, and still sends its arrow.
    {
        Mapper mapper;
        const Actions actions = mapper.step(with(Button::DpadUp), Context::Title, 0.016f);
        assert(actions.snap_y == -1 && has(actions, Key::Up));
    }
    // The right stick's click: the skill picker in the field, a recentred cursor elsewhere.
    {
        Mapper mapper;
        Actions actions = mapper.step(with(Button::RightStick), Context::Game, 0.016f);
        assert(actions.skill_picker && !actions.recentre_cursor);
        mapper.step(Sample{}, Context::Panel, 0.016f);
        actions = mapper.step(with(Button::RightStick), Context::Panel, 0.016f);
        assert(!actions.skill_picker && actions.recentre_cursor);
    }
    // The left trigger swaps weapons in the game, once per pull (hysteresis); B alone is the right-hand skill.
    {
        Mapper mapper;
        Sample pulled; pulled.left_trigger = 1.f;
        Actions actions = mapper.step(pulled, Context::Game, 0.016f);
        assert(has(actions, Key::SwapWeapons) && !actions.rclick_press);
        Sample eased; eased.left_trigger = 0.4f;    // between release (0.3) and press (0.5): still held
        assert(!has(mapper.step(eased, Context::Game, 0.016f), Key::SwapWeapons));
        assert(!has(mapper.step(pulled, Context::Game, 0.016f), Key::SwapWeapons));
        mapper.step(Sample{}, Context::Game, 0.016f);
        assert(has(mapper.step(pulled, Context::Game, 0.016f), Key::SwapWeapons));
        assert(!has(mapper.step(Sample{}, Context::Menu, 0.016f), Key::SwapWeapons));
        assert(mapper.step(with(Button::East), Context::Game, 0.016f).rclick_press);
    }
    // Hops: along the row before off it, never behind or onto the spot the cursor's on.
    {
        const std::vector<Point> grid{ { 0, 0 }, { 29, 0 }, { 58, 0 }, { 20, 29 }, { 0, 29 } };
        assert(snap_pick(0, 0, 1, 0, grid) == 1);               // right: the next in the row, not the nearer-diagonal (20, 29)
        assert(snap_pick(29, 0, 1, 0, grid) == 2);
        assert(snap_pick(58, 0, 1, 0, grid) == -1);             // the row's end: stay
        assert(snap_pick(0, 0, 0, 1, grid) == 4);               // down: straight below
        assert(snap_pick(0, 0, -1, 0, grid) == -1);
        assert(snap_pick(0, 0, 1, 0, {}) == -1);
    }
    // Co-op seats: in the order pads come, four at most; a pad that goes is
    // held for one of its key, which takes its seat back; a minute on, it's
    // free. Two pads of one model each keep their own seat.
    {
        Seats seats;
        assert(seats.connect("pad-a", 10) == 0 && seats.connect("pad-b", 11) == 1 && seats.connect("pad-b", 12) == 2 && seats.connect("pad-d", 13) == 3);
        assert(seats.connect("pad-e", 14) == -1);
        assert(seats.disconnect(12, 1000) == 2 && !seats.present(2) && seats.used(2) && seats.present(1));
        assert(seats.connect("pad-e", 15) == -1);                  // the seat is held for a pad-b
        assert(seats.connect("pad-b", 16) == 2 && seats.present(2) && seats.pad(2) == 16);
        assert(seats.disconnect(11, 5000) == 1);
        assert(seats.expire(5000 + Seats::kHoldMs - 1).empty());
        assert((seats.expire(5000 + Seats::kHoldMs) == std::vector<int>{ 1 }) && !seats.used(1));
        assert(seats.connect("pad-e", 17) == 1 && seats.seat_of(17) == 1);
        // A hand-over: seat 2's pad to seat 0, the colours with the pads.
        assert(seats.colour(0) == 0 && seats.colour(2) == 2);
        seats.swap(0, 2);
        assert(seats.pad(0) == 16 && seats.colour(0) == 2 && seats.pad(2) == 10 && seats.colour(2) == 0);
    }
    // Bindings: the defaults don't clash; d2d.cfg's pad_* round-trips; a bad
    // name or a clash is reported and the default kept.
    {
        assert(clashes(Bindings{}).empty());
        Config config;
        config.bindings[Action::Inventory] = { Button::Back, Bindings::kNone };
        config.bindings[Action::QuestLog] = { Button::North, Bindings::kNone };
        config.swap_sticks = true;
        config.deadzone = 0.2f;
        std::map<std::string, std::string> cfg;
        for (const auto& [key, value] : save_config(config)) cfg[key] = value;
        const auto lookup = [&](const char* key) { const auto found = cfg.find(key); return found == cfg.end() ? nullptr : &found->second; };
        std::vector<std::string> problems;
        assert(load_config(lookup, problems) == config && problems.empty());
        cfg["pad_trade"] = "west,nowhere";
        cfg["pad_deadzone"] = "95";
        const Config partly = load_config(lookup, problems);
        assert(problems.size() == 2 && partly.bindings[Action::Trade] == Bindings{}[Action::Trade] && partly.deadzone == 0.15f);
        problems.clear();
        cfg["pad_trade"] = "west";
        cfg["pad_deadzone"] = "20";
        cfg["pad_inventory"] = "south";                            // Primary's, everywhere
        assert(load_config(lookup, problems).bindings == Bindings{} && problems.size() == 1);
        Bindings dpad;
        dpad[Action::Character] = { Button::DpadUp, Bindings::kNone };   // the d-pad hops in a panel
        assert(clashes(dpad).size() == 2);                          // the hops, and Belt1 in the field
    }
    // A rebound button does its new action, and the old one nothing.
    {
        Config config;
        config.bindings[Action::Inventory] = { Button::Back, Bindings::kNone };
        config.bindings[Action::QuestLog] = { Button::North, Bindings::kNone };
        Mapper mapper{ config };
        assert(has(mapper.step(with(Button::Back), Context::Game, 0.016f), Key::Inventory));
        (void)mapper.step(Sample{}, Context::Game, 0.016f);
        const Actions north = mapper.step(with(Button::North), Context::Game, 0.016f);
        assert(has(north, Key::QuestLog) && !has(north, Key::Inventory));
    }
    // Left-handed: the right stick walks, the left one aims (in the field) and moves the cursor.
    {
        Config config;
        config.swap_sticks = true;
        Mapper mapper{ config };
        Sample sticks; sticks.rx = 1.f; sticks.ly = 1.f;
        const Actions field = mapper.step(sticks, Context::Game, 0.016f);
        assert(field.move_active && field.move_x > 0.99f && field.aim_active && field.aim_y > 0.99f);
        const Actions panel = mapper.step(sticks, Context::Panel, 0.1f);
        assert(panel.cursor_dy > 0 && panel.cursor_dx == 0 && !panel.aim_active);
    }
    // Swapped confirm: east clicks in the menus and south backs out.
    {
        Config config;
        config.swap_confirm = true;
        Mapper mapper{ config };
        assert(mapper.step(with(Button::East), Context::Menu, 0.016f).click_press);
        (void)mapper.step(Sample{}, Context::Menu, 0.016f);
        const Actions south = mapper.step(with(Button::South), Context::Menu, 0.016f);
        assert(has(south, Key::Escape) && !south.click_press);
    }
    return 0;
}