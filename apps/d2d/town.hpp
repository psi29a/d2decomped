// SPDX-License-Identifier: GPL-3.0-or-later
// The town: the in-game character's world state (position, walking,
// panels, NPC menus, store, merc, ...) and one frame of play — input,
// movement, NPCs, then the render.
#pragma once

#include "audio.hpp"
#include "common.hpp"
#include "controls.hpp"
#include "help.hpp"
#include "hireling.hpp"
#include "party.hpp"
#include "gamemenu.hpp"
#include "gamepad_map.hpp"
#include "netgame.hpp"
#include "panels.hpp"
#include "platform.hpp"
#include "roster.hpp"
#include "scene.hpp"
#include "skillbar.hpp"
#include "store.hpp"
#include "ui.hpp"
#include "world_view.hpp"

#include <d2s_items.hpp>
#include <quests.hpp>
#include <overlays.hpp>
#include <rules.hpp>
#include <shrines.hpp>
#include <skills.hpp>
#include <weather.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2d::client {

// The hovered monster's name on its life bar, top centre: a dark red bar
// as wide as the name plus a margin, filled by its share of life left.
// ponytail: D2's own bar (game.exe draws it with the MonsterIndicators
// font and per-type colours) isn't traced; this is its look by eye.
void draw_monster_bar(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const Monster& monster);

// The client's drawing of what the World told it (View): the ground
// items, fires, the merc, pets, missiles and monsters near (cx, cy) as
// units the world draws by depth (npc -2 the merc, -3 pets, -10 - i
// monster i of the View, -1000 - i ground item i).
//
// A unit's states' look (Scene::StateInfo) onto it: the colour shift of
// the one with the highest colorpri, each one's overlays while it lasts,
// its cast overlay once from when the client first saw it (StateClock).
// Overlay lights are stamped by frame_light. These overlay kinds don't
// use LoopWaitTime (FUN_00470c30: only kind 6 waits).
// ponytail: kind 8 rotates the visible state's overlays after 50 ticks
// (FUN_0046def0 / 0046df40); d2d draws the active states together.
struct StateClock {
    struct Seen { std::uint32_t start = 0, last = 0; float x = 0, y = 0; bool off = false; };
    std::map<std::pair<int, const GameData::StateInfo*>, Seen> seen;   // by (unit key, state)
    std::uint32_t now = 0;
    struct Sound { int sound; float x, y; };
    std::vector<Sound> sounds;             // its states' onsound as they come (Town::frame plays them, and the offsounds)
};
inline void dress(const Scene& scene, Unit& unit, int key, std::span<const std::string_view> names, StateClock* clk) {
    int pri = -1;
    for (const auto name : names) {
        const auto found = scene.states.find(std::string(name));
        if (name.empty() || found == scene.states.end()) continue;
        const auto& state = found->second;
        // colorshift counts from 1: "blue" (108) is table 107, blue; 108
        // is purple. "red" (100) → 99 red, "poison" (104) → 103 green.
        if (state.shift >= 1 && state.shift <= 111 && state.pri > pri && scene.colour_shifts.size() >= 111 * 256) {
            pri = state.pri;
            unit.shift = scene.colour_shifts.data() + (state.shift - 1) * 256;
        }
        std::uint32_t start = 0;
        if (clk) {
            auto [seen, fresh] = clk->seen.try_emplace({ key, &state }, StateClock::Seen{ clk->now, clk->now });
            if (!fresh && clk->now - seen->second.last > 200) seen->second.start = clk->now;   // gone a while: a new one
            if (seen->second.start == clk->now && state.on_sound > 0) clk->sounds.push_back({ state.on_sound, unit.x, unit.y });
            seen->second.last = clk->now;
            seen->second.x = unit.x; seen->second.y = unit.y; seen->second.off = false;
            start = seen->second.start;
        }
        for (const auto* overlay : state.over) if (overlay) unit.overs.push_back({ overlay, start, false });
        if (state.cast) unit.overs.push_back({ state.cast, start, true });
    }
}
// The states on a monster and on the player, by States.txt name.
inline std::vector<std::string_view> monster_states(const Scene& scene, const Monster& monster, std::uint32_t now, int player_aura = 0) {
    std::vector<std::string_view> out;
    if (!monster.alive()) return out;
    if (now < monster.poison_until) out.push_back("poison");
    if (now < monster.chill_until) out.push_back("cold");
    if (now < monster.stun_until) out.push_back("stunned");
    for (const auto& effect : { monster.curse, monster.cry })
        if (effect.skill >= 0 && now < effect.until)
            if (const auto* skill = scene.skills.get(effect.skill)) out.push_back(skill->auratarget);
    if (monster.aura > 0)
        if (const auto* skill = scene.skills.get(monster.aura)) out.push_back(skill->aurastate);
    if (monster.in_aura && player_aura > 0)
        if (const auto* skill = scene.skills.get(player_aura)) out.push_back(skill->auratarget);
    out.insert(out.end(), monster.host_states.begin(), monster.host_states.end());
    return out;
}
inline std::vector<std::string_view> player_states(const Scene& scene, const View& view) {
    std::vector<std::string_view> out;
    if (view.poisoned) out.push_back("poison");
    if (view.chilled) out.push_back("cold");
    if (view.stunned) out.push_back("stunned");
    if (const auto* skill = scene.skills.get(view.curse)) out.push_back(skill->auratarget);   // a monster's curse: its auratargetstate
    for (const int buff : view.buffs) if (const auto* skill = scene.skills.get(buff)) out.push_back(skill->aurastate);
    if (view.aura > 0) if (const auto* skill = scene.skills.get(view.aura)) out.push_back(skill->aurastate);
    if (const auto state = d2d::rules::shrine_state(view.boost_code); !state.empty()) out.push_back(state);   // its overlays
    return out;
}
constexpr std::uint32_t kPortalOpenMs = 15 * 40 * 256 / 200;
void view_units(const Scene& scene, const View& view, float camera_x, float camera_y, const std::string* merc_label, std::vector<Unit>& out,
                std::span<const View::Shot> effects = {}, std::uint32_t now_ms = 0, const std::string* corpse_name = nullptr, int cls = 0,
                StateClock* clk = nullptr);
// Over the world: the hovered (else attacked) monster's life bar, the
// death message.
void view_overlays(std::vector<std::uint8_t>& framebuffer, const Scene& scene, const View& view, int hovered);
// An SQ skill's frame now: the mode and when it started, as Fight::seq_view.
std::pair<int, std::uint32_t> view_seq(const Scene& scene, int cls, const View& view, std::uint32_t now_ms);

// The client (docs/design/multiplayer.md): input, panels, camera,
// drawing and sound, over a World (world.hpp) it sends commands to. The
// references below are the World's, for the code that reads them.
// The frame's light (FUN_00475800): the grid round the player at the
// level's own light or the day's, then each light stamped. Positions in
// eighths of a subtile (a cell is 40).
// A light's radius moves toward a new one 8 eighths (a subtile) a frame
// (FUN_004755a0: +0x18 toward +0x1c); a new light starts at its first
// radius (FUN_00474160; an overlay's InitRadius).
// ponytail: light quality is taken as high (2: shadows on).
// `player_at`: where the player stands when it isn't the camera's point (local co-op's camera).
Lighting frame_light(const Scene& scene, const View& view, float cam_x, float cam_y, std::span<const View::Shot> effects = {}, int ambient = -1,
                     std::span<const Unit> units = {}, const Unit* player_look = nullptr, std::uint32_t now = 0,
                     const std::pair<float, float>* player_at = nullptr);

struct Town {
    const Scene* scene = nullptr;
    CharCreateUI& character;                      // the in-game character (save, items, stats)
    World world;                           // the game server's side (in-process: single player)
    const Level* level = nullptr;          // where the character is (the View's): the town, the Blood Moor, the Den of Evil
    d2d::rules::Rng rng{ 0x7f4a7c15u };    // the client's own rolls (which gossip, sound variations)
    Cues cues{ scene };                    // the world's sounds the View brought, due to play
    // The World's state by name, for the devctl verbs (admin and tests, on
    // the server's side); the client itself reads only `view`.
    World::Player& hero = *world.players[0];   // this client's player
    UnitState& player = hero.player;
    std::vector<UnitState>& npc_states = world.npc_states;
    std::optional<Monster>& merc = hero.merc;
    Loot& loot = world.loot;
    Fight& fight = world.fight;
    float& target_x = hero.target_x;
    float& target_y = hero.target_y;
    LocalTransport net;                    // the commands to the World, as their wire form (single player)
    View view;                             // what the World told the client after its last tick
    ViewEncoder view_enc;                  // the host's memory of what this client was sent
    std::unique_ptr<NetGame> net_game;     // --join: a game on a game.exe host (its other players and monsters)
    std::unordered_map<std::uint32_t, Monster> net_monsters;
    struct ShownMode { int mode = -1; std::uint32_t skill_ms = 0, since = 0; };   // since: world ms
    std::unordered_map<std::uint32_t, ShownMode> net_other_modes;   // another player's mode, by unit id   // the host's monsters as d2d draws them, by unit id
    int net_attack = -1, net_attack_skill = 0;               // the host monster being attacked, with what
    int net_warp_sent = -1;                                  // the warp whose 0x13 went to the host
    int net_pick = -1;                                       // the host item being walked to, to pick up
    std::uint32_t net_cursor = 0;                            // the host's id for the item in our hand
    bool net_items_checked = false;                          // the once-a-game item match logged
    std::size_t net_store_shown = 0;                         // the host stock count the store last showed
    std::uint32_t net_waypoint_object = 0, net_waypoint_ms = 0;   // a waypoint we operated: its id, when
    int net_waypoint_level = -1, net_waypoint_tries = 0;                              // ... the level asked for once its menu's open (0x63), -1 none
    std::uint32_t net_drift_ms = 0;                          // since when d2d's player has been far from the host's (0: not)
    std::uint32_t net_follow_ms = 0;                         // when the host was last told where our walk is
    bool inv_close_down = false, char_close_down = false;   // the inventory's / character panel's close button held
    int entry_level = 0;                                     // the level whose "entering" art last started (0: the game's first)
    std::uint32_t entry_until = 0;                           // shown till then (2 s from a level change)
    bool net_clicked_walk = false;                           // this walk went to the host as a click (its target): no follow-ups
    std::uint32_t net_clicked_ms = 0;                        // when, steady ms (World starts the walk a tick later)
    int net_interact_pending = -1;                           // a level NPC whose host unit we wait for before its 0x13
    int net_trade_pending = -1;                              // the host NPC being walked to for a trade
    // Its C->S 0x38: kind 1 trade, 2 gamble, 0 travel (the extra: the handler's
    // level, 0x28 Warriv's east and Meshif's west, 0x4b Meshif's east, 1 Warriv's west).
    int net_trade_kind = 1, net_trade_extra = 0;             // 0 travel, 1 trade, 2 gamble, -1 a talk alone (0x2f, no 0x38)
    bool net_trade_asked = false;                            // its 0x59 + 0x13 went out, NPC info awaited
    int net_operate_type = 2;                                // the host unit being walked to (0x13 once there): 2 object, 0 our corpse
    std::uint32_t net_operate = 0, net_operate_ms = 0;       // ... its id, when
    bool net_operate_moves = false;                          // ... a warp or portal: it takes us to another area
    float net_warp_x = 0, net_warp_y = 0;                    // ... a warp: its mouth (act subtiles), the host's player walked there first
    int net_warp_to = -1;                                    // the level the host's warp takes us to (its 0x15 lands us there)
    auto add_chat(const std::string& text, int colour = 0, std::size_t gold = 0) -> void;
    auto draw_portraits(std::vector<std::uint8_t>& framebuffer, bool left_open) -> void;
    auto draw_coop_portraits(std::vector<std::uint8_t>& framebuffer, bool left_open) -> void;
    static constexpr int kCoopPortraitX = 6, kCoopPortraitWidth = 0x42;   // the column, from the frame's left edge
    struct Said { std::uint16_t string = 0; std::uint32_t until = 0; };
    std::map<std::uint64_t, Said> net_said;                 // the host's overhead messages by unit key (0x26 type 5)
    bool portraits_shown = false;                           // last frame drew party portraits (the chat moves down)
    int merc_portrait_x = -1, merc_portrait_y = 0;          // where last frame drew the merc's (DAT_007beed4 / beed8), -1 none
    std::uint32_t merc_hover_at = 0;                        // since when the cursor's been on it (DAT_007beee8), 0 not
    bool merc_hovered = false;                              // 700 ms on it with nothing in hand: drawn mode 7, its hint shown
    auto event_text(const NetGame::Event& event) const -> std::string;
    auto draw_chat(std::vector<std::uint8_t>& framebuffer, bool left_open, bool right_open) const -> void;
    auto wrap_chat(const std::string& text, int width) const -> std::vector<std::string>;
    std::optional<std::string> chat_typing;                 // a chat message, being typed (Enter)
    std::string typed;                                      // this frame's typed text (SDL text input)
    struct ChatShown { std::string text; std::uint32_t until = 0; int colour = 0; std::size_t gold = 0; };   // gold: that many letters first in gold (a speaker's name)
    std::vector<ChatShown> chat_lines;                      // the top-left chat lines, oldest first
    std::optional<GoldBox> gold_box;                        // the gold box up (store.hpp): drop, offer, deposit or withdraw
    int net_operate_waypoint = -1;                           // ... a waypoint: then 0x49 to this level
    std::uint32_t net_moving_until = 0;                      // such a 0x13 went: no following walks till the host moves us (0x15) or then
    std::uint32_t net_trade_ms = 0, frame_now = 0;           // when it began; walk()'s frame time for send()
    std::unordered_map<std::uint32_t, Loot::GroundItem> net_ground;   // the host's ground items as drawn, labels made once
    int talking_sent = -1;                 // the NPC last reported as talked to (cmd::Chat)
    std::uint32_t net_chat_npc = 0;   // a joined game: the host NPC our talk is open with (0x2f sent, 0x30 due), 0 none
    std::unordered_map<std::uint32_t, std::uint32_t> net_alert_ms;   // a joined game: when the host last ballooned each NPC (0x8a), by its id
    NetGame::NpcQuest net_early_quest;    // the host's quest lines that came before d2d's menu opened
    std::uint32_t net_early_ms = 0;   // when net_early_quest came (world ms)
    bool press_on_ui = false;              // the held left button was pressed on the UI
    std::uint32_t world_ms = 0;            // the World's clock: when it last ticked
    float prev_x = 0, prev_y = 0;          // the player a tick before: the camera slides between the two
    float cam_x = 0, cam_y = 0;            // where the camera is this frame
    float party_x = 0, party_y = 0;        // local co-op: the camera's eased point, toward the party's middle
    std::uint32_t party_ms = 0;            // when it last eased
    // A gamepad playing a hero in the field (no cursor): the left stick walks
    // towards a screen direction (unit length), walking or running as the run
    // toggle says; `attack` / `attack_down` / `alt` are its buttons, which
    // hit what is in reach. The first player's is set each frame by main,
    // the other co-op players' by coop_pad.
    struct PadMove { float x = 0, y = 0; bool active = false, attack = false, attack_down = false, alt = false; bool aim = false; float aim_x = 0, aim_y = 0; };
    struct PadPlayer {
        PadMove move;
        bool walking = false;                     // last frame's stick walk, to send a fresh walk once
        float face_x = 0.707f, face_y = 0.707f;   // the facing the pad last pointed (world, south at first)
        int target = -1;                          // the monster the held button stays on
    };
    PadPlayer pad;                                // the first player's
    using Send = std::function<void(const Command&)>;
    // The pad's walk and its buttons for the hero at `unit` (`field`: no
    // panel or menu is up), the commands out through `out`.
    void pad_walk(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, const Send& out);
    struct PadPick { enum Kind { kNone, kMonster, kItem, kThing } kind = kNone; int id = -1; float x = 0, y = 0; };
    [[nodiscard]] PadPick pad_pick(const PadPlayer& player_pad, const UnitState& unit, bool uses, const pad::Aim& aim = {}) const;
    // The pad's reach for `skill`: a ranged one's (it fires a missile, srvmissile, or its Skills.txt
    // range is "range") locks on across the screen, the nearest any way round unless the stick
    // (`steering`) points it; a swing's is in reach in the facing.
    [[nodiscard]] pad::Aim pad_aim(int skill, bool steering) const;
    bool pad_cue = false;                         // the field plays by pad: the cursor's hover is A's pick (main sets it)
    pad::Family pad_family = pad::Family::Xbox;   // the screen's pad's button labels (main sets it)
    void draw_pad_hint(std::vector<std::uint8_t>& framebuffer) const;
    void pad_act(PadPlayer& player_pad, const UnitState& unit, bool dead, bool field, int left_skill, int right_skill, const Send& out, int left_item = -1, int right_item = -1);   // the items: the hands' charged entries
    // Local co-op's other players at this screen (seats 1..3,
    // docs/design/local-coop.md): their World player (id 0 while their
    // roster is up), pad and orders.
    // ponytail: they play the field only; panels and menus wait for phase 5.
    struct CoopPlayer { int seat = 0, id = 0, colour = 0; PadPlayer pad; pad::Mapper mapper; std::vector<Command> orders; std::optional<Roster> roster; };
    std::vector<CoopPlayer> coop;
    int lead_colour = 0;                          // the first player's seat colour (pad::Seats::colour)
    // A pad took seat `seat` (its colour `colour`): its roster comes up (a held seat's player stays).
    // `pad_now`: the pad as it is, so what it holds (the Start that seated it) isn't pressed in the roster.
    void coop_seat(int seat, int colour, const pad::Sample& pad_now = {});
    // The first player leaves a co-op game: saved, the next joined player
    // leads (World::leave), still played by their own pad; their seat, for
    // that pad to take seat 0, or -1 (no one to lead: the game ends as alone).
    int lead_leaves();
    int handed_over = -1;                         // the seat a hand-over in the game menu gave seat 0 (main swaps the pads, frees the leaver's)
    int partner_left = -1;                        // the seat whose player Save and Exit logged out (main frees it)
    // A seat's player joins with `character` / leaves (saved).
    void coop_join(int seat, const Character& joining);
    void coop_leave(int seat);
    // Typed text for a roster's name (Steam's keyboard on a Deck); whether one wants it.
    void coop_text(std::string_view text, bool backspace);
    [[nodiscard]] bool coop_typing() const;
    // Local co-op with two players in or more: no HUD (orbs, belt, skills,
    // mini-panel), each player's portrait down the left edge instead (draw_coop_portraits).
    [[nodiscard]] bool coop_hud_hidden() const { return !net_game && world.players.size() > 1; }
    // A panel on the screen's left (FUN_00455f20's UIs; Party 0x16 and Hireling 0x24 too).
    [[nodiscard]] bool left_panel_open() const {
        return char_open || stash_open || cube_open || store.npc >= 0 || waypoint.open || quest_log.open || party.open || hireling.open;
    }
    // The keyboard's Enter on that name: the new character is made, as the pad's A.
    void coop_enter();
    // A seat's pad this frame: its field play, as that player's orders.
    void coop_pad(int seat, const pad::Sample& sample, float seconds);
    // A pad's field play for `hero_there` (walk, hit, belt, run, weapons) out
    // through `out`; the keys it pressed that open something (panels, the
    // game menu; Up: the skill picker), which need the screen.
    std::vector<pad::Key> field_play(PadPlayer& player_pad, pad::Mapper& mapper, const World::Player& hero_there, const pad::Sample& sample, float seconds,
                                     const Send& out);
    // Local co-op's screen (docs/design/local-coop.md): the player whose
    // panels, menus, store and character the UI shows. A partner takes it by
    // opening one (or an NPC opening to them) while the first player has
    // nothing open; it's the first player's again once the partner's are shut.
    int focus = 0;                                // that player's id; 0: the first player's
    [[nodiscard]] int focus_id() const;
    [[nodiscard]] World::Player& focused();
    [[nodiscard]] bool focus_free() const;        // the first player's, with nothing open
    [[nodiscard]] int focus_seat() const;         // the seat whose pad plays the UI (0: the first player's)
    [[nodiscard]] int focus_colour() const;       // its colour (the cursor's)
    void take_focus(int id);
    // Local co-op: the fallen body nearest (x, y) within `reach` cells, or null.
    [[nodiscard]] const View::Body* body_near(float x, float y, float reach) const;
    std::vector<SDL_Keycode> focus_keys;          // the keys a partner took the screen with, for this frame's UI
    // The first player's pad while a partner has the screen: field play only.
    void lead_pad(const pad::Sample& sample, float seconds);
    bool coop_choose(int seat, const RosterChoice& choice);
    PadPlayer lead_field;
    pad::Mapper lead_mapper;
    // The pads' settings (d2d.cfg pad_*): every player's mapper plays by them.
    void set_pad_config(const pad::Config& config);
    // The key config (docs/research/re/controls.md) and Configure Controls.
    keys::Table key_table = keys::defaults();
    std::filesystem::path key_dir;                // where <name>.key and default.key live (the saves; empty: not kept)
    ControlsUI controls;
    pad::Button controls_pad_press = pad::Button::Count;   // a pad button just pressed, for the pad page's edit (main sets it)
    bool pad_controls_page = true;                // the "pad_controls" toggle
    bool pad_config_changed = false;              // Accept changed the pad's settings: main's mapper takes them
    void load_keys();
    void say(int phrase);
    void automap_frame();
    std::array<std::pair<std::string, std::string>, 6> automap_saved{};   // the automap options last kept in d2d.cfg
    bool automap_saved_once = false;
    [[nodiscard]] static int say_sound(int cls, int phrase);                         // Say 1..7, Retreat (0..7)
    void clear_screen();
    [[nodiscard]] bool pad_field_or_free() const;
    bool run_held = false;                        // Run's key made the player run
    bool portraits_hidden = false;
    bool hireling_hint = false;                   // FUN_004939b0: armed at a game's start while d2d.cfg's popup_hireling isn't 1
    bool message_log = false;
    HelpScreen help;                              // UI 0x21
    HirelingPanel hireling;                       // UI 0x24
    PartyPanel party;                             // UI 0x16 (a joined game's)
    std::vector<PartyPanel::Row> party_preview;   // devctl `debug party on`: rows to show without a joined game
    [[nodiscard]] std::vector<PartyPanel::Row> party_rows() const;
    [[nodiscard]] PartyPanel::Own party_own() const;
    bool help_opened_mini = false;
    void toggle_help();                     // UI 0x18: only a flag in 1.14d (FUN_00456ee0 draws nothing for it)                // Show Portraits toggled them off
    std::filesystem::path shot_dir;               // Screen Shot's folder (main sets it)
    std::uint32_t last_shot_ms = 0;
    bool screenshot_wanted = false;               // Screen Shot: the next frame drawn goes to a file (main)
    int audio_say = -1;                           // a Say's sound, played by update (it has the audio)
    void save_keys(bool shared) const;
    void update_hotkey_names();
    void accept_controls();
    [[nodiscard]] bool function_held(int action) const;
    pad::Config pad_config;
    std::vector<Command> lead_orders;
    // True when no panel, menu or dialog is up: the pad plays without a cursor.
    [[nodiscard]] bool pad_field() const;
    // An NPC's menu or speech, the game menu or a waypoint: B leaves, the d-pad steps the choices.
    [[nodiscard]] bool pad_dialog() const { return npc_menu.npc >= 0 || speech.npc >= 0 || game_menu.open || waypoint.open; }
    // Where the pad's cursor goes when a menu opens (or its lines change): the choice to start on.
    struct PadFocus { int key = 0, x = 0, y = 0; };
    [[nodiscard]] std::optional<PadFocus> pad_focus() const;
    [[nodiscard]] std::vector<pad::Point> pad_snap_points() const;
    void pad_trade(int mouse_x, int mouse_y);
    // The pad's Secondary (B) is Back with the gold box up, or where the stash
    // or Cube is up (no key toggles them) and the cursor's on no item: true when it's taken.
    [[nodiscard]] bool pad_back(int mouse_x, int mouse_y) const;
    // The skill tooltips' calcs: the character's skill points and their
    // item bonuses, the stats of their gear and passives.
    // ponytail: no auras' or states' stats, no weapon masteries (the World's
    // fight sums those; a joined game has none).
    [[nodiscard]] d2d::rules::CalcEnv tip_env() const;
    void pad_tab(int step);
    void pad_open_picker();
    std::unordered_map<int, Automap> other_automaps;   // other Layers' maps, while elsewhere
    std::uint32_t level_ms = 0;            // when the player entered `level`
    bool have_world = false;
    bool  stash_open = false;
    std::optional<d2d::d2s::Item> held;   // the item on the cursor (the View's)
    int identify_with = -1;               // a right-clicked Scroll / Tome of Identify's id: the next click picks the item
    int imbue_with = -1;                  // Charsi's Imbue chosen (her NPC index): the next item click takes it up and imbues it
    int   stat_pressed = -1;               // char panel stat button held down
    bool  tree_open = false;               // skill tree ('T')
    QuestLog quest_log;                    // the quest log ('Q'), where the character panel goes
    int   tree_tab = 1;                    // 1..3, bottom tab first (0x724bec starts at 1)
    int   skill_pressed = -1;              // skill icon held down
    std::vector<d2d::rules::MercOffer> hire_offers;   // Kashya's list while it's open (the View's)
    std::string merc_label;
    bool  belt_open = false;               // belt popup (` key or a click on the belt)
    bool  cube_open = false;               // right-click the Horadric Cube item
    bool  transmute_down = false;          // its Transmute button held (FUN_0048a190)
    bool  net_cube_open = false;           // a joined game: the host has our Cube open (0x77 0x15; 0x4f 0x17 shuts it)
    NpcMenuState npc_menu;                 // open NPC menu (npc < 0: none)
    Automap automap;                       // Tab
    Store store;                           // an open vendor store (npc < 0: none): the View's stock, the client's tab and buttons
    WaypointUI waypoint;                   // the waypoint panel
    Speech speech;                         // NPC talking (npc < 0: none)
    std::vector<d2d::rules::QuestMsg> npc_quest;   // what the NPC being talked to has on quests
    // The level's random ambient sound (SoundEnviron Day / Night Event,
    // FUN_004e42e0): its sound, the gap to the next and the last one's
    // time, in sound ticks (25 Hz).
    int amb_event = 0;
    std::uint32_t amb_next = 0, amb_last = 0;
    d2d::rules::Rng sound_rng{ 0x5eed5u };
    std::vector<int> gossip_pick;          // per world NPC: chosen gossip topic, -1 = not yet
    SkillBar skillbar{ scene, character };        // the skill buttons, picker and hotkeys (skillbar.hpp)
    int skillbar_set = -1;                        // the weapon set the buttons' skills are for
    // Each local player's charged entries on their hands and hotkeys (the
    // items' ids, -1 none), by Hero::id, while the screen's skillbar is
    // another player's: the save keeps skill ids only (header +0x38..).
    struct HandItems {
        int left = -1, right = -1;
        std::array<int, 16> hotkeys = [] { std::array<int, 16> none{}; none.fill(-1); return none; }();
    };
    std::map<int, HandItems> hand_items;
    void load_hands(int id);                      // the skillbar's items from hand_items[id] (none yet: -1)
    int   hovered_npc = -1;                // Level::npcs index under the cursor (last frame); <= -10: monster -10 - i
    std::uint32_t now_ms = 0;              // this frame's ms (devctl)   // shrines / chests used: when
    bool  player_walked = false;           // `walking` as of the last frame
    bool alt_held = false;                 // devctl `debug alt on`: "Show Items" held
    StateClock state_clock;                // when each unit's states were first seen (their cast overlays)
    std::vector<d2d::rules::OverlayClock> alert_clocks;
    const Level* alert_level = nullptr;     // reset on a level change and on enter (Town is reused between games)
    // The level's objects' modes as last seen (Town::object_sounds), by level->npcs index: "" not seen.
    std::vector<std::string_view> object_modes;
    std::vector<char> object_called;          // an object's call has played (unit +0x80: rules::object_call), by level->npcs index
    const Level* object_level = nullptr;
    std::vector<int> object_loops;          // the objects' loops wanted now (their owners, -1 - index)
    void object_sounds(const Audio& audio, std::uint32_t frame_ms, float sight_x, float sight_y);
    std::uint32_t walk_ms = 0;             // when the player last started or stopped walking (the client's clock)
    bool  player_ran = false;
    bool  inv_open = false;   // 'I' — inventory panel
    bool  char_open = false;  // 'C' — character panel
    bool  stats_down = false, skills_down = false;   // New Stats / New Skills held (DAT_007c02e4 / DAT_007c02e8)
    bool  run_down = false;                          // the run button held (DAT_007befd8)
    GameMenu game_menu;                    // Esc's menu (UI 9)
    MiniPanel mini;                        // the mini-panel (UI 0x15) and the HUD's button for it
    std::filesystem::path cfg_file;        // where the menu's volumes are kept (d2d.cfg; empty: not kept)

    Town(const Scene* game_scene, CharCreateUI& player_character, int start_x = -1, int start_y = -1)
        : scene(game_scene), character(player_character), world(game_scene, start_x, start_y) {
        have_world = world.level && !world.level->dt1s.empty();
        view = world.view();
        level = view.level;
    }

    // What the World tells the client: its View, over as bytes too. The
    // character in it becomes the client's (the panels draw it); the store
    // keeps the client's tab and buttons.
    void publish();
    void send(const Command& command);    // to the World, and to a joined host
    bool net_items(const Command& command);   // a joined game: our item moves and uses to the host; false: not sent (no host id)
    const NetGame::Unit* net_npc(int index) const;   // the host's NPC for d2d's NPC index

    // Into the game with the character the client has (a save loaded, or
    // made): the World takes it.
    void enter();

    // Saving the character: the item on the cursor goes back first, then
    // the World writes it.
    std::string save();

    // devctl: operate object i now, as the server would on arrival.
    void operate(int npc_index, std::uint32_t frame_ms, int force = -1);

    // A fresh game for the character: the Blood Moor's monsters at its
    // difficulty, no loot about.
    void net_overlay();                   // the joined host's units into `view`
    void net_quests(std::uint32_t frame_ms);                    // ... and its NPCs' quest lines and balloons
    void take_npc_quest(int npc, std::vector<d2d::rules::QuestMsg> lines, std::uint32_t frame_ms);   // an NPC's quest lines: a greeting plays, the rest under Talk
    void new_game();

    // The game menu (FUN_0047e090 / FUN_0047e200): opening closes the
    // panels, the automap and the mini-panel; closing reopens those two.
    void open_game_menu();
    void close_game_menu();

    // One InGame frame: keys, panels, clicks, walking, NPCs, then the render.
    // Esc with nothing open opens the game menu; its Exit saves and goes
    // back to the roster (screen).
    void update(std::vector<std::uint8_t>& framebuffer, const Mouse& frame_mouse, const std::vector<SDL_Keycode>& frame_keys,
                Screen& screen, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms);

    // The monster / ground item under the cursor (hovered_npc -10 - i / -1000 - i), or -1.
    [[nodiscard]] int hovered_monster() const;
    [[nodiscard]] int hovered_ground() const;
    // The New Stats / New Skills buttons as the panels stand.
    [[nodiscard]] LevelButtons level_buttons_now() const;
    // The quest log's inputs: this difficulty's flags, what the server told.
    [[nodiscard]] const d2d::rules::QuestBits& quest_bits() const;
    [[nodiscard]] QuestState quest_state() const { return { view.quest_log, view.game_quests, view.den_left, net_game != nullptr }; }
    int view_act() const;
    void toggle_quest_log();               // Q and the mini-panel (FUN_004a3fe0)
    [[nodiscard]] QuestLogButton quest_log_button_now() const;

    // The client's side of a click: what it asks the server for
    // (protocol.hpp). A held left button re-aims the walk; a press picks
    // what's under the cursor: a monster to attack, an item, an object or
    // NPC, else the ground. The right button uses the right skill there.
    [[nodiscard]] std::vector<Command> input(const Mouse& mouse, bool over_ui) const;

    // The game this frame: what the player asks for (input), the World's
    // step, then what it told the client (events).
    void walk(const Mouse& mouse, bool over_ui, Audio& audio, std::uint32_t frame_ms, std::uint32_t last_ms);

    // What the World told the client.
    void handle(const Event& event, Audio& audio, std::uint32_t frame_ms);
    int menu_after_speech = -1;                    // the NPC whose menu opens once its quest speech ends
    bool questdone_sound = false;                  // the quest log's done animation began (draw → update)
    bool quest_log_was_open = false;               // UI 0xf last frame: shutting it marks the animations seen (FUN_004a28d0)
    int replay_speech = 0;                         // questlast: a quest message to play again
    d2d::rules::Rain rain;                         // the weather (its state lasts the session, like game.exe's)
    auto rain_join() -> void;
    std::uint32_t thunder_at = 0;                  // the thunder queued by a strike (FUN_004b9a00's delay)
    int thunder_x = 0, thunder_y = 0;
    bool thunder_due = false;
    std::uint32_t rain_ms = 0;
    float rain_cam_x = 0, rain_cam_y = 0;          // the camera at the last weather tick
    int rain_vol = 0;                              // the rain sound's volume 0..255
    // The Den of Evil cleared (docs/research/re/quests.md "The Den lights
    // up"): S→C 0x2d event 0 starts a count (FUN_0046b0c0); for 30 frames
    // the Den's ambient falls from 80 (FUN_0046bd50), then its rooms get
    // light beams, missile denofevillight, three to a room at free spots
    // (FUN_0046b0d0 / FUN_0046af70), and keep them (client func 23).
    int den_flash = -1;
    bool den_seen = false, den_lit = false;
    std::vector<View::Shot> den_beams;
    [[nodiscard]] int den_ambient() const;
    void den_tick(std::uint32_t frame_ms);
    // The client's critters (docs/research/re/critters.md): the level's
    // Levels.txt cmon / cpct / camt, made per room on its seed as the room
    // comes up (FUN_0046c460 / FUN_0046c1a0), thinking a client frame at a
    // time (FUN_0046d780). The host never hears of them.
    struct Critter {
        int row = -1;                              // MonStats row
        float x = 0, y = 0, goal_x = 0, goal_y = 0;   // cells
        int dir = 0;
        bool walking = false, marked = false;      // marked (+0x34): a bird that's taken off, a rat resting at a corpse
        int wait = 0, after = 0;                   // frames to its next think; to wait once its walk is done
        std::uint32_t mode_ms = 0;
        d2d::rules::Rng seed;                      // its unit seed (+0x20)
    };
    std::vector<Critter> critters;
    int critters_level = -1;
    std::vector<bool> critter_rooms;               // by Level::rooms index: its critters made
    void critter_tick(std::uint32_t frame_ms);
    // NPC `npc`'s menu, placed by its feet on screen as render_world
    // projects them.
    void open_menu(int npc);

    // The frame: the world with its units, the open panels, the tree and
    // the waypoint panel.
    void draw(std::vector<std::uint8_t>& framebuffer, const Mouse& mouse, std::uint32_t frame_ms);
    // draw's body; `wide` (widescreen) is where the world draws, `framebuffer` the 800x600 UI.
    void draw_view(std::vector<std::uint8_t>& framebuffer, std::vector<std::uint8_t>* wide, const Mouse& mouse, std::uint32_t frame_ms);
    std::vector<std::uint8_t> ui_buffer;   // the widescreen UI's 800x600
};

}  // namespace d2d::client
